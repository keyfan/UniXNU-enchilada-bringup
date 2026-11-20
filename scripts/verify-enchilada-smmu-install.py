#!/usr/bin/env python3
"""Verify AdrenoSmmuInstallV1: CBn_BASE from ID1, and absolute context offsets.

Re-derives the base arithmetic from the pinned upstream source and from the MEASURED
ID1 word (0x20008005, seen in 103 retained windows), instead of trusting the module.

Two mistakes this checker exists to prevent, both of which I made while writing the
self-check:
  * treating offset 0 as "not found", when CBAR legitimately lives at 0;
  * a struct zeroing loop bounded by `sizeof p / sizeof p`, which is always 1.
"""
import io
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
GPU = ROOT / "drivers" / "NativeAdreno"
SRC = GPU / "AdrenoSmmuInstallV1.c"
HDR = GPU / "AdrenoSmmuInstallV1.h"
CACHE = ROOT / ".cache" / "adreno-a630-source"
SMMU_H = CACHE / "drivers" / "iommu" / "arm" / "arm-smmu" / "arm-smmu.h"
SMMU_C = CACHE / "drivers" / "iommu" / "arm" / "arm-smmu" / "arm-smmu.c"
CLANG = ROOT.parents[1] / "tools" / "llvm-mingw" / "bin" / "clang.exe"

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
    up_h = SMMU_H.read_text(encoding="utf-8", errors="replace")
    up_c = SMMU_C.read_text(encoding="utf-8", errors="replace")

    # ---- upstream field definitions -------------------------------------
    check("ARM_SMMU_ID1_PAGESIZE" in up_h and "BIT(31)" in up_h,
          "upstream defines ID1_PAGESIZE as BIT(31)")
    check("ARM_SMMU_ID1_NUMPAGENDXB" in up_h and "GENMASK(30, 28)" in up_h,
          "upstream defines NUMPAGENDXB as GENMASK(30,28)")
    check("ARM_SMMU_ID1_NUMCB" in up_h and "GENMASK(7, 0)" in up_h,
          "upstream defines NUMCB as GENMASK(7,0)")
    check("#define ARM_SMMU_CB(s, n)" in up_h and "numpage + (n)" in up_h,
          "upstream derives a bank from numpage + n")
    check("smmu->pgshift = (id & ARM_SMMU_ID1_PAGESIZE) ? 16 : 12" in up_c,
          "upstream picks pgshift 16 or 12 from PAGESIZE")
    check("FIELD_GET(ARM_SMMU_ID1_NUMPAGENDXB, id) + 1" in up_c,
          "upstream computes size = 1 << (NUMPAGENDXB + 1)")

    # ---- our header agrees ----------------------------------------------
    check("kAdrenoInstallId1Pagesize            (1u << 31)" in hdr,
          "header pins PAGESIZE = BIT(31)")
    check("kAdrenoInstallId1NumpagendxbShift    28u" in hdr,
          "header pins the NUMPAGENDXB shift")
    check("kAdrenoInstallId1NumcbMask           0xffu" in hdr,
          "header pins the NUMCB mask")
    check("kAdrenoInstallCbTtbr0                0x0020u" in hdr,
          "header pins CB_TTBR0 = 0x20")

    # ---- the measured word implies the base we claim --------------------
    id1 = 0x20008005
    numcb = id1 & 0xFF
    numpagendxb = (id1 >> 28) & 0x7
    pgshift = 16 if (id1 >> 31) & 1 else 12
    size = 1 << (numpagendxb + 1)
    cb_base = size << pgshift
    check(numcb == 5, "measured NUMCB is 5")
    check(numpagendxb == 2, "measured NUMPAGENDXB is 2")
    check(pgshift == 12, "measured PAGESIZE bit is clear -> pgshift 12")
    check(size == 8, "measured size is 8 pages")
    check(cb_base == 0x8000, "measured CBn_BASE is 0x8000")
    check(cb_base + 4 * (1 << pgshift) == 0xC000,
          "the fifth bank sits at 0xc000")

    # ---- the two bugs this round hit, asserted as rules -----------------
    # 1. offset zero must not be used as a "not found" sentinel for GR1 writes
    check("gr1_seen" in src,
          "a found-flag exists instead of using offset 0 as absence")
    # 2. no self-defeating loop bound. Strip comments first: the fix note quotes the
    # old expression on purpose, and a naive search would flag the explanation.
    def strip_comments(text):
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        return re.sub(r"//[^\n]*", "", text)

    src_code = strip_comments(src)
    check("sizeof p / sizeof p" not in src_code,
          "no struct-zeroing loop bounded by sizeof p / sizeof p")
    check("(uint32_t)sizeof p" in src_code or "sizeof p; ++i" in src_code,
          "the params struct is zeroed over its real size")

    # ---- the GR1/bank split is structural --------------------------------
    check("in[i].gr1 ? (uint64_t)in[i].offset" in src_code,
          "a GR1 offset keeps its own value")
    check("base + (uint64_t)in[i].offset" in src_code,
          "a bank-relative offset takes the base")
    check("cbndx >= geo->num_context_banks" in src_code,
          "a bank index at or beyond NUMCB is refused")

    # ---- no MMIO ---------------------------------------------------------
    for word in ("rd32", "write32", "volatile uint32_t *", "mmio"):
        check(word not in src, "module performs no MMIO (%r)" % word)

    # ---- compile and run -------------------------------------------------
    if CLANG.exists():
        outdir = ROOT / "build" / "enchilada-adreno"
        outdir.mkdir(parents=True, exist_ok=True)
        exe = outdir / "install-selftest.exe"
        harness = outdir / "install_main.c"
        harness.write_text(
            '#include <stdio.h>\n'
            '#include "AdrenoSmmuInstallV1.h"\n'
            'int main(void){uint32_t n=0;int r=adreno_install_verify(&n);\n'
            'printf("checks=%u rc=%d\\n",n,r);return r;}\n',
            encoding="utf-8")
        proc = subprocess.run(
            [str(CLANG), "-std=c11", "-Wall", "-Wextra", "-o", str(exe),
             str(harness), str(SRC), str(GPU / "AdrenoSmmuCtxWordV1.c"),
             "-I", str(GPU)],
            capture_output=True, text=True)
        check(proc.returncode == 0,
              "module compiles with -Wall -Wextra: %s"
              % (proc.stderr.strip().splitlines()[:1] or ""))
        if proc.returncode == 0:
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            check(run.returncode == 0, "self-check passes: %s" % run.stdout.strip())
            m = re.search(r"checks=(\d+)", run.stdout)
            check(m is not None and int(m.group(1)) >= 28,
                  "self-check covers at least 28 cases (got %s)"
                  % (m.group(1) if m else "?"))
    else:
        check(False, "clang not found at %s" % CLANG)

    print("ADRENO_SMMU_INSTALL_VERIFY: %s (%d checks, %d failures)"
          % ("PASS" if not fails else "FAIL", checks, len(fails)))
    for f in fails:
        print("  FAIL:", f)
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())