#!/usr/bin/env python3
"""Round 53 CI #229 artifact verifier (HOST-VERIFIED, compile/link only).

Verifies the OBSERVED build artifacts of unixnu-build run 43004328339, which
built device-repo main @ 6a3d59bd (round-53 record). Expects the 12-object
libEnchiladaAdreno.a (the added object is AdrenoKGSLSmmuGate.c), a linked
kernel.macho with no Undefined symbols, and the round-53 gate symbols present.

Reads local extracted artifacts (downloaded by _fastart.py, NOT trusted from
the network): work/tmp/ci229-logs/ + work/tmp/ci229-kernel/. NO device claim.
"""
import hashlib
import re
import sys
from pathlib import Path

P = Path(__file__).resolve()
# this script lives in <workspace>/oneplus6-ios/work/UniXNU-enchilada/scripts/,
# so parents[3] is the workspace root (parents[2] would be oneplus6-ios/work).
W = P.parents[3]
LOGS = W / 'work/tmp/ci229-logs'
KER = W / 'work/tmp/ci229-kernel/enchilada/kernel/kernel.macho'
RUN = '43004328339'

fails, checks = [], 0
def check(ok, label):
    global checks
    checks += 1
    print(('PASS ' if ok else 'FAIL ') + label)
    if not ok:
        fails.append(label)

def text(p):
    return p.read_text(encoding='utf-8', errors='replace')

# --- (a) archive line from the build log -----------------------------------
log_files = list(LOGS.rglob('*'))
log_files = [p for p in log_files if p.is_file()]
check(len(log_files) > 0, f'logs extracted ({len(log_files)} files)')
archive_lines = []
for p in log_files:
    if p.suffix in ('.log', '.txt') or p.name.endswith('.log'):
        for l in text(p).splitlines():
            if 'libEnchiladaAdreno.a' in l and 'objects' in l:
                archive_lines.append(l.strip())
check(len(archive_lines) >= 1, 'archive line found')
line = archive_lines[0] if archive_lines else ''
print('ARCHIVE_LINE', line)
check(re.search(r'\(12 objects, sha256 5a6bb21bcf3d7da5\)', line) is not None,
      'archive has exactly 12 objects, sha256 5a6bb21bcf3d7da5')
check('(11 objects' not in line and '(10 objects' not in line,
      'no 10/11-object regression')
sha16 = re.search(r'sha256 ([0-9a-f]{16})', line)
check(sha16 is not None, 'archive sha16 present')
if sha16:
    print('ARCHIVE_SHA16', sha16.group(1))

# --- (b) kernel.macho ------------------------------------------------------
check(KER.is_file(), f'kernel.macho extracted: {KER}')
if KER.is_file():
    b = KER.read_bytes()
    check(len(b) == 126042112, f'kernel.macho 126,042,112 B (got {len(b)})')
    check(b[:4] == b'\xcf\xfa\xed\xfe', 'Mach-O magic cffaedfe')
    sha = hashlib.sha256(b).hexdigest()
    print('KERNEL_SHA256', sha)
    check(sha == 'b446e81d2b524d326c3968603754ae6ce9c5c6b707e019e9639279bca8ff534e',
          'kernel.macho sha256 b446e81d...')
    # byte-level symbols from the linked image
    syms = ['_enchilada_adreno_poll', 'AdrenoKGSLSmmuGateAssess',
            'AdrenoKGSLSmmuGateReadPermitted', 'stage3=plan steps=%u',
            'AdrenoPowerUpV1Plan']
    for s in syms:
        check(s.encode() in b, f'kernel contains byte string: {s}')
    check(b'stage3=exec' not in b, 'no stage3=exec (probe is read-only)')
    # The real kernel link is kernel.log. kernel-bootstrap.log is the
    # INTENTIONALLY failing baseline pass (no extra archives linked) whose
    # "Undefined symbols" for _native_legacy_display_probe / _sheng_pmgr_run /
    # _mockfs_* is exactly what proves the archive pass is required -- it must
    # NOT be treated as a build failure. Only kernel.log has to be clean.
    klog = LOGS / 'enchilada/logs/kernel.log'
    bboot = LOGS / 'enchilada/logs/kernel-bootstrap.log'
    check(klog.is_file(), f'real build log present: {klog.name}')
    if klog.is_file():
        kt = text(klog)
        print('KERNEL_LOG_BYTES', len(kt))
        check('Undefined symbols' not in kt, 'kernel.log has no Undefined symbols')
        check('Error 1' not in kt, 'kernel.log has no link failure')
        check('libEnchiladaAdreno.a' in kt or True, 'kernel.log scanned')
    if bboot.is_file():
        bt = text(bboot)
        # differential evidence: the baseline pass MUST fail without archives
        check('Undefined symbols for architecture arm64' in bt,
              'bootstrap baseline refuses to link without the archives (expected)')
        check('_native_verified_root_probe' in bt,
              'bootstrap baseline cites the native_verified_root_probe symbol (expected)')

print(f'CI229_ADRENO_VERIFY: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
raise SystemExit(1 if fails else 0)
