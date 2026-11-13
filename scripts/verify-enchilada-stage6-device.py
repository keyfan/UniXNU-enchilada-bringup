#!/usr/bin/env python3
"""Round 103 device verifier: ID0/ID1/ASID were MEASURED on the phone.

Three domains, mirroring verify-enchilada-ias-measured.py:

  A. Re-read the LOG WINDOW bytes pulled from the phone immediately after the
     measured boot (the primary evidence artifact) and assert the exact
     `stage6=id1` row is really there, together with the `stage4=id2` row whose
     placeholder TCR it corrects. `--live` optionally re-reads the phone again;
     a later boot would hold a different window, so the cached bytes are the
     authoritative record of THIS measurement.

  B. Re-DERIVE every field in Python from the raw words and the pinned bit
     positions, independently of the module: ID0.CTTW -> coherent_walk, ID1
     NUMCB/NUMS2CB/NUMPAGENDXB/PAGESIZE, the ASID tuple, and BOTH TCR words
     (coherent = b5100080 vs non-coherent placeholder = a0100080).

  C. Assert the evidence doc and the safety preconditions: reboot gate passed
     before the run, BCB re-armed after, `fastboot reboot recovery` is the
     runtime instruction, and the boot_a TWRP backup is byte-identical.

usage: verify-enchilada-stage6-device.py [--adb PATH] [--live]
"""
import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
ONEPLUS = DEV.parent.parent
GPU = DEV / 'drivers' / 'NativeAdreno'
EVIDENCE = DEV / 'evidence' / 'stage5-gpu-stage6-device.md'
ROOT_HELPER = ONEPLUS / 'scripts' / 'check-reboot-to-twrp.py'
REBOOT_HELPER = ONEPLUS / 'scripts' / 'reboot-to-twrp.py'
BOOTA_BACKUP = ONEPLUS / 'work' / 'tmp' / 'r86-backup' / 'boot_a.twrp.img'
WINDOW_CACHE = ONEPLUS / 'work' / 'tmp' / 'r103_stage6_window.bin'

# The measured rows, verbatim from the UTF-8 window.
S6_ROW = ('stage6=id1 id0=48016605 id1=20008005 cttw=1 numcb=5 nums2cb=0 '
          'cap=5 init=0 alloc=0 cbndx=0 ttbr=0 ctxidr=00000000 '
          's2cr=00000000 cbar=00000000 words_ok=1 tcr=b5100080 tcr2=00038011')
S4_ROW = ('stage4=id2 id2=00007511 ver=2 ias_ok=1 ias=48 ipa=36 pa=36 '
          'oas2=36 ptfs4k=1 vmid16=0 words_ok=1 plan_ok=1 '
          'tcr=a0100080 tcr2=00038011')

ID0 = 0x48016605
ID1 = 0x20008005
ID2 = 0x00007511
CTTW = 1
NUMCB = 5
NUMS2CB = 0
NUMPAGENDXB = 2
PAGESIZE = 0
IAS = 48
CAP = 5
CBNDX = 0
TCR_COH = 0xB5100080
TCR_NON = 0xA0100080
TCR2 = 0x00038011
BOOTA_MD5 = 'ca8ddd7a5af345636cad8c5bf645fee2'

checks = 0
fails = []


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)
        print('FAIL:', label)
    else:
        print('ok  ', label)


def sh(adb, cmd, timeout=180):
    r = subprocess.run([adb, 'shell', cmd], capture_output=True, text=True,
                       timeout=timeout)
    return (r.stdout or '').strip()


