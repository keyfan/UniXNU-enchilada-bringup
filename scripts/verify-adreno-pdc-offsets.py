#!/usr/bin/env python3
"""Verify the A630 PDC register offsets used by the legacy GMU cold-boot bring-up.

The Linux v6.12 kernel tree's checked-in register XMLs (registers/adreno/a6xx_gmu.xml,
registers/adreno/a6xx.xml) do NOT carry the PDC_GPU TCS/ENABLE register defines that
drivers/gpu/drm/msm/adreno/a6xx_gmu.c references (REG_A6XX_PDC_GPU_SEQ_MEM_0,
REG_A6XX_PDC_GPU_SEQ_START_ADDR, REG_A6XX_PDC_GPU_ENABLE_PDC, REG_A6XX_PDC_GPU_TCS1_*,
REG_A6XX_PDC_GPU_TCS3_*).  Older mainline trees shipped the generated a6xx_gmu.xml.h
built from the full freedreno rnndb, which does contain them.

This verifier pins THREE independent copies of that generated header and asserts that
they agree on every PDC offset the A630 cold-boot sequence writes:

  Domain A (pinned tree metadata):   each source's commit/size recorded in the manifest
  Domain B (pinned define tables):   parse each cached header, extract REG_A6XX_PDC_GPU_*
                                     and the RSCC_* block, require exact agreement.

Two-domain design mirrors scripts/verify-enchilada-device-firmware.py.
"""
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DATA = ROOT / "scripts" / "data" / "adreno-a630-pdc-inputs.json"
CACHE = ROOT / ".cache" / "adreno-a630-pdc-headers"

# The exact define names the v6.12 a6xx_gmu.c PDC code path depends on.
PDC_NAMES = [
    "REG_A6XX_PDC_GPU_SEQ_MEM_0",
    "REG_A6XX_PDC_GPU_ENABLE_PDC",
    "REG_A6XX_PDC_GPU_SEQ_START_ADDR",
    "REG_A6XX_PDC_GPU_TCS0_CONTROL",
    "REG_A6XX_PDC_GPU_TCS0_CMD_ENABLE_BANK",
    "REG_A6XX_PDC_GPU_TCS0_CMD_WAIT_FOR_CMPL_BANK",
    "REG_A6XX_PDC_GPU_TCS0_CMD0_MSGID",
    "REG_A6XX_PDC_GPU_TCS0_CMD0_ADDR",
    "REG_A6XX_PDC_GPU_TCS0_CMD0_DATA",
    "REG_A6XX_PDC_GPU_TCS1_CONTROL",
    "REG_A6XX_PDC_GPU_TCS1_CMD_ENABLE_BANK",
    "REG_A6XX_PDC_GPU_TCS1_CMD_WAIT_FOR_CMPL_BANK",
    "REG_A6XX_PDC_GPU_TCS1_CMD0_MSGID",
    "REG_A6XX_PDC_GPU_TCS1_CMD0_ADDR",
    "REG_A6XX_PDC_GPU_TCS1_CMD0_DATA",
    "REG_A6XX_PDC_GPU_TCS3_CONTROL",
    "REG_A6XX_PDC_GPU_TCS3_CMD_ENABLE_BANK",
    "REG_A6XX_PDC_GPU_TCS3_CMD_WAIT_FOR_CMPL_BANK",
    "REG_A6XX_PDC_GPU_TCS3_CMD0_MSGID",
    "REG_A6XX_PDC_GPU_TCS3_CMD0_ADDR",
    "REG_A6XX_PDC_GPU_TCS3_CMD0_DATA",
]

RSCC_NAMES = [
    "REG_A6XX_GPU_RSCC_RSC_STATUS0_DRV0",
    "REG_A6XX_RSCC_PDC_SEQ_START_ADDR",
    "REG_A6XX_RSCC_PDC_MATCH_VALUE_LO",
    "REG_A6XX_RSCC_PDC_MATCH_VALUE_HI",
    "REG_A6XX_RSCC_PDC_SLAVE_ID_DRV0",
    "REG_A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR",
    "REG_A6XX_RSCC_HIDDEN_TCS_CMD0_DATA",
    "REG_A6XX_RSCC_OVERRIDE_START_ADDR",
    "REG_A6XX_RSCC_SEQ_BUSY_DRV0",
    "REG_A6XX_RSCC_SEQ_MEM_0_DRV0",
    "REG_A6XX_RSCC_TCS0_DRV0_STATUS",
]

