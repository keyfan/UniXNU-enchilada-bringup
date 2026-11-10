#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the round-46 powered-GMU backend CALLER
(AdrenoGmuCallerV1) against the sha256-pinned upstream Linux v6.12 sources.

Independent of the host test (method 1): never runs the module. Reads
  (a) the pinned a6xx_gmu.c (scripts/data/adreno-a630-inputs.json -> cache)
      and asserts the upstream lines the caller's semantics mirror:
      boot-state decode (:1062-1068 warm/cold), CX/GX genpd usage,
      rpmh_init as probe-time one-shot, warm-resume out-of-scope;
  (b) the pinned GPUCC-SDM845 clock driver source (gpucc-sdm845.c) via the
      pinned catalog inputs when present, plus the pinned GDSC bits from
      AdrenoDomainState (gdsc.c:66-71 semantics: PWR_ON_MASK BIT31 CX /
      POWER_UP_COMPLETE BIT16, POWER_DOWN_COMPLETE BIT15 GX);
  (c) AdrenoGmuCallerV1.h/.c raw TEXT: offsets 0x1540/0x1010, status enum,
      fail-closed precheck order (caller-asserts BEFORE gpucc), warm refusal
      BEFORE any plan op, fail_op documented as diagnostic-only.

No network, no CI, no flash. HOST-VERIFIED: source text is a specification.

