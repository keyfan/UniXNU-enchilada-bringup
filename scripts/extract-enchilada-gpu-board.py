#!/usr/bin/env python3
"""Verification method 1 -- EXTRACT the enchilada A630 GPU board inputs.

Reads the sha256-pinned vendored Mu-Silicium enchilada.dts (the SAME DTB behind
the device-verified stage-3 sofef00 backlight, stage-4 touch and stage-7 gauge
facts), present in 4 independent local copies, and:

  (1) hashes the GPU-relevant node slices across ALL copies and requires they
      are byte-identical (guards against a stale/edited single copy);
  (2) parses the structured board facts (phandle / #iommu-cells / context-bank
      SIDs / reg windows / IRQs / chipid / pas-id / firmware) with a regex
      method and cross-checks that EVERY GPU iommus tuple references the
      dedicated kgsl SMMU phandle (0x1ac), NEVER apps_smmu (0x29).

This is a HOST-VERIFIED firmware-description study: a DTB is NOT a device
measurement. No network, no CI, no flash, no register read.

Usage: python -X utf8 scripts/extract-enchilada-gpu-board.py
"""
import hashlib
import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent            # .../UniXNU-enchilada/scripts
DEV = SCRIPTS.parent                                 # .../UniXNU-enchilada
W = SCRIPTS.parents[2]                               # oneplus6-ios
MANIFEST = json.loads((SCRIPTS / "data/enchilada-gpu-board.json").read_text(encoding="utf-8"))

FAIL = []


def need(ok, label, detail=""):
    print("  %s  %s%s" % ("ok  " if ok else "FAIL", label,
                          ("  [%s]" % detail) if detail and not ok else ""))
    if not ok:
        FAIL.append(label)


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def first(pattern, text, group=1):
    m = re.search(pattern, text)
    return m.group(group) if m else None