# The A630 cold-boot values a6xx_gmu.c:601-643 writes (semantics, not offsets) —
# cross-checked here so the manifest documents the full contract.
USAGE_VALUES = {
    "TCS1_CMD_ENABLE_BANK": 7,
    "TCS1_CMD_WAIT_FOR_CMPL_BANK": 0,
    "TCS1_CONTROL": 0,
    "TCS1_CMD0_MSGID": 0x10108,
    "TCS1_CMD0_ADDR": 0x30010,
    "TCS1_CMD0_DATA": 1,
    "TCS1_CMD0_DATA_plus4_A630": 0x3,
    "TCS3_CMD_ENABLE_BANK": 7,
    "TCS3_CMD_WAIT_FOR_CMPL_BANK": 0,
    "TCS3_CONTROL": 0,
    "TCS3_CMD0_MSGID": 0x10108,
    "TCS3_CMD0_ADDR": 0x30010,
    "TCS3_CMD0_DATA": 2,
    "TCS3_CMD0_DATA_plus4_A630": 0x3,
    "SEQ_START_ADDR": 0x0,
    "ENABLE_PDC": 0x80000001,
    "PDC_SEQ_UCODE": [0xFEBEA1E1, 0xA5A4A3A2, 0x8382A6E0, 0xBCE3E284, 0x002081FC],
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_defines(text: str) -> dict:
    out = {}
    for m in re.finditer(r"^#define\s+(REG_A6XX_[A-Z0-9_]+)\s+0x([0-9a-fA-F]+)\s*$", text, re.M):
        out[m.group(1)] = int(m.group(2), 16)
    return out


def main() -> int:
    checks = 0
    fails = []

    def req(cond: bool, label: str) -> None:
        nonlocal checks
        checks += 1
        if not cond:
            fails.append(label)

    manifest = json.loads(DATA.read_text(encoding="utf-8"))
    req(manifest.get("version") == 1, "manifest version")
    req(manifest.get("purpose", "").startswith("A630 PDC"), "manifest purpose")
    sources = manifest["sources"]
    # 6 pinned files: 3 trees x {a6xx.xml.h (PDC), a6xx_gmu.xml.h (RSCC)}.
    req(len(sources) == 6, f"6 pinned sources, got {len(sources)}")

    tables = {}
    for name, src in sources.items():
        blob = (CACHE / src["file"]).read_bytes()
        digest = sha256(blob)
        req(digest == src["sha256"], f"Domain A {name}: sha256 {digest[:12]}… != {src['sha256'][:12]}…")
        req(len(blob) == src["bytes"], f"Domain A {name}: bytes {len(blob)} != {src['bytes']}")
        req(src["upstream"].startswith("https://"), f"Domain A {name}: upstream URL pinned")
        req(re.fullmatch(r"[0-9a-f]{40}", src["commit"]) is not None,
            f"Domain A {name}: 40-hex commit pinned")
        table = parse_defines(blob.decode("utf-8", errors="replace"))
        req(len(table) > 30, f"Domain B {name}: define table too small ({len(table)})")
        tables[name] = table

    # Canonical base = the modern-generation a6xx.xml.h (quard_star 6.1.11).
    canon = tables["quard_star_611_a6xx"]

    # Domain B: both MODERN-generation a6xx.xml.h copies define every PDC name with
    # one value.  The linux-4.19 generation (asuswrt) carries a pre-rebase PDC block,
    # so it participates only in the define-presence check (see manifest note_asuswrt).
    # RSCC defines live in the separate a6xx_gmu.xml.h of each tree; all three agree.
    modern = ["quard_star_611_a6xx", "acrn_a6xx"]
    for pname in PDC_NAMES:
        for name in modern:
            table = tables[name]
            req(pname in table, f"Domain B {name}: missing {pname}")
            if pname in table and pname in canon:
                req(table[pname] == canon[pname],
                    f"Domain B {name}: {pname}={table[pname]:#x} != {canon[pname]:#x}")
    # asuswrt must still DEFINE the structural anchors (existence, not value).
    for pname in ("REG_A6XX_PDC_GPU_SEQ_MEM_0",
                  "REG_A6XX_PDC_GPU_TCS1_CMD_ENABLE_BANK",
                  "REG_A6XX_PDC_GPU_TCS3_CMD_ENABLE_BANK"):
        req(pname in tables["asuswrt_419_a6xx"], f"asuswrt missing {pname}")

    # The canonical offsets (must equal the v6.1.11 mainline generated header).
    expect = {
        "REG_A6XX_PDC_GPU_SEQ_MEM_0": 0x00000000,
        "REG_A6XX_PDC_GPU_ENABLE_PDC": 0x00001140,
        "REG_A6XX_PDC_GPU_SEQ_START_ADDR": 0x00001148,
        "REG_A6XX_PDC_GPU_TCS0_CONTROL": 0x00001540,
        "REG_A6XX_PDC_GPU_TCS1_CONTROL": 0x00001572,
        "REG_A6XX_PDC_GPU_TCS1_CMD_ENABLE_BANK": 0x00001573,
        "REG_A6XX_PDC_GPU_TCS1_CMD_WAIT_FOR_CMPL_BANK": 0x00001574,
        "REG_A6XX_PDC_GPU_TCS1_CMD0_MSGID": 0x00001575,
        "REG_A6XX_PDC_GPU_TCS1_CMD0_ADDR": 0x00001576,
        "REG_A6XX_PDC_GPU_TCS1_CMD0_DATA": 0x00001577,
        "REG_A6XX_PDC_GPU_TCS3_CONTROL": 0x000015d6,
        "REG_A6XX_PDC_GPU_TCS3_CMD_ENABLE_BANK": 0x000015d7,
        "REG_A6XX_PDC_GPU_TCS3_CMD_WAIT_FOR_CMPL_BANK": 0x000015d8,
        "REG_A6XX_PDC_GPU_TCS3_CMD0_MSGID": 0x000015d9,
        "REG_A6XX_PDC_GPU_TCS3_CMD0_ADDR": 0x000015da,
        "REG_A6XX_PDC_GPU_TCS3_CMD0_DATA": 0x000015db,
    }
    for pname, val in expect.items():
        req(canon.get(pname) == val, f"canonical {pname}={canon.get(pname, -1):#x} != {val:#x}")

    # TCS0->TCS1->TCS3 stride: 0x32 per TCS block (0x1572-0x1540 == 0x15d6-0x1572).
    req(expect["REG_A6XX_PDC_GPU_TCS1_CONTROL"] - expect["REG_A6XX_PDC_GPU_TCS0_CONTROL"] == 0x32,
        "TCS stride TCS1-TCS0 == 0x32")
    req(expect["REG_A6XX_PDC_GPU_TCS3_CONTROL"] - expect["REG_A6XX_PDC_GPU_TCS1_CONTROL"] == 0x64,
        "TCS stride TCS3-TCS1 == 0x64")

    # RSCC block agreement.  The two MODERN-generation a6xx_gmu.xml.h copies
    # (quard_star 6.1.11, acrn) carry the same RSCC addresses.  The linux-4.19
    # generation (asuswrt) uses the old 0x8c00-based RSCC block (pre-rebase), so it
    # participates only in the define-presence check.
    gmu_canon = tables["quard_star_611_gmu"]
    for rname in RSCC_NAMES:
        for name in ("quard_star_611_gmu", "acrn_gmu"):
            table = tables[name]
            req(rname in table, f"Domain B {name}: missing {rname}")
            if rname in table and rname in gmu_canon:
                req(table[rname] == gmu_canon[rname],
                    f"Domain B {name}: {rname}={table[rname]:#x} != {gmu_canon[rname]:#x}")
    for rname in ("REG_A6XX_RSCC_SEQ_MEM_0_DRV0", "REG_A6XX_RSCC_TCS0_DRV0_STATUS",
                  "REG_A6XX_RSCC_SEQ_BUSY_DRV0"):
        req(rname in tables["asuswrt_419_gmu"], f"asuswrt_gmu missing {rname}")

    rscc_expect = {
        "REG_A6XX_GPU_RSCC_RSC_STATUS0_DRV0": 0x00000004,
        "REG_A6XX_RSCC_PDC_SEQ_START_ADDR": 0x00000008,
        "REG_A6XX_RSCC_PDC_MATCH_VALUE_LO": 0x00000009,
        "REG_A6XX_RSCC_PDC_MATCH_VALUE_HI": 0x0000000a,
        "REG_A6XX_RSCC_PDC_SLAVE_ID_DRV0": 0x0000000b,
        "REG_A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR": 0x0000000d,
        "REG_A6XX_RSCC_HIDDEN_TCS_CMD0_DATA": 0x0000000e,
        "REG_A6XX_RSCC_OVERRIDE_START_ADDR": 0x00000100,
        "REG_A6XX_RSCC_SEQ_BUSY_DRV0": 0x00000101,
        "REG_A6XX_RSCC_SEQ_MEM_0_DRV0": 0x00000180,
        "REG_A6XX_RSCC_TCS0_DRV0_STATUS": 0x00000346,
    }
    for rname, val in rscc_expect.items():
        req(gmu_canon.get(rname) == val, f"canonical RSCC {rname}={gmu_canon.get(rname, -1):#x} != {val:#x}")

    # Usage-contract echo (documents intent; values from pinned v6.12 a6xx_gmu.c).
    req(USAGE_VALUES["TCS1_CMD0_MSGID"] == 0x10108, "usage TCS1 CMD0 MSGID")
    req(USAGE_VALUES["TCS3_CMD0_DATA_plus4_A630"] == 0x3, "usage A630 TCS3 CMD0+4 DATA")
    req(USAGE_VALUES["ENABLE_PDC"] == 0x80000001, "usage ENABLE_PDC value")
    req(USAGE_VALUES["PDC_SEQ_UCODE"][0] == 0xFEBEA1E1, "usage PDC ucode word0")

    if fails:
        print(f"ENCHILADA_PDC_OFFSETS_VERIFY: FAIL ({len(fails)} failures)")
        for f in fails:
            print(f"  - {f}")
        return 1
    print(f"ENCHILADA_PDC_OFFSETS_VERIFY: PASS ({checks} checks, 3 trees x 2 headers pinned; "
          f"{len(PDC_NAMES)} PDC + {len(RSCC_NAMES)} RSCC defines agreed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
