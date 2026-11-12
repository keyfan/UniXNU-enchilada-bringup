#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Independent cross-verification for AdrenoMetalSeamV1 (ROUND 65).
# Method 2 of 2 (method 1 = the C++ host test + strict freestanding compile).
# Domain A: read the FOUR sha256-pinned upstream sources and re-derive the
#           generation-independent wire ABI + the A630 board facts from them.
# Domain B: read AdrenoMetalSeamV1.h/.c and verify every constant / mask /
#           behavior equals Domain A, WITHOUT trusting the module's own story.
# No device run, no CI, no flash. HOST-VERIFIED only.
import re
import sys
import hashlib
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]          # .../oneplus6-ios/work/UniXNU-enchilada
ONEPLUS = DEV.parents[1]                            # .../oneplus6-ios
UP = DEV.parents[0] / 'UniXNU'                       # .../oneplus6-ios/work/UniXNU
GPU = DEV / 'drivers' / 'NativeAdreno'
CACHE = DEV / '.cache' / 'adreno-a630-source'
DTB = ONEPLUS / 'notes' / 'mu-silicium' / 'enchilada.dts'

PINS = {
    'P1_abi': (UP / 'drivers' / 'Adreno740' / 'NativeAdrenoABI.h',
               7733, '20ac9298bb390d86788c1fc58155e1f9581ffa942e222752efc95c67cb5670d2'),
    'P2_catalog': (CACHE / 'drivers' / 'gpu' / 'drm' / 'msm' / 'adreno' / 'a6xx_catalog.c',
                   51692, '6d9061ee08308126adc7b7250810b0223ee01c678e7afab6eb1738e53524c24c'),
    'P3_dtb': (DTB, 784857,
               'e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275'),
    'P4_gpudoc': (UP / 'docs' / 'porting' / 'gpu.md', None, None),  # content-checked below
}

failures = []
checks = 0
def need(cond, msg):
    global checks
    checks += 1
    if not cond:
        failures.append(msg)

def text(p):
    return p.read_text(encoding='utf-8', errors='replace')

# ---------------- Domain A: re-derive from pinned sources ----------------
A = {}
for key, (p, size, sha) in PINS.items():
    need(p.is_file(), f'{key}: pinned file missing: {p}')
    if not p.is_file():
        continue
    b = p.read_bytes()
    if size is not None:
        need(len(b) == size, f'{key}: size {len(b)} != pinned {size}')
    if sha is not None:
        got = hashlib.sha256(b).hexdigest()
        need(got == sha, f'{key}: sha256 {got} != pinned {sha}')

abi = text(PINS['P1_abi'][0])
cat = text(PINS['P2_catalog'][0])
dtb = text(PINS['P3_dtb'][0])
doc = text(PINS['P4_gpudoc'][0])

# P1: generation-independent wire ABI (one anonymous enum holds all constants)
A['version'] = int(re.search(r'NativeAdrenoAbiVersion\s*=\s*(\d+)', abi).group(1))
A['maxbuf'] = int(re.search(r'NativeAdrenoAbiMaxBuffers\s*=\s*(\d+)', abi).group(1))
A['boshift'] = int(re.search(r'NativeAdrenoAbiMaxBoBytesShift\s*=\s*(\d+)', abi).group(1))
A['meta'] = int(re.search(r'NativeAdrenoAbiMetadataBytes\s*=\s*(\d+)', abi).group(1))
A['queues'] = int(re.search(r'NativeAdrenoAbiMaxQueues\s*=\s*(\d+)', abi).group(1))
A['hostimport'] = int(re.search(r'NativeAdrenoParamsHostImport\s*=\s*(\d+)', abi).group(1))
A['methods'] = sorted(int(x) for x in re.findall(r'NativeAdrenoMethod\w+\s*=\s*(\d+)', abi))
# DeviceInfo fields (P1:133-146)
dblock = re.search(r'struct\s+NativeAdrenoAbiDeviceInfo\s*\{(.*?)\}', abi, re.S)
A['di_fields'] = re.findall(r'\b(\w+)\s*;', dblock.group(1)) if dblock else []
A['abi_asserts'] = len(re.findall(r'static_assert\s*\(', abi))
A['never_guess'] = 'never from a guess' in abi
A['unknown_refused'] = 'Unknown versions, methods and flags are refused' in abi

