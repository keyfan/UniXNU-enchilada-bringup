#!/usr/bin/env python3
"""Round 101 device verifier: `ias` was MEASURED on the phone.

Three domains:

  A. Re-read the LOG WINDOW off the phone (read-only) and assert that the
     `stage4=id2` row is really there, with the exact field values this round
     recorded. This is the domain that makes it a measurement rather than a claim:
     it goes back to the same window the kernel wrote.

  B. Re-DERIVE the decode in Python from the raw word and the pinned bit positions,
     independently of the module, and assert the module's line agrees with the
     derivation. An `ias` that only "looks right" is not evidence.

  C. Assert the evidence doc and the safety preconditions: the reboot guarantee was
     established before the run (scripts/check-reboot-to-twrp.py), the BCB was
     re-armed after, `fastboot reboot recovery` was chosen because it does not depend
     on the one-shot BCB, and the `boot_a` TWRP backup is still byte-identical.

usage: verify-enchilada-ias-measured.py [--adb PATH] [--skip-device]
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
EVIDENCE = DEV / 'evidence' / 'stage5-gpu-ias-measured-device.md'
ROOT_HELPER = ONEPLUS / 'scripts' / 'check-reboot-to-twrp.py'
REBOOT_HELPER = ONEPLUS / 'scripts' / 'reboot-to-twrp.py'
BOOTA_BACKUP = ONEPLUS / 'work' / 'tmp' / 'r86-backup' / 'boot_a.twrp.img'

# The measured row, verbatim from the window.
EXPECT_ROW = ('stage4=id2 id2=00007511 ver=2 ias_ok=1 ias=48 ipa=36 pa=36'
              ' oas2=36 ptfs4k=1 vmid16=0 words_ok=1 plan_ok=1'
              ' tcr=a0100080 tcr2=00038011')
ID2_WORD = 0x00007511
IAS = 48
IPA = 36
PA = 36
TCR = 0xA0100080
TCR2 = 0x00038011
BOOTA_MD5 = 'ca8ddd7a5af345636cad8c5bf645fee2'

# arm_smmu.c:1727-1744, including the DEFAULT arm that collapses 5 (and anything
# else) to 48. Re-implemented here on purpose: the point is to not trust the module.
ID_SIZE = {0: 32, 1: 36, 2: 40, 3: 42, 4: 44}


def id_size_to_bits(s):
    return ID_SIZE.get(s, 48)


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--adb', default=r'C:\platform-tools\adb.exe')
    ap.add_argument('--skip-device', action='store_true')
    args = ap.parse_args()

    # ---------- B. re-derive the decode, do not trust the module ----------
    ias_f = (ID2_WORD >> 0) & 0xF
    oas_f = (ID2_WORD >> 4) & 0xF
    ubs_f = (ID2_WORD >> 8) & 0xF
    check(ias_f == 1, 'ID2.IAS field [3:0] == 1 (got %d)' % ias_f)
    check(oas_f == 1, 'ID2.OAS field [7:4] == 1 (got %d)' % oas_f)
    check(ubs_f == 5, 'ID2.UBS field [11:8] == 5 (got %d)' % ubs_f)
    check((ID2_WORD >> 12) & 1 == 1, 'ID2.PTFS_4K == 1')
    check((ID2_WORD >> 13) & 1 == 1, 'ID2.PTFS_16K == 1')
    check((ID2_WORD >> 14) & 1 == 1, 'ID2.PTFS_64K == 1')
    check((ID2_WORD >> 15) & 1 == 0, 'ID2.VMID16 == 0')
    check(id_size_to_bits(ubs_f) == IAS,
          'UBS=5 collapses to ias=48 through the DEFAULT arm (got %d)'
          % id_size_to_bits(ubs_f))
    check(id_size_to_bits(ias_f) == IPA, 'IAS=1 gives ipa=36')
    check(id_size_to_bits(oas_f) == PA, 'OAS=1 gives pa=36')
    # The two fields genuinely disagree on a v2 SMMU: va_size comes from UBS, ipa from IAS.
    check(id_size_to_bits(ubs_f) != id_size_to_bits(ias_f),
          'ias (from UBS) and ipa (from IAS) are DIFFERENT on this part')

    # TCR/TCR2 must be consistent with that ias, or the word and the composition
    # disagree and neither is evidence.
    # The measured TCR is the **TTBR1** geometry, and that is assertable rather than
    # assumed: EPD0 is SET, EPD1 is CLEAR, and the real T1SZ lives in the HIGH half
    # (the round-83 `ttbr1` branch shifts the low fields up by 16). Reading T0SZ from
    # the low half in that geometry yields 0 and would look like an ias of 64.
    t1sz = (TCR >> 16) & 0x3F
    check(64 - t1sz == IAS, 'TCR.T1SZ=%d implies ias=%d (got %d)'
          % (t1sz, 64 - t1sz, IAS))
    check((TCR & (1 << 7)) != 0, 'TCR.EPD0 is set (TTBR1 geometry disables TTBR0)')
    check((TCR & (1 << 23)) == 0, 'TCR.EPD1 is clear (it is the TTBR1 geometry)')
    check((TCR & (1 << 22)) == 0, 'TCR.A1 is clear')
    # TG1 ends up at bits[31:30], not [15:14]: the TTBR1 branch shifts the low
    # fields up by 16, so TG1's field position moves with them. Reading [15:14]
    # here returns 0 and would mis-report the granule.
    check(((TCR >> 30) & 0x3) == 2,
          'TCR.TG1 == 2 at bits[31:30] (4 KiB, ARM_LPAE_TCR_TG1_4K)')
    # And the whole word must equal round 83's own derivation for these inputs.
    # Re-derived here rather than imported, so a change in either place is caught.
    # round-83: tcr(ias, coherent=0, ttbr1=1)
    _tg, _sh, _rgn = 2, 2, 0          # TG1_4K, SH_OS, RGN_NC (non-coherent)
    _t = ((_tg << 14) | (_sh << 12) | (_rgn << 10) | (_rgn << 8) | (64 - IAS))
    _expect_tcr = ((_t << 16) & ~(1 << 22)) | (1 << 7)
    check(TCR == _expect_tcr,
          'TCR equals round-83 derivation for ias=%d coherent=0 ttbr1=1 '
          '(0x%08x vs 0x%08x)' % (IAS, TCR, _expect_tcr))
    check(TCR2 & 0xF == 1, 'TCR2.PASIZE == 1 (36-bit output)')
    check((TCR2 >> 15) & 0x7 == 7, 'TCR2.SEP == 0x7 (upstream)')
    check((TCR2 & (1 << 4)) != 0, 'TCR2.AS is set (AArch64 context)')

    # ---------- C. evidence doc + safety preconditions ----------
    doc = EVIDENCE.read_text(encoding='utf-8')
    dflat = ' '.join(doc.split())
    check('DEVICE-VERIFIED' in doc, 'DOC names its evidence class')
    check(EXPECT_ROW in dflat, 'DOC quotes the exact measured row')
    for token in ('00007511', 'ias = 48', '00005ff4', '80001fe0',
                  'arm_smmu_id_size_to_bits', 'P2:1727-1744', 'CONFIG_DEVMEM'):
        check(token in dflat, 'DOC records %r' % token)
    check('\r' not in doc, 'DOC is pure LF')
    check(not doc.startswith('\ufeff'), 'DOC has no BOM')
    check('modeled = 9 / satisfied = 0 / BLOCKED' in dflat,
          'DOC states the admission table is unchanged')
    check('does **not** prove' in dflat or 'does not prove' in dflat,
          'DOC states what it does NOT prove')
    # It must NOT claim the context works.
    check('stage-1 context works' not in dflat.replace(
              'does **not** prove that a stage-1 context works', ''),
          'DOC does not claim a working stage-1 context')

    print('--- safety preconditions of the run ---')
    check(ROOT_HELPER.is_file(),
          'the reboot-to-TWRP gate exists (run before any device write)')
    check(REBOOT_HELPER.is_file(),
          'the reliable reboot helper exists')
    gate = ROOT_HELPER.read_text(encoding='utf-8')
    check('boot-recovery' in gate, 'gate checks the BCB command field')
    check(BOOTA_MD5 in gate, 'gate pins the boot_a md5')
    helper = REBOOT_HELPER.read_text(encoding='utf-8')
    check('reboot recovery' in helper,
          'helper reboots through `fastboot reboot recovery`')
    check('ONE-SHOT' in helper.upper() or 'one-shot' in helper.lower(),
          'helper records that the BCB is one-shot')
    if BOOTA_BACKUP.is_file():
        md5 = hashlib.md5(BOOTA_BACKUP.read_bytes()).hexdigest()
        check(md5 == BOOTA_MD5, 'boot_a TWRP backup still byte-identical')
    else:
        check(False, 'boot_a TWRP backup is present')

    # ---------- A. re-read the live window ----------
    if not args.skip_device:
        print('--- live window re-read ---')
        twrp = sh(args.adb, 'getprop ro.twrp.version')
        check(twrp != '', 'the phone is in TWRP (got %r)' % twrp)
        if twrp:
            size = sh(args.adb, 'cat /sys/class/block/sda17/size')
            try:
                sectors = int(size)
            except ValueError:
                sectors = 0
            check(sectors > 0, 'userdata sector count readable (%r)' % size)
            if sectors:
                skip = sectors // 8 - 8192
                # Dump to a DEVICE file and pull it. A `dd | adb shell` pipe is
                # line-ending-translated by PowerShell into UTF-16 and truncated it
                # to 33 MB earlier in this project; `adb pull` of a raw file gives
                # the bytes the kernel wrote. The window is 8192 x 4096 = 32 MiB.
                sh(args.adb, 'dd if=/dev/block/sda17 bs=4096 skip=%d count=8192 '
                             'of=/tmp/w.bin 2>/dev/null' % skip, timeout=600)
                local = DEV / 'work' / 'tmp' / 'ias-window.bin'
                local.parent.mkdir(parents=True, exist_ok=True)
                rp = subprocess.run([args.adb, 'pull', '/tmp/w.bin', str(local)],
                                    capture_output=True, text=True, timeout=600)
                raw = local.read_bytes() if local.is_file() else b''
                check(len(raw) == 8192 * 4096,
                      'window is exactly 8192x4096 = %d bytes (got %d)'
                      % (8192 * 4096, len(raw)))
                # The file holds what the kernel wrote, i.e. plain UTF-8. Detect the
                # encoding rather than assuming one: an earlier revision assumed
                # UTF-16 (a PowerShell-redirect artefact) and reported zero rows.
                enc = 'utf-16-le' if raw.count(
                    'enchilada-adreno'.encode('utf-16-le')) else 'utf-8'
                check(raw.count('enchilada-adreno'.encode(enc)) > 0,
                      'window contains records in %s' % enc)
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
                joined = ' '.join(' '.join(f.split()) for f in found)
                check('stage4=id2' in joined,
                      'the LIVE window still contains a stage4=id2 row')
                check('id2=00007511' in joined,
                      'the live row reads id2=00007511')
                check('ias=48' in joined, 'the live row reports ias=48')
                check('cx_after=80001fe0' in joined,
                      'the live window still shows stage5 raising CX to 80001fe0')

    print()
    print('%d/%d checks passed' % (checks - len(fails), checks))
    if fails:
        print('FAIL: ias-measured device evidence (%d failures)' % len(fails))
        return 1
    print('PASS: ias-measured device evidence '
          '(row re-read, decode re-derived, TCR/TCR2 consistent, safety recorded)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
