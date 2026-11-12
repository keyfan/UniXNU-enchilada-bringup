#!/usr/bin/env python3
"""ROUND 66 method-2 cross-verifier for the A630 Darwin-KMD ABI directory
contract. Two INDEPENDENT domains:

  Domain A - re-derive the wire ABI + the A630 facts from SEVEN sha256-pinned
             upstream sources at the EXACT Mesa commit UniXNU builds.
  Domain B - audit the new directory contract (.h/.c/test) against those
             derived facts, including the honesty gate on DeviceInfo.

This is a HOST-VERIFIED source/layout check. No device run, no flash, no CI,
no Turnip dylib build. Run: python -X utf8 scripts/verify-enchilada-darwin-kmd-abi.py
"""
import hashlib
import json
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
ONEPLUS = Path(__file__).resolve().parents[3]
UP = ONEPLUS / 'work' / 'UniXNU'
GPU = DEV / 'drivers' / 'NativeAdreno'
DIR = GPU / 'AdrenoDarwinKmdAbiV1'
CACHE = DEV / '.cache' / 'adreno-a630-source'

PINS = [
    ('P1_ABI', UP / 'drivers' / 'Adreno740' / 'NativeAdrenoABI.h', 7733,
     '20ac9298bb390d86788c1fc58155e1f9581ffa942e222752efc95c67cb5670d2'),
    # The pinned Mesa device table is vendored into scripts/data/ (TRACKED,
    # not gitignored .cache/) because GitLab's Anubis anti-bot challenge blocks
    # plain urllib re-fetch on CI; a pushed verification command must be
    # reproducible from the repository alone.
    ('P2_MESA', DEV / 'scripts' / 'data' / 'freedreno_devices.85fda3b3.py', 53914,
     '3a343e614b30509a01db4a22380eaa4604abafbb59552cd8b177c5912b29aef6'),
    ('P3_DTB', ONEPLUS / 'notes' / 'mu-silicium' / 'enchilada.dts', 784857,
     'e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275'),
    ('P4_CATALOG', CACHE / 'drivers' / 'gpu' / 'drm' / 'msm' / 'adreno' / 'a6xx_catalog.c', 51692,
     '6d9061ee08308126adc7b7250810b0223ee01c678e7afab6eb1738e53524c24c'),
    ('P5_COMPILER', UP / 'scripts' / 'data' / 'adreno-compiler-inputs.json', 2550,
     '04089993e4f4d268b660bb0e6eba3d202225ab93389b8e68d67e10244381dcad'),
    ('P6_PATCH', UP / 'patches' / 'mesa' / 'turnip-darwin-kmd.patch', 56241,
     '7f4690131a05133547af55efda9ed9b4fcd10d8b5e420cccbcfa64b97eda8911'),
    ('P7_BUILD_TURNIP', ONEPLUS / 'work' / 'fajita-out' / 'fajita' / 'tree' /
     'scripts' / 'build-turnip-darwin.py', 7931,
     '5db3832ee578d85f82840ef218a0450430bf004641526c0932a3e9d7c2999a53'),
]

CHECKS = 0
FAILS = []


def check(cond, label):
    global CHECKS
    CHECKS += 1
    if not cond:
        FAILS.append(label)


def read(name):
    for key, path, size, sha in PINS:
        if key == name:
            return path.read_bytes().decode('utf-8')
    raise KeyError(name)