need(A['version'] == 2, f'P1 version {A["version"]} != 2')
need(A['maxbuf'] == 128, f'P1 MaxBuffers {A["maxbuf"]} != 128')
need(A['boshift'] == 28, f'P1 MaxBoBytesShift {A["boshift"]} != 28')
need(A['meta'] == 64, f'P1 MetadataBytes {A["meta"]} != 64')
need(A['queues'] == 8, f'P1 MaxQueues {A["queues"]} != 8')
need(A['hostimport'] == 1, f'P1 ParamsHostImport {A["hostimport"]} != 1')
need(A['methods'] == list(range(1, 17)), f'P1 methods {A["methods"]} != 1..16')
need(A['di_fields'] == ['chip_id','gmem_bytes','gmem_base','uche_trap_base',
                        'va_start','va_size','highest_bank_bit','ubwc_swizzle',
                        'macrotile_mode','ubwc_version','flags','reserved'],
     f'P1 DeviceInfo fields {A["di_fields"]} mismatch')
need(A['abi_asserts'] == 16, f'P1 static_assert count {A["abi_asserts"]} != 16')
need(A['never_guess'], 'P1: "never from a guess" provenance sentence missing')
need(A['unknown_refused'], 'P1: "Unknown versions ... refused" sentence missing')

# P2: A630 catalog entry -- slice EXACTLY the entry that owns 0x06030001
idx = cat.find('0x06030001')
need(idx > 0, 'P2: 0x06030001 not found in catalog')
start = cat.rfind('.chip_ids = ADRENO_CHIP_IDS(', 0, idx)
end = cat.find('}, {', idx)
a630 = cat[start:end] if start > 0 and end > start else ''
need(len(a630) > 100, 'P2: could not isolate the A630 catalog entry')
A['chip_v1'] = '0x06030001' in a630
A['chip_alt'] = '0x06030002' in a630
A['gen1'] = 'ADRENO_6XX_GEN1' in a630
A['revn'] = bool(re.search(r'\.revn\s*=\s*630', a630))
A['gmem'] = 'SZ_1M' in a630
A['zap'] = 'a630_zap.mdt' in a630
A['no_aspace'] = 'address_space_size' not in a630
A['sqe_fw'] = 'a630_sqe.fw' in a630
A['gmu_fw'] = 'a630_gmu.bin' in a630
for k, what in [('chip_v1','chip 0x06030001'),('chip_alt','chip 0x06030002'),
                ('gen1','ADRENO_6XX_GEN1'),('revn','revn 630'),('gmem','gmem SZ_1M'),
                ('zap','zapfw a630_zap.mdt'),('no_aspace','no address_space_size'),
                ('sqe_fw','fw a630_sqe.fw'),('gmu_fw','fw a630_gmu.bin')]:
    need(A[k], f'P2 A630 entry: {what} not found in pinned slice')
A['gmem_bytes'] = 0x00100000  # SZ_1M

# P3: DTB GPU facts (exact property names, not the display 'sde-'/'mdss-' variants)
A['dtb_chipid'] = bool(re.search(r'qcom,chipid\s*=\s*<0x6030001>', dtb))
A['dtb_hbb'] = bool(re.search(r'qcom,highest-bank-bit\s*=\s*<0x0f>', dtb))
A['dtb_ubwc'] = bool(re.search(r'qcom,ubwc-mode\s*=\s*<0x02>', dtb))
A['dtb_pas'] = bool(re.search(r'qcom,pas-id\s*=\s*<0x0d>', dtb))
A['dtb_zapname'] = 'qcom,firmware-name = "a630_zap"' in dtb
need(A['dtb_chipid'], 'P3 DTB qcom,chipid=<0x6030001> missing')
need(A['dtb_hbb'], 'P3 DTB highest-bank-bit=<0x0f> missing')
need(A['dtb_ubwc'], 'P3 DTB ubwc-mode=<0x02> missing')
need(A['dtb_pas'], 'P3 DTB pas-id=<0x0d> missing')
need(A['dtb_zapname'], 'P3 DTB firmware-name="a630_zap" missing')
A['hbb'] = 15
A['ubwc'] = 2
A['pas'] = 13
A['chipid'] = 0x06030001

