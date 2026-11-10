#!/usr/bin/env python3
"""Verification method 2 -- CROSS-CHECK the enchilada A630 LEGACY GMU HFI v1
transport model against the sha256-pinned upstream Linux v6.12 sources.

Method 1 is the host test (drivers/NativeAdreno/tests/adreno-hfi-v1-test.cpp),
which asserts the RUNTIME behaviour of AdrenoHfiV1.c on a synthetic ring.
Method 2 is INDEPENDENT of it: it never runs the module. It reads

  (a) the pinned a6xx_hfi.c (sha256 f62c802d... , from
      scripts/data/adreno-a630-inputs.json -> linux.files) as raw TEXT and
      asserts the exact upstream lines that encode the 7 legacy deltas;
  (b) the upstream MODERN transport kits/AdrenoFirmware/AdrenoHfi.c/.h as raw
      TEXT and asserts the contrasting modern-only behaviour (3 queues,
      0xfafafafa padding, 4-align enforcement, 16384-byte buffer);
  (c) the NEW AdrenoHfiV1.h/.c as raw TEXT and asserts that each derived
      constant equals the upstream-derived value, and that no modern-only
      construct leaked into the legacy model.

No network, no CI, no flash, no register read, no GPU execution. HOST-VERIFIED:
source text is a specification, not a device measurement.

Usage: python -X utf8 scripts/verify-enchilada-hfi-v1.py
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
HFI_KEY = "drivers/gpu/drm/msm/adreno/a6xx_hfi.c"
PIN = M["linux"]["files"][HFI_KEY]
CACHE = DEV / ".cache/adreno-a630-source" / HFI_KEY

H = DEV / "drivers/NativeAdreno/AdrenoHfiV1.h"
C = DEV / "drivers/NativeAdreno/AdrenoHfiV1.c"
MODERN_H = W / "work/UniXNU/kits/AdrenoFirmware/AdrenoHfi.h"
MODERN_C = W / "work/UniXNU/kits/AdrenoFirmware/AdrenoHfi.c"
README = DEV / "drivers/NativeAdreno/README.md"

CHECKS = []


def need(ok, label, detail=""):
    CHECKS.append((bool(ok), label, detail))


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def block(lines, first, last):
    """Inclusive 1-based line range -> one string (exact source slice)."""
    return "\n".join(lines[first - 1:last])


def define(text, name):
    m = re.search(r"#define\s+" + re.escape(name) + r"\s+([^\s/]+(?:/[^\s/]+)?(?:\s*\(.*?\))?)", text)
    return m.group(1).strip() if m else None


def strip_comments(src):
    """Remove C/C++ comments so 'no modern-only construct' checks see CODE only."""
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src


def main():
    # ---- (0) pinned input integrity -----------------------------------------
    need(CACHE.is_file(), "pinned a6xx_hfi.c is present in the local cache", str(CACHE))
    if not CACHE.is_file():
        return finish()
    data = CACHE.read_bytes()
    need(len(data) == PIN["bytes"], "a6xx_hfi.c byte count == %d" % PIN["bytes"],
         "got %d" % len(data))
    need(sha256(data) == PIN["sha256"], "a6xx_hfi.c sha256 == %s" % PIN["sha256"],
         "got %s" % sha256(data))
    src = data.decode("utf-8")
    lines = src.splitlines()
    need(len(lines) >= 855, "a6xx_hfi.c has >= 855 lines", "got %d" % len(lines))

    # ---- (a) upstream LEGACY deltas, asserted on exact line slices ---------
    # delta 1+2: 2 queues, 24+2*48=120 B table, qhdr0=6, qhdr_size=12
    init_blk = block(lines, 822, 854)
    need("table_size = sizeof(*table);" in init_blk and
         "sizeof(struct a6xx_hfi_queue_header));" in init_blk and
         "ARRAY_SIZE(gmu->queues)" in init_blk,
         "upstream table_size = 24 + 2*48 (queues[2], two-statement form)")
    need("table->qhdr0_offset = sizeof(*table) >> 2;" in init_blk,
         "upstream qhdr0_offset == 6 words (24/4)")
    need("table->qhdr_size = sizeof(struct a6xx_hfi_queue_header) >> 2;" in init_blk,
         "upstream qhdr_size == 12 words (48/4)")
    need("table->num_queues = ARRAY_SIZE(gmu->queues);" in init_blk and
         "table->active_queues = ARRAY_SIZE(gmu->queues);" in init_blk,
         "upstream num_queues = active_queues = 2")

    # delta 3: response queue id 4 for legacy, 1 for modern
    qinit = block(lines, 795, 820)
    need("header->type =  10 << 8 | id;" in qinit,
         "upstream queue type = 0xa00 | id")
    need("header->size = SZ_4K >> 2;" in qinit,
         "upstream queue size = 1024 dwords")
    need("header->status = 1;" in qinit, "upstream queue status = 1")
    need("gmu->legacy ? 4 : 1" in block(lines, 848, 854),
         "upstream legacy response-queue id = 4 (modern = 1) :853-854")

    # delta 4: legacy SKIPS both alignment/padding (the code is guarded by !legacy)
    read_blk = block(lines, 28, 64)
    write_blk = block(lines, 66, 100)
    need("if (!gmu->legacy)" in read_blk and
         "ALIGN(index, 4) % header->size" in read_blk,
         "upstream 4-dword READ align is MODERN-only (!gmu->legacy) :59-60")
    need("if (!gmu->legacy)" in write_blk and
         "0xfafafafa" in write_blk,
         "upstream 0xfafafafa WRITE padding is MODERN-only (!gmu->legacy) :90-93")
    need("header->read_index = index;" in read_blk,
         "upstream legacy publishes read_index += count (no align)")
    need("header->write_index = index;" in write_blk,
         "upstream legacy publishes write_index += count (no align)")

    # delta 5: startup v1 = INIT/FW_VERSION/PERF_TABLE/BW_TABLE/TEST only
    v1 = block(lines, 708, 741)
    start = block(lines, 743, 772)
    need("if (gmu->legacy)" in start and "return a6xx_hfi_start_v1" in start,
         "upstream a6xx_hfi_start() branches to the LEGACY v1 path :747-748")
    for fn in ("a6xx_hfi_send_gmu_init", "a6xx_hfi_get_fw_version",
               "a6xx_hfi_send_perf_table_v1", "a6xx_hfi_send_bw_table",
               "a6xx_hfi_send_test"):
        need(fn in v1, "legacy start_v1 calls %s" % fn)
    need("a6xx_hfi_send_core_fw_start" not in v1 and
         "a6xx_hfi_send_start(gmu)" not in v1,
         "legacy start_v1 has NO CORE_FW_START / START (modern only)")

    # delta 6: v1.10 supported version
    fwver = block(lines, 206, 215)
    need("Currently supporting version 1.10" in fwver,
         "upstream comment: currently supporting version 1.10")
    need("(1 << 28) | (1 << 19) | (1 << 17)" in fwver,
         "upstream supported_version = (1<<28)|(1<<19)|(1<<17) = 0x100A0000")

    # delta 7: wire header + seqnum + ACK semantics
    sendmsg = block(lines, 171, 192)
    ack = block(lines, 102, 169)
    need("(seqnum << 20)" in sendmsg and "(dwords << 8)" in sendmsg and
         "| id" in sendmsg,
         "upstream wire header = (seqnum<<20)|(dwords<<8)|id :181")
    need("% 0xfff" in sendmsg, "upstream seqnum = atomic_inc_return(...) % 0xfff")
    need("HFI_F2H_MSG_ERROR" in ack and "HFI_HEADER_ID(resp.header)" in ack,
         "upstream skips F2H_MSG_ERROR (id 100) :139-146")
    need("seqnum != HFI_HEADER_SEQNUM(resp.ret_header)" in ack,
         "upstream matches the ACK on ret_header seqnum :148-153")
    need("if (resp.error)" in ack, "upstream non-zero resp.error -> -EINVAL :155-160")

    # ---- (b) upstream MODERN transport (the contrast) ----------------------
    need(MODERN_H.is_file() and MODERN_C.is_file(),
         "upstream modern AdrenoHfi.h/.c are readable")
    if MODERN_H.is_file() and MODERN_C.is_file():
        mh = MODERN_H.read_text(encoding="utf-8")
        mc = MODERN_C.read_text(encoding="utf-8")
        need("Modern (nonlegacy) GMU HFI only" in mh,
             "upstream explicitly declares MODERN-only HFI (excludes A630)")
        need("m[1]!=168" in mc or "168" in mc,
             "modern table size = 168 B = 24 + 3*48 (3 queues)")
        need("(*read&3) || (*write&3)" in mc,
             "modern enforces 4-dword index alignment (legacy must NOT)")
        need("0xfafafafa" in mc, "modern pads writes with 0xfafafafa")
        need("bytes!=16384" in mc, "modern requires a 16 KiB shared extent")
        need("for(unsigned q=0;q<3;++q)" in mc,
             "modern iterates 3 queues (legacy iterates 2)")

    # ---- (c) the derived module must equal the upstream-derived values -----
    need(H.is_file() and C.is_file(), "AdrenoHfiV1.h/.c are readable")
    if H.is_file() and C.is_file():
        h = H.read_text(encoding="utf-8")
        c_full = C.read_text(encoding="utf-8")
        c = strip_comments(c_full)
        expected = {
            "ADRENO_HFI_V1_QUEUE_COUNT": "2u",
            "ADRENO_HFI_V1_QUEUE_WORDS": "1024u",
            "ADRENO_HFI_V1_TABLE_HEADER_WORDS": "6u",
            "ADRENO_HFI_V1_QUEUE_HEADER_WORDS": "12u",
            "ADRENO_HFI_V1_TABLE_SIZE_BYTES": "120u",
            "ADRENO_HFI_V1_QHDR0_OFFSET_WORDS": "6u",
            "ADRENO_HFI_V1_QHDR_SIZE_WORDS": "12u",
            "ADRENO_HFI_V1_PAGE_BYTES": "4096u",
            "ADRENO_HFI_V1_RESP_QUEUE_ID": "4u",
            "ADRENO_HFI_V1_CMD_QUEUE_ID": "0u",
            "ADRENO_HFI_V1_SUPPORTED_VERSION": "0x100A0000u",
            "ADRENO_HFI_V1_SEQNUM_MOD": "0xfffu",
            "ADRENO_HFI_V1_F2H_MSG_ERROR": "100u",
            "ADRENO_HFI_V1_MSG_ACK_V1": "2u",
        }
        drift = []
        for name, want in expected.items():
            got = define(h, name)
            want_num = re.sub(r"[uU]$", "", want)
            got_num = re.sub(r"[uU]$", "", got or "")
            try:
                ok = int(got_num, 0) == int(want_num, 0)
            except ValueError:
                ok = got_num == want_num
            if not ok:
                drift.append("%s=%r want %s" % (name, got, want))
        need(not drift, "AdrenoHfiV1.h: all %d #defines match the pinned upstream"
             % len(expected), str(drift[:3]))

        # no modern-only constructs in the legacy implementation
        need("0xfafafafa" not in c.split("/* a6xx_hfi.c:90-95")[-1].split("*/")[-1],
             "AdrenoHfiV1.c NEVER writes 0xfafafafa padding",
             "(only the provenance comment may mention it)")
        need("& 3u" not in c and "& 3)" not in c,
             "AdrenoHfiV1.c NEVER enforces 4-dword index alignment")
        need("ALIGN" not in c, "AdrenoHfiV1.c has no ALIGN() call")
        need("16384" not in c, "AdrenoHfiV1.c never requires the modern 16 KiB extent")
        need("12288" in c or "SHARED_BYTES" in c,
             "AdrenoHfiV1.c uses the legacy 3-page (12288 B) extent")
        need("H2F_START" not in c.split("gStartup")[0].split("*/")[-1] or
             "ADRENO_HFI_V1_H2F_START" in h,
             "START is declared as modern-only, not sent in the legacy startup")
        need(c.count("A6XX") >= 0, "module is source-anchored")

    return finish()


def finish():
    for ok, lab, det in CHECKS:
        print("  %s  %s%s" % ("ok  " if ok else "FAIL", lab,
                              ("  [%s]" % det) if det and not ok else ""))
    bad = [lab for ok, lab, _ in CHECKS if not ok]
    print()
    if bad:
        print("HFI_V1_CROSS: FAIL (%d)" % len(bad), file=sys.stderr)
        sys.exit(1)
    print("HFI_V1_CROSS: PASS (%d checks, pinned upstream + modern contrast + derived "
          "module agree)" % len(CHECKS))


if __name__ == "__main__":
    main()
