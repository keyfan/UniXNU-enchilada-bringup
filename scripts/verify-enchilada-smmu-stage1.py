#!/usr/bin/env python3
"""Round 75 method-2 cross-verifier for AdrenoSmmuStage1V1.

Domain A re-derives the ARM SMMU v2 context-bank words from THREE sha256-pinned
sources, and RECOMPUTES the SCTLR default and every S2CR field from the source
bits rather than trusting the module's constants.

Domain B asserts the module mirrors them, keeps SCTLR LAST in the plan, and keeps
readiness derived and 0.

Never compiles or runs the module; never touches the network.
"""
import hashlib
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
CACHE = DEV / '.cache' / 'adreno-a630-source'
GPU = DEV / 'drivers' / 'NativeAdreno'

PINS = [
    ('P1_arm_smmu_h', CACHE / 'drivers/iommu/arm/arm-smmu/arm-smmu.h', 16630,
     'ed85998794695d1a824dc1b86f680c8d124546688fd2665e00e3cbcfa9546b53'),
    ('P2_arm_smmu_c', CACHE / 'drivers/iommu/arm/arm-smmu/arm-smmu.c', 67693,
     'd5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a'),
    ('P3_io_pgtable_arm_c', CACHE / 'drivers/iommu/io-pgtable-arm.c', 38934,
     'b78adbf87352e574556638fae1206d62bfbc06787a85125f75b9d5da0abab278'),
]

checks = 0
fails = []


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)


def flat(s):
    return ' '.join(s.split())


texts = {}
for name, path, size, sha in PINS:
    b = path.read_bytes() if path.is_file() else b''
    check(len(b) == size, f'A1 {name} size {len(b)} != {size}')
    check(hashlib.sha256(b).hexdigest() == sha, f'A1 {name} sha256 mismatch')
    check(b'\r' not in b, f'A1 {name} has CR')
    texts[name] = b.decode('utf-8').split('\n')


def line(name, number):
    lines = texts[name]
    return lines[number - 1] if 0 < number <= len(lines) else ''


smmu_h, smmu_c, pgtable_c = (texts[k] for k, *_ in PINS)

# ---- A2: the context-bank byte offsets (P1:145-198) ---------------------
A2 = [('SCTLR', 145, 0x00), ('ACTLR', 156, 0x04), ('TCR2', 161, 0x10),
      ('TTBR0', 167, 0x20), ('TTBR1', 168, 0x28), ('TCR', 171, 0x30),
      ('CONTEXTIDR', 191, 0x34), ('S1_MAIR0', 192, 0x38),
      ('S1_MAIR1', 193, 0x3c), ('FSR', 198, 0x58)]
for name, ln, off in A2:
    want = '0x%x' % off
    check(want in line('P1_arm_smmu_h', ln),
          f'A2 P1:{ln} CB_{name} is {want} (line: {line("P1_arm_smmu_h", ln).strip()!r})')

# ---- A3: SCTLR bits (P1:146-154) ---------------------------------------
A3 = [('S1_ASIDPNE', 146, 12), ('CFCFG', 147, 7), ('HUPCF', 148, 8),
      ('CFIE', 149, 6), ('CFRE', 150, 5), ('E', 151, 4), ('AFE', 152, 2),
      ('TRE', 153, 1), ('M', 154, 0)]
for name, ln, bit in A3:
    check(f'BIT({bit})' in line('P1_arm_smmu_h', ln),
          f'A3 P1:{ln} SCTLR_{name} is BIT({bit})')

# ---- A4: the SCTLR default, RECOMPUTED from the bits -------------------
# arm-smmu.c:641-651: CFIE | CFRE | AFE | TRE | M, plus S1_ASIDPNE for stage1,
# plus E only when CONFIG_CPU_BIG_ENDIAN.
body = flat(' '.join(smmu_c[640:652]))
check('ARM_SMMU_SCTLR_CFIE | ARM_SMMU_SCTLR_CFRE | ARM_SMMU_SCTLR_AFE |'
      ' ARM_SMMU_SCTLR_TRE | ARM_SMMU_SCTLR_M' in body,
      'A4 P2:642-643 the five-bit default')
