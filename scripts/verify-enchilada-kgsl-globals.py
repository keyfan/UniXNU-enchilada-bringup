#!/usr/bin/env python3
"""Round 80: verify the kgsl globals table captured under a briefly-mounted
debugfs, and prove the all-zero-IOVA claim is exact.

Two independent directions:
  Domain A: re-read /sys/kernel/debug/kgsl/globals over adb (mounting and then
            UNMOUNTING debugfs, verifying the mount state is restored) and compare
            the full table with the values recorded in the evidence doc.
  Domain B: assert the STRUCTURE of the table -- 17 entries, four complete ring
            sets, real sizes, and EVERY iova exactly zero -- so a future round
            that sees a non-zero iova fails loudly rather than silently passing.

The device interaction mounts debugfs and always unmounts it in a finally block,
then asserts the mount count returned to its pre-round value.

usage: verify-enchilada-kgsl-globals.py [--adb PATH] [--skip-device]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
EVIDENCE = DEV / 'evidence' / 'stage5-gpu-kgsl-globals.md'

# The table exactly as observed: (start, end, size, name)
EXPECT = [
    ('0x0000000000000000', '0x0000000000000000', '4096', 'setstate'),
    ('0x0000000000000000', '0x0000000000000000', '262144', 'gpu-qdss'),
    ('0x0000000000000000', '0x0000000000000000', '32768', 'memstore'),
    ('0x0000000000000000', '0x0000000000000000', '4096', 'scratch'),
    ('0x0000000000000000', '0x0000000000000000', '4096', 'pagetable_desc'),
    ('0x0000000000000000', '0x0000000000000000', '4096', 'profile_desc'),
    ('0x0000000000000000', '0x0000000000000000', '32768', 'ringbuffer'),
    ('0x0000000000000000', '0x0000000000000000', '4096', 'pagetable_desc'),
    ('0x0000000000000000', '0x0000000000000000', '4096', 'profile_desc'),
    ('0x0000000000000000', '0x0000000000000000', '32768', 'ringbuffer'),
    ('0x0000000000000000', '0x0000000000000000', '4096', 'pagetable_desc'),
    ('0x0000000000000000', '0x0000000000000000', '4096', 'profile_desc'),
    ('0x0000000000000000', '0x0000000000000000', '32768', 'ringbuffer'),
    ('0x0000000000000000', '0x0000000000000000', '4096', 'pagetable_desc'),
    ('0x0000000000000000', '0x0000000000000000', '4096', 'profile_desc'),
    ('0x0000000000000000', '0x0000000000000000', '32768', 'ringbuffer'),
    ('0x0000000000000000', '0x0000000000000000', '196608', 'profile'),
]

LINE = re.compile(r'^(0x[0-9a-f]{16})-(0x[0-9a-f]{16})\s+(\d+)\s+(\S+)$')

checks = 0
fails = []


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)


def sh(adb, cmd):
    r = subprocess.run([adb, 'shell', cmd], capture_output=True, text=True, timeout=120)
    return (r.stdout or '').strip()


def parse_table(text):
    rows = []
    for raw in text.splitlines():
        m = LINE.match(raw.strip())
        if m:
            rows.append(m.groups())
    return rows


def analyse(rows, tag):
    """Structure assertions that hold for the recorded table AND any re-read."""
    check(len(rows) == 17, f'{tag} table has 17 entries (got {len(rows)})')
    names = [r[3] for r in rows]
    for want in ('setstate', 'gpu-qdss', 'memstore', 'scratch', 'profile'):
        check(want in names, f'{tag} table contains {want}')
    rings = names.count('ringbuffer')
    pt = names.count('pagetable_desc')
    pf = names.count('profile_desc')
    check(rings == 4, f'{tag} four ringbuffers (got {rings})')
    check(pt == 4, f'{tag} four pagetable_desc (got {pt})')
    check(pf == 4, f'{tag} four profile_desc (got {pf})')
    check(rings == pt == pf, f'{tag} the four ring sets are complete')
    # Sizes are real and non-zero.
    check(all(int(r[2]) > 0 for r in rows), f'{tag} every size is non-zero')
    check(int(dict((r[3], r[2]) for r in rows)['ringbuffer']) == 32768,
          f'{tag} ringbuffer is 32768 B')
    check(int(dict((r[3], r[2]) for r in rows)['gpu-qdss']) == 262144,
          f'{tag} gpu-qdss is 262144 B')
    check(int(dict((r[3], r[2]) for r in rows)['profile']) == 196608,
          f'{tag} profile is 196608 B')
    # THE LOAD-BEARING ASSERTION: every iova is exactly zero, start AND end.
    nonzero = [r for r in rows
               if r[0] != '0x0000000000000000' or r[1] != '0x0000000000000000']
    check(nonzero == [],
          f'{tag} EVERY iova is exactly 0x0-0x0 (offenders: {nonzero})')
    # And start == end, i.e. a degenerate range -- not a real allocation.
    check(all(r[0] == r[1] for r in rows),
          f'{tag} every range is degenerate (start == end)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--adb', default=r'C:\platform-tools\adb.exe')
    ap.add_argument('--skip-device', action='store_true')
    args = ap.parse_args()

    doc = EVIDENCE.read_text(encoding='utf-8')
    dflat = ' '.join(doc.split())

    # ---- the DOC's own recorded table --------------------------------
    rows_recorded = parse_table(doc)
    analyse(rows_recorded, 'DOC')
    check(rows_recorded == [tuple(r) for r in EXPECT],
          'DOC table matches the expected 17 rows exactly')

    # ---- the doc's prose claims --------------------------------------
    for token in ('mount -t debugfs', 'UMNT_RC=0', 'active_cnt', 'strict_memory',
                  'BYPASS', 'four rings', 'every one of them has IOVA',
                  'cannot distinguish'):
        check(token in dflat, 'DOC records %r' % token)
    check('DEVICE-SOURCED' in doc, 'DOC names its evidence class')
    check('STAYS `"host-verified"`' in doc, 'DOC keeps the area host-verified')
    check('\r' not in doc, 'DOC is pure LF')
    check(not doc.startswith('\ufeff'), 'DOC has no BOM')
    # The doc must REFUSE to over-claim the cause of the zero IOVAs.
    check('does not claim it can' in dflat or 'cannot distinguish them' in dflat,
          'DOC refuses to attribute a single cause to the zero IOVAs')

    # ---- live re-read (mounts debugfs, always unmounts) --------------
    if not args.skip_device:
        r = subprocess.run([args.adb, 'devices'], capture_output=True, text=True, timeout=60)
        lines = (r.stdout or '').splitlines()
        USABLE = {'device', 'recovery', 'sideload', 'rescue', 'bootloader'}
        online = [l for l in lines[1:]
                  if len(l.split()) >= 2 and l.split()[1] in USABLE]
        check(len(online) == 1, 'exactly one usable device is attached')

        before = sh(args.adb, 'mount | grep -c debugfs')
        text = ''
        try:
            sh(args.adb, 'mount -t debugfs none /sys/kernel/debug')
            mounted = sh(args.adb, 'mount | grep -c debugfs')
            check(mounted == '1', 'debugfs mounted for the read (got %r)' % mounted)
            active = sh(args.adb, 'cat /sys/kernel/debug/kgsl/kgsl-3d0/active_cnt')
            check(active == '0', 'DEVICE active_cnt == 0 (got %r)' % active)
            ctx = sh(args.adb, 'ls /sys/kernel/debug/kgsl/kgsl-3d0/ctx')
            check(ctx.strip() == '', 'DEVICE ctx/ is empty (got %r)' % ctx)
            strict = sh(args.adb, 'cat /sys/kernel/debug/kgsl/debug/strict_memory')
            check(strict == '0', 'DEVICE strict_memory == 0 (got %r)' % strict)
            text = sh(args.adb, 'cat /sys/kernel/debug/kgsl/globals')
        finally:
            # ALWAYS restore, even on an assertion or subprocess failure.
            sh(args.adb, 'umount /sys/kernel/debug')
            after = sh(args.adb, 'mount | grep -c debugfs')
            check(after == before,
                  'debugfs mount count restored %r -> %r' % (before, after))

        rows_live = parse_table(text)
        analyse(rows_live, 'LIVE')
        check(rows_live == [tuple(r) for r in EXPECT],
              'LIVE table matches the recorded 17 rows exactly '
              '(got %d rows)' % len(rows_live))

    print('Domain A: the recorded table and prose, and a live re-read where the '
          'mount is always restored%s' % ('' if not args.skip_device else ' (SKIPPED)'))
    print('Domain B: 17 entries, four complete ring sets, real sizes, and EVERY '
          'iova exactly 0x0-0x0 with a degenerate range')
    print('KGSL_GLOBALS_VERIFY: %s (%d checks, %d failures)'
          % ('PASS' if not fails else 'FAIL', checks, len(fails)))
    for f in fails:
        print('  FAIL ' + f)
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
