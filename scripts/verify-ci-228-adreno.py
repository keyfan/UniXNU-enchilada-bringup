#!/usr/bin/env python3
"""ROUND 52: two-domain verification of CI #228 (stage-3 kernel probe).

Domain A (-logs artifact): requires the exact one-line archive-23 PASS with
11 objects, no Undefined symbols in kernel.log, and no 10/12-object regression.
Domain B (-kernel artifact): requires kernel.macho magic/size, the stage-3
PLAN format string, the pure AdrenoPowerUpV1 Plan helpers, and the shim symbol.
The artifact paths are the extracted work/tmp/ci228-logs and ci228-kernel dirs
(produced by _fastart.py + _extract_ci228.py). Nothing here touches the device.
"""
import hashlib
import sys
from pathlib import Path

W = Path(r'C:\Users\LCPC\Documents\deepseek-harness\default-workspace\oneplus6-ios')
LOGS = W / 'work' / 'tmp' / 'ci228-logs' / 'enchilada' / 'logs'
KDIR = W / 'work' / 'tmp' / 'ci228-kernel' / 'enchilada' / 'kernel'

fails = []
checks = [0]


def need(ok, label, extra=''):
    checks[0] += 1
    print(('PASS' if ok else 'FAIL') + ': ' + label + ((' | ' + str(extra)) if extra else ''))
    if not ok:
        fails.append(label)


def sha(b):
    return hashlib.sha256(b).hexdigest()


need(LOGS.is_dir(), '-logs artifact extracted')
arc = LOGS / 'archive-23-build-enchilada-adreno.log'
need(arc.is_file(), 'archive-23 log present')
if arc.is_file():
    line = arc.read_text(encoding='utf-8', errors='replace').strip()
    need(line == 'PASS: libEnchiladaAdreno.a (11 objects, sha256 d2f35797698a7ba2)',
         'archive line exact (11 objects)', line)
    need('(10 objects' not in line and '(12 objects' not in line,
         'no 10/12-object regression')
klog = LOGS / 'kernel.log'
need(klog.is_file(), 'kernel.log present')
if klog.is_file():
    kt = klog.read_text(encoding='utf-8', errors='replace')
    need('Undefined symbols' not in kt, 'no Undefined symbols in kernel.log')

km = KDIR / 'kernel.macho'
need(km.is_file(), '-kernel artifact extracted')
if km.is_file():
    data = km.read_bytes()
    need(data[:4] == b'\xcf\xfa\xed\xfe', 'Mach-O 64 magic 0xfeedfacf', data[:4].hex())
    need(len(data) == 126038016, 'kernel.macho size 126,038,016 B', len(data))
    print('KERNEL_SHA256', sha(data))
    for sym in (b'_enchilada_adreno_poll', b'stage3=plan steps=%u',
                b'AdrenoPowerUpV1Plan', b'AdrenoPowerUpV1PreIsReady',
                b'AdrenoPowerUpV1InputsValid'):
        need(sym in data, 'kernel contains ' + sym.decode())
    need(b'stage3=exec' not in data, 'no stage3=exec (Execute never called)')

print('CI228_ADRENO_VERIFY: %s (%d checks, %d failures)' %
      ('PASS' if not fails else 'FAIL', checks[0], len(fails)))
sys.exit(0 if not fails else 1)
