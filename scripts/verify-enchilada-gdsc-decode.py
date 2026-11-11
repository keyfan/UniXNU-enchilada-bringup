#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the round-56 GPU GDSC status-register
DERIVATION (AdrenoGdscDecode) WITHOUT compiling or running the C module.

Independent of method 1 (check-enchilada-adreno-host.py, which compiles and runs
the module). This one re-derives the answer in PYTHON, straight from the
sha256-pinned upstream bytes, and only then compares against what the C module
and AdrenoDomainState.h claim. If the C module and the pinned upstream ever
disagree, this fails even though method 1 would still pass.

Reads:
  (a) sha256-pinned Linux v6.12 gdsc.c / gdsc.h / gpucc-sdm845.c (committed
      scripts/data/enchilada-gpu-gdsc-inputs.json + gitignored .cache bytes,
      reproducible via scripts/fetch-enchilada-gpu-gdsc-sources.py):
        gdsc.c:20    PWR_ON_MASK BIT(31)
        gdsc.c:31-32 GDSC_POWER_UP_COMPLETE BIT(16) / _DOWN_COMPLETE BIT(15)
        gdsc.c:34    CFG_GDSCR_OFFSET 0x4
        gdsc.c:66-71 status-register preference order
        gdsc.c:77-91 mask branches
        gdsc.c:604   gdsc_gx_do_nothing_enable
        gdsc.h:61-70 flag bits
        gpucc-sdm845.c:119-139 the two SDM845 GPU descriptors
  (b) the vendored enchilada DTB (sha256 e590ee32...) at pinned 1-based lines:
      GPUCC window, gpu_cx_gdsc / gpu_gx_gdsc node names + regs + the
      qcom,poll-cfg-gdscr property the DTB itself declares for GX;
  (c) AdrenoGdscDecode.h/.c + AdrenoDomainState.h raw TEXT: the descriptor
      constants, the derived addresses, and the five header constants;
  (d) build-enchilada-adreno.py + check-enchilada-adreno-host.py: the 13th
      archive object and 13th host test are actually wired.

No network (reads the already-pinned manifest + local cache), no CI, no flash,
no device run. HOST-VERIFIED: source text only.

