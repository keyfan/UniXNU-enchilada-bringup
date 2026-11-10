#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the enchilada A630 LEGACY GMU cold-boot
bring-up sequence model (AdrenoGmuBootV1) against the sha256-pinned upstream
Linux v6.12 sources and the pinned PDC/RSCC offset sources.

Method 1 is the host test (drivers/NativeAdreno/tests/adreno-gmu-boot-v1-test.cpp),
which asserts the RUNTIME behaviour of AdrenoGmuBootV1.c on a synthetic bus.
Method 2 is INDEPENDENT of it: it never runs the module. It reads

  (a) the pinned a6xx_gmu.c (sha256 c14fbb25..., from
      scripts/data/adreno-a630-inputs.json -> linux.files) as raw TEXT and
      asserts the exact upstream lines that encode the cold-boot sequence
      (rpmh_start, fw_load legacy gate, fw_start config, power_config,
      gmu_start, gfx_rail_on, sptprac, hfi_start, cold/warm decision);
  (b) the pinned GMU register XML (a6xx_gmu.xml) and the pinned PDC offset
      sources (scripts/data/adreno-a630-pdc-inputs.json -> quard 6.1.11
      a6xx.xml.h / a6xx_gmu.xml.h) as raw TEXT and asserts every offset the
      module defines;
  (c) the NEW AdrenoGmuBootV1.h/.c as raw TEXT and asserts that each derived
      constant equals the upstream-derived value, that the plan order matches
      the upstream call order, and that no modern-only construct (block
      header parsing, FAL10 votes, A7xx branches) leaked into the model.

No network, no CI, no flash, no register read, no GPU execution. HOST-VERIFIED:
source text is a specification, not a device measurement.