check('if (stage1)' in body and 'ARM_SMMU_SCTLR_S1_ASIDPNE' in body,
      'A4 P2:644-645 stage1 adds S1_ASIDPNE')
check('CONFIG_CPU_BIG_ENDIAN' in body and 'ARM_SMMU_SCTLR_E' in body,
      'A4 P2:646-647 E is added only for a big-endian CPU')

BIT = {n: 1 << b for n, _l, b in A3}
LE = BIT['CFIE'] | BIT['CFRE'] | BIT['AFE'] | BIT['TRE'] | BIT['M'] | BIT['S1_ASIDPNE']
BE = LE | BIT['E']
check(LE == 0x1067, f'A4 recomputed LE SCTLR is 0x1067 (got 0x{LE:04X})')
check(BE == 0x1077, f'A4 recomputed BE SCTLR is 0x1077 (got 0x{BE:04X})')
check(BE - LE == BIT['E'], 'A4 the two differ EXACTLY in E')
check(bool(LE & BIT['M']), 'A4 the default DOES enable translation (M set)')
check(not (LE & BIT['CFCFG']), 'A4 CFCFG is NOT in the default')

# ---- A5: S2CR geometry and fields --------------------------------------
check('(0xc00 + ((n) << 2))' in line('P1_arm_smmu_h', 105),
      'A5 P1:105 GR0_S2CR(n) = 0xc00 + (n << 2)')
check('(0x0 + ((n) << 2))' in line('P1_arm_smmu_h', 123),
      'A5 P1:123 GR1_CBAR(n) = n << 2')
A5 = [('PRIVCFG', 106, 25, 24), ('TYPE', 113, 17, 16), ('EXIDVALID', 119, 10, 10),
      ('CBNDX', 120, 7, 0)]
for name, ln, hi, lo in A5:
    want = f'GENMASK({hi}, {lo})' if hi != lo else f'BIT({lo})'
    check(want in line('P1_arm_smmu_h', ln),
          f'A5 P1:{ln} S2CR_{name} is {want}')
check('S2CR_TYPE_TRANS,' in line('P1_arm_smmu_h', 115), 'A5 TYPE TRANS == 0')
check('S2CR_TYPE_BYPASS,' in line('P1_arm_smmu_h', 116), 'A5 TYPE BYPASS == 1')
check('S2CR_TYPE_FAULT,' in line('P1_arm_smmu_h', 117), 'A5 TYPE FAULT == 2')
# Recompute the field values from the masks.
check(0x03000000 == ((1 << 26) - 1) - ((1 << 24) - 1),
      'A5 PRIVCFG mask is 0x03000000')
check(0x00030000 == ((1 << 18) - 1) - ((1 << 16) - 1),
      'A5 TYPE mask is 0x00030000')
check(0x000000ff == ((1 << 8) - 1), 'A5 CBNDX mask is 0x000000ff')

# ---- A6: the S2CR writer composition (P2:951-968) ----------------------
s2 = flat(' '.join(smmu_c[950:969]))
check('FIELD_PREP(ARM_SMMU_S2CR_TYPE' in s2, 'A6 P2:961 TYPE composed')
check('FIELD_PREP(ARM_SMMU_S2CR_CBNDX' in s2, 'A6 P2:962 CBNDX composed')
check('FIELD_PREP(ARM_SMMU_S2CR_PRIVCFG' in s2, 'A6 P2:963 PRIVCFG composed')
check('ARM_SMMU_S2CR_EXIDVALID' in s2, 'A6 P2:967 EXIDVALID is conditional')
check('arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_S2CR(idx), reg)' in s2,
      'A6 P2:968 written to GR0_S2CR(idx)')