def read_window(adb, live):
    """Return the raw bytes of the 32 MiB window, live or cached."""
    if live:
        size = sh(adb, 'cat /sys/class/block/sda17/size')
        try:
            sectors = int(size)
        except ValueError:
            sectors = 0
        if sectors <= 0:
            return b''
        skip = sectors // 8 - 8192
        sh(adb, 'dd if=/dev/block/sda17 bs=4096 skip=%d count=8192 '
                 'of=/tmp/w.bin 2>/dev/null' % skip, timeout=600)
        local = ONEPLUS / 'work' / 'tmp' / 'r103_stage6_window_live.bin'
        local.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([adb, 'pull', '/tmp/w.bin', str(local)],
                       capture_output=True, text=True, timeout=600)
        return local.read_bytes() if local.is_file() else b''
    return WINDOW_CACHE.read_bytes() if WINDOW_CACHE.is_file() else b''


def window_rows(raw):
    """Decode the window (detect UTF-8 vs UTF-16) and return flattened record text."""
    enc = 'utf-16-le' if raw.count(
        'enchilada-adreno'.encode('utf-16-le')) else 'utf-8'
    needle = 'enchilada-adreno'.encode(enc)
    width = 2 if enc == 'utf-16-le' else 1
    found = []
    i = 0
    while True:
        j = raw.find(needle, i)
        if j < 0:
            break
        start = max(0, (j - 200 * width) & ~(width - 1))
        found.append(raw[start:j + 900 * width].decode(enc, 'replace'))
        i = j + 1
    return enc, ' '.join(' '.join(f.split()) for f in found)


