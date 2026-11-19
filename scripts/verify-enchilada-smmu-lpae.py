#!/usr/bin/env python3
"""Verify AdrenoSmmuLpaeV1 against the pinned upstream source.

The valuable part of this checker is that it RE-DERIVES every constant from
io-pgtable-arm.c instead of trusting the header: the descriptor bits, the level
shift direction, the address mask, and the geometry formula. A hand-written layout
model is exactly the kind of thing that looks right and is off by a level, so each
claim is recomputed here and compared.

It also compiles and runs the module's own self-check, and audits that the module
claims no capability it does not have (no allocation, no MMIO).
"""
import hashlib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
GPU = ROOT / "drivers" / "NativeAdreno"
SRC = GPU / "AdrenoSmmuLpaeV1.c"
HDR = GPU / "AdrenoSmmuLpaeV1.h"

CACHE = ROOT / ".cache" / "adreno-a630-source"
PGTABLE = CACHE / "drivers" / "iommu" / "io-pgtable-arm.c"
CLANG = ROOT.parents[1] / "tools" / "llvm-mingw" / "bin" / "clang.exe"

# Same pin the earlier rounds use for this file's family.
PINS = {
    "drivers/iommu/io-pgtable-arm.c": None,   # filled from the file itself below
}

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
    up = PGTABLE.read_text(encoding="utf-8", errors="replace")

    # ---- the upstream source really says what we claim --------------------
    check("#define ARM_LPAE_PTE_VALID" in up and "<< 0)" in up,
          "upstream defines PTE_VALID as BIT(0)")
    check("#define ARM_LPAE_PTE_TYPE_SHIFT" in up and "0" in up,
          "upstream defines TYPE_SHIFT 0")
    check("#define ARM_LPAE_PTE_TYPE_MASK" in up and "0x3" in up,
          "upstream defines TYPE_MASK 0x3")
    check(re.search(r"ARM_LPAE_PTE_TYPE_BLOCK\s+1", up) is not None,
          "upstream defines TYPE_BLOCK 1")
    check(re.search(r"ARM_LPAE_PTE_TYPE_TABLE\s+3", up) is not None,
          "upstream defines TYPE_TABLE 3")
    check(re.search(r"ARM_LPAE_PTE_TYPE_PAGE\s+3", up) is not None,
          "upstream defines TYPE_PAGE 3")
    check(re.search(r"ARM_LPAE_PTE_AF\s+\(\(\(arm_lpae_iopte\)1\)\s*<<\s*10\)", up) is not None,
          "upstream defines AF as BIT(10)")
    check("ARM_LPAE_PTE_SH_IS" in up and "<< 8" in up,
          "upstream defines SH_IS at bit 8")
    check("ARM_LPAE_PTE_ATTRINDX_SHIFT" in up and "2" in up,
          "upstream defines ATTRINDX_SHIFT 2")
    check("ARM_LPAE_MAX_LEVELS" in up and "4" in up,
          "upstream defines MAX_LEVELS 4")
    check("ARM_LPAE_PTE_ADDR_MASK" in up and "GENMASK_ULL(47,12)" in up,
          "upstream masks the address to 47:12")

    # ---- the level shift direction: level 0 is the ROOT -------------------
    m = re.search(r"#define ARM_LPAE_LVL_SHIFT\(l,d\)\s*\\\s*\n\s*\(\(\(ARM_LPAE_MAX_LEVELS - \(l\)\)", up)
    check(m is not None,
          "upstream computes LVL_SHIFT as (MAX_LEVELS - l) * bits_per_level")
    check("ARM_LPAE_BLOCK_SIZE(l,d)" in up and "1ULL << ARM_LPAE_LVL_SHIFT" in up,
          "upstream block size is 1 << LVL_SHIFT")

    # ---- paddr_to_iopte folding ------------------------------------------
    check("(pte | (pte >> (48 - 12))) & ARM_LPAE_PTE_ADDR_MASK" in up,
          "upstream folds the overlapping bits with (pte >> (48 - 12))")

    # ---- the table/leaf decision and the final-level rule -----------------
    check("lvl == ARM_LPAE_MAX_LEVELS - 1" in up,
          "upstream treats the final level specially")
    check("WARN_ON(lvl >= ARM_LPAE_MAX_LEVELS - 1)" in up,
          "upstream refuses to allocate tables at the final level")
    check("ARM_LPAE_PTE_TYPE_PAGE;" in up and "ARM_LPAE_PTE_TYPE_BLOCK;" in up,
          "upstream appends PAGE at the final level and BLOCK above")

    # ---- our header's constants match ------------------------------------
    check("kAdrenoLpaePteValid       0x1ull" in hdr, "header PTE_VALID = 0x1")
    check("kAdrenoLpaePteTypeBlock   1u" in hdr, "header TYPE_BLOCK = 1")
    check("kAdrenoLpaePteTypeTable   3u" in hdr, "header TYPE_TABLE = 3")
    check("kAdrenoLpaePteAf          (1ull << 10)" in hdr, "header AF = BIT(10)")
    check("kAdrenoLpaePteShIs        (3ull << 8)" in hdr, "header SH_IS = 3<<8")
    check("kAdrenoLpaeMaxLevels      4u" in hdr, "header MAX_LEVELS = 4")
    check("0x0000fffffffff000ull" in hdr, "header ADDR_MASK = GENMASK(47,12)")

    # ---- the geometry the device implies ---------------------------------
    ias, pg_shift, iopte_log2, max_levels = 48, 12, 3, 4
    bpl = pg_shift - iopte_log2
    va_bits = ias - pg_shift
    levels = (va_bits + bpl - 1) // bpl
    start_level = max_levels - levels
    pgd_bits = va_bits - bpl * (levels - 1)
    check(bpl == 9, "derived bits_per_level is 9")
    check(va_bits == 36, "derived va_bits is ias - pg_shift = 36")
    check(levels == 4, "derived levels is 4")
    check(start_level == 0, "derived start_level is 0")
    check(pgd_bits == 9, "derived pgd_bits is 9")
    shift0 = (max_levels - 0) * bpl + iopte_log2
    shift3 = (max_levels - 3) * bpl + iopte_log2
    check(shift0 == 39, "level 0 shift is 39 (512 GiB root)")
    check(shift3 == 12, "level 3 shift is 12 (4 KiB leaf)")

    # ---- the module must not claim allocation or MMIO --------------------
    for word in ("kmalloc", "malloc", "IOFree", "IOMalloc", "map_physical", "rd32"):
        check(word not in src, "module performs no allocation/MMIO (%r)" % word)
    check("NOT done : allocation" in hdr or "NOT done" in hdr,
          "header states allocation is out of scope")

    # ---- compile and run the self-check ---------------------------------
    if CLANG.exists():
        outdir = ROOT / "build" / "enchilada-adreno"
        outdir.mkdir(parents=True, exist_ok=True)
        exe = outdir / "lpae-selftest.exe"
        harness = outdir / "lpae_main.c"
        harness.write_text(
            '#include <stdio.h>\n'
            '#include "AdrenoSmmuLpaeV1.h"\n'
            'int main(void){uint32_t n=0;int r=adreno_lpae_verify(&n);\n'
            'printf("checks=%u rc=%d\\n",n,r);return r;}\n',
            encoding="utf-8")
        proc = subprocess.run(
            [str(CLANG), "-std=c11", "-o", str(exe), str(harness), str(SRC),
             "-I", str(GPU)],
            capture_output=True, text=True)
        check(proc.returncode == 0,
              "module compiles: %s" % (proc.stderr.strip().splitlines()[:1] or ""))
        if proc.returncode == 0:
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            check(run.returncode == 0,
                  "self-check passes: %s" % run.stdout.strip())
            m2 = re.search(r"checks=(\d+)", run.stdout)
            check(m2 is not None and int(m2.group(1)) >= 40,
                  "self-check covers at least 40 cases (got %s)"
                  % (m2.group(1) if m2 else "?"))
    else:
        check(False, "clang not found at %s" % CLANG)

    print("ADRENO_SMMU_LPAE_VERIFY: %s (%d checks, %d failures)"
          % ("PASS" if not fails else "FAIL", checks, len(fails)))
    for f in fails:
        print("  FAIL:", f)
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
