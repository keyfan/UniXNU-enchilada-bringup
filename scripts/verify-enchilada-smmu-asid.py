#!/usr/bin/env python3
"""Round 102 cross-verifier for AdrenoSmmuAsidV1 -- the ASID / context-bank allocator.

Domain A re-derives every constant from the sha256-pinned v6.12 sources:
  * `asid = cbndx` (arm-smmu.c:795), so the allocator's real subject is the CONTEXT
    BANK BITMAP, not a free-running ASID space;
  * `__arm_smmu_alloc_bitmap()` (arm-smmu.h, inline) is FIRST-FIT from `start` with
    `find_next_zero_bit`, returning -ENOSPC when full;
  * `start = num_s2_context_banks` for a stage-1 domain (arm-smmu.c:734);
  * `num_context_banks = FIELD_GET(ID1.NUMCB)` (arm-smmu.c:1866), an 8-bit field;
  * the upstream bitmap is clamped to `ARM_SMMU_MAX_CBS = 128` (arm-smmu.h);
  * `TTBRn_ASID` is GENMASK_ULL(63,48) and `S2CR_CBNDX` is GENMASK(7,0);
  * `GR1_CBAR(n) = 0x0 + (n << 2)`.

Domain B asserts the module mirrors those and records what it still cannot do.

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
]

checks = 0
fails = []


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)
        print('FAIL:', label)
    else:
        print('ok  ', label)


def flat(s):
    return ' '.join(s.split())


texts = {}
for name, path, size, sha in PINS:
    b = path.read_bytes() if path.is_file() else b''
    check(len(b) == size, 'A1 %s size %d != %d' % (name, len(b), size))
    check(hashlib.sha256(b).hexdigest() == sha, 'A1 %s sha256 mismatch' % name)
    check(b'\r' not in b, 'A1 %s has CR' % name)
    texts[name] = b.decode('utf-8').split('\n')


def line(name, number):
    lines = texts[name]
    return lines[number - 1] if 0 < number <= len(lines) else ''


smmu_h = texts['P1_arm_smmu_h']
smmu_c = texts['P2_arm_smmu_c']

# ---- A2: asid = cbndx, the load-bearing line ---------------------------
check('cfg->asid = cfg->cbndx;' in line('P2_arm_smmu_c', 795),
      'A2 P2:795 the ASID IS the context-bank index (not a free-running space)')
# The stage-2 sibling assigns vmid = cbndx + 1 on the line above, which is why the
# two must not be conflated.
check('cfg->vmid = cfg->cbndx + 1;' in line('P2_arm_smmu_c', 793),
      'A2 P2:793 the VMID path is DIFFERENT (cbndx + 1), so they are not one allocator')

# ---- A3: the first-fit bitmap allocator --------------------------------
# P1:464-475 (verified line numbers, not an estimate: an earlier revision used
# 405-420 and every A3 assertion failed against unrelated text).
check('__arm_smmu_alloc_bitmap(unsigned long *map, int start, int end)' in
      line('P1_arm_smmu_h', 464),
      'A3 the allocator signature takes (map, start, end)')
alloc = flat(' '.join(smmu_h[463:475]))
check('find_next_zero_bit(map, end, start)' in alloc,
      'A3 A FIRST-FIT search: find_next_zero_bit restarts at `start` every call')
check('-ENOSPC' in alloc, 'A3 A full range returns -ENOSPC')
check('test_and_set_bit(idx, map)' in alloc,
      'A3 the bit is set only after the zero test, so the loop is a retry not a scan')

# ---- A4: the stage-1 start offset -------------------------------------
check('start = smmu->num_s2_context_banks;' in line('P2_arm_smmu_c', 734),
      'A4 P2:734 a stage-1 domain starts ABOVE the stage-2 reserve')

# ---- A5: the two ceilings --------------------------------------------
check('#define ARM_SMMU_MAX_CBS' in line('P1_arm_smmu_h', 253) and
      '128' in line('P1_arm_smmu_h', 253),
      'A5 P1:253 the upstream bitmap is clamped to 128 entries')
check('GENMASK(7, 0)' in line('P1_arm_smmu_h', 63),
      'A5 P1:63 ID1.NUMCB is GENMASK(7,0): an 8-bit count, so <= 256')
check('num_context_banks = FIELD_GET(ARM_SMMU_ID1_NUMCB, id)' in
      flat(' '.join(smmu_c[1864:1868])),
      'A5 P2:1866 num_context_banks is a PROBE-TIME read of ID1')

# ---- A6: the field widths --------------------------------------------
check('GENMASK_ULL(63, 48)' in line('P1_arm_smmu_h', 169),
      'A6 P1:169 TTBRn_ASID is a 16-bit field at bits 63..48')
check('GENMASK(7, 0)' in line('P1_arm_smmu_h', 120),
      'A6 P1:120 S2CR_CBNDX is 8 bits')
check('(0x0 + ((n) << 2))' in line('P1_arm_smmu_h', 123),
      'A6 P1:123 GR1_CBAR(n) = 0x0 + (n << 2)')
check('ARM_SMMU_CB_CONTEXTIDR' in line('P1_arm_smmu_h', 191),
      'A6 P1:191 CONTEXTIDR offset is declared')

# ---- A7: ID1.NUMCB is NOT an id_size encoding ------------------------
# The ID2 path uses arm_smmu_id_size_to_bits(); ID1.NUMCB is a raw count. A module
# that applied the size table here would be wrong by a wide margin.
check('arm_smmu_id_size_to_bits' not in flat(' '.join(smmu_c[1864:1868])),
      'A7 P2:1866 does NOT pass ID1.NUMCB through id_size_to_bits')

# ======================= Domain B: the deliverable =======================
h = (GPU / 'AdrenoSmmuAsidV1.h').read_text(encoding='utf-8')
c = (GPU / 'AdrenoSmmuAsidV1.c').read_text(encoding='utf-8')
t = (GPU / 'tests' / 'adreno-smmu-asid-v1-test.cpp').read_text(encoding='utf-8')

# The header must record EVERY pin it derives from, or a later edit cannot tell
# which revision a constant came from.
for name, path, size, sha in PINS:
    check(sha in h, 'B1 header records %s sha256' % name)
check('P1:795' in h or 'P2:795' in h, 'B1 header cites the asid=cbndx line')
check('ARM_SMMU_MAX_CBS' in h or 'kAdrenoAsidMaxCbs' in h,
      'B1 header names the 128 ceiling')

B2 = [('kAdrenoAsidId1NumcbMask', '0x000000ffu'),
      ('kAdrenoAsidId1MaxBanks', '256u'),
      ('kAdrenoAsidMaxCbs', '128u'),
      ('kAdrenoAsidFieldBits', '16u'),
      ('kAdrenoAsidFieldMask', '0xffffull'),
      ('kAdrenoAsidLow8Mask', '0xffu'),
      ('kAdrenoAsidBitmapWords', '2u')]
for sym, val in B2:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          'B2 %s=%s' % (sym, val))

APIS = ['adreno_asid_capacity_from_id1', 'adreno_asid_init', 'adreno_asid_alloc',
        'adreno_asid_free', 'adreno_asid_is_owned', 'adreno_asid_count',
        'adreno_asid_ttbr_asid_field', 'adreno_asid_contextidr',
        'adreno_asid_s2cr_cbndx', 'adreno_asid_cbar_offset',
        'adreno_asid_missing', 'adreno_asid_input_name',
        'adreno_asid_input_anchor', 'adreno_asid_ready', 'adreno_asid_verify']
for api in APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          'B3 .c defines %s' % api)
for bad in ('#include <string.h>', '#include <stdlib.h>', '#include <stdio.h>',
            'IOConnectCall', 'IOServiceOpen', 'writel(', 'readl(',
            'volatile uint32_t *', 'malloc('):
    check(bad not in c, 'B3 .c has no %s' % bad)

# ---- B4: FIRST-FIT, the property that distinguishes it from a counter ----
alloc_body = c[c.find('int adreno_asid_alloc('):]
alloc_body = alloc_body[:alloc_body.find('\n}\n')]
check('for (i = b->start; i < b->capacity; ++i)' in alloc_body,
      'B4 the scan starts at `start` (the stage-2 reserve), not at 0')
check('if (adreno_asid_is_owned(b, i) == 0)' in alloc_body,
      'B4 the first CLEAR bit is taken -- first-fit, as upstream does')
check('*cbndx = 0xffffffffu;' in alloc_body,
      'B4 a refusal CLEARS the out-parameter so a stale index is never read')
check('*cbndx = 0xffffffffu;' in alloc_body and
      alloc_body.find('*cbndx = 0xffffffffu;') < alloc_body.find('for (i = b->start'),
      'B4 the out-parameter is cleared BEFORE the scan, not only on failure')

# ---- B5: the clamp is honoured, and zero is refused --------------------
cap_body = c[c.find('uint32_t adreno_asid_capacity_from_id1('):]
cap_body = cap_body[:cap_body.find('\n}\n')]
check('kAdrenoAsidMaxCbs' in cap_body, 'B5 the capacity honours ARM_SMMU_MAX_CBS')
check('count == 0u' in flat(cap_body), 'B5 zero banks is refused, not returned')
check('kAdrenoAsidId1NumcbMask' in cap_body,
      'B5 only the low 8 bits of ID1 are the count')

# ---- B6: the derive-only boundary ------------------------------------
check('adreno_asid_missing() == 0u ? 1 : 0' in c,
      'B6 ready() is DERIVED from the missing mask')
check('mask |= kAdrenoAsidInputId1;' in c, 'B6 the ID1 read is recorded missing')
check('mask |= kAdrenoAsidInputCaller;' in c,
      'B6 the absent bank owner is recorded missing')
check('kAdrenoAsidInputAll = 3u' in h, 'B6 the all-mask is two bits')

# ---- B7: the test -----------------------------------------------------
tf = re.findall(r'static void (test[A-Za-z0-9]+)\(\)', t)
check(len(tf) == 8, 'B7 8 test groups (got %d: %r)' % (len(tf), tf))
main_body = t[t.find('int main()'):]
for fn in ('testCeilings', 'testCapacity', 'testInitRefusals', 'testFirstFit',
           'testExhaustion', 'testFreeRefusals', 'testTuple', 'testMissingAndVerify'):
    check(fn in tf, 'B7 %s is declared' % fn)
    check('%s();' % fn in main_body, 'B7 main() calls %s()' % fn)
tflat = flat(re.sub(r'"\s*"', '', t))
check('PASS: enchilada A630 SMMU ASID allocator contract '
      '(8 test groups, 0 failures)' in tflat, 'B7 PASS line')
# The first-fit test must actually exercise the restart, not just the first scan.
check('FREE THE LOWER ONE AND ALLOCATE AGAIN' in t,
      'B7 the test documents why the restart matters')
check('adreno_asid_free(&b, 2u) == 0' in t and
      'adreno_asid_alloc(&b, &cb) == 0 && cb == 2u' in t,
      'B7 the test proves a freed LOWER index is reused before a higher free one')
# The stage-2 reserve must be asserted, not assumed.
check('adreno_asid_init(&b, 8u, 2u) == 0' in t,
      'B7 the test initialises with a non-zero start')
check('cb == 2u' in t, 'B7 the first allocation is `start`, not 0')

# ---- B8: wiring ------------------------------------------------------
build = (DEV / 'scripts' / 'build-enchilada-adreno.py').read_text(encoding='utf-8')
host = (DEV / 'scripts' / 'check-enchilada-adreno-host.py').read_text(encoding='utf-8')
check("GPU / 'AdrenoSmmuAsidV1.c', ['-mgeneral-regs-only']" in build,
      'B8 build script compiles the new unit')
check("'AdrenoSmmuAsidV1': GPU / 'AdrenoSmmuAsidV1.c'," in host,
      'B8 host SOURCES entry')
check("('adreno-smmu-asid-v1-test'" in host, 'B8 host TESTS entry')
check('smmu-asid-v1 host tests' in host,
      'B8 PASS enumeration lists the suite')
units = len(re.findall(r"\(c_rec, GPU / '[^']+'", build))
print('   (C archive units now %d)' % units)

raw = (GPU / 'AdrenoSmmuAsidV1.c').read_bytes()
check(b'\r' not in raw, 'B9 module is pure LF')
check(not raw.startswith(b'\xef\xbb\xbf'), 'B9 module has no BOM')

print()
print('%d/%d checks passed' % (checks - len(fails), checks))
if fails:
    print('FAIL: ASID allocator cross-verifier (%d failures)' % len(fails))
    sys.exit(1)
print('PASS: ASID allocator cross-verifier '
      '(asid=cbndx, first-fit from start, both ceilings, 16-bit field)')