# P4: docs/porting/gpu.md section 1
doc_flat = ' '.join(doc.split())
A['section1'] = 'implement the matching kernel backend and compiler support before selecting its frontend' in doc_flat
need(A['section1'], 'P4 section-1 decisive quote missing')

# P5: Mesa A630 compiler support (broad-lookup fact against
# gitlab.freedesktop.org freedreno_devices.py: GPUId(630) / a6xx_gen1).
# Recorded in the header/evidence as a documented source fact, not re-fetched.

# ---------------- Domain B: verify the new module equals Domain A ----------
h = text(GPU / 'AdrenoMetalSeamV1.h')
c = text(GPU / 'AdrenoMetalSeamV1.c')
t = text(GPU / 'tests' / 'adreno-metal-seam-v1-test.cpp')

def h_enum(name):
    m = re.search(rf'{name}\s*=\s*(\d+)', h)
    return int(m.group(1)) if m else None

need(h_enum('kEnchiladaMetalAbiVersion') == A['version'], 'B: abi version != P1')
need(h_enum('kEnchiladaMetalAbiMaxBuffers') == A['maxbuf'], 'B: MaxBuffers != P1')
need(h_enum('kEnchiladaMetalAbiMaxBoBytesShift') == A['boshift'], 'B: MaxBoBytesShift != P1')
need(h_enum('kEnchiladaMetalAbiMetadataBytes') == A['meta'], 'B: MetadataBytes != P1')
need(h_enum('kEnchiladaMetalAbiMaxQueues') == A['queues'], 'B: MaxQueues != P1')
need(h_enum('kEnchiladaMetalParamsHostImport') == A['hostimport'], 'B: HostImport != P1')

mm = re.search(r'enum\s+enchilada_metal_method\s*\{(.*?)\}', h, re.S)
b_methods = sorted(int(x) for x in re.findall(r'=\s*(\d+)\s*,?\s*(?://[^\n]*)?$', mm.group(1), re.M)) if mm else []
need(b_methods == A['methods'], f'B: methods {b_methods} != P1 {A["methods"]}')

dib = re.search(r'struct\s+enchilada_metal_abi_device_info\s*\{(.*?)\}', h, re.S)
b_di = re.findall(r'\b(\w+)\s*;', dib.group(1)) if dib else []
need(b_di == A['di_fields'], f'B: DeviceInfo fields {b_di} != P1 {A["di_fields"]}')

b_asserts = len(re.findall(r'static_assert\s*\(', h))
need(b_asserts == 20, f'B: header static_assert count {b_asserts} != 20 (16 P1 + 4 replies)')

def macro(name):
    m = re.search(rf'#define\s+{name}\s+(0x[0-9a-fA-F]+u|\d+u)', h)
    return m.group(1).rstrip('u') if m else None
need(int(macro('ENCHILADA_METAL_A630_CHIP_ID'), 16) == A['chipid'], 'B: chip_id != P3/catalog')
need(int(macro('ENCHILADA_METAL_A630_CHIP_ID_ALT'), 16) == 0x06030002, 'B: chip_id_alt != catalog')
need(int(macro('ENCHILADA_METAL_A630_GMEM_BYTES'), 16) == A['gmem_bytes'], 'B: gmem != SZ_1M')
need(macro('ENCHILADA_METAL_A630_HIGHEST_BANK_BIT') == str(A['hbb']), 'B: hbb != DTB 15')
need(macro('ENCHILADA_METAL_A630_UBWC_MODE') == str(A['ubwc']), 'B: ubwc != DTB 2')
need(macro('ENCHILADA_METAL_A630_ZAP_PAS_ID') == str(A['pas']), 'B: pas != DTB 13')