def main():
    # ------------------------------------------------------------------ Domain A
    # A1: all seven pins are byte-exact.
    for key, path, size, sha in PINS:
        check(path.is_file(), f'A1 {key} exists')
        if not path.is_file():
            continue
        data = path.read_bytes()
        check(len(data) == size, f'A1 {key} size {len(data)} == {size}')
        check(hashlib.sha256(data).hexdigest() == sha, f'A1 {key} sha256')
        check(b'\r' not in data, f'A1 {key} pure LF')

    abi = read('P1_ABI')

    # A2: derive the wire-ABI enum constants from P1 (not from the new header).
    consts = {
        'NativeAdrenoAbiVersion': 2,
        'NativeAdrenoAbiMaxBuffers': 128,
        'NativeAdrenoAbiMaxBoBytesShift': 28,
        'NativeAdrenoAbiMetadataBytes': 64,
        'NativeAdrenoAbiMaxQueues': 8,
        'NativeAdrenoParamsHostImport': 1,
        'NativeAdrenoDeviceClientIova': 1,
        'NativeAdrenoDeviceCachedCoherent': 2,
    }
    derived_consts = {}
    for k, want in consts.items():
        m = re.search(r'\b' + k + r'\s*=\s*(\d+)', abi)
        check(m is not None and int(m.group(1)) == want, f'A2 {k} == {want}')
        if m:
            derived_consts[k] = int(m.group(1))
    methods = re.findall(r'\bNativeAdrenoMethod(\w+)\s*=\s*(\d+)', abi)
    check(len(methods) == 16, f'A2 16 methods (got {len(methods)})')
    derived_methods = {('NativeAdrenoMethod' + n): int(v) for n, v in methods}
    check(derived_methods.get('NativeAdrenoMethodQuery') == 1, 'A2 Query == 1')
    check(derived_methods.get('NativeAdrenoMethodBoImport') == 16, 'A2 BoImport == 16')
    check(sorted(derived_methods.values()) == list(range(1, 17)), 'A2 methods are 1..16')

    # A3: derive the 16 static_assert sizes from P1.
    asserts = re.findall(r'static_assert\(sizeof\((\w+)\)\s*==\s*([^,]+),', abi)
    check(len(asserts) == 16, f'A3 16 static_asserts (got {len(asserts)})')
    derived_sizes = {}
    for typ, expr in asserts:
        expr = expr.strip()
        if expr.isdigit():
            derived_sizes[typ] = int(expr)
        elif 'NativeAdrenoAbiMaxBuffers' in expr:
            derived_sizes[typ] = 32 + 24 * derived_consts['NativeAdrenoAbiMaxBuffers']
    check(derived_sizes.get('NativeAdrenoAbiParams') == 24, 'A3 params 24')
    check(derived_sizes.get('NativeAdrenoAbiDeviceInfo') == 64, 'A3 device_info 64')
    check(derived_sizes.get('NativeAdrenoAbiSubmit') == 32 + 24 * 128, 'A3 submit 3104')
    check(derived_sizes.get('NativeAdrenoAbiMetadata') == 80, 'A3 metadata 80')

    # A4: derive the 20 upstream struct names from P1.
    structs = set(re.findall(r'\bstruct\s+(NativeAdrenoAbi\w+)\s*\{', abi))
    check(len(structs) == 20, f'A4 20 structs (got {len(structs)})')
    check('NativeAdrenoAbiDeviceInfo' in structs, 'A4 DeviceInfo present')
    check('NativeAdrenoAbiSubmit' in structs, 'A4 Submit present')

    # A5: the pinned Mesa commit (the EXACT one UniXNU builds) carries A630.
    mesa = read('P2_MESA')
    mesa_pin = '85fda3b36405bcaea8bffc64aa4f2651dc90770d'
    for tok in ['GPUId(630)', 'CHIP.A6XX', '[a6xx_base, a6xx_gen1]',
                'highest_bank_bit = 15', 'macrotile_mode = 0', 'num_ccu = 2',
                'a6xx_gen1 = GPUProps(', 'reg_size_vec4 = 96',
                'instr_cache_size = 64', 'REG_A6XX_UCHE_UNKNOWN_0E12, 0x10000001']:
        check(tok in mesa, f'A5 Mesa pin token {tok!r}')
    check('GPUId(740)' in mesa and 'CHIP.A7XX' in mesa and '0x43050a01' in mesa,
          'A5 A740 is a different generation')
    check('Making sure you' not in mesa and 'not a bot' not in mesa.lower(),
          'A5 no anti-bot HTML in the pinned file')

    # A6: the DTB GPU facts (same DTB behind device-verified stages 3/4/7).
    dtb = read('P3_DTB')
    for tok in ['qcom,kgsl-3d0@5000000', 'qcom,chipid = <0x6030001>',
                'qcom,highest-bank-bit = <0x0f>', 'qcom,ubwc-mode = <0x02>',
                'qcom,pas-id = <0x0d>', 'qcom,firmware-name = "a630_zap"']:
        check(tok in dtb, f'A6 DTB token {tok!r}')

    # A7: the pinned catalog A630 entry.
    cat = read('P4_CATALOG')
    i = cat.find('0x06030001')
    check(i > 0, 'A7 catalog has 0x06030001')
    # Isolate EXACTLY the A630 entry: from its own .chip_ids to the next
    # entry's .chip_ids. A fixed +/- window would also catch the neighbouring
    # A615/A640 entries (which DO set .address_space_size = SZ_16G).
    start = cat.rfind('.chip_ids', 0, i)
    end = cat.find('.chip_ids', i)
    check(start > 0 and end > start, 'A7 A630 entry bounded by .chip_ids')
    entry = cat[start:end]
    for tok in ['0x06030001', '0x06030002', 'ADRENO_6XX_GEN1', 'revn = 630',
                'SZ_1M', 'a630_zap.mdt', 'a630_gmu.bin', 'a630_sqe.fw']:
        check(tok in entry, f'A7 catalog A630 token {tok!r}')
    check('address_space_size' not in entry, 'A7 A630 has no address_space_size')

    # A8: compiler inputs pin the exact commit + the darwin KMD patch.
    comp = json.loads(read('P5_COMPILER'))
    check(comp['mesa']['commit'] == mesa_pin, 'A8 compiler mesa.commit == pin')
    check('patches/mesa/turnip-darwin-kmd.patch' in comp['patches'],
          'A8 darwin KMD patch is in the build')

    # A9: the patch makes tu-darwin-abi-dir a ONE-HEADER directory contract.
    patch = read('P6_PATCH')
    check("option('tu-darwin-abi-dir'" in patch, 'A9 meson option declared')
    check("freedreno_kmds.contains('darwin')" in patch, 'A9 darwin branch')
    check("include_directories(get_option('tu-darwin-abi-dir')" in patch,
          'A9 include_directories points at the passed dir')
    check("+#include \"NativeAdrenoABI.h\"" in patch, 'A9 single header include')
    check(patch.count('NativeAdrenoABI.h') >= 2, 'A9 header referenced >= 2x')
    # The patch does NOT touch the device table: A630 support comes from Mesa.
    for forbidden in ['GPUId(630)', 'a6xx_gen1', 'freedreno_devices']:
        check(forbidden not in patch, f'A9 patch does not edit device table ({forbidden})')

    # A10: build-turnip-darwin.py passes the directory + darwin KMD.
    bt = read('P7_BUILD_TURNIP')
    check("'-Dfreedreno-kmds=darwin'" in bt, 'A10 meson -Dfreedreno-kmds=darwin')
    check("'-Dtu-darwin-abi-dir='" in bt, 'A10 meson -Dtu-darwin-abi-dir=')
    check("drivers/Adreno740" in bt, 'A10 upstream points the option at Adreno740')
    check(mesa_pin in read('P5_COMPILER'), 'A10 explicit pinned commit present')

    # ------------------------------------------------------------------ Domain B
    hdr_path = DIR / 'NativeAdrenoABI.h'
    c_path = DIR / 'AdrenoDarwinKmdAbiV1.c'
    test_path = GPU / 'tests' / 'adreno-darwin-kmd-abi-v1-test.cpp'
    for key, p in [('B1 header', hdr_path), ('B1 source', c_path), ('B1 test', test_path)]:
        check(p.is_file(), f'{key} exists')
    if not (hdr_path.is_file() and c_path.is_file() and test_path.is_file()):
        return finish()
    hdr = hdr_path.read_bytes().decode('utf-8')
    src = c_path.read_bytes().decode('utf-8')
    test = test_path.read_bytes().decode('utf-8')
    for key, t in [('header', hdr), ('source', src), ('test', test)]:
        check('\r' not in t, f'B1 {key} pure LF')

    # B2: EVERY upstream constant is mirrored with the SAME name and value.
    for k, want in derived_consts.items():
        m = re.search(r'\b' + k + r'\s*=\s*(\d+)', hdr)
        check(m is not None and int(m.group(1)) == want, f'B2 mirror {k} == {want}')
    hm = re.findall(r'\bNativeAdrenoMethod(\w+)\s*=\s*(\d+)', hdr)
    hmethods = {('NativeAdrenoMethod' + n): int(v) for n, v in hm}
    check(hmethods == derived_methods, 'B2 all 16 method names/values mirrored')

    # B3: EVERY upstream struct name is mirrored.
    hstructs = set(re.findall(r'\bstruct\s+(NativeAdrenoAbi\w+)\s*\{', hdr))
    check(hstructs == structs, f'B3 struct set identical (new-only={hstructs - structs}, missing={structs - hstructs})')

    # B4: the 16 layout asserts are present in BOTH the C++ and C11 branches.
    cpp_asserts = re.findall(r'\bstatic_assert\(sizeof\(struct (\w+)\)\s*==\s*([^,]+),', hdr)
    c_asserts = re.findall(r'\b_Static_assert\(sizeof\(struct (\w+)\)\s*==\s*([^,]+),', hdr)
    check(len(cpp_asserts) == 16, f'B4 16 C++ static_asserts (got {len(cpp_asserts)})')
    check(len(c_asserts) == 16, f'B4 16 C11 _Static_asserts (got {len(c_asserts)})')
    for typ, expr in cpp_asserts:
        want = derived_sizes.get(typ)
        ok = (expr.strip().isdigit() and int(expr.strip()) == want) or \
             (want == 32 + 24 * 128 and 'NativeAdrenoAbiMaxBuffers' in expr)
        check(ok, f'B4 C++ assert {typ} matches P1')

    # B5: the four A630 pins equal the source-derived values.
    pin_map = {
        'ENCHILADA_A630_CHIP_ID': ('0x06030001u', 'A5/DTB/catalog'),
        'ENCHILADA_A630_CHIP_ID_ALT': ('0x06030002u', 'catalog'),
        'ENCHILADA_A630_GMEM_BYTES': ('0x00100000u', 'catalog SZ_1M'),
        'ENCHILADA_A630_HIGHEST_BANK_BIT': ('15u', 'DTB 0x0f + Mesa'),
        'ENCHILADA_A630_MACROTILE_MODE': ('0u', 'Mesa macrotile_mode = 0'),
        'ENCHILADA_A630_UBWC_MODE': ('2u', 'DTB 0x02'),
        'ENCHILADA_A630_ZAP_PAS_ID': ('13u', 'DTB 0x0d'),
    }
    for macro, (val, _) in pin_map.items():
        m = re.search(r'#define\s+' + macro + r'\s+(\S+)', hdr)
        check(m is not None and m.group(1) == val, f'B5 {macro} == {val}')
    # Cross-check the numeric pins against the actual source strings.
    check('0x06030001' in cat and '0x06030001' in hdr, 'B5 chip_id from catalog')
    check('SZ_1M' in entry and '0x00100000u' in hdr, 'B5 gmem from catalog')
    check('qcom,highest-bank-bit = <0x0f>' in dtb and '15u' in hdr, 'B5 bank bit from DTB')
    check('highest_bank_bit = 15' in mesa, 'B5 bank bit confirmed at pinned Mesa')
    check('macrotile_mode = 0' in mesa, 'B5 macrotile from pinned Mesa')

    # B6: field-mask arithmetic (4 specified = 0x143, 7 derived = 0x6bc).
    fields = re.findall(r'\bkEnchiladaDarwinField(\w+)\s*=\s*1u\s*<<\s*(\d+)', hdr)
    check(len(fields) == 11, f'B6 11 device fields (got {len(fields)})')
    spec_names = {'ChipId', 'GmemBytes', 'HighestBankBit', 'MacrotileMode'}
    spec_bits = [1 << int(b) for n, b in fields if n in spec_names]
    check(sorted(spec_bits) == [1, 2, 64, 256], 'B6 specified bits are 0,1,6,8')
    check(sum(spec_bits) == 0x143, 'B6 specified mask == 0x143')
    all_bits = [1 << int(b) for _, b in fields]
    check(sum(all_bits) == 0x7ff, 'B6 field_all == 0x7ff')
    check(0x7ff - sum(spec_bits) == 0x6bc, 'B6 derived mask == 0x6bc')

    # B7: the .c producer is honest - only 4 fields filled, 8 left zero,
    # and the consistency gate refuses fabricated/wrong values.
    for tok in ['out->chip_id = ENCHILADA_A630_CHIP_ID',
                'out->gmem_bytes = ENCHILADA_A630_GMEM_BYTES',
                'out->highest_bank_bit = ENCHILADA_A630_HIGHEST_BANK_BIT',
                'out->macrotile_mode = ENCHILADA_A630_MACROTILE_MODE']:
        check(tok in src, f'B7 source pins {tok!r}')
    check(src.count('out->') == 4, 'B7 .c writes exactly the 4 pinned fields')
    for gate in ['info->gmem_base != 0u', 'info->uche_trap_base != 0u',
                 'info->va_start != 0u', 'info->va_size != 0u',
                 'info->ubwc_swizzle != 0u', 'info->ubwc_version != 0u',
                 'info->flags != 0u', 'info->reserved != 0u']:
        check(gate in src, f'B7 honesty gate {gate!r}')
    check('specified_mask != ENCHILADA_A630_SPECIFIED_FIELDS' in src,
          'B7 wrong-mask refused')
    check('info == (void *)0' in src, 'B7 NULL refused')
    for forbidden in ['writel', 'reg_write', 'gmu_write', 'volatile uint32_t *',
                      'mmio', 'IOConnectCallStructMethod']:
        check(forbidden not in src and forbidden not in hdr,
              f'B7 no MMIO/IOKit ({forbidden})')
    check('zero_bytes' in src and '#include' in src, 'B7 freestanding zeroing')

    # B8: the host test covers 6 groups and uses the derived masks.
    groups = re.findall(r'\bstatic void (test\w+)\(', test)
    check(len(groups) == 6, f'B8 6 test groups (got {len(groups)})')
    check('0x00000143u' in test and '0x000006bcu' in test,
          'B8 test uses derived masks 0x143/0x6bc')
    check('PASS: enchilada A630 Darwin-KMD ABI directory contract' in test,
          'B8 PASS line')
    check('NativeAdrenoABI.h' in test, 'B8 test includes the directory header')

    # B9: the directory contract is a single-header drop-in, honestly scoped.
    headers = [p.name for p in DIR.iterdir() if p.suffix == '.h']
    check(headers == ['NativeAdrenoABI.h'], f'B9 exactly one header (got {headers})')
    for tok in ['ROUND 66', 'HOST-VERIFIED', 'NOT a GPU driver',
                'STAYS "host-verified"', 'never from a guess',
                '85fda3b36405bcaea8bffc64aa4f2651dc90770d']:
        check(tok in hdr, f'B9 header boundary {tok!r}')
    check('no device run' in hdr.lower() and 'no flash' in hdr.lower(),
          'B9 header states no device run / no flash')

    return finish()


def finish():
    if FAILS:
        for f in FAILS:
            print(f'  FAIL {f}')
        print(f'DARWIN_KMD_ABI_CROSS: FAIL ({CHECKS} checks, {len(FAILS)} failures)')
        return 1
    print('Domain A: 7 sha256-pinned upstream sources, 8 ABI constants, '
          '16 methods, 16 layout asserts, 20 structs, A630 at the exact Mesa pin')
    print('Domain B: 1-header directory contract, 4 A630 pins, 0x143/0x6bc masks, '
          'honesty gate, 6 host-test groups, no MMIO')
    print(f'DARWIN_KMD_ABI_CROSS: PASS ({CHECKS} checks, 0 failures)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
