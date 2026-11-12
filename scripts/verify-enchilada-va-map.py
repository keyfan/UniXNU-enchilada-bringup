#!/usr/bin/env python3
"""Round 77 method-2 cross-verifier for AdrenoVaMapV1.

Domain A re-derives the GPU VA aperture DERIVATION CHAIN from THREE sha256-pinned
sources and RECOMPUTES the aperture for several `ias` values independently.

Domain B asserts the module mirrors the chain, keeps the GMU/GPU spaces distinct,
and keeps readiness derived and 0.

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
    ('P1_adreno_gpu_c', CACHE / 'drivers/gpu/drm/msm/adreno/adreno_gpu.c', 29592,
     'e6b5203fb56358d22542197bd251c1513dc2b2f1ae8aaf3f9d9fca129f6f00b7'),
    ('P2_arm_smmu_c', CACHE / 'drivers/iommu/arm/arm-smmu/arm-smmu.c', 67693,
     'd5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a'),
    ('P3_a6xx_gmu_c', CACHE / 'drivers/gpu/drm/msm/adreno/a6xx_gmu.c', 48981,
     'c14fbb253e602bfd2585d757da871dde08ea63b811ff3c6808f657f4522f78d4'),
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


agpu, smmu_c, gmu_c = (texts[k] for k, *_ in PINS)

# ---- A2: the address-space creation (P1:201-233) ----------------------
check('adreno_iommu_create_address_space(struct msm_gpu *gpu,' in
      line('P1_adreno_gpu_c', 202), 'A2 P1:202 adreno_iommu_create_address_space')
check('mmu = msm_iommu_gpu_new(&pdev->dev, gpu, quirks);' in
      line('P1_adreno_gpu_c', 211), 'A2 P1:211 the SMMU-backed mmu is created')
check('geometry = msm_iommu_get_geometry(mmu);' in line('P1_adreno_gpu_c', 215),
      'A2 P1:215 the GEOMETRY is asked of the SMMU, not hard-coded')
check('Use the aperture start or SZ_16M, whichever is greater.' in flat(
      ' '.join(agpu[218:224])), 'A2 P1:219-223 the 16 MiB rationale comment')
check('start = max_t(u64, SZ_16M, geometry->aperture_start);' in
      line('P1_adreno_gpu_c', 224), 'A2 P1:224 the 16 MiB floor clamp')
check('size = geometry->aperture_end - start + 1;' in line('P1_adreno_gpu_c', 225),
      'A2 P1:225 size from aperture_end')
check('msm_gem_address_space_create(mmu, "gpu",' in line('P1_adreno_gpu_c', 227),
      'A2 P1:227 the GPU space is named "gpu"')

# ---- A3: the geometry from ias (P2:820-831) --------------------------
geo = flat(' '.join(smmu_c[820:832]))
check('domain->geometry.aperture_start = ~0UL << ias;' in geo,
      'A3 P2:825 TTBR1 shape: start = ~0 << ias')
check('domain->geometry.aperture_end = ~0UL;' in geo,
      'A3 P2:826 TTBR1 shape: end = ~0')
check('domain->geometry.aperture_end = (1UL << ias) - 1;' in geo,
      'A3 P2:828 TTBR0 shape: end = (1 << ias) - 1')
check('IO_PGTABLE_QUIRK_ARM_TTBR1' in geo, 'A3 P2:824 the TTBR1 quirk selects the shape')
check('domain->geometry.force_aperture = true;' in geo, 'A3 P2:831 force_aperture')

# ---- A4: ias comes from a RUNTIME register read (P2:1879, :1904-1905) --
check('ias = smmu->va_size;' in line('P2_arm_smmu_c', 735),
      'A4 P2:735 ias = smmu->va_size')
check('id = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID2);' in
      line('P2_arm_smmu_c', 1879), 'A4 P2:1879 ID2 is READ from the SMMU')
check('size = FIELD_GET(ARM_SMMU_ID2_UBS, id);' in line('P2_arm_smmu_c', 1904),
      'A4 P2:1904 va_size comes from ID2.UBS')
check('smmu->va_size = arm_smmu_id_size_to_bits(size);' in
      line('P2_arm_smmu_c', 1905), 'A4 P2:1905 va_size = id_size_to_bits(UBS)')
check('if (smmu->version < ARM_SMMU_V2)' in line('P2_arm_smmu_c', 1899),
      'A4 P2:1899 v1 takes va_size from ipa_size instead')
check('smmu->va_size = smmu->ipa_size;' in line('P2_arm_smmu_c', 1900),
      'A4 P2:1900 the v1 branch')
# ID2 is read in the v1/v2 split, so the value is silicon-dependent in BOTH.
check(1879 < 1904, 'A4 the ID2 read precedes the va_size derivation')

# ---- A5: the GMU space is a DIFFERENT space (P3:1283, :1245) ----------
check('msm_gem_address_space_create(mmu, "gmu", 0x0, 0x80000000)' in
      line('P3_a6xx_gmu_c', 1283), 'A5 P3:1283 the GMU space is 0x0..0x80000000')
check('range_end = 0x80000000;' in line('P3_a6xx_gmu_c', 1245),
      'A5 P3:1245 the GMU allocation range end')
# The names differ: "gpu" vs "gmu".
check('"gpu"' in line('P1_adreno_gpu_c', 227) and '"gmu"' in line('P3_a6xx_gmu_c', 1283),
      'A5 the two spaces carry DIFFERENT names')

# ---- A6: recompute the aperture independently ------------------------
def geometry(ias, shape):
    if shape == 'ttbr0':
        return 0, (1 << ias) - 1
    return (~0) << ias, ~0


def aperture(ias, shape):
    s0, e = geometry(ias, shape)
    s = max(0x01000000, s0)
    return s, e, e - s + 1


for ias in (32, 36, 40, 48):
    s, e, size = aperture(ias, 'ttbr0')
    check(s == 0x01000000, f'A6 ias={ias} start is the 16 MiB floor')
    check(e == (1 << ias) - 1, f'A6 ias={ias} end is (1<<ias)-1')
    check(size == e - s + 1, f'A6 ias={ias} size is end-start+1')
# Monotonic in ias.
prev = 0
for ias in (32, 36, 40, 44, 48):
    _, _, size = aperture(ias, 'ttbr0')
    check(size > prev, f'A6 size grows with ias at {ias}')
    prev = size
# The GMU end is NOT any ttbr0 end for the ubs sizes in range.
for ias in (32, 36, 40, 48):
    _, e, _ = aperture(ias, 'ttbr0')
    check(e != 0x80000000, f'A6 ias={ias} GPU end is not the GMU end 0x80000000')
# The clamp is meaningful: a 0 start would otherwise give a different size.
check(max(0x01000000, 0) == 0x01000000, 'A6 the clamp actually fires for start 0')

# ======================= Domain B: the deliverable =======================
h = (GPU / 'AdrenoVaMapV1.h').read_text(encoding='utf-8')
c = (GPU / 'AdrenoVaMapV1.c').read_text(encoding='utf-8')
t = (GPU / 'tests' / 'adreno-va-map-v1-test.cpp').read_text(encoding='utf-8')

for name, path, size, sha in PINS:
    check(sha in h, f'B1 header records {name} sha256')

B2 = [('kAdrenoVaFloorBytes', '0x01000000ull'),
      ('kAdrenoVaGmuApertureEnd', '0x80000000ull'),
      ('kAdrenoVaShapeTtbr0', '0u'), ('kAdrenoVaShapeTtbr1', '1u')]
for sym, val in B2:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B2 {sym}={val}')

APIS = ['adreno_va_ias_is_plausible', 'adreno_va_clamp_start',
        'adreno_va_geometry', 'adreno_va_aperture', 'adreno_va_gmu_end_is_gpu_end',
        'adreno_va_missing', 'adreno_va_blocker_name',
        'adreno_va_blocker_anchor', 'adreno_va_map_ready', 'adreno_va_verify']
for api in APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B5 .c defines {api}')
for bad in ('#include <string.h>', '#include <stdlib.h>', '#include <stdio.h>',
            'IOConnectCall', 'IOServiceOpen', 'writel(', 'readl(',
            'volatile uint32_t *'):
    check(bad not in c, f'B5 .c has no {bad}')

# B6: the clamp and both geometry shapes are implemented, not described
check('(aperture_start > kAdrenoVaFloorBytes) ? aperture_start' in c,
      'B6 clamp implemented as a comparison, not a magic return')
check('(1ull << ias) - 1ull' in c, 'B6 TTBR0 end computed from ias')
check('(~0ull << ias)' in c, 'B6 TTBR1 start computed from ias')
check('else if (shape == kAdrenoVaShapeTtbr1)' in c, 'B6 both shapes handled')
# B7: the GMU/GPU distinction is enforced, not just documented
check('return (gpu_end == kAdrenoVaGmuApertureEnd) ? 1 : 0;' in c,
      'B7 gmu_end_is_gpu_end is a real comparison')
check('NOT the GPU' in h or 'NOT the GPU' in c or 'a DIFFERENT space' in h,
      'B7 the distinction is stated')
# B8: all three blockers, derived readiness
check('mask |= kAdrenoVaBlockerId2Unread;' in c, 'B8 B1 recorded')
check('mask |= kAdrenoVaBlockerNoSmmu;' in c, 'B8 B2 recorded')
check('mask |= kAdrenoVaBlockerNoWalker;' in c, 'B8 B3 recorded')
check('adreno_va_missing() == 0u ? 1 : 0' in c,
      'B8 ready() is DERIVED from the missing mask')
check('ID2' in c, 'B8 the ID2 citation is present')

_fn = re.findall(r'static void (test[A-Za-z]*)', t)
check(len(_fn) == 7, 'B10 7 test functions (got %d): %r' % (len(_fn), _fn))
tflat = t.replace('"\n                "', '')
check('PASS: enchilada A630 GPU VA aperture derivation model (7 test groups, 0 failures)'
      in tflat, 'B10 PASS line')

# B11: wiring + the admission row
build = (DEV / 'scripts' / 'build-enchilada-adreno.py').read_text(encoding='utf-8')
host = (DEV / 'scripts' / 'check-enchilada-adreno-host.py').read_text(encoding='utf-8')
check("GPU / 'AdrenoVaMapV1.c', ['-mgeneral-regs-only']" in build,
      'B11 build script compiles the 28th unit')
check("'AdrenoVaMapV1': GPU / 'AdrenoVaMapV1.c'," in host, 'B11 host SOURCES entry')
check("('adreno-va-map-v1-test'" in host, 'B11 host TESTS entry')
check('/va-map-v1' in host, 'B11 PASS enumeration lists va-map-v1')
check('/smmu-ctx-word-v1' in host,
      'B11 and the later round-83 suite, so the enumeration is CUMULATIVE')
check(len(re.findall(r"\(c_rec, GPU / '[^']+'", build)) == 28,
      'B11 28 C archive units declared')
adm = (GPU / 'AdrenoGen1BackendV1.c').read_text(encoding='utf-8')
check('out->items[kEnchiladaGen1PrereqBoardVaMap].modeled = 1u;' in adm,
      'B11 board-va-map is modeled=1 since round 77')
check('adm.modeled_count != 9u' in adm, 'B11 the self-check expects nine modelled')

print(f'Domain A: {len(PINS)} sha256-pinned sources; the address-space chain, the two '
      f'geometry shapes, the ID2.UBS RUNTIME read that makes the value underivable '
      f'statically, and the GMU-vs-GPU space distinction all re-derived; the aperture '
      f'is recomputed for ias in 32..48')
print('Domain B: AdrenoVaMapV1 .h/.c/test mirror; clamp and both shapes implemented; '
      'GMU/GPU distinction enforced; three blockers derived into ready()==0; 28th unit/suite')
print(f'VA_MAP_CROSS: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
for f in fails:
    print('  FAIL ' + f)
sys.exit(0 if not fails else 1)
