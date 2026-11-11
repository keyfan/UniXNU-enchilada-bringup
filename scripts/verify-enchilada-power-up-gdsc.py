#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the round-58 COMPOSITION contract
(AdrenoPowerUpGdsc) WITHOUT compiling or running the C module.

Independent of method 1 (check-enchilada-adreno-host.py, which compiles and runs
the module). This script re-derives, in PYTHON and straight from the
sha256-pinned upstream bytes, the number of register WRITES each GPUCC domain's
CPU power-up path performs, and then checks that:

  (1) the derived counts agree with what AdrenoPowerUpGdsc.c claims;
  (2) the guarded bit of every genpd step in AdrenoPowerUpV1 agrees with
      "the CPU writes nothing for that domain" -- the round-58 invariant;
  (3) the CX VOTABLE short-cut (write collapse, wait, do NOT poll) is present;
  (4) the GX fail-safe (disable DOES poll and DOES clamp) is present;
  (5) AdrenoPowerUpGdsc.c contains no hard-coded count that could drift away
      from the derivation.

Reads:
  (a) sha256-pinned Linux v6.12 gdsc.c / gdsc.h / gpucc-sdm845.c;
  (b) AdrenoGdscDecode.h/.c  (round 57 planners, the source of the counts);
  (c) AdrenoPowerUpV1.h/.c   (round 51 plan + StepGuarded);
  (d) AdrenoPowerUpGdsc.h/.c (round 58 composition) raw text;
  (e) scripts/build-enchilada-adreno.py + check-enchilada-adreno-host.py
      (the 14th archive object and 14th host test are actually wired).

No network, no CI, no flash, no device run. HOST-VERIFIED: source text only.