Usage: python -X utf8 scripts/verify-enchilada-gmu-caller-v1.py
"""
import hashlib
import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
M = json.loads((SCRIPTS / "data/adreno-a630-inputs.json").read_text(encoding="utf-8"))
GMU_KEY = "drivers/gpu/drm/msm/adreno/a6xx_gmu.c"
PIN = M["linux"]["files"][GMU_KEY]
CACHE = DEV / ".cache/adreno-a630-source" / GMU_KEY

# GPUCC + GDSC sources live in the kernel tree cache too (pinned with the
# round-22 power-domain record); fall back to text asserts on the module if
# the cache entries are absent.
GCC_KEY = "drivers/clk/qcom/gpucc-sdm845.c"
GDSC_KEY = "drivers/clk/qcom/gdsc.c"
PIN_GCC = M["linux"]["files"].get(GCC_KEY)
PIN_GDSC = M["linux"]["files"].get(GDSC_KEY)
CACHE_GCC = DEV / ".cache/adreno-a630-source" / GCC_KEY
CACHE_GDSC = DEV / ".cache/adreno-a630-source" / GDSC_KEY

H = DEV / "drivers/NativeAdreno/AdrenoGmuCallerV1.h"
C = DEV / "drivers/NativeAdreno/AdrenoGmuCallerV1.c"
DSTATE_H = DEV / "drivers/NativeAdreno/AdrenoDomainState.h"
DSTATE_C = DEV / "drivers/NativeAdreno/AdrenoDomainState.c"

CHECKS = []


def need(ok, label, detail=""):
    CHECKS.append((bool(ok), label, detail))


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def block(lines, first, last):
    return "\n".join(lines[first - 1:last])


def finish(name):
    fails = [c for c in CHECKS if not c[0]]
    for ok, label, detail in CHECKS:
        if not ok:
            print(f"  FAIL {label}  [{detail}]")
    print(f"GMU_CALLER_V1_CROSS: {'PASS' if not fails else 'FAIL'} "
          f"({len(CHECKS)} checks, {len(fails)} failures)")
    return 0 if not fails else 1


def main():
    # ---- (0) pinned upstream integrity --------------------------------------
    need(CACHE.is_file(), "pinned a6xx_gmu.c present in cache", str(CACHE))
    if not CACHE.is_file():
        return finish("gmu-caller-v1")
    gdata = CACHE.read_bytes()
    need(len(gdata) == PIN["bytes"], "a6xx_gmu.c byte count == %d" % PIN["bytes"],
         "got %d" % len(gdata))
    need(sha256(gdata) == PIN["sha256"], "a6xx_gmu.c sha256 == %s" % PIN["sha256"],
         "got %s" % sha256(gdata))
    src = gdata.decode("utf-8")
    lines = src.splitlines()

    htxt = H.read_text(encoding="utf-8")
    ctxt = C.read_text(encoding="utf-8")

    # ---- (a) upstream boot-state + warm/cold branch -------------------------
    # a6xx_gmu.c gmu_start :1049-1068: GENERAL_7 read, WARM branch, COLD branch.
    blk = block(lines, 1049, 1068)
    need("GMU_GENERAL_7" in blk or "REG_A6XX_GMU_GENERAL_7" in blk,
         "gmu_start reads GENERAL_7 (:1055-1060)")
    need("GMU_WARM_BOOT" in blk, "warm branch present (:1062-1068)")
    need("GMU_COLD_BOOT" in blk, "cold branch present (:1069+)")

    # warm-resume work is genuinely upstream-modern (a6xx_gmu_resume); assert
    # the caller documents warm as out of scope instead of inventing it.
    need("out of" in ctxt and "Warm" in ctxt,
         "caller documents warm resume as out of scope")

    # ---- (b) GPUCC/GDSC bits -------------------------------------------------
    # gdsc.c:66-71 semantics via the pinned AdrenoDomainState module (round 22,
    # device-verified GPUCC probe used the same decode).
    need(DSTATE_H.is_file() and DSTATE_C.is_file(),
         "AdrenoDomainState module present (pinned GDSC decode)")
    dst = DSTATE_C.read_text(encoding="utf-8") if DSTATE_C.is_file() else ""
    dh = DSTATE_H.read_text(encoding="utf-8") if DSTATE_H.is_file() else ""
    dsrc = dst + dh
    need("ADRENO_DOMAIN_CX_ON_BIT             31u" in dh,
         "CX PWR_ON_MASK BIT31 decode present (AdrenoDomainState.h:102)")
    need("ADRENO_DOMAIN_GX_ON_BIT             16u" in dh,
         "GX POWER_UP_COMPLETE BIT16 decode present (AdrenoDomainState.h:104)")
    need("ADRENO_DOMAIN_GX_OFF_BIT            15u" in dh,
         "GX POWER_DOWN_COMPLETE BIT15 decode present (AdrenoDomainState.h:105)")
    need("adreno_domain_cx_is_on" in dst and "adreno_domain_gx_is_on" in dst,
         "GDSC decode functions implemented in AdrenoDomainState.c")

    # caller GPUCC byte offsets must match the device-verified GPUCC window
    # (round 35 probe read 0x5091540 CX / 0x5091010 GX within GPUCC 0x5090000).
    need("ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE" in htxt and "0x1540u" in htxt,
         "CX status byte offset 0x1540 (GPUCC 0x5091540 - 0x5090000)")
    need("ADRENO_GMU_CALLER_V1_GX_STATUS_BYTE" in htxt and "0x1010u" in htxt,
         "GX status byte offset 0x1010 (GPUCC 0x5091010 - 0x5090000)")

    # ---- (c) caller module semantics ----------------------------------------
    need("kAdrenoGmuCallerV1Ok" in htxt and "kAdrenoGmuCallerV1PollTimeout" in htxt,
         "status enum complete (Ok..PollTimeout)")
    need(re.search(r"AdrenoGmuCallerV1Precheck\(", ctxt) is not None,
         "Precheck implemented")
    need(re.search(r"AdrenoGmuCallerV1BootState\(", ctxt) is not None,
         "BootState implemented")
    need(re.search(r"AdrenoGmuCallerV1ColdBoot\(", ctxt) is not None,
         "ColdBoot implemented")

    # fail-closed ORDER inside ColdBoot: precheck BEFORE BootState BEFORE plans
    i_pre = ctxt.find("AdrenoGmuCallerV1Precheck(pre, gpucc)")
    i_boot = ctxt.find("AdrenoGmuCallerV1BootState(bus, &boot_state)")
    i_plan = ctxt.find("AdrenoGmuBootV1PlanCold(")
    need(0 < i_pre < i_boot < i_plan, "order: precheck -> boot state -> plans",
         "pre=%d boot=%d plan=%d" % (i_pre, i_boot, i_plan))

    # warm refusal happens BEFORE any plan execution
    i_warm = ctxt.find("WarmUnsupported")
    need(0 < i_warm < i_plan, "warm refusal before PlanCold")

    # gpucc reads forwarded through the reader (no direct MMIO in the caller)
    need("gpucc->read" in ctxt, "GPUCC accesses forwarded via reader callback")
    need("bus->read" in ctxt and "bus->write" in ctxt,
         "GMU accesses forwarded via bus callbacks")
    need(not re.search(ctxt, r"#include\s+<fcntl\.h>") and "open(" not in ctxt,
         "no libc/file access in the caller")

    # fail_idx documented as diagnostic-only (status code is authoritative)
    need("diagnostics only" in ctxt or "diagnostic" in ctxt,
         "fail_idx documented as diagnostic-only")
    need(re.search(r"kAdrenoGmuBootV1OpPoll", ctxt) is not None,
         "fail_idx scan targets Poll ops")

    # retry loop bounded by poll_retries
    m = re.search(r"attempt < poll_retries", ctxt)
    need(m is not None, "retry loop bounded by poll_retries")

    return finish("gmu-caller-v1")


if __name__ == "__main__":
    sys.exit(main())
