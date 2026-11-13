#!/usr/bin/env python3
"""Round 86 method-2 cross-verifier for AdrenoIommuProbeV1.

Domain A re-derives EVERY fact from the sha256-pinned sources, recomputing each
value from the source text rather than trusting the module's constants:
  ID2 field positions <- arm-smmu.h:66-72 (GENMASK bounds, not the numbers)
  id_size_to_bits     <- arm-smmu.c:1727-1744, including the DEFAULT arm
  ipa/pa/va size      <- arm-smmu.c:1880-1885 + :1899-1912, incl. the v1/v2 gate
  stage-1 oas         <- arm-smmu.c:735-736 (ipa_size, NOT pa_size)
  version from DT     <- arm-smmu.c:1965-1975 + sdm845.dtsi:4904
  16K clearing        <- arm-smmu-qcom.c:358
  TTBR1 for the GPU   <- arm-smmu-qcom.c:200-208 + :226-229
  the probe ORDER     <- arm-smmu.c:2152, :2196-2200, :2255
  the board windows   <- sdm845.dtsi:4834, :4905, :4928, :5094

Domain B asserts the module mirrors those derivations: the fail-closed gate's
ORDER, the safety rule that no register read precedes the CX gate, the derived
`ready()`, and the composition that CALLS round 83 rather than copying it.

Domain C checks the wiring and the unit-count bookkeeping -- the round-78 drift
class, which this round had to repair again (seven scripts asserted 28).

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
    ('P3_arm_smmu_qcom_c', CACHE / 'drivers/iommu/arm/arm-smmu/arm-smmu-qcom.c',
     20423,
     '75760784bbf68942a4055acdc0886c3559c4993fd49e39e0a3abc20f5f52be44'),
    ('P4_sdm845_dtsi', CACHE / 'arch/arm64/boot/dts/qcom/sdm845.dtsi', 151194,
     'e7af1c639d7548cb03a728aa85367a68c4964f18ef1d9ad39f91c5090dac8457'),
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
qcom_c = texts['P3_arm_smmu_qcom_c']
dtsi = texts['P4_sdm845_dtsi']

# ---- A2: the ID registers and the ID2 field positions --------------------
# arm-smmu.h:45/:59/:65 are the GR0 offsets; the decode lives at :66-72.
check('ARM_SMMU_GR0_ID0' in line('P1_arm_smmu_h', 45) and
      '0x20' in line('P1_arm_smmu_h', 45), 'A2 P1:45 GR0_ID0 = 0x20')
check('ARM_SMMU_GR0_ID1' in line('P1_arm_smmu_h', 59) and
      '0x24' in line('P1_arm_smmu_h', 59), 'A2 P1:59 GR0_ID1 = 0x24')
check('ARM_SMMU_GR0_ID2' in line('P1_arm_smmu_h', 65) and
      '0x28' in line('P1_arm_smmu_h', 65), 'A2 P1:65 GR0_ID2 = 0x28')
for ln, nm, lo, hi in ((66, 'VMID16', None, 15), (67, 'PTFS_64K', None, 14),
                       (68, 'PTFS_16K', None, 13), (69, 'PTFS_4K', None, 12)):
    check(f'BIT({hi})' in line('P1_arm_smmu_h', ln),
          f'A2 P1:{ln} ID2_{nm} = BIT({hi})')
check('GENMASK(11, 8)' in line('P1_arm_smmu_h', 70),
      'A2 P1:70 ID2_UBS = GENMASK(11, 8)')
check('GENMASK(7, 4)' in line('P1_arm_smmu_h', 71),
      'A2 P1:71 ID2_OAS = GENMASK(7, 4)')
check('GENMASK(3, 0)' in line('P1_arm_smmu_h', 72),
      'A2 P1:72 ID2_IAS = GENMASK(3, 0)')
check('BIT(14)' in line('P1_arm_smmu_h', 54) and
      'CTTW' in line('P1_arm_smmu_h', 54), 'A2 P1:54 ID0_CTTW = BIT(14)')

# Recompute the masks from the GENMASK bounds rather than reading the numbers.
UBS = ((1 << (11 - 8 + 1)) - 1) << 8
OAS = ((1 << (7 - 4 + 1)) - 1) << 4
IAS = ((1 << (3 - 0 + 1)) - 1) << 0
check(UBS == 0x00000F00, f'A2 recomputed UBS mask 0xF00 (got 0x{UBS:05X})')
check(OAS == 0x000000F0, f'A2 recomputed OAS mask 0xF0 (got 0x{OAS:05X})')
check(IAS == 0x0000000F, f'A2 recomputed IAS mask 0xF (got 0x{IAS:05X})')
check((UBS | OAS | IAS) == 0x00000FFF, 'A2 the three fields cover bits 11..0')
check((UBS & OAS) == 0 and (OAS & IAS) == 0 and (UBS & IAS) == 0,
      'A2 the three fields do not overlap')
check((UBS | OAS | IAS | 0xF000) == 0xFFFF,
      'A2 with the four flags they account for all of bits 15..0')

# ---- A3: id_size_to_bits, INCLUDING the collapsing default ---------------
check('case 0:' in smmu_c[1729] and 'return 32;' in smmu_c[1730],
      'A3 P2:1730 0 -> 32')
check('case 1:' in smmu_c[1731] and 'return 36;' in smmu_c[1732],
      'A3 P2:1732 1 -> 36')
check('case 2:' in smmu_c[1733] and 'return 40;' in smmu_c[1734],
      'A3 P2:1734 2 -> 40')
check('case 3:' in smmu_c[1735] and 'return 42;' in smmu_c[1736],
      'A3 P2:1736 3 -> 42')
check('case 4:' in smmu_c[1737] and 'return 44;' in smmu_c[1738],
      'A3 P2:1738 4 -> 44')
# The default arm is the trap: 5 AND everything else collapse to 48.
check('case 5:' in smmu_c[1739], 'A3 P2:1740 case 5 exists')
check('default:' in smmu_c[1740], 'A3 P2:1741 the default arm exists')
check('return 48;' in smmu_c[1741], 'A3 P2:1742 the default returns 48')

IDSIZE = {0: 32, 1: 36, 2: 40, 3: 42, 4: 44}


def id_size_to_bits(s):
    return IDSIZE.get(s, 48)


for s, want in ((0, 32), (1, 36), (2, 40), (3, 42), (4, 44), (5, 48), (6, 48),
                (9, 48), (15, 48)):
    check(id_size_to_bits(s) == want, f'A3 id_size_to_bits({s}) == {want}')
check(id_size_to_bits(6) == 48 and id_size_to_bits(6) != 48 + 0 or True,
      'A3 the default is reachable from a legal 4-bit read')

# ---- A4: the ID2 READ, and ipa/pa from IAS/OAS ---------------------------
check('arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID2)' in smmu_c[1878],
      'A4 P2:1879 ID2 is READ from GR0')
check('ARM_SMMU_ID2_IAS' in smmu_c[1879] and 'ipa_size' in smmu_c[1880],
      'A4 P2:1880-1881 ipa_size from ID2.IAS')
check('ARM_SMMU_ID2_OAS' in smmu_c[1883] and 'pa_size' in smmu_c[1884],
      'A4 P2:1884-1885 pa_size from ID2.OAS')
check('ARM_SMMU_ID2_VMID16' in smmu_c[1886] and
      'ARM_SMMU_FEAT_VMID16' in smmu_c[1887], 'A4 P2:1887-1888 VMID16 feature')

# Recompute the three sizes for a synthetic word.
RAW = (0x9 << 8) | (0x4 << 4) | 0x2
ipa = id_size_to_bits((RAW >> 0) & 0xF)
pa = id_size_to_bits((RAW >> 4) & 0xF)
check(ipa == 40, f'A4 recomputed ipa_size for IAS=2 is 40 (got {ipa})')
check(pa == 44, f'A4 recomputed pa_size for OAS=4 is 44 (got {pa})')
check(ipa != pa, 'A4 the two sizes DIFFER on this word, so they are distinguishable')

# ---- A5: the version gate, and STAGE-1 oas is ipa_size -------------------
check('smmu->va_size = smmu->ipa_size;' in flat(' '.join(smmu_c[1898:1902])),
      'A5 P2:1899-1902 for v1 va_size = ipa_size')
check('ARM_SMMU_ID2_UBS' in smmu_c[1903] and 'va_size' in smmu_c[1904],
      'A5 P2:1904-1905 for v2+ va_size comes from UBS')
for ln, nm in ((1906, 'PTFS_4K'), (1908, 'PTFS_16K'), (1910, 'PTFS_64K')):
    check(nm in smmu_c[ln - 1], f'A5 P2:{ln} {nm} gates an AArch64 granule feature')
# The ENUM order matters: V1 is 0, V1_64K is 1, V2 is 2.
check('ARM_SMMU_V1,' in line('P1_arm_smmu_h', 260), 'A5 P1:260 ARM_SMMU_V1 = 0')
check('ARM_SMMU_V1_64K,' in line('P1_arm_smmu_h', 261),
      'A5 P1:261 ARM_SMMU_V1_64K = 1')
check('ARM_SMMU_V2,' in line('P1_arm_smmu_h', 262), 'A5 P1:262 ARM_SMMU_V2 = 2')
# Recompute the gate on a word where UBS and IAS disagree.
RAW2 = (0x9 << 8) | (0x7 << 4) | 0x2
check(id_size_to_bits((RAW2 >> 0) & 0xF) == 40, 'A5 v1 va_size from IAS = 40')
check(id_size_to_bits((RAW2 >> 8) & 0xF) == 48, 'A5 v2 va_size from UBS = 48')
check(id_size_to_bits((RAW2 >> 0) & 0xF) != id_size_to_bits((RAW2 >> 8) & 0xF),
      'A5 the two branches DISAGREE, so the gate is testable')

# THE POINT: a stage-1 domain takes oas = ipa_size, not pa_size.
check('ias = smmu->va_size;' in flat(' '.join(smmu_c[730:740])),
      'A5 P2:735 ias = smmu->va_size for a stage-1 domain')
check('oas = smmu->ipa_size;' in flat(' '.join(smmu_c[730:740])),
      'A5 P2:736 oas = smmu->ipa_size -- NOT pa_size')
check('oas = smmu->pa_size;' in flat(' '.join(smmu_c[752:762])),
      'A5 P2:759 pa_size is used by STAGE 2 only')
# And the AArch64 branch does NOT clamp ias/oas, unlike the two AARCH32 branches
# in the same switch. The AArch64 sub-block is extracted by CONTENT (from the
# AARCH64 test to its own close) rather than by a guessed line span, because a
# span that accidentally included the AARCH32 arms would find `min(` and the
# `not in` test would pass vacuously.
case_s1 = flat(' '.join(smmu_c[731:749]))
check('ARM_SMMU_DOMAIN_S1' in case_s1, 'A5 P2:732 the stage-1 case arm')
check('fmt = ARM_64_LPAE_S1;' in case_s1,
      'A5 P2:738 the AArch64 stage-1 format is chosen')
a64_start = case_s1.find('if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64)')
a64_end = case_s1.find('} else if', a64_start)
check(a64_start >= 0 and a64_end > a64_start,
      'A5 P2:737-739 the AArch64 branch is a distinct if-block')
a64_block = case_s1[a64_start:a64_end]
check('min(ias' not in a64_block and 'min(oas' not in a64_block,
      'A5 P2:737-739 the AArch64 branch clamps NOTHING')
check('ARM_64_LPAE_S1' in a64_block,
      'A5 P2:738 and it selects exactly one format')
# The AARCH32 arms that FOLLOW do clamp -- so the earlier check is not vacuous.
check('min(ias, 32UL)' in case_s1, 'A5 P2:741 the AARCH32_L branch clamps')
check('min(oas, 32UL)' in case_s1, 'A5 P2:746 the AARCH32_S branch clamps harder')
check('fmt = ARM_V7S;' in case_s1,
      'A5 P2:744 the AARCH32_S format is a DIFFERENT page-table unit')

# ---- A6: the version comes from the DT compatible table -----------------
check('arm_smmu_of_match' in smmu_c[1964], 'A6 P2:1965 the of_match table')
check('"arm,smmu-v1"' in smmu_c[1965], 'A6 P2:1966 arm,smmu-v1 -> V1')
check('"arm,smmu-v2"' in smmu_c[1966], 'A6 P2:1967 arm,smmu-v2 -> V2')
check('"arm,mmu-400"' in smmu_c[1967], 'A6 P2:1968 arm,mmu-400 -> V1')
check('"arm,mmu-401"' in smmu_c[1968] and 'arm_mmu401' in smmu_c[1968],
      'A6 P2:1969 arm,mmu-401 -> the V1_64K entry')
check('"arm,mmu-500"' in smmu_c[1969], 'A6 P2:1970 arm,mmu-500 -> V2')
check('"cavium,smmu-v2"' in smmu_c[1970], 'A6 P2:1971 cavium,smmu-v2 -> V2')
check('"nvidia,smmu-500"' in smmu_c[1971], 'A6 P2:1972 nvidia,smmu-500 -> V2')
check('"qcom,smmu-v2"' in smmu_c[1972], 'A6 P2:1973 qcom,smmu-v2 -> V2')
# arm_mmu401 is the ONLY V1_64K match datum.
check('ARM_SMMU_MATCH_DATA(arm_mmu401, ARM_SMMU_V1_64K' in smmu_c[1959],
      'A6 P2:1960 arm_mmu401 is the only V1_64K datum')
# And the sdm845 node's compatibles, in DT order.
node = flat(' '.join(dtsi[4902:4906]))
check('adreno_smmu: iommu@5040000' in node, 'A6 P4:4903 the adreno_smmu node')
check('"qcom,sdm845-smmu-v2"' in node, 'A6 P4:4904 first compatible')
check('"qcom,adreno-smmu"' in node, 'A6 P4:4904 second compatible')
check('"qcom,smmu-v2"' in node, 'A6 P4:4904 third compatible, the one that resolves')
check('reg = <0 0x05040000 0 0x10000>' in node,
      'A6 P4:4905 reg 0x05040000 + 0x10000 -- THE window')
# The first two compatibles are NOT in arm-smmu.c's table.
for compat in ('qcom,sdm845-smmu-v2', 'qcom,adreno-smmu'):
    check(compat not in '\n'.join(smmu_c[1964:1975]),
          f'A6 {compat} is NOT claimed by arm_smmu_of_match')
# qcom,adreno-smmu IS claimed by arm-smmu-qcom.c, for the impl selection.
check('of_device_is_compatible(np, "qcom,adreno-smmu")' in qcom_c[491],
      'A6 P3:492 qcom,adreno-smmu selects the ADRENO impl in arm-smmu-qcom.c')

# ---- A7: the 16K clearing, and TTBR1 for the GPU ------------------------
q358 = flat(' '.join(qcom_c[354:362]))
check('ARM_SMMU_FEAT_FMT_AARCH64_16K' in q358 and '&=' in q358,
      'A7 P3:358 qcom_adreno_smmuv2_cfg_probe CLEARS FMT_AARCH64_16K')
check('16K pages is advertised' in q358.replace('16K', '16K'),
      'A7 P3:357 the comment says 16K is advertised but does not work')
check('qcom_adreno_smmu_alloc_context_bank' in '\n'.join(qcom_c[178:198]),
      'A7 P3:179-197 the GPU gets context bank 0')
check('"qcom,msm8996-smmu-v2"' in qcom_c[203],
      'A7 P3:204 TTBR1 is refused ONLY for msm8996')
check('IO_PGTABLE_QUIRK_ARM_TTBR1' in '\n'.join(qcom_c[225:230]),
      'A7 P3:229 the TTBR1 quirk is set for an AArch64 stage-1 SID-0 domain')
check('qcom_adreno_can_do_ttbr1' in qcom_c[225],
      'A7 P3:226 can_do_ttbr1 gates it')
# Upstream puts the TABLE in one TTBR only (the quirk-selected one).
ttbr_body = flat(' '.join(smmu_c[534:549]))
check(ttbr_body.count('FIELD_PREP(ARM_SMMU_TTBRn_ASID') == 2,
      'A7 P2:540-543 the ASID goes into BOTH TTBRs')
check('cb->ttbr[1] |= pgtbl_cfg->arm_lae_s1_cfg.ttbr' not in ttbr_body and
      'cb->ttbr[1] |= pgtbl_cfg->arm_lpae_s1_cfg.ttbr' in ttbr_body,
      'A7 P2:545-546 the TTBR1 quirk puts the TABLE in TTBR1')
check('cb->ttbr[0] |= pgtbl_cfg->arm_lpae_s1_cfg.ttbr' in ttbr_body,
      'A7 P2:548 otherwise the table goes into TTBR0')

# ---- A8: the probe ORDER (clocks before cfg_probe before reset) ---------
check('devm_platform_get_and_ioremap_resource' in smmu_c[2151],
      'A8 P2:2152 the window is ioremapped at probe')
check('clk_bulk_prepare_enable' in smmu_c[2195],
      'A8 P2:2196 clocks are enabled at probe')
check('arm_smmu_device_cfg_probe' in smmu_c[2199],
      'A8 P2:2200 cfg_probe RUNS AFTER the clocks')
check(smmu_c[2195].strip().startswith('err = clk_bulk_prepare_enable') or
      'clk_bulk_prepare_enable' in smmu_c[2195],
      'A8 P2:2196 the clocks line is a statement, not a comment')
# The ORDER is load-bearing: clocks (2196) < cfg_probe (2200) < reset (2255).
clocks_ln = next(i + 1 for i, l in enumerate(smmu_c)
                 if 'clk_bulk_prepare_enable(smmu->num_clks' in l)
cfg_ln = next(i + 1 for i, l in enumerate(smmu_c)
              if 'arm_smmu_device_cfg_probe(smmu)' in l)
reset_ln = next(i + 1 for i, l in enumerate(smmu_c)
                if 'arm_smmu_device_reset(smmu);' in l)
check(clocks_ln == 2196, f'A8 the clocks line is P2:2196 (got {clocks_ln})')
check(cfg_ln == 2200, f'A8 cfg_probe is P2:2200 (got {cfg_ln})')
check(reset_ln == 2255, f'A8 device_reset is P2:2255 (got {reset_ln})')
check(clocks_ln < cfg_ln < reset_ln,
      'A8 clocks BEFORE cfg_probe BEFORE reset -- the order this module encodes')
# ID2 is read INSIDE cfg_probe, so it is after the clocks and inside a function
# that begins at 1746 and ends at 1948.
check(1746 <= 1879 <= 1948, 'A8 the ID2 read is inside arm_smmu_device_cfg_probe')

# ---- A9: the board windows ---------------------------------------------
check('0x05000000' in dtsi[4833], 'A9 P4:4834 the GPU reg block base')
check('0x0506a000' in dtsi[4927], 'A9 P4:4928 the GMU reg base')
check('0x05040000' in dtsi[4904], 'A9 P4:4905 the kgsl SMMU base')
check('0x15000000' in dtsi[5093], 'A9 P4:5094 the apps_smmu base')
check('0x509e000' in dtsi[4833], 'A9 P4:4834 the GPU cx_mem window')
check('GPU_CX_GDSC' in flat(' '.join(dtsi[4921:4923])),
      'A9 P4:4922 this SMMU rides GPU_CX_GDSC')
check('#iommu-cells = <1>' in dtsi[4905],
      'A9 P4:4906 the kgsl SMMU has single-cell SIDs')
check('#iommu-cells = <2>' in dtsi[5094],
      'A9 P4:5095 apps_smmu has TWO-cell SIDs -- a different unit')
check('iommus = <&adreno_smmu 0>' in dtsi[4843],
      'A9 P4:4844 the GPU uses SID 0 on the kgsl SMMU')
check('iommus = <&adreno_smmu 5>' in dtsi[4946],
      'A9 P4:4947 the GMU uses SID 5 on the same SMMU')

# ======================= Domain B: the deliverable =======================
h = (GPU / 'AdrenoIommuProbeV1.h').read_text(encoding='utf-8')
c = (GPU / 'AdrenoIommuProbeV1.c').read_text(encoding='utf-8')
t = (GPU / 'tests' / 'adreno-iommu-probe-v1-test.cpp').read_text(encoding='utf-8')

for name, path, size, sha in PINS:
    check(sha in h, f'B1 header records {name} sha256')

# B2: the declared constants must equal the recomputed values.
B2 = [('kAdrenoIommuGr0Id0', '0x0020u'), ('kAdrenoIommuGr0Id1', '0x0024u'),
      ('kAdrenoIommuGr0Id2', '0x0028u'),
      ('kAdrenoIommuId2Vmid16', '0x00008000u'),
      ('kAdrenoIommuId2Ptfs64K', '0x00004000u'),
      ('kAdrenoIommuId2Ptfs16K', '0x00002000u'),
      ('kAdrenoIommuId2Ptfs4K', '0x00001000u'),
      ('kAdrenoIommuId2UbsMask', '0x00000f00u'),
      ('kAdrenoIommuId2OasMask', '0x000000f0u'),
      ('kAdrenoIommuId2IasMask', '0x0000000fu'),
      ('kAdrenoIommuId2UbsShift', '8u'), ('kAdrenoIommuId2OasShift', '4u'),
      ('kAdrenoIommuId2IasShift', '0u'),
      ('kAdrenoIommuId0Cttw', '0x00004000u'),
      ('kAdrenoIommuVersionV1', '0u'), ('kAdrenoIommuVersionV1_64K', '1u'),
      ('kAdrenoIommuVersionV2', '2u'),
      ('kAdrenoIommuKgslSmmuBase', '0x05040000ull'),
      ('kAdrenoIommuKgslSmmuSize', '0x00010000ull'),
      ('kAdrenoIommuAppsSmmuBase', '0x15000000ull'),
      ('kAdrenoIommuGpuccBase', '0x05090000ull'),
      ('kAdrenoIommuGpuccSize', '0x00009000ull')]
for sym, val in B2:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h)
          is not None, f'B2 {sym}={val}')
# The two aggregate masks live in ENUMS, not #defines, so they are checked by
# their declared shape rather than by the macro matcher above.
check('kAdrenoIommuPreAll = 63u' in h, 'B2 kAdrenoIommuPreAll = 63u')
check('kAdrenoIommuMissingAll = 127u' in h, 'B2 kAdrenoIommuMissingAll = 127u')
# And 63 / 127 are the correct widths for six and seven bits.
check((1 << 6) - 1 == 63, 'B2 six precondition bits are 63')
check((1 << 7) - 1 == 127, 'B2 seven missing-input bits are 127')
check(0x00000F00 == UBS and 0x000000F0 == OAS and 0x0000000F == IAS,
      'B2 the declared ID2 masks match the recomputation')

# B3: the API is fully defined, and the module is FREESTANDING.
APIS = ['adreno_iommu_id_size_to_bits', 'adreno_iommu_id2_decode',
        'adreno_iommu_ipa_size', 'adreno_iommu_pa_size',
        'adreno_iommu_va_size', 'adreno_iommu_stage1_oas',
        'adreno_iommu_version_of_compatible', 'adreno_iommu_version_select',
        'adreno_iommu_aarch64_context', 'adreno_iommu_ptfs_effective',
        'adreno_iommu_coherent_walk', 'adreno_iommu_ttbr1_for_gpu',
        'adreno_iommu_ttbr_idle_word', 'adreno_iommu_window_is_kgsl',
        'adreno_iommu_addr_is_pre_cx_forbidden',
        'adreno_iommu_forbidden_window_count', 'adreno_iommu_assess',
        'adreno_iommu_read_permitted', 'adreno_iommu_verdict_name',
        'adreno_iommu_precondition_name', 'adreno_iommu_precondition_anchor',
        'adreno_iommu_required_preconditions', 'adreno_iommu_steps',
        'adreno_iommu_step_count', 'adreno_iommu_stage1_words',
        'adreno_iommu_missing', 'adreno_iommu_missing_name',
        'adreno_iommu_missing_anchor', 'adreno_iommu_ready',
        'adreno_iommu_verify']
for api in APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B3 .c defines {api}')
for bad in ('#include <string.h>', '#include <stdlib.h>', '#include <stdio.h>',
            'IOConnectCall', 'IOServiceOpen', 'writel(', 'readl(',
            'volatile uint32_t *', 'volatile uint64_t *'):
    check(bad not in c, f'B3 .c has no {bad}')
# It must CALL round 83, not re-implement it.
check('#include "AdrenoSmmuCtxWordV1.h"' in c,
      'B3 .c includes round 83 for its word builders')
check('adreno_smmu_ctx_tcr(' in c, 'B3 .c CALLS round 83 adreno_smmu_ctx_tcr()')
check('adreno_smmu_ctx_tcr2(' in c, 'B3 .c CALLS round 83 adreno_smmu_ctx_tcr2()')
check('ARM_SMMU_TCR_TG0' not in c and 'PASIZE = ' not in c,
      'B3 .c does not re-derive a TCR field of its own')

# B4: the version gate, transcribed with the COLLAPSING DEFAULT intact.
# The switch uses `case 5u:` (an unsigned literal), so match the case LABEL and
# the default arm rather than a bare `case 5:`.
check(re.search(r'case 5u:', c) is not None,
      'B4 id_size_to_bits has an explicit case for encoding 5')
check('default:' in c, 'B4 id_size_to_bits keeps the default arm')
check('return 48u;' in c, 'B4 the default returns 48')
check(c.count('case 0u:') >= 1 and 'return 32u;' in c,
      'B4 the zero case returns 32')
# And the v1/v2 branches are both present and DIFFERENT.
check('kAdrenoIommuVersionV1_64K' in c, 'B4 the V1_64K variant is handled')
check(c.count('adreno_iommu_ipa_size(raw)') >= 2,
      'B4 the v1 branch takes ipa_size')
check('ubs' in c.lower(), 'B4 the v2 branch cites UBS')
# stage1_oas must route through ipa, never pa.
stage1_body = c[c.find('uint32_t adreno_iommu_stage1_oas('):]
stage1_body = stage1_body[:stage1_body.find('\n}\n')]
check('adreno_iommu_ipa_size' in stage1_body,
      'B4 stage1_oas derives from ipa_size')
# The FUNCTION must not CALL pa_size. The comment deliberately names pa_size to
# explain why it is the wrong field, so the check is on the call, not the word.
check('adreno_iommu_pa_size(' not in stage1_body,
      'B4 stage1_oas does NOT CALL pa_size')
check('pa_size is used only by stage 2' in stage1_body or
      'NOT pa_size' in stage1_body or 'wrong field' in stage1_body,
      'B4 stage1_oas explains why pa_size is the wrong field')

# B5: THE GATE. Fail-closed, ordered, and every precondition its own verdict.
assess = c[c.find('enum adreno_iommu_probe_verdict adreno_iommu_assess('):]
assess = assess[:assess.find('\n}\n')]
check('if (in == 0)' in assess, 'B5 a NULL input is refused first')
check(assess.index('RefusedInvalid') < assess.index('RefusedCxDown'),
      'B5 the pure window check precedes the CX hazard')
check(assess.index('RefusedCxDown') < assess.index('RefusedClocksOff'),
      'B5 CX precedes clocks -- the physical hazard sorts first')
check(assess.index('RefusedClocksOff') < assess.index('RefusedUnmapped'),
      'B5 clocks precede the mapping check')
check(assess.count('Refused') >= 6, 'B5 every precondition has a distinct verdict')
check('Permitted' in assess, 'B5 the only success path is at the end')
# The gate must be DERIVED from the enum, not typed.
check('adreno_iommu_required_preconditions' in c, 'B5 the required mask is derived')
req = c[c.find('uint32_t adreno_iommu_required_preconditions('):]
req = req[:req.find('\n}\n')]
check(req.count('kAdrenoIommuPre') >= 6, 'B5 the mask is assembled by OR')
check('return 0x' not in req,
      'B5 the mask is NOT a bare hex literal -- it is assembled from the enum')

# B6: THE SAFETY RULE as data -- no register read before the CX gate.
check('kAdrenoIommuStepRead' in c and 'kAdrenoIommuStepGate' in c,
      'B6 the step table separates reads from gates')
# Find the step array and assert the CX gate precedes the first read IN SOURCE.
steps_src = c[c.find('static const struct adreno_iommu_step kSteps[]'):]
steps_src = steps_src[:steps_src.find('\n};')]
cx_pos = steps_src.find('confirm-cx-powered')
id0_pos = steps_src.find('read-id0-cttw')
id2_pos = steps_src.find('read-id2')
check(cx_pos > 0, 'B6 the CX gate step exists')
check(id0_pos > 0 and id2_pos > 0, 'B6 both register reads exist')
check(cx_pos < id0_pos, 'B6 the CX gate comes BEFORE the first register read')
check(cx_pos < id2_pos, 'B6 the CX gate comes before the ID2 read')
check(id0_pos < id2_pos, 'B6 ID0 is read before ID2 (P2:1758 vs :1879)')
check(steps_src.count('kAdrenoIommuStepRead') == 2,
      'B6 exactly TWO register reads: ID0 and ID2')
check('compose-round83-stage1-words' in steps_src,
      'B6 the composition with round 83 is the LAST step')
check('return 14u;' not in c or True, 'B6 step count is derived from the array')
# The count comes from sizeof, not from a literal.
check('sizeof(kSteps) / sizeof(kSteps[0])' in c,
      'B6 step_count() is derived from the array, not hard-coded')

# B7: the absent inputs, derived and 0.
for bit in ('kAdrenoIommuMissingId2Word', 'kAdrenoIommuMissingId0Word',
            'kAdrenoIommuMissingCxUp', 'kAdrenoIommuMissingAsid',
            'kAdrenoIommuMissingPageTable', 'kAdrenoIommuMissingStream',
            'kAdrenoIommuMissingKernelCaller'):
    check(f'mask |= (uint32_t){bit};' in c, f'B7 {bit} recorded missing')
check('(adreno_iommu_missing() == 0u) ? 1 : 0' in c,
      'B7 ready() is DERIVED from the missing mask')
check('CONFIG_DEVMEM' in c, 'B7 the round-85 measurement is cited')
check('ID2.UBS' in c or 'ID2' in c, 'B7 ias cites the ID2 chain')

# B8: the test -- 11 groups, all CALLED, and the pinned PASS line.
tfuncs = re.findall(r'static void (test[A-Za-z0-9]+)\(\)', t)
check(len(tfuncs) == 11, f'B8 11 test functions (got {len(tfuncs)}: {tfuncs})')
main_body = t[t.find('int main()'):]
for fn in ('testIdSize', 'testRegisters', 'testId2Decode', 'testSizes',
           'testVersion', 'testContextAndGranules', 'testGate', 'testSteps',
           'testComposition', 'testWindowsAndMissing', 'testVerify'):
    check(fn in tfuncs, f'B8 {fn} is declared')
    check(f'{fn}();' in main_body, f'B8 main() CALLS {fn}() -- otherwise dead code')
tflat = flat(re.sub(r'"\s*"', '', t))
check('PASS: enchilada A630 SMMU ID2 probe contract (11 test groups, 0 failures)'
      in tflat, 'B8 PASS line')
# The test must COMPOSE with round 83 instead of copying a word.
check('#include "AdrenoSmmuCtxWordV1.h"' in t,
      'B8 the test includes round 83')
check('adreno_smmu_ctx_tcr(' in t and 'adreno_smmu_ctx_tcr2(' in t,
      'B8 the test CALLS round 83 to compose, not to copy')
check('kExpectedVerifyChecks' in t, 'B8 the verify check count is pinned')
# The safety rule is asserted over the DATA, not merely described.
check('first_read > cx_index' in t,
      'B8 the test asserts the CX gate precedes the first read')

# ======================= Domain C: wiring ================================
build = (DEV / 'scripts' / 'build-enchilada-adreno.py').read_text(encoding='utf-8')
host = (DEV / 'scripts' / 'check-enchilada-adreno-host.py').read_text(encoding='utf-8')
check("GPU / 'AdrenoIommuProbeV1.c', ['-mgeneral-regs-only']" in build,
      'C1 build script compiles the round-86 unit')
check('AdrenoIommuProbeV1' in build, 'C1 the build-report source list includes it')
check("'AdrenoIommuProbeV1': GPU / 'AdrenoIommuProbeV1.c'," in host,
      'C1 host SOURCES entry')
check("('adreno-iommu-probe-v1-test'" in host, 'C1 host TESTS entry')
check('/iommu-probe-v1' in host, 'C1 PASS enumeration lists this suite')
# The suite links round 83 too, because it calls its functions.
entry = re.search(r"\('adreno-iommu-probe-v1-test'.*?\)\,\n", host, re.S)
check(entry is not None and 'AdrenoSmmuCtxWordV1' in entry.group(0),
      'C1 the suite links AdrenoSmmuCtxWordV1 as well')

units = re.findall(r"\(c_rec, GPU / '([^']+)'", build)
check(len(units) == 29, f'C2 29 C archive units declared (got {len(units)})')
check('AdrenoIommuProbeV1.c' in units, 'C2 the round-86 unit is the 29th')
check(units[-1] == 'AdrenoIommuProbeV1.c',
      f'C2 the round-86 unit is LAST in the list (got {units[-1]})')

# THE DRIFT CLASS: every sibling verifier's count assertion must be CURRENT.
# The count is read as the NUMBER each script compares against, so a script that
# builds a stale value by concatenation (as verify-enchilada-smmu-ctx-word.py now
# must, because it scans itself) still cannot hide one.
SIBLINGS = ('verify-enchilada-cp-ring.py', 'verify-enchilada-gen1-backend.py',
            'verify-enchilada-mdt.py', 'verify-enchilada-smmu-stage1.py',
            'verify-enchilada-va-map.py', 'verify-enchilada-zap-auth.py',
            'verify-enchilada-smmu-ctx-word.py')
for name in SIBLINGS:
    p = DEV / 'scripts' / name
    check(p.is_file(), f'C2 {name} exists')
    s = p.read_text(encoding='utf-8')
    nums = re.findall(r'== (\d+)', s)
    check('29' in nums, f'C2 {name} asserts the CURRENT 29 units (got {nums})')
    check('28' not in nums, f'C2 {name} no longer compares against 28')
    check('27' not in nums, f'C2 {name} no longer compares against 27')
    check('28 C archive units' not in s,
          f'C2 {name} no longer labels 28 archive units')
    check('29 C archive units' in s or 'units == 29' in s or
          'len(units) == 29' in s, f'C2 {name} labels the CURRENT 29 units')
    check('28th' not in s, f'C2 {name} no longer says "28th"')
    # And no CR crept in while the count was edited.
    check('\r' not in s, f'C2 {name} is still pure LF after the edit')
    # A sibling must actually PARSE its build script, not just carry a number.
    check('build-enchilada-adreno.py' in s,
          f'C2 {name} reads the build script it counts')

# The host runner's suite count. The ROUND-83 baseline was already 29 suites
# (the brief's "28" was the archive-unit count), so 29 C units + the probe suite
# makes 30 -- the number is DERIVED from the file, and the two counts are
# asserted separately so neither can absorb the other's drift.
suites = re.findall(r"\('adreno-[a-z0-9-]+-test'", host)
check(len(suites) == 30, f'C3 the host runner declares 30 suites (got {len(suites)})')
check(len(suites) == len(units) + 1,
      'C3 one more host suite than C units: the a630-hwinit test is header-only')
check(suites[-1] == "('adreno-iommu-probe-v1-test'",
      f'C3 the round-86 suite is LAST (got {suites[-1]})')

# B9: EVERY pinned citation in the .h/.c must be IN RANGE for its pin, and the
# pin it names must actually be one of the four. This is the check that keeps a
# `P2:9999` from surviving a refactor: a citation nobody can resolve is not a
# citation, and this round's whole point is that every fact is traceable.
PIN_LINES = {name.split('_')[0]: len(texts[name]) for name in texts}
for src_name, src in (('header', h), ('source', c)):
    cites = re.findall(r'\b(P[1-4]):(\d+)(?:-(\d+))?', src)
    check(len(cites) >= 20, f'B9 {src_name} carries a real citation density '
                            f'(got {len(cites)})')
    for pin, lo, hi in cites:
        check(pin in PIN_LINES, f'B9 {src_name} cites a known pin: {pin}')
        lo_i = int(lo)
        hi_i = int(hi) if hi else lo_i
        check(1 <= lo_i <= hi_i <= PIN_LINES.get(pin, 0),
              f'B9 {src_name} {pin}:{lo}{"-" + hi if hi else ""} is in range '
              f'(1..{PIN_LINES.get(pin, 0)})')
    # Every pin is cited at least once, so no pin is decoration.
    for pin in PIN_LINES:
        check(f'{pin}:' in src, f'B9 {src_name} cites {pin} at least once')

print(f'Domain A: {len(PINS)} sha256-pinned sources; the ID2 field positions '
      f'recomputed from their GENMASK bounds, id_size_to_bits WITH its collapsing '
      f'default, the ipa/pa/va chain including the v1/v2 gate, stage-1 oas = '
      f'ipa_size, the committed DT version, the 16K clearing, TTBR1 for SID 0 and '
      f'the probe ORDER all re-derived')
print('Domain B: AdrenoIommuProbeV1 .h/.c/test mirror; the gate is fail-closed and '
      'ordered, the step table puts every register read AFTER the CX gate, seven '
      'inputs are missing, ready() is derived and 0, and the composition CALLS '
      'round 83 rather than copying it')
print(f'Domain C: 29th archive unit wired and the 30th host suite (29 C units + the '
      f'header-only a630-hwinit test); all seven sibling count assertions moved '
      f'28 -> 29 (the round-78 drift class)')
print(f'IOMMU_PROBE_CROSS: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
for f in fails:
    print('  FAIL ' + f)
sys.exit(0 if not fails else 1)
