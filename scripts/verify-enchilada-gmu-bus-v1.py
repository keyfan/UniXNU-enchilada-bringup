#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the enchilada A6XX_GEN1 GMU bus adapter
(AdrenoGmuBusV1) against the sha256-pinned upstream Linux v6.12 sources.

Method 1 is the host test (drivers/NativeAdreno/tests/adreno-gmu-bus-v1-test.cpp),
which asserts the RUNTIME behaviour of AdrenoGmuBusV1.c on simulated windows.
Method 2 is INDEPENDENT of it: it never runs the adapter. It reads

  (a) the pinned a6xx_gmu.c (sha256 from scripts/data/adreno-a630-inputs.json)
      as raw TEXT and asserts the io contract lines: word-offset addressing,
      the RSCC delta 0x23000, and the PlanRpmhInit op offsets the adapter must
      translate (PDC_ENABLE_PDC / SEQ_START_ADDR / TCS windows);
  (b) the pinned PDC offset sources (scripts/data/adreno-a630-pdc-inputs.json,
      quard 6.1.11 a6xx.xml.h) as raw TEXT and asserts every PDC offset
      ADRENO_GMU_BOOT_V1_PDC_* the adapter consumes through its windows;
  (c) the NEW AdrenoGmuBusV1.h/.c as raw TEXT and asserts the adapter
      implements exactly the word-offset translation (offset << 2), the
      derived RSCC window (gmu_mmio + 0x23000), fail-closed bounds
      ((offset << 2) + 4 <= bytes), and no power/mapping action.

No network, no CI, no flash, no register read, no GPU execution. HOST-VERIFIED:
source text is a specification, not a device measurement.

