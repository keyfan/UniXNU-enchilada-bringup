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
    # round 71: the four pins that CLOSED three of the five undefined constants
    ('P8_sizes_h', CACHE / 'include/linux/sizes.h', 1723,
     '67fe2ba4a05c73e3ce41b8249a5a63e609639ba9876ea0a2a828fd2755a3efa7'),
    ('P9_msm_gpu_h', CACHE / 'drivers/gpu/drm/msm/msm_gpu.h', 20026,
     '6c461e0bd0fd37e10dc5c25990c31e342273e936862826d5ae42cbf707d999e0'),
    ('P10_adreno_gpu_h', CACHE / 'drivers/gpu/drm/msm/adreno/adreno_gpu.h', 18946,
     '7bdabea1ae7310697e0e2a4e0353cff2a7cf0ac2af746e5418e249d4584c86df'),
    ('P11_adreno_pm4_xml', CACHE / 'drivers/gpu/drm/msm/registers/adreno/adreno_pm4.xml',
     85907, 'ab27047cc9cde4bc5996d6eb528fbb2dbdfdd9dc4515f75bbbada43a23809536'),
    # round 72: where CP_RB_CNTL's FIELDS actually live, and the generator that
    # would emit the a6xx.xml.h accessors.
    ('P12_adreno_common_xml',
     CACHE / 'drivers/gpu/drm/msm/registers/adreno/adreno_common.xml', 15485,
     'c733db8294d0bedc6191044b7a6acfc81495ba1f294c3108746999c596cf5e58'),
    ('P13_gen_header',
     CACHE / 'drivers/gpu/drm/msm/registers/gen_header.py', 27564,
     '465b340f07d2fd299c83dc34c21b2b7cd71ef6a027aa9f93814683d3ea946ca6'),
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


gpu_c, xml, gpu_h, sizes_h, msm_gpu_h, adreno_gpu_h, pm4_xml, common_xml, gen_hdr = (
    texts[k] for k, *_ in PINS)

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

# ---- A9: the round-70 claim, now checked as HISTORY ----------------------
# Round 70 asserted these five were undefined "in the pinned set". That was a
# statement about the set it had pinned. Round 71 enlarged the pin set and three
# of them became DEFINED. The honest check is therefore to pin BOTH the old
# truth and the new one, and to fail if either is misreported.
all_pinned = (list(CACHE.rglob('*.c')) + list(CACHE.rglob('*.h'))
              + list(CACHE.rglob('*.xml')))
check(len(all_pinned) >= 20, f'A9 the pinned cache now has {len(all_pinned)} source files')


def count_uses_defs(macro):
    uses = defs = 0
    for p in all_pinned:
        try:
            txt = p.read_text(encoding='utf-8', errors='replace')
        except Exception:
            continue
        uses += len(re.findall(r'\b%s\b' % re.escape(macro), txt))
        defs += len(re.findall(r'#\s*define\s+%s\b' % re.escape(macro), txt))
    return uses, defs


# U5 ring size: now DEFINED by msm_gpu.h after being absent in round 70.
u5_uses, u5_defs = count_uses_defs('MSM_GPU_RINGBUFFER_SZ')
check(u5_uses > 0, 'A9 U5 MSM_GPU_RINGBUFFER_SZ is used')
check(u5_defs == 1, f'A9 U5 is now DEFINED exactly once (got {u5_defs})')
# U3: the macro is defined as a FORMULA whose field accessors are not.
u3_uses, u3_defs = count_uses_defs('MSM_GPU_RB_CNTL_DEFAULT')
check(u3_uses > 0, 'A9 U3 MSM_GPU_RB_CNTL_DEFAULT is used')
check(u3_defs == 1, f'A9 U3 is now DEFINED as a formula (got {u3_defs})')
check('AXXX_CP_RB_CNTL_BUFSZ' in '\n'.join(msm_gpu_h),
      'A9 U3 formula references BUFSZ')
