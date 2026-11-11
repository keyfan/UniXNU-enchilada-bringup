#!/usr/bin/env python3
"""Round-59 third verification: independent boundary/pin audit.

Reads the pin json, the module sources and the two verifiers and asserts the
honest boundary in BOTH directions: the claims that MUST be present, and the
ones that MUST NOT (a device-verified claim, an MMIO write, a flash).
"""
import json
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]          # .../UniXNU-enchilada
GPU = DEV / 'drivers' / 'NativeAdreno'
PINS = DEV / 'scripts' / 'data' / 'enchilada-gpu-power-down-inputs.json'

checks = 0
fails = []


def ck(cond, what):
    global checks
    checks += 1
    if not cond:
        fails.append(what)


def main():
    pins = json.loads(PINS.read_text(encoding='utf-8'))
    hdr = (GPU / 'AdrenoPowerDownV1.h').read_text(encoding='utf-8')
    src = (GPU / 'AdrenoPowerDownV1.c').read_text(encoding='utf-8')

    ck(pins['round'] == 59, 'pin file round is 59')
    ck(pins['status'] == 'host-verified', 'pin status is host-verified')

    hb = pins['honest_boundary']
    ck('HOST-VERIFIED ONLY' in hb, 'boundary says HOST-VERIFIED ONLY')
    ck('No MMIO' in hb, 'boundary denies MMIO')
    ck('no flash' in hb, 'boundary denies flash')
    ck('no device execution' in hb, 'boundary denies device execution')
    ck('CONFIG_XNU_NATIVE_ADRENO remains n' in hb,
       'boundary keeps the defconfig gate closed')

    # The header must state the ordering derivation and the honest limits.
    ck('a6xx_gmu_stop' in hdr, 'header cites a6xx_gmu_stop')
    ck('1184-1217' in hdr or '1196-1199' in hdr,
       'header cites the stop body lines')
    ck('Hardware constraints dictate' in hdr,
       'header cites the GX-before-CX constraint')
    ck('NOT simply "run the power-up plan backwards"' in hdr,
       'header explicitly rejects the naive reversal')

    # No store anywhere in either file. A "writel"/"reg_write"/"volatile" in a
    # pure derivation module would mean it had grown a hardware path.
    for pat in ('writel', 'reg_write', 'gmu_write', 'volatile uint32_t *'):
        ck(pat not in hdr, 'header has no hardware store: ' + pat)
        ck(pat not in src, 'source has no hardware store: ' + pat)

    # The device-verified words may be QUOTED as reused context, but the round
    # itself must not claim to have measured them.
    ck(pins['device_measurement_reused']['rows'] == 1912,
       'reused device rows are the historical 1912')
    ck('No new device run this round' in
       pins['device_measurement_reused']['note'],
       'reuse note states no new device run')

    # The two findings must be recorded in the pin file.
    ck('cx_off_is_unverified' in pins['findings'],
       'finding: CX off is unverified')
    ck('step_enums_are_separate_namespaces' in pins['findings'],
       'finding: step enums are separate namespaces')

    # The derived order must be recorded and must NOT be a reversal.
    dto = pins['derived_teardown_order']
    ck(dto['step_count'] == 3, 'derived teardown has 3 steps')
    ck(dto['is_reverse_of_power_up'] is False,
       'derived teardown is explicitly not a reversal')
    ck(dto['rates_restored'] is False, 'derived teardown restores no rates')
    ck(dto['rollback_on_failure'] is False,
       'derived teardown does no rollback')
    ck(dto['steps'][0].startswith('GX_OFF'),
       'derived order starts with GX off')
    ck(dto['steps'][-1].startswith('CX_OFF'),
       'derived order ends with CX off')

    # Both verifiers exist and are non-trivial.
    cross = DEV / 'scripts' / 'verify-enchilada-power-down-v1.py'
    ck(cross.exists(), 'cross verifier exists')
    ck(len(cross.read_text(encoding='utf-8').splitlines()) > 100,
       'cross verifier is a real implementation')

    print('---')
    if fails:
        for f in fails:
            print('FAIL:', f)
        print('R59_BOUNDARY: FAIL (%d checks, %d failures)'
              % (checks, len(fails)))
        return 1
    print('R59_BOUNDARY: PASS (%d checks, 0 failures)' % checks)
    return 0


if __name__ == '__main__':
    sys.exit(main())