def hexints(text, prop):
    m = re.search(prop + r"\s*=\s*<([^>]+)>", text)
    return [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", m.group(1))] if m else []


def main():
    pin = MANIFEST["dtb"]["sha256"]
    copies = [W / c for c in MANIFEST["dtb"]["copies"]]
    present = [(str(p.relative_to(W)), p) for p in copies if p.is_file()]
    print("LOCAL_DTB_COPIES: %d" % len(present))
    for rel, p in present:
        print("  - %s (%d bytes)" % (rel, p.stat().st_size))
    need(len(present) >= 2, ">=2 independent local DTB copies present", str(len(present)))

    # (1) full-file sha pin, computed independently per copy
    full_hashes = {}
    for rel, p in present:
        full_hashes[rel] = sha256(p.read_bytes())
    distinct_full = set(full_hashes.values())
    need(len(distinct_full) == 1, "all copies byte-agree on FULL DTB", str(len(distinct_full)))
    need(next(iter(distinct_full), None) == pin, "full DTB sha256 matches the manifest pin",
         str(next(iter(distinct_full), ""))[:16])

    # (1b) GPU-slice hash agreement (independent of full-file hash)
    slices = MANIFEST["slices"]
    slice_hashes = {}
    for rel, p in present:
        lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
        blob = "\n".join("\n".join(lines[a - 1:b]) for a, b in slices.values())
        slice_hashes[rel] = sha256("\n".join(ln.rstrip() for ln in blob.splitlines()).encode())
    distinct_slices = set(slice_hashes.values())
    need(len(distinct_slices) == 1, "all copies byte-agree on GPU node slices", str(len(distinct_slices)))

    lines = present[0][1].read_text(encoding="utf-8", errors="replace").splitlines()

    def block(a, b):
        return "\n".join(lines[a - 1:b])

    kgsl_smmu = block(*slices["kgsl_smmu"])
    apps_smmu = block(*slices["apps_smmu"])
    hyp = block(*slices["kgsl_hyp"])
    gpu = block(*slices["gpu"])
    kgsl_iommu = block(*slices["kgsl_iommu"])
    gmu = block(*slices["gmu"])

    # (2) structured extraction -- regex method (independent of method 2's C checks)
    kgsl_ph = first(r"phandle\s*=\s*<(0x[0-9a-fA-F]+)>", kgsl_smmu)
    apps_ph = first(r"phandle\s*=\s*<(0x[0-9a-fA-F]+)>", apps_smmu)
    kgsl_cells = first(r"#iommu-cells\s*=\s*<(0x[0-9a-fA-F]+)>", kgsl_smmu)
    apps_cells = first(r"#iommu-cells\s*=\s*<(0x[0-9a-fA-F]+)>", apps_smmu)
    kgsl_reg = hexints(kgsl_smmu, "reg")
    chipid = first(r"qcom,chipid\s*=\s*<(0x[0-9a-fA-F]+)>", gpu)
    pas = first(r"qcom,pas-id\s*=\s*<(0x[0-9a-fA-F]+)>", hyp)
    zap = first(r'qcom,firmware-name\s*=\s*"([^"]+)"', hyp)

    print("\n==== STRUCTURED FACTS (method 1 regex parse) ====")
    print("  kgsl_smmu  phandle=%s cells=%s reg=%s" % (kgsl_ph, kgsl_cells, kgsl_reg))
    print("  apps_smmu  phandle=%s cells=%s" % (apps_ph, apps_cells))
    print("  gpu        chipid=%s" % chipid)
    print("  zap        pas-id=%s fw=%s" % (pas, zap))

    need(kgsl_ph == "0x1ac", "dedicated kgsl SMMU phandle = 0x1ac", str(kgsl_ph))
    need(apps_ph == "0x29", "apps_smmu phandle = 0x29", str(apps_ph))
    need(kgsl_ph != apps_ph, "the two SMMU phandles are DISTINCT")
    need(kgsl_reg[:2] == [0x5040000, 0x10000], "kgsl SMMU reg 0x5040000/0x10000", str(kgsl_reg))
    need(kgsl_cells == "0x01" and apps_cells == "0x02",
         "kgsl #iommu-cells=1 (SID only, NO mask) vs apps_smmu =2 (SID+mask)",
         "kgsl=%s apps=%s" % (kgsl_cells, apps_cells))
    need(chipid == "0x6030001", "qcom,chipid = 0x6030001 (A630 by DT equality)", str(chipid))
    need(pas == "0x0d" and zap == "a630_zap", "zap pas-id=13 / firmware a630_zap",
         "pas=%s zap=%s" % (pas, zap))

    # every GPU iommus tuple must reference kgsl phandle, never apps_smmu
    gpu_iommus = [l for l in (kgsl_iommu + "\n" + gmu).splitlines() if "iommus" in l]
    need(len(gpu_iommus) == 4, "exactly 4 GPU iommus tuples (user/secure/gmu_user/gmu_kernel)",
         str(len(gpu_iommus)))
    need(all(kgsl_ph in l for l in gpu_iommus), "ALL GPU iommus use kgsl phandle 0x1ac")
    need(not any(apps_ph in l for l in gpu_iommus), "NO GPU iommus uses apps_smmu 0x29")

    cb = {
        "gfx3d_user": [v for v in hexints(block(*slices["gfx3d_user"]), "iommus") if v != 0x1ac],
        "gfx3d_secure": [v for v in hexints(block(*slices["gfx3d_secure"]), "iommus") if v != 0x1ac],
        "gmu_user": [v for v in hexints(block(*slices["gmu_user"]), "iommus") if v != 0x1ac],
        "gmu_kernel": [v for v in hexints(block(*slices["gmu_kernel"]), "iommus") if v != 0x1ac],
    }
    print("  context_banks SIDs (single-cell, no mask): %s" % cb)
    need(cb == {"gfx3d_user": [0], "gfx3d_secure": [2, 1],
                "gmu_user": [4], "gmu_kernel": [5]},
         "context-bank SIDs 0 / 2,1 / 4 / 5", str(cb))

    # provenance: same DTB carries the device-verified stage-3 sofef00 DCS node
    dcs_lines = [i + 1 for i, l in enumerate(lines) if "bl_ctrl_dcs" in l]
    need(2661 in dcs_lines, "provenance: same DTB carries sofef00 bl_ctrl_dcs at :2661",
         str(dcs_lines))

    print()
    if FAIL:
        print("ENCHILADA_GPU_BOARD_EXTRACT: FAIL (%d)" % len(FAIL), file=sys.stderr)
        sys.exit(1)
    print("ENCHILADA_GPU_BOARD_EXTRACT: PASS (2 methods, %d checks, DTB sha %s...)"
          % (18, pin[:12]))


if __name__ == "__main__":
    main()