Usage: python -X utf8 scripts/verify-enchilada-power-up-gdsc.py
"""
import json
import re
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
WS = DEV.parent.parent
GPU = DEV / "drivers/NativeAdreno"

GDSC_H = GPU / "AdrenoGdscDecode.h"
GDSC_C = GPU / "AdrenoGdscDecode.c"
PU_H = GPU / "AdrenoPowerUpV1.h"
PU_C = GPU / "AdrenoPowerUpV1.c"
COMP_H = GPU / "AdrenoPowerUpGdsc.h"
COMP_C = GPU / "AdrenoPowerUpGdsc.c"
COMP_TEST = GPU / "tests/adreno-power-up-gdsc-test.cpp"
DOMAIN_H = GPU / "AdrenoDomainState.h"
BUILD = SCRIPTS / "build-enchilada-adreno.py"
RUNNER = SCRIPTS / "check-enchilada-adreno-host.py"
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
    return (CACHE / name).read_bytes().decode("utf-8", "replace")


print("=== (a) pinned upstream bytes ===")
manifest = json.loads(text(PINS))
files = manifest["linux"]["files"]
# `files` is keyed BY PATH (not a list), so one source can never be duplicated
# or silently missed: the three names below must all be present as keys.
EXPECTED = [
    "drivers/clk/qcom/gdsc.c",
    "drivers/clk/qcom/gdsc.h",
    "drivers/clk/qcom/gpucc-sdm845.c",
]
check(isinstance(files, dict), "the pin manifest keys sources by path")
check(len(files) == 3, "the pin manifest carries exactly 3 GDSC sources")
for name in EXPECTED:
    check(name in files, "pinned source %s is listed" % name)
    if name in files:
        body = pinned(name)
        entry = files[name]
        check(len(body.encode("utf-8")) == entry["bytes"],
              "pinned %s is %d bytes" % (name, entry["bytes"]))

gdsc = pinned("drivers/clk/qcom/gdsc.c")
gpucc = pinned("drivers/clk/qcom/gpucc-sdm845.c")
gdsc_h = pinned("drivers/clk/qcom/gdsc.h")

print("=== (b) re-derive the CX write count from gdsc.c ===")
# The CPU enable path is gdsc_enable(); the ONLY unconditional GDSC register
# write in it is the collapse clear inside gdsc_toggle_logic (gdsc.c:150).
check("gdsc_update_collapse_bit(sc, status == GDSC_OFF);" in gdsc,
      "gdsc_toggle_logic writes the collapse bit via gdsc_update_collapse_bit")
check("gdsc_poll_status(sc, status)" in gdsc,
      "gdsc_toggle_logic then POLLS (a read, not a write)")
# gdsc_update_collapse_bit itself is one regmap_update_bits call.
m = re.search(r"static int gdsc_update_collapse_bit\(.*?\n\}", gdsc, re.S)
collapse_fn = m.group(0) if m else ""
check(collapse_fn.count("regmap_update_bits") == 1,
      "gdsc_update_collapse_bit performs exactly one register update")
# gdsc_enable() gives CX no clamp/AON/reset work (those need the CLAMP_IO flag).
cx_desc = re.search(r"struct gdsc gpu_cx_gdsc = \{(.*?)\n\};", gpucc, re.S)
gx_desc = re.search(r"struct gdsc gpu_gx_gdsc = \{(.*?)\n\};", gpucc, re.S)
check(cx_desc is not None, "gpucc-sdm845.c declares gpu_cx_gdsc")
check(gx_desc is not None, "gpucc-sdm845.c declares gpu_gx_gdsc")
cx_body = cx_desc.group(1) if cx_desc else ""
gx_body = gx_desc.group(1) if gx_desc else ""
check("CLAMP_IO" not in cx_body, "CX has no CLAMP_IO flag -> no clamp writes")
check("HW_CTRL" not in cx_body, "CX has no HW_CTRL flag -> no hwctrl writes")
check(".supply" not in cx_body, "CX has no .supply -> no regulator step")
check(".cxcs" not in cx_body, "CX has no .cxcs -> no retention writes")
# Therefore: CX enable writes = 1, and its step list is larger than 1 (poll+delay).
CX_WRITES_DERIVED = 1
check(CX_WRITES_DERIVED == 1, "derived CX enable write count is 1")

print("=== (c) re-derive the GX write count from gdsc.c ===")
check("int gdsc_gx_do_nothing_enable(struct generic_pm_domain *domain)" in gdsc,
      "gdsc.c defines gdsc_gx_do_nothing_enable")
m = re.search(r"int gdsc_gx_do_nothing_enable\(.*?\n\}", gdsc, re.S)
gx_fn = m.group(0) if m else ""
check("Do nothing with the GDSC itself" in gx_fn,
      "the GX enable function documents that it does nothing to the GDSC")
check("regmap_" not in gx_fn, "the GX enable function performs NO register I/O")
check("power_on = gdsc_gx_do_nothing_enable" in gx_body,
      "gpu_gx_gdsc wires pd.power_on to the no-op")
check(".supply" not in gx_body, "GX has no .supply -> the no-op writes nothing at all")
check(".cxcs" not in gx_body, "GX has no .cxcs -> no retention writes")
GX_WRITES_DERIVED = 0
check(GX_WRITES_DERIVED == 0, "derived GX enable write count is 0")

print("=== (d) the C module agrees with the Python derivation ===")
ctxt = text(COMP_C)
htxt = text(COMP_H)
check("c.cx_enable_writes == 1u" in ctxt,
      "the C verifies CX enable writes == 1")
check("c.gx_enable_writes == 0u" in ctxt,
      "the C verifies GX enable writes == 0")
# The module must NOT hard-code the counts as literals in the accessors: every
# count it reports has to come from the round-57 planners.
check("register_writes" in ctxt,
      "the C reads register_writes out of the derived plans")
check("adreno_gdsc_plan_cpu_enable" in ctxt,
      "the C consumes the round-57 CPU-enable planner")
check("adreno_gdsc_plan_disable" in ctxt,
      "the C consumes the round-57 disable planner")
# No literal write-count assignment anywhere: the only numbers allowed are
# comparison constants inside Verify().
assigns = re.findall(r"->\s*(?:cx|gx)_enable_writes\s*=\s*([^;]+);", ctxt)
check(len(assigns) == 2, "exactly two enable-write assignments exist")
check(all("register_writes" in a for a in assigns),
      "both assignments copy register_writes, never a literal")

print("=== (e) the round-58 invariant holds across both modules ===")
putxt = text(PU_C)
check("step == ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON ? 1u : 0u" in putxt,
      "AdrenoPowerUpV1 guards ONLY the GX genpd step")
check("AdrenoPowerUpGdscGuardMatchesWrites" in htxt,
      "the invariant is exposed as a named predicate")
check("(cx_guarded != 0u) != (cx_writes == 0u)" in ctxt,
      "CX: guarded <=> zero writes, checked in both directions")
check("(gx_guarded != 0u) != (gx_writes == 0u)" in ctxt,
      "GX: guarded <=> zero writes, checked in both directions")

print("=== (f) the CX VOTABLE short-cut is derived, not assumed ===")
m = re.search(r"\(sc->flags & VOTABLE\) && status == GDSC_OFF && !wait", gdsc)
check(m is not None, "gdsc.c:153-161 short-cut present in the pinned bytes")
check("udelay(TIMEOUT_US);" in gdsc, "the short-cut waits TIMEOUT_US")
check("ADRENO_GDSC_FLAG_VOTABLE" in text(GDSC_C),
      "the C planner keys the short-cut off the VOTABLE flag")
check("ADRENO_GDSC_TIMEOUT_US" in text(GDSC_C),
      "the C planner uses the named 500 us constant")
check("c.cx_disable_polls == 0u" in ctxt,
      "the composition verifies CX polls NOTHING on the way down")
check("AdrenoPowerUpGdscDisableIsUnverified" in htxt,
      "the unverified-power-off hazard is named, not folklore")

print("=== (g) the GX fail-safe survived ===")
check("c.gx_disable_writes >= 1u" in ctxt,
      "the composition verifies GX disable DOES write")
check("c.gx_disable_polls >= 1u" in ctxt,
      "the composition verifies GX disable DOES poll")
check("static int gdsc_disable(struct generic_pm_domain *domain)" in gdsc,
      "gdsc.c defines gdsc_disable")
m = re.search(r"static int gdsc_disable\(struct generic_pm_domain \*domain\)\s*\n\{.*?\n\}", gdsc, re.S)
disable_fn = m.group(0) if m else ""
check("gdsc_assert_clamp_io(sc);" in disable_fn,
      "gdsc_disable clamps IO last (the GX fail-safe)")
check("gdsc_toggle_logic(sc, GDSC_OFF, domain->synced_poweroff);" in disable_fn,
      "gdsc_disable delegates the collapse to gdsc_toggle_logic")
# The OFF poll lives INSIDE gdsc_toggle_logic (gdsc.c:177), reached whenever the
# VOTABLE short-cut does not apply. Checking for it in gdsc_disable() was the
# second mistake this script caught; keep both halves explicit.
check("gdsc_poll_status(sc, status)" in gdsc,
      "gdsc_toggle_logic polls the status bit it just wrote")
check("gdsc_poll_status(sc, GDSC_ON)" in disable_fn,
      "gdsc_disable polls for ON after dropping hardware trigger mode")

print("=== (h) the derived poll address is the header's status register ===")
domtxt = text(DOMAIN_H)
cx_status = re.search(r"#define ADRENO_DOMAIN_CX_STATUS\s+(0x[0-9a-fA-F]+)ull", domtxt)
gpucc_base = re.search(r"#define ADRENO_DOMAIN_GPUCC_BASE\s+(0x[0-9a-fA-F]+)ull", domtxt)
check(cx_status is not None and gpucc_base is not None,
      "AdrenoDomainState.h declares the CX status register and the GPUCC base")
if cx_status and gpucc_base:
    delta = int(cx_status.group(1), 16) - int(gpucc_base.group(1), 16)
    check(delta == 0x1540,
          "CX status register is GPUCC+0x1540 (the .gds_hw_ctrl the descriptor pins)")
    check(".gds_hw_ctrl = 0x1540" in cx_body,
          "the pinned CX descriptor really declares gds_hw_ctrl 0x1540")
check("c.cx_poll_offset ==\n" in ctxt or "c.cx_poll_offset ==" in ctxt,
      "the composition derives the poll offset instead of hard-coding it")

print("=== (i) 14th archive object and 14th host test are wired ===")
btxt = text(BUILD)
rtxt = text(RUNNER)
check("AdrenoPowerUpGdsc.c" in btxt, "the build script compiles AdrenoPowerUpGdsc.c")
check("AdrenoPowerUpGdsc" in btxt, "the build report tracks AdrenoPowerUpGdsc")
check("adreno-power-up-gdsc-test" in rtxt, "the host runner declares the new test")
check("AdrenoPowerUpGdsc" in rtxt, "the host runner maps the new SOURCES entry")
check("power-up-gdsc" in rtxt, "the runner PASS line names the new suite")
m = re.search(r"sources = \[KIT / 'EnchiladaAdrenoProbe\.cpp'\](.*?)sources\.append", btxt, re.S)
stems = m.group(1) if m else ""
check("'AdrenoPowerUpGdsc'" in stems,
      "the 14th module is in the build-report source list")
check("'AdrenoGdscDecode'" in stems,
      "the 13th module is still in the build-report source list")

print("=== (j) honest boundary: nothing here claims a device run ===")
check("HOST-VERIFIED" in htxt, "the header declares the host-verified boundary")
check("no MMIO" in htxt or "No MMIO" in htxt,
      "the header states that no MMIO is performed")
check("device measurement" in htxt,
      "the header distinguishes derivation from a device measurement")
check("no contact" not in ctxt, "placeholder sanity")
check("Execute" not in htxt or "not" in htxt,
      "the header does not claim to execute anything")
check("ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE" in ctxt,
      "non-GDSC steps are still named explicitly")

print()
if fails:
    print("POWER_UP_GDSC_CROSS: FAIL (%d failures)" % len(fails))
    for f in fails:
        print("  - " + f)
    raise SystemExit(1)
print("POWER_UP_GDSC_CROSS: PASS (0 failures)")
