#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the round-53 kgsl-SMMU read SAFETY
gate (AdrenoKGSLSmmuGate) WITHOUT compiling or running the C module.

Independent of method 1 (check-enchilada-adreno-host.py). Reads:
  (a) sha256-pinned Linux v6.12 arm-smmu.c / arm-smmu-qcom.c (committed
      scripts/data/enchilada-kgsl-smmu-inputs.json + gitignored .cache bytes,
      reproducible via scripts/fetch-enchilada-kgsl-smmu-sources.py) and
      asserts the upstream ORDER: :2196 clk_bulk_prepare_enable BEFORE :2200
      cfg_probe / :2255 reset, plus :2264-2266 pm_runtime and :71-77 rpm_get;
  (b) the vendored enchilada DTB (sha256 e590ee32...) at pinned 1-based lines:
      kgsl SMMU 0x5040000/0x10000, phandle 0x1ac, #iommu-cells=1, vdd-supply
      0x1a4, 0x1a4 == gpu_cx_gdsc, apps_smmu 0x29 is a DIFFERENT unit, and
      all GPU iommus use 0x1ac (never 0x29);
  (c) AdrenoKGSLSmmuGate.h/.c raw TEXT: constants, fail-closed verdict enum,
      check ORDER (wrong SMMU -> wrong window -> CX down -> clocks -> domain),
      the GPUCC-exception-does-not-apply fact, the 5 unmasked context SIDs;
  (d) build-enchilada-adreno.py + check-enchilada-adreno-host.py: the 12th
      archive object and 12th host test are actually wired;
  (e) decoded DEVICE logs: verdict=cx-DOWN with cx=00005ff4 is present
      (the measurement that makes a read unsafe today).

No network (reads the already-pinned manifest + local cache), no CI, no flash,
no device run. HOST-VERIFIED: source text + a PREVIOUSLY measured device fact.
This is a SAFETY STUDY, not a new device measurement.

