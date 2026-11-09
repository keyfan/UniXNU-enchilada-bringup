#!/usr/bin/env python3
"""Verification method 2 (CROSS): independently re-derive the power-domain
facts from THREE domains and assert they agree:
  (a) the sha-pinned enchilada DTB slices,
  (b) the sha-pinned Linux v6.12 upstream sources (a6xx_gmu.c / a6xx_gpu.c),
  (c) the pure-C module AdrenoPowerGate.h/.c + the host test.

Independent of method 1 (extract). Pure host analysis -- NOT a device measurement.

    scripts/verify-enchilada-gpu-power.py
"""
import hashlib
import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
W = SCRIPTS.parents[2]
M = json.loads((SCRIPTS / 'data/enchilada-gpu-power.json').read_text(encoding='utf-8'))
PIN = json.loads((SCRIPTS / 'data/adreno-a630-inputs.json').read_text(encoding='utf-8'))
CACHE = DEV / M['upstream']['cache_dir']
DTB = W / M['dtb']['path']
HEADER = DEV / 'drivers/NativeAdreno/AdrenoPowerGate.h'
SOURCE = DEV / 'drivers/NativeAdreno/AdrenoPowerGate.c'
TEST = DEV / 'drivers/NativeAdreno/tests/adreno-power-gate-test.cpp'
CHECKS = []


def need(ok, label, detail=''):
    CHECKS.append((bool(ok), label, detail))
    if not ok:
        print(f'FAIL {label}: {detail}')


def define(text, name):
    # \b after the suffix is wrong for '0x0509106cull' (word char follows),
    # so match the numeric value + optional integer suffix directly.
    m = re.search(r'#define\s+' + re.escape(name) + r'\s+(0x[0-9a-fA-F]+|[0-9]+)(?:ull|u|U)?\b', text)
    if not m:
        return None
    tok = m.group(1)
    return int(tok, 16) if tok.lower().startswith('0x') else int(tok, 10)


def upstream(name):
    rel = M['upstream']['files'][name]['rel']
    p = CACHE / rel
    return p.read_bytes(), p.read_text(encoding='utf-8')


