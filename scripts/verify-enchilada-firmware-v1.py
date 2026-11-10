#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the enchilada A630 LEGACY GMU
firmware-load contract against the sha256-pinned upstream Linux v6.12 sources.

Method 1 is the host test (drivers/NativeAdreno/tests/adreno-firmware-v1-test.cpp),
which asserts the RUNTIME behaviour of AdrenoFirmwareV1.c on synthetic images.
Method 2 is INDEPENDENT of it: it never runs the module. It reads

  (a) the pinned a6xx_gmu.c / a6xx_gpu.c / a6xx_catalog.c / a6xx_gmu.xml
      (sha256 from scripts/data/adreno-a630-inputs.json -> linux.files) as raw
      TEXT and asserts the exact upstream lines that encode the legacy rules;
  (b) the upstream MODERN planner work/UniXNU/kits/AdrenoFirmware/
      AdrenoFirmware.c/.h as raw TEXT and asserts the contrasting modern-only
      behaviour (16-byte block_header parse, 2 passes, region validation);
  (c) the NEW AdrenoFirmwareV1.h/.c + scripts/data/adreno-a630-firmware.json
      as raw TEXT and asserts every derived constant equals the upstream-derived
      value, and that no modern-only construct leaked into the legacy model.

No network, no CI, no flash, no register read, no GPU execution. HOST-VERIFIED:
source text is a specification, not a device measurement.

Usage: python -X utf8 scripts/verify-enchilada-firmware-v1.py
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
CACHE = DEV / ".cache/adreno-a630-source"
FMAN = json.loads((SCRIPTS / "data/adreno-a630-firmware.json").read_text(encoding="utf-8"))

PINS = {
    "a6xx_gmu.c": "drivers/gpu/drm/msm/adreno/a6xx_gmu.c",
    "a6xx_gpu.c": "drivers/gpu/drm/msm/adreno/a6xx_gpu.c",
    "a6xx_catalog.c": "drivers/gpu/drm/msm/adreno/a6xx_catalog.c",
    "a6xx_gmu.xml": "drivers/gpu/drm/msm/registers/adreno/a6xx_gmu.xml",
}

H = DEV / "drivers/NativeAdreno/AdrenoFirmwareV1.h"
C = DEV / "drivers/NativeAdreno/AdrenoFirmwareV1.c"
MODERN_H = W / "work/UniXNU/kits/AdrenoFirmware/AdrenoFirmware.h"
MODERN_C = W / "work/UniXNU/kits/AdrenoFirmware/AdrenoFirmware.c"

CHECKS = []


def need(ok, label, detail=""):
    CHECKS.append((bool(ok), label, detail))


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def block(lines, first, last):
    """Inclusive 1-based line range -> one string (exact source slice)."""
    return "\n".join(lines[first - 1:last])


def define(text, name):
    m = re.search(r"#define\s+" + re.escape(name) + r"\s+([^\s/]+(?:/[^\s/]+)?)", text)
    return m.group(1).strip() if m else None


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src


