#!/usr/bin/env python3
"""CI #231 verifier: did the round-59 15th A630 archive unit actually compile
and link inside a real XNU kernel build?

Domain 1 (this script): the uploaded CI artifacts.
Domain 2: the workflow run/job status through the REST API, checked separately
so a green artifact can never be mistaken for a green run.
"""
import hashlib
import sys
from pathlib import Path

WS = Path(__file__).resolve().parents[3]          # .../oneplus6-ios
LOGS = WS / 'work' / 'tmp' / 'ci231-logs'
KERNEL = WS / 'work' / 'tmp' / 'ci231-kernel' / 'enchilada' / 'kernel' / 'kernel.macho'

EXPECT_ARCHIVE = 'PASS: libEnchiladaAdreno.a (15 objects, sha256 '
EXPECT_ARCHIVE_SHA16 = 'f0030d424fa6fc81'
EXPECT_KERNEL_BYTES = 126046208
EXPECT_KERNEL_SHA256 = (
    '9de56c6ac592dd53f07bae866bf8379ede11703a617439bcca5f68c10938bb8b')

# Symbols that MUST be present in the linked kernel: an archive unit is only
# "linked" if its code survived into the image. The round-59 teardown module is
# the point of this run, so every one of its nine exports is asserted.
MUST_HAVE = [
    'AdrenoPowerDownV1ShouldRun',
    'AdrenoPowerDownV1InputsValid',
    'AdrenoPowerDownV1StepGuarded',
    'AdrenoPowerDownV1StepStatus',
    'AdrenoPowerDownV1PlanSize',
    'AdrenoPowerDownV1Plan',
    'AdrenoPowerDownV1Execute',
    'AdrenoPowerDownV1CxOffIsUnverified',
    'AdrenoPowerDownV1Verify',
    # round-58 units must not have regressed out
    'AdrenoPowerUpGdscContract',
    'AdrenoPowerUpGdscVerify',
    'adreno_gdsc_plan_enable',
    'AdrenoKGSLSmmuGateAssess',
    'EnchiladaAdrenoProbe',
]
# The previous round's kernel was 126,042,112 B; a 16th object that fails to
# link would leave the size unchanged, so the size is asserted exactly.
PREV_KERNEL_BYTES = 126042112

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
    ck(LOGS.is_dir(), 'the CI #231 logs directory is extracted')
    ck(KERNEL.is_file(), 'kernel.macho is extracted')

    # ---- archive line ----
    hit = None
    for p in LOGS.rglob('*'):
        if not p.is_file() or p.suffix != '.log':
            continue
        if 'adreno' not in p.name:
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
        ck(line.startswith(EXPECT_ARCHIVE), 'the archive reports 15 objects')
        ck(EXPECT_ARCHIVE_SHA16 in line,
           'the archive sha256 prefix is %s' % EXPECT_ARCHIVE_SHA16)
        ck('(14 objects' not in line and '(13 objects' not in line,
           'no 13/14-object regression')

    # ---- the real build log must be clean ----
    kernel_logs = [p for p in LOGS.rglob('kernel.log') if p.is_file()]
    ck(len(kernel_logs) == 1,
       'exactly one kernel.log is extracted (got %d)' % len(kernel_logs))
    if kernel_logs:
        ktxt = kernel_logs[0].read_text(encoding='utf-8', errors='replace')
        ck('Undefined symbols' not in ktxt,
           'the REAL build log has no undefined symbols')
        ck('Error 1' not in ktxt, 'the REAL build log has no "Error 1"')
        print('KERNEL_LOG_BYTES', kernel_logs[0].stat().st_size)

    # ---- the bootstrap log is an INTENTIONALLY failing baseline ----
    boot_logs = [p for p in LOGS.rglob('kernel-bootstrap.log') if p.is_file()]
    ck(len(boot_logs) == 1,
       'exactly one kernel-bootstrap.log is extracted (got %d)' % len(boot_logs))
    if boot_logs:
        btxt = boot_logs[0].read_text(encoding='utf-8', errors='replace')
        ck('Undefined symbols for architecture arm64' in btxt,
           'the bootstrap log still shows the BASELINE undefined symbols')
        ck('_native_verified_root_probe' in btxt,
           'the bootstrap baseline still references the extra-archive family')

    # ---- kernel.macho ----
    data = KERNEL.read_bytes()
    ck(len(data) == EXPECT_KERNEL_BYTES,
       'kernel.macho is %d bytes (got %d)' % (EXPECT_KERNEL_BYTES, len(data)))
    ck(len(data) > PREV_KERNEL_BYTES,
       'kernel.macho GREW vs CI #230 by %d bytes (the 15th object really linked)'
       % (len(data) - PREV_KERNEL_BYTES))
    ck(data[:4] == b'\xcf\xfa\xed\xfe', 'kernel.macho has a Mach-O 64 magic')
    digest = hashlib.sha256(data).hexdigest()
    print('KERNEL_SHA256', digest)
    ck(digest == EXPECT_KERNEL_SHA256,
       'kernel.macho sha256 matches the recorded build')

    text = data.decode('latin-1')
    for sym in MUST_HAVE:
        ck(sym in text, 'kernel contains %s' % sym)

    # the probe must still be the only executable stage path
    ck('stage3=plan steps=%u' in text, 'the stage-3 plan row format survives')
    ck('stage3=exec' not in text, 'no stage-3 EXECUTE path was compiled in')

    print('---')
    if fails:
        print('CI231_ADRENO_VERIFY: FAIL (%d checks, %d failures)'
              % (checks, len(fails)))
        return 1
    print('CI231_ADRENO_VERIFY: PASS (%d checks, 0 failures)' % checks)
    return 0


if __name__ == '__main__':
    sys.exit(main())