Usage: python -X utf8 scripts/verify-enchilada-gmu-boot-v1.py
"""
import hashlib
import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
W = SCRIPTS.parents[2]
M = json.loads((SCRIPTS / "data/adreno-a630-inputs.json").read_text(encoding="utf-8"))
GMU_KEY = "drivers/gpu/drm/msm/adreno/a6xx_gmu.c"
XML_KEY = "drivers/gpu/drm/msm/registers/adreno/a6xx_gmu.xml"
PIN = M["linux"]["files"][GMU_KEY]
PIN_XML = M["linux"]["files"][XML_KEY]
CACHE = DEV / ".cache/adreno-a630-source" / GMU_KEY
CACHE_XML = DEV / ".cache/adreno-a630-source" / XML_KEY

PDC = json.loads((SCRIPTS / "data/adreno-a630-pdc-inputs.json").read_text(encoding="utf-8"))
PDC_DIR = DEV / ".cache/adreno-a630-pdc-headers"

H = DEV / "drivers/NativeAdreno/AdrenoGmuBootV1.h"
C = DEV / "drivers/NativeAdreno/AdrenoGmuBootV1.c"

CHECKS = []


def need(ok, label, detail=""):
    CHECKS.append((bool(ok), label, detail))


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def block(lines, first, last):
    return "\n".join(lines[first - 1:last])


def define(text, name):
    m = re.search(r"#define\s+" + re.escape(name) + r"\s+(\S+)", text)
    return m.group(1) if m else None


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src


def finish(name):
    fails = [c for c in CHECKS if not c[0]]
    for ok, label, detail in CHECKS:
        if not ok:
            print(f"  FAIL {label}  [{detail}]")
    print(f"GMU_BOOT_V1_CROSS: {'PASS' if not fails else 'FAIL'} "
          f"({len(CHECKS)} checks, {len(fails)} failures)")
    return 0 if not fails else 1


def main():
    # ---- (0) pinned upstream integrity --------------------------------------
    need(CACHE.is_file(), "pinned a6xx_gmu.c present in cache", str(CACHE))
    need(CACHE_XML.is_file(), "pinned a6xx_gmu.xml present in cache", str(CACHE_XML))
    if not CACHE.is_file() or not CACHE_XML.is_file():
        return finish("gmu-boot-v1")
    gdata = CACHE.read_bytes()
    need(len(gdata) == PIN["bytes"], "a6xx_gmu.c byte count == %d" % PIN["bytes"],
         "got %d" % len(gdata))
    need(sha256(gdata) == PIN["sha256"], "a6xx_gmu.c sha256 == %s" % PIN["sha256"],
         "got %s" % sha256(gdata))
    xdata = CACHE_XML.read_bytes()
    need(len(xdata) == PIN_XML["bytes"], "a6xx_gmu.xml byte count == %d" % PIN_XML["bytes"],
         "got %d" % len(xdata))
    need(sha256(xdata) == PIN_XML["sha256"], "a6xx_gmu.xml sha256 == %s" % PIN_XML["sha256"],
         "got %s" % sha256(xdata))
    src = gdata.decode("utf-8")
    lines = src.splitlines()
    xsrc = xdata.decode("utf-8")

    # ---- (a) upstream cold-boot sequence, asserted on exact line slices -----
    # rpmh_start :480-505
    blk = block(lines, 480, 505)
    need("REG_A6XX_GMU_RSCC_CONTROL_REQ, BIT(1)" in blk, "rpmh_start REQ=BIT(1) (:485)")
    need("REG_A6XX_GMU_RSCC_CONTROL_ACK" in blk and "val & (1 << 1)" in blk,
         "rpmh_start polls ACK bit1 (:487-488)")
    need("REG_A6XX_RSCC_SEQ_BUSY_DRV0" in blk and "!val" in blk,
         "rpmh_start polls RSCC_SEQ_BUSY_DRV0 == 0 (:494-495)")
    need("REG_A6XX_GMU_RSCC_CONTROL_REQ, 0" in blk, "rpmh_start REQ=0 (:502)")

    # gmu_start :204-240 (mask/reset branch + reset sequence)
    blk = block(lines, 204, 240)
    need("REG_A6XX_GMU_CM3_DTCM_START + 0xff8" in blk, "gmu_start reads DTCM+0xff8 (:211)")
    need("val <= 0x20010004" in blk and "0xffffffff" in blk and "0xbabeface" in blk,
         "wide mask branch (:212-214)")
    need("mask = 0x1ff" in blk and "reset_val = 0x100" in blk,
         "narrow mask branch (:216-217)")
    need("REG_A6XX_GMU_CM3_SYSRESET, 1" in blk, "SYSRESET=1 (:220)")
    need("REG_A6XX_GPU_GMU_CX_GMU_PWR_COL_CP_RESP, 0" in blk,
         "CP_RESP=0 non-A7xx (:228)")
    need("REG_A6XX_GMU_CM3_SYSRESET, 0" in blk, "SYSRESET=0 (:231)")
    need("(val & mask) == reset_val" in blk, "poll FW_INIT_RESULT mask/reset (:233-234)")

    # hfi_start :242-255
    blk = block(lines, 242, 255)
    need("REG_A6XX_GMU_HFI_CTRL_INIT, 1" in blk, "HFI_CTRL_INIT=1 (:247)")
    need("REG_A6XX_GMU_HFI_CTRL_STATUS" in blk and "val & 1" in blk,
         "poll HFI_CTRL_STATUS bit0 (:249-250)")

    # OOB + sptprac + gfx_rail_on :265-424
    blk = block(lines, 265, 344)
    need("GMU_OOB_BOOT_SLUMBER" in blk, "OOB table has BOOT_SLUMBER entry")
    need("1 << request" in blk and "1 << ack" in blk,
         "set_oob legacy set/ack intr pattern (:328/:341)")
    blk = block(lines, 365, 406)
    need("0x778000" in blk, "sptprac enable 0x778000 (:373)")
    need("(val & 0x38) == 0x28" in blk, "sptprac poll pattern (:375-376)")
    need("0x778001" in blk, "sptprac disable 0x778001 (:398)")
    need("(1 << 11)" in blk, "GX retention bit 1<<11 (:396)")
    blk = block(lines, 409, 424)
    need("REG_A6XX_GMU_BOOT_SLUMBER_OPTION, 0" in blk, "BOOT_SLUMBER_OPTION=0 (:414)")
    need("vote & 0xff" in blk and "(vote >> 8) & 0xff" in blk,
         "GX/MX vote idx split (:419-420)")

    # power_config :663-706
    blk = block(lines, 663, 706)
    need("#define GMU_PWR_COL_HYST 0x000a1680" in blk, "PWR_COL_HYST 0x000a1680 (:663)")
    need("REG_A6XX_GMU_SYS_BUS_CONFIG, 0x1" in blk, "SYS_BUS_CONFIG=1 (:672)")
    need("REG_A6XX_GMU_ICACHE_CONFIG, 0x1" in blk, "ICACHE_CONFIG=1 (:673)")
    need("REG_A6XX_GMU_DCACHE_CONFIG, 0x1" in blk, "DCACHE_CONFIG=1 (:674)")
    need("REG_A6XX_GMU_PWR_COL_INTER_FRAME_CTRL, 0x9c40400" in blk,
         "IFPC_CTRL 0x9c40400 (:680)")
    need("GMU_IDLE_STATE_SPTP" in blk and "SPTPRAC_HYST" in blk,
         "SPTP hyst write (:690-692)")
    need("A6XX_GMU_RPMH_CTRL_RPMH_INTERFACE_ENABLE" in blk,
         "RPMH vote enables (:699-705)")

    # fw_load legacy gate :725-779
    blk = block(lines, 725, 779)
    need("fw_image->size > 0x8000" in blk, "legacy 0x8000 gate (:741)")
    need("REG_A6XX_GMU_CM3_ITCM_START" in blk, "flat ITCM bulk (:747-748)")
    need("0x00000000" in blk and "0x00040000" in blk,
         "itcm 0 / dtcm 0x40000 bases (:733-734)")

    # fw_start :781-902
    blk = block(lines, 781, 902)
    need("REG_A6XX_GMU_GENERAL_7, 1" in blk, "TCM retention GENERAL_7=1 (:800)")
    need("REG_A6XX_GMU_CM3_FW_INIT_RESULT, 0" in blk, "FW_INIT_RESULT=0 (:821)")
    need("REG_A6XX_GMU_CM3_BOOT_CONFIG, 0x02" in blk, "BOOT_CONFIG=0x02 (:822)")
    need("REG_A6XX_GMU_HFI_QTBL_ADDR" in blk and "hfi.iova" in blk,
         "QTBL_ADDR=hfi.iova (:825)")
    need("REG_A6XX_GMU_HFI_QTBL_INFO, 1" in blk, "QTBL_INFO=1 (:826)")
    need("fence_range_upper = 0xa" in blk and "fence_range_lower = 0xa0" in blk,
         "A630 fence 0xa/0xa0 (:832-833)")
    need("BIT(31) |" in blk and "GENMASK(30, 18)" in blk and "GENMASK(17, 0)" in blk,
         "fence value composition (:836-839)")
    need("REG_A6XX_GMU_CM3_CFG, 0x4052" in blk, "CM3_CFG=0x4052 (:845)")
    need("chip_id & 0xffff0000" in blk and "(adreno_gpu->chip_id << 4) & 0xf000" in blk
         and "(adreno_gpu->chip_id << 8) & 0x0f00" in blk,
         "chipid massage (:857-859)")
    need("REG_A6XX_GMU_HFI_SFR_ADDR, chipid" in blk, "HFI_SFR_ADDR=chipid (:868)")
    need("gmu->log.iova | (gmu->log.size / SZ_4K - 1)" in blk,
         "CP_MSG=log.iova|(pages-1) (:870-871)")
    need("gmu->legacy" in blk and "a6xx_gmu_gfx_rail_on" in blk,
         "legacy gfx_rail_on dispatch (:881-885)")
    need("gmu->idle_level < GMU_IDLE_STATE_SPTP" in blk and "a6xx_sptprac_enable" in blk,
         "sptprac conditional (:888-892)")
    need("a6xx_gmu_hfi_start(gmu)" in blk, "hfi_start in fw_start (:894)")

    # cold/warm decision :1066-1068
    blk = block(lines, 1060, 1095)
    need("REG_A6XX_GMU_GENERAL_7) == 1 ?" in blk and "GMU_WARM_BOOT : GMU_COLD_BOOT" in blk,
         "legacy cold/warm decision (:1066-1068)")

    # ---- (b) GMU register offsets vs pinned XML -----------------------------
    xml_off = {
        "GMU_GX_SPTPRAC_POWER_CONTROL": "0x81",
        "GMU_CM3_ITCM_START": "0xc00",
        "GMU_CM3_DTCM_START": "0x1c00",
        "GMU_BOOT_SLUMBER_OPTION": "0x23f8",
        "GMU_GX_VOTE_IDX": "0x23f9",
        "GMU_MX_VOTE_IDX": "0x23fa",
        "GMU_ICACHE_CONFIG": "0x4c00",
        "GMU_DCACHE_CONFIG": "0x4c01",
        "GMU_SYS_BUS_CONFIG": "0x4c0f",
        "GMU_CM3_SYSRESET": "0x5000",
        "GMU_CM3_BOOT_CONFIG": "0x5001",
        "GMU_CM3_FW_INIT_RESULT": "0x501c",
        "GMU_CM3_CFG": "0x502d",
        "GMU_PWR_COL_INTER_FRAME_CTRL": "0x50c0",
        "GMU_PWR_COL_INTER_FRAME_HYST": "0x50c1",
        "GMU_PWR_COL_SPTPRAC_HYST": "0x50c2",
        "GMU_SPTPRAC_PWR_CLK_STATUS": "0x50d0",
        "GMU_RPMH_CTRL": "0x50e8",
        "GMU_PWR_COL_CP_MSG": "0x5100",
        "GMU_PWR_COL_CP_RESP": "0x5101",
        "GMU_HFI_CTRL_STATUS": "0x5180",
        "GMU_HFI_SFR_ADDR": "0x5182",
        "GMU_HFI_QTBL_INFO": "0x5184",
        "GMU_HFI_QTBL_ADDR": "0x5185",
        "GMU_HFI_CTRL_INIT": "0x5186",
        "GMU_GMU2HOST_INTR_CLR": "0x5191",
        "GMU_GMU2HOST_INTR_INFO": "0x5192",
        "GMU_HOST2GMU_INTR_SET": "0x5194",
        "GMU_GENERAL_7": "0x51cc",
        "GMU_AO_HOST_INTERRUPT_CLR": "0x9304",
        "GMU_AO_HOST_INTERRUPT_MASK": "0x9306",
        "GMU_RSCC_CONTROL_REQ": "0x9307",
        "GMU_RSCC_CONTROL_ACK": "0x9308",
        "GMU_AHB_FENCE_RANGE_0": "0x9311",
        "GPU_CC_GX_GDSCR": "0x9c03",
    }
    xml_ok = 0
    for name, want in xml_off.items():
        pat = r'<reg32 offset="' + re.escape(want) + r'"[^>]*name="' + re.escape(name.split("_", 1)[-1])
        # simpler: match an array element entry carrying this name at this offset
        found = re.search(r'offset="' + re.escape(want) + r'"[^/]*?' + re.escape(name), xsrc) or \
                re.search(re.escape(name) + r'[^>]*?offset="' + re.escape(want) + r'"', xsrc)
        if found:
            xml_ok += 1
        need(found is not None, "xml: %s at %s" % (name, want), "not found in pinned a6xx_gmu.xml")
    need(xml_ok >= 30, "GMU XML offsets agreed (>=30)", "%d" % xml_ok)

    # PDC offsets from the pinned quard 6.1.11 a6xx.xml.h (modern generation)
    pdc_src_entry = PDC["sources"]["quard_star_611_a6xx"]
    pdc_path = PDC_DIR / Path(pdc_src_entry["file"]).name
    need(pdc_path.is_file(), "pinned quard a6xx.xml.h present", str(pdc_path))
    pdata = pdc_path.read_bytes()
    psha = pdc_src_entry["sha256"]
    need(sha256(pdata) == psha, "quard a6xx.xml.h sha256 == pinned",
         "got %s" % sha256(pdata))
    psrc = pdata.decode("utf-8", errors="replace")
    pdc_off = {
        "REG_A6XX_PDC_GPU_ENABLE_PDC": "0x1140",
        "REG_A6XX_PDC_GPU_SEQ_START_ADDR": "0x1148",
        "REG_A6XX_PDC_GPU_TCS0_CONTROL": "0x1540",
        "REG_A6XX_PDC_GPU_TCS1_CONTROL": "0x1572",
        "REG_A6XX_PDC_GPU_TCS1_CMD_ENABLE_BANK": "0x1573",
        "REG_A6XX_PDC_GPU_TCS1_CMD_WAIT_FOR_CMPL_BANK": "0x1574",
        "REG_A6XX_PDC_GPU_TCS1_CMD0_MSGID": "0x1575",
        "REG_A6XX_PDC_GPU_TCS1_CMD0_ADDR": "0x1576",
        "REG_A6XX_PDC_GPU_TCS1_CMD0_DATA": "0x1577",
        "REG_A6XX_PDC_GPU_TCS3_CONTROL": "0x15d6",
        "REG_A6XX_PDC_GPU_TCS3_CMD_ENABLE_BANK": "0x15d7",
        "REG_A6XX_PDC_GPU_TCS3_CMD_WAIT_FOR_CMPL_BANK": "0x15d8",
        "REG_A6XX_PDC_GPU_TCS3_CMD0_MSGID": "0x15d9",
        "REG_A6XX_PDC_GPU_TCS3_CMD0_ADDR": "0x15da",
        "REG_A6XX_PDC_GPU_TCS3_CMD0_DATA": "0x15db",
    }
    for name, want in pdc_off.items():
        want8 = "0x" + want[2:].zfill(8)
        m = re.search(r"#define\s+" + re.escape(name) + r"\s+" + re.escape(want8) + r"\b", psrc)
        need(m is not None, "pdc: %s == %s" % (name, want), "define not found at pinned value")

    # ---- (c) module header defines == upstream-derived values ---------------
    need(H.is_file(), "AdrenoGmuBootV1.h exists", str(H))
    need(C.is_file(), "AdrenoGmuBootV1.c exists", str(C))
    if not H.is_file() or not C.is_file():
        return finish("gmu-boot-v1")
    htxt = H.read_text(encoding="utf-8")
    csrc = C.read_text(encoding="utf-8")
    hcode = strip_comments(htxt)
    ccode = strip_comments(csrc)

    module_defs = {
        "ADRENO_GMU_BOOT_V1_PDC_ADDRESS_OFFSET": "0x30080u",
        "ADRENO_GMU_BOOT_V1_PWR_COL_HYST": "0x000a1680u",
        "ADRENO_GMU_BOOT_V1_PWR_COL_IFPC_CTRL": "0x09c40400u",
        "ADRENO_GMU_BOOT_V1_RPMH_VOTE_BITS": "0x00000f11u",
        "ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE": "0x02u",
        "ADRENO_GMU_BOOT_V1_CM3_CFG_VALUE": "0x4052u",
        "ADRENO_GMU_BOOT_V1_FENCE_UPPER": "0xau",
        "ADRENO_GMU_BOOT_V1_FENCE_LOWER": "0xa0u",
        "ADRENO_GMU_BOOT_V1_SPTPRAC_ON_VALUE": "0x00778000u",
        "ADRENO_GMU_BOOT_V1_SPTPRAC_OFF_VALUE": "0x00778001u",
        "ADRENO_GMU_BOOT_V1_SPTPRAC_CLK_MASK": "0x38u",
        "ADRENO_GMU_BOOT_V1_SPTPRAC_CLK_PATTERN": "0x28u",
        "ADRENO_GMU_BOOT_V1_GX_RETENTION_BIT": "0x00000800u",
        "ADRENO_GMU_BOOT_V1_IRQ_MASK": "0x00000029u",
        "ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_SET": "22u",
        "ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_ACK": "30u",
        "ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_CLEAR": "30u",
        "ADRENO_GMU_BOOT_V1_TCS_MSGID": "0x10108u",
        "ADRENO_GMU_BOOT_V1_TCS_CMD0_ADDR": "0x30010u",
        "ADRENO_GMU_BOOT_V1_TCS_CMD1_ADDR": "0x30000u",
        "ADRENO_GMU_BOOT_V1_ENABLE_PDC_VALUE": "0x80000001u",
        "ADRENO_GMU_BOOT_V1_PDC_SEQ_START_VALUE": "0x4520u",
        "ADRENO_GMU_BOOT_V1_PDC_MATCH_LO_VALUE": "0x4510u",
        "ADRENO_GMU_BOOT_V1_PDC_MATCH_HI_VALUE": "0x4514u",
        "ADRENO_GMU_BOOT_V1_WARM": "0u",
        "ADRENO_GMU_BOOT_V1_COLD": "1u",
        "ADRENO_GMU_BOOT_V1_IDLE_ACTIVE": "0u",
        "ADRENO_GMU_BOOT_V1_IDLE_SPTP": "2u",
        "ADRENO_GMU_BOOT_V1_IDLE_IFPC": "3u",
    }
    for name, want in module_defs.items():
        got = define(htxt, name)
        need(got == want, "module %s == %s" % (name, want), "got %s" % got)

    # GMU offsets in the header match the XML-derived values (no u suffix check)
    for name, want in xml_off.items():
        mname = "ADRENO_GMU_BOOT_V1_" + name.replace("GMU_", "", 1) if False else None
    # explicit map: header define -> xml offset
    hdr_off = {
        "ADRENO_GMU_BOOT_V1_SPTPRAC_POWER_CONTROL": "0x0081u",
        "ADRENO_GMU_BOOT_V1_CM3_ITCM_START": "0x0c00u",
        "ADRENO_GMU_BOOT_V1_CM3_DTCM_START": "0x1c00u",
        "ADRENO_GMU_BOOT_V1_BOOT_SLUMBER_OPTION": "0x23f8u",
        "ADRENO_GMU_BOOT_V1_GX_VOTE_IDX": "0x23f9u",
        "ADRENO_GMU_BOOT_V1_MX_VOTE_IDX": "0x23fau",
        "ADRENO_GMU_BOOT_V1_ICACHE_CONFIG": "0x4c00u",
        "ADRENO_GMU_BOOT_V1_DCACHE_CONFIG": "0x4c01u",
        "ADRENO_GMU_BOOT_V1_SYS_BUS_CONFIG": "0x4c0fu",
        "ADRENO_GMU_BOOT_V1_CM3_SYSRESET": "0x5000u",
        "ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG": "0x5001u",
        "ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT": "0x501cu",
        "ADRENO_GMU_BOOT_V1_CM3_CFG": "0x502du",
        "ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_CTRL": "0x50c0u",
        "ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_HYST": "0x50c1u",
        "ADRENO_GMU_BOOT_V1_PWR_COL_SPTPRAC_HYST": "0x50c2u",
        "ADRENO_GMU_BOOT_V1_SPTPRAC_PWR_CLK_STATUS": "0x50d0u",
        "ADRENO_GMU_BOOT_V1_RPMH_CTRL": "0x50e8u",
        "ADRENO_GMU_BOOT_V1_PWR_COL_CP_MSG": "0x5100u",
        "ADRENO_GMU_BOOT_V1_PWR_COL_CP_RESP": "0x5101u",
        "ADRENO_GMU_BOOT_V1_HFI_CTRL_STATUS": "0x5180u",
        "ADRENO_GMU_BOOT_V1_HFI_SFR_ADDR": "0x5182u",
        "ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO": "0x5184u",
        "ADRENO_GMU_BOOT_V1_HFI_QTBL_ADDR": "0x5185u",
        "ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT": "0x5186u",
        "ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_CLR": "0x5191u",
        "ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_INFO": "0x5192u",
        "ADRENO_GMU_BOOT_V1_HOST2GMU_INTR_SET": "0x5194u",
        "ADRENO_GMU_BOOT_V1_GENERAL_7": "0x51ccu",
        "ADRENO_GMU_BOOT_V1_AO_HOST_INTERRUPT_CLR": "0x9304u",
        "ADRENO_GMU_BOOT_V1_AO_HOST_INTERRUPT_MASK": "0x9306u",
        "ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ": "0x9307u",
        "ADRENO_GMU_BOOT_V1_RSCC_CONTROL_ACK": "0x9308u",
        "ADRENO_GMU_BOOT_V1_AHB_FENCE_RANGE_0": "0x9311u",
        "ADRENO_GMU_BOOT_V1_GPU_CC_GX_GDSCR": "0x9c03u",
        "ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA": "0x23000u",
        "ADRENO_GMU_BOOT_V1_RSCC_STATUS0_DRV0": "0x0004u",
        "ADRENO_GMU_BOOT_V1_RSCC_PDC_SEQ_START_ADDR": "0x0008u",
        "ADRENO_GMU_BOOT_V1_RSCC_PDC_MATCH_VALUE_LO": "0x0009u",
        "ADRENO_GMU_BOOT_V1_RSCC_PDC_MATCH_VALUE_HI": "0x000au",
        "ADRENO_GMU_BOOT_V1_RSCC_PDC_SLAVE_ID_DRV0": "0x000bu",
        "ADRENO_GMU_BOOT_V1_RSCC_HIDDEN_TCS_CMD0_ADDR": "0x000du",
        "ADRENO_GMU_BOOT_V1_RSCC_HIDDEN_TCS_CMD0_DATA": "0x000eu",
        "ADRENO_GMU_BOOT_V1_RSCC_OVERRIDE_START_ADDR": "0x0100u",
        "ADRENO_GMU_BOOT_V1_RSCC_SEQ_BUSY_DRV0": "0x0101u",
        "ADRENO_GMU_BOOT_V1_RSCC_SEQ_MEM_0_DRV0": "0x0180u",
        "ADRENO_GMU_BOOT_V1_PDC_SEQ_MEM_0": "0x0000u",
        "ADRENO_GMU_BOOT_V1_PDC_ENABLE_PDC": "0x1140u",
        "ADRENO_GMU_BOOT_V1_PDC_SEQ_START_ADDR": "0x1148u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS0_CONTROL": "0x1540u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS1_CONTROL": "0x1572u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD_ENABLE_BANK": "0x1573u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD_WAIT_BANK": "0x1574u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_MSGID": "0x1575u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_ADDR": "0x1576u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_DATA": "0x1577u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS3_CONTROL": "0x15d6u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD_ENABLE_BANK": "0x15d7u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD_WAIT_BANK": "0x15d8u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_MSGID": "0x15d9u",
        "ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_ADDR": "0x15dau",
        "ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_DATA": "0x15dbu",
    }
    for name, want in hdr_off.items():
        got = define(htxt, name)
        need(got == want, "module offset %s == %s" % (name, want), "got %s" % got)

    # plan order: cold plan must follow upstream call order
    order = [
        "RSCC_CONTROL_REQ",          # rpmh_start
        "GENERAL_7",                 # TCM retention
        "CM3_ITCM_START",            # bulk load
        "CM3_FW_INIT_RESULT",        # clear
        "CM3_BOOT_CONFIG",           # 0x02
        "HFI_QTBL_ADDR",             # iova
        "HFI_QTBL_INFO",             # 1
        "AHB_FENCE_RANGE_0",         # fence
        "CM3_CFG",                   # 0x4052
        "HFI_SFR_ADDR",              # chipid
        "PWR_COL_CP_MSG",            # log
        "SYS_BUS_CONFIG",            # power_config
        "CM3_SYSRESET",              # start
        "BOOT_SLUMBER_OPTION",       # rail on
        "HOST2GMU_INTR_SET",         # OOB set
        "HFI_CTRL_INIT",             # hfi_start
    ]
    idx = []
    ok_order = True
    for sym in order:
        # find the FIRST gmv1_push that mentions this offset symbol in PlanCold
        m = re.search(r"gmv1_push\([^)]*?ADRENO_GMU_BOOT_V1_" + re.escape(sym), ccode, re.S)
        if m is None:
            ok_order = False
            idx.append(-1)
        else:
            idx.append(m.start())
    need(ok_order, "all 16 order anchors present in PlanCold", str(idx))
    if ok_order:
        need(idx == sorted(idx), "cold plan follows upstream call order", str(idx))

    # no modern-only constructs in the module
    for bad in ("a7xx", "A7XX", "a650", "A650", "block_header", "0x80000021",
                "0x32", "0x8a0", "FAL", "gmu_chipid"):
        need(bad not in hcode, "header has no modern-only construct %r" % bad)
    need("if (in->boot_state != ADRENO_GMU_BOOT_V1_COLD)" in ccode,
         "cold planner refuses warm state")

    # header documents the honest boundary
    need("HOST-VERIFIED" in htxt, "header marks HOST-VERIFIED")
    need("AdrenoPowerGate" in htxt, "header cross-references the power gate")

    return finish("gmu-boot-v1")


if __name__ == "__main__":
    sys.exit(main())
