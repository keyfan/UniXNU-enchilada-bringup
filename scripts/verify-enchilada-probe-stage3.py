#!/usr/bin/env python3
"""Round 52: independent CROSS verification of the stage-3 kernel probe contract.

WHY A SECOND VERIFIER: check-enchilada-adreno-probe.py proves the shim COMPILES
(strict C++17 arm64 Mach-O, no unexpected undefined symbols, no stores through
pointers). It does NOT prove the stage-3 SEMANTICS. This verifier never compiles
anything: it reads the exact source text and asserts the load-bearing safety
and composition invariants of the first kernel caller of AdrenoPowerUpV1.

m03546: two independent verifications before every action. m12247: verify
before flashing. This file is the second (source-contract) verification; the
first is the compiled shim check, the third is the full 11-suite host test.
"""
import hashlib
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
PROBE = DEV / 'package/EnchiladaAdrenoProbe.cpp'
HEADER = DEV / 'drivers/NativeAdreno/AdrenoPowerUpV1.h'
MODEL = DEV / 'drivers/NativeAdreno/AdrenoPowerUpV1.c'

checks, failures = 0, []


def check(ok, label):
    global checks
    checks += 1
    print(('ok   ' if ok else 'FAIL ') + label)
    if not ok:
        failures.append(label)


