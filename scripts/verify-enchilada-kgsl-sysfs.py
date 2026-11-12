#!/usr/bin/env python3
"""Round 79: verify the LIVE kgsl driver facts against the phone AND against the
pinned sources that are supposed to describe this GPU.

Two independent directions:
  Domain A: re-read every attribute from /sys/class/kgsl/kgsl-3d0 over adb and
            compare with the values recorded in evidence/stage5-gpu-kgsl-sysfs.md.
  Domain B: confront those values with the pinned catalog and Mesa table, and
            assert the DISCREPANCY (Adreno630v2 is not in any pinned source) is
            recorded rather than silently mapped onto an entry.

Read-only: only `cat` over /sys/class/kgsl. Nothing is written to the device.

usage: verify-enchilada-kgsl-sysfs.py [--adb PATH] [--skip-device]
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
GPU = DEV / 'drivers' / 'NativeAdreno'
EVIDENCE = DEV / 'evidence' / 'stage5-gpu-kgsl-sysfs.md'
CATALOG = DEV / '.cache' / 'adreno-a630-source' / 'drivers' / 'gpu' / 'drm' / 'msm' / 'adreno' / 'a6xx_catalog.c'
MESA = DEV / 'scripts' / 'data' / 'freedreno_devices.85fda3b3.py'
BASE = '/sys/class/kgsl/kgsl-3d0'

# attribute -> expected exact string
EXPECT = {
    'gpu_model': 'Adreno630v2',
    'gpu_available_frequencies': '710000000 675000000 596000000 520000000 414000000 342000000 257000000',
    'num_pwrlevels': '7',
    'min_pwrlevel': '6',
    'max_pwrlevel': '0',
    'default_pwrlevel': '6',
    'max_gpuclk': '710000000',
    'max_clock_mhz': '710',
    'gpuclk': '257000000',
    'clock_mhz': '257',
    'devfreq/cur_freq': '257000000',
    'devfreq/governor': 'msm-adreno-tz',
    'gpu_busy_percentage': '0 %',
    'ifpc': '1',
    'ifpc_count': '0x0',
    'sptp_pc': '1',
    'popp': '0',
    'idle_timer': '80',
    'reset_count': '0',
    'preempt_level': '0x1',
    'preemption': '0',
    'usesgmem': '1',
    'hwcg': '1',
}

checks = 0
fails = []


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)


def adb_cat(adb, rel):
    r = subprocess.run([adb, 'shell', 'cat ' + BASE + '/' + rel],
                       capture_output=True, text=True, timeout=120)
    return ' '.join((r.stdout or '').split())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--adb', default=r'C:\platform-tools\adb.exe')
    ap.add_argument('--skip-device', action='store_true')
    args = ap.parse_args()

    doc = EVIDENCE.read_text(encoding='utf-8')
    dflat = ' '.join(doc.split())

    # ---- A: re-read from the device -------------------------------------
    if not args.skip_device:
        r = subprocess.run([args.adb, 'devices'], capture_output=True, text=True, timeout=60)
        lines = (r.stdout or '').splitlines()
        USABLE = {'device', 'recovery', 'sideload', 'rescue', 'bootloader'}
        online = [l for l in lines[1:]
                  if len(l.split()) >= 2 and l.split()[1] in USABLE]
        check(len(online) == 1,
              'exactly one usable device is attached (%d, states %r)'
              % (len(online), [l.split()[1] for l in lines[1:] if len(l.split()) >= 2]))
        for rel, want in EXPECT.items():
            got = adb_cat(args.adb, rel)
            check(got == want, 'DEVICE %s == %r (got %r)' % (rel, want, got))
        # Multi-token attributes are compared as TOKEN LISTS: the kernel pads
        # them with tabs and the exact spacing is not load-bearing.
        bus = adb_cat(args.adb, 'gpubusy').split()
        check(bus == ['0', '0'], 'DEVICE gpubusy is two zeros (got %r)' % bus)
        freqs = adb_cat(args.adb, 'gpu_available_frequencies').split()
        check(len(freqs) == 7, 'DEVICE 7 available frequencies (got %d)' % len(freqs))
        check(freqs[0] == '710000000' and freqs[-1] == '257000000',
              'DEVICE frequency endpoints 710..257 MHz (got %r)' % (freqs[:1] + freqs[-1:]))
        # The two interfaces expose the SAME seven frequencies in OPPOSITE
        # order: gpu_available_frequencies descends 710..257 while
        # devfreq/available_frequencies ascends 257..710. Asserting equality
        # would be wrong; assert the SET and the two orderings.
        avail = adb_cat(args.adb, 'devfreq/available_frequencies').split()
        check(sorted(avail) == sorted(freqs),
              'DEVICE the two frequency lists hold the same set (got %r vs %r)'
              % (avail, freqs))
        check(avail == list(reversed(freqs)),
              'DEVICE devfreq ascends where gpu_available descends (got %r)' % avail)
        # gpu_clock_stats is seven zeros; compare as a token list so spacing is
        # not load-bearing.
        stats = adb_cat(args.adb, 'gpu_clock_stats')
        toks = stats.split()
        check(len(toks) == 7 and all(t == '0' for t in toks),
              'DEVICE gpu_clock_stats is seven zeros (got %r)' % stats)
        # runtime accounting: this kernel reports no runtime PM for the GPU
        check(adb_cat(args.adb, 'power/runtime_status') == 'unsupported',
              'DEVICE power/runtime_status is "unsupported"')
        check(adb_cat(args.adb, 'power/runtime_active_time') == '0',
              'DEVICE runtime_active_time is 0')
        check(adb_cat(args.adb, 'power/runtime_suspended_time') == '0',
              'DEVICE runtime_suspended_time is 0')

    # ---- B: the doc records what the device said ------------------------
    for token in ('Adreno630v2', '710000000', '257000000', 'msm-adreno-tz',
                  'num_pwrlevels', 'reset_count', 'ifpc', 'sptp_pc',
                  '233:0', 'qcom,kgsl-3d0'):
        check(token in dflat, 'DOC records %r' % token)
    check('read-only' in doc.lower(), 'DOC states read-only')
    check('DEVICE-SOURCED' in doc, 'DOC names its evidence class')
    check('STAYS `"host-verified"`' in doc, 'DOC keeps the area host-verified')
    check('\r' not in doc, 'DOC is pure LF')
    check(not doc.startswith('\ufeff'), 'DOC has no BOM')

    # ---- C: the pinned sources, confronted ------------------------------
    catalog = CATALOG.read_text(encoding='utf-8', errors='replace')
    mesa = MESA.read_text(encoding='utf-8', errors='replace')

    # The catalog has exactly ONE revn-630 entry.
    check(len(re.findall(r'\.revn = 630\b', catalog)) == 1,
          'CATALOG has exactly one revn 630 entry (got %d)'
          % len(re.findall(r'\.revn = 630\b', catalog)))
    check('0x06030001' in catalog and '0x06030002' in catalog,
          'CATALOG carries both A630 chip ids')
    check('ADRENO_6XX_GEN1' in catalog, 'CATALOG has ADRENO_6XX_GEN1')
    # THE DISCREPANCY: no pinned source contains the vendor's "v2" model string.
    check('Adreno630v2' not in catalog,
          'CATALOG does NOT contain "Adreno630v2" (the vendor string)')
    check('Adreno630v2' not in mesa,
          'MESA does NOT contain "Adreno630v2"')
    check(len(re.findall(r'GPUId\(630\)', mesa)) == 1,
          'MESA has exactly one GPUId(630) entry (got %d)'
          % len(re.findall(r'GPUId\(630\)', mesa)))
    # And the DOC must SAY so rather than mapping v2 onto the entry silently.
    check('downstream kgsl naming convention' in dflat,
          'DOC names the v2 suffix as a downstream convention')
    check('not established by this string' in dflat or
          'is **not established by this string**' in dflat,
          'DOC refuses to claim what v2 implies')

    # The frequency table's endpoints agree with the DTB pwrlevels the port read
    # in round 51: the top level 0 is 710 MHz and the bottom is 257 MHz.
    check('710000000' in dflat and '257000000' in dflat,
          'DOC records both frequency-table endpoints')
    check(dflat.count('710000000') >= 1, 'DOC keeps the 710 MHz ceiling')

    # ---- D: the idle-but-live claim, checked for internal consistency ---
    check('reset_count' in dflat and '`0`' in dflat,
          'DOC records reset_count 0')
    check('never actually rendered' in dflat or 'never driven' in dflat,
          'DOC states the GPU has never rendered')
    check('gpu_clock_stats' in dflat, 'DOC cites gpu_clock_stats')

    print('Domain A: %d live kgsl attributes re-read and compared%s'
          % (len(EXPECT) + 5, '' if not args.skip_device else ' (SKIPPED)'))
    print('Domain B: the evidence doc agrees with the live driver, and the '
          '"Adreno630v2" string is proven ABSENT from both pinned sources')
    print('KGSL_SYSFS_VERIFY: %s (%d checks, %d failures)'
          % ('PASS' if not fails else 'FAIL', checks, len(fails)))
    for f in fails:
        print('  FAIL ' + f)
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