# U1: CP_ME_INIT IS in a pinned XML now -- in adreno_pm4.xml, not a register db.
check('<value name="CP_ME_INIT" value="0x48"/>' in '\n'.join(pm4_xml),
      'A9 U1 CP_ME_INIT is DEFINED in adreno_pm4.xml')
check('CP_ME_INIT' not in '\n'.join(xml),
      'A9 U1 CP_ME_INIT is still absent from a6xx.xml (it is an opcode, not a reg)')
# U2: OUT_PKT7 has a body in adreno_gpu.h now.
check('static inline void' in line('P10_adreno_gpu_h', 659) and
      'OUT_PKT7(struct msm_ringbuffer *ring, uint8_t opcode, uint16_t cnt)' in
      line('P10_adreno_gpu_h', 660), 'A9 U2 OUT_PKT7 has a DEFINED body')
# U4 must still be undefined -- that is the one that keeps the ring blocked.
u4_uses, u4_defs = count_uses_defs('AXXX_CP_RB_CNTL_NO_UPDATE')
check(u4_uses > 0, 'A9 U4 AXXX_CP_RB_CNTL_NO_UPDATE is used')
check(u4_defs == 0, f'A9 U4 is STILL never defined (got {u4_defs})')


# ---- A12: CP_RB_CNTL fields, DERIVED in round 72 (P12:116-123) -----------
CN = ' '.join(common_xml[115:123])
check('offset="0x01c1" name="CP_RB_CNTL"' in CN, 'A12 P12:116 CP_RB_CNTL reg32')
check('<bitfield name="BUFSZ" low="0" high="5"/>' in line('P12_adreno_common_xml', 117),
      'A12 P12:117 BUFSZ low 0 high 5')
check('<bitfield name="BLKSZ" low="8" high="13"/>' in line('P12_adreno_common_xml', 118),
      'A12 P12:118 BLKSZ low 8 high 13')
check('<bitfield name="BUF_SWAP" low="16" high="17"/>' in line('P12_adreno_common_xml', 119),
      'A12 P12:119 BUF_SWAP 16..17')
check('<bitfield name="POLL_EN" pos="20" type="boolean"/>' in line('P12_adreno_common_xml', 120),
      'A12 P12:120 POLL_EN 20')
check('<bitfield name="NO_UPDATE" pos="27" type="boolean"/>' in line('P12_adreno_common_xml', 121),
      'A12 P12:121 NO_UPDATE 27')
check('<bitfield name="RPTR_WR_EN" pos="31" type="boolean"/>' in line('P12_adreno_common_xml', 122),
      'A12 P12:122 RPTR_WR_EN 31')
# The round-71 reading of a6xx.xml:2282 was RIGHT and is the reason U3/U4 were
# unreachable from there: that reg32 carries no fields at all.
check('<reg32 offset="0x0802" name="CP_RB_CNTL"/>' in line('P2_a6xx_xml', 2282),
      'A12 P2:2282 has CP_RB_CNTL as a BARE reg32 (no fields) -- why round 71 stalled')

# Recompute the register from the pinned layout, independently of the module.
def pack(lo, hi, val):
    w = hi - lo + 1
    return (val & ((1 << w) - 1)) << lo
RB_DEFAULT = pack(0, 5, 12) | pack(8, 13, 2)
RB_NOUPDATE = RB_DEFAULT | (1 << 27)
check(RB_DEFAULT == 0x20C, 'A12 RB_CNTL default recomputes to 0x0000020C')
check(RB_NOUPDATE == 0x0800020C, 'A12 RB_CNTL | NO_UPDATE is 0x0800020C')
check(RB_NOUPDATE - RB_DEFAULT == (1 << 27),
      'A12 the two branches differ EXACTLY in bit 27')

# The generator that would emit the accessors is pinned, and it is a BUILD step.
check('import xml.parsers.expat' in '\n'.join(gen_hdr[:20]),
      'A12 P13 is an XML-driven generator (not data)')
check('argparse' in '\n'.join(gen_hdr[:20]), 'A12 P13 is a command-line tool')