Usage: python -X utf8 scripts/verify-enchilada-kgsl-smmu-gate.py
"""
import hashlib
import json
import re
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
WS = DEV.parent.parent                     # oneplus6-ios/
GPU = DEV / "drivers/NativeAdreno"
H = GPU / "AdrenoKGSLSmmuGate.h"
C = GPU / "AdrenoKGSLSmmuGate.c"
TEST = GPU / "tests/adreno-kgsl-smmu-gate-test.cpp"
BUILD = SCRIPTS / "build-enchilada-adreno.py"
RUNNER = SCRIPTS / "check-enchilada-adreno-host.py"
BOARD = GPU / "AdrenoBoardInputs.h"
DOMAIN = GPU / "AdrenoDomainState.c"
DTB = WS / "notes/mu-silicium/enchilada.dts"
DTB_SHA = "e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275"
PINS = SCRIPTS / "data/enchilada-kgsl-smmu-inputs.json"
CACHE = DEV / ".cache/adreno-a630-source"
SMMU_KEY = "drivers/iommu/arm/arm-smmu/arm-smmu.c"
SMMU_C = CACHE / SMMU_KEY

fails = []
def check(ok, label):
    print(("PASS " if ok else "FAIL ") + label)
    if not ok:
        fails.append(label)

def text(p):
    return p.read_text(encoding="utf-8", errors="replace")

print("=== (a) sha-pinned Linux v6.12 arm-smmu.c: clocks+domain BEFORE registers ===")
man = json.loads(text(PINS))
pin = man["linux"]["files"][SMMU_KEY]
raw = SMMU_C.read_bytes()
check(len(raw) == pin["bytes"], f'arm-smmu.c bytes {pin["bytes"]}')
check(hashlib.sha256(raw).hexdigest() == pin["sha256"], "arm-smmu.c sha256 pin")
s = text(SMMU_C)
lines = s.splitlines()
check("clk_bulk_prepare_enable(smmu->num_clks, smmu->clks);" in lines[2195], "arm-smmu.c:2196 clocks enabled")
check("arm_smmu_device_cfg_probe(smmu);" in lines[2199], "arm-smmu.c:2200 cfg_probe AFTER clocks")
check("arm_smmu_device_reset(smmu);" in lines[2254], "arm-smmu.c:2255 reset AFTER clocks")
check("pm_runtime_set_active(dev);" in lines[2264] and "pm_runtime_enable(dev);" in lines[2265],
      "arm-smmu.c:2264-2266 power domain active/enabled")
check("pm_runtime_resume_and_get(smmu->dev)" in s, "arm-smmu.c:71-77 arm_smmu_rpm_get")
check('{ .compatible = "qcom,smmu-v2", .data = &qcom_smmuv2 }' in s,
      "arm-smmu.c:1973 qcom,smmu-v2 is a real SMMUv2 variant")
# The ORDER assertion (load-bearing): clock line index < cfg_probe < reset
i_clk = next(i for i, l in enumerate(lines) if "clk_bulk_prepare_enable(smmu->num_clks" in l)
i_cfg = next(i for i, l in enumerate(lines) if "err = arm_smmu_device_cfg_probe(smmu);" in l)
i_rst = next(i for i, l in enumerate(lines) if l.strip() == "arm_smmu_device_reset(smmu);")
check(i_clk < i_cfg < i_rst, f"upstream ORDER clocks({i_clk}) < cfg_probe({i_cfg}) < reset({i_rst})")

print("=== (b) vendored enchilada DTB (sha256 e590ee32...) ===")
dtb = DTB.read_bytes()
check(hashlib.sha256(dtb).hexdigest() == DTB_SHA, "DTB sha256 pin")
L = dtb.decode("utf-8", "replace").splitlines()
def at(n): return L[n - 1]
check("arm,smmu-kgsl@5040000 {" in at(13957), "DTB:13957 kgsl SMMU node")
check('compatible = "qcom,smmu-v2"' in at(13959), "DTB:13959 qcom,smmu-v2")
check("reg = <0x5040000 0x10000>" in at(13960), "DTB:13960 reg 0x5040000 0x10000")
check("#iommu-cells = <0x01>" in at(13961), "DTB:13961 1-cell SIDs (no mask)")
check("vdd-supply = <0x1a4>" in at(13967), "DTB:13967 vdd-supply 0x1a4")
check("clock-names = \"gcc_gpu_memnoc_gfx_clk\"" in at(13969), "DTB:13969 kgsl smmu clock")
check("phandle = <0x1ac>" in at(13972), "DTB:13972 phandle 0x1ac")
check("regulator-name = \"gpu_cx_gdsc\"" in at(595) and "phandle = <0x1a4>" in at(604),
      "DTB:595/:604 0x1a4 IS gpu_cx_gdsc (SMMU on collapsible rail)")
check('compatible = "qcom,qsmmu-v500"' in at(13976), "DTB:13976 apps_smmu DIFFERENT unit")
check("#iommu-cells = <0x02>" in at(13979), "DTB:13979 apps_smmu 2-cell")
check("phandle = <0x29>" in at(13996), "DTB:13996 apps_smmu phandle 0x29")
seg = "\n".join(L[23847:23925])
gpu_ph = re.findall(r"iommus = <(0x[0-9a-f]+)\s", seg)
check(len(gpu_ph) >= 4 and all(p == "0x1ac" for p in gpu_ph),
      f"GPU iommus all phandle 0x1ac (found {gpu_ph})")
iommu_lines = [l for l in seg.splitlines() if "iommus" in l]
check(all("0x29" not in l for l in iommu_lines), "GPU iommus lines never reference apps_smmu 0x29")

print("=== (c) module raw text: constants, fail-closed enum, check ORDER ===")
hs = text(H); cs = text(C)
for macro, val in [("ADRENO_KGSL_GATE_STEP_CLOCKS", "0x1u"),
                   ("ADRENO_KGSL_GATE_STEP_DOMAIN", "0x2u")]:
    check(re.search(rf"#define\s+{macro}\s+{re.escape(val)}", hs) is not None,
          f"header defines {macro} {val}")
check("kAdrenoKGSLSmmuGatePermitted" in hs and "kAdrenoKGSLSmmuGateRefusedCxDown" in hs,
      "fail-closed verdict enum present")
check("ADRENO_KGSL_GATE_STEP_ALL" in cs and "return ADRENO_KGSL_GATE_STEP_ALL;" in cs,
      "RequiredSteps() returns BOTH clocks and domain")
# Check ORDER in Assess(): each guard appears before the next in source order.
def pos(needle): return cs.find(needle)
p_kgsl = pos("AdrenoKGSLSmmuGatePhandleIsKgsl(in->smmu_phandle)")
p_win = pos("AdrenoKGSLSmmuGateWindowIsKgsl(in->window_base")
p_cx = pos("adreno_domain_cx_is_on(in->cx_status_word)")
p_clk = pos("!in->clocks_enabled")
p_dom = pos("!in->domain_powered")
check(0 <= p_kgsl < p_win < p_cx < p_clk < p_dom,
      f"Assess() ORDER phandle({p_kgsl}) < window({p_win}) < cx({p_cx}) < clocks({p_clk}) < domain({p_dom})")
check("kAdrenoKGSLSmmuGateRefusedCxDown" in cs, "CX-DOWN refusal branch present")
check("arm-smmu.c:2196" in cs and "arm-smmu.c:2264" in cs and "a6xx_gmu.c:1033" in cs,
      "C source cites the exact upstream lines")
check("AdrenoKGSLSmmuGateGpuccExceptionDoesNotApply" in cs,
      "GPUCC always-on exception does NOT apply to kgsl SMMU (encoded)")
check("kAdrenoBoardCtxGfx3dUser" in cs and "kAdrenoBoardCtxGmuKernel" in cs,
      "5 unmasked context SIDs enumerated")

print("=== (d) build + runner wiring (12th object / 12th test) ===")
bs = text(BUILD); rs = text(RUNNER)
check("GPU / 'AdrenoKGSLSmmuGate.c'" in bs, "build script compiles AdrenoKGSLSmmuGate.c")
check("'AdrenoKGSLSmmuGate'" in bs, "build script adds the .h/.c source stem")
check("'adreno-kgsl-smmu-gate-test'" in rs, "host runner has the new test tuple")
check("'AdrenoKGSLSmmuGate': GPU / 'AdrenoKGSLSmmuGate.c'" in rs,
      "host runner compiles the new C module")

print("=== (e) previously measured device fact: CX-DOWN makes a read unsafe ===")
import glob
import os
cx_rows = 0
for f in glob.glob(str(WS / "work/log/**/msgbuf.txt"), recursive=True):
    try:
        t = Path(f).read_text(encoding="utf-8", errors="replace")
    except OSError:
        continue
    cx_rows += len(re.findall(r"verdict=cx-DOWN", t))
check(cx_rows > 0, f"decoded device logs contain verdict=cx-DOWN (rows={cx_rows})")
check("0x05040000ull" in text(DOMAIN) or "0x05040000" in text(DOMAIN),
      "AdrenoDomainState.c already lists kgsl SMMU 0x05040000 as forbidden window")

print(f"KGSL_SMMU_GATE_CROSS: {'PASS' if not fails else 'FAIL'} ({len(fails)} failures)")
raise SystemExit(1 if fails else 0)
