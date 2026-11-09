#!/usr/bin/env python3
"""Derive the A630 hw_init register table from SHA-pinned Linux v6.12 sources.

This is the enchilada counterpart of UniXNU's scripts/derive-a740-hwinit.py,
copied faithfully for the FIRST Adreno generation (A6XX / ADRENO_6XX_GEN1):

  * Offsets come from the pinned a6xx.xml / adreno_common.xml / a6xx_gmu.xml
    with EXACT A6XX variant selection (first file with hits wins), NOT from a
    guessed constant and NOT from the A7XX variants the sheng backend uses.
  * The hwcg values come from the pinned a6xx_catalog.c a630_hwcg[] table.
  * The CP protect ranges come from the pinned a630_protect_regs[] table and
    DECLARE_ADRENO_PROTECT(a630_protect, 32), encoded exactly as
    a6xx_set_cp_protect() consumes them.

Writes drivers/NativeAdreno/NativeAdrenoA630HwInit.h (dword register offsets,
"Do not edit by hand"). --check compares the tracked header against a fresh
derivation instead of writing. Inputs must be fetched first with
fetch-adreno-a630-sources.py; their bytes+sha256 are pinned in
scripts/data/adreno-a630-inputs.json.

HOST-VERIFIED ONLY: this is a generation-specific register map derived from
public, immutable source bytes. It is NOT a GPU driver, proves nothing about
the device, reads no MMIO, claims no chip-id register offset, and claims no
GPU SMMU stream id / mask. Those need board inputs and a device probe.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / ".cache/adreno-a630-source/drivers/gpu/drm/msm"
XML = SRC / "registers/adreno"
OUT = ROOT / "drivers/NativeAdreno/NativeAdrenoA630HwInit.h"
REFERENCE = OUT
ORDER = ["A2XX", "A3XX", "A4XX", "A5XX", "A6XX", "A7XX", "A8XX"]
TARGET = "A6XX"


def matches(variants):
    """Exact upstream variant predicate, specialised to the A6XX target."""
    if variants is None:
        return True
    for part in variants.split(","):
        part = part.strip()
        if "-" in part:
            left, right = part.split("-", 1)
            low = ORDER.index(left) if left else 0
            high = ORDER.index(right) if right else len(ORDER) - 1
            if low <= ORDER.index(TARGET) <= high:
                return True
        elif part == TARGET:
            return True
    return False


def resolve(name, files, kind="reg(?:32|64)"):
    """Exact upstream first-file-wins resolver (a6xx.xml before adreno_common.xml)."""
    for f in files:
        text = (XML / f).read_text(encoding="utf-8", errors="replace")
        hits = set()
        for m in re.finditer(
            r"<(" + kind + r') offset="(0x[0-9a-fA-F]+)" name="' + name + r'"([^>]*)>', text
        ):
            v = re.search(r'variants="([^"]+)"', m.group(3))
            if matches(v.group(1) if v else None):
                hits.add(int(m.group(2), 16))
        if len(hits) > 1:
            raise SystemExit("ambiguous " + name + " in " + f)
        if hits:
            return hits.pop()
    raise SystemExit("missing " + name)


# Upstream's 50 GPU names MINUS the four A7XX-only ones (CP_BV_APRIV_CNTL,
# CP_LPAC_APRIV_CNTL, CP_BV_RB_RPTR_ADDR, GRAS_NC_MODE_CNTL), which carry
# variants="A7XX-" and therefore do not exist on A630. 46 + 3 GMU + 1 array = 50.
GPU = [
    "GBIF_HALT", "RBBM_GBIF_HALT", "RBBM_SECVID_TSB_CNTL", "RBBM_SECVID_TSB_TRUSTED_BASE",
    "RBBM_SECVID_TSB_TRUSTED_SIZE", "RBBM_GBIF_CLIENT_QOS_CNTL", "UCHE_GBIF_GX_CONFIG",
    "RBBM_PERFCTR_GPU_BUSY_MASKED", "UCHE_TRAP_BASE", "UCHE_WRITE_THRU_BASE", "UCHE_GMEM_RANGE_MIN",
    "UCHE_GMEM_RANGE_MAX", "UCHE_CACHE_WAYS", "CP_AHB_CNTL", "RBBM_PERFCTR_CNTL",
    "RBBM_INTERFACE_HANG_INT_CNTL", "UCHE_CLIENT_PF", "CP_PROTECT_CNTL", "UCHE_CMDQ_CONFIG",
    "CP_APRIV_CNTL", "RB_CMP_DBG_ECO_CNTL", "RBBM_INT_0_MASK", "CP_SQE_INSTR_BASE", "CP_RB_BASE",
    "CP_RB_CNTL", "CP_RB_RPTR_ADDR", "CP_SQE_CNTL", "CP_RB_RPTR", "CP_RB_WPTR", "RBBM_STATUS",
    "RBBM_INT_0_STATUS", "CP_HW_FAULT", "CP_INTERRUPT_STATUS", "RBBM_CLOCK_CNTL",
    "RBBM_SECVID_TRUST_CNTL", "RBBM_STATUS3", "CP_SQE_STAT_ADDR", "CP_SQE_STAT_DATA",
    "RB_NC_MODE_CNTL", "TPL1_NC_MODE_CNTL", "SP_NC_MODE_CNTL", "CP_APERTURE_CNTL_HOST",
    "UCHE_MODE_CNTL", "RBBM_NC_MODE_CNTL", "RBBM_INT_CLEAR_CMD", "CP_ALWAYS_ON_COUNTER",
]
GMU = ["GPU_GMU_AO_GMU_CGC_MODE_CNTL", "GPU_GMU_AO_GMU_CGC_DELAY_CNTL",
       "GPU_GMU_AO_GMU_CGC_HYST_CNTL"]


def main():
    catalog = (SRC / "adreno/a6xx_catalog.c").read_text(encoding="utf-8", errors="replace")
    defines = [(n, resolve(n, ["a6xx.xml", "adreno_common.xml"])) for n in GPU]
    defines += [(n, resolve(n, ["a6xx_gmu.xml"])) for n in GMU]
    defines.append(("CP_PROTECT", resolve("CP_PROTECT", ["a6xx.xml"], "array")))

    block = re.search(r"a630_hwcg\[\]\s*=\s*\{(.*?)\{\},\s*\};", catalog, re.S).group(1)
    hwcg = []
    for name, value in re.findall(
        r"\{\s*REG_A[67]XX_([A-Z0-9_]+),\s*(0x[0-9a-fA-F]+)\s*\}", block
    ):
        hwcg.append((name, resolve(name, ["a6xx.xml", "adreno_common.xml"]), int(value, 16)))
    if len(hwcg) != block.count("REG_"):
        raise SystemExit("unparsed a630_hwcg entry")

    pblock = re.search(r"a630_protect_regs\[\]\s*=\s*\{(.*?)\};", catalog, re.S).group(1)
    pblock = re.sub(r"/\*.*?\*/", "", pblock, flags=re.S)
    protect = []
    for entry in [e.strip() for e in pblock.split(",\n")]:
        entry = entry.strip().rstrip(",")
        if not entry:
            continue
        if entry == "0":
            protect.append(0)
            continue
        m = re.fullmatch(
            r"A6XX_PROTECT_(NORDWR|RDONLY)\((0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\)", entry
        )
        if not m:
            raise SystemExit("unparsed protect entry " + entry)
        reg, length = int(m.group(2), 16), int(m.group(3), 16)
        protect.append(
            ((1 << 31) if m.group(1) == "NORDWR" else 0)
            | ((length & 0x3FFF) << 18)
            | (reg & 0x3FFFF)
        )
    count_max = int(
        re.search(r"DECLARE_ADRENO_PROTECT\(a630_protect,\s*(\d+)\)", catalog).group(1)
    )
    # a6xx_set_cp_protect(): entries 0..count-2 at their index, last at count_max-1.
    writes = [(i, v) for i, v in enumerate(protect[:-1]) if v] + [(count_max - 1, protect[-1])]

    lines = [
        "/* SPDX-License-Identifier: MIT */",
        "#ifndef NATIVE_ADRENO_A630_HWINIT_H",
        "#define NATIVE_ADRENO_A630_HWINIT_H",
        "#include <stdint.h>",
        "/* Generated by scripts/derive-a630-hwinit.py from the pinned a6xx.xml/",
        " * adreno_common.xml/a6xx_gmu.xml (A6XX variants) and a6xx_catalog.c",
        " * (a630_hwcg, a630_protect). Dword register offsets. Do not edit by hand. */",
    ]
    for name, offset in defines:
        lines.append("#define A630_%s 0x%05x" % (name, offset))
    lines.append("static const uint32_t NativeAdrenoA630Hwcg[][2]={")
    lines += ["    {0x%05x,0x%08x},   /* %s */" % (o, v, n) for n, o, v in hwcg]
    lines.append("};")
    lines.append("#define NATIVE_ADRENO_A630_HWCG_COUNT %d" % len(hwcg))
    lines.append("/* {CP_PROTECT index, value} in a6xx_set_cp_protect order. */")
    lines.append("static const uint32_t NativeAdrenoA630Protect[][2]={")
    lines += ["    {%2d,0x%08x}," % w for w in writes]
    lines.append("};")
    lines.append("#define NATIVE_ADRENO_A630_PROTECT_COUNT %d" % len(writes))
    lines.append("#endif")
    text = "\n".join(lines) + "\n"

    if "--check" in sys.argv[1:]:
        if REFERENCE.read_text(encoding="utf-8") != text:
            raise SystemExit("NativeAdrenoA630HwInit.h is stale")
        print("PASS: A630 hw_init table matches the pinned sources")
        return
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print("PASS: %d registers, %d hwcg entries, %d protect writes (A6XX GEN1 / A630)"
          % (len(defines), len(hwcg), len(writes)))


main()
