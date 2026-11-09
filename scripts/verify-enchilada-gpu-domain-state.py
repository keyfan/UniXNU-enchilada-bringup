#!/usr/bin/env python3
"""Verification 2 of 2 for the enchilada GPU domain-state probe design.

Method: CROSS, derived backwards from the shipped C header. Independent from
extract-enchilada-gpu-domain-state.py in both parser and direction:

  * it parses drivers/NativeAdreno/AdrenoDomainState.h #defines,
  * it re-reads the DTB and the two pinned sources itself,
  * and it asserts the three domains AGREE: board DTB <-> C header <-> upstream
    clock/gdsc source,
  * plus a structural honesty check that the module contains no MMIO path.

  python -X utf8 scripts/verify-enchilada-gpu-domain-state.py
"""
import hashlib
import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
W = SCRIPTS.parents[2]
M = json.loads((SCRIPTS / 'data/enchilada-gpu-domain-state.json').read_text(encoding='utf-8'))
HDR = DEV / 'drivers/NativeAdreno/AdrenoDomainState.h'
SRC = DEV / 'drivers/NativeAdreno/AdrenoDomainState.c'
TEST = DEV / 'drivers/NativeAdreno/tests/adreno-domain-state-test.cpp'
DTB = W / M['dtb']['path']
CACHE = DEV / M['upstream']['cache_dir']

CHECKS = []


def need(ok, label, detail=''):
    CHECKS.append((bool(ok), label, detail))
    if not ok:
        print(f'FAIL {label}: {detail}')


def define(text, name):
    m = re.search(r'#define\s+' + re.escape(name) + r'\s+'
                  r'(0x[0-9a-fA-F]+|[0-9]+)(?:ull|u|U)?\b', text)
    if not m:
        return None
    tok = m.group(1)
    return int(tok, 16) if tok.lower().startswith('0x') else int(tok, 10)


def block(lines, a, b):
    return '\n'.join(lines[a - 1:b])


def hexints(text, prop):
    out = []
    for m in re.finditer(r'(?:^|\s)' + re.escape(prop) + r'\s*=\s*<([^>]*)>', text, re.M):
        for tok in m.group(1).split():
            try:
                out.append(int(tok, 16))
            except ValueError:
                pass
    return out


