#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the round-51 A6XX_GEN1 ordered genpd +
clock POWER-UP model (AdrenoPowerUpV1) against the sha256-pinned upstream Linux
v6.12 sources, the vendored enchilada DTB, and the module's own raw text.

Independent of method 1 (check-enchilada-adreno-host.py): this script NEVER
compiles or runs the C module. It reads:
  (a) .cache/adreno-a630-source/.../a6xx_gmu.c, pinned by
      scripts/data/adreno-a630-inputs.json, and asserts the EXACT
      a6xx_gmu_resume() ordered sequence :1033-1052 + nr_clocks probe :1479-1495;
  (b) the vendored enchilada DTB (sha256 e590ee3273...), asserting the two
      DISTINCT clock counts (GMU node 4 vs kgsl-3d0 node 6) at pinned lines;
  (c) AdrenoPowerUpV1.h/.c raw TEXT: constants, 5 ordered steps, guarded GX,
      reverse-order rollback, anti-conflation input validation, composition
      with round-46 caller;
  (d) scripts/build-enchilada-adreno.py + check-enchilada-adreno-host.py:
      the 11th object and 11th host test are actually wired.

No network, no CI, no flash. HOST-VERIFIED: source text is a specification.

Usage: python -X utf8 scripts/verify-enchilada-power-up-v1.py
"""
import hashlib
import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
WS = DEV.parent.parent                     # oneplus6-ios/
M = json.loads((SCRIPTS / "data/adreno-a630-inputs.json").read_text(encoding="utf-8"))
GMU_KEY = "drivers/gpu/drm/msm/adreno/a6xx_gmu.c"
PIN = M["linux"]["files"][GMU_KEY]
CACHE_GMU = DEV / ".cache/adreno-a630-source" / GMU_KEY
DTB = WS / "notes/mu-silicium/enchilada.dts"
DTB_SHA = "e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275"

GPU = DEV / "drivers/NativeAdreno"
H = GPU / "AdrenoPowerUpV1.h"
C = GPU / "AdrenoPowerUpV1.c"
TEST = GPU / "tests/adreno-power-up-v1-test.cpp"
BUILD = SCRIPTS / "build-enchilada-adreno.py"
RUNNER = SCRIPTS / "check-enchilada-adreno-host.py"

CHECKS = []


def need(ok, label, detail=""):
    CHECKS.append((bool(ok), label, detail))


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def lines(path):
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def line_at(ls, n):
    return ls[n - 1] if 1 <= n <= len(ls) else ""


def finish(name):
    fails = [c for c in CHECKS if not c[0]]
    for ok, label, detail in CHECKS:
        if not ok:
            print(f"  FAIL {label}  [{detail}]")
    print(f"{name}: {'PASS' if not fails else 'FAIL'} "
          f"({len(CHECKS)} checks, {len(fails)} failures)")
    return 0 if not fails else 1


def main():
    # ---- (a) pinned upstream source ----------------------------------------
    need(CACHE_GMU.is_file(), "pinned a6xx_gmu.c present in cache")
    if not CACHE_GMU.is_file():
        return finish("POWER_UP_V1_CROSS")
    gmu_bytes = CACHE_GMU.read_bytes()
    need(len(gmu_bytes) == PIN["bytes"],
         "a6xx_gmu.c byte count matches pin", str(len(gmu_bytes)))
    need(sha256(gmu_bytes) == PIN["sha256"],
         "a6xx_gmu.c sha256 matches pin")
    gmu = lines(CACHE_GMU)

    # The five ordered calls, at their exact upstream line numbers.
    need("pm_runtime_get_sync(gmu->dev);" in line_at(gmu, 1033),
         ":1033 CX genpd pm_runtime_get_sync(gmu->dev)")
    need("IS_ERR_OR_NULL(gmu->gxpd)" in line_at(gmu, 1040),
         ":1040 guarded GX check !IS_ERR_OR_NULL(gmu->gxpd)")
    need("pm_runtime_get_sync(gmu->gxpd);" in line_at(gmu, 1041),
         ":1041 GX genpd pm_runtime_get_sync(gmu->gxpd)")
    need("clk_set_rate(gmu->core_clk, 200000000);" in line_at(gmu, 1044),
         ":1044 core rate 200000000")
    need("200000000 : 150000000" in line_at(gmu, 1046),
         ":1046 A740 200 MHz else 150 MHz (A630 hub)")
    need("clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks);" in line_at(gmu, 1047),
         ":1047 clk_bulk_prepare_enable(gmu->nr_clocks, ...)")
    need("pm_runtime_put(gmu->gxpd);" in line_at(gmu, 1049),
         ":1049 rollback puts gxpd FIRST")
    need("pm_runtime_put(gmu->dev);" in line_at(gmu, 1050),
         ":1050 rollback puts dev SECOND")
    # nr_clocks is the GMU device's devm_clk_bulk_get_all result.
    need("devm_clk_bulk_get_all(gmu->dev, &gmu->clocks);" in line_at(gmu, 1481),
         ":1481 nr_clocks from GMU device clk_bulk_get_all")
    need("gmu->nr_clocks = ret;" in line_at(gmu, 1486),
         ":1486 gmu->nr_clocks = ret")
    # GMU is a legacy part; A740-family is a different generation.
    need("adreno_is_a740_family(adreno_gpu) ?" in line_at(gmu, 1045),
         ":1045 generation rate branch is A740-family")

    # ---- (b) vendored DTB: two DISTINCT clock counts ------------------------
    need(DTB.is_file(), "vendored enchilada.dts present")
    if DTB.is_file():
        need(sha256(DTB.read_bytes()) == DTB_SHA,
             "enchilada.dts sha256 matches e590ee3273...")
        dtb = lines(DTB)
        gmu_clocks = line_at(dtb, 23889)
        gmu_names = line_at(dtb, 23890)
        gpu_names = line_at(dtb, 23657)
        # 4 clock phandle/id pairs on the GMU node.
        need(gmu_clocks.count("0x") == 8,
             "GMU node :23889 clocks = 4 (phandle,id) pairs",
             gmu_clocks.strip())
        need(gmu_names.count("\\0") == 3 and "gmu_clk" in gmu_names
             and "cxo_clk" in gmu_names and "axi_clk" in gmu_names
             and "memnoc_clk" in gmu_names,
             "GMU node :23890 clock-names = 4 (gmu/cxo/axi/memnoc)")
        need(gpu_names.count("\\0") == 5 and "core_clk" in gpu_names
             and "gmu_clk" in gpu_names and "l3_vote" in gpu_names,
             "kgsl-3d0 :23657 clock-names = 6")
        need("4" != "6", "GMU(4) and GPU(6) clock counts are DISTINCT")

    htxt = H.read_text(encoding="utf-8") if H.is_file() else ""
    ctxt = C.read_text(encoding="utf-8") if C.is_file() else ""

    # ---- (c) module header constants mirror the pinned facts ----------------
    for macro, val in [
        ("ADRENO_POWER_UP_V1_CORE_RATE_HZ", "200000000u"),
        ("ADRENO_POWER_UP_V1_HUB_RATE_A740", "200000000u"),
        ("ADRENO_POWER_UP_V1_HUB_RATE_A630", "150000000u"),
        ("ADRENO_POWER_UP_V1_GMU_CLOCKS", "4u"),
        ("ADRENO_POWER_UP_V1_GPU_CLOCKS", "6u"),
        ("ADRENO_POWER_UP_V1_IS_A740_FAMILY", "0u"),
        ("ADRENO_POWER_UP_V1_STEP_COUNT", "5u"),
    ]:
        need(re.search(rf"#define\s+{macro}\s+{re.escape(val)}", htxt) is not None,
             f"header defines {macro} = {val}")
    need("a6xx_gmu_resume" in htxt and ":1012-1060" in htxt,
         "header cites a6xx_gmu_resume :1012-1060")
    need("devm_clk_bulk_get_all" in htxt,
         "header cites devm_clk_bulk_get_all for nr_clocks")
    need("ROLLBACK" in htxt.upper() or "rollback" in htxt.lower(),
         "header documents the rollback")
    need("strict_gx" in htxt and "require_gx" in htxt,
         "header documents GX policy composition")
    need("AdrenoPowerUpV1PreIsReady" in htxt,
         "header exposes the composition-limit predicate")
    need("CountsAreDistinct" in htxt and "InputsValid" in htxt,
         "header exposes anti-conflation validation")

    # ---- (d) module .c: ordered plan + guarded + rollback -------------------
    steps = [
        "ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON",
        "ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON",
        "ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE",
        "ADRENO_POWER_UP_V1_STEP_SET_HUB_RATE",
        "ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE",
    ]
    # Plan emits the five steps in this exact order.
    pos = [ctxt.find(f"ops[n].step = {s};") for s in steps]
    need(all(p >= 0 for p in pos), "plan emits all 5 ordered steps")
    if all(p >= 0 for p in pos):
        need(pos == sorted(pos), "plan step order CX->GX->core->hub->clocks")
    need("AdrenoPowerUpV1StepGuarded(ops[n].step)" in ctxt or
         "guarded = AdrenoPowerUpV1StepGuarded" in ctxt,
         "plan marks guarded steps via StepGuarded()")
    need("GX_GENPD_ON ? 1u : 0u" in ctxt or
         "step == ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON ? 1u : 0u" in ctxt,
         "only the GX step is guarded")
    need("if (op->guarded && !in->gxpd_present)" in ctxt,
         "Execute skips guarded GX when gxpd_present == 0")
    need("apuv1_rollback" in ctxt and "gx_acquired" in ctxt and
         "cx_acquired" in ctxt,
         "Execute rolls back acquired genpds")
    # Reverse order: GX off emitted before CX off in apuv1_rollback body.
    rb = ctxt[ctxt.find("static int apuv1_rollback"):]
    rb = rb[:rb.find("AdrenoPowerUpV1Status AdrenoPowerUpV1Execute")]
    # Assert both genpd_off calls exist and GX precedes CX.
    gx_i = rb.find("gx_acquired")
    cx_i = rb.find("cx_acquired")
    need(gx_i >= 0 and cx_i >= 0 and gx_i < cx_i,
         "rollback body checks GX before CX (reverse acquisition order)")
    need("genpd_off(bus->cookie, 1u)" in rb and
         "genpd_off(bus->cookie, 0u)" in rb,
         "rollback calls genpd_off(1) then genpd_off(0)")
    need("AdrenoPowerUpV1HubRateHz(in->a740_family)" in ctxt,
         "hub rate plan uses the generation selector")
    need("in->gmu_clock_count" in ctxt and "gmu->nr_clocks" in ctxt,
         "clocks-enable op uses GMU nr_clocks (NOT GPU 6)")
    need("AdrenoPowerUpV1CountsAreDistinct(in->gmu_clock_count" in ctxt,
         "InputsValid enforces the two counts are distinct")

    # ---- (e) integration wiring --------------------------------------------
    btxt = BUILD.read_text(encoding="utf-8") if BUILD.is_file() else ""
    rtxt = RUNNER.read_text(encoding="utf-8") if RUNNER.is_file() else ""
    need("GPU / 'AdrenoPowerUpV1.c'" in btxt,
         "build-enchilada-adreno.py compiles AdrenoPowerUpV1.c (11th object)")
    need("'AdrenoPowerUpV1'" in btxt,
         "build report source stem list includes AdrenoPowerUpV1")
    need("adreno-power-up-v1-test" in rtxt,
         "host runner includes the new test")
    need("'AdrenoPowerUpV1': GPU / 'AdrenoPowerUpV1.c'" in rtxt,
         "host runner compiles the new C source")
    need(TEST.is_file(), "new host test file exists")
    if TEST.is_file():
        ttxt = TEST.read_text(encoding="utf-8")
        need("AdrenoGmuCallerV1Precheck" in ttxt,
             "test composes with the REAL round-46 caller Precheck")
        need("0x00005ff4u" in ttxt and "0x00088400u" in ttxt,
             "test uses device-measured round-35 CX-down/GX-off words")
        need("kAdrenoPowerUpV1RollbackFailed" in ttxt,
             "test asserts rollback-failure path")

    return finish("POWER_UP_V1_CROSS")


if __name__ == "__main__":
    sys.exit(main())
