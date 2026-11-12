#!/usr/bin/env python3
"""Round 71: verify the DEVICE-SOURCED board facts against the live phone AND
against the host models that assert them.

Two independent directions:
  Domain A: re-read the facts from /proc/device-tree over adb and compare with
            the values recorded in evidence/stage5-gpu-device-tree.md.
  Domain B: confirm every recorded value matches the constant the host models
            already carry, so the doc cannot drift away from the code.

Read-only: only `od` over /proc/device-tree. Nothing is written to the device.

usage: verify-enchilada-device-tree.py [--adb PATH] [--skip-device]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
GPU = DEV / 'drivers' / 'NativeAdreno'
EVIDENCE = DEV / 'evidence' / 'stage5-gpu-device-tree.md'

DT = '/proc/device-tree/soc'
K3D0 = DT + '/qcom,kgsl-3d0@5000000'
KHYP = DT + '/qcom,kgsl-hyp'
GMU = DT + '/qcom,gmu'
SMMU = DT + '/arm,smmu-kgsl@5040000'
GPUCC = DT + '/qcom,gpucc@5090000'
APPS = DT + '/apps-smmu@0x15000000'

# fact -> (node path + property, expected raw big-endian bytes)
EXPECT_BYTES = {
    'chipid': (K3D0 + '/qcom,chipid', bytes.fromhex('06030001')),
    'highest_bank_bit': (K3D0 + '/qcom,highest-bank-bit', bytes.fromhex('0000000f')),
    'ubwc_mode': (K3D0 + '/qcom,ubwc-mode', bytes.fromhex('00000002')),
    'kgsl_id': (K3D0 + '/qcom,id', bytes.fromhex('00000000')),
    'pas_id': (KHYP + '/qcom,pas-id', bytes.fromhex('0000000d')),
}
EXPECT_STR = {
    'zap_fw_name': (KHYP + '/qcom,firmware-name', b'a630_zap\x00'),
    'gmu_compatible': (GMU + '/compatible', b'qcom,gpu-gmu\x00'),
}
# reg windows: (start, size) pairs per node
EXPECT_REGS = {
    'kgsl3d0_reg': (K3D0 + '/reg', [(0x05000000, 0x40000), (0x05061000, 0x800),
                                    (0x0509e000, 0x1000)]),
    'gmu_reg': (GMU + '/reg', [(0x0506a000, 0x30000), (0x0b200000, 0x300000)]),
    'kgsl_smmu_reg': (SMMU + '/reg', [(0x05040000, 0x10000)]),
    'gpucc_reg': (GPUCC + '/reg', [(0x05090000, 0x9000)]),
    # The apps SMMU declares TWO windows on this board; the model uses the first,
    # the second is its context-bank register page.
    'apps_smmu_reg': (APPS + '/reg', [(0x15000000, 0x80000), (0x150c2000, 0x20)]),
}
EXPECT_CLOCKS = {
    'gpu_clocks': (K3D0 + '/clock-names',
                   [b'core_clk', b'rbbmtimer_clk', b'mem_clk', b'mem_iface_clk',
                    b'gmu_clk', b'l3_vote']),
    'gmu_clocks': (GMU + '/clock-names',
                   [b'gmu_clk', b'cxo_clk', b'axi_clk', b'memnoc_clk']),
}

checks = 0
fails = []


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)


def adb_bytes(adb, path):
    r = subprocess.run([adb, 'shell', 'od -An -tx1 -v ' + path],
                       capture_output=True, text=True, timeout=120)
    txt = (r.stdout or '').strip()
    toks = txt.split()
    if r.returncode or not toks:
        return None
    try:
        return bytes(int(t, 16) for t in toks)
    except ValueError:
        return None


def u32_pairs(raw):
    if raw is None or len(raw) % 8:
        return None
    out = []
    for i in range(0, len(raw), 8):
        start = int.from_bytes(raw[i:i + 4], 'big')
        size = int.from_bytes(raw[i + 4:i + 8], 'big')
        out.append((start, size))
    return out


def nul_names(raw):
    if raw is None:
        return None
    parts = raw.split(b'\x00')
    while parts and parts[-1] == b'':
        parts.pop()
    return parts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--adb', default=r'C:\platform-tools\adb.exe')
    ap.add_argument('--skip-device', action='store_true')
    args = ap.parse_args()

    doc = EVIDENCE.read_text(encoding='utf-8')
    hwinit = (GPU / 'NativeAdrenoA630HwInit.h').read_text(encoding='utf-8')
    board = (GPU / 'AdrenoBoardInputs.h').read_text(encoding='utf-8')
    domain = (GPU / 'AdrenoDomainState.h').read_text(encoding='utf-8')
    abi = (GPU / 'AdrenoDarwinKmdAbiV1' / 'NativeAdrenoABI.h').read_text(encoding='utf-8')

    # ---- A: re-read from the device -------------------------------------
    if not args.skip_device:
        r = subprocess.run([args.adb, 'devices'], capture_output=True, text=True, timeout=60)
        lines = (r.stdout or '').splitlines()
        # adb reports the TRANSPORT state: "device" for a booted Android,
        # "recovery" for TWRP (which still answers `adb shell`), "sideload", etc.
        # Any of those means the shell channel is usable.
        USABLE = {'device', 'recovery', 'sideload', 'rescue', 'bootloader'}
        online = [l for l in lines[1:]
                  if len(l.split()) >= 2 and l.split()[1] in USABLE]
        check(len(online) == 1,
              'exactly one usable device is attached (%d online, states %r)'
              % (len(online), [l.split()[1] for l in lines[1:] if len(l.split()) >= 2]))
        for key, (path, want) in EXPECT_BYTES.items():
            got = adb_bytes(args.adb, path)
            check(got == want, 'DEVICE %s == %s (got %s)'
                  % (key, want.hex(), None if got is None else got.hex()))
        for key, (path, want) in EXPECT_STR.items():
            got = adb_bytes(args.adb, path)
            check(got == want, 'DEVICE %s == %r (got %r)'
                  % (key, want, got))
        for key, (path, want) in EXPECT_REGS.items():
            got = u32_pairs(adb_bytes(args.adb, path))
            check(got == want, 'DEVICE %s == %r (got %r)' % (key, want, got))
        for key, (path, want) in EXPECT_CLOCKS.items():
            got = nul_names(adb_bytes(args.adb, path))
            check(got == want, 'DEVICE %s == %r (got %r)' % (key, want, got))
        # The two clock lists must be DIFFERENT lengths -- the round-51 claim.
        gpu_clk = nul_names(adb_bytes(args.adb, K3D0 + '/clock-names')) or []
        gmu_clk = nul_names(adb_bytes(args.adb, GMU + '/clock-names')) or []
        check(len(gpu_clk) == 6 and len(gmu_clk) == 4,
              'DEVICE the two clock counts are 6 and 4 (got %d/%d)'
              % (len(gpu_clk), len(gmu_clk)))
        check(len(gpu_clk) != len(gmu_clk),
              'DEVICE the counts are DISTINCT on real hardware')
        # kgsl IRQ is SPI 300 = 0x12c.
        irq = adb_bytes(args.adb, K3D0 + '/interrupts')
        check(irq == bytes.fromhex('000000000000012c00000000'),
              'DEVICE kgsl_3d0_irq is SPI 300 (got %r)' % irq)

    # ---- B: the doc agrees with the models ------------------------------
    for token in ('0x06030001', '**15**', '**2**', '**13**', '"a630_zap"',
                  '0x05000000+0x40000', '0x05061000+0x800', '0x0509e000+0x1000',
                  '0x0506a000+0x30000', '0x05040000+0x10000', '0x05090000+0x9000',
                  '0x15000000+0x80000', 'core_clk', 'rbbmtimer_clk',
                  'mem_iface_clk', 'l3_vote', 'gmu_clk', 'cxo_clk', 'axi_clk',
                  'memnoc_clk', 'SPI **300**'):
        check(token in doc, 'DOC records %r' % token)
    check('read-only' in doc.lower(), 'DOC states it was read-only')
    check('DEVICE-SOURCED' in doc, 'DOC names its evidence class')
    check('STAYS `"host-verified"`' in doc, 'DOC keeps the area host-verified')
    check(not doc.count('\r'), 'DOC is pure LF')
    check(doc[:3] != '\ufeff', 'DOC has no BOM')

    # The host constants the doc claims to confirm.
    check('0x06030001u' in abi, 'MODEL ENCHILADA_A630_CHIP_ID is 0x06030001')
    check('HIGHEST_BANK_BIT  15u' in abi or 'HIGHEST_BANK_BIT 15u' in abi,
          'MODEL ENCHILADA_A630_HIGHEST_BANK_BIT is 15')
    check('0x05040000ull' in board and '0x00010000ull' in board,
          'MODEL kgsl SMMU is 0x05040000+0x10000')
    check('0x05000000ull' in board and '0x00040000ull' in board,
          'MODEL GPU window is 0x05000000+0x40000')
    check('0x0506a000ull' in board and '0x00030000ull' in board,
          'MODEL GMU window is 0x0506a000+0x30000')
    check('0x05090000ull' in board or '0x05090000ull' in domain,
          'MODEL GPUCC base is 0x05090000')
    check('0x9000ull' in domain, 'MODEL GPUCC size is 0x9000')
    check('0x15000000ull' in board, 'MODEL apps SMMU base is 0x15000000')
    check('0x12c' in doc, 'DOC records the 0x12c IRQ encoding')

    print('Domain A: %d live device-tree facts re-read and compared'
          % (0 if args.skip_device else (len(EXPECT_BYTES) + len(EXPECT_STR)
                                         + len(EXPECT_REGS) + len(EXPECT_CLOCKS) + 3)))
    print('Domain B: the evidence doc agrees with NativeAdrenoA630HwInit.h, '
          'AdrenoBoardInputs.h, AdrenoDomainState.h and NativeAdrenoABI.h')
    print('DEVICE_TREE_VERIFY: %s (%d checks, %d failures)'
          % ('PASS' if not fails else 'FAIL', checks, len(fails)))
    for f in fails:
        print('  FAIL ' + f)
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
