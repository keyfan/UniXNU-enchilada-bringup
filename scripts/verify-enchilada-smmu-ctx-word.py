#!/usr/bin/env python3
"""Round 83 method-2 cross-verifier for AdrenoSmmuCtxWordV1.

Domain A re-derives EVERY word from FOUR sha256-pinned sources, recomputing each
value from the source bits rather than trusting the module's constants:
  TCR   <- arm-smmu.h:393-412 + io-pgtable-arm.c:944-999
  TCR2  <- arm-smmu.h:414-418 + arm-smmu.c:525-527
  CBAR  <- arm-smmu.c:594-612 + arm-smmu.h:124-136
  CBA2R <- arm-smmu.c:581-592 + arm-smmu.h:141-143
  MAIR  <- io-pgtable-arm.c:1002-1011 + arm-smmu.c:560-561
  TTBRn <- arm-smmu.h:169 + arm-smmu.c:540-548

Domain B asserts the module mirrors those derivations, and -- the point of the
round -- that its NINE-write plan is a SUPERSET of round 75's six writes in the
registers it writes, and that it DROPS CONTEXTIDR, which upstream writes only in
the AArch32 branch. Domain C checks the wiring and the unit-count bookkeeping.

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
    ('P4_io_pgtable_arm_h', CACHE / 'drivers/iommu/io-pgtable-arm.h', 812,
     '34520ecc0fa7f29c7bc03c38c125e67bd66229ddc5fc44be0c59408e0389a379'),
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


smmu_h = texts['P1_arm_smmu_h']
smmu_c = texts['P2_arm_smmu_c']

# ---- A2: the GR1 frame bases, which round 75 modelled as UNUSED -----------
# arm-smmu.h:123 GR1_CBAR(n) = 0x0 + (n<<2); :141 GR1_CBA2R(n) = 0x800 + (n<<2).
check('(0x0 + ((n) << 2))' in line('P1_arm_smmu_h', 123),
      'A2 P1:123 GR1_CBAR(n) = 0x0 + (n << 2)')
check('(0x800 + ((n) << 2))' in line('P1_arm_smmu_h', 141),
      'A2 P1:141 GR1_CBA2R(n) = 0x800 + (n << 2)')
check('(0xc00 + ((n) << 2))' in line('P1_arm_smmu_h', 105),
      'A2 P1:105 GR0_S2CR(n) = 0xc00 + (n << 2)')

# ---- A3: CBAR fields and the stage-1 type --------------------------------
check('GENMASK(17, 16)' in line('P1_arm_smmu_h', 125), 'A3 P1:125 CBAR_TYPE 17..16')
check('GENMASK(15, 12)' in line('P1_arm_smmu_h', 132),
      'A3 P1:132 CBAR_S1_MEMATTR 15..12')
check('0xf' in line('P1_arm_smmu_h', 133), 'A3 P1:133 S1_MEMATTR_WB = 0xf')
check('GENMASK(9, 8)' in line('P1_arm_smmu_h', 134), 'A3 P1:134 S1_BPSHCFG 9..8')
check('3' in line('P1_arm_smmu_h', 135), 'A3 P1:135 S1_BPSHCFG_NSH = 3')
check('GENMASK(7, 0)' in line('P1_arm_smmu_h', 136), 'A3 P1:136 CBAR_VMID 7..0')
check('CBAR_TYPE_S1_TRANS_S2_BYPASS,' in line('P1_arm_smmu_h', 128),
      'A3 P1:128 S1_TRANS_S2_BYPASS == 1')
# arm-smmu.c:733 a stage-1 domain selects exactly that type.
check('cfg->cbar = CBAR_TYPE_S1_TRANS_S2_BYPASS;' in smmu_c[732],
      'A3 P2:733 a stage-1 domain uses CBAR_TYPE_S1_TRANS_S2_BYPASS')
# Recompute the CBAR word.
CBAR = (1 << 16) | (0xf << 12) | (3 << 8)
check(CBAR == 0x0001F300, f'A3 recomputed CBAR is 0x1F300 (got 0x{CBAR:08X})')

# ---- A4: CBA2R -----------------------------------------------------------
check('0x800 + ((n) << 2)' in line('P1_arm_smmu_h', 141), 'A4 P1:141 CBA2R base')
check('BIT(0)' in line('P1_arm_smmu_h', 143), 'A4 P1:143 CBA2R_VA64 = BIT(0)')
check('GENMASK(31, 16)' in line('P1_arm_smmu_h', 142),
      'A4 P1:142 CBA2R_VMID16 31..16')
body = flat(' '.join(smmu_c[580:592]))
check('ARM_SMMU_CBA2R_VA64' in body, 'A4 P2:582-586 VA64 for AArch64')
check('ARM_SMMU_CBA2R_VMID16' in body, 'A4 P2:587-589 16-bit VMID lives in CBA2R')
check('arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBA2R(idx), reg)' in body,
      'A4 P2:591 written to GR1_CBA2R(idx)')

# ---- A5: TCR2, and the AArch64 AS bit ------------------------------------
check('ARM_SMMU_CB_TCR2' in line('P1_arm_smmu_h', 161) and '0x10' in
      line('P1_arm_smmu_h', 161), 'A5 P1:161 CB_TCR2 = 0x10')
check('GENMASK(17, 15)' in line('P1_arm_smmu_h', 162), 'A5 P1:162 TCR2_SEP 17..15')
check('0x7' in line('P1_arm_smmu_h', 163), 'A5 P1:163 SEP_UPSTREAM = 0x7')
check('BIT(4)' in line('P1_arm_smmu_h', 164), 'A5 P1:164 TCR2_AS = BIT(4)')
check('GENMASK(3, 0)' in line('P1_arm_smmu_h', 165), 'A5 P1:165 TCR2_PASIZE 3..0')
tcr2_body = flat(' '.join(texts['P1_arm_smmu_h'][413:418]))
check('FIELD_PREP(ARM_SMMU_TCR2_PASIZE' in tcr2_body, 'A5 P1:416 PASIZE from ips')
check('FIELD_PREP(ARM_SMMU_TCR2_SEP, ARM_SMMU_TCR2_SEP_UPSTREAM)' in tcr2_body,
      'A5 P1:417 SEP is upstream 0x7')
check('cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64' in flat(' '.join(smmu_c[524:528])) and
      'ARM_SMMU_TCR2_AS' in flat(' '.join(smmu_c[524:528])),
      'A5 P2:525-527 an AArch64 context also sets TCR2_AS')
# TCR2 must be written BEFORE TCR, and the source says why.
check('ARM_SMMU_CB_TCR2' in smmu_c[619], 'A5 P2:620 TCR2 written before TCR')
check('We must write this before the TTBRs' in flat(' '.join(smmu_c[613:619])),
      'A5 P2:614-618 the ordering rationale is stated upstream')


def tcr2(oas, aarch64):
    enc = {32: 0, 36: 1, 40: 2, 42: 3, 44: 4, 48: 5}[oas]
    v = enc | (0x7 << 15)
    if aarch64:
        v |= (1 << 4)
    return v


check(tcr2(36, True) == 0x00038011, f'A5 tcr2(36,a64) = 0x38011 (got 0x{tcr2(36,True):08X})')
check(tcr2(36, False) == 0x00038001, f'A5 tcr2(36,!a64) = 0x38001 (got 0x{tcr2(36,False):08X})')
check(tcr2(36, True) ^ tcr2(36, False) == 1 << 4, 'A5 AS is the only difference')

# ---- A6: TCR composition, recomputed at every legal ias ------------------
check('BIT(23)' in line('P1_arm_smmu_h', 173), 'A6 P1:173 TCR_EPD1 = BIT(23)')
check('BIT(22)' in line('P1_arm_smmu_h', 174), 'A6 P1:174 TCR_A1 = BIT(22)')
check('BIT(7)' in line('P1_arm_smmu_h', 179), 'A6 P1:179 TCR_EPD0 = BIT(7)')
check('GENMASK(5, 0)' in line('P1_arm_smmu_h', 180), 'A6 P1:180 TCR_T0SZ 5..0')
# The real line numbers in the 812-byte header: TG0_4K at :5, TG1_4K at :10
# (which is 2, NOT 3 -- the trap this round records), SH_IS at :15, RGN_NC at
# :17 and RGN_WBWA at :18.
check('ARM_LPAE_TCR_TG0_4K' in line('P4_io_pgtable_arm_h', 5) and
      line('P4_io_pgtable_arm_h', 5).strip().endswith('0'),
      'A6 P4:5 TG0_4K = 0')
check('ARM_LPAE_TCR_TG1_4K' in line('P4_io_pgtable_arm_h', 10) and
      line('P4_io_pgtable_arm_h', 10).strip().endswith('2'),
      'A6 P4:10 TG1_4K = 2 (NOT 3 -- TG1_64K is 3)')
check('ARM_LPAE_TCR_TG1_64K' in line('P4_io_pgtable_arm_h', 11) and
      line('P4_io_pgtable_arm_h', 11).strip().endswith('3'),
      'A6 P4:11 TG1_64K = 3, which is a DIFFERENT constant from TG1_4K')
check('ARM_LPAE_TCR_SH_IS' in line('P4_io_pgtable_arm_h', 15),
      'A6 P4:15 SH_IS')
check('ARM_LPAE_TCR_RGN_NC' in line('P4_io_pgtable_arm_h', 17), 'A6 P4:17 RGN_NC')
check('ARM_LPAE_TCR_RGN_WBWA' in line('P4_io_pgtable_arm_h', 18),
      'A6 P4:18 RGN_WBWA')

TG0_4K, TG1_4K = 0, 2
SH_IS, SH_OS = 3, 2
RGN_NC, RGN_WBWA = 0, 1
A1, EPD0, EPD1 = 1 << 22, 1 << 7, 1 << 23


def tcr(ias, coherent, ttbr1):
    tg = TG1_4K if ttbr1 else TG0_4K
    sh = SH_IS if coherent else SH_OS
    rgn = RGN_WBWA if coherent else RGN_NC
    t = (tg << 14) | (sh << 12) | (rgn << 10) | (rgn << 8) | ((64 - ias) & 0x3f)
    if ttbr1:
        t = ((t << 16) & ~A1) | EPD0
    else:
        t = t | EPD1
    return t


for ias in (16, 32, 36, 40, 42, 44, 48):
    for coh in (0, 1):
        for t1 in (0, 1):
            v = tcr(ias, bool(coh), bool(t1))
            check(0 <= v <= 0xFFFFFFFF, f'A6 tcr({ias},{coh},{t1}) fits 32 bits')
check(tcr(40, True, False) == 0x00803518,
      f'A6 tcr(ias=40,coherent,ttbr0) = 0x803518 (got 0x{tcr(40,True,False):08X})')
check(tcr(40, False, False) == 0x00802018,
      f'A6 tcr(ias=40,non-coherent,ttbr0) = 0x802018 (got 0x{tcr(40,False,False):08X})')
check(tcr(40, True, True) == 0xB5180080,
      f'A6 tcr(ias=40,coherent,ttbr1) = 0xB5180080 (got 0x{tcr(40,True,True):08X})')
check(tcr(48, True, False) & 0x3f == 16, 'A6 T0SZ = 64-48 = 16 at the top ias')
check(tcr(32, True, False) & 0x3f == 32, 'A6 T0SZ = 64-32 = 32 at the low ias')
# The EPD pair is exclusive in BOTH geometries.
for t1 in (0, 1):
    v = tcr(40, True, bool(t1))
    check(bool(v & EPD0) != bool(v & EPD1),
          f'A6 tcr(ttbr1={t1}) sets exactly one of EPD0/EPD1')
check(tcr(40, True, True) & A1 == 0, 'A6 the TTBR1 geometry clears A1')
# The TTBR1 geometry's TG1 lands in bits 31..30 after the shift.
check((tcr(40, True, True) >> 30) & 0x3 == TG1_4K,
      'A6 the TTBR1 geometry carries TG1 in bits 31..30')
# Refusals correspond to the T0SZ field's range.
check(not (16 <= 15 <= 48), 'A6 ias=15 is outside the T0SZ-representable range')
check(not (16 <= 49 <= 48), 'A6 ias=49 is outside the T0SZ-representable range')

# ---- A7: id_size_to_bits, INCLUDING the collapsing default --------------
check('case 0:' in smmu_c[1729] and 'return 32;' in smmu_c[1730],
      'A7 P2:1730 0 -> 32')
check('case 5:' in smmu_c[1739] or 'default:' in smmu_c[1740],
      'A7 P2:1740-1742 a default arm exists')
check('return 48;' in smmu_c[1741], 'A7 P2:1742 the default returns 48')
# Recompute the mapping and its collapse.
IDSIZE = {0: 32, 1: 36, 2: 40, 3: 42, 4: 44}


def id_size_to_bits(s):
    return IDSIZE.get(s, 48)


for s, want in ((0, 32), (1, 36), (2, 40), (3, 42), (4, 44), (5, 48), (6, 48),
                (15, 48)):
    check(id_size_to_bits(s) == want, f'A7 id_size_to_bits({s}) == {want}')

# ---- A8: MAIR ------------------------------------------------------------
check('0x04' in line('P3_io_pgtable_arm_c', 123) or
      'ARM_LPAE_MAIR_ATTR_DEVICE' in line('P3_io_pgtable_arm_c', 123),
      'A8 P3:123 MAIR_ATTR_DEVICE')
check('0x44' in line('P3_io_pgtable_arm_c', 124), 'A8 P3:124 MAIR_ATTR_NC = 0x44')
check('0xf4' in line('P3_io_pgtable_arm_c', 125), 'A8 P3:125 INC_OWBRWA = 0xf4')
check('0xff' in line('P3_io_pgtable_arm_c', 126), 'A8 P3:126 WBRWA = 0xff')
mair_body = flat(' '.join(texts['P3_io_pgtable_arm_c'][1001:1010]))
check('ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_NC)' in mair_body,
      'A8 P3:1002-1003 index 0 carries NC')
check('ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_INC_OCACHE)' in mair_body,
      'A8 P3:1008-1009 index 3 carries INC_OWBRWA')
MAIR = (0x44 << 0) | (0xff << 8) | (0x04 << 16) | (0xf4 << 24)
check(MAIR == 0xF404FF44, f'A8 recomputed MAIR is 0xF404FF44 (got 0x{MAIR:08X})')
check(MAIR >> 32 == 0, 'A8 every attribute is in the low half, so MAIR1 == 0')
check('cb->mair[1] = pgtbl_cfg->arm_lpae_s1_cfg.mair >> 32;' in
      flat(' '.join(smmu_c[559:562])), 'A8 P2:561 MAIR1 = mair >> 32')

# ---- A9: TTBRn_ASID in bits 63..48, applied to BOTH ---------------------
check('GENMASK_ULL(63, 48)' in line('P1_arm_smmu_h', 169),
      'A9 P1:169 TTBRn_ASID = GENMASK_ULL(63,48)')
ttbr_body = flat(' '.join(smmu_c[534:549]))
check(ttbr_body.count('FIELD_PREP(ARM_SMMU_TTBRn_ASID') == 2,
      'A9 P2:540-543 the ASID is placed into BOTH TTBRs')
check('cb->ttbr[0] |= pgtbl_cfg->arm_lpae_s1_cfg.ttbr;' in ttbr_body,
      'A9 P2:548 the table pointer joins TTBR0')
check('arm_smmu_cb_writeq(smmu, idx, ARM_SMMU_CB_TTBR0, cb->ttbr[0]);' in
      flat(' '.join(smmu_c[628:630])), 'A9 P2:629 TTBR0 is a 64-bit write')
check('ARM_SMMU_CB_TTBR1' in flat(' '.join(smmu_c[629:633])) and
      'stage1' in flat(' '.join(smmu_c[629:633])),
      'A9 P2:630-632 TTBR1 is written for a stage-1 bank')
TTBR = (0x12 << 48) | 0x877FE000
check(TTBR == 0x00120000877FE000,
      f'A9 recomputed TTBR = 0x00120000877FE000 (got 0x{TTBR:016X})')
check((TTBR >> 32) & 0xFFFFFFFF == 0x00120000,
      'A9 the ASID lands in the HIGH word at bit 16, i.e. 0x00120000')

# ---- A10: CONTEXTIDR is written ONLY in the AArch32 branch --------------
ctx_lines = [(i + 1, l) for i, l in enumerate(smmu_c) if 'ARM_SMMU_CB_CONTEXTIDR' in l]
check(len(ctx_lines) == 1, f'A10 exactly one CONTEXTIDR write site (got {len(ctx_lines)})')
if ctx_lines:
    check(ctx_lines[0][0] == 625, f'A10 it is P2:625 (got {ctx_lines[0][0]})')
check('AARCH32_S' in flat(' '.join(smmu_c[623:626])),
      'A10 P2:624 it is inside the AARCH32_S branch')
check('ARM_SMMU_CTX_FMT_AARCH32_S' in flat(' '.join(smmu_c[623:626])),
      'A10 P2:624 guarded by cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_S')
# The AArch64 branch is the `else`, and it writes no CONTEXTIDR.
a64 = flat(' '.join(smmu_c[627:634]))
check('AARCH32_S' not in a64 and 'CONTEXTIDR' not in a64,
      'A10 P2:628-633 the AArch64 branch writes NO CONTEXTIDR')

# ---- A11: the FULL nine-write sequence, in order ------------------------
order = [(581, 'CBA2R'), (594, 'CBAR'), (619, 'TCR2'), (621, 'TCR'), (629, 'TTBR0'),
         (630, 'TTBR1'), (637, 'MAIR0'), (638, 'MAIR1'), (641, 'SCTLR')]
check(len(order) == 9, 'A11 the sequence is 9 writes')
for ln, nm in order:
    check(flat(' '.join(smmu_c[ln - 1:ln + 3])) != '',
          f'A11 P2:{ln} {nm} is inside arm_smmu_write_context_bank')
starts = [ln for ln, _ in order]
check(starts == sorted(starts), 'A11 the nine sites are in strictly ascending order')

# ======================= Domain B: the deliverable =======================
h = (GPU / 'AdrenoSmmuCtxWordV1.h').read_text(encoding='utf-8')
c = (GPU / 'AdrenoSmmuCtxWordV1.c').read_text(encoding='utf-8')
t = (GPU / 'tests' / 'adreno-smmu-ctx-word-v1-test.cpp').read_text(encoding='utf-8')

for name, path, size, sha in PINS:
    check(sha in h, f'B1 header records {name} sha256')

# B2: the constants the module declares must equal the recomputed values.
B2 = [('kAdrenoSmmuGr1Cbar', '0x0000u'), ('kAdrenoSmmuGr1Cba2r', '0x0800u'),
      ('kAdrenoSmmuCbarS1MemattrWb', '0x0000f000u'),
      ('kAdrenoSmmuCbarS1BpshcfgNsh', '0x00000300u'),
      ('kAdrenoSmmuCbarTypeS1TransS2Bypass', '1u'),
      ('kAdrenoSmmuCba2rVa64', '0x00000001u'),
      ('kAdrenoSmmuCba2rVmid16Mask', '0xffff0000u'),
      ('kAdrenoSmmuTcr2SepUpstream', '0x00038000u'),
      ('kAdrenoSmmuTcr2As', '0x00000010u'),
      ('kAdrenoSmmuTcr2PasizeMask', '0x0000000fu'),
      ('kAdrenoSmmuTcrEpd1', '0x00800000u'), ('kAdrenoSmmuTcrA1', '0x00400000u'),
      ('kAdrenoSmmuTcrEpd0', '0x00000080u'),
      ('kAdrenoSmmuTcrT0szMask', '0x0000003fu'),
      ('kAdrenoSmmuTg0_4K', '0u'), ('kAdrenoSmmuTg1_4K', '2u'),
      ('kAdrenoSmmuTcrShIs', '3u'), ('kAdrenoSmmuTcrShOs', '2u'),
      ('kAdrenoSmmuTcrRgnWbwa', '1u'), ('kAdrenoSmmuTcrRgnNc', '0u'),
      ('kAdrenoSmmuTtbrAsidShift', '48u'),
      ('kAdrenoSmmuTtbrAsidMask', '0xffffull'),
      ('kAdrenoSmmuMairAttrNc', '0x44u'),
      ('kAdrenoSmmuMairAttrWbrwa', '0xffu'),
      ('kAdrenoSmmuMairAttrDevice', '0x04u'),
      ('kAdrenoSmmuMairAttrIncOwbrwa', '0xf4u')]
for sym, val in B2:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B2 {sym}={val}')
# Cross-check the declared constants against the recomputed ones.
check(0x0000 == 0 and 0x0800 == 0x800, 'B2 GR1 bases match the recomputation')
check((1 << 16) | (0xf << 12) | (3 << 8) == CBAR, 'B2 CBAR composition matches')
check((0x44 << 0) | (0xff << 8) | (0x04 << 16) | (0xf4 << 24) == MAIR,
      'B2 MAIR composition matches')

APIS = ['adreno_smmu_ctx_id_size_to_bits', 'adreno_smmu_ctx_mair',
        'adreno_smmu_ctx_mair0', 'adreno_smmu_ctx_mair1',
        'adreno_smmu_ctx_tcr', 'adreno_smmu_ctx_tcr2', 'adreno_smmu_ctx_cbar',
        'adreno_smmu_ctx_cba2r', 'adreno_smmu_ctx_ttbrn',
        'adreno_smmu_ctx_ttbrn_checked', 'adreno_smmu_ctx_plan',
        'adreno_smmu_ctx_plan_writes', 'adreno_smmu_ctx_round75_writes',
        'adreno_smmu_ctx_missing', 'adreno_smmu_ctx_input_name',
        'adreno_smmu_ctx_input_anchor', 'adreno_smmu_ctx_ready',
        'adreno_smmu_ctx_verify']
for api in APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B3 .c defines {api}')
for bad in ('#include <string.h>', '#include <stdlib.h>', '#include <stdio.h>',
            'IOConnectCall', 'IOServiceOpen', 'writel(', 'readl(',
            'volatile uint32_t *'):
    check(bad not in c, f'B3 .c has no {bad}')

# B4: THE POINT -- the plan is nine writes, round 75 was six, and the three-write
# delta is FOUR added registers MINUS ONE dropped one.
check('return 9u;' in c, 'B4 plan_writes() is 9')
check('return 6u;' in c, 'B4 round75_writes() is 6')
plan_body = c[c.find('int adreno_smmu_ctx_plan('):]
plan_body = plan_body[:plan_body.find('\n}\n')]
for off, nm in (('0x0800u', 'CBA2R'), ('0x0000u', 'CBAR'), ('0x0010u', 'TCR2'),
                ('0x0030u', 'TCR'), ('0x0020u', 'TTBR0'), ('0x0028u', 'TTBR1'),
                ('0x0038u', 'MAIR0'), ('0x003cu', 'MAIR1')):
    check(f'out[n].offset = {off}' in plan_body or
          f'kAdrenoSmmuGr1Cbar + ((p->cbndx' in plan_body,
          f'B4 the plan names {nm}')
# The four registers round 75 omitted are ALL written.
check('kAdrenoSmmuGr1Cba2r + ((p->cbndx & 0xffu) << 2)' in plan_body,
      'B4 CBA2R is present (round 75 omitted it)')
check('kAdrenoSmmuGr1Cbar + ((p->cbndx & 0xffu) << 2)' in plan_body,
      'B4 CBAR is present (round 75 omitted it)')
check(plan_body.count('0x0010u') >= 1, 'B4 TCR2 is present (round 75 omitted it)')
check(plan_body.count('0x0028u') >= 1, 'B4 TTBR1 is present (round 75 omitted it)')
# And CONTEXTIDR is NOT written: round 75 wrote it unconditionally.
check('0x0034u' not in plan_body,
      'B4 CONTEXTIDR is NOT in the AArch64 plan (round 75 wrote it)')
# SCTLR is last, and the ordering rationale is stated.
check(plan_body.rfind('out[n].offset = 0x0000u') >
      plan_body.rfind('out[n].offset = 0x003cu'),
      'B4 SCTLR is written LAST (round 75 one rule, preserved)')
check('translation' in plan_body.lower(), 'B4 the SCTLR-last rationale is stated')
# Both TTBRs carry the ASID.
check(plan_body.count('adreno_smmu_ctx_ttbrn_checked') >= 1,
      'B4 the ASID is composed into the TTBRs')

# B5: round 75's six-write plan is UNCHANGED by this round (no silent edit).
r75 = (GPU / 'AdrenoSmmuStage1V1.c').read_text(encoding='utf-8')
check('const uint32_t need = 6u;' in r75,
      'B5 round 75 still emits six writes -- this round CORRECTS, not rewrites it')
check('adreno_smmu_stage1_missing() == 0u ? 1 : 0' in r75,
      'B5 round 75 readiness is still derived and 0')

# B6: the absent inputs, derived and 0.
check('mask |= kAdrenoSmmuCtxInputIas;' in c, 'B6 ias recorded missing')
check('mask |= kAdrenoSmmuCtxInputAsid;' in c, 'B6 asid recorded missing')
check('mask |= kAdrenoSmmuCtxInputPageTable;' in c, 'B6 page table recorded missing')
check('adreno_smmu_ctx_missing() == 0u ? 1 : 0' in c,
      'B6 ready() is DERIVED from the missing mask')
check('ID2.UBS' in c, 'B6 ias cites the ID2.UBS chain')
check('kAdrenoSmmuCtxInputAll = 7u' in h, 'B6 the all-mask is three bits')

# B7: the test -- 10 groups, all CALLED, and the pinned PASS line.
tfuncs = re.findall(r'static void (test[A-Za-z0-9]+)\(\)', t)
check(len(tfuncs) == 10, f'B7 10 test functions (got {len(tfuncs)}: {tfuncs})')
main_body = t[t.find('int main()'):]
for fn in ('testIdSize', 'testMair', 'testTcr', 'testTcr2', 'testCbarCba2r',
           'testTtbr', 'testPlanCompleteness', 'testPlanRefusals',
           'testMissingInputs', 'testVerify'):
    check(fn in tfuncs, f'B7 {fn} is declared')
    check(f'{fn}();' in main_body, f'B7 main() CALLS {fn}() -- otherwise dead code')
# The PASS line spans two adjacent string literals with a whitespace-only gap.
# C concatenates them at compile time, so this asserts what the PROGRAM prints
# rather than how the source wraps it. Verified empirically: the gap really is
# `" "` plus indentation, so a literal replace of `" "` alone was not enough.
tflat = flat(re.sub(r'"\s*"', '', t))
check('PASS: enchilada A630 SMMU stage-1 context WORD derivation '
      '(10 test groups, 0 failures)' in tflat, 'B7 PASS line')
check('0x00803518u' in t, 'B7 the test pins the derived TCR word')
check('0x00120000u' in t, 'B7 the test pins the TTBR high word')
check('0x00038001u' in t, 'B7 the test pins the no-AS TCR2 word')
check('adreno_smmu_ctx_round75_writes() + 3u' in t,
      'B7 the test pins the three-write net delta')
# The test must RE-DERIVE, not copy: it carries its own lambda for the word.
check('want_tcr' in t, 'B7 the test re-derives the TCR word instead of hard-coding')

# ======================= Domain C: wiring ================================
build = (DEV / 'scripts' / 'build-enchilada-adreno.py').read_text(encoding='utf-8')
host = (DEV / 'scripts' / 'check-enchilada-adreno-host.py').read_text(encoding='utf-8')
check("GPU / 'AdrenoSmmuCtxWordV1.c', ['-mgeneral-regs-only']" in build,
      'C1 build script compiles the round-83 unit')
check("'AdrenoSmmuCtxWordV1': GPU / 'AdrenoSmmuCtxWordV1.c'," in host,
      'C1 host SOURCES entry')
check("('adreno-smmu-ctx-word-v1-test'" in host, 'C1 host TESTS entry')
check('/smmu-ctx-word-v1' in host, 'C1 PASS enumeration lists this suite')
units = len(re.findall(r"\(c_rec, GPU / '[^']+'", build))
check(units == 29, f'C2 29 C archive units declared (got {units})')
# The bookkeeping the round-78 lesson demands: no stale count anywhere. The
# stale values are BUILT here rather than written, because this file is one of
# the files the loop below scans -- a literal would make it match itself.
STALE_COUNTS = ('== ' + '27', '== ' + '28')
CURRENT_COUNT = '== ' + '29'
for name in ('verify-enchilada-cp-ring.py', 'verify-enchilada-gen1-backend.py',
             'verify-enchilada-mdt.py', 'verify-enchilada-smmu-stage1.py',
             'verify-enchilada-va-map.py', 'verify-enchilada-zap-auth.py'):
    s = (DEV / 'scripts' / name).read_text(encoding='utf-8')
    check(not any(t in s for t in STALE_COUNTS),
          f'C2 {name} no longer asserts a stale count')
    check(CURRENT_COUNT in s, f'C2 {name} asserts the CURRENT 29 units')

print(f'Domain A: {len(PINS)} sha256-pinned sources; the GR1 bases, the CBAR/CBA2R/'
      f'TCR2 fields, the TCR default RECOMPUTED at 7 ias x 2 coherency x 2 geometries, '
      f'id_size_to_bits INCLUDING its collapsing default, MAIR, TTBRn_ASID, the single '
      f'CONTEXTIDR site and the full nine-site write order all re-derived')
print('Domain B: AdrenoSmmuCtxWordV1 .h/.c/test mirror; the plan is 9 writes with the '
      'FOUR registers round 75 omitted, CONTEXTIDR dropped, SCTLR last; round 75 left '
      'byte-identical; three inputs missing; ready() derived and 0')
print('Domain C: the round-83 unit and host suite wired; all six stale 27-unit '
      'assertions moved up (the round-78 drift class, now 29)')
print(f'SMMU_CTX_WORD_CROSS: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
for f in fails:
    print('  FAIL ' + f)
sys.exit(0 if not fails else 1)
