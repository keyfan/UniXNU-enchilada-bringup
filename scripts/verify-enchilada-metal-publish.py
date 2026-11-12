#!/usr/bin/env python3
"""Round 67 method-2 cross-verifier for AdrenoMetalPublishV1.

Domain A re-derives the publish/discovery/client-window policy from SEVEN
sha256-pinned sources. Domain B asserts the new pure-C module mirrors those
facts honestly (and keeps every A630 GPU window ZERO, board-derived).
Two independent directions: byte/sha pins + semantic text derivation (A),
and source-structure assertions on the deliverable (B).
"""
import hashlib
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
ONEPLUS = Path(__file__).resolve().parents[3]
UP = ONEPLUS / 'work' / 'UniXNU'
GPU = DEV / 'drivers' / 'NativeAdreno'

PINS = [
    ('P1_publish_cpp', UP / 'drivers' / 'Adreno740' / 'NativeAdrenoPublish.cpp',
     48729, '013e43c09db8e5d3e444a3a0f1f56d14e381df47097689478811b20a162f5183'),
    ('P2_plugin_mm', UP / 'drivers' / 'AdrenoMetal' / 'NativeAdrenoMetalPlugin.mm',
     26233, '5105c1627020da7d404c7f7454f1d4887c09c3273fc54991c47f5d64e68e3454'),
    ('P3_adreno740_md', UP / 'docs' / 'drivers' / 'adreno740.md',
     7290, 'd7dec513732a66dbc0ffdd1ee707b15b71c4604664c0c09eb44fc658596f9bd3'),
    ('P4_add_gpu_plugin_data', UP / 'scripts' / 'add-gpu-plugin-data.py',
     6777, '51393cf9ec91faf885cca08658076654ad4f8409dacaa43c2b90c403292f8b49'),
    ('P5_native_abi_h', UP / 'drivers' / 'Adreno740' / 'NativeAdrenoABI.h',
     7733, '20ac9298bb390d86788c1fc58155e1f9581ffa942e222752efc95c67cb5670d2'),
    ('P6_porting_gpu_md', UP / 'docs' / 'porting' / 'gpu.md',
     7861, '521486857e22068cc8a87a604ab075c33d0b21d61476495ebfc0f19e5d4da7f4'),
    ('P7_kernel_yml', ONEPLUS / 'work' / 'tmp' / 'kernel.yml',
     14554, '6629ee5180434b7ba8d638f50542ecd83b1f373240a0efcf4c28f7483fe7bb0b'),
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
        check(False, f'A1 {name} file missing')
        texts[name] = ''
        continue
    b = path.read_bytes()
    check(len(b) == size, f'A1 {name} size {len(b)} != {size}')
    check(hashlib.sha256(b).hexdigest() == sha, f'A1 {name} sha256 mismatch')
    # P7 is a FETCHED GitHub Actions workflow (CRLF is legitimate); the six
    # in-repo source pins are LF. Only assert LF for those six.
    if name != 'P7_kernel_yml':
        check(b'\r' not in b, f'A1 {name} has CR')
    texts[name] = b.decode('utf-8')

p1, p2, p3, p4, p5, p6, p7 = (texts[k] for k, *_ in PINS)

# ---- A2: P1 publish constants (verbatim enum values) ----
A2 = [
    ('NativeAdrenoPluginOpenType', '0x4147'),
    ('NativeAdrenoTunnelMagic', '0x4e544e4c'),
    ('NativeAdrenoHandoffBase', '0x7000'),
    ('NativeAdrenoHandoffLast', '0x7003'),
    ('NativeAdrenoScanoutType', '0x5343'),
    ('NativeAdrenoHandoffOpenType', '0x4148'),
    ('NativeAdrenoExportMethod', '0x7100'),
    ('NativeAdrenoExportMaxPiece', '3968'),
    ('NativeAdrenoMailboxType', '0xC0DE'),
    ('NativeAdrenoMailboxMagic', '0x4d41494c'),
    ('NativeAdrenoMaxClients', '4'),
]
for sym, val in A2:
    check(re.search(r'\b' + sym + r'\s*=\s*' + re.escape(val) + r'\b', p1) is not None,
          f'A2 P1 {sym}={val}')
check(re.search(r'limit\s*&&\s*limit\s*<\s*8\s*\?\s*limit\s*:\s*8', p1) is not None,
      'A2 P1 non-render-server cap 8')

# ---- A3: P1 kClientWindows (sheng-only provenance) ----
A3 = [('0x20000000ull', '0x40000000ull'), ('0x60000000ull', '0x0AAA9000ull'),
      ('0x6AAA9000ull', '0x0AAA9000ull'), ('0x75552000ull', '0x0AAA9000ull')]
for start, size in A3:
    check(re.search(r'\{\s*' + start + r'\s*,\s*' + size + r'\s*\}', p1) is not None,
          f'A3 P1 window {start}/{size}')
check('0x20000000..0x80000000' in flat(p1), 'A3 P1 2 GiB aperture comment')
check('slot*NativeAdrenoAbiMaxQueues' in flat(p1).replace('slot * ', 'slot') or
      'slot*NativeAdrenoAbiMaxQueues' in p1, 'A3 P1 setScope queue base')

# ---- A4: P2 allow-list ----
check('" ,\\t\\r\\n"' in p2, 'A4 P2 strtok_r delimiter set')
check('strcmp(name, "*")' in p2, 'A4 P2 star branch')
check('strcmp(name, self)' in p2, 'A4 P2 exact branch')
check("[nameLength - 1] == '*'" in p2, 'A4 P2 trailing-star branch')
check('strncmp(name, self, nameLength - 1)' in p2, 'A4 P2 prefix compare')
check('getprogname()' in p2, 'A4 P2 uses process name')

# ---- A5: P3 docs ----
p3f = flat(p3)
check('--publish' in p3, 'A5 P3 --publish')
check('--clients NAME,...' in p3, 'A5 P3 --clients')
check('256 MiB' in p3, 'A5 P3 256 MiB claim')
check('1 GiB window at `0x20000000`' in p3f, 'A5 P3 1 GiB window')
check('MTLAddDevice' in p3, 'A5 P3 MTLAddDevice publish path')
check('CARenderServer' in p3 or 'GopFramebuffer' in p3, 'A5 P3 composite path')

# ---- A6: P4 seed staging ----
check("'._-*'" in p4, "A6 P4 client-name charset '._-*'")
check('NativeAdrenoMetal.clients' in p4, 'A6 P4 .clients marker')
check('NativeAdrenoMetal.publish' in p4, 'A6 P4 .publish marker')
check('0o644' in p4, 'A6 P4 marker mode 0644')
check("'hdiutil'" in p4, 'A6 P4 hdiutil (macOS-only)')
check("b'NXSB'" in p4, 'A6 P4 APFS NXSB assertion')
check('CAEnableDeepFramebuffer' in p4, 'A6 P4 deep framebuffer off')

# ---- A7: P5 wire ABI ----
check(re.search(r'NativeAdrenoAbiMaxBoBytesShift\s*=\s*28', p5) is not None,
      'A7 P5 MaxBoBytesShift=28')
check('256MiB per object' in p5, 'A7 P5 256 MiB per object comment')
check(re.search(r'NativeAdrenoAbiMaxQueues\s*=\s*8', p5) is not None,
      'A7 P5 MaxQueues=8')
check('never from a guess' in flat(p5), 'A7 P5 never from a guess')

# ---- A8: P6 porting guide section 1 ----
p6f = flat(p6)
check('For another GPU generation' in p6f, 'A8 P6 generation clause')
check('implement the matching kernel backend and compiler support before selecting its frontend' in p6f,
      'A8 P6 backend-before-frontend rule')

# ---- A9: P7 CI cannot build the frontend ----
for tok in ('build-turnip', 'build-adreno-metal', 'make-gpu-addon',
            'add-gpu-plugin', 'codesign', 'hdiutil', 'meson', 'xcrun'):
    check(tok not in p7, f'A9 P7 has no frontend step {tok}')
check('macos-26' in p7 or 'macOS-26' in p7 or 'macos' in p7.lower(),
      'A9 P7 is the macOS kernel CI')

# ======================= Domain B: the deliverable =======================
h = (GPU / 'AdrenoMetalPublishV1.h').read_text(encoding='utf-8')
c = (GPU / 'AdrenoMetalPublishV1.c').read_text(encoding='utf-8')
t = (GPU / 'tests' / 'adreno-metal-publish-v1-test.cpp').read_text(encoding='utf-8')

# B1: 7 pins recorded verbatim in the header provenance block
for name, path, size, sha in PINS:
    check(sha in h, f'B1 header records {name} sha256')
check(h.count('48729 B') == 1, 'B1 header records P1 size')

# B2: header mirrors every P1 constant under an unambiguous name
B2 = [
    ('kEnchiladaPublishPluginOpenType', '0x4147u'),
    ('kEnchiladaPublishTunnelMagic', '0x4e544e4cu'),
    ('kEnchiladaPublishHandoffBase', '0x7000u'),
    ('kEnchiladaPublishHandoffLast', '0x7003u'),
    ('kEnchiladaPublishScanoutType', '0x5343u'),
    ('kEnchiladaPublishHandoffOpenType', '0x4148u'),
    ('kEnchiladaPublishExportMethod', '0x7100u'),
    ('kEnchiladaPublishExportMaxPiece', '3968u'),
    ('kEnchiladaPublishMailboxType', '0xc0deu'),
    ('kEnchiladaPublishMailboxMagic', '0x4d41494cu'),
    ('kEnchiladaPublishMaxClients', '4u'),
    ('kEnchiladaPublishMaxQueues', '8u'),
    ('kEnchiladaPublishClientCap', '8u'),
    ('kEnchiladaPublishPerObjectBytesShift', '28u'),
]
for sym, val in B2:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B2 {sym}={val}')
check('ENCHILADA_PUBLISH_MARKER_PUBLISH "NativeAdrenoMetal.publish"' in h,
      'B2 publish marker name')
check('ENCHILADA_PUBLISH_MARKER_CLIENTS "NativeAdrenoMetal.clients"' in h,
      'B2 clients marker name')
check('ENCHILADA_PUBLISH_RENDER_SERVER "backboardd"' in h, 'B2 render server name')

# B3: sheng reference windows are PROVENANCE-ONLY and distinct from 256 MiB
for macro, val in [('SHENG_SLOT0_START', '0x20000000ull'),
                   ('SHENG_SLOT0_BYTES', '0x40000000ull'),
                   ('SHENG_SLOT1_START', '0x60000000ull'),
                   ('SHENG_SLOT2_START', '0x6aaa9000ull'),
                   ('SHENG_SLOT3_START', '0x75552000ull'),
                   ('SHENG_CLIENT_BYTES', '0x0aaa9000ull')]:
    check(re.search(r'#define\s+ENCHILADA_PUBLISH_' + macro + r'\s+' + re.escape(val), h) is not None,
          f'B3 {macro}={val}')
check('PROVENANCE DATA' in h and 'NEVER' in h, 'B3 sheng windows marked provenance-only')
check('DOC-CONSTANT CORRECTION' in h, 'B3 256 MiB correction documented')
check('0x0AAA9000' in h and '170.6 MiB' in h, 'B3 ~170.6 MiB correction')

# B4: stage / blocker map + C/C++ layout asserts
check('kEnchiladaPublishStageCount = 9u' in h, 'B4 9 stages')
check('kEnchiladaPublishTodayBlockers 0x3fu' in h, 'B4 today blockers 0x3f')
for bit in ('1', '2', '4', '8', '16', '32'):
    check(re.search(r'Blocker\w+\s*=\s*' + bit + r'\b', h) is not None,
          f'B4 blocker bit {bit}')
check('extern "C"' in h, 'B4 extern C guards')
check(h.count('static_assert(') >= 4, 'B4 C++ static_asserts')
check(h.count('_Static_assert(') >= 4, 'B4 C11 _Static_asserts')

# B5: .c implements the whole API, no MMIO / IOKit / libc
APIS = ['enchilada_publish_open_type_is_plugin', 'enchilada_publish_open_type_is_handoff',
        'enchilada_publish_open_type_is_marker', 'enchilada_publish_open_type_name',
        'enchilada_publish_is_render_server', 'enchilada_publish_client_name_valid',
        'enchilada_publish_client_listed', 'enchilada_publish_client_cap',
        'enchilada_publish_queue_base', 'enchilada_publish_windows_a630',
        'enchilada_publish_windows_consistent', 'enchilada_publish_windows_are_board_derived',
        'enchilada_publish_sheng_reference', 'enchilada_publish_per_object_cap_bytes_shift',
        'enchilada_publish_stage_name', 'enchilada_publish_blocker_name',
        'enchilada_publish_stage_blockers', 'enchilada_publish_establishment_blockers',
        'enchilada_publish_frontend_allowed']
for api in APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B5 .c defines {api}')
for bad in ('#include <string.h>', '#include <stdlib.h>', 'IOConnectCall', 'IOServiceOpen',
            'mmio', 'writel', 'reg_write', 'volatile uint32_t *'):
    check(bad not in c, f'B5 .c has no {bad}')
check('#include "AdrenoMetalPublishV1.h"' in c, 'B5 .c includes own header only')

# B6: the WINDOW HONESTY GATE (the load-bearing finding)
check('kEnchiladaPublishA630WindowsSpecifiedMask 0u' in h, 'B6 specified mask zero')
check('enchilada_publish_windows_a630' in c and 'zero_windows' in c, 'B6 a630 windows zeroed')
check('w[i].start != 0u || w[i].bytes != 0u' in c, 'B6 any nonzero window refused')
check('BOARD-DERIVED' in h and 'never from a guess' in h, 'B6 board-derived rationale')
check('return 1;' in c[c.find('enchilada_publish_windows_are_board_derived'):][:200],
      'B6 board_derived() returns 1')

# B7: stage blocker OR == 0x3f, portable allow-list stage has no blockers
check('return 0u; /* pure policy' in c, 'B7 allow-list stage portable (no blockers)')
check('kEnchiladaPublishStageClientAllowList' in c, 'B7 allow-list stage mapped')
check('kEnchiladaPublishStageCount' in c[c.find('establishment_blockers'):],
      'B7 OR over all stages')

# B8: allow-list policy + name validation from P2/P4
check('is_delimiter' in c and "'\\t'" in c, 'B8 five delimiter handling')
check("tok[0] == '*'" in c, 'B8 star branch')
check("tok[n - 1u] == '*'" in c, 'B8 trailing-star branch')
check('is_name_char' in c, 'B8 name charset validation')
check('literal_eq' in c, 'B8 bounded literal compare (no strlen)')

# B9: frontend gate needs all SEVEN prerequisites
check('gen1_backend && compiler && signed_zap && smmu_context &&' in c,
      'B9 seven-way AND (part 1)')
check('os_abi && macos_toolchain && board_va_map' in c, 'B9 seven-way AND (part 2)')

# B10: host test shape
check(t.count('static void test') == 7, 'B10 7 test groups')
check('PASS: enchilada A630 Metal publish/discovery policy contract (7 test groups, 0 failures)' in t,
      'B10 PASS line')
check('testWindowHonestyGate' in t and 'testShengReference' in t, 'B10 window groups present')
check('testEstablishmentChain' in t, 'B10 chain group present')

print(f'Domain A: {len(PINS)} sha256-pinned sources, publish/window/allow-list/doc/CI derived')
print('Domain B: AdrenoMetalPublishV1.h/.c/test mirror, sheng provenance-only, window-zero gate')
print(f'METAL_PUBLISH_CROSS: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
for f in fails:
    print('  FAIL ' + f)
sys.exit(0 if not fails else 1)
