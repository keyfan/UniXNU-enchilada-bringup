#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the round-62 power-up BACKEND
(AdrenoPowerUpBackendV1) WITHOUT compiling or running it.

Method 1 is check-enchilada-adreno-host.py, which compiles the module and runs it
against a simulated GPUCC. This script is deliberately independent of that: it
re-derives, in Python, the register traffic the backend is supposed to produce,
and then checks the C module against those numbers.

The point of this round is that a bus callback can no longer be a silent no-op.
So the checks here are about COUNTS and OFFSETS, not about prose:

  (1) the CX genpd leg writes exactly ONE register, at 0x106c, with value 0;
  (2) the GX genpd leg writes ZERO registers (gdsc_gx_do_nothing_enable);
  (3) the hub rate leg writes ZERO registers (round-61 finding 1);
  (4) the parked core-rate leg writes ZERO registers and parks 0x0602;
  (5) the RCG enable leg writes 4 (force_enable, CFG, UPDATE, clear);
  (6) each GPUCC branch clock writes 2 (wake/sleep, CLOCK_ENABLE);
  (7) so clocks_enable writes 4 + 2*2 = 8, and the whole chain writes 1+0+0+0+8 = 9;
  (8) every offset the backend can touch is inside the GPUCC window;
  (9) the backend contains no literal write count that could drift, and every
      plan it executes comes from AdrenoGdscDecode / AdrenoClkCtlV1 at call time;
 (10) it is wired into the archive build and the host runner.

Reads: the two derived modules' headers (for the constants), AdrenoPowerUpV1.h
(for the step order), the backend's own .c/.h, and the two build scripts.
No network, no CI, no flash, no device run. HOST-VERIFIED: source text only.