def main():
    for p, label in ((HDR, 'header'), (SRC, 'source'), (TEST, 'test'), (DTB, 'pinned DTB')):
        need(p.is_file(), f'{label} exists', str(p))
    if any(not p.is_file() for p in (HDR, SRC, TEST, DTB)):
        return finish()

    htext = HDR.read_text(encoding='utf-8')
    stext = SRC.read_text(encoding='utf-8')
    ttext = TEST.read_text(encoding='utf-8')
    lines = DTB.read_text(encoding='utf-8', errors='replace').splitlines()
    dtb_bytes = DTB.read_bytes()

    need(hashlib.sha256(dtb_bytes).hexdigest() == M['dtb']['sha256'],
         'DTB sha256 still matches the pin')

    # ---------- domain A: the C header ----------
    wanted = {
        'ADRENO_DOMAIN_GPUCC_BASE': int(M['gpucc_window']['base'], 16),
        'ADRENO_DOMAIN_GPUCC_SIZE': int(M['gpucc_window']['size'], 16),
        'ADRENO_DOMAIN_GPUCC_MAX_REGISTER': int(M['gpucc_window']['max_register'], 16),
        'ADRENO_DOMAIN_GPUCC_REG_STRIDE': M['gpucc_window']['reg_stride'],
        'ADRENO_DOMAIN_CX_GDSCR': int(M['domain_state']['cx']['gdscr'], 16),
        'ADRENO_DOMAIN_GX_GDSCR': int(M['domain_state']['gx']['gdscr'], 16),
        'ADRENO_DOMAIN_CX_STATUS': int(M['domain_state']['cx']['status_register'], 16),
        'ADRENO_DOMAIN_GX_STATUS': int(M['domain_state']['gx']['status_register'], 16),
        'ADRENO_DOMAIN_CX_GDS_HW_CTRL': int(M['domain_state']['cx']['gdscr'], 16) - int(M['gpucc_window']['base'], 16) + 0x1540 - 0x106c,
        'ADRENO_DOMAIN_CFG_GDSCR_OFFSET': 0x4,
        'ADRENO_DOMAIN_CX_ON_BIT': M['domain_state']['cx']['on_bit'],
        'ADRENO_DOMAIN_GX_ON_BIT': M['domain_state']['gx']['on_bit'],
        'ADRENO_DOMAIN_GX_OFF_BIT': M['domain_state']['gx']['off_bit'],
        'ADRENO_DOMAIN_FORBIDDEN_COUNT': len(M['forbidden_windows']),
    }
    for name, want in wanted.items():
        got = define(htext, name)
        need(got == want, f'header {name} == {want}', f'got {got}')

    # ---------- domain B: the vendor DTB ----------
    gpucc_dtb = block(lines, *M['dtb']['slice_gpucc'])
    cx_dtb = block(lines, *M['dtb']['slice_gdsc_cx'])
    gx_dtb = block(lines, *M['dtb']['slice_gdsc_gx'])
    need(hexints(gpucc_dtb, 'reg') == [0x5090000, 0x9000], 'DTB GPUCC reg',
         str(hexints(gpucc_dtb, 'reg')))
    need(hexints(cx_dtb, 'reg')[:1] == [0x509106c], 'DTB CX gdsc reg',
         str(hexints(cx_dtb, 'reg')))
    need(hexints(gx_dtb, 'reg')[:1] == [0x509100c], 'DTB GX gdsc reg',
         str(hexints(gx_dtb, 'reg')))

    # header values must equal what the DTB declares (+/- the documented offset)
    gpucc_base = define(htext, 'ADRENO_DOMAIN_GPUCC_BASE')
    need(define(htext, 'ADRENO_DOMAIN_CX_GDSCR') == hexints(cx_dtb, 'reg')[0],
         'header CX gdscr == DTB cx reg')
    need(define(htext, 'ADRENO_DOMAIN_GX_GDSCR') == hexints(gx_dtb, 'reg')[0],
         'header GX gdscr == DTB gx reg')
    need(define(htext, 'ADRENO_DOMAIN_GPUCC_BASE') == 0x5090000,
         'header GPUCC base == the node unit address')

    # ---------- domain C: sha-pinned upstream ----------
    gpath = CACHE / 'drivers/clk/qcom/gpucc-sdm845.c'
    dpath = CACHE / 'drivers/clk/qcom/gdsc.c'
    hpath = CACHE / 'drivers/clk/qcom/gdsc.h'
    gtext = dtext = xtext = ''
    for path, spec, var in (
        (gpath, M['upstream']['files']['drivers/clk/qcom/gpucc-sdm845.c'], 'gpucc'),
        (dpath, M['upstream']['files']['drivers/clk/qcom/gdsc.c'], 'gdsc'),
    ):
        need(path.is_file(), f'pinned {var} source present', str(path))
        if path.is_file():
            data = path.read_bytes()
            need(len(data) == spec['bytes'], f'{var} bytes', f'got {len(data)}')
            got = hashlib.sha256(data).hexdigest()
            need(got == spec['sha256'], f'{var} sha256', got[:16])
            txt = data.decode('utf-8', 'replace')
            for marker in spec['required_markers']:
                need(marker in txt, f'{var} marker present: {marker}', '')
            if var == 'gpucc':
                gtext = txt
            else:
                dtext = txt
    if hpath.is_file():
        spec = M['upstream']['files']['drivers/clk/qcom/gdsc.h']
        data = hpath.read_bytes()
        need(len(data) == spec['bytes'], 'gdsc.h bytes', f'got {len(data)}')
        got = hashlib.sha256(data).hexdigest()
        need(got == spec['sha256'], 'gdsc.h sha256', got[:16])
        xtext = data.decode('utf-8', 'replace')
        for marker in spec['required_markers']:
            need(marker in xtext, f'gdsc.h marker present: {marker}', '')
    else:
        need(False, 'pinned gdsc.h present', str(hpath))

    # the three domains must agree on the register decode
    if gtext and dtext:
        # cx: .gdscr = 0x106c, .gds_hw_ctrl = 0x1540 -> status = 0x1540
        need('.gdscr = 0x106c' in gtext, 'upstream CX gdscr is 0x106c', '')
        need('.gds_hw_ctrl = 0x1540' in gtext, 'upstream CX gds_hw_ctrl is 0x1540', '')
        need('reg = sc->gds_hw_ctrl;' in dtext, 'upstream prefers gds_hw_ctrl for CX', '')
        need(define(htext, 'ADRENO_DOMAIN_CX_STATUS')
             == gpucc_base + 0x1540,
             'header CX_STATUS == GPUCC + 0x1540 (agrees with gds_hw_ctrl)', '')
        need(define(htext, 'ADRENO_DOMAIN_CX_STATUS')
             != define(htext, 'ADRENO_DOMAIN_CX_GDSCR'),
             'header does not mistake the control register for the status register', '')

        # gx: POLL_CFG_GDSCR -> gdscr + 0x4
        need('.gdscr = 0x100c' in gtext, 'upstream GX gdscr is 0x100c', '')
        need('POLL_CFG_GDSCR' in gtext, 'upstream GX sets POLL_CFG_GDSCR', '')
        need(define(htext, 'ADRENO_DOMAIN_GX_STATUS')
             == gpucc_base + 0x100c + 0x4,
             'header GX_STATUS == GPUCC + 0x100c + CFG_GDSCR_OFFSET', '')
        need('#define CFG_GDSCR_OFFSET\t\t0x4' in dtext,
             'upstream CFG_GDSCR_OFFSET is literally 0x4', '')

        # bit semantics
        need('#define PWR_ON_MASK\t\tBIT(31)' in dtext, 'upstream PWR_ON_MASK is BIT(31)', '')
        need(define(htext, 'ADRENO_DOMAIN_CX_ON_BIT') == 31, 'header CX on-bit is 31', '')
        need('#define GDSC_POWER_UP_COMPLETE\t\tBIT(16)' in dtext,
             'upstream GDSC_POWER_UP_COMPLETE is BIT(16)', '')
        need('#define GDSC_POWER_DOWN_COMPLETE\tBIT(15)' in dtext,
             'upstream GDSC_POWER_DOWN_COMPLETE is BIT(15)', '')
        need(define(htext, 'ADRENO_DOMAIN_GX_ON_BIT') == 16, 'header GX on-bit is 16', '')
        need(define(htext, 'ADRENO_DOMAIN_GX_OFF_BIT') == 15, 'header GX off-bit is 15', '')
        # gdsc_poll_status proves the register is readable while transitioning
        need('gdsc_poll_status' in dtext and 'STATUS_POLL_TIMEOUT_US' in dtext,
             'upstream polls the status register during a transition (readable when collapsed)', '')

    # ---------- forbidden windows agree across JSON / C / DTB ----------
    gpu_dtb = block(lines, *M['dtb']['slice_gpu'])
    need(hexints(gpu_dtb, 'reg')[:2] == [0x5000000, 0x40000], 'DTB kgsl-3d0 reg',
         str(hexints(gpu_dtb, 'reg')[:2]))
    for w in M['forbidden_windows']:
        base = int(w['base'], 16)
        need(f'0x{base:08x}ull' in stext, f'C source records forbidden base {w["base"]}', '')
        token = f'0x{base:x}'
        need(token in dtb_bytes.decode('utf-8', 'replace'),
             f'DTB contains forbidden base {w["base"]}', '')
    need(define(htext, 'ADRENO_DOMAIN_FORBIDDEN_COUNT') == len(M['forbidden_windows']),
         'header forbidden count == JSON window list', '')

    # The GPUCC carve-out must exist in the C, not just the test.
    need('GPUCC carve-out' in stext, 'C source documents the GPUCC carve-out', '')
    need('adreno_domain_window_is_forbidden' in stext, 'C source implements the forbidden test', '')

    # ---------- honesty: no MMIO anywhere in the module ----------
    banned = ('armcpu_map_physical', 'mmio_read', 'volatile uint32_t *', 'IOMemoryMap',
              'IOService', 'ml_io_read')
    for name, txt in (('header', htext), ('source', stext), ('test', ttext)):
        for token in banned:
            need(token not in txt, f'{name} contains no MMIO/platform symbol: {token}', '')

    # ---------- the tests must actually assert the correction ----------
    need('testCxStatusRegisterCorrection' in ttext,
         'host test guards the CX status-register correction', '')
    need('testGpuccCarveOut' in ttext,
         'host test guards the GPUCC carve-out', '')
    need('ADRENO_DOMAIN_CX_STATUS != ADRENO_DOMAIN_CX_GDSCR' in ttext,
         'host test asserts CX status differs from the DTB control register', '')

    # ---------- the design must stay host-verified ----------
    need(M['status'] == 'host-verified', 'manifest status stays host-verified',
         M['status'])
    need(len(M['still_unsafe_without_device_evidence']) >= 3,
         'manifest keeps its honest limits', str(len(M['still_unsafe_without_device_evidence'])))

    return finish()


def finish():
    total = len(CHECKS)
    bad = sum(1 for ok, _, _ in CHECKS if not ok)
    if bad:
        print(f'ENCHILADA_GPU_DOMAIN_STATE_CROSS: FAIL ({bad} of {total} checks)')
        return 1
    print(f'ENCHILADA_GPU_DOMAIN_STATE_CROSS: PASS ({total} checks, '
          f'DTB <-> C header <-> upstream gdsc source agree)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