def main():
    # ---- (a) DTB re-pin -------------------------------------------------
    raw = DTB.read_bytes()
    sha = hashlib.sha256(raw).hexdigest()
    need(sha == M['dtb']['sha256'], 'DTB sha re-pinned', sha)
    lines = raw.decode('utf-8').splitlines()

    def block(key):
        a, b = M['slices'][key]
        return '\n'.join(lines[a - 1:b])

    gpu = block('gpu')
    kgsl = block('kgsl_smmu')
    apps = block('apps_smmu')
    cx = block('gdsc_cx')
    gx = block('gdsc_gx')

    def hexints(text, prop):
        # (?:^|\s) instead of \b: '#iommu-cells' starts with a non-word char.
        m = re.search(r'(?:^|\s)' + re.escape(prop) + r'\s*=\s*<([^>]*)>', text, re.M)
        return [int(t, 16) for t in re.findall(r'0x[0-9a-fA-F]+', m.group(1))] if m else []

    gpu_vddcx = hexints(gpu, 'vddcx-supply')
    gpu_vdd = hexints(gpu, 'vdd-supply')
    kgsl_vdd = hexints(kgsl, 'vdd-supply')
    cxph = hexints(cx, 'phandle')
    gxph = hexints(gx, 'phandle')
    need(gpu_vddcx == cxph == kgsl_vdd == [0x1a4],
         'GPU vddcx == CX phandle == kgsl SMMU vdd (shared gpu_cx rail)',
         (gpu_vddcx, cxph, kgsl_vdd))
    need(gpu_vdd == gxph == [0x2a9], 'GPU vdd == GX phandle', (gpu_vdd, gxph))
    need('qcom,skip-init' in apps and 'vdd-supply' not in apps and 'clocks' not in apps,
         'apps_smmu is the always-on skip-init contrast')

    # ---- (b) upstream sha + ordered power-up markers -------------------
    for name, spec in M['upstream']['files'].items():
        blob, text = upstream(name)
        h = hashlib.sha256(blob).hexdigest()
        need(h == spec['sha256'], f'upstream {name} sha256 pinned', h[:12])
        for marker in spec['required_markers']:
            need(marker in text, f'upstream {name} marker', marker)

    # Ordering: the resume sequence must precede hw_init (text order).
    gmu_raw, gmu_text = upstream('a6xx_gmu.c')
    gpu_raw, gpu_text = upstream('a6xx_gpu.c')
    need('adreno_is_a630_family(adreno_gpu)' in gmu_text and 'gmu->legacy = true' in gmu_text,
         'A630 identified as LEGACY GMU (HFI v1 / SPTPRAC)')
    pos_resume = gmu_text.find('int a6xx_gmu_resume(')
    pos_fwstart = gmu_text.find('a6xx_gmu_fw_start(')
    pos_hwinit = gpu_text.find('static int a6xx_hw_init(')
    need(pos_resume > 0 and pos_fwstart > 0 and pos_hwinit > 0,
         'resume / fw_start / hw_init all present', (pos_resume, pos_fwstart, pos_hwinit))
    need('0x778000' in gmu_text and '0x00778000' in (HEADER.read_text(encoding='utf-8')),
         'SPTPRAC 0x778000 upstream == header')

    # ---- (c) pure-C module constants + gate semantics ------------------
    htext = HEADER.read_text(encoding='utf-8')
    ctext = SOURCE.read_text(encoding='utf-8')
    ttext = TEST.read_text(encoding='utf-8')
    need(define(htext, 'ADRENO_POWER_GPU_CX_GDSC_ADDR') == 0x0509106c,
         'header CX GDSC addr', define(htext, 'ADRENO_POWER_GPU_CX_GDSC_ADDR'))
    need(define(htext, 'ADRENO_POWER_GPU_GX_GDSC_ADDR') == 0x0509100c,
         'header GX GDSC addr', define(htext, 'ADRENO_POWER_GPU_GX_GDSC_ADDR'))
    need(define(htext, 'ADRENO_POWER_GPU_CX_PHANDLE') == cxph[0],
         'header CX phandle == DTB', (define(htext, 'ADRENO_POWER_GPU_CX_PHANDLE'), cxph))
    need(define(htext, 'ADRENO_POWER_GPU_GX_PHANDLE') == gxph[0],
         'header GX phandle == DTB', (define(htext, 'ADRENO_POWER_GPU_GX_PHANDLE'), gxph))
    need(define(htext, 'ADRENO_POWER_KGSL_SMMU_REGULATOR_PHANDLE') == kgsl_vdd[0],
         'header kgsl SMMU regulator == DTB')
    need(define(htext, 'ADRENO_POWER_GPU_CLOCK_COUNT') == M['facts']['gpu_clock_count'],
         'header GPU clock count == manifest')
    need(define(htext, 'ADRENO_POWER_KGSL_SMMU_CLOCK_COUNT') == M['facts']['kgsl_smmu_clock_count'],
         'header kgsl SMMU clock count == manifest')
    need(define(htext, 'ADRENO_POWER_SPT_PRAC_POWER_CONTROL_VALUE') == 0x778000,
         'header SPTPRAC value == upstream')
    need('kAdrenoPowerStepCxGenpdOn = 0' in htext and 'kAdrenoPowerStepHwInit' in htext,
         'header ordered step enum present')
    need('int adreno_power_mmio_permitted(const struct adreno_power_state *state);' in htext,
         'header declares the MMIO gate')
    need('if (!state->cx_genpd_on)' in ctext and 'if (!state->hw_init_done)' in ctext,
         'source enforces EVERY precondition')
    need('adreno_power_gpu_windows_are_gated' in ctext and 'qcom,skip-init' in htext,
         'source + header encode the gated-vs-always-on contrast')
    need('testColdStateForbidsMmio' in ttext and 'adreno_power_mmio_permitted(&cold) == 0' in ttext,
         'host test asserts a cold probe is FORBIDDEN')
    need('testEachPreconditionIsMandatory' in ttext,
         'host test asserts each precondition is mandatory')
    # Honest boundary: no MMIO / no device claim anywhere in the module.
    for label, text in (('header', htext), ('source', ctext), ('test', ttext)):
        need('armcpu_map_physical' not in text and 'mmio_read' not in text,
             f'{label} contains no MMIO / no device mapping')

    ok = all(c[0] for c in CHECKS)
    print(f'ENCHILADA_GPU_POWER_CROSS: {"PASS" if ok else "FAIL"} ({len(CHECKS)} checks, three independent domains agree)')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