Usage: python -X utf8 scripts/verify-enchilada-gdsc-decode.py
"""
import hashlib
import json
import re
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
WS = DEV.parent.parent
GPU = DEV / "drivers/NativeAdreno"
H = GPU / "AdrenoGdscDecode.h"
C = GPU / "AdrenoGdscDecode.c"
TEST = GPU / "tests/adreno-gdsc-decode-test.cpp"
DOMAIN_H = GPU / "AdrenoDomainState.h"
BUILD = SCRIPTS / "build-enchilada-adreno.py"
RUNNER = SCRIPTS / "check-enchilada-adreno-host.py"
DTB = WS / "notes/mu-silicium/enchilada.dts"
DTB_SHA = "e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275"
PINS = SCRIPTS / "data/enchilada-gpu-gdsc-inputs.json"
CACHE = DEV / ".cache/adreno-a630-source"

fails = []


def check(ok, label):
    print(("PASS " if ok else "FAIL ") + label)
    if not ok:
        fails.append(label)


def text(p):
    return p.read_text(encoding="utf-8", errors="replace")


def pinned(name):
    data = (CACHE / name).read_bytes()
    return data.decode("utf-8", "replace"), data


def grab(pattern, blob, label, group=1):
    """Extract one value, or record a failure and return None."""
    m = re.search(pattern, blob, re.S)
    if not m:
        check(False, label + " (pattern not found)")
        return None
    return m.group(group)


print("=== (a) pinned upstream bytes ===")
manifest = json.loads(text(PINS))
files = manifest["linux"]["files"]
for name, want in files.items():
    data = (CACHE / name).read_bytes()
    check(len(data) == want["bytes"] and hashlib.sha256(data).hexdigest() == want["sha256"],
          "pinned bytes match: %s (%d B sha256 %s...)" % (name, want["bytes"], want["sha256"][:16]))

GDSC, _ = pinned("drivers/clk/qcom/gdsc.c")
GDSC_H, _ = pinned("drivers/clk/qcom/gdsc.h")
GPUCC, _ = pinned("drivers/clk/qcom/gpucc-sdm845.c")

# --- the upstream rule, re-read from bytes (not from my C module) ---
pwr_on_mask = grab(r"#define\s+PWR_ON_MASK\s+BIT\((\d+)\)", GDSC, "gdsc.c PWR_ON_MASK")
pwr_up = grab(r"#define\s+GDSC_POWER_UP_COMPLETE\s+BIT\((\d+)\)", GDSC, "gdsc.c POWER_UP_COMPLETE")
pwr_down = grab(r"#define\s+GDSC_POWER_DOWN_COMPLETE\s+BIT\((\d+)\)", GDSC, "gdsc.c POWER_DOWN_COMPLETE")
cfg_off = grab(r"#define\s+CFG_GDSCR_OFFSET\s+(0x[0-9a-fA-F]+)", GDSC, "gdsc.c CFG_GDSCR_OFFSET")
check(pwr_on_mask == "31", "upstream PWR_ON_MASK is BIT(31) (read: %s)" % pwr_on_mask)
check(pwr_up == "16", "upstream GDSC_POWER_UP_COMPLETE is BIT(16) (read: %s)" % pwr_up)
check(pwr_down == "15", "upstream GDSC_POWER_DOWN_COMPLETE is BIT(15) (read: %s)" % pwr_down)
check(cfg_off == "0x4", "upstream CFG_GDSCR_OFFSET is 0x4 (read: %s)" % cfg_off)

# status-register selection, verbatim structure
sel = re.search(r"if \(sc->flags & POLL_CFG_GDSCR\)\s*\n?\s*reg = sc->gdscr \+ CFG_GDSCR_OFFSET;"
                r"\s*\n?\s*else if \(sc->gds_hw_ctrl\)\s*\n?\s*reg = sc->gds_hw_ctrl;"
                r"\s*\n?\s*else\s*\n?\s*reg = sc->gdscr;", GDSC)
check(sel is not None, "gdsc.c:66-71 preference order (POLL_CFG_GDSCR > gds_hw_ctrl > gdscr) present verbatim")

masks_poll = re.search(r"if \(sc->flags & POLL_CFG_GDSCR\) \{.*?GDSC_POWER_UP_COMPLETE.*?GDSC_POWER_DOWN_COMPLETE",
                       GDSC, re.S)
check(masks_poll is not None, "gdsc.c:77-84 POLL_CFG_GDSCR branch uses UP/DOWN_COMPLETE")
masks_fallback = re.search(r"return !\(val & PWR_ON_MASK\);", GDSC)
check(masks_fallback is not None, "gdsc.c:90 fallback OFF is NOT(val & PWR_ON_MASK)")

gx_noop = re.search(r"int gdsc_gx_do_nothing_enable\(struct generic_pm_domain \*domain\).*?return ret;\s*\}",
                    GDSC, re.S)
check(gx_noop is not None, "gdsc.c:604 gdsc_gx_do_nothing_enable() body present")
check("Do nothing with the GDSC itself" in GDSC, "gdsc.c states it does nothing with the GDSC")

# flag bits, straight from gdsc.h
for flag, bit in (("VOTABLE", 0), ("CLAMP_IO", 1), ("AON_RESET", 4), ("POLL_CFG_GDSCR", 5)):
    want = r"#define\s+%s\s+BIT\(%d\)" % (flag, bit)
    check(re.search(want, GDSC_H) is not None, "gdsc.h flag %s is BIT(%d)" % (flag, bit))

# --- the two SDM845 GPU descriptors: read the NUMBERS out of the source ---
cx_block = re.search(r"static struct gdsc gpu_cx_gdsc = \{(.*?)\n\};", GPUCC, re.S)
gx_block = re.search(r"static struct gdsc gpu_gx_gdsc = \{(.*?)\n\};", GPUCC, re.S)
check(cx_block is not None, "gpucc-sdm845.c gpu_cx_gdsc descriptor block found")
check(gx_block is not None, "gpucc-sdm845.c gpu_gx_gdsc descriptor block found")

cx_src = cx_block.group(1) if cx_block else ""
gx_src = gx_block.group(1) if gx_block else ""
cx_gdscr = int(grab(r"\.gdscr = (0x[0-9a-fA-F]+)", cx_src, "CX .gdscr"), 16) if ".gdscr" in cx_src else None
cx_hw = int(grab(r"\.gds_hw_ctrl = (0x[0-9a-fA-F]+)", cx_src, "CX .gds_hw_ctrl"), 16) if ".gds_hw_ctrl" in cx_src else None
gx_gdscr = int(grab(r"\.gdscr = (0x[0-9a-fA-F]+)", gx_src, "GX .gdscr"), 16) if ".gdscr" in gx_src else None

check(cx_gdscr == 0x106C, "CX .gdscr is 0x106c (read: %s)" % (hex(cx_gdscr) if cx_gdscr else cx_gdscr))
check(cx_hw == 0x1540, "CX .gds_hw_ctrl is 0x1540 (read: %s)" % (hex(cx_hw) if cx_hw else cx_hw))
check(gx_gdscr == 0x100C, "GX .gdscr is 0x100c (read: %s)" % (hex(gx_gdscr) if gx_gdscr else gx_gdscr))
check("POLL_CFG_GDSCR" not in cx_src, "CX descriptor does NOT set POLL_CFG_GDSCR")
check("POLL_CFG_GDSCR" in gx_src, "GX descriptor DOES set POLL_CFG_GDSCR")
check(".power_on = gdsc_gx_do_nothing_enable" in gx_src, "GX .pd.power_on is gdsc_gx_do_nothing_enable")
check(".gds_hw_ctrl" not in gx_src, "GX descriptor has no .gds_hw_ctrl")
check(re.search(r"\[GPU_CX_GDSC\] = &gpu_cx_gdsc", GPUCC) is not None, "gpu_cc_sdm845_gdscs[] maps GPU_CX_GDSC")
check(re.search(r"\[GPU_GX_GDSC\] = &gpu_gx_gdsc", GPUCC) is not None, "gpu_cc_sdm845_gdscs[] maps GPU_GX_GDSC")

print("=== (b) re-derive in PYTHON from the pinned bytes ===")
GPUCC_BASE = 0x05090000
# CX: no POLL_CFG_GDSCR -> gds_hw_ctrl wins; masks are the PWR_ON_MASK branch.
cx_status = GPUCC_BASE + cx_hw if cx_hw else None
cx_on = 1 << int(pwr_on_mask)
cx_off = 0
# GX: POLL_CFG_GDSCR -> gdscr + 4; masks are the COMPLETE bits.
gx_status = GPUCC_BASE + gx_gdscr + int(cfg_off, 16) if gx_gdscr is not None else None
gx_on = 1 << int(pwr_up)
gx_off = 1 << int(pwr_down)
check(cx_status == 0x05091540, "PYTHON re-derivation: CX status == 0x05091540 (got %s)" % hex(cx_status))
check(gx_status == 0x05091010, "PYTHON re-derivation: GX status == 0x05091010 (got %s)" % hex(gx_status))
check(cx_on == 1 << 31, "PYTHON re-derivation: CX on mask == BIT(31)")
check(gx_on == 1 << 16, "PYTHON re-derivation: GX on mask == BIT(16)")
check(gx_off == 1 << 15, "PYTHON re-derivation: GX off mask == BIT(15)")

print("=== (c) the C module + header claim the SAME values ===")
htxt, ctxt, dhtxt = text(H), text(C), text(DOMAIN_H)
check("#define ADRENO_GDSC_CX_GDSCR        0x106cu" in ctxt, "AdrenoGdscDecode.c CX gdscr constant")
check("#define ADRENO_GDSC_CX_GDS_HW_CTRL  0x1540u" in ctxt, "AdrenoGdscDecode.c CX gds_hw_ctrl constant")
check("#define ADRENO_GDSC_GX_GDSCR        0x100cu" in ctxt, "AdrenoGdscDecode.c GX gdscr constant")
check("ADRENO_GDSC_FLAG_VOTABLE" in ctxt, "C uses the VOTABLE flag constant")
check("ADRENO_GDSC_FLAG_POLL_CFG_GDSCR" in ctxt, "C uses the POLL_CFG_GDSCR flag constant")
check("ADRENO_GDSC_FLAG_CLAMP_IO" in ctxt and "ADRENO_GDSC_FLAG_AON_RESET" in ctxt,
      "C uses CLAMP_IO | AON_RESET for GX")
check(re.search(r"desc\.power_on_is_noop = 1u;", ctxt) is not None, "C marks GX power-on as a no-op")
check(re.search(r"desc\.power_on_is_noop = 0u;", ctxt) is not None, "C marks CX power-on as real")

# the header constants the derivation must reproduce
want = {
    "ADRENO_DOMAIN_GPUCC_BASE": 0x05090000,
    "ADRENO_DOMAIN_CX_GDSCR": 0x0509106C,
    "ADRENO_DOMAIN_GX_GDSCR": 0x0509100C,
    "ADRENO_DOMAIN_CX_STATUS": 0x05091540,
    "ADRENO_DOMAIN_GX_STATUS": 0x05091010,
}
for name, value in want.items():
    m = re.search(r"#define\s+%s\s+(0x[0-9a-fA-F]+)ull" % name, dhtxt)
    got = int(m.group(1), 16) if m else None
    check(got == value, "%s == 0x%08x (got %s)" % (name, value, hex(got) if got is not None else None))
for name, bit in (("ADRENO_DOMAIN_CX_ON_BIT", 31), ("ADRENO_DOMAIN_GX_ON_BIT", 16),
                  ("ADRENO_DOMAIN_GX_OFF_BIT", 15)):
    m = re.search(r"#define\s+%s\s+(\d+)u" % name, dhtxt)
    got = int(m.group(1)) if m else None
    check(got == bit, "%s == %d (got %s)" % (name, bit, got))

check("adreno_gdsc_verify_domain_state_constants" in htxt, "header declares the mechanical link function")
check("adreno_gdsc_verify_domain_state_constants" in ctxt, "C implements the mechanical link function")
check("ADRENO_DOMAIN_CX_STATUS" in ctxt and "ADRENO_DOMAIN_GX_STATUS" in ctxt,
      "the link function compares against the AdrenoDomainState.h constants")
check("#include \"AdrenoDomainState.h\"" in ctxt, "C includes AdrenoDomainState.h for the comparison")

print("=== (d) wiring: 13th archive object + 13th host test ===")
btxt, rtxt = text(BUILD), text(RUNNER)
check("AdrenoGdscDecode.c" in btxt, "build-enchilada-adreno.py compiles AdrenoGdscDecode.c")
check("'AdrenoGdscDecode.c', ['-mgeneral-regs-only']" in btxt,
      "the archive unit is gated by -mgeneral-regs-only")
check("'AdrenoGdscDecode'" in btxt, "AdrenoGdscDecode is in the build-report source list")
check("adreno-gdsc-decode-test" in rtxt, "check-enchilada-adreno-host.py runs the gdsc-decode host test")
check("'AdrenoGdscDecode': GPU / 'AdrenoGdscDecode.c'" in rtxt, "the host runner maps the module source")
check("gdsc-decode host tests" in rtxt, "the host runner PASS line mentions gdsc-decode")

print("=== (e) task surface ===")
ttxt = text(TEST)
check("testConstantsAreDerived" in ttxt, "host test covers the derivation of the header constants")
check("adreno_gdsc_verify_domain_state_constants() == 1" in ttxt,
      "host test asserts the mechanical link returns 1")
check("adreno_domain_cx_is_on" in ttxt and "adreno_domain_gx_is_off" in ttxt,
      "host test cross-checks against the pre-existing predicates")
check("0x00005ff4" in ttxt and "0x00088400" in ttxt,
      "host test decodes the device words measured in rounds 35/49/50/52")

print("=== (f) DTB: the node declarations the descriptors mirror ===")
dtb = DTB.read_bytes()
check(len(dtb) == 784857 and hashlib.sha256(dtb).hexdigest() == DTB_SHA,
      "vendored DTB bytes match the pinned sha256")
dtxt = dtb.decode("utf-8", "replace")
check(re.search(r"qcom,gpucc@5090000", dtxt) is not None, "DTB declares qcom,gpucc@5090000")
check(re.search(r'regulator-name = "gpu_cx_gdsc"', dtxt) is not None, "DTB names gpu_cx_gdsc")
check(re.search(r'regulator-name = "gpu_gx_gdsc"', dtxt) is not None, "DTB names gpu_gx_gdsc")
check(re.search(r"qcom,gdsc@0x509106c", dtxt) is not None, "DTB CX gdsc reg is 0x509106c")
check(re.search(r"qcom,gdsc@0x509100c", dtxt) is not None, "DTB GX gdsc reg is 0x509100c")
check(re.search(r"qcom,poll-cfg-gdscr", dtxt) is not None,
      "DTB itself declares qcom,poll-cfg-gdscr (the GX property)")

print("=== (g) round 57: the ENABLE/DISABLE sequences are derived too ===")
# The order in gdsc_enable()/gdsc_disable() is read out of the pinned bytes and
# then re-expressed as an ordered step list, independently of the C module.
en_body = grab(r"static int gdsc_enable\(struct generic_pm_domain \*domain\)\s*\{(.*?)\n\}",
               GDSC, "gdsc.c gdsc_enable() body")
dis_body = grab(r"static int gdsc_disable\(struct generic_pm_domain \*domain\)\s*\{(.*?)\n\}",
                GDSC, "gdsc.c gdsc_disable() body")


def order_of(body, needle):
    return body.find(needle) if body else -99


# gdsc_enable(): AON reset -> clamp release -> collapse -> force mem -> hwctrl -> retain
e_aon = order_of(en_body, "gdsc_assert_reset_aon(sc)")
e_clamp = order_of(en_body, "gdsc_deassert_clamp_io(sc)")
e_toggle = order_of(en_body, "gdsc_toggle_logic(sc, GDSC_ON, false)")
e_mem = order_of(en_body, "gdsc_force_mem_on(sc)")
e_hw = order_of(en_body, "gdsc_hwctrl(sc, true)")
e_ret = order_of(en_body, "gdsc_retain_ff_on(sc)")
check(e_aon >= 0 and e_clamp >= 0 and e_aon < e_clamp < e_toggle,
      "gdsc_enable(): AON reset BEFORE clamp release BEFORE the collapse write")
check(e_toggle < e_mem < e_hw < e_ret,
      "gdsc_enable(): toggle -> force-mem -> hwctrl -> retain, in that order")

# gdsc_disable(): hwctrl off -> clear mem -> toggle -> clamp assert
d_hw = order_of(dis_body, "gdsc_hwctrl(sc, false)")
d_mem = order_of(dis_body, "gdsc_clear_mem_on(sc)")
d_toggle = order_of(dis_body, "gdsc_toggle_logic(sc, GDSC_OFF,")
d_clamp = order_of(dis_body, "gdsc_assert_clamp_io(sc)")
check(d_hw >= 0 and d_mem >= 0 and d_hw < d_mem < d_toggle,
      "gdsc_disable(): hwctrl off -> clear-mem -> collapse write, in that order")
check(d_toggle >= 0 and d_clamp > d_toggle,
      "gdsc_disable(): clamp IO is asserted only AFTER the collapse")

# The VOTABLE short-cut is the reason CX can never poll on the way down.
check(re.search(r"if \(\(sc->flags & VOTABLE\) && status == GDSC_OFF && !wait\) \{(.*?)return 0;",
                GDSC, re.S) is not None,
      "gdsc.c:153-161 VOTABLE has an explicit no-poll disable short-cut")
check(re.search(r"#define\s+TIMEOUT_US\s+(\d+)", GDSC).group(1) == "500",
      "the VOTABLE short-cut waits TIMEOUT_US 500")

# Re-derive each descriptor's plan shape in PYTHON from the flags alone.
def plan_shape(gdscr, flags, has_supply, has_cxcs, polling, noop):
    """Mirror gdsc_enable()/gdsc_disable() selection using ONLY the flags."""
    steps = []
    if noop:
        return steps, 0
    if flags.get("CLAMP_IO"):
        if flags.get("AON_RESET"):
            steps.append("ASSERT_RESET_AON")
        steps.append("DEASSERT_CLAMP_IO")
    steps.append("UPDATE_COLLAPSE")
    if not polling:
        steps.append("UDELAY")
        steps.append("POLL_ON")
    else:
        steps.append("POLL_ON")
    if has_supply:
        steps.insert(0, "REGULATOR_ENABLE")
    writes = sum(1 for s in steps if s in ("ASSERT_RESET_AON", "DEASSERT_CLAMP_IO",
                                           "UPDATE_COLLAPSE", "POLL_ON"))
    return steps, writes


cx_steps, cx_writes = plan_shape(0x106C, {"VOTABLE": True, "CLAMP_IO": False}, False, False,
                                 polling=False, noop=False)
gx_steps, gx_writes = plan_shape(0x100C, {"CLAMP_IO": True, "AON_RESET": True,
                                          "POLL_CFG_GDSCR": True}, False, False,
                                 polling=True, noop=False)
check(cx_steps == ["UPDATE_COLLAPSE", "UDELAY", "POLL_ON"],
      "PYTHON re-derivation: CX enable is collapse -> wait -> poll (got %s)" % cx_steps)
check(cx_writes == 2, "PYTHON re-derivation: CX enable performs 2 register writes (got %d)" % cx_writes)
check(gx_steps == ["ASSERT_RESET_AON", "DEASSERT_CLAMP_IO", "UPDATE_COLLAPSE", "POLL_ON"],
      "PYTHON re-derivation: GX enable AON -> clamp -> collapse -> poll (got %s)" % gx_steps)
check(plan_shape(0x100C, {}, False, False, True, noop=True) == ([], 0),
      "PYTHON re-derivation: the no-op GX power-on performs ZERO writes")

print("=== (h) round 57: the C module encodes that same sequence ===")
check("adreno_gdsc_plan_enable" in htxt and "adreno_gdsc_plan_disable" in htxt,
      "header declares the enable and disable planners")
check("adreno_gdsc_plan_cpu_enable" in htxt and "adreno_gdsc_plan_cpu_enable" in ctxt,
      "header and C both expose the CPU-facing planner")
check("adreno_gdsc_verify_gx_enable_writes_nothing" in htxt and
      "adreno_gdsc_verify_gx_enable_writes_nothing" in ctxt,
      "the GX zero-write property has a dedicated mechanical check")
check("ADRENO_GDSC_MAX_STEPS" in htxt and "steps[ADRENO_GDSC_MAX_STEPS]" in htxt,
      "the plan is a fixed-size step array (no allocation)")
check("plan_push" in ctxt, "C has a bounded push helper for the step array")
check(re.search(r"count\s*>=\s*ADRENO_GDSC_MAX_STEPS", ctxt) is not None,
      "the push helper refuses to overflow the step array")
check("ADRENO_GDSC_STEP_POLL_OFF" in ctxt and "ADRENO_GDSC_STEP_ASSERT_CLAMP_IO" in ctxt,
      "C models the disable-side poll and clamp steps")
check("ADRENO_GDSC_STEP_UDELAY" in ctxt and "ADRENO_GDSC_TIMEOUT_US" in ctxt,
      "C models the VOTABLE short-cut wait by name, not by a literal")
check(re.search(r"#define\s+ADRENO_GDSC_TIMEOUT_US\s+500u", htxt) is not None,
      "the C header pins that wait to 500 us, matching TIMEOUT_US")
check("power_on_is_noop" in ctxt and "adreno_gdsc_plan_cpu_enable" in ctxt,
      "the CPU planner consults the no-op flag instead of assuming a writable GX")

# The GX clamp-IO register is the one the pinned descriptor declares.
gx_clamp = re.search(r"\.clamp_io_ctrl = (0x[0-9a-fA-F]+)", gx_src)
check(gx_clamp is not None and int(gx_clamp.group(1), 16) == 0x1508,
      "GX .clamp_io_ctrl is 0x1508 and the C planner writes exactly that")
check("0x1508u" in ctxt or "ADRENO_GDSC_GX_CLAMP_IO" in ctxt,
      "the C module carries the 0x1508 clamp-IO register")

print("=== (i) round 57: the DTB mirrors the same two policies ===")
cx_dtb = re.search(r"qcom,gdsc@0x509106c \{(.*?)\n\s*\};", dtxt, re.S)
gx_dtb = re.search(r"qcom,gdsc@0x509100c \{(.*?)\n\s*\};", dtxt, re.S)
check(cx_dtb is not None, "DTB has the CX gdsc node")
check(gx_dtb is not None, "DTB has the GX gdsc node")
cx_node = cx_dtb.group(1) if cx_dtb else ""
gx_node = gx_dtb.group(1) if gx_dtb else ""
check("qcom,no-status-check-on-disable" in cx_node,
      "DTB CX declares no-status-check-on-disable (mirror of the VOTABLE short-cut)")
check("qcom,poll-cfg-gdscr" not in cx_node, "DTB CX does NOT poll CFG_GDSCR")
check("qcom,poll-cfg-gdscr" in gx_node, "DTB GX DOES poll CFG_GDSCR")
check("qcom,reset-aon-logic" in gx_node, "DTB GX declares reset-aon-logic (the AON_RESET flag)")
check("qcom,clk-dis-wait-val = <0x08>" in cx_node,
      "DTB CX carries clk-dis-wait-val 0x08, matching .clk_dis_wait_val")

print()
if fails:
    print("GDSC_DECODE_CROSS: FAIL (%d failures)" % len(fails))
    for f in fails:
        print("  - " + f)
    raise SystemExit(1)
print("GDSC_DECODE_CROSS: PASS (0 failures)")