# The init value is BYPASS unless disable_bypass, in which case FAULT.
init = flat(' '.join(smmu_c[64:67]))
check('disable_bypass ? S2CR_TYPE_FAULT : S2CR_TYPE_BYPASS' in init,
      'A6 P2:66 the init value is BYPASS, or FAULT when disable_bypass')

# ---- A7: the page-table format the TCR describes (P3) ------------------
check(len(pgtable_c) > 100, 'A7 P3 io-pgtable-arm.c is present and non-trivial')
check(any('ARM_PGTABLE' in l or 'l_pgtable' in l for l in pgtable_c[:40]) or
      any('arm_lpae' in l for l in pgtable_c[:400]),
      'A7 P3 is the ARM LPAE page-table implementation')

# ======================= Domain B: the deliverable =======================
h = (GPU / 'AdrenoSmmuStage1V1.h').read_text(encoding='utf-8')
c = (GPU / 'AdrenoSmmuStage1V1.c').read_text(encoding='utf-8')
t = (GPU / 'tests' / 'adreno-smmu-stage1-v1-test.cpp').read_text(encoding='utf-8')

for name, path, size, sha in PINS:
    check(sha in h, f'B1 header records {name} sha256')

B2 = [('kAdrenoSmmuCbSctlr', '0x00u'), ('kAdrenoSmmuCbTtbr0', '0x20u'),
      ('kAdrenoSmmuCbTcr', '0x30u'), ('kAdrenoSmmuCbContextidr', '0x34u'),
      ('kAdrenoSmmuCbS1Mair0', '0x38u'), ('kAdrenoSmmuCbFsr', '0x58u'),
      ('kAdrenoSmmuSctlrS1Asidpne', '0x00001000u'),
      ('kAdrenoSmmuSctlrCfie', '0x00000040u'),
      ('kAdrenoSmmuSctlrCfre', '0x00000020u'),
      ('kAdrenoSmmuSctlrAfe', '0x00000004u'),
      ('kAdrenoSmmuSctlrTre', '0x00000002u'),
      ('kAdrenoSmmuSctlrM', '0x00000001u'),
      ('kAdrenoSmmuSctlrE', '0x00000010u'),
      ('kAdrenoSmmuGr0S2crBase', '0x0c00u'),
      ('kAdrenoSmmuS2crPrivcfgMask', '0x03000000u'),
      ('kAdrenoSmmuS2crTypeMask', '0x00030000u'),
      ('kAdrenoSmmuS2crExidvalid', '0x00000400u'),
      ('kAdrenoSmmuS2crCbndxMask', '0x000000ffu')]
for sym, val in B2:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B2 {sym}={val}')

APIS = ['adreno_smmu_sctlr_stage1', 'adreno_smmu_sctlr_stage1_be',
        'adreno_smmu_sctlr_enabled', 'adreno_smmu_s2cr_addr',
        'adreno_smmu_s2cr_value', 'adreno_smmu_s2cr_type_for_state',
        'adreno_smmu_stage1_plan', 'adreno_smmu_stage1_missing',
        'adreno_smmu_stage1_input_name', 'adreno_smmu_stage1_input_anchor',
        'adreno_smmu_stage1_ready', 'adreno_smmu_stage1_verify']
for api in APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B5 .c defines {api}')
for bad in ('#include <string.h>', '#include <stdlib.h>', '#include <stdio.h>',
            'IOConnectCall', 'IOServiceOpen', 'writel(', 'readl(',
            'volatile uint32_t *'):
    check(bad not in c, f'B5 .c has no {bad}')

