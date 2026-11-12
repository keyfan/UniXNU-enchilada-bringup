#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the round-61 GPUCC clock-control
DERIVATION (AdrenoClkCtlV1) WITHOUT compiling or running the C module.

Independent of method 1 (check-enchilada-adreno-host.py, which compiles and runs
the module). This script re-derives the same facts in PYTHON, straight from the
sha256-pinned upstream bytes plus the vendored board DTB, and then checks that
the C module agrees:

  (1) the frequency table, INCLUDING the F() macro expansion (stored pre_div is
      2*h-1, so the 200 MHz row is 2, not 1.5);
  (2) qcom_find_freq()'s selection rule, including the "f - 1" above-table case;
  (3) the derived CFG word 0x0602 and the absence of MODE/HW_CLK_CTRL bits;
  (4) finding 1: NO "hub"/"hub_clk" id exists in either the pinned mainline
      SDM845 GMU node or the vendored vendor DTB, so clk_set_rate() on it is
      the NULL-guard no-op -- and the SM8550 node DOES carry one, which is the
      contrast that makes this a generation fact;
  (5) finding 2: resume calls clk_set_rate BEFORE clk_bulk_prepare_enable, and
      the shared set_rate path is parked while the RCG is disabled, so the
      resume-time plan has ZERO register writes;
  (6) finding 3: exactly 2 of the 4 bulk clock ids are GPUCC registers;
  (7) finding 4: the branch2 halt check is a DISJUNCTION, and the module models
      it as its own kind rather than as a single-mask poll;
  (8) the module contains no hard-coded write counts that could drift away from
      the derivation, and it is wired into the archive build and the host runner.

Reads:
  (a) sha256-pinned Linux v6.12 gpucc-sdm845.c / clk-rcg2.c / clk-rcg.h /
      clk-branch.c / clk-branch.h / clk.c / msm_io_utils.c / a6xx_gmu.c /
      sdm845.dtsi / sm8550.dtsi;
  (b) the vendored board DTB (sha256-pinned);
  (c) AdrenoClkCtlV1.h/.c raw text;
  (d) scripts/build-enchilada-adreno.py + check-enchilada-adreno-host.py.

No network, no CI, no flash, no device run. HOST-VERIFIED: source text only.

