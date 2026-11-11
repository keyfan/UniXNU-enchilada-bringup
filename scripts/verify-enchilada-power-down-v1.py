#!/usr/bin/env python3
"""Round-59 cross verifier: re-derive the teardown ORDER from the pinned
upstream bytes in PYTHON, and assert the C module agrees.

Method 2 of 2 (method 1 is tests/adreno-power-down-v1-test.cpp). It reads the
sha256-pinned Linux v6.12 a6xx_gmu.c and gdsc.c out of the on-disk cache, walks
the source text of a6xx_gmu_stop() and gdsc.c's constraint comments, and
compares the observed statement order against:
  (a) the header's step enum + STEP_COUNT,
  (b) the .c file's plan emission order,
  (c) the host test's assertions.

Nothing here is a device measurement. No MMIO, no flash, no firmware.
"""
import hashlib
import json
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parent.parent          # .../UniXNU-enchilada
WS = DEV.parent.parent                                # .../oneplus6-ios
GPU = DEV / 'drivers' / 'NativeAdreno'
CACHE = DEV / '.cache' / 'adreno-a630-source'
PINS = DEV / 'scripts' / 'data' / 'enchilada-gpu-power-down-inputs.json'

checks = 0
failures = []


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        failures.append(what)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main():
    if not PINS.exists():
        print('FAIL: missing pin file', PINS)
        return 1
    pins = json.loads(PINS.read_text(encoding='utf-8'))

    # ---- Domain A: the pinned bytes are exactly what the pin file claims ----
    files = pins['linux']['files']
    check(isinstance(files, dict), 'linux.files is a dict keyed by path')
    gmu_bytes = None
    gdsc_bytes = None
    for name, meta in files.items():
        path = CACHE / name
        if not path.exists():
            check(False, 'pinned source present: ' + name)
            continue
        data = path.read_bytes()
        check(len(data) == meta['bytes'],
              'byte length matches pin for ' + name)
        check(sha256(data) == meta['sha256'],
              'sha256 matches pin for ' + name)
        if name.endswith('a6xx_gmu.c'):
            gmu_bytes = data
        if name.endswith('gdsc.c'):
            gdsc_bytes = data

    check(gmu_bytes is not None, 'a6xx_gmu.c is pinned')
    check(gdsc_bytes is not None, 'gdsc.c is pinned')
    if gmu_bytes is None or gdsc_bytes is None:
        return report()

    gmu = gmu_bytes.decode('utf-8', 'replace')
    gdsc = gdsc_bytes.decode('utf-8', 'replace')
    gmu_lines = gmu.splitlines()
    gdsc_lines = gdsc.splitlines()

    # ---- Domain B: a6xx_gmu_stop() physically emits GX, clocks, then CX ----
    stop_start = None
    for i, line in enumerate(gmu_lines):
        if line.startswith('int a6xx_gmu_stop(struct a6xx_gpu *a6xx_gpu)'):
            stop_start = i
            break
    check(stop_start is not None, 'a6xx_gmu_stop() found by signature')
    if stop_start is None:
        return report()

    # Collect the body up to the closing brace at column 0.
    body_lines = []
    for line in gmu_lines[stop_start:]:
        if body_lines and line.startswith('}'):
            break
        body_lines.append(line)
    body = '\n'.join(body_lines)

    i_gate = body.find('!pm_runtime_active(gmu->dev)')
    i_gx = body.find('pm_runtime_put_sync(gmu->gxpd)')
    i_clk = body.find('clk_bulk_disable_unprepare(gmu->nr_clocks')
    i_cx = body.find('pm_runtime_put_sync(gmu->dev)')

    check(i_gate >= 0, 'the :1189 not-active gate is present')
    check(i_gx >= 0, 'gxpd put is present')
    check(i_clk >= 0, 'clk_bulk_disable_unprepare is present')
    check(i_cx >= 0, 'gmu->dev put (CX) is present')

    # THE ORDERING: gate before gx, gx before clocks, clocks before CX.
    check(i_gate < i_gx, 'gate precedes the GX put')
    check(i_gx < i_clk, 'GX put precedes the clock disable')
    check(i_clk < i_cx, 'clock disable precedes the CX put (CX LAST)')

    # The CX-last rule is stated in prose too; both mentions must survive.
    check('Make sure the GX domain is off before turning off the GMU (CX)' in body,
          'the upstream CX-last comment is still in a6xx_gmu_stop')

    constraint = ('Hardware constraints dictate that the GX be powered down '
                  'before the CX')
    check(constraint.replace(' ', '') in gdsc.replace('\n', '').replace(' ', '')
          or 'Hardware constraints dictate that the GX be powered down' in gdsc,
          'gdsc.c still states the GX-before-CX hardware constraint')

    # Rates are NOT undone: no clk_set_rate in the teardown path.
    check('clk_set_rate' not in body,
          'teardown performs no clk_set_rate (rates are not restored)')

    # ---- Domain C: the C module's step enum matches the derived order ------
    hdr = (GPU / 'AdrenoPowerDownV1.h').read_text(encoding='utf-8')
    src = (GPU / 'AdrenoPowerDownV1.c').read_text(encoding='utf-8')
    test = (GPU / 'tests' / 'adreno-power-down-v1-test.cpp').read_text(
        encoding='utf-8')

    check('#define ADRENO_POWER_DOWN_V1_STEP_GX_GENPD_OFF   0u' in hdr,
          'header step 0 is GX off')
    check('#define ADRENO_POWER_DOWN_V1_STEP_CLOCKS_DISABLE 1u' in hdr,
          'header step 1 is clocks disable')
    check('#define ADRENO_POWER_DOWN_V1_STEP_CX_GENPD_OFF   2u' in hdr,
          'header step 2 is CX off')
    check('#define ADRENO_POWER_DOWN_V1_STEP_COUNT          3u' in hdr,
          'header STEP_COUNT is 3')

    # The .c emits the steps in the same order it declared them.
    i_emit_gx = src.find('ops[n].step = APD_STEP_GX_OFF;')
    i_emit_clk = src.find('ops[n].step = APD_STEP_CLKS;')
    i_emit_cx = src.find('ops[n].step = APD_STEP_CX_OFF;')
    check(i_emit_gx >= 0 and i_emit_clk >= 0 and i_emit_cx >= 0,
          'the .c emits all three steps')
    check(i_emit_gx < i_emit_clk < i_emit_cx,
          'the .c emission order is GX -> clocks -> CX')

    # Only GX is guarded, matching IS_ERR_OR_NULL(gmu->gxpd).
    check('return step == APD_STEP_GX_OFF ? 1u : 0u;' in src,
          'StepGuarded guards ONLY the GX step')

    # The :1189 gate is modeled, not assumed.
    check('in->gmu_active != 0u ? 1 : 0' in src,
          'ShouldRun reads the caller-supplied gmu_active flag')
    check('!AdrenoPowerDownV1ShouldRun(in)' in src,
          'Execute honors the not-active gate')

    # No rollback: the teardown bus struct has no genpd_on / clocks_enable
    # MEMBER. Assert on the struct definition (strip comments first, because the
    # header's prose legitimately NAMES the absent callbacks to explain why).
    def strip_comments(text):
        import re
        text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
        text = re.sub(r'//[^\n]*', '', text)
        return text

    hdr_code = strip_comments(hdr)
    src_code = strip_comments(src)

    check('genpd_on' not in hdr_code,
          'the teardown bus struct has NO genpd_on member')
    check('clocks_enable' not in hdr_code,
          'the teardown bus struct has NO clocks_enable member')
    check('set_rate' not in hdr_code,
          'the teardown bus struct has NO set_rate member')
    # The module DOES legitimately write pre->cx_genpd_on / pre->gx_genpd_on /
    # pre->clocks_enabled -- that is how it CLEARS them. What must be absent is
    # a BUS DISPATCH of a power-up callback, i.e. `bus->genpd_on(` etc.
    check('bus->genpd_on' not in src_code,
          'the teardown module never DISPATCHES bus->genpd_on')
    check('bus->clocks_enable' not in src_code,
          'the teardown module never DISPATCHES bus->clocks_enable')
    check('bus->set_rate' not in src_code,
          'the teardown module never DISPATCHES bus->set_rate')
    # ...while it does clear the caller's readiness record.
    check('pre->cx_genpd_on = 0u' in src,
          'Execute clears pre->cx_genpd_on (no readiness after teardown)')
    check('pre->clocks_enabled = 0u' in src,
          'Execute clears pre->clocks_enabled')

    # The CX-unverified finding is re-exported from round 58, not re-invented.
    check('AdrenoPowerUpGdscDisableIsUnverified(0u)' in src,
          'CxOffIsUnverified delegates to the round-58 predicate')

    # The host test asserts the same order position by position.
    check('ops[0].step == ADRENO_POWER_DOWN_V1_STEP_GX_GENPD_OFF' in test,
          'host test asserts position 0 = GX')
    check('ops[1].step == ADRENO_POWER_DOWN_V1_STEP_CLOCKS_DISABLE' in test,
          'host test asserts position 1 = clocks')
    check('ops[2].step == ADRENO_POWER_DOWN_V1_STEP_CX_GENPD_OFF' in test,
          'host test asserts position 2 = CX')

    # ---- Domain D: wiring ----
    build = (DEV / 'scripts' / 'build-enchilada-adreno.py').read_text(
        encoding='utf-8')
    host = (DEV / 'scripts' / 'check-enchilada-adreno-host.py').read_text(
        encoding='utf-8')
    check("'AdrenoPowerDownV1.c'" in build,
          'the build script archives AdrenoPowerDownV1.c')
    check("'AdrenoPowerDownV1.c', ['-mgeneral-regs-only']" in build,
          'the archive unit carries the freestanding flags')
    check("'AdrenoPowerDownV1'" in build,
          'the build report stems include AdrenoPowerDownV1')
    check("'AdrenoPowerDownV1': GPU / 'AdrenoPowerDownV1.c'" in host,
          'the host runner maps the source')
    check("('adreno-power-down-v1-test'" in host,
          'the host runner registers the test')

    # ---- Domain E: honest boundary ----
    check(pins.get('honest_boundary') is not None,
          'the pin file records an honest boundary')
    hb = json.dumps(pins.get('honest_boundary', ''))
    check('host' in hb.lower(), 'the boundary says host-verified')
    check(pins.get('round') == 59, 'the pin file is tagged round 59')

    return report()


def report():
    print('---')
    if failures:
        for f in failures:
            print('FAIL:', f)
        print('POWER_DOWN_V1_CROSS: FAIL (%d checks, %d failures)'
              % (checks, len(failures)))
        return 1
    print('POWER_DOWN_V1_CROSS: PASS (%d checks, 0 failures)' % checks)
    return 0


if __name__ == '__main__':
    sys.exit(main())