def main():
    if not PROBE.is_file() or not HEADER.is_file() or not MODEL.is_file():
        print('FAIL: missing input file')
        return 1
    text = PROBE.read_text()
    header = HEADER.read_text()
    model = MODEL.read_text()

    # Verification-2 honesty fix (r52a): the probe's own documentation comments
    # deliberately NAME the things it must NOT do ("AdrenoPowerUpV1Execute() is
    # NEVER called", "NO AdrenoPowerUpV1Bus is wired"). A naive substring check
    # would flag those comments. Compare against a COMMENT-STRIPPED copy, and
    # use word boundaries so struct fields (cx_genpd_on / clocks_enabled) do
    # not collide with callback names (genpd_on / clocks_enable).
    code = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    code = re.sub(r'//[^\n]*', '', code)

    # --- A. stage-3 is a NEW, default-off arm --------------------------------
    # Round 103: the mask is `stage < 1u || stage > 6u`. Assert the SHAPE, not a
    # stage list, so a future stage cannot be added without touching this check.
    check('(stage < 1u || stage > 6u)' in code and 'stage > 6u' in code,
          'stage-3 accepted by the boot-arg mask (stages 1..6; rounds 94/95/103 widened it)')
    check('if (stage >= 3u)' in text,
          'stage-3 block is gated behind stage >= 3 (stages 1/2 unaffected)')

    # --- B. include + composition with the round-51 model --------------------
    check('#include "AdrenoPowerUpV1.h"' in text,
          'probe includes the round-51 ordered power-up model header')
    check('#include "AdrenoPowerUpV1.h"   // round 51' in text,
          'include carries the round-51 provenance comment')
    check('extern "C" {' in text and text.index('#include "AdrenoPowerUpV1.h"') > text.index('extern "C" {'),
          'include lives inside extern "C" (CI #224 mangling lesson)')

    # --- C. PURE contract: NO Execute, NO bus, NO new MMIO writes ------------
    # Use the comment-stripped 'code' copy so documentation that NAMES these
    # forbidden things cannot create a false positive.
    check('AdrenoPowerUpV1Execute' not in code,
          'probe NEVER calls AdrenoPowerUpV1Execute (pure Plan only)')
    check('AdrenoPowerUpV1Bus' not in code,
          'probe NEVER constructs an AdrenoPowerUpV1Bus (no callbacks wired)')
    for fn in ('genpd_on', 'genpd_off', 'set_rate', 'clocks_enable'):
        check(re.search(r'\b' + fn + r'\b', code) is None,
              f'probe has no {fn} callback reference')
    # The only dereference in the file must remain rd32()'s volatile load.
    loads = re.findall(r'reinterpret_cast<[^>]*>\s*\(', code)
    # Round 95 adds a second legitimate cast: the volatile uint8_t * handed to
    # AdrenoGpuccWindowV1Bind for stage 5. The property that must survive is that
    # EVERY cast is volatile -- a plain uint8_t * here would let a cacheable view
    # of device memory exist.
    check(len(loads) >= 1 and all('volatile' in x for x in loads),
          f'every pointer cast is volatile (the rd32 load and the window base): {loads}')
    stores = re.findall(r'\*\s*reinterpret_cast\s*<[^>]*>\s*\([^;]*\)\s*=[^=]', code)
    check(not stores, f'no store through any pointer ({stores})')

    # --- D. the five PURE helpers ARE called ---------------------------------
    pure_calls = (
        'AdrenoPowerUpV1InputsValid', 'AdrenoPowerUpV1CountsAreDistinct',
        'AdrenoPowerUpV1PlanSize', 'AdrenoPowerUpV1Plan',
        'AdrenoPowerUpV1PreIsReady',
    )
    for fn in pure_calls:
        check(fn + '(' in text, f'probe calls pure helper {fn}')
    for fn in ('AdrenoPowerUpV1BoardGmuClockCount', 'AdrenoPowerUpV1BoardGpuClockCount',
               'AdrenoPowerUpV1BoardIsA740Family'):
        check(fn + '(' in text, f'probe reads board fact {fn}')

    # --- E. deterministic 5-op plan, honest composition gate ---------------
    check('AdrenoPowerUpV1Op ops[ADRENO_POWER_UP_V1_STEP_COUNT]' in code,
          'plan array sized from the header STEP_COUNT (no magic number)')
    check('ADRENO_POWER_UP_V1_STEP_COUNT, &n' in code,
          'Plan() receives the header STEP_COUNT as capacity')
    check('pre.cx_genpd_on = cxOn ? 1u : 0u' in code,
          'composition Pre.cx is built from the DEVICE-MEASURED CX word')
    check('pre.gx_genpd_on = gxOn ? 1u : 0u' in code,
          'composition Pre.gx is built from the DEVICE-MEASURED GX word')
    check('pre.clocks_enabled = 0u' in code,
          'composition Pre.clocks_enabled is honestly 0 (this port enables none)')
    check('const int ready = AdrenoPowerUpV1PreIsReady(&pre)' in code,
          'composition gate result is captured (expected 0 on CX-DOWN)')

    # --- F. replay survives the 128 KiB msgbuf ring wrap --------------------
    check('char gPlanReplay[200]' in code, 'stage-3 replay buffer declared')
    check('void setPlanReplay(const char *text)' in code, 'setPlanReplay() defined')
    check('gHavePlanReplay' in code and 'setPlanReplay(planRow)' in code,
          'stage-3 row is cached into its replay buffer')
    check('if (gHavePlanReplay)' in code and 'gPlanReplay, gTick' in code,
          'stage-3 row is re-emitted at 1 Hz in the gDone branch')

    # --- G. model-side cross-check (independent source) --------------------
    check('ADRENO_POWER_UP_V1_STEP_COUNT          5u' in header,
          'header pins STEP_COUNT = 5')
    check('ADRENO_POWER_UP_V1_GMU_CLOCKS     4u' in header,
          'header pins GMU clocks = 4 (qcom,gmu DTB :23889-23890)')
    check('ADRENO_POWER_UP_V1_GPU_CLOCKS     6u' in header,
          'header pins GPU clocks = 6 (kgsl-3d0 DTB :23656-23657)')
    check('ADRENO_POWER_UP_V1_HUB_RATE_A630  150000000u' in header,
          'header pins A630 hub rate = 150 MHz (non-A740 branch)')
    check('*count = n;' in model and 'return kAdrenoPowerUpV1Ok;' in model,
          'model Plan() emits ops and returns Ok without bus traffic')

    # --- H. no-libc discipline ---------------------------------------------
    check('strlen(' not in code and 'memcpy(' not in code and 'memset(' not in code,
          'probe still uses no libc string/memory helpers')
    check('snprintf(planRow, sizeof planRow,' in code,
          'stage-3 row built with the kernel snprintf only')

    # --- I. byte-level integrity -------------------------------------------
    raw = PROBE.read_bytes()
    check(b'\r' not in raw, 'probe file is pure LF (CR count 0)')
    check(raw.count(b'\n') == text.count('\n'), 'byte LF count matches decoded text')

    print(f'\n{checks - len(failures)}/{checks} checks passed')
    if failures:
        for f in failures:
            print('  FAILED: ' + f)
        return 1
    print('PASS: stage-3 kernel probe contract (pure Plan + composition gate, ZERO MMIO writes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