# B6: SCTLR is LAST in the plan -- the one ordering rule that matters
plan_body = c[c.find('int adreno_smmu_stage1_plan'):]
plan_body = plan_body[:plan_body.find('\n}\n')]
i_sctlr = plan_body.find('out[n].offset = kAdrenoSmmuCbSctlr')
i_mair0 = plan_body.find('out[n].offset = kAdrenoSmmuCbS1Mair0')
i_ttbr0 = plan_body.find('out[n].offset = kAdrenoSmmuCbTtbr0')
check(i_mair0 >= 0 and i_ttbr0 >= 0 and i_sctlr >= 0, 'B6 the plan names all three')
check(i_sctlr > i_mair0 and i_sctlr > i_ttbr0,
      'B6 SCTLR is written AFTER MAIR0 and TTBR0 (translation enabled last)')
check('translation' in plan_body.lower(), 'B6 the ordering rationale is stated')

# B7: all four inputs missing, derived readiness, 0
check('mask |= kAdrenoSmmuInputPageTable;' in c, 'B7 I1 page table recorded missing')
check('mask |= kAdrenoSmmuInputAsid;' in c, 'B7 I2 ASID recorded missing')
check('mask |= kAdrenoSmmuInputStreamBinding;' in c,
      'B7 I3 stream binding recorded missing')
check('mask |= kAdrenoSmmuInputCxPowered;' in c, 'B7 I4 CX powered recorded missing')
check('adreno_smmu_stage1_missing() == 0u ? 1 : 0' in c,
      'B7 ready() is DERIVED from the missing mask')
check('Busy=10' in c, 'B7 I3 cites the round-53 measurement')
check('0x00005ff4' in c, 'B7 I4 cites the round-35 measurement')

# B8: BYPASS is what a non-faulting stream gets -- round 53's precondition
check('return kAdrenoSmmuS2crTypeBypass;' in c,
      'B8 a non-faulting stream resolves to BYPASS, never TRANS')

check(len(re.findall(r'static void (test[A-Za-z]*)', t)) == 6,
      'B10 6 test functions')
tflat = t.replace('"\n                "', '')
check('PASS: enchilada A630 SMMU stage-1 context word model (6 test groups, 0 failures)'
      in tflat, 'B10 PASS line')

# B11: wiring + the admission row
build = (DEV / 'scripts' / 'build-enchilada-adreno.py').read_text(encoding='utf-8')
host = (DEV / 'scripts' / 'check-enchilada-adreno-host.py').read_text(encoding='utf-8')
check("GPU / 'AdrenoSmmuStage1V1.c', ['-mgeneral-regs-only']" in build,
      'B11 build script compiles the 27th unit')
check("'AdrenoSmmuStage1V1': GPU / 'AdrenoSmmuStage1V1.c'," in host,
      'B11 host SOURCES entry')
check("('adreno-smmu-stage1-v1-test'" in host, 'B11 host TESTS entry')
check('/smmu-stage1-v1 host tests' in host, 'B11 PASS enumeration updated')
check(len(re.findall(r"\(c_rec, GPU / '[^']+'", build)) == 26,
      'B11 26 C archive units declared')
adm = (GPU / 'AdrenoGen1BackendV1.c').read_text(encoding='utf-8')
check('out->items[kEnchiladaGen1PrereqSmmuStage1].modeled = 1u;' in adm,
      'B11 kgsl-smmu-stage1 is modeled=1 since round 75')
check('adm.modeled_count != 8u' in adm, 'B11 the self-check expects eight modelled')

print(f'Domain A: {len(PINS)} sha256-pinned sources; the ten CB offsets, nine SCTLR '
      f'bits, the SCTLR default RECOMPUTED from those bits (0x1067 LE / 0x1077 BE), '
      f'the S2CR geometry/fields and the S2CR writer composition all re-derived')
print('Domain B: AdrenoSmmuStage1V1 .h/.c/test mirror; SCTLR written LAST; all four '
      'inputs recorded missing; ready() derived and 0; 27th unit/suite')
print(f'SMMU_STAGE1_CROSS: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
for f in fails:
    print('  FAIL ' + f)
sys.exit(0 if not fails else 1)
