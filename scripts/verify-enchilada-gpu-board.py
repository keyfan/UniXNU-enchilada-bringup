#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the enchilada A630 GPU board inputs.

Independent of extract-enchilada-gpu-board.py: instead of re-parsing the DTB
with the same regex method, this script takes the EXTRACTED constants as inputs
(from the sha-pinned manifest) and cross-checks them against THREE different
domains that were produced by unrelated work:

  (a) device-verified C constants ALREADY in this repo
      (AdrenoCatalog.h, SmmuSurvey.h, Dwc3Registers.h);
  (b) the sha256-pinned Linux v6.12 a6xx_catalog.c (upstream pin-before-derive
      cache, fetched by scripts/fetch-adreno-a630-sources.py);
  (c) the NEW host module AdrenoBoardInputs.h -- every #define is asserted to
      equal the DTB-derived value (guards against transcription drift).

Node blocks are sliced by the EXACT 1-based line numbers in the manifest
(independently verified across 4 byte-identical copies by method 1).

This is HOST-VERIFIED: a DTB / a C header / a Linux source are descriptions,
NOT device measurements. No network, no CI, no flash, no register read.

Usage: python -X utf8 scripts/verify-enchilada-gpu-board.py
"""
import hashlib
import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
W = SCRIPTS.parents[2]
M = json.loads((SCRIPTS / "data/enchilada-gpu-board.json").read_text(encoding="utf-8"))
DTB = W / "notes/mu-silicium/enchilada.dts"
BOARD_H = DEV / "drivers/NativeAdreno/AdrenoBoardInputs.h"
CATALOG_H = DEV / "drivers/NativeAdreno/AdrenoCatalog.h"
SMMU_H = DEV / "drivers/NativeSmmu/SmmuSurvey.h"
DWC3_H = DEV / "drivers/NativeDwc3/Dwc3Registers.h"
CACHE_CATALOG_C = DEV / M["linux_catalog"]["cache"]

CHECKS = []


def need(ok, label, detail=""):
    CHECKS.append((bool(ok), label, detail))


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def first(pattern, text, group=1):
    m = re.search(pattern, text)
    return m.group(group) if m else None


def hexints(text, prop):
    m = re.search(prop + r"\s*=\s*<([^>]+)>", text)
    return [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", m.group(1))] if m else []


def define(text, name):
    # Module constants are written either as hex (0x..., addresses) or plain
    # decimal (cell counts, IRQ ids, PAS id). Both carry a u/ull suffix.
    m = re.search(r"#define\s+" + re.escape(name) +
                  r"\s+(0x[0-9a-fA-F]+|[0-9]+)(?:u|ull|U)?\b", text)
    if not m:
        return None
    tok = m.group(1)
    return int(tok, 16) if tok.lower().startswith("0x") else int(tok, 10)


def main():
    raw = DTB.read_bytes()
    need(sha256(raw) == M["dtb"]["sha256"],
         "method 2 independently re-pins the FULL DTB sha256", sha256(raw)[:16])
    lines = raw.decode("utf-8", errors="replace").splitlines()

    def block(a, b):
        return "\n".join(lines[a - 1:b])

    S = M["slices"]
    kgsl_smmu = block(*S["kgsl_smmu"])
    apps_smmu = block(*S["apps_smmu"])
    hyp = block(*S["kgsl_hyp"])
    gpu = block(*S["gpu"])
    gmu = block(*S["gmu"])

    cat_h = CATALOG_H.read_text(encoding="utf-8")
    smmu_h = SMMU_H.read_text(encoding="utf-8")
    dwc3_h = DWC3_H.read_text(encoding="utf-8")
    board_h = BOARD_H.read_text(encoding="utf-8")

    # ---- (a) DTB <-> device-verified C constants in this repo ---------------
    dtb_chipid = first(r"qcom,chipid\s*=\s*<(0x[0-9a-fA-F]+)>", gpu)
    c_chipid = define(cat_h, "ADRENO_A630_DTB_CHIPID")
    need(dtb_chipid == "0x6030001" and c_chipid == 0x06030001 and
         int(dtb_chipid, 16) == c_chipid,
         "chipid: DTB 0x6030001 == AdrenoCatalog.h ADRENO_A630_DTB_CHIPID",
         "dtb=%s c=%s" % (dtb_chipid, c_chipid))

    dtb_gpu_base = first(r"reg\s*=\s*<(0x[0-9a-fA-F]+)", gpu)
    c_gpu_base = define(cat_h, "ADRENO_KGSL3D0_BASE")
    need(dtb_gpu_base and c_gpu_base == 0x05000000 and
         int(dtb_gpu_base, 16) == c_gpu_base,
         "GPU reg base: DTB 0x5000000 == ADRENO_KGSL3D0_BASE",
         "dtb=%s c=%s" % (dtb_gpu_base, c_gpu_base))

    c_apps = define(smmu_h, "SMMU_SURVEY_SDM845_BASE")
    c_dwc3 = first(r"kAppsSmmuBase\s*=\s*(0x[0-9a-fA-F]+)ull", dwc3_h)
    need(c_apps == 0x15000000 and c_dwc3 == "0x15000000" and
         "0x15000000" in apps_smmu,
         "apps_smmu base 0x15000000 agrees across SmmuSurvey.h + Dwc3Registers.h + DTB",
         "SmmuSurvey=%s Dwc3=%s" % (c_apps, c_dwc3))

    # ---- (b) DTB board facts (method 2 uses its own hexints parse) ----------
    # NOTE: node name is "arm,smmu-kgsl@5040000" (bare hex, NO 0x prefix);
    # take the base from the node's own reg property.
    kgsl_reg = hexints(kgsl_smmu, "reg")
    need(kgsl_reg[:2] == [0x5040000, 0x10000] and 0x5040000 != 0x15000000,
         "DEDICATED kgsl SMMU @0x5040000, NOT apps_smmu @0x15000000", str(kgsl_reg))
    need(first(r"#iommu-cells\s*=\s*<(0x[0-9a-fA-F]+)>", kgsl_smmu) == "0x01" and
         first(r"#iommu-cells\s*=\s*<(0x[0-9a-fA-F]+)>", apps_smmu) == "0x02",
         "kgsl #iommu-cells=1 (SID only, no mask) vs apps_smmu =2 (SID+mask)")
    need(first(r"phandle\s*=\s*<(0x[0-9a-fA-F]+)>", kgsl_smmu) == "0x1ac" and
         first(r"phandle\s*=\s*<(0x[0-9a-fA-F]+)>", apps_smmu) == "0x29",
         "phandles: kgsl=0x1ac vs apps=0x29 (distinct SMMU instances)")
    pas = first(r"qcom,pas-id\s*=\s*<(0x[0-9a-fA-F]+)>", hyp)
    zap = first(r'qcom,firmware-name\s*=\s*"([^"]+)"', hyp)
    need(pas == "0x0d" and zap == "a630_zap",
         "qcom,pas-id=0x0d == GPU_PAS_ID 13, firmware a630_zap", "pas=%s zap=%s" % (pas, zap))

    def cb_sids(text):
        return [v for v in hexints(text, "iommus") if v != 0x1ac]

    need(cb_sids(block(*S["gfx3d_user"])) == [0], "gfx3d_user SID 0x00")
    need(cb_sids(block(*S["gfx3d_secure"])) == [2, 1], "gfx3d_secure SIDs 0x02,0x01")
    need(cb_sids(block(*S["gmu_user"])) == [4], "gmu_user SID 0x04")
    need(cb_sids(block(*S["gmu_kernel"])) == [5], "gmu_kernel SID 0x05")
    gpu_iommus = [l for l in (block(*S["kgsl_iommu"]) + "\n" + gmu).splitlines() if "iommus" in l]
    need(len(gpu_iommus) == 4 and all("0x1ac" in l for l in gpu_iommus) and
         not any("0x29" in l for l in gpu_iommus),
         "ALL GPU iommus use kgsl 0x1ac; NONE use apps_smmu 0x29", "%d lines" % len(gpu_iommus))

    need(hexints(gpu, "reg")[:6] == [0x5000000, 0x40000, 0x5061000, 0x800, 0x509e000, 0x1000],
         "GPU 3 register windows verbatim", str(hexints(gpu, "reg")))
    need(hexints(gmu, "reg")[:4] == [0x506a000, 0x30000, 0xb200000, 0x300000],
         "GMU 2 register windows verbatim (GMU + PDC)", str(hexints(gmu, "reg")))
    need(0x12c in hexints(gpu, "interrupts"), "GPU IRQ 300 (0x12c)")
    need(0x130 in hexints(gmu, "interrupts") and 0x131 in hexints(gmu, "interrupts"),
         "GMU IRQs HFI 304 (0x130) + GMU 305 (0x131)")
    hbb = first(r"qcom,highest-bank-bit\s*=\s*<(0x[0-9a-fA-F]+)>", gpu)
    ubwc = first(r"qcom,ubwc-mode\s*=\s*<(0x[0-9a-fA-F]+)>", gpu)
    need(hbb == "0x0f" and ubwc == "0x02", "highest_bank_bit=15, ubwc_mode=2",
         "hbb=%s ubwc=%s" % (hbb, ubwc))

    # ---- (b2) sha256-pinned Linux v6.12 a6xx_catalog.c ---------------------
    lc = M["linux_catalog"]
    cat_c_ok = False
    cat_c_detail = "MISSING (run scripts/fetch-adreno-a630-sources.py)"
    if CACHE_CATALOG_C.is_file():
        b = CACHE_CATALOG_C.read_bytes()
        t = b.decode("utf-8", errors="replace")
        cat_c_ok = (sha256(b) == lc["sha256"] and
                    all(k in t for k in lc["required_markers"]))
        cat_c_detail = "sha=%s bytes=%d" % (sha256(b)[:12], len(b))
    need(cat_c_ok, "pinned Linux v6.12 a6xx_catalog.c confirms chipids/fw/GEN1", cat_c_detail)

    # ---- (c) NEW host module AdrenoBoardInputs.h: no transcription drift ----
    board_expected = {
        "ADRENO_BOARD_KGSL_SMMU_BASE": 0x05040000,
        "ADRENO_BOARD_KGSL_SMMU_SIZE": 0x00010000,
        "ADRENO_BOARD_KGSL_SMMU_PHANDLE": 0x1ac,
        "ADRENO_BOARD_KGSL_SMMU_IOMMU_CELLS": 1,
        "ADRENO_BOARD_APPS_SMMU_BASE": 0x15000000,
        "ADRENO_BOARD_APPS_SMMU_PHANDLE": 0x29,
        "ADRENO_BOARD_APPS_SMMU_IOMMU_CELLS": 2,
        "ADRENO_BOARD_GPU_BASE": 0x05000000,
        "ADRENO_BOARD_GPU_SIZE": 0x00040000,
        "ADRENO_BOARD_GPU_CX_DBGC": 0x05061000,
        "ADRENO_BOARD_GPU_CX_MISC": 0x0509e000,
        "ADRENO_BOARD_GMU_BASE": 0x0506a000,
        "ADRENO_BOARD_GMU_SIZE": 0x00030000,
        "ADRENO_BOARD_GMU_PDC_BASE": 0x0b200000,
        "ADRENO_BOARD_GMU_PDC_SIZE": 0x00300000,
        "ADRENO_BOARD_GPU_IRQ": 300,
        "ADRENO_BOARD_GMU_HFI_IRQ": 304,
        "ADRENO_BOARD_GMU_IRQ": 305,
        "ADRENO_BOARD_CHIPID": 0x06030001,
        "ADRENO_BOARD_HIGHEST_BANK_BIT": 15,
        "ADRENO_BOARD_UBWC_MODE": 2,
        "ADRENO_BOARD_ZAP_PAS_ID": 13,
        "ADRENO_BOARD_KGSL_IOMMU_PROTECT_BASE": 0x00040000,
        "ADRENO_BOARD_KGSL_IOMMU_PROTECT_SIZE": 0x0000c000,
        "ADRENO_BOARD_KGSL_MICRO_MMU_CONTROL": 0x00006000,
        "ADRENO_BOARD_KGSL_SECURE_ALIGN_MASK": 0x00000fff,
    }
    drift = [(n, v, define(board_h, n)) for n, v in board_expected.items()
             if define(board_h, n) != v]
    need(not drift, "AdrenoBoardInputs.h: all %d #defines match the DTB-derived values"
         % len(board_expected), str(drift[:3]))

    # provenance: same DTB carries device-verified stage-3 sofef00 DCS node
    need(any("bl_ctrl_dcs" in l and i + 1 == 2661 for i, l in enumerate(lines)),
         "provenance: same DTB carries device-verified sofef00 bl_ctrl_dcs at :2661")

    for ok, lab, det in CHECKS:
        print("  %s  %s%s" % ("ok  " if ok else "FAIL", lab,
                              ("  [%s]" % det) if det and not ok else ""))
    bad = [lab for ok, lab, _ in CHECKS if not ok]
    print()
    if bad:
        print("ENCHILADA_GPU_BOARD_CROSS: FAIL (%d)" % len(bad), file=sys.stderr)
        sys.exit(1)
    print("ENCHILADA_GPU_BOARD_CROSS: PASS (%d checks, three independent domains agree)"
          % len(CHECKS))


if __name__ == "__main__":
    main()
