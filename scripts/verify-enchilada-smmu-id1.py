#!/usr/bin/env python3
"""Verify AdrenoSmmuId1V1: the ID1 decode that supplies num_context_banks.

Runs the module's own self-check plus source audits that catch the mistakes this
file is most likely to contain: a wrong offset, a field wider than P1 says, a
"plausible" default that invents a bank count, and any claim to have read the
register when nothing did.

The pinned sources are re-hashed, so a modified upstream header cannot silently
change what "P1:63" means.
"""
import hashlib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
GPU = ROOT / "drivers" / "NativeAdreno"
SRC = GPU / "AdrenoSmmuId1V1.c"
HDR = GPU / "AdrenoSmmuId1V1.h"

CLANG = (ROOT.parents[1] / "tools" / "llvm-mingw" / "bin" / "clang.exe")

# Pinned upstream sources (same hashes AdrenoSmmuAsidV1 cites).
PINS = {
    "drivers/iommu/arm/arm-smmu/arm-smmu.h":
        "ed85998794695d1a824dc1b86f680c8d124546688fd2665e00e3cbcfa9546b53",
    "drivers/iommu/arm/arm-smmu/arm-smmu.c":
        "d5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a",
}
CACHE = ROOT / ".cache" / "adreno-a630-source"

fails = []
checks = 0


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)


def main():
    src = SRC.read_text(encoding="utf-8")
    hdr = HDR.read_text(encoding="utf-8")

    # ---- pinned provenance -------------------------------------------------
    for rel, want in PINS.items():
        path = CACHE / rel
        if not path.exists():
            check(False, "pinned source missing: %s" % rel)
            continue
        got = hashlib.sha256(path.read_bytes()).hexdigest()
        check(got == want, "pinned sha256 mismatch for %s" % rel)

    upstream_h = (CACHE / "drivers/iommu/arm/arm-smmu/arm-smmu.h").read_text(
        encoding="utf-8", errors="replace")
    upstream_c = (CACHE / "drivers/iommu/arm/arm-smmu/arm-smmu.c").read_text(
        encoding="utf-8", errors="replace")

    # ---- the constants really come from upstream ---------------------------
    check("ARM_SMMU_GR0_ID1" in upstream_h, "upstream declares ARM_SMMU_GR0_ID1")
    m = re.search(r"#define\s+ARM_SMMU_GR0_ID1\s+(0x[0-9a-fA-F]+)", upstream_h)
    check(m is not None and int(m.group(1), 16) == 0x24,
          "GR0_ID1 is 0x24 upstream")
    check("ARM_SMMU_ID1_NUMCB" in upstream_h, "upstream declares ID1_NUMCB")
    check("GENMASK(7, 0)" in upstream_h, "NUMCB is GENMASK(7,0)")
    check("ID1_NUMCB" in upstream_c, "upstream reads NUMCB in arm-smmu.c")

    # ---- our file agrees with upstream ------------------------------------
    check("0x0024u" in hdr, "header pins the 0x24 offset")
    check("0x000000ffu" in hdr, "header pins the eight-bit mask")

    # ---- the decode cannot invent a bank count ---------------------------
    check("& kAdrenoSmmuId1NumcbMask" in src, "decode masks with the pinned field")
    check("num_context_banks == 0u" in src, "zero NUMCB is detected")
    check("plausible = 0u" in src, "zero NUMCB is marked implausible")
    check("return 0u;\n    }\n    out->plausible = 1u;" in src,
          "a non-zero NUMCB is what marks it plausible")

    # ---- it must not claim a hardware read it did not make ----------------
    forbidden = ["read32", "rd32", "volatile uint32_t *", "mmio", "iomap"]
    for word in forbidden:
        check(word not in src,
              "module performs no MMIO of its own (found %r)" % word)
    # The value IS measured: id1=0x20008005 -> NUMCB=5 across 103 retained windows.
    # An earlier version of this verifier demanded the opposite wording, which was the
    # retracted claim; it now demands the measured one, so the false story cannot return
    # without failing here.
    check("MEASURED" in hdr, "header records the value as measured")
    check("0x20008005" in hdr, "header names the measured word")
    check("NUMCB=5" in hdr or "NUMCB = 5" in hdr, "header names the decoded count")
    check("section 28" in hdr, "header points at the correction that retracted the old claim")
    check("nobody has read ID1" not in hdr and "Nobody has read ID1" not in hdr,
          "header no longer claims ID1 was never read")

    # ---- compile and run the self-check ----------------------------------
    if CLANG.exists():
        out = ROOT / "build" / "enchilada-adreno" / "id1-selftest.exe"
        out.parent.mkdir(parents=True, exist_ok=True)
        harness = ROOT / "build" / "enchilada-adreno" / "id1_main.c"
        harness.write_text(
            '#include <stdio.h>\n'
            '#include "AdrenoSmmuId1V1.h"\n'
            'int main(void){uint32_t n=0;int r=adreno_smmu_id1_verify(&n);\n'
            'printf("checks=%u rc=%d\\n",n,r);return r;}\n',
            encoding="utf-8")
        compile_cmd = [str(CLANG), "-std=c11", "-o", str(out),
                       str(harness), str(SRC), "-I", str(GPU)]
        proc = subprocess.run(compile_cmd, capture_output=True, text=True)
        check(proc.returncode == 0,
              "module compiles: %s" % (proc.stderr.strip().splitlines()[:1] or ""))
        if proc.returncode == 0:
            run = subprocess.run([str(out)], capture_output=True, text=True)
            check(run.returncode == 0, "self-check passes: %s" % run.stdout.strip())
            m = re.search(r"checks=(\d+)", run.stdout)
            check(m is not None and int(m.group(1)) >= 16,
                  "self-check covers at least 16 cases (got %s)"
                  % (m.group(1) if m else "?"))
    else:
        check(False, "clang not found at %s" % CLANG)

    print("ADREno_SMMU_ID1_VERIFY: %s (%d checks, %d failures)"
          % ("PASS" if not fails else "FAIL", checks, len(fails)))
    for f in fails:
        print("  FAIL:", f)
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
