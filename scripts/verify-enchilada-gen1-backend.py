#!/usr/bin/env python3
"""Round 69 method-2 cross-verifier for AdrenoGen1BackendV1.

Domain A re-derives every A630 backend fact from SEVEN sha256-pinned sources
(Linux v6.12 msm a6xx_gpu.c / a6xx_gmu.c / a6xx_catalog.c / a6xx_hfi.c /
a6xx.xml, gpucc-sdm845.c, the vendor DTB) and re-derives the two clock counts
from the DTB itself. Domain B asserts the freestanding C module mirrors those
facts and records the twelve prerequisites honestly.

The verifier NEVER compiles or runs the module and never touches the network:
it reads the pinned bytes and the deliverable text only. Two independent
directions: byte/sha pins + semantic line derivation (A), source-structure
assertions on the deliverable (B).
"""
import hashlib
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
ONEPLUS = Path(__file__).resolve().parents[3]
WS = ONEPLUS.parent
CACHE = DEV / '.cache' / 'adreno-a630-source'
DTB = WS / 'oneplus6-ios' / 'notes' / 'mu-silicium' / 'enchilada.dts'
GPU = DEV / 'drivers' / 'NativeAdreno'

PINS = [
    ('P1_a6xx_gpu_c', CACHE / 'drivers/gpu/drm/msm/adreno/a6xx_gpu.c', 69087,
     'c33479377574ed193e82c272b3708e19a2b2153088f246e2639d8a43fef943a2'),
    ('P2_a6xx_gmu_c', CACHE / 'drivers/gpu/drm/msm/adreno/a6xx_gmu.c', 48981,
     'c14fbb253e602bfd2585d757da871dde08ea63b811ff3c6808f657f4522f78d4'),
    ('P3_a6xx_catalog_c', CACHE / 'drivers/gpu/drm/msm/adreno/a6xx_catalog.c', 51692,
     '6d9061ee08308126adc7b7250810b0223ee01c678e7afab6eb1738e53524c24c'),
    ('P4_a6xx_xml', CACHE / 'drivers/gpu/drm/msm/registers/adreno/a6xx.xml', 307513,
     '7f432c5e189f14ce559e0e7722c077ac778cfb63d64b2043c35d09b379bcd602'),
    ('P5_dtb', DTB, 784857,
     'e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275'),
    ('P6_gpucc_sdm845_c', CACHE / 'drivers/clk/qcom/gpucc-sdm845.c', 5176,
     '80c2fbef5c65df986f4c391bbeedbf53e8744e5dd326f0e05dc6b4021f542a54'),
    ('P7_a6xx_hfi_c', CACHE / 'drivers/gpu/drm/msm/adreno/a6xx_hfi.c', 22853,
     'f62c802d25475b86ac05cc58df012e088230ed64a0e9319456eb20ebb40c9faa'),
    ('P8_a6xx_gpu_h', CACHE / 'drivers/gpu/drm/msm/adreno/a6xx_gpu.h', 3251,
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
        check(False, f'A1 {name} file missing ({path})')
        texts[name] = []
        continue
    b = path.read_bytes()
    check(len(b) == size, f'A1 {name} size {len(b)} != {size}')
    check(hashlib.sha256(b).hexdigest() == sha, f'A1 {name} sha256 mismatch')
    check(b'\r' not in b, f'A1 {name} has CR')
    texts[name] = b.decode('utf-8').split('\n')


def line(name, number):
    """1-based line accessor, so anchors are quoted not paraphrased."""
    lines = texts[name]
    return lines[number - 1] if 0 < number <= len(lines) else ''


gpu_c, gmu_c, catalog_c, xml, dtb, gpucc_c, hfi_c, gpu_h = (
    texts[k] for k, *_ in PINS)

# ---- A2: GPU_PAS_ID 13, derived from TWO independent pins -----------------
check(re.search(r'#define\s+GPU_PAS_ID\s+13\b', line('P1_a6xx_gpu_c', 17)) is not None,
      'A2 P1:17 #define GPU_PAS_ID 13')
check('adreno_zap_shader_load(gpu, GPU_PAS_ID)' in line('P1_a6xx_gpu_c', 819),
      'A2 P1:819 zap loader receives GPU_PAS_ID')
check('static int a6xx_zap_shader_init' in line('P1_a6xx_gpu_c', 811),
      'A2 P1:811 zap_shader_init definition')
check('a6xx_zap_shader_init(gpu)' in line('P1_a6xx_gpu_c', 1164),
      'A2 P1:1164 call site')
check('SECVID_TRUST_CNTL' in ' '.join(gpu_c[1150:1185]),
      'A2 P1:1158-1180 no-zap fallback uses SECVID_TRUST_CNTL')
check(re.search(r'qcom,pas-id\s*=\s*<0x0d>', line('P5_dtb', 23612)) is not None,
      'A2 P5:23612 qcom,pas-id = <0x0d>')
check('"a630_zap"' in line('P5_dtb', 23613), 'A2 P5:23613 firmware-name a630_zap')
check('"qcom,pil-tz-generic"' in line('P5_dtb', 23611),
      'A2 P5:23611 kgsl-hyp is a pil-tz-generic node')
# The two encodings agree: 0x0d == 13.
check(int('0x0d', 16) == 13, 'A2 DTS 0x0d decodes to 13')

# ---- A3: A630 catalog entry ----------------------------------------------
check('ADRENO_6XX_GEN1' in line('P3_a6xx_catalog_c', 887),
      'A3 P3:887 family ADRENO_6XX_GEN1')
check('.revn = 630' in line('P3_a6xx_catalog_c', 888), 'A3 P3:888 revn 630')
check('0x06030001' in line('P3_a6xx_catalog_c', 884), 'A3 P3:884 chip id primary')
check('0x06030002' in line('P3_a6xx_catalog_c', 885), 'A3 P3:885 chip id alt')
check('.gmem = SZ_1M' in line('P3_a6xx_catalog_c', 893), 'A3 P3:893 gmem SZ_1M')
check('"a630_sqe.fw"' in line('P3_a6xx_catalog_c', 890), 'A3 P3:890 SQE firmware')
check('"a630_gmu.bin"' in line('P3_a6xx_catalog_c', 891), 'A3 P3:891 GMU firmware')
check('.zapfw = "a630_zap.mdt"' in line('P3_a6xx_catalog_c', 897),
      'A3 P3:897 zapfw is the .mdt (signed split chain)')
check('.gmu_cgc_mode = 0x00020202' in line('P3_a6xx_catalog_c', 901),
      'A3 P3:901 gmu_cgc_mode')
check('.prim_fifo_threshold = 0x00180000' in line('P3_a6xx_catalog_c', 902),
      'A3 P3:902 prim_fifo_threshold')
a630_block = ' '.join(catalog_c[882:903])
check('gmu_chipid' not in a630_block, 'A3 P3:882-903 A630 has NO gmu_chipid')
check('.hwcg = a630_hwcg' in line('P3_a6xx_catalog_c', 899), 'A3 P3:899 hwcg table')
check('.protect = &a630_protect' in line('P3_a6xx_catalog_c', 900),
      'A3 P3:900 protect table')

# ---- A4: legacy GMU / HFI v1 ---------------------------------------------
check('/* HFI v1, has sptprac */' in line('P2_a6xx_gmu_c', 1737),
      'A4 P2:1737 HFI v1 has sptprac')
check('gmu->legacy = true;' in line('P2_a6xx_gmu_c', 1738),
      'A4 P2:1738 A630 sets legacy')
check('adreno_is_a630_family(adreno_gpu)' in line('P2_a6xx_gmu_c', 1736),
      'A4 P2:1736 the A630 branch')
check('/* Mark legacy for manual SPTPRAC control */' in line('P2_a6xx_gmu_c', 1612),
      'A4 P2:1612 wrapper marks legacy')
check('if (!gmu->legacy)' in line('P7_a6xx_hfi_c', 59),
      'A4 P7:59 legacy read branch')
check('if (!gmu->legacy) {' in line('P7_a6xx_hfi_c', 90),
      'A4 P7:90 legacy write branch')
check('0xfafafafa' in line('P7_a6xx_hfi_c', 92),
      'A4 P7:92 the padding a legacy queue does NOT get')

# ---- A5: SPTPRAC ---------------------------------------------------------
check('int a6xx_sptprac_enable(struct a6xx_gmu *gmu)' in line('P2_a6xx_gmu_c', 365),
      'A5 P2:365 sptprac_enable')
check('0x778000' in line('P2_a6xx_gmu_c', 373), 'A5 P2:373 enable value 0x778000')
check('(val & 0x38) == 0x28' in line('P2_a6xx_gmu_c', 376),
      'A5 P2:376 poll (val & 0x38) == 0x28')
check(', 1, 100);' in line('P2_a6xx_gmu_c', 376),
      'A5 P2:376 poll delay 1..100 us')
check('void a6xx_sptprac_disable(struct a6xx_gmu *gmu)' in line('P2_a6xx_gmu_c', 387),
      'A5 P2:387 sptprac_disable')
check('0x778001' in line('P2_a6xx_gmu_c', 398), 'A5 P2:398 disable value 0x778001')
check('if (!gmu->legacy)' in line('P2_a6xx_gmu_c', 370),
      'A5 P2:370 both sptprac paths are legacy-only')

# ---- A6: GMU resume order and rates --------------------------------------
check('pm_runtime_get_sync(gmu->dev);' in line('P2_a6xx_gmu_c', 1033),
      'A6 P2:1033 CX runtime get')
check('if (!IS_ERR_OR_NULL(gmu->gxpd))' in line('P2_a6xx_gmu_c', 1040),
      'A6 P2:1040 GX guarded')
check('pm_runtime_get_sync(gmu->gxpd);' in line('P2_a6xx_gmu_c', 1041),
      'A6 P2:1041 GX runtime get')
check('clk_set_rate(gmu->core_clk, 200000000);' in line('P2_a6xx_gmu_c', 1044),
      'A6 P2:1044 core rate 200 MHz')
check('200000000 : 150000000);' in line('P2_a6xx_gmu_c', 1046),
      'A6 P2:1046 hub rate A740?200:150 MHz')
check('adreno_is_a740_family(adreno_gpu) ?' in line('P2_a6xx_gmu_c', 1045),
      'A6 P2:1045 the A740-family predicate')
check('clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks)' in line('P2_a6xx_gmu_c', 1047),
      'A6 P2:1047 clocks enabled last')
check('pm_runtime_put(gmu->gxpd);' in line('P2_a6xx_gmu_c', 1049) and
      'pm_runtime_put(gmu->dev);' in line('P2_a6xx_gmu_c', 1050),
      'A6 P2:1049-1050 reverse-order rollback')
check('REG_A6XX_GMU_GENERAL_7' in line('P2_a6xx_gmu_c', 1067) and
      'GMU_WARM_BOOT : GMU_COLD_BOOT' in line('P2_a6xx_gmu_c', 1068),
      'A6 P2:1066-1068 legacy cold/warm decision')

# ---- A7: GMU memory regions (and the icache NOTE) -----------------------
check('gmu->dummy.size = SZ_4K;' in line('P2_a6xx_gmu_c', 1696),
      'A7 P2:1696 dummy stays SZ_4K')
check('0x60400000, "debug"' in line('P2_a6xx_gmu_c', 1700),
      'A7 P2:1700 the a660/a7xx debug widening branch')
check('adreno_is_a660_family(adreno_gpu) ||' in line('P2_a6xx_gmu_c', 1697),
      'A7 P2:1697 widening is a660/a7xx-only')
check('0x60000000, "dummy"' in line('P2_a6xx_gmu_c', 1709),
      'A7 P2:1709 dummy IOVA 0x60000000')
check('when porting legacy' in line('P2_a6xx_gmu_c', 1721),
      'A7 P2:1721 the legacy porting NOTE')
check('you don\'t get random pagefaults' in line('P2_a6xx_gmu_c', 1723),
      'A7 P2:1723 the NOTE says legacy needs no icache')
check('SZ_16K, 0, "debug")' in line('P2_a6xx_gmu_c', 1741),
      'A7 P2:1741 A630 debug SZ_16K at IOVA 0')
check('SZ_16K, 0, "log")' in line('P2_a6xx_gmu_c', 1747),
      'A7 P2:1747 log SZ_16K at IOVA 0')
check('SZ_16K, 0, "hfi")' in line('P2_a6xx_gmu_c', 1752),
      'A7 P2:1752 hfi SZ_16K at IOVA 0')
check('SZ_256K - SZ_16K, 0x44000, "dcache")' in line('P2_a6xx_gmu_c', 1733),
      'A7 P2:1733 dcache exists only in the a640 branch')

# ---- A8: CP ring word offsets -------------------------------------------
check('<reg64 offset="0x0800" name="CP_RB_BASE"/>' in line('P4_a6xx_xml', 2281),
      'A8 P4:2281 CP_RB_BASE 0x0800 reg64')
check('<reg32 offset="0x0802" name="CP_RB_CNTL"/>' in line('P4_a6xx_xml', 2282),
      'A8 P4:2282 CP_RB_CNTL 0x0802')
check('<reg64 offset="0x0804" name="CP_RB_RPTR_ADDR"/>' in line('P4_a6xx_xml', 2283),
      'A8 P4:2283 CP_RB_RPTR_ADDR 0x0804 reg64')
check('<reg32 offset="0x0806" name="CP_RB_RPTR"/>' in line('P4_a6xx_xml', 2284),
      'A8 P4:2284 CP_RB_RPTR 0x0806')
check('<reg32 offset="0x0807" name="CP_RB_WPTR"/>' in line('P4_a6xx_xml', 2285),
      'A8 P4:2285 CP_RB_WPTR 0x0807')

# ---- A9: GBIF absent on A630 (a6xx_gpu.h, not a6xx_gpu.c) --------------
check('static inline bool a6xx_has_gbif(struct adreno_gpu *gpu)' in
      line('P8_a6xx_gpu_h', 71), 'A9 P8:71 a6xx_has_gbif')
check('if(adreno_is_a630(gpu))' in line('P8_a6xx_gpu_h', 73) and
      'return false;' in line('P8_a6xx_gpu_h', 74),
      'A9 P8:73-74 A630 has no GBIF')
check('return true;' in line('P8_a6xx_gpu_h', 76),
      'A9 P8:76 every other generation returns true')

# ---- A10: gpucc GDSC descriptors ----------------------------------------
check('static struct gdsc gpu_cx_gdsc = {' in line('P6_gpucc_sdm845_c', 119),
      'A10 P6:119 gpu_cx_gdsc')
check('.gdscr = 0x106c,' in line('P6_gpucc_sdm845_c', 120),
      'A10 P6:120 CX gdscr 0x106c')
check('.gds_hw_ctrl = 0x1540,' in line('P6_gpucc_sdm845_c', 121),
      'A10 P6:121 CX gds_hw_ctrl 0x1540')
check('static struct gdsc gpu_gx_gdsc = {' in line('P6_gpucc_sdm845_c', 130),
      'A10 P6:130 gpu_gx_gdsc')
check('.gdscr = 0x100c,' in line('P6_gpucc_sdm845_c', 131),
      'A10 P6:131 GX gdscr 0x100c')
check('.power_on = gdsc_gx_do_nothing_enable,' in line('P6_gpucc_sdm845_c', 135),
      'A10 P6:135 GX power_on does nothing (software)')

# ---- A11: the DTB's TWO clock lists, re-derived from the node ------------
def dtb_block(first, last):
    return ' '.join(dtb[first - 1:last])


gmu_node = dtb_block(23875, 23891)
gpu_node = dtb_block(23632, 23671)
m_gmu_names = re.search(r'clock-names = "([^"]+)"', gmu_node)
m_gpu_names = re.search(r'clock-names = "([^"]+)"', gpu_node)
check(m_gmu_names is not None, 'A11 DTB qcom,gmu has clock-names')
check(m_gpu_names is not None, 'A11 DTB qcom,kgsl-3d0 has clock-names')
gmu_clocks = m_gmu_names.group(1).split('\\0') if m_gmu_names else []
gpu_clocks = m_gpu_names.group(1).split('\\0') if m_gpu_names else []
check(len(gmu_clocks) == 4, f'A11 four GMU clocks (got {len(gmu_clocks)})')
check(len(gpu_clocks) == 6, f'A11 six GPU clocks (got {len(gpu_clocks)})')
check(gmu_clocks == ['gmu_clk', 'cxo_clk', 'axi_clk', 'memnoc_clk'],
      f'A11 GMU clock names {gmu_clocks}')
check(gpu_clocks == ['core_clk', 'rbbmtimer_clk', 'mem_clk', 'mem_iface_clk',
                     'gmu_clk', 'l3_vote'], f'A11 GPU clock names {gpu_clocks}')
check(len(gmu_clocks) != len(gpu_clocks), 'A11 the two counts are DISTINCT')
check(re.search(r'reg = <0x506a000 0x30000 0xb200000 0x300000>', gmu_node) is not None,
      'A11 P5:23878 GMU reg window 0x506a000+0x30000 (+PDC window)')
check('qcom,kgsl-smmu-v2' in dtb_block(23848, 23851),
      'A11 P5:23849 kgsl SMMU v2')
check(re.search(r'reg = <0x5040000 0x10000>', dtb_block(23848, 23851)) is not None,
      'A11 P5:23850 kgsl SMMU at 0x5040000+0x10000')
check('qcom,chipid = <0x6030001>' in gpu_node, 'A11 P5:23641 chipid 0x6030001')
check(re.search(r'reg = <0x5000000 0x40000', gpu_node) is not None,
      'A11 P5:23636 GPU register window 0x5000000+0x40000')
check('qcom,highest-bank-bit = <0x0f>' in gpu_node,
      'A11 P5:23647 highest bank bit 0x0f')
check('qcom,ubwc-mode = <0x02>' in gpu_node, 'A11 P5:23649 ubwc mode 2')
check('qcom,gpu-quirk-hfi-use-reg' in gpu_node,
      'A11 P5:23643 HFI-use-reg quirk present')

# ======================= Domain B: the deliverable =======================
h = (GPU / 'AdrenoGen1BackendV1.h').read_text(encoding='utf-8')
c = (GPU / 'AdrenoGen1BackendV1.c').read_text(encoding='utf-8')
t = (GPU / 'tests' / 'adreno-gen1-backend-v1-test.cpp').read_text(encoding='utf-8')

# B1: all seven pins recorded verbatim in the header provenance block
for name, path, size, sha in PINS:
    check(sha in h, f'B1 header records {name} sha256')

# B2: header mirrors the derived constants
B2 = [
    ('kEnchiladaGen1ChipId', '0x06030001u'),
    ('kEnchiladaGen1ChipIdAlt', '0x06030002u'),
    ('kEnchiladaGen1Revn', '630u'),
    ('kEnchiladaGen1GmemBytes', '0x00100000u'),
    ('kEnchiladaGen1GmuCgcMode', '0x00020202u'),
    ('kEnchiladaGen1PrimFifoThreshold', '0x00180000u'),
    ('kEnchiladaGen1HasGmuChipid', '0u'),
    ('kEnchiladaGen1GPasId', '13u'),
    ('kEnchiladaGen1SptpracEnable', '0x778000u'),
    ('kEnchiladaGen1SptpracDisable', '0x778001u'),
    ('kEnchiladaGen1SptpracPollMask', '0x38u'),
    ('kEnchiladaGen1SptpracPollTarget', '0x28u'),
    ('kEnchiladaGen1SptpracPollUsecMin', '1u'),
    ('kEnchiladaGen1SptpracPollUsecMax', '100u'),
    ('kEnchiladaGen1CoreRateHz', '200000000u'),
    ('kEnchiladaGen1HubRateHz', '150000000u'),
    ('kEnchiladaGen1HubRateHzA740', '200000000u'),
    ('kEnchiladaGen1CpRbBase', '0x0800u'),
    ('kEnchiladaGen1CpRbCntl', '0x0802u'),
    ('kEnchiladaGen1CpRbRptrAddr', '0x0804u'),
    ('kEnchiladaGen1CpRbRptr', '0x0806u'),
    ('kEnchiladaGen1CpRbWptr', '0x0807u'),
    ('kEnchiladaGen1GmuDebugBytes', '0x00004000u'),
    ('kEnchiladaGen1GmuLogBytes', '0x00004000u'),
    ('kEnchiladaGen1GmuHfiBytes', '0x00004000u'),
    ('kEnchiladaGen1GmuDummyBytes', '0x00001000u'),
    ('kEnchiladaGen1GmuDummyIova', '0x60000000u'),
    ('kEnchiladaGen1GmuRegionBytes', '0x0000d000u'),
]
for sym, val in B2:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B2 {sym}={val}')

# B3: firmware names mirror the catalog, and the PAS attribution is explicit
check('"a630_zap.mdt"' in h, 'B3 zapfw name is the .mdt')
check('"a630_sqe.fw"' in h, 'B3 SQE name')
check('"a630_gmu.bin"' in h, 'B3 GMU name')
check('"a630_zap"' in h, 'B3 DTS firmware-name')
check('NOT inherited from the A740/sheng tree' in c,
      'B3 PAS origin refuses the sheng attribution')
check('a6xx_gpu.c:17' in c and 'GPU_PAS_ID 13' in c, 'B3 PAS origin cites :17')
check('enchilada.dts:23612' in c, 'B3 PAS origin cites the DTB property')
check(h.count('static_assert') == 0 and h.count('_Static_assert') == 0,
      'B3 no layout asserts needed: the module declares no packed struct')

# B4: the resume enum is five ordered steps, and the count is asserted
check('kEnchiladaGen1ResumeStepCount    = 5u' in h or
      'kEnchiladaGen1ResumeStepCount = 5u' in h, 'B4 five resume steps')
for step in ('CxRuntimeGet', 'GxRuntimeGet', 'CoreRate', 'HubRate', 'Clocks'):
    check(f'kEnchiladaGen1Resume{step}' in h, f'B4 resume step {step}')

# B5: twelve prerequisites, in a fixed order, with a count sentinel
check('kEnchiladaGen1PrereqCount = 12u' in h, 'B5 twelve prerequisites')
for nm in ('PowerPath', 'Windows', 'GmuBoot', 'HfiV1', 'Firmware', 'ZapAuth',
           'SmmuStage1', 'CpRing', 'BoardVaMap', 'DeviceProof', 'ConfigOn',
           'MacosFrontend'):
    check(f'kEnchiladaGen1Prereq{nm}' in h, f'B5 prereq {nm}')

# B6: .c implements the whole API and stays freestanding
APIS = ['enchilada_gen1_backend_a630_pins', 'enchilada_gen1_backend_has_gbif',
        'enchilada_gen1_backend_gbif_reason',
        'enchilada_gen1_backend_gmu_resume',
        'enchilada_gen1_backend_hub_rate_hz',
        'enchilada_gen1_backend_gmu_regions',
        'enchilada_gen1_backend_has_icache_region',
        'enchilada_gen1_backend_gmu_region_bytes',
        'enchilada_gen1_backend_sptprac', 'enchilada_gen1_backend_pas_id',
        'enchilada_gen1_backend_pas_origin',
        'enchilada_gen1_backend_cp_ring_offsets',
        'enchilada_gen1_backend_admit',
        'enchilada_gen1_backend_prereq_name',
        'enchilada_gen1_backend_prereq_anchor',
        'enchilada_gen1_backend_verify']
for api in APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B6 .c defines {api}')
for bad in ('#include <string.h>', '#include <stdlib.h>', '#include <stdio.h>',
            'IOConnectCall', 'IOServiceOpen', 'writel', 'readl',
            'volatile uint32_t *', 'qcom_scm('):
    check(bad not in c, f'B6 .c has no {bad}')
check('#include "AdrenoGen1BackendV1.h"' in c, 'B6 .c includes its own header')

# B7: the adjudication is honest -- modeled != satisfied, 5 vs 0, BLOCKED
check('out->items[kEnchiladaGen1PrereqZapAuth].modeled = 1u;' in c,
      'B7 ZAP_AUTH is modelled since round 73 (the SHAPE only)')
check('out->items[kEnchiladaGen1PrereqCpRing].modeled = 1u;' in c,
      'B7 CP_RING is modelled since round 70 (the ORDER only)')
check(c.count('.modeled = 1u;') == 9, 'B7 exactly nine items are modelled (rounds 70/73/75/77)')
check(c.count('.satisfied') >= 4, 'B7 satisfied is a separate column')
check('kEnchiladaGen1AdmitBlocked' in c and
      'out->verdict = (out->satisfied_count == kEnchiladaGen1PrereqCount)' in c,
      'B7 verdict is tri-state, not a boolean')
check('modeled != satisfied' in h or 'modeled != satisfied' in flat(c),
      'B7 the distinction is stated in prose too')

# B8: the a660/a7xx widening must NOT be applied to A630
check('kEnchiladaGen1GmuDummyBytes     0x00001000u' in h, 'B8 dummy stays SZ_4K')
check('must NOT be applied to A630' in c, 'B8 the widening exclusion is stated')
check('SZ_8K' in c or '0x2000' in c, 'B8 the widened value is named as excluded')

# B9: host test shape
check(t.count('static void test') == 9, 'B9 9 test functions')
# The PASS line is a C string continued across two literals, so join them first.
tflat = t.replace('"\n                "', '')
check('PASS: enchilada A6XX_GEN1 backend admission contract (9 test groups, 0 failures)'
      in tflat, 'B9 PASS line')
for grp in ('testCatalogPins', 'testGbif', 'testGmuResumePlan', 'testGmuRegions',
            'testSptprac', 'testPasAndZap', 'testCpRingOffsets', 'testAdmission',
            'testSelfCheck'):
    check(f'{grp}();' in t, f'B9 {grp} invoked from main')

# B10: wiring -- 23rd unit + 23rd suite, and the archive list gains it
build = (DEV / 'scripts' / 'build-enchilada-adreno.py').read_text(encoding='utf-8')
host = (DEV / 'scripts' / 'check-enchilada-adreno-host.py').read_text(encoding='utf-8')
check("GPU / 'AdrenoGen1BackendV1.c', ['-mgeneral-regs-only']" in build,
      'B10 build script compiles the 23rd unit')
check("'AdrenoGen1BackendV1': GPU / 'AdrenoGen1BackendV1.c'," in host,
      'B10 host checker has the SOURCES entry')
check("('adreno-gen1-backend-v1-test', [GPU / 'tests/adreno-gen1-backend-v1-test.cpp'], ['AdrenoGen1BackendV1'])"
      in host, 'B10 host checker has the TESTS entry')
check("'AdrenoGen1BackendV1'" in build.split('sources = ')[1],
      'B10 build-report source list gains the stem')
check('/gen1-backend-v1' in host, 'B10 PASS enumeration updated')
# The unit count in the build script: 22 C units + the one C++ probe = 23.
units = re.findall(r"\(c_rec, GPU / '[^']+'", build)
check(len(units) == 28, f'B10 28 C archive units declared (got {len(units)})')

# B11: the FIVE local copies of GPU_PAS_ID must all agree with both pins.
# A constant known in five places and adjudicated in none is exactly the gap
# this round closes, so the agreement is asserted rather than assumed.
LOCAL_PAS = [
    ('AdrenoBoardInputs.h', GPU / 'AdrenoBoardInputs.h', r'ADRENO_BOARD_ZAP_PAS_ID\s+(\d+)u'),
    ('AdrenoFirmwareV1.h', GPU / 'AdrenoFirmwareV1.h', r'ADRENO_FW_V1_GPU_PAS_ID\s+(\d+)u'),
    ('AdrenoMetalSeamV1.h', GPU / 'AdrenoMetalSeamV1.h', r'ENCHILADA_METAL_A630_ZAP_PAS_ID\s+(\d+)u'),
    ('NativeAdrenoABI.h', GPU / 'AdrenoDarwinKmdAbiV1' / 'NativeAdrenoABI.h',
     r'ENCHILADA_A630_ZAP_PAS_ID\s+(\d+)u'),
    ('AdrenoGen1BackendV1.h', GPU / 'AdrenoGen1BackendV1.h',
     r'kEnchiladaGen1GPasId\s+(\d+)u'),
]
pas_values = {}
for label, path, pattern in LOCAL_PAS:
    text = path.read_text(encoding='utf-8') if path.is_file() else ''
    m = re.search(pattern, text)
    check(m is not None, f'B11 {label} declares the PAS id')
    pas_values[label] = int(m.group(1)) if m else None
check(all(v == 13 for v in pas_values.values() if v is not None),
      f'B11 every local PAS copy is 13 (got {pas_values})')
check(len(set(pas_values.values())) == 1,
      f'B11 the five local copies AGREE with each other (got {pas_values})')

print(f'Domain A: {len(PINS)} sha256-pinned sources; PAS 13, catalog, legacy HFI, '
      f'sptprac, resume order, GMU regions, CP ring, GDSCs, DTB clocks all re-derived')
print('Domain B: AdrenoGen1BackendV1 .h/.c/test mirror; 12 prerequisites adjudicated '
      '(modeled 9 since round 77, satisfied 0, BLOCKED); wiring is the 23rd unit and 23rd suite')
print(f'GEN1_BACKEND_CROSS: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
for f in fails:
    print('  FAIL ' + f)
sys.exit(0 if not fails else 1)
