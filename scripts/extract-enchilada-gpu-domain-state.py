#!/usr/bin/env python3
"""Verification 1 of 2 for the enchilada GPU domain-state probe design.

Method: EXTRACT. Re-derive every board fact straight from the sha-pinned vendor
DTB and every hardware semantics fact from the sha-pinned Linux v6.12 clock/gdsc
sources, then compare against scripts/data/enchilada-gpu-domain-state.json.
Independent parser from verify-enchilada-gpu-domain-state.py: this one works from
the JSON claim set forward, the other works from the C header backwards.

  python -X utf8 scripts/extract-enchilada-gpu-domain-state.py
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
DTB = W / M['dtb']['path']
CACHE = DEV / M['upstream']['cache_dir']

CHECKS = []


def need(ok, label, detail=''):
    CHECKS.append((bool(ok), label, detail))
    if not ok:
        print(f'FAIL {label}: {detail}')


def lines_of(path):
    return path.read_text(encoding='utf-8', errors='replace').splitlines()


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


def strings(text, prop):
    vals = []
    for m in re.finditer(r'(?:^|\s)' + re.escape(prop) + r'\s*=\s*"([^"]*)"', text, re.M):
        vals.extend(v for v in m.group(1).split('\\0') if v)
    return vals


def main():
    need(DTB.is_file(), 'pinned DTB exists', str(DTB))
    if not DTB.is_file():
        return finish()
    raw = DTB.read_bytes()
    need(len(raw) == M['dtb']['bytes'], 'DTB byte length matches the pin',
         f"got {len(raw)} want {M['dtb']['bytes']}")
    digest = hashlib.sha256(raw).hexdigest()
    need(digest == M['dtb']['sha256'], 'DTB sha256 matches the pin', digest[:16])

    lines = lines_of(DTB)

    # ---- GPUCC / GFXCC rails ------------------------------------------------
    gpucc = block(lines, *M['dtb']['slice_gpucc'])
    gfxcc = block(lines, *M['dtb']['slice_gfxcc'])
    need('qcom,gpucc@5090000' in gpucc, 'GPUCC node present')
    need(hexints(gpucc, 'reg') == [0x5090000, 0x9000],
         'GPUCC reg == 0x5090000+0x9000', str(hexints(gpucc, 'reg')))
    vdd_cx = hexints(gpucc, 'vdd_cx-supply')
    vdd_mx = hexints(gpucc, 'vdd_mx-supply')
    vdd_gfx = hexints(gfxcc, 'vdd_gfx-supply')
    need(hexints(gfxcc, 'reg') == [0x5090000, 0x9000],
         'GFXCC reg == 0x5090000+0x9000', str(hexints(gfxcc, 'reg')))
    need(vdd_cx == [0x1b], 'GPUCC vdd_cx-supply == 0x1b', str(vdd_cx))
    need(vdd_mx == [0x8d], 'GPUCC vdd_mx-supply == 0x8d', str(vdd_mx))
    need(vdd_gfx == [0x1d], 'GFXCC vdd_gfx-supply == 0x1d', str(vdd_gfx))

    # ---- the PMIC rails those phandles name --------------------------------
    for key, spec in M['phandles'].items():
        want_line = spec['line']
        actual = lines[want_line - 1].strip()
        need(actual.startswith('regulator-name'),
             f'{key} line {want_line} is a regulator-name', actual)
        need(spec['phandle'].strip('0x') in ''.join(
            re.findall(r'phandle\s*=\s*<(0x[0-9a-fA-F]+)>', block(lines, want_line - 6, want_line + 8))),
            f'{key} phandle {spec["phandle"]} found near line {want_line}')

    # ---- GDSC nodes ---------------------------------------------------------
    cx = block(lines, *M['dtb']['slice_gdsc_cx'])
    gx = block(lines, *M['dtb']['slice_gdsc_gx'])
    need(strings(cx, 'regulator-name') == ['gpu_cx_gdsc'], 'CX GDSC name', str(strings(cx, 'regulator-name')))
    need(hexints(cx, 'reg') == [0x509106c, 0x04], 'CX GDSC reg == 0x509106c+0x04', str(hexints(cx, 'reg')))
    need(hexints(cx, 'hw-ctrl-addr') == [0x1a], 'CX hw-ctrl-addr is the vendor index 0x1a',
         str(hexints(cx, 'hw-ctrl-addr')))
    need('qcom,poll-cfg-gdscr' not in cx, 'CX GDSC does NOT set poll-cfg-gdscr')
    need(strings(gx, 'regulator-name') == ['gpu_gx_gdsc'], 'GX GDSC name', str(strings(gx, 'regulator-name')))
    need(hexints(gx, 'reg') == [0x509100c, 0x04], 'GX GDSC reg == 0x509100c+0x04', str(hexints(gx, 'reg')))
    need('qcom,poll-cfg-gdscr' in gx, 'GX GDSC DOES set poll-cfg-gdscr')

    # ---- upstream gpucc-sdm845.c: the register decode ----------------------
    gpucc_src = M['upstream']['files']['drivers/clk/qcom/gpucc-sdm845.c']
    path = CACHE / 'drivers/clk/qcom/gpucc-sdm845.c'
    need(path.is_file(), 'gpucc-sdm845.c is pinned', str(path))
    if path.is_file():
        data = path.read_bytes()
        need(len(data) == gpucc_src['bytes'], 'gpucc-sdm845.c bytes', f'got {len(data)}')
        got = hashlib.sha256(data).hexdigest()
        need(got == gpucc_src['sha256'], 'gpucc-sdm845.c sha256', got[:16])
        text = data.decode('utf-8', 'replace')
        for marker in gpucc_src['required_markers']:
            need(marker in text, f'gpucc marker present: {marker}', '')

    # ---- upstream gdsc.c: which register is polled -------------------------
    gdsc_src = M['upstream']['files']['drivers/clk/qcom/gdsc.c']
    gpath = CACHE / 'drivers/clk/qcom/gdsc.c'
    need(gpath.is_file(), 'gdsc.c is pinned', str(gpath))
    gd_text = ''
    if gpath.is_file():
        gdata = gpath.read_bytes()
        need(len(gdata) == gdsc_src['bytes'], 'gdsc.c bytes', f'got {len(gdata)}')
        got = hashlib.sha256(gdata).hexdigest()
        need(got == gdsc_src['sha256'], 'gdsc.c sha256', got[:16])
        gd_text = gdata.decode('utf-8', 'replace')
        for marker in gdsc_src['required_markers']:
            need(marker in gd_text, f'gdsc.c marker present: {marker}', '')

    # ---- derived register addresses ---------------------------------------
    gpucc_base = M['gpucc_window']['base']
    gpucc_base = int(gpucc_base, 16)
    for side in ('cx', 'gx'):
        st = M['domain_state'][side]
        need(int(st['gdscr'], 16) == gpucc_base + (0x106c if side == 'cx' else 0x100c),
             f'{side} gdscr == GPUCC + {"0x106c" if side == "cx" else "0x100c"}', st['gdscr'])
        need(int(st['status_register'], 16) - gpucc_base
             == (0x1540 if side == 'cx' else 0x1010),
             f'{side} status register offset is {"0x1540" if side == "cx" else "0x1010"}',
             st['status_register'])
        inside = gpucc_base <= int(st['status_register'], 16) < gpucc_base + int(M['gpucc_window']['size'], 16)
        need(inside, f'{side} status register lies inside the GPUCC window', st['status_register'])

    # ---- the correction, stated as a check --------------------------------
    need(int(M['domain_state']['cx']['status_register'], 16)
         != int(M['domain_state']['cx']['gdscr'], 16),
         'CX status register is NOT the DTB reg value (the round-28 correction)')

    # ---- forbidden windows vs the DTB regs --------------------------------
    gpu = block(lines, *M['dtb']['slice_gpu'])
    need(hexints(gpu, 'reg')[:2] == [0x5000000, 0x40000], 'kgsl-3d0 reg[0]',
         str(hexints(gpu, 'reg')[:2]))
    kgsl = block(lines, *M['dtb']['slice_kgsl_smmu'])
    need(hexints(kgsl, 'reg') == [0x5040000, 0x10000], 'kgsl SMMU reg',
         str(hexints(kgsl, 'reg')))
    need(hexints(kgsl, 'vdd-supply') == [0x1a4], 'kgsl SMMU shares the gpu_cx rail',
         str(hexints(kgsl, 'vdd-supply')))
    for w in M['forbidden_windows']:
        need(int(w['base'], 16) != gpucc_base, f'forbidden window {w["name"]} is not GPUCC', w['base'])

    # Every declared forbidden base must be findable in the DTB text.
    dtb_text = raw.decode('utf-8', 'replace')
    for w in M['forbidden_windows']:
        token = f"0x{int(w['base'], 16):x}"
        need(token in dtb_text, f'forbidden window {w["name"]} base {token} appears in the DTB')
    return finish()


def finish():
    total = len(CHECKS)
    bad = sum(1 for ok, _, _ in CHECKS if not ok)
    if bad:
        print(f'ENCHILADA_GPU_DOMAIN_STATE_EXTRACT: FAIL ({bad} of {total} checks)')
        return 1
    print(f'ENCHILADA_GPU_DOMAIN_STATE_EXTRACT: PASS ({total} checks, DTB sha '
          f'{M["dtb"]["sha256"][:12]}...)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