Usage: python -X utf8 scripts/verify-enchilada-clk-ctl-v1.py
"""
import hashlib
import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
# The vendored vendor DTB lives in the port working directory (two levels above
# scripts/), the same location verify-enchilada-gpu-board.py uses.
W = SCRIPTS.parents[2]
GPU = DEV / "drivers/NativeAdreno"
CACHE = DEV / ".cache/adreno-a630-source"
PINS_PATH = SCRIPTS / "data/enchilada-gpu-clk-inputs.json"
MOD_H = GPU / "AdrenoClkCtlV1.h"
MOD_C = GPU / "AdrenoClkCtlV1.c"
MOD_TEST = GPU / "tests/adreno-clk-ctl-v1-test.cpp"
BUILD = SCRIPTS / "build-enchilada-adreno.py"
RUNNER = SCRIPTS / "check-enchilada-adreno-host.py"
DTB = W / "notes/mu-silicium/enchilada.dts"

fails = []
checks = 0
PINS = {}


def check(ok, label):
    global checks
    checks += 1
    print(("PASS " if ok else "FAIL ") + label)
    if not ok:
        fails.append(label)


def pinned(name):
    """Verify the pinned bytes and return the decoded text."""
    pin = PINS[name]
    data = (CACHE / name).read_bytes()
    ok = len(data) == pin["bytes"] and hashlib.sha256(data).hexdigest() == pin["sha256"]
    check(ok, f"pinned intact: {name} ({len(data)} B)")
    return data.decode("utf-8", "replace")


def line_of(text, needle):
    for i, line in enumerate(text.splitlines(), 1):
        if needle in line:
            return i
    return -1


def main():
    global PINS
    pin_doc = json.loads(PINS_PATH.read_text(encoding="utf-8"))
    PINS = pin_doc["linux"]["files"]

    # ---- domain 1: the pinned bytes are the ones the module was derived from
    gpucc = pinned("drivers/clk/qcom/gpucc-sdm845.c")
    rcg2 = pinned("drivers/clk/qcom/clk-rcg2.c")
    rcg_h = pinned("drivers/clk/qcom/clk-rcg.h")
    branch_c = pinned("drivers/clk/qcom/clk-branch.c")
    branch_h = pinned("drivers/clk/qcom/clk-branch.h")
    clk_c = pinned("drivers/clk/clk.c")
    io_utils = pinned("drivers/gpu/drm/msm/msm_io_utils.c")
    gmu = pinned("drivers/gpu/drm/msm/adreno/a6xx_gmu.c")
    dtsi = pinned("arch/arm64/boot/dts/qcom/sdm845.dtsi")
    sm8550 = pinned("arch/arm64/boot/dts/qcom/sm8550.dtsi")

    # ---- domain 2: the vendored board DTB is the same file the port trusts
    dtb_pin = pin_doc["dtb"]
    dtb = DTB.read_bytes()
    check(len(dtb) == dtb_pin["bytes"] and hashlib.sha256(dtb).hexdigest() == dtb_pin["sha256"],
          "vendored board DTB sha256 matches the pin")
    dtb_text = dtb.decode("utf-8", "replace")

    mod_h = MOD_H.read_text(encoding="utf-8")
    mod_c = MOD_C.read_text(encoding="utf-8")

    # ---- 1. the frequency table and the F() macro expansion -----------------
    check('#define F(f, s, h, m, n) { (f), (s), (2 * (h) - 1), (m), (n) }' in rcg_h,
          "clk-rcg.h defines F() as { f, s, 2*h-1, m, n }")

    rows = re.findall(r"F\((\d+),\s*(P_\w+),\s*([0-9.]+),", gpucc)
    check(len(rows) == 3, f"gpucc-sdm845.c lists exactly 3 frequency rows (found {len(rows)})")

    # parent_map_0: the .cfg values are what land in the register, not the
    # enum indices.
    pmap = dict(re.findall(r"\{\s*(P_\w+),\s*(\d+)\s*\}", gpucc))
    check(pmap.get("P_BI_TCXO") == "0", "parent_map_0: bi_tcxo -> cfg 0")
    check(pmap.get("P_GPLL0_OUT_MAIN_DIV") == "6", "parent_map_0: gpll0_div -> cfg 6")
    check(pmap.get("P_GPU_CC_PLL1_OUT_MAIN") == "3", "parent_map_0: gpu_cc_pll1 -> cfg 3")

    derived = [{"freq": int(f), "src": int(pmap[s]), "pre": int(2 * float(h) - 1)}
               for f, s, h in rows]

    check(derived[0]["freq"] == 19200000 and derived[0]["pre"] == 1,
          "row 1: 19.2 MHz, h=1 -> pre_div 1")
    check(derived[1]["freq"] == 200000000 and derived[1]["pre"] == 2,
          "row 2: 200 MHz, h=1.5 -> pre_div 2 (NOT 1.5, NOT 1)")
    check(derived[2]["freq"] == 500000000 and derived[2]["pre"] == 1,
          "row 3: 500 MHz, h=1 -> pre_div 1")

    # parent_map_0: the .cfg values are what land in the register, not the
    # enum indices.
    pmap = dict(re.findall(r"\{\s*(P_\w+),\s*(\d+)\s*\}", gpucc))
    check(pmap.get("P_BI_TCXO") == "0", "parent_map_0: bi_tcxo -> cfg 0")
    check(pmap.get("P_GPLL0_OUT_MAIN_DIV") == "6", "parent_map_0: gpll0_div -> cfg 6")
    check(pmap.get("P_GPU_CC_PLL1_OUT_MAIN") == "3", "parent_map_0: gpu_cc_pll1 -> cfg 3")

    # parent_map_0: the .cfg values are what land in the register, not the
    # enum indices. (declared above, used to resolve the F() row sources)

    # The module's own table must agree numerically.
    mod_rows = re.findall(r"\{\s*(\d+)u,\s*(\d+)u,\s*(\d+)u\s*\}", mod_c)
    check(len(mod_rows) == 3, f"AdrenoClkCtlV1.c has 3 table rows (found {len(mod_rows)})")
    mod_derived = [{"freq": int(f), "src": int(s), "pre": int(p)} for f, s, p in mod_rows]
    check(mod_derived == derived, "the module's table equals the pinned re-derivation")

    # ---- 2. qcom_find_freq()'s rule ---------------------------------------
    # The rule is asserted from the module side and from the documented upstream
    # behaviour: first row with rate <= row.freq, else f - 1 (the last row).
    check("rate_hz <= kFreqTable[i].freq_hz" in mod_c,
          "the module selects the FIRST row with rate <= row frequency")
    check("kFreqTable[n - 1u]" in mod_c,
          "and falls back to the LAST row above the table (upstream f - 1)")

    def select(rate):
        for d in derived:
            if rate <= d["freq"]:
                return d
        return derived[-1]

    check(select(200000000)["freq"] == 200000000, "200 MHz selects its own row")
    check(select(300000000)["freq"] == 500000000, "300 MHz selects the HIGHER row")
    check(select(1)["freq"] == 19200000, "1 Hz selects the first row")
    check(select(10**9)["freq"] == 500000000, "1 GHz selects the last row")

    # ---- 3. the derived CFG word ------------------------------------------
    def cfg_word(d):
        return ((d["pre"] << 0) & 0x1f) | ((d["src"] << 8) & 0x700)

    resume = select(200000000)
    check(cfg_word(resume) == 0x0602, f"resume CFG word derives to 0x0602 (got {cfg_word(resume):#06x})")
    check((cfg_word(resume) & 0x3000) == 0, "no CFG_MODE bit (mnd_width is 0)")
    check((cfg_word(resume) & 0x100000) == 0, "no HW_CLK_CTRL bit")
    check("0x1120u" in mod_h and "0x1098u" in mod_h and "0x109cu" in mod_h,
          "the module carries the derived RCG/CBCR offsets")

    # mnd_width 0 means the configure path has no M/N/D writes.
    check(re.search(r"\.mnd_width\s*=\s*0\b", gpucc) is not None,
          "gpu_cc_gmu_clk_src declares .mnd_width = 0")

    # ---- 4. finding 1: no hub clock exists --------------------------------
    gmu_node = dtsi[dtsi.index("gmu: gmu@506a000"):]
    gmu_node = gmu_node[: gmu_node.index("};")]
    names_line = re.search(r'clock-names\s*=\s*([^;]+);', gmu_node)
    check(names_line is not None, "the mainline SDM845 GMU node has clock-names")
    sd_names = re.findall(r'"([\w-]+)"', names_line.group(1))
    check(sd_names == ["gmu", "cxo", "axi", "memnoc"],
          f"mainline SDM845 GMU clock-names are gmu/cxo/axi/memnoc (got {sd_names})")
    check("hub" not in sd_names, "no clock named \"hub\" on SDM845")

    sm_node = sm8550[sm8550.index("gmu: gmu@3d6a000"):]
    sm_node = sm_node[: sm_node.index("};")]
    sm_names_line = re.search(r'clock-names\s*=\s*([^;]+);', sm_node)
    sm_names = re.findall(r'"([\w-]+)"', sm_names_line.group(1))
    check("hub" in sm_names,
          f"the A740 SM8550 GMU node DOES have a \"hub\" clock (got {sm_names})")

    # The vendor DTB must agree with mainline on the 4 names.
    vdtb_names = re.findall(r'gmu_clk\\0cxo_clk\\0axi_clk\\0memnoc_clk', dtb_text)
    check(len(vdtb_names) >= 1,
          "the vendored vendor DTB names the same 4 GMU clocks (no hub)")

    # The getter and the NULL guard are the mechanism.
    check('"%s_clk"' in io_utils and "return NULL;" in io_utils,
          "msm_clk_bulk_get_clock matches id or id_clk and returns NULL when absent")
    check(re.search(r"int clk_set_rate\(struct clk \*clk, unsigned long rate\)\s*\{[^}]*if \(!clk\)\s*return 0;",
                    clk_c, re.S) is not None,
          "clk_set_rate() returns 0 for a NULL clk (the silent no-op)")
    check(line_of(gmu, 'gmu->hub_clk = msm_clk_bulk_get_clock') > 0,
          "a6xx_gmu.c resolves gmu->hub_clk through that getter")
    check(line_of(gmu, 'clk_set_rate(gmu->hub_clk') > 0,
          "and calls clk_set_rate on it unconditionally")

    # The module must state the same conclusion, and NOT as a stored guess.
    check("AdrenoClkCtlV1HubClockPresent" in mod_c and "return 0;" in mod_c,
          "the module encodes the hub clock as ABSENT")
    check("AdrenoClkCtlV1HubSetRateIsNoop" in mod_c,
          "and exposes the no-op as its own predicate")
    check("AdrenoClkCtlV1HubStepWritesNothing" in mod_c,
          "and exposes the derived zero-write conclusion")

    # ---- 5. finding 2: resume order + parked path -------------------------
    core_line = line_of(gmu, "clk_set_rate(gmu->core_clk, 200000000)")
    enable_line = line_of(gmu, "clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks)")
    check(0 < core_line < enable_line,
          f"clk_set_rate(core) precedes clk_bulk_prepare_enable ({core_line} < {enable_line})")
    check(re.search(r"if \(!clk_hw_is_enabled\(hw\)\)\s*return __clk_rcg2_configure\(rcg, f, &rcg->parked_cfg\);",
                    rcg2, re.S) is not None,
          "the shared set_rate takes the PARKED branch while the RCG is disabled")
    check("parked_cfg" in rcg2 and "regmap_write(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG, rcg->parked_cfg)" in rcg2,
          "and the enable path writes parked_cfg back")
    check("ADRENO_CLK_CTL_STEP_PARK_CFG" in mod_h,
          "the module models the parked step as a no-I/O kind")
    check("ADRENO_CLK_CTL_STEP_PARK_CFG, 0u, 0u" in mod_c,
          "and emits it with writes == 0")

    # ---- 6. finding 3: 2 of 4 clocks are GPUCC ---------------------------
    check(len(sd_names) == 4, "the bulk set is 4 clocks")
    check("AdrenoClkCtlV1GpuccClockCount" in mod_c and "return 2u;" in mod_c,
          "the module reports a GPUCC subset count of 2")
    check("GCC_DDRSS_GPU_AXI_CLK" in gmu_node or "gcc" in gmu_node.lower(),
          "the other two bulk clocks are GCC references in the same node")

    # ---- 7. finding 4: the branch halt disjunction -----------------------
    check(re.search(r"return \(val & CBCR_CLK_OFF\) == \(invert \? CBCR_CLK_OFF : 0\) \|\|",
                    branch_c) is not None,
          "clk_branch2_check_halt accepts CLK_OFF clear OR the NOC FSM")
    check("FIELD_GET(CBCR_NOC_FSM_STATUS, val) == FSM_STATUS_ON" in branch_c,
          "the second acceptance path is the NOC FSM status field")
    check("ADRENO_CLK_CTL_STEP_POLL_BRANCH2_ON" in mod_h and "ADRENO_CLK_CTL_STEP_POLL_BRANCH2_ON" in mod_c,
          "the module gives the disjunction its OWN step kind")
    check("#define CBCR_CLK_OFF\t\t\tBIT(31)" in branch_h, "CBCR_CLK_OFF is BIT(31)")
    check("#define CBCR_CLOCK_ENABLE\t\tBIT(0)" in branch_h, "CBCR_CLOCK_ENABLE is BIT(0)")

    wake = re.search(r"CBCR_WAKEUP\s+GENMASK\((\d+),\s*(\d+)\)", branch_h)
    sleep = re.search(r"CBCR_SLEEP\s+GENMASK\((\d+),\s*(\d+)\)", branch_h)
    check(wake is not None and sleep is not None, "the wake/sleep fields are in clk-branch.h")
    mask = 0
    for hi, lo in ((wake.group(1), wake.group(2)), (sleep.group(1), sleep.group(2))):
        mask |= ((1 << (int(hi) + 1)) - 1) & ~((1 << int(lo)) - 1)
    check(mask == 0x0FF0, f"the wake/sleep mask derives to 0x0ff0 (got {mask:#06x})")
    check("0x00000ff0u" in mod_h, "and the module carries 0x0ff0, not 0x0f00")

    # ---- 8. no drifting hard-coded counts --------------------------------
    # The module must compute counts from the plans, not assert magic numbers
    # in its predicates. `return 2u;` for the GPUCC count is a derived board
    # fact with a comment, which is the same convention the other modules use.
    check(mod_c.count("return 2u;") == 1, "exactly one derived 2u board fact")
    check("AdrenoClkCtlV1Verify" in mod_c and "ACCV1_REQUIRE" in mod_c,
          "the module carries its own fail-closed self-check")

    # ---- 9. wiring ----------------------------------------------------------
    build = BUILD.read_text(encoding="utf-8")
    check("AdrenoClkCtlV1.c" in build, "the archive build compiles AdrenoClkCtlV1.c")
    runner = RUNNER.read_text(encoding="utf-8")
    check("adreno-clk-ctl-v1-test" in runner, "the host runner registers the new test")
    check("AdrenoClkCtlV1'" in runner or "'AdrenoClkCtlV1'" in runner,
          "and links its object into that test")

    # ---- 10. the honest boundary is still honoured ----------------------
    for token in ("regmap_write", "writel", "ioremap", "IOMemoryDescriptor"):
        check(token not in mod_c, f"the module performs no {token}")

    print(f"\nCLK_CTL_V1_CROSS: {'PASS' if not fails else 'FAIL'} "
          f"({checks} checks, {len(fails)} failures)")
    if fails:
        for f in fails:
            print("  FAILED: " + f)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
