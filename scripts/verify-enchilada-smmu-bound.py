#!/usr/bin/env python3
"""Round 82: verify that the kgsl SMMU is bound and that page tables exist, and
that this CORRECTS the round-75 wording rather than contradicting its blockers.

Three directions:
  Domain A: re-read the live device (mounting debugfs, ALWAYS unmounting in a
            finally block, asserting the mount count is restored).
  Domain B: assert the evidence doc records the measurements AND the precise
            correction -- the port has no page table; the DEVICE has 92 pages.
  Domain C: confront round 75's own header, proving its four blocker identifiers
            still exist while its "no page table" PHRASING is the thing corrected.

usage: verify-enchilada-smmu-bound.py [--adb PATH] [--skip-device]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
GPU = DEV / 'drivers' / 'NativeAdreno'
EVIDENCE = DEV / 'evidence' / 'stage5-gpu-smmu-bound.md'
STAGE1_H = GPU / 'AdrenoSmmuStage1V1.h'

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--adb', default=r'C:\platform-tools\adb.exe')
    ap.add_argument('--skip-device', action='store_true')
    args = ap.parse_args()

    doc = EVIDENCE.read_text(encoding='utf-8')
    dflat = ' '.join(doc.split())

    # ---- the doc's measurements --------------------------------------
    for token in ('92', 'io-pgtable/pages', '5040000.arm,smmu-kgsl',
                  '15000000.apps-smmu', 'qcom,smmu-v2', 'UMNT_RC=0',
                  'unsupported', 'Busy=10', '0x00005ff4'):
        check(token in dflat, 'DOC records %r' % token)
    check('DEVICE-SOURCED' in doc, 'DOC names its evidence class')
    check('STAYS `"host-verified"`' in doc, 'DOC keeps the area host-verified')
    check('\r' not in doc, 'DOC is pure LF')
    check(not doc.startswith('\ufeff'), 'DOC has no BOM')
    # The correction must be stated as a correction, not as a reversal.
    check('corrects a claim' in dflat or 'correction' in dflat.lower(),
          'DOC frames this as a correction')
    check('the port has no page table' in dflat.lower() or
          'the DEVICE has both' in dflat,
          'DOC distinguishes the port absence from the device presence')
    # And it must NOT claim the blockers are resolved.
    check('unchanged' in dflat, 'DOC keeps the blockers unchanged')
    check('does **not** unblock stage 5' in dflat,
          'DOC does not unblock stage 5')

    # ---- round 75's four blockers still exist in its header -----------
    h = STAGE1_H.read_text(encoding='utf-8')
    for ident in ('kAdrenoSmmuInputPageTable', 'kAdrenoSmmuInputAsid',
                  'kAdrenoSmmuInputStreamBinding', 'kAdrenoSmmuInputCxPowered'):
        check(ident in h, 'ROUND-75 header still declares %s' % ident)
    check('kAdrenoSmmuInputAll = 15u' in h or 'kAdrenoSmmuInputAll = 15u' in h,
          'ROUND-75 header still declares the four-bit all mask')
    # And its ready() is still derived (so it cannot silently flip).
    c = (GPU / 'AdrenoSmmuStage1V1.c').read_text(encoding='utf-8')
    check('adreno_smmu_stage1_missing() == 0u ? 1 : 0' in c,
          'ROUND-75 ready() is still DERIVED')

    # ---- live re-read -------------------------------------------------
    if not args.skip_device:
        r = subprocess.run([args.adb, 'devices'], capture_output=True, text=True, timeout=60)
        lines = (r.stdout or '').splitlines()
        USABLE = {'device', 'recovery', 'sideload', 'rescue', 'bootloader'}
        online = [l for l in lines[1:]
                  if len(l.split()) >= 2 and l.split()[1] in USABLE]
        check(len(online) == 1, 'exactly one usable device is attached')

        before = sh(args.adb, 'mount | grep -c debugfs')
        try:
            sh(args.adb, 'mount -t debugfs none /sys/kernel/debug')
            mounted = sh(args.adb, 'mount | grep -c debugfs')
            check(mounted == '1', 'debugfs mounted for the read (got %r)' % mounted)

            pages = sh(args.adb, 'cat /sys/kernel/debug/iommu/io-pgtable/pages')
            check(pages == '92', 'DEVICE io-pgtable/pages == 92 (got %r)' % pages)

            bound = sh(args.adb, 'ls /sys/bus/platform/drivers/arm-smmu/')
            check('5040000.arm,smmu-kgsl' in bound,
                  'DEVICE the kgsl SMMU is bound to arm-smmu')
            check('15000000.apps-smmu' in bound,
                  'DEVICE the apps SMMU is bound to arm-smmu')

            modalias = sh(args.adb,
                          'cat /sys/devices/platform/soc/5040000.arm,smmu-kgsl/modalias')
            check('qcom,smmu-v2' in modalias,
                  'DEVICE modalias carries qcom,smmu-v2 (got %r)' % modalias)
            check('arm,smmu-kgsl' in modalias, 'DEVICE modalias names arm,smmu-kgsl')

            status = sh(args.adb,
                        'cat /sys/devices/platform/soc/5040000.arm,smmu-kgsl/power/runtime_status')
            check(status == 'unsupported',
                  'DEVICE SMMU runtime_status is unsupported (got %r)' % status)
            active = sh(args.adb,
                        'cat /sys/devices/platform/soc/5040000.arm,smmu-kgsl/power/runtime_active_time')
            check(active == '0', 'DEVICE SMMU runtime_active_time is 0 (got %r)' % active)

            tests = sh(args.adb, 'ls /sys/kernel/debug/iommu/tests/')
            check('apps_iommu_test_device' in tests,
                  'DEVICE apps iommu test device present')
            check('kgsl' not in tests.lower(),
                  'DEVICE there is NO kgsl-specific iommu test node')
        finally:
            sh(args.adb, 'umount /sys/kernel/debug')
            after = sh(args.adb, 'mount | grep -c debugfs')
            check(after == before,
                  'debugfs mount count restored %r -> %r' % (before, after))

    print('Domain A: the live device re-read, with the mount always restored%s'
          % ('' if not args.skip_device else ' (SKIPPED)'))
    print('Domain B: the doc records 92 pages, both bound SMMUs, the modalias and '
          'the precise port-vs-device correction')
    print('Domain C: round 75 four blocker identifiers and the derived ready() are '
          'unchanged, so the correction is wording, not a reversal')
    print('SMMU_BOUND_VERIFY: %s (%d checks, %d failures)'
          % ('PASS' if not fails else 'FAIL', checks, len(fails)))
    for f in fails:
        print('  FAIL ' + f)
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
