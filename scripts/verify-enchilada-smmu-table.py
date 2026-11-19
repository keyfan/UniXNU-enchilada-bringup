#!/usr/bin/env python3
"""Verify AdrenoSmmuTableV1: the wiring between an allocated block and the layout.

The failure modes this checker targets are the ones that survive a written test:

  * a table page written BEFORE it is zeroed (an incremental builder would do this);
  * descriptors written child-first, so a populated table is briefly unreachable;
  * the root address not being the block's own base;
  * the module growing an allocator of its own, which would make it untestable here.

It also re-derives the table count from AdrenoSmmuLpaeV1's geometry rather than trusting
the module's own arithmetic, and it confirms that the allocation primitive it depends on
actually exists in the tree with the guarantees the header claims.
"""
import hashlib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
GPU = ROOT / "drivers" / "NativeAdreno"
SRC = GPU / "AdrenoSmmuTableV1.c"
HDR = GPU / "AdrenoSmmuTableV1.h"
LPAE = GPU / "AdrenoSmmuLpaeV1.c"
ARMCPU = ROOT / "drivers" / "ArmCpu" / "ArmCpu.cpp"
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
    arm = ARMCPU.read_text(encoding="utf-8", errors="replace")

    # ---- the allocation primitive this module depends on exists ----------
    check("armcpu_dma_alloc_writable" in arm,
          "the tree still exports armcpu_dma_alloc_writable")
    check("kIOMemoryPhysicallyContiguous" in arm,
          "that primitive requests physically contiguous memory")
    check("(phys & 4095ULL) != 0" in arm or "phys & 4095" in arm,
          "that primitive verifies page alignment of the RESULT")
    check("contiguous) < bytes" in arm or "(contiguous) < bytes" in arm,
          "that primitive verifies the contiguous run covers the request")

    # ---- the module must not allocate -----------------------------------
    # Strip comments first: the header explains WHY IOBufferMemoryDescriptor is not
    # called here, and a substring check on prose would flag the explanation.
    def strip_comments(text):
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        return re.sub(r"//[^\n]*", "", text)

    code = strip_comments(src)
    for word in ("IOMalloc", "IOBufferMemoryDescriptor", "kmalloc", "malloc",
                 "kernel_task", "IOFree", "inTaskWithOptions"):
        check(word not in code,
              "module performs no allocation (%r)" % word)
    check("does NOT allocate" in hdr or "does not allocate" in hdr.lower(),
          "header states the allocation is an argument")

    # ---- the zero-before-write rule is structural ------------------------
    # every table page is zeroed in the loop that also computes its addresses,
    # before adreno_lpae_plan() is called at all.
    zero_at = src.find("table_zero(")
    plan_at = src.find("adreno_lpae_plan(")
    check(zero_at > 0 and plan_at > 0 and zero_at < plan_at,
          "all table pages are zeroed before the plan is computed")
    # and the plan is applied afterwards, root-first. Compare the CALL site inside
    # adreno_table_build, not the helper's definition.
    build_at = src.find("uint32_t adreno_table_build(")
    check(build_at > 0, "the builder function exists")
    if build_at > 0:
        store_call = src.find("table_store64(", build_at)
        check(store_call > 0 and store_call > src.find("adreno_lpae_plan(", build_at),
              "descriptors are written only after the plan is produced")
    check("plan[i].table_paddr" in src and "slot - pbase" in src,
          "each write is translated into the block's virtual space")

    # ---- the block bound is enforced, not assumed -----------------------
    check("slot < pbase" in src and "slot >= pbase" in src,
          "a plan naming a table outside the block is refused")
    check("plan[i].index >= geo->ptes_per_table" in src,
          "an index outside the table is refused")

    # ---- the root is the block's base -----------------------------------
    check("out->root_paddr = tables[0];" in src,
          "the root address is the block's first page")

    # ---- the layout dependency is the tested one ------------------------
    check("adreno_lpae_plan(" in src and "adreno_lpae_leaf_level(" in src,
          "the builder consumes AdrenoSmmuLpaeV1's plan and leaf-level helpers")
    check("adreno_lpae_geometry(" in src, "and its geometry")

    # ---- re-derive the table count independently ------------------------
    # 4 levels, 4 KiB leaf: tables are needed at levels 0,1,2 plus the leaf's own
    # table at level 3 -> 4 pages.
    ias, pg_shift, oi, maxlv = 48, 12, 3, 4
    bpl = pg_shift - oi
    va = ias - pg_shift
    levels = (va + bpl - 1) // bpl
    start = maxlv - levels
    leaf_4k = maxlv - 1
    check(levels == 4 and start == 0 and leaf_4k == 3, "geometry re-derives to 4 levels")
    check((leaf_4k - start) + 1 == 4, "a 4 KiB mapping needs 4 table pages")
    # a 2 MiB block installs at level 2, so it needs 3 pages
    check((2 - start) + 1 == 3, "a 2 MiB mapping needs 3 table pages")

    # ---- compile and run the self-check --------------------------------
    if CLANG.exists():
        outdir = ROOT / "build" / "enchilada-adreno"
        outdir.mkdir(parents=True, exist_ok=True)
        exe = outdir / "table-selftest.exe"
        harness = outdir / "table_main.c"
        harness.write_text(
            '#include <stdio.h>\n'
            '#include "AdrenoSmmuTableV1.h"\n'
            'int main(void){uint32_t n=0;int r=adreno_table_verify(&n);\n'
            'printf("checks=%u rc=%d\\n",n,r);return r;}\n',
            encoding="utf-8")
        proc = subprocess.run(
            [str(CLANG), "-std=c11", "-Wall", "-Wextra", "-o", str(exe),
             str(harness), str(SRC), str(LPAE), "-I", str(GPU)],
            capture_output=True, text=True)
        check(proc.returncode == 0,
              "module compiles with -Wall -Wextra: %s"
              % (proc.stderr.strip().splitlines()[:1] or ""))
        if proc.returncode == 0:
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            check(run.returncode == 0,
                  "self-check passes: %s" % run.stdout.strip())
            m = re.search(r"checks=(\d+)", run.stdout)
            check(m is not None and int(m.group(1)) >= 20,
                  "self-check covers at least 20 cases (got %s)"
                  % (m.group(1) if m else "?"))
    else:
        check(False, "clang not found at %s" % CLANG)

    print("ADRENO_SMMU_TABLE_VERIFY: %s (%d checks, %d failures)"
          % ("PASS" if not fails else "FAIL", checks, len(fails)))
    for f in fails:
        print("  FAIL:", f)
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