# field-mask arithmetic: 11 fields; specified = bits 0,1,6 = 0x43; derived = 0x7bc
need('0x000007ffu' in h, 'B: DEVICE_FIELD_ALL 0x7ff missing')
spec = 0x43
derived = 0x7ff & ~spec
need(derived == 0x7bc, f'A: derived mask arithmetic {derived:#x} != 0x7bc')
need(spec & derived == 0 and (spec | derived) == 0x7ff, 'A: specified/derived not disjoint+complete')
need('ENCHILADA_METAL_DEVICE_FIELD_ALL &' in h and '~' in h,
     'B: BOARD_DERIVED_FIELDS must be derived from the ALL mask, not hardcoded')
need('(uint32_t)' in h, 'B: masks must be forced uint32_t (strict C -Wsign-conversion)')

sm = re.search(r'enum\s+enchilada_metal_stage\s*\{(.*?)\}', h, re.S)
b_stages = [x for x in re.findall(r'(kEnchiladaMetalStage\w+)\s*=\s*\d+', sm.group(1))
            if not x.endswith('Count')] if sm else []
need(len(b_stages) == 9, f'B: stage count {len(b_stages)} != 9 (Fetch..Discovery)')
bm = re.search(r'enum\s+enchilada_metal_blocker\s*\{(.*?)\}', h, re.S)
b_blockers = len(re.findall(r'1u\s*<<', bm.group(1))) if bm else 0
need(b_blockers == 5, f'B: blocker count {b_blockers} != 5')
need('0x0000001fu' in h, 'B: TODAY_BLOCKERS 0x1f missing')

# .c honesty: only 3 fields filled, 8 left zero, params gated, compiler=1, frontend gate
need(c.count('out->chip_id =') == 1 and c.count('out->gmem_bytes =') == 1 and
     c.count('out->highest_bank_bit =') == 1, 'C: exactly 3 DeviceInfo fields filled')
need('zero_bytes((unsigned char *)out, sizeof(*out))' in c, 'C: DeviceInfo zeroed first')
for f in ['gmem_base','uche_trap_base','va_start','va_size','ubwc_swizzle',
          'macrotile_mode','ubwc_version','flags','reserved']:
    need(f'info->{f} != 0u' in c, f'C: consistency must reject fabricated {f}')
need('p->abi_version != (uint32_t)kEnchiladaMetalAbiVersion' in c, 'C: version gate missing')
need('unknown flags refused' in c or 'flags != 0u &&' in c, 'C: flag gate missing')
need('return 1;' in c and 'GPUId(630)' in c, 'C: compiler_a630_available documents Mesa fact')
need('(gen1_backend && compiler_a630 && signed_zap && smmu_context && os_abi)' in c,
     'C: frontend gate must require all five prerequisites')
need('return 0u; /* pinned inputs' in c, 'C: Fetch stage portable')
need('kEnchiladaMetalBlockerGen1Backend |' in c and
     'kEnchiladaMetalBlockerSignedZap |' in c and
     'kEnchiladaMetalBlockerSmmuContext' in c, 'C: KernelBackend carries 3 hardware blockers')

# host test wiring: 6 groups + corrected mask literals
need(all(g in t for g in ['testWireAbiMirror','testLayouts','testParamsValid',
                          'testA630DeviceInfo','testEstablishmentChain','testFrontendGate']),
     'TEST: one of the 6 groups missing')
need('0x00000043u' in t and '0x000007bcu' in t, 'TEST: corrected mask literals missing')

# ---------------- report ----------------
if failures:
    for f in failures:
        print('FAIL:', f)
    print(f'METAL_SEAM_CROSS: FAIL ({checks} checks, {len(failures)} failures)')
    sys.exit(1)
print(f'METAL_SEAM_CROSS: PASS ({checks} checks, 0 failures)')
print('  Domain A: 4 pinned sources (ABI/catalog/DTB/gpu.md) re-derived; Mesa A630 = documented P5')
print('  Domain B: AdrenoMetalSeamV1.{h,c} + host test match Domain A; 20 asserts; 9 stages / 5 blockers')
sys.exit(0)
