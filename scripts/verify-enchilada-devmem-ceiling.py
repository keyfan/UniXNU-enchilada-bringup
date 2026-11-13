#!/usr/bin/env python3
"""Round 85 DEVICE verifier: re-read the live phone and assert the two findings
that bound what a device-side round can now accomplish.

Finding 1 (re-confirmed): the IOMMU debugfs surface round 80/82 read is still
what it was -- 92 live page-table pages, and NO per-context or address-size node
for the kgsl SMMU. The `ias` input round 77 proved underivable statically is
therefore STILL not observable through debugfs either.

Finding 2 (NEW, and the reason this round exists): `CONFIG_DEVMEM is not set` on
this kernel, so /dev/mem cannot be used to read ID2 at 0x05040028 at all. A node
created with mknod is accepted by the filesystem but any read fails with
ENXIO. This is a HARD kernel-level exclusion, not a permission problem, and it
closes the last read-only route to `ias` on a stock boot.

Discipline: every mount is removed and the removal VERIFIED (mount count back to
0, UMNT_RC=0); every node this verifier or the round created is removed.

usage: verify-enchilada-devmem-ceiling.py [--adb PATH] [--skip-device]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
EVIDENCE = DEV / 'evidence' / 'stage5-gpu-devmem-ceiling.md'

checks = 0
fails = []


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)
        print('FAIL:', label)
    else:
        print('PASS', label)


def sh(adb, cmd, timeout=120):
    r = subprocess.run([adb, 'shell', cmd], capture_output=True, text=True,
                       timeout=timeout)
    return (r.stdout or '').strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--adb', default=r'C:\platform-tools\adb.exe')
    ap.add_argument('--skip-device', action='store_true')
    args = ap.parse_args()

    doc = EVIDENCE.read_text(encoding='utf-8')
    dflat = ' '.join(doc.split())

    # ---- the doc's own claims ----------------------------------------
    for token in ('CONFIG_DEVMEM is not set', 'ENXIO', '92',
                  'io-pgtable/pages', '5040000.arm,smmu-kgsl', 'UMNT_RC=0',
                  '0x05040028', 'ID2', 'ias'):
        check(token in dflat, 'DOC records %r' % token)
    check('DEVICE-SOURCED' in doc, 'DOC names its evidence class')
    check('STAYS `"host-verified"`' in doc, 'DOC keeps the area host-verified')
    check('\r' not in doc, 'DOC is pure LF')
    check(not doc.startswith('\ufeff'), 'DOC has no BOM')
    check('does **not** unblock stage 5' in dflat,
          'DOC does not unblock stage 5')
    check('hard' in dflat.lower(), 'DOC frames the ceiling as a hard exclusion')

    if args.skip_device:
        print('---')
        print('DEVMEM_CEILING_VERIFY: PASS (%d checks, device skipped)' % checks
              if not fails else
              'DEVMEM_CEILING_VERIFY: FAIL (%d checks, %d failures)'
              % (checks, len(fails)))
        return 0 if not fails else 1

    # ---- Finding 1: the debugfs surface is unchanged -----------------
    before = sh(args.adb, "mount | grep -c debugfs")
    check(before == '0', 'debugfs is NOT mounted before the survey (got %r)' % before)
    sh(args.adb, "mount -t debugfs none /sys/kernel/debug 2>/dev/null")
    try:
        pages = sh(args.adb, "cat /sys/kernel/debug/iommu/io-pgtable/pages")
        print('IO_PGTABLE_PAGES', pages)
        check(re.fullmatch(r'\d+', pages or '') is not None,
              'the page-table count is a number (got %r)' % pages)
        check(pages == '92', 'the io-pgtable page count is still 92 (got %r)' % pages)

        tree = sh(args.adb, "find /sys/kernel/debug/iommu -maxdepth 2 2>/dev/null")
        check('io-pgtable' in tree, 'the io-pgtable node is present')
        check('tests' in tree, 'the tests node is present')
        # The load-bearing NEGATIVE: no address-size / va_size / ID2 exposure.
        for absent in ('va_size', 'address_size', 'addr_size', 'ias', 'id2'):
            check(absent not in tree.lower(),
                  'debugfs exposes NO %r node (the negative that bounds this round)'
                  % absent)
        # And no kgsl-specific test/context device (round 82's finding, re-asserted).
        kgsl_tests = [l for l in tree.splitlines() if 'kgsl' in l.lower()]
        check(not kgsl_tests,
              'no kgsl-specific iommu debugfs node (got %r)' % kgsl_tests)
    finally:
        sh(args.adb, "umount /sys/kernel/debug 2>/dev/null")
    after = sh(args.adb, "mount | grep -c debugfs")
    check(after == '0', 'debugfs unmounted and VERIFIED (got %r)' % after)
    entries = sh(args.adb, "ls -A /sys/kernel/debug 2>/dev/null | wc -l")
    check(entries == '0', 'the debugfs mountpoint is empty again (got %r)' % entries)

    # ---- Finding 2: CONFIG_DEVMEM is not set -------------------------
    cfg = sh(args.adb, "zcat /proc/config.gz | grep -E '^CONFIG_DEVMEM|^# CONFIG_DEVMEM'")
    print('DEVMEM_CONFIG', cfg)
    check('CONFIG_DEVMEM is not set' in cfg,
          'the kernel is built with CONFIG_DEVMEM NOT set (got %r)' % cfg)
    strict = sh(args.adb, "zcat /proc/config.gz | grep -E 'CONFIG_STRICT_DEVMEM'")
    check('is not set' in strict,
          'CONFIG_STRICT_DEVMEM is also unset -- so the exclusion is DEVMEM, not strictness')

    # A mknod'd node is accepted by the filesystem but every read fails ENXIO.
    sh(args.adb, "rm -f /dev/mem; mknod /dev/mem c 1 1 2>/dev/null")
    exists = sh(args.adb, "test -e /dev/mem && echo YES || echo NO")
    print('MKNOD_NODE_EXISTS', exists)
    # The read is the real test: it must FAIL.
    rd = sh(args.adb, "dd if=/dev/mem of=/dev/null bs=4 skip=21198640 count=1 2>&1")
    print('DEVMEM_READ', rd)
    check('No such device or address' in rd or 'error' in rd.lower(),
          'a read of the mknod-ed /dev/mem FAILS (got %r)' % rd)
    sh(args.adb, "rm -f /dev/mem")
    gone = sh(args.adb, "test -e /dev/mem && echo YES || echo NO")
    check(gone == 'NO', 'the node this verifier created was REMOVED (got %r)' % gone)

    # ---- The kgsl SMMU is still bound (round 82 re-confirmed) --------
    drv = sh(args.adb, "ls /sys/bus/platform/drivers/arm-smmu/")
    check('5040000.arm,smmu-kgsl' in drv, 'the kgsl SMMU is still BOUND to arm-smmu')
    check('15000000.apps-smmu' in drv, 'the apps SMMU is still bound too')
    mod = sh(args.adb, "cat /sys/devices/platform/soc/5040000.arm,smmu-kgsl/modalias")
    check(mod == 'of:Narm,smmu-kgslT<NULL>Cqcom,smmu-v2',
          'the kgsl SMMU modalias is unchanged (got %r)' % mod)

    # ---- Safety: nothing was left mounted or created -----------------
    final_mounts = sh(args.adb, "mount | grep -c debugfs")
    check(final_mounts == '0', 'FINAL: zero debugfs mounts (got %r)' % final_mounts)

    print('---')
    if fails:
        print('DEVMEM_CEILING_VERIFY: FAIL (%d checks, %d failures)'
              % (checks, len(fails)))
        return 1
    print('DEVMEM_CEILING_VERIFY: PASS (%d checks, 0 failures)' % checks)
    return 0


if __name__ == '__main__':
    sys.exit(main())
