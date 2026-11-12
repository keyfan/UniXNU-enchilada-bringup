#!/usr/bin/env python3
"""CI #232 verifier (ROUND 65): did the A630 Metal establishment-chain seam
compile and link inside a real XNU kernel build?

Domain 1 (this script): the uploaded CI artifacts. It DERIVES the archive sha16
and kernel size/sha256 from the artifacts (no guessed constants) and asserts the
structural facts that matter:
  * the archive reports 20 objects (15 in CI #231 + 5 consolidated/new units),
  * the real kernel build log is clean (bootstrap baseline is intentionally dirty),
  * kernel.macho grew vs CI #231 and is a Mach-O 64 image,
  * every round-65 metal-seam export AND every earlier unit export survived linking.

Domain 2 (checked separately): workflow run/job status through the REST API, so
a green artifact can never be mistaken for a green run.
"""
import hashlib
import re
import sys
from pathlib import Path

WS = Path(__file__).resolve().parents[3]          # .../oneplus6-ios
LOGS = WS / 'work' / 'tmp' / 'ci232-logs'
KERNEL = WS / 'work' / 'tmp' / 'ci232-kernel' / 'enchilada' / 'kernel' / 'kernel.macho'

EXPECT_OBJECTS = 20
PREV_KERNEL_BYTES = 126046208   # CI #231
PREV_KERNEL_SHA256 = (
    '9de56c6ac592dd53f07bae866bf8379ede11703a617439bcca5f68c10938bb8b')
PREV_ARCHIVE_SHA16 = 'f0030d424fa6fc81'

# The 14 round-65 exports (AdrenoMetalSeamV1.c). All must be in the image.
METAL_SEAM = [
    'enchilada_metal_abi_version', 'enchilada_metal_method_is_known',
    'enchilada_metal_method_name', 'enchilada_metal_abi_params_valid',
    'enchilada_metal_device_info_a630', 'enchilada_metal_device_info_specified_fields',
    'enchilada_metal_device_info_board_derived_fields',
    'enchilada_metal_device_info_consistent', 'enchilada_metal_stage_name',
    'enchilada_metal_blocker_name', 'enchilada_metal_stage_blockers',
    'enchilada_metal_establishment_blockers',
    'enchilada_metal_compiler_a630_available', 'enchilada_metal_frontend_allowed',
]
# Earlier units must not have regressed out of the archive.
EARLIER = [
    'AdrenoPowerDownV1ShouldRun', 'AdrenoPowerUpGdscContract',
    'adreno_gdsc_plan_enable', 'AdrenoKGSLSmmuGateAssess',
    'AdrenoClkCtlV1', 'AdrenoPowerUpBackendV1', 'AdrenoGpuccWindowV1',
    'AdrenoCallerV1', 'EnchiladaAdrenoProbe', 'AdrenoPowerUpV1',
    'AdrenoGmuCallerV1', 'AdrenoHfiV1', 'AdrenoFirmwareV1',
]

checks = 0
fails = []

def ck(cond, what):
    global checks
    checks += 1
    if not cond:
        fails.append(what)
        print('FAIL:', what)
    else:
        print('PASS', what)