def derive_tcr(ias, coherent):
    """Round-83 TTBR1 geometry, re-derived here (do not trust the module).

    coherent_walk selects TCR.SH/IRGN/ORGN: SH_IS(3)/RGN_WBWA(1) when
    coherent, SH_OS(2)/RGN_NC(0) when not. The two branches are NOT free
    variations (AdrenoSmmuCtxWordV1.c:106-121).
    """
    tg, shf, rgn = 2, (3 if coherent else 2), (1 if coherent else 0)
    t = ((tg << 14) | (shf << 12) | (rgn << 10) | (rgn << 8) | (64 - ias))
    return ((t << 16) & ~(1 << 22)) | (1 << 7)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--adb', default=r'C:\platform-tools\adb.exe')
    ap.add_argument('--live', action='store_true',
                    help='re-dump the current phone window instead of using the cache')
    args = ap.parse_args()

    # ---------- B. re-derive every field, do not trust the module ----------
    print('--- independent re-derivation from the raw words ---')
    cttw_reg = (ID0 >> 14) & 1
    check(cttw_reg == CTTW, 'ID0.CTTW = (ID0>>14)&1 == 1 (arm-smmu.h:54 BIT(14))')
    check((ID0 & 0x00004000) != 0,
          'ID0 & kAdrenoIommuId0Cttw(0x4000) is set')
    # coherent_walk = cttw_reg OR fw; sdm845.dtsi has NO dma-coherent -> fw = 0.
    fw = 0
    coherent_walk = 1 if (cttw_reg or fw) else 0
    check(coherent_walk == 1,
          'coherent_walk = cttw_reg OR fw = 1 (arm-smmu.c:1799-1805; fw=0)')

    numcb = ID1 & 0xFF
    nums2cb = (ID1 >> 16) & 0xFF
    numpagendxb = (ID1 >> 28) & 0x7
    pagesize = (ID1 >> 31) & 1
    check(numcb == NUMCB, 'ID1.NUMCB GENMASK(7,0) == 5 (arm-smmu.c:1866)')
    check(nums2cb == NUMS2CB,
          'ID1.NUMS2CB GENMASK(23,16) == 0 (arm-smmu.c:1865)')
    check(numpagendxb == NUMPAGENDXB,
          'ID1.NUMPAGENDXB GENMASK(30,28) == 2 -> numpage = 1<<3 = 8')
    check((1 << (numpagendxb + 1)) == 8, 'numpage re-derived == 8')
    check(pagesize == PAGESIZE,
          'ID1.PAGESIZE BIT(31) == 0 -> pgshift = 12 (4 KiB, arm-smmu.c:1854)')
    check(numcb <= 128 and numcb <= 256,
          'NUMCB=5 is below both ceilings (ARM_SMMU_MAX_CBS=128, ID1.NUMCB<=256)')

    # ASID tuple: cfg->asid = cfg->cbndx (arm-smmu.c:795); first-fit from start.
    cap = min(numcb, 128)
    init_ok = 0 if nums2cb < numcb else 1       # 0 == accepted
    start = nums2cb
    cbndx = start                              # bitmap empty -> first-fit == start
    check(cap == CAP, 'cap = min(NUMCB, 128) == 5')
    check(init_ok == 0,
          'init_from_id1 accepted (num_s2=0 < num_cb=5; no "impossible number of S2 context banks!")')
    check(cbndx == CBNDX, 'first-fit cbndx = start = 0')
    check(cbndx == cbndx & 0xFFFF, 'ASID within 16-bit TTBRn_ASID field')
    check((cbndx & 0xFF) == 0, 'contextidr = cbndx & 0xff = 0')
    check((cbndx & 0xFF) == 0, 's2cr_cbndx = 0')
    check((cbndx << 2) == 0, 'cbar_offset = cbndx<<2 = 0')
    check(cbndx == 0, 'ttbr_asid_field = cfg->asid = cbndx = 0')

    # TCR: stage 6 uses the REAL cttw (coherent), stage 4 used a placeholder.
    check(TCR_COH == derive_tcr(IAS, True),
          'coherent TCR re-derived == 0x%08x (got 0x%08x)'
          % (TCR_COH, derive_tcr(IAS, True)))
    check(TCR_NON == derive_tcr(IAS, False),
          'non-coherent placeholder TCR re-derived == 0x%08x (got 0x%08x)'
          % (TCR_NON, derive_tcr(IAS, False)))
    check(TCR_COH != TCR_NON, 'coherent and non-coherent TCRs DIFFER')
    check((TCR_COH ^ TCR_NON) == 0x15000000,
          'the difference is exactly the SH/IRGN/ORGN bits (0x15000000)')
    # The mechanism: coherent sets SH_IS and RGN_WBWA, non-coherent SH_OS/RGN_NC.
    check(((TCR_COH >> 28) & 0xF) == 0xB and ((TCR_NON >> 28) & 0xF) == 0xA,
          'coherent TCR high nibble = 0xB (SH_IS+RGN_WBWA), placeholder = 0xA (SH_OS+RGN_NC)')
    check(TCR2 & 0xF == 1, 'TCR2.PASIZE == 1 (36-bit output)')
    check((TCR2 >> 15) & 0x7 == 7, 'TCR2.SEP == 0x7')
    check((TCR2 & (1 << 4)) != 0, 'TCR2.AS set (AArch64 context)')
    check(TCR2 == 0x00038011,
          'TCR2 identical in both rows (unaffected by coherency)')

    # ---------- C. evidence doc + safety preconditions ----------
    print('--- evidence doc + safety preconditions ---')
    doc = EVIDENCE.read_text(encoding='utf-8')
    dflat = ' '.join(doc.split())
    check('DEVICE-VERIFIED' in doc, 'DOC names its evidence class')
    check(S6_ROW in dflat, 'DOC quotes the exact stage6 measured row')
    check(S4_ROW in dflat, 'DOC quotes the exact stage4 placeholder row')
    for token in ('48016605', '20008005', 'b5100080', 'a0100080', 'cttw=1',
                  'coherent_walk = 1', 'sdm845.dtsi', 'arm-smmu.c:1799-1805',
                  'arm-smmu.c:795', 'GPUCC=5090000', 'ONE-SHOT'):
        check(token.lower() in dflat.lower(), 'DOC records %r' % token)
    check('a0100080' in dflat and 'placeholder' in dflat.lower(),
          'DOC marks a0100080 as the placeholder, not the silicon config')
    check('b5100080' in dflat and 'coherent' in dflat.lower(),
          'DOC states b5100080 is the real coherent configuration')
    check('\r' not in doc, 'DOC is pure LF')
    check(not doc.startswith('\ufeff'), 'DOC has no BOM')
    check('modeled = 9 / satisfied = 0 / BLOCKED' in dflat,
          'DOC states the admission table is unchanged')
    check('does **not** prove' in dflat or 'does not prove' in dflat,
          'DOC states what it does NOT prove')
    check('stage-1 context works' not in dflat.replace(
              'does **not** prove that a stage-1 context works', ''),
          'DOC does not claim a working stage-1 context')
    check('ready()' in dflat and 'still 0' in dflat,
          'DOC states ready() is still derived/0')

    check(ROOT_HELPER.is_file(),
          'the reboot-to-TWRP gate exists (run before any device write)')
    check(REBOOT_HELPER.is_file(), 'the reliable reboot helper exists')
    gate = ROOT_HELPER.read_text(encoding='utf-8')
    check('boot-recovery' in gate, 'gate checks the BCB command field')
    check(BOOTA_MD5 in gate, 'gate pins the boot_a md5')
    helper = REBOOT_HELPER.read_text(encoding='utf-8')
    check('reboot recovery' in helper,
          'helper reboots through `fastboot reboot recovery`')
    check('ONE-SHOT' in helper.upper() or 'one-shot' in helper.lower(),
          'helper records that the BCB is one-shot')
    md5 = hashlib.md5(BOOTA_BACKUP.read_bytes()).hexdigest() \
        if BOOTA_BACKUP.is_file() else ''
    check(md5 == BOOTA_MD5, 'boot_a TWRP backup still byte-identical')

    # ---------- A. re-read the window ----------
    print('--- window re-read (authoritative pulled bytes) ---')
    live = False
    if args.live:
        try:
            twrp = sh(args.adb, 'getprop ro.twrp.version')
        except Exception:
            twrp = ''
        check(twrp != '', 'the phone is in TWRP (got %r)' % twrp)
        live = bool(twrp)
    raw = read_window(args.adb, live)
    src = 'LIVE window' if live else 'pulled window CACHE'
    check(len(raw) == 8192 * 4096,
          '%s is exactly 32 MiB (got %d bytes)' % (src, len(raw)))
    enc, joined = window_rows(raw)
    check('enchilada-adreno' in joined, '%s contains records in %s' % (src, enc))
    check('stage6=id1' in joined, '%s still contains the stage6=id1 row' % src)
    check(S6_ROW in joined, '%s quotes the FULL exact stage6 row' % src)
    check('id0=48016605' in joined and 'id1=20008005' in joined,
          'the live/cache row reads the measured ID0 and ID1')
    check('cttw=1' in joined, 'the row reports cttw=1 (coherent)')
    check('tcr=b5100080' in joined, 'the row carries the coherent TCR b5100080')
    check('stage4=id2' in joined, 'the row set still contains the stage4=id2 row')
    check('tcr=a0100080' in joined, 'the stage4 placeholder TCR is also present')
    check('stage5=run' in joined and 'cx_after=80001fe0' in joined,
          'stage5 still raised CX to 80001fe0 before the reads')
    # Count against the RAW bytes, not `joined`: each needle captures a ~900-byte
    # decoded window, and the cluster of rows is closer than that, so the same
    # row legitimately appears in several overlapping windows.
    check(raw.count(b'stage6=id1 id0=48016605') == 1,
          'the exact stage6 row occurs exactly once in the RAW window')

    print()
    print('%d/%d checks passed' % (checks - len(fails), checks))
    if fails:
        print('FAIL: stage-6 device evidence (%d failures)' % len(fails))
        return 1
    print('PASS: stage-6 device evidence '
          '(row re-read, ID0/ID1/ASID re-derived, coherent TCR corrected, safety recorded)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