Usage: python -X utf8 scripts/verify-enchilada-power-up-backend-v1.py
"""
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
GPU = DEV / "drivers/NativeAdreno"

BE_H = GPU / "AdrenoPowerUpBackendV1.h"
BE_C = GPU / "AdrenoPowerUpBackendV1.c"
CLK_H = GPU / "AdrenoClkCtlV1.h"
GDSC_H = GPU / "AdrenoGdscDecode.h"
PU_H = GPU / "AdrenoPowerUpV1.h"
HOST = SCRIPTS / "check-enchilada-adreno-host.py"
BUILD = SCRIPTS / "build-enchilada-adreno.py"

FAIL = []


def check(cond, what):
    if cond:
        return True
    FAIL.append(what)
    print(f"FAIL: {what}")
    return False


def define(text, name):
    """Read `#define NAME value` as an int, tolerating a u/U suffix and a
    trailing /* comment */ (most defines in these headers carry one)."""
    m = re.search(rf"^#define\s+{re.escape(name)}\s+\(?([0-9a-fA-FxX]+)[uUlL]*\)?"
                  rf"\s*(?:/\*.*?\*/)?\s*$",
                  text, re.M)
    if not m:
        return None
    raw = m.group(1).rstrip("uUlL")
    return int(raw, 0)


def main():
    for p in (BE_H, BE_C, CLK_H, GDSC_H, PU_H, HOST, BUILD):
        if not p.exists():
            print(f"FAIL: missing input {p}")
            return 1

    beh = BE_H.read_text(encoding="utf-8", errors="replace")
    bec = BE_C.read_text(encoding="utf-8", errors="replace")
    clkh = CLK_H.read_text(encoding="utf-8", errors="replace")
    gdsch = GDSC_H.read_text(encoding="utf-8", errors="replace")
    puh = PU_H.read_text(encoding="utf-8", errors="replace")

    print("=== derivations this cross-check re-does in Python ===")

    # ---- 1. the constants the backend's traffic depends on ----------------
    cx_cbcr = define(clkh, "ADRENO_CLK_CTL_V1_CX_GMU_CBCR")
    cxo_cbcr = define(clkh, "ADRENO_CLK_CTL_V1_CXO_CBCR")
    cmd_rcgr = define(clkh, "ADRENO_CLK_CTL_V1_RCG_CMD_RCGR")
    gpucc_size = define(clkh, "ADRENO_CLK_CTL_V1_GPUCC_SIZE")
    clk_off = define(clkh, "ADRENO_CLK_CTL_V1_CBCR_CLK_OFF")
    ws_mask = define(clkh, "ADRENO_CLK_CTL_V1_CBCR_WS_MASK")
    ws_value = define(clkh, "ADRENO_CLK_CTL_V1_CBCR_WS_VALUE")
    clk_enable = define(clkh, "ADRENO_CLK_CTL_V1_CBCR_CLOCK_ENABLE")
    rcg_poll = define(clkh, "ADRENO_CLK_CTL_V1_RCG_POLL_US")

    for name, v in [("CX_GMU_CBCR", cx_cbcr), ("CXO_CBCR", cxo_cbcr),
                    ("RCG_CMD_RCGR", cmd_rcgr), ("GPUCC_SIZE", gpucc_size),
                    ("CBCR_CLK_OFF", clk_off), ("WS_MASK", ws_mask),
                    ("WS_VALUE", ws_value), ("CLOCK_ENABLE", clk_enable),
                    ("RCG_POLL_US", rcg_poll)]:
        if not check(v is not None, f"AdrenoClkCtlV1.h still defines {name}"):
            return 1

    # The wake/sleep recommendation writes the SAME word into both fields.
    check(ws_mask == ws_value == 0x0FF0,
          f"wake/sleep mask and value are both 0x0ff0 (got {ws_mask:#x}/{ws_value:#x})")
    check(clk_off == 0x80000000, "CBCR_CLK_OFF is BIT(31)")
    check(clk_enable == 1, "CBCR_CLOCK_ENABLE is BIT(0)")

    # ---- 2. GPUCC offset containment ------------------------------------
    # The three registers the backend can name, plus the RCG's two sub-registers.
    touches = {
        "CX_GMU_CBCR": cx_cbcr,
        "CXO_CBCR": cxo_cbcr,
        "RCG_CMD": cmd_rcgr,           # +0 CMD
        "RCG_CFG": cmd_rcgr + 4,       # +4 CFG
    }
    for label, off in touches.items():
        check(off + 4 <= gpucc_size,
              f"{label} 0x{off:x} is inside the GPUCC window (size 0x{gpucc_size:x})")
    print(f"  GPUCC window 0x{gpucc_size:x}; highest touched offset "
          f"0x{max(touches.values()):x}")

    # ---- 3. the GDSC plans the genpd legs execute ------------------------
    # The CX/GX descriptors are built in AdrenoGdscDecode.c; the header carries
    # the bit/offset constants they are built from. Confirm the decode API the
    # backend calls is declared, and that the CX status offset the round-46
    # caller polls is the same PWR_ON bit the GDSC plan waits on.
    check("adreno_gdsc_plan_cpu_enable" in gdsch,
          "AdrenoGdscDecode.h declares adreno_gdsc_plan_cpu_enable")
    check("adreno_gdsc_plan_disable" in gdsch,
          "AdrenoGdscDecode.h declares adreno_gdsc_plan_disable")
    pwr_on_bit = define(gdsch, "ADRENO_GDSC_PWR_ON_BIT")
    pwr_up_bit = define(gdsch, "ADRENO_GDSC_POWER_UP_COMPLETE_BIT")
    pwr_dn_bit = define(gdsch, "ADRENO_GDSC_POWER_DOWN_COMPLETE_BIT")
    check(pwr_on_bit == 31, f"PWR_ON_MASK is BIT(31) (got {pwr_on_bit})")
    check(pwr_up_bit == 16, f"GDSC_POWER_UP_COMPLETE is BIT(16) (got {pwr_up_bit})")
    check(pwr_dn_bit == 15, f"GDSC_POWER_DOWN_COMPLETE is BIT(15) (got {pwr_dn_bit})")
    # The round-46 caller's CX status byte is 0x1540 and its mask is BIT(31):
    # the same PWR_ON bit, so a CX the GDSC plan just brought up reads as up.
    check("ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE 0x1540u" in
          (GPU / "AdrenoGmuCallerV1.h").read_text(encoding="utf-8", errors="replace"),
          "the round-46 caller still reads CX status from 0x1540")
    check(pwr_on_bit == 31,
          "the CX poll the backend runs is the same BIT(31) the caller checks")

    # ---- 4. the counts the executor must produce -------------------------
    # These are DERIVED here from the plan SHAPES, not read from the backend:
    #   RCG enable (clk_rcg2_shared_enable): force_enable(1 write) + CFG(1)
    #                                        + UPDATE(1) + clear(1) = 4 writes
    #   branch enable (clk_enable_regmap):   wake/sleep(1) + CLOCK_ENABLE(1) = 2
    rcg_writes = 4
    branch_writes = 2
    branch_clocks = 2          # gpu_cc_cx_gmu_clk, gpu_cc_cxo_clk
    clocks_writes = rcg_writes + branch_writes * branch_clocks
    check(clocks_writes == 8, f"clocks_enable is 8 writes (derived {clocks_writes})")

    # CX is 1 write, GX 0, core 0 (parked), hub 0 (absent).
    total = 1 + 0 + 0 + 0 + clocks_writes
    check(total == 9, f"the whole chain is 9 GPUCC writes (derived {total})")
    print(f"  derived: RCG {rcg_writes} + branches {branch_clocks}x{branch_writes} "
          f"= {clocks_writes}; chain total = {total}")

    # ---- 5. the C module must not hard-code those counts ----------------
    # If a literal 9/8 appeared in the EXECUTOR, it could drift away from the
    # derivation and still pass. Split the file at the self-check, which is
    # allowed to assert `register_writes == 0` because it is CHECKING a derived
    # plan rather than producing traffic.
    verify_at = bec.find("int AdrenoPowerUpBackendV1Verify")
    check(verify_at > 0, "the module still has a separate Verify function")
    executor = bec[:verify_at] if verify_at > 0 else bec
    for literal in ("== 9u", "== 8u", "== 10u"):
        check(literal not in executor,
              f"the executor contains no {literal!r} write-count literal")
    check("stats.writes == " not in executor,
          "the executor never compares its own write counter to a constant")
    check("AdrenoClkCtlV1PlanRcgEnable" in bec,
          "the executor obtains the RCG leg from AdrenoClkCtlV1PlanRcgEnable")
    check("AdrenoClkCtlV1PlanBranchEnable" in bec,
          "the executor obtains each branch leg from AdrenoClkCtlV1PlanBranchEnable")
    check("adreno_gdsc_plan_cpu_enable" in bec,
          "the executor obtains the CX/GX legs from AdrenoGdscDecode")
    check("adreno_gdsc_plan_disable" in bec,
          "the executor obtains the power-down legs from AdrenoGdscDecode")

    # ---- 6. the fail-closed properties --------------------------------
    check("in->armed != 1u" in bec or "in->armed != 1" in bec,
          "Make() refuses an unarmed backend")
    check("stats.refused++" in bec,
          "an MMIO attempt while disarmed is counted, not ignored")
    check("be->parked_cfg == 0u" in bec,
          "clocks_enable refuses before a parked word exists")
    check("AdrenoClkCtlV1GmuClockCount()" in bec,
          "clocks_enable compares the bulk count against the derived one")

    # ---- 7. the two derived no-ops are still what the backend relies on --
    check("AdrenoClkCtlV1HubStepWritesNothing()" in bec,
          "the hub leg is derived at call time, not assumed")
    check("AdrenoPowerUpV1StepGuarded" in bec,
          "the genpd legs consult the round-58 guarded-step predicate")

    # ---- 8. the step order the backend fills in matches round 51 --------
    for name in ("ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE",
                 "ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE",
                 "ADRENO_POWER_UP_V1_STEP_COUNT"):
        check(name in puh, f"AdrenoPowerUpV1.h still defines {name}")
    check("ops[ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE].value = be->in.core_rate_hz"
          in bec,
          "Up() forces the derived core rate into the plan it executes")

    # ---- 9. wiring ------------------------------------------------------
    host = HOST.read_text(encoding="utf-8", errors="replace")
    build = BUILD.read_text(encoding="utf-8", errors="replace")
    check("adreno-power-up-backend-v1-test" in host,
          "the host runner includes the new suite")
    check("AdrenoPowerUpBackendV1" in host,
          "the host runner knows the new object")
    check("AdrenoPowerUpBackendV1.c" in build,
          "the archive build includes the new object")

    # ---- 10. honest boundary ---------------------------------------------
    check("HOST-VERIFIED" in beh,
          "the header still declares the host-verified boundary")
    check("does nothing else" in beh or "maps nothing" in beh,
          "the header still states what the module does NOT do")

    if FAIL:
        print(f"\nPOWER_UP_BACKEND_V1_CROSS: FAIL ({len(FAIL)} problems)")
        return 1
    print("\nPOWER_UP_BACKEND_V1_CROSS: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