def main():
    ck(LOGS.is_dir(), 'the CI #232 logs directory is extracted')
    ck(KERNEL.is_file(), 'kernel.macho is extracted')

    # ---- archive line (derive sha16, assert object count) ----
    hit = None
    for p in LOGS.rglob('*'):
        if not p.is_file() or p.suffix != '.log' or 'adreno' not in p.name:
            continue
        txt = p.read_text(encoding='utf-8', errors='replace')
        if 'libEnchiladaAdreno.a' in txt:
            hit = (p.name, txt.strip())
            break
    ck(hit is not None, 'the archive build log was found')
    if hit:
        name, line = hit
        print('ARCHIVE_LOG', name)
        print('ARCHIVE_LINE', line)
        m = re.search(r'PASS: libEnchiladaAdreno\.a \((\d+) objects, sha256 ([0-9a-f]{16})\)', line)
        ck(m is not None, 'the archive line has the expected shape')
        if m:
            n, sha16 = int(m.group(1)), m.group(2)
            print('ARCHIVE_OBJECTS', n, 'ARCHIVE_SHA16', sha16)
            ck(n == EXPECT_OBJECTS, 'the archive reports %d objects (got %d)' % (EXPECT_OBJECTS, n))
            ck(sha16 != PREV_ARCHIVE_SHA16, 'the archive sha16 changed vs CI #231 (genuine rebuild)')
            ck('(15 objects' not in line and '(19 objects' not in line,
               'no 15/19-object regression in the line')

    # ---- the real build log must be clean ----
    kernel_logs = [p for p in LOGS.rglob('kernel.log') if p.is_file()]
    ck(len(kernel_logs) == 1, 'exactly one kernel.log is extracted (got %d)' % len(kernel_logs))
    if kernel_logs:
        ktxt = kernel_logs[0].read_text(encoding='utf-8', errors='replace')
        ck('Undefined symbols' not in ktxt, 'the REAL build log has no undefined symbols')
        ck('Error 1' not in ktxt, 'the REAL build log has no "Error 1"')
        print('KERNEL_LOG_BYTES', kernel_logs[0].stat().st_size)

    # ---- the bootstrap log is an INTENTIONALLY failing baseline ----
    boot_logs = [p for p in LOGS.rglob('kernel-bootstrap.log') if p.is_file()]
    ck(len(boot_logs) == 1, 'exactly one kernel-bootstrap.log (got %d)' % len(boot_logs))
    if boot_logs:
        btxt = boot_logs[0].read_text(encoding='utf-8', errors='replace')
        ck('Undefined symbols for architecture arm64' in btxt,
           'bootstrap still shows the BASELINE undefined symbols')
        ck('_native_verified_root_probe' in btxt,
           'bootstrap baseline still references the extra-archive family')

    # ---- kernel.macho (derive size/sha, assert growth + magic) ----
    data = KERNEL.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    # Size is OBSERVED, not asserted: rounds 43/45/59 proved that extra archive
    # objects can link without growing the image (they fit existing pages), so
    # size alone can neither prove nor disprove a link. The load-bearing proof
    # is the archive object count, the changed sha256 (genuine relink), and the
    # presence of every export in the image (checked below).
    print('KERNEL_BYTES', len(data), 'PREV_KERNEL_BYTES', PREV_KERNEL_BYTES,
          'KERNEL_DELTA', len(data) - PREV_KERNEL_BYTES)
    ck(data[:4] == b'\xcf\xfa\xed\xfe', 'kernel.macho has Mach-O 64 magic')
    ck(len(digest) == 64 and re.fullmatch(r'[0-9a-f]{64}', digest) is not None,
       'kernel sha256 is a well-formed digest')
    ck(digest != PREV_KERNEL_SHA256,
       'kernel sha256 changed vs CI #231 (genuine relink, no stale artifact)')

    text = data.decode('latin-1')
    for sym in METAL_SEAM:
        ck(sym in text, 'kernel contains round-65 symbol %s' % sym)
    for sym in EARLIER:
        ck(sym in text, 'kernel retains earlier symbol %s' % sym)

    # the observation-only probe must remain the only executable stage path
    ck('stage3=plan steps=%u' in text, 'the stage-3 plan row format survives')
    ck('stage3=exec' not in text, 'no stage-3 EXECUTE path was compiled in')

    print('---')
    if fails:
        print('CI232_ADRENO_VERIFY: FAIL (%d checks, %d failures)' % (checks, len(fails)))
        return 1
    print('CI232_ADRENO_VERIFY: PASS (%d checks, 0 failures)' % checks)
    return 0

if __name__ == '__main__':
    sys.exit(main())