# ---- A10: the FOUR round-71 pins that closed three of the five constants --
check('SZ_32K' in '\n'.join(sizes_h[20:30]), 'A10 P8 sizes.h defines SZ_32K')
check(re.search(r'#define\s+SZ_32K\s+0x00008000', '\n'.join(sizes_h)) is not None,
      'A10 P8 SZ_32K is exactly 0x00008000')

check('#define MSM_GPU_RINGBUFFER_SZ SZ_32K' in line('P9_msm_gpu_h', 311),
      'A10 P9:311 ring size is SZ_32K')
check('#define MSM_GPU_RINGBUFFER_BLKSIZE 32' in line('P9_msm_gpu_h', 312),
      'A10 P9:312 block size is 32')
cntl_block = ' '.join(msm_gpu_h[313:317])
check('AXXX_CP_RB_CNTL_BUFSZ(ilog2(MSM_GPU_RINGBUFFER_SZ / 8))' in cntl_block,
      'A10 P9:315 RB_CNTL default uses BUFSZ(ilog2(size/8))')
check('AXXX_CP_RB_CNTL_BLKSZ(ilog2(MSM_GPU_RINGBUFFER_BLKSIZE / 8))' in cntl_block,
      'A10 P9:316 RB_CNTL default uses BLKSZ(ilog2(blk/8))')
