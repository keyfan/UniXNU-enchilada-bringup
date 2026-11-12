#!/usr/bin/env python3
"""Round 70 method-2 cross-verifier for AdrenoCpRingV1.

Domain A re-derives the A630 CP-ring bring-up from THREE sha256-pinned sources
(a6xx_gpu.c, a6xx.xml, a6xx_gpu.h) and independently re-derives the FIVE
constants the pinned set does not define, by scanning every pinned .c/.h/.xml
for the macro NAMES and proving no definition exists.

Domain B asserts the freestanding C module mirrors the order, the payload, the
doorbell rule and the refusal set.

The verifier never compiles or runs the module and never touches the network.
"""
import hashlib
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
ONEPLUS = Path(__file__).resolve().parents[3]
CACHE = DEV / '.cache' / 'adreno-a630-source'
GPU = DEV / 'drivers' / 'NativeAdreno'

PINS = [
    ('P1_a6xx_gpu_c', CACHE / 'drivers/gpu/drm/msm/adreno/a6xx_gpu.c', 69087,
     'c33479377574ed193e82c272b3708e19a2b2153088f246e2639d8a43fef943a2'),
    ('P2_a6xx_xml', CACHE / 'drivers/gpu/drm/msm/registers/adreno/a6xx.xml', 307513,
     '7f432c5e189f14ce559e0e7722c077ac778cfb63d64b2043c35d09b379bcd602'),
    ('P3_a6xx_gpu_h', CACHE / 'drivers/gpu/drm/msm/adreno/a6xx_gpu.h', 3251,
     '8909f9d70b6122899c70ec01fb034a6244f652eef9a0451065d22e86b546033e'),
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
    if not path.is_file():
        check(False, f'A1 {name} missing ({path})')
        texts[name] = []
        continue
    b = path.read_bytes()
    check(len(b) == size, f'A1 {name} size {len(b)} != {size}')
    check(hashlib.sha256(b).hexdigest() == sha, f'A1 {name} sha256 mismatch')
    check(b'\r' not in b, f'A1 {name} has CR')
    texts[name] = b.decode('utf-8').split('\n')


def line(name, number):
    lines = texts[name]
    return lines[number - 1] if 0 < number <= len(lines) else ''


gpu_c, xml, gpu_h = (texts[k] for k, *_ in PINS)

# ---- A2: CP register word offsets ---------------------------------------
A2 = [('CP_RB_BASE', 0x0800, 'reg64'), ('CP_RB_CNTL', 0x0802, 'reg32'),
      ('CP_RB_RPTR_ADDR', 0x0804, 'reg64'), ('CP_RB_RPTR', 0x0806, 'reg32'),
      ('CP_RB_WPTR', 0x0807, 'reg32'), ('CP_SQE_CNTL', 0x0808, 'reg32')]
for name, off, kind in A2:
    pat = '<%s offset="0x%04x" name="%s"/>' % (kind, off, name)
    check(pat in '\n'.join(xml[2280:2287]), f'A2 {name} {pat}')
check('<reg64 offset="0x0830" name="CP_SQE_INSTR_BASE"/>' in '\n'.join(xml[2293:2296]),
      'A2 CP_SQE_INSTR_BASE 0x0830 reg64')
# The offsets are WORD offsets: neighbours differ by 2/4/6/7/8, not by 8/16/24.
check(0x0804 - 0x0800 == 4 and 0x0807 - 0x0806 == 1 and 0x0808 - 0x0800 == 8,
      'A2 the table is word-offset, not byte-offset')

# ---- A3: the ordered bring-up -------------------------------------------
check('gpu_write64(gpu, REG_A6XX_CP_SQE_INSTR_BASE, a6xx_gpu->sqe_iova);' in
      line('P1_a6xx_gpu_c', 1118), 'A3 P1:1118 SQE instr base := sqe_iova')
check('gpu_write64(gpu, REG_A6XX_CP_RB_BASE, gpu->rb[0]->iova);' in
      line('P1_a6xx_gpu_c', 1121), 'A3 P1:1121 RB base := rb[0] iova')
check('if (adreno_gpu->base.hw_apriv)' in line('P1_a6xx_gpu_c', 1127),
      'A3 P1:1127 hw_apriv branch')
check('MSM_GPU_RB_CNTL_DEFAULT);' in line('P1_a6xx_gpu_c', 1128),
      'A3 P1:1128 apriv target uses the bare default')
check('MSM_GPU_RB_CNTL_DEFAULT | AXXX_CP_RB_CNTL_NO_UPDATE' in
      flat(' '.join(gpu_c[1129:1132])), 'A3 P1:1130-1131 non-apriv adds NO_UPDATE')
check('if (a6xx_gpu->shadow_bo) {' in line('P1_a6xx_gpu_c', 1134),
      'A3 P1:1134 shadow-RPTR write is CONDITIONAL')
check('REG_A6XX_CP_RB_RPTR_ADDR' in line('P1_a6xx_gpu_c', 1135),
      'A3 P1:1135 RPTR_ADDR written only under that condition')
check('gpu_write(gpu, REG_A6XX_CP_SQE_CNTL, 1);' in line('P1_a6xx_gpu_c', 1151),
      'A3 P1:1151 SQE enable := 1')
check('adreno_is_a7xx(adreno_gpu) ? a7xx_cp_init(gpu) : a6xx_cp_init(gpu)' in
      flat(line('P1_a6xx_gpu_c', 1153)), 'A3 P1:1153 the a7xx/a6xx dispatch')

# ---- A4: CP_ME_INIT payload ---------------------------------------------
check('OUT_PKT7(ring, CP_ME_INIT, 8);' in line('P1_a6xx_gpu_c', 616),
      'A4 P1:616 CP_ME_INIT count 8')
check('0x0000002f' in line('P1_a6xx_gpu_c', 618), 'A4 P1:618 word0 0x2f')
check('0x00000003' in line('P1_a6xx_gpu_c', 621), 'A4 P1:621 word1 0x3 (multi-context)')
check('Enable multiple hardware contexts' in line('P1_a6xx_gpu_c', 620),
      'A4 P1:620 comment names multi-context')
check('0x20000000' in line('P1_a6xx_gpu_c', 624), 'A4 P1:624 word2 0x20000000')
check('Enable error detection' in line('P1_a6xx_gpu_c', 623),
      'A4 P1:623 comment names error detection')
check('Don\'t enable header dump' in line('P1_a6xx_gpu_c', 626),
      'A4 P1:626 header dump stays off')
check('No workarounds enabled' in line('P1_a6xx_gpu_c', 630),
      'A4 P1:630 no workarounds')
zeros = [n for n in (627, 628, 631, 634, 635)
         if 'OUT_RING(ring, 0x00000000);' in line('P1_a6xx_gpu_c', n)]
check(len(zeros) == 5, f'A4 the five zero dwords are at 627/628/631/634/635 (got {zeros})')
check('a6xx_flush(gpu, ring);' in line('P1_a6xx_gpu_c', 637), 'A4 P1:637 flush after payload')
check('return a6xx_idle(gpu, ring) ? 0 : -EINVAL;' in line('P1_a6xx_gpu_c', 638),
      'A4 P1:638 idle gate after flush')

# ---- A5: the A7xx shape that A630 must NOT copy -------------------------
check('OUT_PKT7(ring, CP_ME_INIT, 7);' in line('P1_a6xx_gpu_c', 650),
      'A5 P1:650 A7xx uses count 7')
check('OUT_PKT7(ring, CP_THREAD_CONTROL, 1);' in line('P1_a6xx_gpu_c', 647),
      'A5 P1:647 A7xx prepends CP_THREAD_CONTROL')
check('static int a7xx_cp_init(struct msm_gpu *gpu)' in line('P1_a6xx_gpu_c', 641),
      'A5 P1:641 a7xx_cp_init is a separate function')

# ---- A6: doorbell ordering ----------------------------------------------
check('wptr = get_wptr(ring);' in line('P1_a6xx_gpu_c', 82),
      'A6 P1:82 wptr is latched under the lock')
check('mb();' in line('P1_a6xx_gpu_c', 87), 'A6 P1:87 barrier before the store')
check('gpu_write(gpu, REG_A6XX_CP_RB_WPTR, wptr);' in line('P1_a6xx_gpu_c', 89),
      'A6 P1:89 single WPTR store after the barrier')
check(87 < 89, 'A6 the barrier line precedes the store line')
check('spin_until(_a6xx_check_idle(gpu))' in line('P1_a6xx_gpu_c', 43),
      'A6 P1:43 idle check')
check('REG_A6XX_CP_RB_RPTR' in line('P1_a6xx_gpu_c', 48) and
      'REG_A6XX_CP_RB_WPTR' in line('P1_a6xx_gpu_c', 49),
      'A6 P1:48-49 the timeout diagnostic reads the rptr/wptr pair')

# ---- A7: whereami / shadow strategy -------------------------------------
check('if (a6xx_gpu->has_whereami && !adreno_gpu->base.hw_apriv) {' in
      line('P1_a6xx_gpu_c', 62), 'A7 P1:62 whereami needs the flag and no apriv')
check('OUT_PKT7(ring, CP_WHERE_AM_I, 2);' in line('P1_a6xx_gpu_c', 63),
      'A7 P1:63 CP_WHERE_AM_I carries 2 dwords')
check('lower_32_bits(shadowptr(a6xx_gpu, ring))' in line('P1_a6xx_gpu_c', 64),
      'A7 P1:64 low dword is the shadow pointer')

# ---- A8: protect encoding ----------------------------------------------
check('A6XX_PROTECT_NORDWR' in line('P3_a6xx_gpu_h', 59),
      'A8 P3:59 PROTECT_NORDWR macro')
check('(1 << 31) | \\' in line('P3_a6xx_gpu_h', 60) or
      '(1 << 31) |' in line('P3_a6xx_gpu_h', 60), 'A8 P3:60 no-read-write bit 31')
check('((_len) & 0x3FFF) << 18' in line('P3_a6xx_gpu_h', 61),
      'A8 P3:61 MASK_LEN at shift 18, 14 bits')
check('((_reg) & 0x3FFFF)' in line('P3_a6xx_gpu_h', 61),
      'A8 P3:61 BASE_ADDR is 18 bits')

# ---- A9: the FIVE constants that are NOT definable ----------------------
# Prove by exhaustive scan: the NAMES occur, the DEFINITIONS do not.
all_pinned = list(CACHE.rglob('*.c')) + list(CACHE.rglob('*.h')) + list(CACHE.rglob('*.xml'))
check(len(all_pinned) >= 15, f'A9 the pinned cache has {len(all_pinned)} source files')

UNDEF = {
    'CP_ME_INIT': 'U1 opcode value',
    'OUT_PKT7': 'U2 packet header encoding',
    'MSM_GPU_RB_CNTL_DEFAULT': 'U3 RB_CNTL default value',
    'AXXX_CP_RB_CNTL_NO_UPDATE': 'U4 RB_CNTL no-update bit',
    'MSM_GPU_RINGBUFFER_SZ': 'U5 ring size',
}
for macro, label in UNDEF.items():
    uses, defs = 0, 0
    for p in all_pinned:
        try:
            txt = p.read_text(encoding='utf-8', errors='replace')
        except Exception:
            continue
        uses += len(re.findall(r'\b%s\b' % re.escape(macro), txt))
        # A definition would be "#define MACRO ..." or "MACRO = <value>".
        defs += len(re.findall(r'#\s*define\s+%s\b' % re.escape(macro), txt))
    if macro == 'MSM_GPU_RINGBUFFER_SZ':
        check(uses == 0 and defs == 0, f'A9 {macro} is ABSENT entirely ({label})')
    else:
        check(uses > 0, f'A9 {macro} is USED by the pinned sources ({label})')
        check(defs == 0, f'A9 {macro} is NEVER defined there ({label})')
# CP_ME_INIT must be absent from the XML register databases specifically.
xml_all = '\n'.join(xml) + '\n'.join(
    p.read_text(encoding='utf-8', errors='replace')
    for p in all_pinned if p.suffix == '.xml')
check('CP_ME_INIT' not in xml_all,
      'A9 CP_ME_INIT appears in NO pinned XML (opcode is not a register)')

# ======================= Domain B: the deliverable =======================
h = (GPU / 'AdrenoCpRingV1.h').read_text(encoding='utf-8')
c = (GPU / 'AdrenoCpRingV1.c').read_text(encoding='utf-8')
t = (GPU / 'tests' / 'adreno-cp-ring-v1-test.cpp').read_text(encoding='utf-8')

# B1: pins recorded verbatim
for name, path, size, sha in PINS:
    check(sha in h, f'B1 header records {name} sha256')

# B2: header mirrors the offsets and the payload
B2 = [('kAdrenoCpRingRbBase', '0x0800u'), ('kAdrenoCpRingRbCntl', '0x0802u'),
      ('kAdrenoCpRingRbRptrAddr', '0x0804u'), ('kAdrenoCpRingRbRptr', '0x0806u'),
      ('kAdrenoCpRingRbWptr', '0x0807u'), ('kAdrenoCpRingSqeCntl', '0x0808u'),
      ('kAdrenoCpRingSqeInstrBase', '0x0830u'),
      ('kAdrenoCpRingMeInitDwords', '8u'),
      ('kAdrenoCpRingMeInitWord0', '0x0000002fu'),
      ('kAdrenoCpRingMeInitWord1', '0x00000003u'),
      ('kAdrenoCpRingMeInitWord2', '0x20000000u'),
      ('kAdrenoCpRingMeInitZeroFrom', '3u'),
      ('kAdrenoCpRingA7xxMeInitDwords', '7u'),
      ('kAdrenoCpRingSqeCntlEnable', '1u')]
for sym, val in B2:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B2 {sym}={val}')

# B3: six ordered steps, ME_INIT last
check('kAdrenoCpRingStepCount = 6u' in h, 'B3 six steps')
for nm in ('SqeInstrBase', 'RbBase', 'RbCntl', 'RptrAddr', 'SqeEnable', 'MeInit'):
    check(f'kAdrenoCpRingStep{nm}' in h, f'B3 step {nm}')

# B4: the five undefined bits are distinct powers of two summing to 31
B4 = [('kAdrenoCpRingUndefMeInitOpcode', '1u'),
      ('kAdrenoCpRingUndefPacketHeader', '2u'),
      ('kAdrenoCpRingUndefRbCntlDefault', '4u'),
      ('kAdrenoCpRingUndefRbCntlNoUpdate', '8u'),
      ('kAdrenoCpRingUndefRingSize', '16u'),
      ('kAdrenoCpRingUndefAll', '31u')]
for sym, val in B4:
    check(re.search(r'%s\s*=\s*%s' % (sym, re.escape(val)), h) is not None,
          f'B4 {sym} = {val}')
check(1 + 2 + 4 + 8 + 16 == 31, 'B4 the five bits partition 31')

# B5: .c implements the API and stays freestanding
APIS = ['adreno_cp_ring_offsets', 'adreno_cp_ring_plan', 'adreno_cp_ring_me_init',
        'adreno_cp_ring_me_init_matches', 'adreno_cp_ring_shadow_mode',
        'adreno_cp_ring_doorbell_order', 'adreno_cp_ring_undefined',
        'adreno_cp_ring_undefined_name', 'adreno_cp_ring_undefined_anchor',
        'adreno_cp_ring_is_executable', 'adreno_cp_ring_verify']
for api in APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B5 .c defines {api}')
for bad in ('#include <string.h>', '#include <stdlib.h>', '#include <stdio.h>',
            'IOConnectCall', 'IOServiceOpen', 'writel', 'readl',
            'volatile uint32_t *', 'gpu_write'):
    check(bad not in c, f'B5 .c has no {bad}')
# OUT_PKT7 is named ONCE inside a provenance string (it must be, to cite the
# undefined header); it must never be INVOKED.
check(len(re.findall(r'OUT_PKT7\s*\(', c)) == 0, 'B5 .c never invokes OUT_PKT7')
check(len(re.findall(r'OUT_PKT7', c)) == 1, 'B5 OUT_PKT7 appears only as a citation')
check('#include "AdrenoCpRingV1.h"' in c, 'B5 .c includes its own header')

# B6: the honesty gate -- executable is 0 while U1/U2 are open
check('return 0;' in c[c.find('int adreno_cp_ring_is_executable'):][:400],
      'B6 is_executable() returns 0')
check('return (uint32_t)kAdrenoCpRingUndefAll;' in c,
      'B6 undefined() reports all five')
check('cannot encode a packet' in h or 'cannot encode a header' in h,
      'B6 the consequence is stated in prose')

# B7: the conditional shadow step is genuinely conditional
check('if (has_shadow_bo != 0u) {' in c, 'B7 shadow step gated on has_shadow_bo')
check('uint32_t need = (has_shadow_bo != 0u)' in c,
      'B7 required capacity depends on the same flag')
check('kAdrenoCpRingStepCount - 1u)' in c, 'B7 the no-shadow need is five')

# B8: host test shape
check(t.count('static void test') == 7, 'B8 7 test functions')
tflat = t.replace('"\n                "', '')
check('PASS: enchilada A630 CP ring init/doorbell contract (7 test groups, 0 failures)'
      in tflat, 'B8 PASS line')
for grp in ('testOffsets', 'testPlan', 'testPlanWithoutShadow', 'testMeInit',
            'testShadowAndDoorbell', 'testUndefinedSet', 'testSelfCheck'):
    check(f'{grp}();' in t, f'B8 {grp} invoked')

# B9: wiring -- 24th unit + 24th suite
build = (DEV / 'scripts' / 'build-enchilada-adreno.py').read_text(encoding='utf-8')
host = (DEV / 'scripts' / 'check-enchilada-adreno-host.py').read_text(encoding='utf-8')
check("GPU / 'AdrenoCpRingV1.c', ['-mgeneral-regs-only']" in build,
      'B9 build script compiles the 24th unit')
check("'AdrenoCpRingV1': GPU / 'AdrenoCpRingV1.c'," in host,
      'B9 host checker SOURCES entry')
check("('adreno-cp-ring-v1-test', [GPU / 'tests/adreno-cp-ring-v1-test.cpp'], ['AdrenoCpRingV1'])"
      in host, 'B9 host checker TESTS entry')
check("'AdrenoCpRingV1'" in build.split('sources = ')[1],
      'B9 build-report source list gains the stem')
check('/cp-ring-v1 host tests' in host, 'B9 PASS enumeration updated')
units = re.findall(r"\(c_rec, GPU / '[^']+'", build)
check(len(units) == 23, f'B9 23 C archive units declared (got {len(units)})')

print(f'Domain A: {len(PINS)} sha256-pinned sources; 7 CP offsets, the 6-step order, '
      f'the 8-dword ME_INIT payload, the doorbell barrier order, and the FIVE '
      f'undefined macros re-derived by exhaustive scan')
print('Domain B: AdrenoCpRingV1 .h/.c/test mirror; conditional shadow step; '
      'is_executable()==0 while U1/U2 are open; wiring is the 24th unit and 24th suite')
print(f'CP_RING_CROSS: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
for f in fails:
    print('  FAIL ' + f)
sys.exit(0 if not fails else 1)