Usage: python -X utf8 scripts/verify-enchilada-gmu-bus-v1.py
"""
import hashlib
import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
M = json.loads((SCRIPTS / "data/adreno-a630-inputs.json").read_text(encoding="utf-8"))
GMU_KEY = "drivers/gpu/drm/msm/adreno/a6xx_gmu.c"
PIN = M["linux"]["files"][GMU_KEY]
CACHE = DEV / ".cache/adreno-a630-source" / GMU_KEY

PDC = json.loads((SCRIPTS / "data/adreno-a630-pdc-inputs.json").read_text(encoding="utf-8"))
PDC_DIR = DEV / ".cache/adreno-a630-pdc-headers"

H = DEV / "drivers/NativeAdreno/AdrenoGmuBusV1.h"
C = DEV / "drivers/NativeAdreno/AdrenoGmuBusV1.c"

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
    print(f"GMU_BUS_V1_CROSS: {'PASS' if not fails else 'FAIL'} "
          f"({len(CHECKS)} checks, {len(fails)} failures)")
    return 0 if not fails else 1


def main():
    # ---- (0) pinned upstream integrity --------------------------------------
    need(CACHE.is_file(), "pinned a6xx_gmu.c present in cache", str(CACHE))
    if not CACHE.is_file():
        return finish("gmu-bus-v1")
    gdata = CACHE.read_bytes()
    need(len(gdata) == PIN["bytes"], "a6xx_gmu.c byte count == %d" % PIN["bytes"],
         "got %d" % len(gdata))
    need(sha256(gdata) == PIN["sha256"], "a6xx_gmu.c sha256 == %s" % PIN["sha256"],
         "got %s" % sha256(gdata))
    src = gdata.decode("utf-8")
    lines = src.splitlines()

    # ---- (a) upstream io contract, asserted on exact line slices ------------
    # gmu_read/gmu_write word-offset addressing (a6xx_gmu.h:104-115; the .c
    # helpers gmu_read/gmu_write are defined in the header but used everywhere).
    blk = block(lines, 1765, 1775)
    need("gmu->rscc = gmu->mmio + 0x23000" in blk,
         "rscc window = gmu mmio + 0x23000 (:1771)")
    # rscc accessors word-offset (SEQ_BUSY polled via gmu_poll_timeout_rscc :494)
    need("gmu_poll_timeout_rscc(gmu, REG_A6XX_RSCC_SEQ_BUSY_DRV0" in src,
         "upstream polls RSCC_SEQ_BUSY_DRV0 through the RSCC window (:494)")
    # setup_pdc writes through the pdc window (lines 641-643)
    blk = block(lines, 641, 643)
    need("REG_A6XX_PDC_GPU_SEQ_START_ADDR, 0" in blk,
         "setup_pdc writes PDC_SEQ_START_ADDR=0 (:642)")
    need("REG_A6XX_PDC_GPU_ENABLE_PDC, 0x80000001" in blk,
         "setup_pdc writes ENABLE_PDC=0x80000001 (:643)")
    # a6xx_gmu_rpmh_init seq_mem writes (lines 590-605)
    blk = block(lines, 590, 605)
    need("gmu_write_rscc(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0, 0xa7a506a0)" in blk,
         "rsc ucode into RSCC_SEQ_MEM_0_DRV0 (:590-594)")
    need("pdc_write(seqptr, REG_A6XX_PDC_GPU_SEQ_MEM_0, 0xfebea1e1)" in blk,
         "pdc ucode into PDC_SEQ_MEM_0 (:601-605)")

    # ---- (b) PDC offsets the adapter translates -----------------------------
    pdc_src_entry = PDC["sources"]["quard_star_611_a6xx"]
    pdc_path = PDC_DIR / Path(pdc_src_entry["file"]).name
    need(pdc_path.is_file(), "pinned quard a6xx.xml.h present", str(pdc_path))
    if not pdc_path.is_file():
        return finish("gmu-bus-v1")
    pdata = pdc_path.read_bytes()
    need(sha256(pdata) == pdc_src_entry["sha256"], "quard a6xx.xml.h sha256 == pinned",
         "got %s" % sha256(pdata))
    psrc = pdata.decode("utf-8", errors="replace")
    pdc_off = {
        "REG_A6XX_PDC_GPU_ENABLE_PDC": "0x1140",
        "REG_A6XX_PDC_GPU_SEQ_START_ADDR": "0x1148",
        "REG_A6XX_PDC_GPU_TCS3_CMD0_DATA": "0x15db",
    }
    for name, want in pdc_off.items():
        want8 = "0x" + want[2:].zfill(8)
        m = re.search(r"#define\s+" + re.escape(name) + r"\s+" + re.escape(want8) + r"\b", psrc)
        need(m is not None, "pdc: %s == %s" % (name, want), "define not found at pinned value")

    # ---- (c) the adapter implements exactly the translation -----------------
    need(H.is_file(), "AdrenoGmuBusV1.h exists", str(H))
    need(C.is_file(), "AdrenoGmuBusV1.c exists", str(C))
    if not H.is_file() or not C.is_file():
        return finish("gmu-bus-v1")
    htxt = H.read_text(encoding="utf-8")
    csrc = C.read_text(encoding="utf-8")
    hcode = strip_comments(htxt)
    ccode = strip_comments(csrc)

    got = define(htxt, "ADRENO_GMU_BUS_V1_RSCC_DELTA")
    need(got == "0x23000u", "adapter RSCC_DELTA == 0x23000u", "got %s" % got)

    # domain enum order matches the model 1:1
    for sym, val in (("kAdrenoGmuBusV1DomainGmu", "0"),
                     ("kAdrenoGmuBusV1DomainRscc", "1"),
                     ("kAdrenoGmuBusV1DomainPdcSeq", "2"),
                     ("kAdrenoGmuBusV1DomainPdc", "3"),
                     ("kAdrenoGmuBusV1DomainCount", "4")):
        m = re.search(re.escape(sym) + r"\s*=\s*" + val + r"\b", hcode)
        need(m is not None, "domain %s == %s" % (sym, val), "enum entry not found")

    # the translation IS word-offset: the << 2 shift must exist in the resolve path
    need("(uint64_t)word_offset << 2" in ccode or "word_offset << 2" in ccode,
         "resolve multiplies word offset by 4 (word-offset addressing)")
    # bounds: offset+4 <= bytes (fail closed)
    need("+ 4u > win" in ccode or "+ 4u > c->bytes[domain]" in ccode,
         "bounds check keeps the last 4 bytes addressable")
    # RSCC window derived, not caller-supplied
    need("windows->gmu_mmio + ADRENO_GMU_BUS_V1_RSCC_DELTA" in ccode,
         "RSCC base derived from GMU window")
    need("windows->gmu_bytes - ADRENO_GMU_BUS_V1_RSCC_DELTA" in ccode,
         "RSCC byte guard = gmu_bytes - delta")
    # fail-closed refusals. ROUND 46 CHANGED THIS CONTRACT: values == NULL is
    # legal ONLY when a bulk image was registered, in which case the cookie
    # resolves the ITCM image. The unconditional "values == 0 -> refuse" check
    # was deliberately removed, so asserting it would now be asserting a rule
    # the source no longer has. Assert the ACTUAL round-46 shape instead, which
    # is a two-part refusal: NULL needs a registered image AND a sane word count.
    need("if (c->bulk_image == 0 || words == 0 || words > c->bulk_words)"
         in ccode, "bulk: NULL data needs a registered image of adequate size")
    need("if (src == 0) {" in ccode, "bulk: NULL resolution is conditional")
    need("src = c->bulk_image;" in ccode, "bulk: the cookie supplies the image")
    need("if (words == 0)" in ccode, "bulk refuses a zero word count")
    need("domain >= kAdrenoGmuBusV1DomainCount" in ccode, "unknown domain refused")
    need("c->base[domain] == 0" in ccode, "NULL window refused")
    # typedef shares the model bus type
    need("typedef AdrenoGmuBootV1Bus AdrenoGmuBusV1" in hcode,
         "adapter bus type == model bus type (same callbacks)")
    # no power/mapping action in the adapter
    for bad in ("clock_enable", "regulator", "genpd", "power_domain",
                "gmu_rpmh_start", "PlanCold", "hw_init", "cache"):
        need(bad not in ccode, "adapter performs no %s action" % bad)
    # header documents the honest boundary
    need("HOST-VERIFIED" in htxt, "header marks HOST-VERIFIED")
    need("NO mapping" in htxt or "no mapping" in htxt,
         "header documents no-mapping contract")

    return finish("gmu-bus-v1")


if __name__ == "__main__":
    sys.exit(main())
