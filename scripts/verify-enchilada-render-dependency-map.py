#!/usr/bin/env python3
"""ROUND 104 — cross-verifier for the "到 Adreno 出图" dependency map.

The map (evidence/stage5-gpu-render-dependency-map.md) is a RESEARCH / INVENTORY
document: no GPU code, no flash, no reboot, no device run, no CI dispatch. This
verifier makes it *checkable* rather than merely plausible:

  Domain A - the document's own contract (status, path aliases, all nine
             sections, the two-target split, the CoreImage honest correction,
             the three investigated questions, the K1..K8 / U1..U4 tables,
             the per-segment verdicts, the ROUND 103 technical debt, the
             admission table, the evidence index, LF / no BOM).
  Domain B - every load-bearing literal it cites is present in the cited
             source file (37 cross-file anchors + 1 absence anchor). A map that quotes a string
             which does not exist is not a map.
  Domain C - the numeric / honesty invariants: CONFIG_XNU_NATIVE_ADRENO=n,
             9 modelled / 0 satisfied / BLOCKED, coherent_walk=1 -> b5100080
             (a0100080 is only a placeholder), and the three verdict kinds.

It does not run hardware, does not build anything, does not change a verdict.
"""
import pathlib
import re
import sys

DEV = pathlib.Path(__file__).resolve().parents[1]
ONEPLUS = DEV.parents[1]
UP = DEV.parent / "UniXNU"

MAP = DEV / "evidence" / "stage5-gpu-render-dependency-map.md"

checks = 0
fails = 0


def ck(cond, label):
    global checks, fails
    checks += 1
    if cond:
        print(f"  ok  {label}")
    else:
        fails += 1
        print(f"FAIL  {label}")


def sec(name):
    print(f"\n=== {name} ===")