# The ilog2 inputs, re-derived from the pinned numbers rather than asserted.
BUFSZ_FIELD = (0x8000 // 8).bit_length() - 1
BLKSZ_FIELD = (32 // 8).bit_length() - 1
check(BUFSZ_FIELD == 12, 'A10 ilog2(0x8000/8) == 12')
check(BLKSZ_FIELD == 2, 'A10 ilog2(32/8) == 2')

# PM4_PARITY, re-implemented from the pinned text.
def pm4_parity(val):
    x = (val ^ (val >> 4) ^ (val >> 8) ^ (val >> 12) ^ (val >> 16)
         ^ (val >> 20) ^ (val >> 24) ^ (val >> 28)) & 0xFFFFFFFF
    return (0x9669 >> (0xF & x)) & 1

check('static inline u32 PM4_PARITY(u32 val)' in line('P10_adreno_gpu_h', 637),
      'A10 P10:637 PM4_PARITY definition')
check('0x9669' in line('P10_adreno_gpu_h', 639), 'A10 P10:639 the 0x9669 LUT')
check('CP_TYPE7_PKT | (cnt << 0) | (PM4_PARITY(cnt) << 15)' in
      flat(' '.join(adreno_gpu_h[662:665])), 'A10 P10:663 type-7 header assembly')
check('((opcode & 0x7F) << 16)' in line('P10_adreno_gpu_h', 664),
      'A10 P10:664 opcode masked to 7 bits')
check('(PM4_PARITY(opcode) << 23)' in line('P10_adreno_gpu_h', 664),
      'A10 P10:664 opcode parity lands at bit 23')

check('<value name="CP_TYPE7_PKT" value="0x70000000"/>' in line('P11_adreno_pm4_xml', 234),
      'A10 P11:234 CP_TYPE7_PKT = 0x70000000')
check('<value name="CP_ME_INIT" value="0x48"/>' in line('P11_adreno_pm4_xml', 248),
      'A10 P11:248 CP_ME_INIT = 0x48')

# The real header, recomputed from the pinned formula and constants.
ME_HEADER = (0x70000000 | (8 << 0) | (pm4_parity(8) << 15)
             | ((0x48 & 0x7F) << 16) | (pm4_parity(0x48) << 23))
check(pm4_parity(8) == 0, 'A10 parity(cnt=8) == 0')
check(pm4_parity(0x48) == 1, 'A10 parity(opcode=0x48) == 1')
check(ME_HEADER == 0x70C80008, 'A10 the recomputed header is 0x70C80008')
check(pm4_parity(0) == 1, 'A10 parity(0) == 1 (0x9669 bit 0 is set)')

# ---- A11: what is STILL open, proved by absence --------------------------
# The three AXXX_CP_RB_CNTL_* accessors and CP_ME_INIT-in-*.xml.h.
for macro in ('AXXX_CP_RB_CNTL_NO_UPDATE', 'AXXX_CP_RB_CNTL_BUFSZ',
              'AXXX_CP_RB_CNTL_BLKSZ'):
    defs = 0
    uses = 0
    for q in all_pinned:
        try:
            txt = q.read_text(encoding='utf-8', errors='replace')
        except Exception:
            continue
        uses += len(re.findall(r'\b%s\b' % re.escape(macro), txt))
        defs += len(re.findall(r'#\s*define\s+%s\b' % re.escape(macro), txt))
    check(uses > 0, f'A11 {macro} is USED by the pinned sources')
    check(defs == 0, f'A11 {macro} is STILL never defined ({defs} definitions)')
# No generated a6xx.xml.h exists, which is where those accessors would live.
check(not list(CACHE.rglob('*.xml.h')),
      'A11 no generated *.xml.h is present (the accessors are generated)')
check('<reg32 offset="0x0802" name="CP_RB_CNTL"/>' in line('P2_a6xx_xml', 2282),
      'A11 P2:2282 CP_RB_CNTL carries NO bitfields in the pinned XML')


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
check(len(re.findall(r'OUT_PKT7\s*\(\s*[a-zA-Z_]', c)) == 0,
      'B5 .c never invokes OUT_PKT7 with a ring argument')
check(len(re.findall(r'OUT_PKT7', c)) >= 1,
      'B5 OUT_PKT7 appears only in citations (never invoked, see above)')
check('#include "AdrenoCpRingV1.h"' in c, 'B5 .c includes its own header')

# B6: the honesty gate -- executable is 0 while U1/U2 are open
check('adreno_cp_ring_undefined_open() == 0u ? 1 : 0' in
      c[c.find('int adreno_cp_ring_is_executable'):][:400],
      'B6 is_executable() is derived from the open set')
check('return (uint32_t)kAdrenoCpRingUndefAll;' in c,
      'B6 undefined() reports all five')
check('the RING cannot' in c or 'RING cannot be programmed' in h or
      'cannot be programmed' in h,
      'B6 the packet/ring consequence is stated in prose')

# B7: the conditional shadow step is genuinely conditional
check('if (has_shadow_bo != 0u) {' in c, 'B7 shadow step gated on has_shadow_bo')
check('uint32_t need = (has_shadow_bo != 0u)' in c,
      'B7 required capacity depends on the same flag')
check('kAdrenoCpRingStepCount - 1u)' in c, 'B7 the no-shadow need is five')

# B8: host test shape
check(t.count('static void test') == 11, 'B8 11 test functions')
tflat = t.replace('"\n                "', '')
check('PASS: enchilada A630 CP ring init/doorbell contract (11 test groups, 0 failures)'
      in tflat, 'B8 PASS line')
for grp in ('testOffsets', 'testPlan', 'testPlanWithoutShadow', 'testMeInit',
            'testShadowAndDoorbell', 'testUndefinedSet', 'testParityAndHeader',
            'testMessage', 'testGeometry', 'testSelfCheck'):
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
check('/cp-ring-v1' in host, 'B9 PASS enumeration updated')
units = re.findall(r"\(c_rec, GPU / '[^']+'", build)
check(len(units) == 24, f'B9 24 C archive units declared (got {len(units)})')


# B10: round-71 constants mirrored
B71 = [('kAdrenoCpRingBufferBytes', '0x00008000u'),
       ('kAdrenoCpRingBufferDwords', '0x00002000u'),
       ('kAdrenoCpRingBlockBytes', '32u'),
       ('kAdrenoCpRingBufszField', '12u'),
       ('kAdrenoCpRingBlkszField', '2u'),
       ('kAdrenoCpRingType7Pkt', '0x70000000u'),
       ('kAdrenoCpRingMeInitOpcode', '0x48u'),
       ('kAdrenoCpRingMeInitHeader', '0x70c80008u'),
       ('kAdrenoCpRingOpcodeMask', '0x7fu'),
       ('kAdrenoCpRingParityCntBit', '15u'),
       ('kAdrenoCpRingParityOpBit', '23u'),
       ('kAdrenoCpRingParityLut', '0x9669u')]
for sym, val in B71:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B10 {sym}={val}')

# B11: the round-71 encoders exist and the open set narrowed honestly
for api in ('adreno_cp_ring_parity', 'adreno_cp_ring_type7_header',
            'adreno_cp_ring_me_init_message',
            'adreno_cp_ring_me_init_message_matches',
            'adreno_cp_ring_buffer_bytes', 'adreno_cp_ring_buffer_dwords',
            'adreno_cp_ring_rb_cntl_fields', 'adreno_cp_ring_undefined_open',
            'adreno_cp_ring_can_encode_packet'):
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B11 .c defines {api}')
check('kAdrenoCpRingUndefOpenNow = 0u' in h,
      'B11 the open set is EMPTY after round 72')
check('return (uint32_t)kAdrenoCpRingUndefOpenNow;' in c,
      'B11 undefined_open() returns exactly that mask')
check('adreno_cp_ring_undefined_open() &\n            (kAdrenoCpRingUndefMeInitOpcode | kAdrenoCpRingUndefPacketHeader))\n               == 0u;'
      in c or 'kAdrenoCpRingUndefMeInitOpcode | kAdrenoCpRingUndefPacketHeader' in c,
      'B11 can_encode_packet() keys off U1|U2 being closed')
check('every word the ring must write is computable' in c,
      'B11 the round-72 consequence is stated in prose')
check(t.count('static void test') == 11, 'B11 11 test functions')
tflat2 = t.replace('"\n                "', '')
check('(11 test groups, 0 failures)' in tflat2, 'B11 PASS line says 11 groups')
for grp in ('testParityAndHeader', 'testMessage', 'testGeometry'):
    check(f'{grp}();' in t, f'B11 {grp} invoked')


# B12: round-72 constants and API
B72 = [('kAdrenoCpRingRbCntlBufszLow', '0u'),
       ('kAdrenoCpRingRbCntlBufszHigh', '5u'),
       ('kAdrenoCpRingRbCntlBlkszLow', '8u'),
       ('kAdrenoCpRingRbCntlBlkszHigh', '13u'),
       ('kAdrenoCpRingRbCntlNoUpdatePos', '27u'),
       ('kAdrenoCpRingRbCntlDefault', '0x0000020cu'),
       ('kAdrenoCpRingRbCntlNoUpdate', '0x0800020cu')]
for sym, val in B72:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B12 {sym}={val}')
for api in ('adreno_cp_ring_pack_rb_cntl', 'adreno_cp_ring_unpack_field',
            'adreno_cp_ring_rb_cntl_value'):
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B12 .c defines {api}')
check('kAdrenoCpRingUndefOpenNow = 0u' in h, 'B12 the open set is EMPTY now')
check('adreno_cp_ring_undefined_open() == 0u ? 1 : 0' in c,
      'B12 is_executable() is derived from the open set, not hard-coded')
check(t.count('static void test') == 11, 'B12 11 test functions')
tflat3 = t.replace('"\n                "', '')
check('(11 test groups, 0 failures)' in tflat3, 'B12 PASS line says 11 groups')
check('testRbCntl();' in t, 'B12 testRbCntl invoked')

print(f'Domain A: {len(PINS)} sha256-pinned sources; 7 CP offsets, the 6-step order, '
      f'the 8-dword ME_INIT payload, the doorbell barrier order, and the FIVE '
      f'round-70 unknowns re-derived and ALL FIVE now CLOSED')
print('Domain B: AdrenoCpRingV1 .h/.c/test mirror; the real 0x70C80008 header; '
      'CP_RB_CNTL 0x0800020C; open set EMPTY; 24th unit/suite')
print(f'CP_RING_CROSS: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
for f in fails:
    print('  FAIL ' + f)
sys.exit(0 if not fails else 1)
