#!/usr/bin/env python3
"""Verification method 1 (EXTRACT): read the GPU power-domain facts straight
from the sha-pinned enchilada DTB and assert they form a coherent, gated domain.

Independent of method 2 (which cross-checks the module + upstream sources).
Pure host analysis of a firmware description -- NOT a device measurement.

    scripts/extract-enchilada-gpu-power.py
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
CHECKS = []


def need(ok, label, detail=''):
    CHECKS.append((bool(ok), label, detail))
    if not ok:
        print(f'FAIL {label}: {detail}')


def dtb():
    p = W / M['dtb']['path']
    raw = p.read_bytes()
    return raw, raw.decode('utf-8').splitlines()


def block(lines, key):
    a, b = M['slices'][key]
    return '\n'.join(lines[a - 1:b])


def hexints(text, prop):
    # (?:^|\s) instead of \b: property names like '#iommu-cells' start with a
    # non-word char, so a \b word-boundary anchor can never match before them.
    m = re.search(r'(?:^|\s)' + re.escape(prop) + r'\s*=\s*<([^>]*)>', text, re.M)
    if not m:
        return []
    return [int(t, 16) for t in re.findall(r'0x[0-9a-fA-F]+', m.group(1))]


def strings(text, prop):
    m = re.search(r'\b' + re.escape(prop) + r'\s*=\s*"([^"]*)"', text)
    if not m:
        return []
    return m.group(1).split('\\0')


def main():
    raw, lines = dtb()
    sha = hashlib.sha256(raw).hexdigest()
    need(sha == M['dtb']['sha256'], 'DTB full-file sha256 pinned', sha)
    need(len(raw) == M['dtb']['bytes'], 'DTB byte count pinned', len(raw))

    # All 4 archived copies must be byte-identical (same DTB as stage-3/4/7).
    identical = []
    for rel in M['dtb']['copies']:
        p = W / rel
        identical.append(p.exists() and hashlib.sha256(p.read_bytes()).hexdigest() == sha)
    need(all(identical), '4 DTB copies byte-identical', identical)

    gx = block(lines, 'gdsc_cx')   # actually cx, see below
    cx = block(lines, 'gdsc_cx')
    gx = block(lines, 'gdsc_gx')
    gpu = block(lines, 'gpu')
    kgsl = block(lines, 'kgsl_smmu')
    apps = block(lines, 'apps_smmu')

    # GDSC nodes.
    need('regulator-name = "gpu_cx_gdsc"' in cx, 'CX GDSC name', 'gpu_cx_gdsc')
    need('regulator-name = "gpu_gx_gdsc"' in gx, 'GX GDSC name', 'gpu_gx_gdsc')
    need('0x509106c' in cx, 'CX GDSC reg 0x509106c')
    need('0x509100c' in gx, 'GX GDSC reg 0x509100c')
    cxph = hexints(cx, 'phandle')
    gxph = hexints(gx, 'phandle')
    need(cxph == [0x1a4], 'CX GDSC phandle 0x1a4', cxph)
    need(gxph == [0x2a9], 'GX GDSC phandle 0x2a9', gxph)

    # GPU power inputs.
    vddcx = hexints(gpu, 'vddcx-supply')
    vdd = hexints(gpu, 'vdd-supply')
    need(vddcx == [0x1a4], 'GPU vddcx-supply -> CX GDSC', vddcx)
    need(vdd == [0x2a9], 'GPU vdd-supply -> GX GDSC', vdd)
    need(strings(gpu, 'regulator-names') == ['vddcx', 'vdd'], 'GPU regulator-names', strings(gpu, 'regulator-names'))
    clocks = strings(gpu, 'clock-names')
    need(clocks == M['facts']['gpu_clock_names'], 'GPU 6 clock-names', clocks)
    need(len(clocks) == M['facts']['gpu_clock_count'], 'GPU clock count 6', len(clocks))
    need('qcom,no-nap' in gpu, 'GPU qcom,no-nap present')

    # kgsl SMMU shares the CX rail + has its own clock.
    ksupply = hexints(kgsl, 'vdd-supply')
    need(ksupply == [0x1a4], 'kgsl SMMU vdd-supply -> SAME CX GDSC', ksupply)
    need(strings(kgsl, 'clock-names') == ['gcc_gpu_memnoc_gfx_clk'], 'kgsl SMMU clock name', strings(kgsl, 'clock-names'))
    need(len(hexints(kgsl, 'clocks')) == 2, 'kgsl SMMU clocks cell present', hexints(kgsl, 'clocks'))

    # apps_smmu is the always-on contrast (why flash #8 survey was safe).
    need('qcom,skip-init' in apps, 'apps_smmu qcom,skip-init')
    need('vdd-supply' not in apps, 'apps_smmu has NO vdd-supply')
    need('clocks' not in apps, 'apps_smmu has NO clocks')
    need(hexints(apps, '#iommu-cells') == [2], 'apps_smmu #iommu-cells = 2')

    # Provenance: same DTB as the device-verified stage-3 backlight node.
    prov = lines[M['keylines']['bl_ctrl_dcs'] - 1]
    need('bl_ctrl_dcs' in prov, 'provenance bl_ctrl_dcs at :2661', prov.strip())

    ok = all(c[0] for c in CHECKS)
    print(f'ENCHILADA_GPU_POWER_EXTRACT: {"PASS" if ok else "FAIL"} ({len(CHECKS)} checks, DTB sha {sha[:12]}...)')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