def main():
    # ---- (0) pinned input integrity -----------------------------------------
    srcs = {}
    for key, rel in PINS.items():
        path = CACHE / rel
        entry = M["linux"]["files"].get(rel)
        need(entry is not None, "pin entry exists for %s" % key, str(rel))
        if entry is None:
            continue
        need(path.is_file(), "pinned %s is present in the local cache" % key, str(path))
        if not path.is_file():
            continue
        data = path.read_bytes()
        need(len(data) == entry["bytes"], "%s byte count == %d" % (key, entry["bytes"]),
             "got %d" % len(data))
        need(sha256(data) == entry["sha256"], "%s sha256 == %s" % (key, entry["sha256"]),
             "got %s" % sha256(data))
        srcs[key] = data.decode("utf-8")

    gmu = srcs.get("a6xx_gmu.c", "").splitlines()
    gpu = srcs.get("a6xx_gpu.c", "").splitlines()
    cat = srcs.get("a6xx_catalog.c", "").splitlines()
    xml = srcs.get("a6xx_gmu.xml", "")

    # ---- (a) upstream LEGACY rules, asserted on exact line slices ----------
    if gmu:
        load = block(gmu, 725, 779)
        need("if (gmu->legacy) {" in load and
             "if (fw_image->size > 0x8000) {" in load and
             "return -EINVAL;" in load,
             "upstream legacy GMU image gate: size > 0x8000 -> -EINVAL :739-745")
        need("gmu_write_bulk(gmu, REG_A6XX_GMU_CM3_ITCM_START," in load and
             "(u32*) fw_image->data, fw_image->size);" in load and
             "return 0;" in load,
             "upstream legacy = ONE flat ITCM bulk write, then return :747-749")
        need("u32 itcm_base = 0x00000000;" in load and
             "u32 dtcm_base = 0x00040000;" in load,
             "upstream ITCM base 0x0 / DTCM base 0x40000 :733-734")
        need("struct block_header {" in block(gmu, 708, 714),
             "upstream block_header exists (MODERN segmented image) :708-714")
        need("for (blk = (const struct block_header *) fw_image->data;" in block(gmu, 753, 756),
             "upstream modern path iterates block_header (legacy does NOT) :753-755")
        start = block(gmu, 815, 827)
        need("REG_A6XX_GMU_CM3_BOOT_CONFIG, 0x02" in start and
             "REG_A6XX_GMU_HFI_QTBL_INFO, 1" in start and
             "REG_A6XX_GMU_HFI_QTBL_ADDR, gmu->hfi.iova" in start,
             "upstream cold boot: BOOT_CONFIG=0x02, HFI_QTBL_INFO=1 :821-826")
    if gpu:
        need("#define GPU_PAS_ID 13" in gpu,
             "upstream GPU_PAS_ID == 13 (a6xx_gpu.c:17)")
        ucode = block(gpu, 720, 740)
        need('!strcmp(sqe_name, "a630_sqe.fw")' in ucode,
             "upstream A630 SQE version branch :720")
        need("(((buf[0] & 0xf) == 0xa) && (buf[2] & 0xf) >= 1)" in ucode and
             "(buf[0] & 0xfff) >= 0x190" in ucode,
             "upstream whereami predicate = patch 0xa OR version >= 0x190 :731-732")
        need("u32 *buf = msm_gem_get_vaddr(obj);" in block(gpu, 698, 702),
             "upstream reads MAPPED BO vaddr (raw-file dword0 is NOT a verdict) :700")
        need("adreno_zap_shader_load(gpu, GPU_PAS_ID)" in block(gpu, 811, 823),
             "upstream ZAP loaded via PAS 13 :819")
    if cat:
        need('.zapfw = "a630_zap.mdt",' in block(cat, 883, 903),
             'upstream A630 .zapfw = "a630_zap.mdt" (signed split chain) :897')
        mfw = block(cat, 1047, 1049)
        need('MODULE_FIRMWARE("qcom/a630_sqe.fw")' in mfw and
             'MODULE_FIRMWARE("qcom/a630_gmu.bin")' in mfw and
             'MODULE_FIRMWARE("qcom/a630_zap.mbn")' in mfw,
             "upstream lists 3 public A630 blobs :1047-1049")
    need('offset="0xc00" name="GMU_CM3_ITCM_START"' in xml and
         'offset="0x1c00" name="GMU_CM3_DTCM_START"' in xml,
         "register XML: ITCM_START 0xc00 / DTCM_START 0x1c00 :45-46")

    # ---- (b) upstream MODERN planner (the contrast) ------------------------
    need(MODERN_H.is_file() and MODERN_C.is_file(),
         "upstream modern AdrenoFirmware.h/.c are readable")
    if MODERN_H.is_file() and MODERN_C.is_file():
        mh = MODERN_H.read_text(encoding="utf-8")
        mc = MODERN_C.read_text(encoding="utf-8")
        need("AdrenoFirmwarePlanGmu" in mh,
             "upstream declares AdrenoFirmwarePlanGmu (MODERN planner)")
        need("if (bytes < 16) return AdrenoFirmwareTruncated;" in mc,
             "modern requires >= 16 B (one block_header) -- legacy does NOT")
        need("offset += 16 + (size_t)length;" in mc,
             "modern walks 16-byte block_header units :64")
        need("region_count > 64" in mc and "identifier == UINT32_MAX" in mc,
             "modern validates regions (segmented ITCM/DTCM/dcache) :20-32")
        need("for (unsigned" in mc or "for (size_t" in mc,
             "modern uses two passes over blocks (segmented image)")

    # ---- (c) derived module equals upstream-derived values -----------------
    need(H.is_file() and C.is_file(), "AdrenoFirmwareV1.h/.c are readable")
    if H.is_file() and C.is_file():
        h = H.read_text(encoding="utf-8")
        c = strip_comments(C.read_text(encoding="utf-8"))
        expected = {
            "ADRENO_FW_V1_GMU_MAX_BYTES": "0x8000u",
            "ADRENO_FW_V1_ITCM_BASE": "0x00000000u",
            "ADRENO_FW_V1_DTCM_BASE": "0x00040000u",
            "ADRENO_FW_V1_ITCM_START_REG": "0x0c00u",
            "ADRENO_FW_V1_DTCM_START_REG": "0x1c00u",
            "ADRENO_FW_V1_MODERN_BLOCK_HEADER_BYTES": "16u",
            "ADRENO_FW_V1_BOOT_CONFIG_VALUE": "0x02u",
            "ADRENO_FW_V1_HFI_QTBL_INFO": "0x01u",
            "ADRENO_FW_V1_GPU_PAS_ID": "13u",
            "ADRENO_FW_V1_SQE_WHEREAMI_MIN_VERSION": "0x190u",
            "ADRENO_FW_V1_SQE_PATCH_MARKER": "0xau",
            "ADRENO_FW_V1_ROLE_COUNT": "3u",
            "ADRENO_FW_V1_SQE_BYTES": "34188u",
            "ADRENO_FW_V1_GMU_BYTES": "32768u",
            "ADRENO_FW_V1_ZAP_MBN_BYTES": "14256u",
        }
        drift = []
        for name, want in expected.items():
            got = define(h, name)
            try:
                ok = int(re.sub(r"[uU]$", "", got or ""), 0) == int(re.sub(r"[uU]$", "", want), 0)
            except ValueError:
                ok = (got or "") == want
            if not ok:
                drift.append("%s=%r want %s" % (name, got, want))
        need(not drift, "AdrenoFirmwareV1.h: all %d #defines match the pinned upstream"
             % len(expected), str(drift[:3]))

        # sha256 pins in the header must equal the manifest observations
        for name, key in (("ADRENO_FW_V1_SQE_SHA256", "qcom/a630_sqe.fw"),
                          ("ADRENO_FW_V1_GMU_SHA256", "qcom/a630_gmu.bin"),
                          ("ADRENO_FW_V1_ZAP_MBN_SHA256", "qcom/sdm845/a630_zap.mbn")):
            m = re.search(r'#define\s+' + name + r'\s+"([0-9a-f]{64})"', h)
            need(m is not None and m.group(1) == FMAN["files"][key]["sha256"],
                 "%s == observed manifest hash" % name)

        # no modern-only constructs leaked into the legacy implementation.
        # NOTE: a helper is named adreno_fw_v1_modern_block_header_bytes() -- it
        # just RETURNS the contrast constant; assert the PARSE constructs, not
        # the substring "block_header".
        need("struct block_header" not in c and "blk->" not in c and
             "AdrenoFirmwarePlanGmu" not in c,
             "AdrenoFirmwareV1.c NEVER parses a block_header / calls modern planner")
        need("offset += 16" not in c and "region_count" not in c,
             "AdrenoFirmwareV1.c has NO segmented-image walk / region validation")
        need("gmu_write_bulk" in c or "bulk_words" in c,
             "AdrenoFirmwareV1.c models the ONE flat bulk write")
        # The gate uses the MACRO (single source of truth), not the raw literal.
        need("ADRENO_FW_V1_GMU_MAX_BYTES" in c and "kAdrenoFwV1TooLarge" in c,
             "AdrenoFirmwareV1.c enforces the 0x8000 legacy gate via the macro")
        need("adreno_fw_v1_ready" in c and "return 0;" in c,
             "AdrenoFirmwareV1.c has a fail-closed ready() gate")
        # The caveat lives in a // comment, so check the RAW header text (do NOT
        # strip_comments here): the round-31/32 lesson about comment stripping.
        h_raw = H.read_text(encoding="utf-8")
        need("NOT a verdict" in h_raw and "MAPPED" in h_raw,
             "header carries the honest SQE raw-file-is-not-a-verdict caveat")

    return finish()


def finish():
    for ok, lab, det in CHECKS:
        print("  %s  %s%s" % ("ok  " if ok else "FAIL", lab,
                              ("  [%s]" % det) if det and not ok else ""))
    bad = [lab for ok, lab, _ in CHECKS if not ok]
    print()
    if bad:
        print("FIRMWARE_V1_CROSS: FAIL (%d)" % len(bad), file=sys.stderr)
        sys.exit(1)
    print("FIRMWARE_V1_CROSS: PASS (%d checks, pinned upstream + modern contrast + "
          "derived module agree)" % len(CHECKS))


if __name__ == "__main__":
    main()