def main():
    if not MAP.exists():
        print(f"MISSING MAP: {MAP}")
        return 1
    raw = MAP.read_bytes()
    doc = raw.decode("utf-8", errors="replace")

    # ------------------------------------------------------------------ A
    sec("A. document contract (research-only, self-contained)")
    ck(len(raw) > 5000, "the map is a substantial document (>5000 bytes)")
    ck(b"\r" not in raw, "pure LF (no CR bytes)")
    ck(not raw.startswith(b"\xef\xbb\xbf"), "no UTF-8 BOM")
    ck("**Status: HOST-VERIFIED." in doc, "status is HOST-VERIFIED")
    ck("RESEARCH / INVENTORY ONLY" in doc, "status says RESEARCH / INVENTORY ONLY")
    ck("no GPU code was written" in doc, "no GPU code was written")
    ck("no flash" in doc, "no flash")
    ck("no reboot" in doc, "no reboot")
    ck("no device run" in doc, "no device run")
    ck("no CI dispatch" in doc, "no CI dispatch")
    ck("no MMIO write" in doc, "no MMIO write")
    ck("`CONFIG_XNU_NATIVE_ADRENO` remains `n`" in doc,
       "CONFIG_XNU_NATIVE_ADRENO remains n")
    ck('STAYS `"host-verified"`' in doc, "gpu area STAYS host-verified")
    ck("does **not** unblock stage 5" in doc, "explicitly does not unblock stage 5")
    ck("It is a map, not a step." in doc, "declares itself a map, not a step")

    sec("A. path aliases + ACP ref note")
    ck("`ONEPLUS/` = `oneplus6-ios/`" in doc, "ONEPLUS/ alias defined")
    ck("`DEV/` = `oneplus6-ios/work/UniXNU-enchilada/`" in doc, "DEV/ alias defined")
    ck("`GPU/` = `DEV/drivers/NativeAdreno/`" in doc, "GPU/ alias defined")
    ck("`EV/` = `DEV/evidence/`" in doc, "EV/ alias defined")
    ck("`SCR/` = `DEV/scripts/`" in doc, "SCR/ alias defined")
    ck("ACP message ids" in doc, "mNNNNN refs explained as ACP ids, not instructions")

    sec("A. all nine sections present")
    for i in range(10):
        ck(f"## {i}." in doc, f"section ## {i}. is present")

    sec("A. section 0 - two DIFFERENT targets, not conflated")
    ck("two DIFFERENT targets" in doc, "targets explicitly separated")
    ck('(A) "UI' in doc and "(B)" in doc, "targets labelled (A) and (B)")
    ck("Adreno 真的参与渲染" in doc, "the literal user objective (B) is quoted")
    ck("Adreno involved?" in doc, "the Adreno-involved question is asked")
    ck("**NO** | **YES**" in doc, "(A)=NO / (B)=YES answer given")
    ck("[VERDICT: the goal is (B)" in doc, "goal is (B), (A) only a bypass")
    ck("enchilada renders on the CPU" in doc, "current desktop is CPU-rendered")

    sec("A. section 1 - HONEST CORRECTION of the CoreImage crash")
    ck("## 1. HONEST CORRECTION" in doc, "honest-correction section exists")
    ck("HISTORICAL, unverified external inference" in doc,
       "the CoreImage crash is marked unverified")
    ck("PerfPowerServices sig6" in doc, "the real CRASH rows are named")
    ck("no literal evidence in 246 decoded" in doc,
       "the 246-log negative result is stated")
    ck("NOT to an unverified crash signature" in doc,
       "the userland anchor is the measured state, not the crash")
    ck("[gpu clients]" in doc and "[bo pool] 0 BOs" in doc,
       "the measured empty GPU state is the anchor")

    sec("A. section 2 - the three investigated facts")
    ck("## 2. The three facts that had to be investigated" in doc,
       "three-question section exists")
    for q in ("①", "②", "③"):
        ck(f"| {q} |" in doc, f"question {q} is tabulated")
    ck("PIVOT" in doc, "at least one PIVOT verdict")
    ck("BLOCKED" in doc, "at least one BLOCKED verdict")
    ck("CONTINUE" in doc, "at least one CONTINUE verdict")

    sec("A. sections 3/4 - kernel K1..K8 and userland U1..U4 tables")
    for k in range(1, 9):
        ck(f"| K{k} |" in doc, f"kernel segment K{k} is tabulated")
    for u in range(1, 5):
        ck(f"| U{u} |" in doc, f"userland segment U{u} is tabulated")
    ck("For another GPU generation" in doc,
       "upstream generation-first ordering rule is cited")
    ck("HARD BLOCKER #1" in doc and "HARD BLOCKER #2" in doc,
       "the hard blockers are numbered")

    sec("A. section 5 - Branch A precedents registered, not recommended")
    ck("## 5. Branch A" in doc, "Branch A section exists")
    ck("bergamota" in doc and "berga-tools.zip" in doc, "bergamota precedent cited")
    ck("HackiOS" in doc and "libredwood-gl.dylib" in doc,
       "HackiOS / scion precedent cited")
    ck("softpipe" in doc, "softpipe (CPU) nature of the bypass is named")
    ck("[VERDICT: PIVOT a *separate* work-item for Branch A" in doc,
       "Branch A is a separate work-item, not the goal")

    sec("A. section 6 - per-segment verdict + single smallest next action")
    ck("## 6. Per-segment verdict" in doc, "per-segment verdict section exists")
    ck("Minimal next action" in doc, "a minimal-next-action column exists")
    ck("1 of 12" in doc, "the '1 of 12' increment is stated honestly")
    ck("does not, by itself, shorten the distance to (B)" in doc,
       "field-level progress is not oversold as reaching (B)")
    ck("EITHER (i) continue I1" in doc and "(ii) escalate K3" in doc,
       "the two informed next-round choices are offered")

    sec("A. section 7 - ROUND 103 technical debt must not be lost")
    ck("## 7. Technical debt" in doc, "technical-debt section exists")
    ck("coherent_walk = 1" in doc, "device-measured coherent_walk=1 recorded")
    ck("b5100080" in doc, "the real coherent TCR b5100080 recorded")
    ck("a0100080" in doc, "the placeholder a0100080 recorded")
    ck("PLACEHOLDER" in doc, "a0100080 explicitly labelled a placeholder")
    ck("ANY later LPAE page table" in doc,
       "the inherit-this-fact instruction for a future I1 round")
    ck("NEVER the stage-4" in doc, "the never-use-the-placeholder rule")

    sec("A. section 8 - admission table + HONEST BOUNDARY")
    ck("## 8. Admission table" in doc, "admission section exists")
    ck("9 modelled" in doc, "9 modelled gates recorded")
    ck("3 unmodeled" in doc, "3 unmodeled prereqs recorded")
    ck("modeled=9 /" in doc, "modeled=9 stated")
    ck("satisfied=0 / BLOCKED" in doc, "satisfied=0 / BLOCKED stated")
    ck("config-on" in doc, "config-on prereq named")
    ck("**HONEST BOUNDARY:**" in doc, "honest-boundary paragraph present")
    ck("does not unblock stage 5" in doc, "boundary restates no stage-5 unlock")

    sec("A. section 9 - evidence index")
    ck("## 9. Evidence index" in doc, "evidence-index section exists")
    ck("every assertion above is greppable" in doc,
       "index claims greppability (checked in Domain B)")
    ck("m04481" in doc and "m04487" in doc, "the direction refs are cited")
    ck("c27d7fd85dbed35c8555ba1b11ebbaa394a493d9" in doc,
       "the remote main at time of writing is recorded")

    # ------------------------------------------------------------------ B
    sec("B. every cited literal actually exists in its source file")
    pairs = [
        ("userland CPU", DEV/"NOTES-userland.md", "enchilada renders on the CPU"),
        ("feas inference", DEV/"evidence"/"stage5-gpu-feasibility.md", "HISTORICAL, unverified external inference"),
        ("feas sig6", DEV/"evidence"/"stage5-gpu-feasibility.md", "PerfPowerServices sig6"),
        ("ci macos26", ONEPLUS/"notes"/"enchilada"/"ci-kernel.yml", "runs-on: macos-26"),
        ("ci xcode", ONEPLUS/"notes"/"enchilada"/"ci-kernel.yml", "Xcode_"),
        ("metal publish wf sha", DEV/"evidence"/"stage5-gpu-metal-publish.md", "6629ee5180434b7ba8d638f50542ecd83b1f373240a0efcf4c28f7483fe7bb0b"),
        ("gpu.md generation", UP/"docs"/"porting"/"gpu.md", "For another GPU generation"),
        ("zap 1of8", DEV/"evidence"/"stage5-gpu-zap-auth.md", "one of eight"),
        ("smmu no mmio", DEV/"evidence"/"stage5-gpu-smmu-stage1.md", "Computing the words is not programming them. There is no MMIO, no mapping, no page"),
        ("stage6 b5100080", DEV/"evidence"/"stage5-gpu-stage6-device.md", "b5100080"),
        ("stage6 a0100080", DEV/"evidence"/"stage5-gpu-stage6-device.md", "a0100080"),
        ("gen1 9 modelled", DEV/"drivers"/"NativeAdreno"/"AdrenoGen1BackendV1.c", "9 modelled"),
        ("gen1 blocked", DEV/"drivers"/"NativeAdreno"/"AdrenoGen1BackendV1.c", "0 satisfied, BLOCKED"),
        ("defconfig adreno n", DEV/"configs"/"enchilada_defconfig", "CONFIG_XNU_NATIVE_ADRENO=n"),
        ("defconfig legacy y", DEV/"configs"/"enchilada_defconfig", "CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y"),
        ("board cpu present", DEV/"board.toml", "offline-legacy-cpu-present=1"),
        ("kgsl never rendered", DEV/"evidence"/"stage5-gpu-kgsl-sysfs.md", "never actually rendered anything"),
        ("bergamota gpu accel", ONEPLUS/"notes"/"refs"/"bergamota-README.md", "GPU acceleration on the Adreno 630"),
        ("bergamota tools", ONEPLUS/"notes"/"refs"/"bergamota-README.md", "berga-tools.zip"),
        ("scion softpipe", ONEPLUS/"notes"/"scion"/"GPU.md", "Without a GPU it's softpipe"),
        ("darwin kmd GPUId630", DEV/"evidence"/"stage5-gpu-darwin-kmd-abi.md", "GPUId(630)"),
        ("metal seam userclient", DEV/"evidence"/"stage5-gpu-metal-seam.md", "not reachable from any IOKit user client"),
        ("display gpu handoff", DEV/"drivers"/"NativeMobileDisplay"/"README.md", "not used for a logical display"),
        ("caller no IOService", DEV/"evidence"/"stage5-gpu-caller-v1.md", "The IOKit `IOService` itself is not written"),
        ("ci239 forbid", DEV/"evidence"/"stage5-gpu-ci-239.md", "_IOConnectCallMethod"),
        ("a630 renders nothing", DEV/"evidence"/"stage5-gpu-a630-registers.md", "renders nothing"),
        ("kgsl globals 17", DEV/"evidence"/"stage5-gpu-kgsl-globals.md", "17 entries"),
        ("kgsl globals iova", DEV/"evidence"/"stage5-gpu-kgsl-globals.md", "exactly `0x0-0x0` with a"),
        ("ci246 forbid", DEV/"scripts"/"verify-ci-246-adreno.py", "_qcom_scm_call"),
        ("presenter IOService", DEV/"drivers"/"NativeMobileDisplay"/"NativeGOPPresenter.cpp", "class NativeGOPPresenter : public IOService"),
        ("presenter IOUserClient", DEV/"drivers"/"NativeMobileDisplay"/"NativeGOPPresenter.cpp", "class NativeGOPClient : public IOUserClient"),
        ("gop wire handoff", DEV/"drivers"/"NativeMobileDisplay"/"NativeGOPWire.h", "offers a finished frame instead of copying it"),
        ("gop wire nothing copies", DEV/"drivers"/"NativeMobileDisplay"/"NativeGOPWire.h", "Nothing here copies pixels"),
        ("presenter not set", DEV/"scripts"/"build-native-legacy-display.py", "enchilada does not set"),
        ("legacy IM accel absent", DEV/"drivers"/"NativeMobileDisplay"/"NativeLegacyDisplay.cpp", "which does not exist here (backboardd aborts"),
        ("gputrace diag abi", DEV/"drivers"/"NativeMobileDisplay"/"NativeGpuTrace.h", "Diagnostic ABI, independent of rendering policy"),
        ("mobile proto only", DEV/"drivers"/"NativeMobileDisplay"/"NativeMobileDisplay.cpp", "Prototype only. No registry publication or display operation is enabled"),
    ]
    for label, path, literal in pairs:
        ok = path.exists() and literal in path.read_text(encoding="utf-8", errors="replace")
        ck(ok, f"{label}: '{literal[:48]}'")
    board_text = (DEV/"board.toml").read_text(encoding="utf-8")
    ck("sheng-fb-gpu" not in board_text,
       "board.toml does NOT arm sheng-fb-gpu (hand-off is written but unarmed)")

    # ------------------------------------------------------------------ C
    sec("C. numeric / honesty invariants")
    defc = (DEV/"configs"/"enchilada_defconfig").read_text(encoding="utf-8")
    board = (DEV/"board.toml").read_text(encoding="utf-8")
    gen1 = (DEV/"drivers"/"NativeAdreno"/"AdrenoGen1BackendV1.c").read_text(encoding="utf-8")
    stage6 = (DEV/"evidence"/"stage5-gpu-stage6-device.md").read_text(encoding="utf-8")
    ck("CONFIG_XNU_NATIVE_ADRENO=n" in defc, "defconfig really has ADRENO=n")
    ck("CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y" in defc,
       "defconfig really has the CPU display =y")
    ck("offline-legacy-cpu-present=1" in board, "board.toml arms CPU present")
    ck("9 modelled" in gen1 and "0 satisfied, BLOCKED" in gen1,
       "the admission-table source really says 9/0/BLOCKED")
    ck("b5100080" in stage6 and "a0100080" in stage6,
       "the device evidence holds BOTH the real and placeholder TCR")
    ck("coherent_walk = 1 / b5100080" in doc,
       "the map's debt rule uses the real coherent TCR")
    ck("unverified" in doc and "CI::GLContext::GLContext" in doc,
       "the CoreImage crash is present but explicitly unverified")
    ck(all(v in doc for v in ("CONTINUE", "BLOCKED", "PIVOT")),
       "all three verdict kinds are used")
    ck("_qcom_scm_call" in (DEV/"scripts"/"verify-ci-246-adreno.py").read_text(encoding="utf-8"),
       "the signing chain really is forbidden in the CI image")
    ck(doc.count("## ") >= 10, "at least ten '## ' headings (0..9 + subheads)")
    ck(re.search(r"does not\s+change any verdict", doc) is not None,
       "the map does not claim to change any verdict")

    print(f"\nRENDER_DEPENDENCY_MAP_VERIFY: "
          f"{'PASS' if fails == 0 else 'FAIL'} ({checks} checks, {fails} failures)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
