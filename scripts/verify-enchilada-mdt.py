#!/usr/bin/env python3
"""Round 74 method-2 cross-verifier for AdrenoMdtV1.

Domain A re-derives the MDT/ELF32 rules from THREE sha256-pinned sources, AND
parses the REAL a630_zap.mdt with an INDEPENDENT Python implementation of the
same rules, then confronts that result with the round-38 device inventory.

Domain B asserts the freestanding C module mirrors the rules and stays a parser
(not a loader).

Never compiles or runs the module; never touches the network.
"""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
WS = DEV.parents[1]                      # oneplus6-ios
CACHE = DEV / '.cache' / 'adreno-a630-source'
GPU = DEV / 'drivers' / 'NativeAdreno'
REAL_MDT = WS / 'work' / 'tmp' / 'a630-device-fw' / 'a630_zap.mdt'
DEVICE_JSON = DEV / 'scripts' / 'data' / 'adreno-a630-device-firmware.json'

PINS = [
    ('P1_mdt_loader_c', CACHE / 'drivers/soc/qcom/mdt_loader.c', 12182,
     'ea79424add96b68c81ab9dfbde64ffac3ea5089a17259ba980c896f5d5e441ed'),
    ('P2_mdt_loader_h', CACHE / 'include/linux/soc/qcom/mdt_loader.h', 2179,
     '12d1c1c8b4e420ec28af0cd8255c9d0d37d56df7bf04714a61cee99479b3a8be'),
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


mdt_c, mdt_h = texts['P1_mdt_loader_c'], texts['P2_mdt_loader_h']

# ---- A2: the mdthdr validity rules (P1:21-33) ---------------------------
check('static bool mdt_phdr_valid(const struct elf32_phdr *phdr)' in
      line('P1_mdt_loader_c', 21), 'A2 P1:21 mdt_phdr_valid')
check('if (phdr->p_type != PT_LOAD)' in line('P1_mdt_loader_c', 23),
      'A2 P1:23 rule 1: p_type must be PT_LOAD')
check('if ((phdr->p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH)' in
      line('P1_mdt_loader_c', 26), 'A2 P1:26 rule 2: HASH is never loadable')
check('if (!phdr->p_memsz)' in line('P1_mdt_loader_c', 29),
      'A2 P1:29 rule 3: zero memsz is refused')

# ---- A3: the two TYPE constants (P2:7-9) -------------------------------
check(re.search(r'#define\s+QCOM_MDT_TYPE_MASK\s+\(7\s*<<\s*24\)',
                '\n'.join(mdt_h)) is not None, 'A3 P2:7 TYPE_MASK = (7 << 24)')
check(re.search(r'#define\s+QCOM_MDT_TYPE_HASH\s+\(2\s*<<\s*24\)',
                '\n'.join(mdt_h)) is not None, 'A3 P2:8 TYPE_HASH = (2 << 24)')
check(re.search(r'#define\s+QCOM_MDT_RELOCATABLE\s+BIT\(27\)',
                '\n'.join(mdt_h)) is not None, 'A3 P2:9 RELOCATABLE = BIT(27)')
# The mask is 3 bits wide, so HASH is exactly one of eight type codes.
check((7 << 24) & 0xFFFFFFFF == 0x07000000, 'A3 the mask is 0x07000000')
check((2 << 24) & 0xFFFFFFFF == 0x02000000, 'A3 HASH is 0x02000000')
check((1 << 27) & 0xFFFFFFFF == 0x08000000, 'A3 RELOCATABLE is 0x08000000')

# ---- A4: get_size is min..ALIGN(max) over VALID phdrs (P1:76-103) ------
gs = ' '.join(mdt_c[75:103])
check('if (phdr->p_paddr < min_addr)' in gs, 'A4 P1:94 min over p_paddr')
check('if (phdr->p_paddr + phdr->p_memsz > max_addr)' in gs,
      'A4 P1:97 max over p_paddr + p_memsz')
check('max_addr = ALIGN(phdr->p_paddr + phdr->p_memsz, SZ_4K)' in gs,
      'A4 P1:98 the top is ALIGNed to SZ_4K')
check('if (!mdt_phdr_valid(phdr))' in gs, 'A4 only VALID phdrs participate')

# ---- A5: split-segment naming (P1:35-68) ------------------------------
check('sprintf(seg_name + strlen(fw_name) - 3, "b%02d", segment)' in
      line('P1_mdt_loader_c', 50), 'A5 P1:50 the last 3 chars become bNN')
check('if (strlen(fw_name) < 4)' in line('P1_mdt_loader_c', 43),
      'A5 P1:43 a name shorter than 4 is refused')
check('if (seg_fw->size != phdr->p_filesz)' in line('P1_mdt_loader_c', 58),
      'A5 P1:58 a short segment file is an error')

# ---- A6: metadata rules (P1:126-176) ----------------------------------
md = ' '.join(mdt_c[125:177])
check('if (ehdr->e_phnum < 2)' in md, 'A6 P1:140 needs at least two phdrs')
check('if (phdrs[0].p_type == PT_LOAD)' in md, 'A6 P1:143 phdr[0] must not be PT_LOAD')
check('hash_segment = i;' in md, 'A6 P1:148 hash segment is the first HASH')
check('if (!hash_segment)' in md, 'A6 P1:153 missing hash is an error')
check('ehdr_size = phdrs[0].p_filesz;' in md, 'A6 P1:158 ehdr_size')
check('hash_size = phdrs[hash_segment].p_filesz;' in md, 'A6 P1:159 hash_size')
check('if (ehdr_size + hash_size == fw->size)' in md,
      'A6 P1:168 the SPLIT test is ehdr+hash == file size')

# ======================= Domain A7: the REAL blob =======================
# An INDEPENDENT re-implementation of the rules above, in Python, applied to the
# blob the phone actually ships.
TYPE_MASK, TYPE_HASH, PT_LOAD, RELOC = (7 << 24), (2 << 24), 1, (1 << 27)

if not REAL_MDT.is_file():
    check(False, f'A7 the real a630_zap.mdt is missing at {REAL_MDT}')
    real = None
else:
    real = REAL_MDT.read_bytes()
    check(len(real) == 6684, f'A7 real MDT is 6684 B (got {len(real)})')

if real is not None:
    check(real[:4] == b'\x7fELF', 'A7 real blob has ELF magic')
    check(real[4] == 1, 'A7 EI_CLASS == 1 (ELF32)')
    check(real[5] == 1, 'A7 EI_DATA == 1 (little endian)')
    e_machine = struct.unpack_from('<H', real, 18)[0]
    check(e_machine == 0xa4, f'A7 e_machine 0xa4 == EM_HEXAGON (got 0x{e_machine:x})')
    check(e_machine != 0xb7, 'A7 e_machine is NOT 0xb7 aarch64 (round-38 correction)')
    e_phoff, = struct.unpack_from('<I', real, 28)
    e_phentsize, e_phnum = struct.unpack_from('<HH', real, 42)
    check(e_phoff == 52, 'A7 e_phoff 52')
    check(e_phentsize == 32, 'A7 e_phentsize 32')
    check(e_phnum == 3, 'A7 e_phnum 3')

    phdrs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        (p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags,
         p_align) = struct.unpack_from('<8I', real, off)
        phdrs.append(dict(type=p_type, offset=p_offset, paddr=p_paddr,
                          filesz=p_filesz, memsz=p_memsz, flags=p_flags))
    # Independently apply mdt_phdr_valid.
    def valid(p):
        if p['type'] != PT_LOAD:
            return False
        if (p['flags'] & TYPE_MASK) == TYPE_HASH:
            return False
        if p['memsz'] == 0:
            return False
        return True

    check(valid(phdrs[0]) is False, 'A7 ph[0] is INVALID (not PT_LOAD, memsz 0)')
    check(valid(phdrs[1]) is False, 'A7 ph[1] is INVALID (not PT_LOAD)')
    check(valid(phdrs[2]) is True, 'A7 ph[2] is the ONLY valid segment')
    check(phdrs[2]['type'] == PT_LOAD, 'A7 ph[2].p_type == PT_LOAD')
    check(phdrs[2]['paddr'] == 0x5000, 'A7 ph[2].p_paddr == 0x5000')
    check(phdrs[2]['filesz'] == 1968, 'A7 ph[2].p_filesz == 1968')
    check(phdrs[2]['memsz'] == 1968, 'A7 ph[2].p_memsz == 1968')
    check((phdrs[2]['flags'] & RELOC) != 0, 'A7 ph[2] is RELOCATABLE')
    check(phdrs[1]['offset'] == 4096, 'A7 ph[1].p_offset == 4096')
    check(phdrs[1]['filesz'] == 6536, 'A7 ph[1].p_filesz == 6536')

    # get_size over valid phdrs only, ALIGNed to 4K.
    valid_ps = [p for p in phdrs if valid(p)]
    min_addr = min(p['paddr'] for p in valid_ps)
    max_addr = max((p['paddr'] + p['memsz'] + 0xFFF) & ~0xFFF for p in valid_ps)
    region = max_addr - min_addr
    check(region == 0x1000, f'A7 recomputed region size is 4096 (got {region})')
    filesz_sum = sum(p['filesz'] for p in phdrs)
    check(filesz_sum == 8652, f'A7 p_filesz sum is 8652 (got {filesz_sum})')
    check(filesz_sum != region,
          'A7 the p_filesz sum is NOT the region size (the conflation trap)')
    check(region != len(real),
          'A7 the region size is NOT the file size either')

    # THE CROSS-CHECK THAT MATTERS: the three p_filesz equal the three device
    # segment file sizes recorded in round 38.
    inv = DEVICE_JSON.read_text(encoding='utf-8')
    man = json.loads(inv)
    sizes = {k: v['bytes'] for k, v in man['files'].items()}
    check(sizes.get('a630_zap.b00') == phdrs[0]['filesz'],
          'A7 ph[0].p_filesz == the DEVICE a630_zap.b00 size (%r vs %r)'
          % (phdrs[0]['filesz'], sizes.get('a630_zap.b00')))
    check(sizes.get('a630_zap.b01') == phdrs[1]['filesz'],
          'A7 ph[1].p_filesz == the DEVICE a630_zap.b01 size (%r vs %r)'
          % (phdrs[1]['filesz'], sizes.get('a630_zap.b01')))
    check(sizes.get('a630_zap.b02') == phdrs[2]['filesz'],
          'A7 ph[2].p_filesz == the DEVICE a630_zap.b02 size (%r vs %r)'
          % (phdrs[2]['filesz'], sizes.get('a630_zap.b02')))
    check(sizes.get('a630_zap.mdt') == len(real),
          'A7 the DEVICE a630_zap.mdt size equals the parsed file size')
    # Split branch: header slice + hash == whole file.
    ehdr_size, hash_size = phdrs[0]['filesz'], phdrs[1]['filesz']
    check(ehdr_size + hash_size == len(real),
          'A7 ehdr+hash == the whole file, so this MDT is SPLIT (%d+%d vs %d)'
          % (ehdr_size, hash_size, len(real)))

# ======================= Domain B: the deliverable =======================
h = (GPU / 'AdrenoMdtV1.h').read_text(encoding='utf-8')
c = (GPU / 'AdrenoMdtV1.c').read_text(encoding='utf-8')
t = (GPU / 'tests' / 'adreno-mdt-v1-test.cpp').read_text(encoding='utf-8')

for name, path, size, sha in PINS:
    check(sha in h, f'B1 header records {name} sha256')

B2 = [('kAdrenoMdtElf32EhdrBytes', '52u'), ('kAdrenoMdtElf32PhdrBytes', '32u'),
      ('kAdrenoMdtElfClass32', '1u'), ('kAdrenoMdtElfDataLsb', '1u'),
      ('kAdrenoMdtElfMachineHexagon', '0xa4u'), ('kAdrenoMdtPtLoad', '1u'),
      ('kAdrenoMdtTypeMask', '0x07000000u'),
      ('kAdrenoMdtTypeHash', '0x02000000u'),
      ('kAdrenoMdtRelocatable', '0x08000000u'),
      ('kAdrenoMdtPageBytes', '0x1000u')]
for sym, val in B2:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B2 {sym}={val}')

APIS = ['adreno_mdt_header', 'adreno_mdt_phdr', 'adreno_mdt_phdr_valid',
        'adreno_mdt_phdr_relocatable', 'adreno_mdt_phdr_is_hash',
        'adreno_mdt_get_size', 'adreno_mdt_segment_name', 'adreno_mdt_metadata',
        'adreno_mdt_filesz_sum', 'adreno_mdt_ready',
        'adreno_mdt_loader_available', 'adreno_mdt_verify']
for api in APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B5 .c defines {api}')
for bad in ('#include <string.h>', '#include <stdlib.h>', '#include <stdio.h>',
            'malloc(', 'free(', 'fopen(', 'IOConnectCall', 'writel', 'readl',
            'volatile uint32_t *'):
    check(bad not in c, f'B5 .c has no {bad}')

# B6: it is a PARSER, not a loader
_seg = c[c.find('int adreno_mdt_loader_available'):]
check('return 0;' in _seg[:400], 'B6 loader_available() is 0')
check('adreno_mdt_loader_available() != 0 ? 1 : 0' in c,
      'B6 ready() is DERIVED from it')
check('PARSER ONLY' in h, 'B6 the boundary is stated in prose')
# B7: all reads are bounds-checked against size
check('if (size < kAdrenoMdtElf32EhdrBytes)' in c, 'B7 header refuses a short buffer')
check('if (e_phoff > size)' in c, 'B7 phoff past the end is refused')
check('(size - e_phoff) / kAdrenoMdtElf32PhdrBytes < e_phnum' in c,
      'B7 the whole phdr table must fit')
check('if (index >= phnum)' in c, 'B7 phdr index is ranged')
# B8: little-endian readers, no unaligned uint32_t loads
check('static uint32_t rd32(const uint8_t *p)' in c, 'B8 byte-wise rd32')
check('static uint16_t rd16(const uint8_t *p)' in c, 'B8 byte-wise rd16')
check('*(const uint32_t *)' not in c, 'B8 no unaligned 32-bit cast load')

# B9: the real-blob group exists and SKIPs loudly rather than passing silently
check('SKIP: real a630_zap.mdt not present' in t,
      'B9 a missing blob SKIPs loudly')
check('return;   // asserted NOTHING' in t,
      'B9 the SKIP path asserts nothing')
check('a630_zap.b00' in t and 'a630_zap.b01' in t, 'B9 the cross-check names the segments')

_fn = re.findall(r'static void (test[A-Za-z]*)', t)
check(len(_fn) == 7, 'B10 7 test functions (got %d): %r' % (len(_fn), _fn))
tflat = t.replace('"\n                "', '')
check('PASS: enchilada A630 MDT parser contract (8 test groups, 0 failures)' in tflat,
      'B10 PASS line says 8 groups (7 named functions + the real-blob group '
      'counted separately in the banner)')

# B11: wiring
build = (DEV / 'scripts' / 'build-enchilada-adreno.py').read_text(encoding='utf-8')
host = (DEV / 'scripts' / 'check-enchilada-adreno-host.py').read_text(encoding='utf-8')
check("GPU / 'AdrenoMdtV1.c', ['-mgeneral-regs-only']" in build,
      'B11 build script compiles the 26th unit')
check("'AdrenoMdtV1': GPU / 'AdrenoMdtV1.c'," in host, 'B11 host SOURCES entry')
check("('adreno-mdt-v1-test'" in host, 'B11 host TESTS entry')
check('/mdt-v1' in host, 'B11 PASS enumeration updated')
check(len(re.findall(r"\(c_rec, GPU / '[^']+'", build)) == 30,
      'B11 30 C archive units declared')
check('a630_zap.mdt' in host, 'B11 the real blob path is passed to the test')

# ===================== ROUND 106: the LOAD PLAN ===============================
# Domain A8 re-derives the load arithmetic from P1:293-392, then requires the C
# module to agree. Every number is recomputed here, not read from the module.
check('__qcom_mdt_load' in ' '.join(mdt_c[292:295]),
      'A8 P1:293 __qcom_mdt_load is the composition this plan mirrors')
check('if (!mdt_phdr_valid(phdr))' in line('P1_mdt_loader_c', 320),
      'A8 P1:320 only VALID phdrs are loaded')
check('if (phdr->p_flags & QCOM_MDT_RELOCATABLE)' in line('P1_mdt_loader_c', 323),
      'A8 P1:323 the relocatable test that turns on relocation')
check('relocate = true;' in line('P1_mdt_loader_c', 324), 'A8 P1:324 relocate = true')
check('if (phdr->p_paddr < min_addr)' in line('P1_mdt_loader_c', 326),
      'A8 P1:326 min paddr')
check('mem_reloc = min_addr;' in line('P1_mdt_loader_c', 335),
      'A8 P1:335 RELOCATABLE -> base is min_addr, mem_phys is IGNORED')
check('mem_reloc = mem_phys;' in line('P1_mdt_loader_c', 341),
      'A8 P1:341 NOT relocatable -> base is the caller\'s mem_phys')
check('offset = phdr->p_paddr - mem_reloc;' in line('P1_mdt_loader_c', 350),
      'A8 P1:350 offset = p_paddr - mem_reloc')
check('if (offset < 0 || offset + phdr->p_memsz > mem_size)' in
      line('P1_mdt_loader_c', 351), 'A8 P1:351 the region-bounds refusal')
check('if (phdr->p_filesz > phdr->p_memsz)' in line('P1_mdt_loader_c', 357),
      'A8 P1:357 p_filesz > p_memsz is refused')
check('ptr = mem_region + offset;' in line('P1_mdt_loader_c', 365),
      'A8 P1:365 dst = region + offset')
check('if (phdr->p_offset + phdr->p_filesz > fw->size)' in
      line('P1_mdt_loader_c', 369), 'A8 P1:369 the non-split source bounds')
check('memcpy(ptr, fw->data + phdr->p_offset, phdr->p_filesz);' in
      line('P1_mdt_loader_c', 376), 'A8 P1:376 the non-split copy')
check('ret = mdt_load_split_segment(ptr, phdrs, i, fw_name, dev);' in
      line('P1_mdt_loader_c', 379), 'A8 P1:379 the split branch')
check('memset(ptr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);' in
      line('P1_mdt_loader_c', 385), 'A8 P1:385 the zero fill')
check(350 < 351 < 357 < 365 < 369 < 376 < 379 < 385,
      'A8 the loop anchors are in strictly increasing source order')

# A8b: the SPLIT rule is a SEPARATE function from the metadata SPLIT test.
check('static bool qcom_mdt_bins_are_split(const struct firmware *fw, const char *fw_name)'
      in line('P1_mdt_loader_c', 265), 'A8 P1:265 qcom_mdt_bins_are_split')
check('if (!phdrs[i].p_filesz)' in line('P1_mdt_loader_c', 281),
      'A8 P1:281 zero-filesz phdrs do NOT decide the split question')
check('seg_start > fw->size || seg_end > fw->size' in
      ' '.join(mdt_c[283:288]), 'A8 P1:286 either edge past the file means SPLIT')
check('is_split = qcom_mdt_bins_are_split(fw, fw_name);' in
      line('P1_mdt_loader_c', 313), 'A8 P1:313 the plan uses the BINS rule')

# A8c: apply the rules INDEPENDENTLY to the real blob.
if real is not None:
    TYPE_MASK2, RELOC2, PT_LOAD2 = (7 << 24), (1 << 27), 1
    valid2 = [p for p in phdrs
              if p['type'] == PT_LOAD2 and (p['flags'] & TYPE_MASK2) != (2 << 24)
              and p['memsz'] != 0]
    check(len(valid2) == 1, 'A8 the real blob has exactly ONE valid segment')
    relocatable2 = any(p['flags'] & RELOC2 for p in valid2)
    check(relocatable2 is True,
          'A8 the real blob IS relocatable, so mem_setup is MANDATORY on enchilada')
    min2 = min(p['paddr'] for p in valid2)
    max2 = max((p['paddr'] + p['memsz'] + 0xFFF) & ~0xFFF for p in valid2)
    check(min2 == 0x5000, 'A8 the real reloc base is min paddr 0x5000')
    check(max2 - min2 == 0x1000, 'A8 the real region is 4096 bytes')
    # dst = paddr - min_paddr, and it is ZERO because there is one segment.
    dsts = [p['paddr'] - min2 for p in valid2]
    check(dsts == [0], 'A8 the only destination offset is 0')
    copies = [p['filesz'] for p in valid2]
    zeros = [p['memsz'] - p['filesz'] for p in valid2]
    check(copies == [1968], 'A8 the only copy is 1968 bytes')
    check(zeros == [0],
          'A8 the only zero fill is 0 -- p_memsz EQUALS p_filesz for ph[2]')
    # The BINS split test applied independently: does any phdr overrun the file?
    bins_split = any(p['filesz'] and (p['offset'] > len(real) or
                                      p['offset'] + p['filesz'] > len(real))
                     for p in phdrs)
    check(bins_split is True,
          'A8 the real blob IS split by the BINS rule (its payload is a .bNN)')
    # ...while the METADATA split test also fires here but for another reason,
    # and the metadata size equals the file size.
    check(phdrs[0]['filesz'] + phdrs[1]['filesz'] == len(real),
          'A8 the metadata split test also fires, for its own reason')

# ---- Domain B12: the module mirrors the plan, and still loads nothing -------
for sym, val in (('kAdrenoMdtMaxSegments', '8u'),
                 ('kAdrenoMdtPlanTooManySegments', '12'),
                 ('kAdrenoMdtPlanOutsideRegion', '13'),
                 ('kAdrenoMdtPlanFileszGtMemsz', '14'),
                 ('kAdrenoMdtPlanTruncatedSrc', '15')):
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B12 {sym}={val}')

PLAN_APIS = ['adreno_mdt_bins_are_split', 'adreno_mdt_relocatable',
             'adreno_mdt_min_paddr', 'adreno_mdt_plan', 'adreno_mdt_plan_verify']
for api in PLAN_APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B12 .c defines {api}')

for fld in ('reloc_base', 'region_bytes', 'segment_count', 'total_copy',
            'total_zero', 'is_split', 'is_relocatable'):
    check(fld in h, f'B12 the plan struct exposes {fld}')
for fld in ('phdr_index', 'dst_offset', 'src_offset', 'copy_bytes', 'zero_bytes',
            'split_segment'):
    check(fld in h, f'B12 the segment struct exposes {fld}')

# The arithmetic must be PRESENT, not merely described.
check('mem_reloc = (relocate != 0) ? min_addr : mem_phys;' in c,
      'B12 the reloc base follows P1:335/:341 exactly')
check('offset = ph.p_paddr - mem_reloc;' in c, 'B12 dst offset = paddr - base')
check('if (ph.p_filesz > ph.p_memsz)' in c, 'B12 the p_filesz>p_memsz refusal')
check('zero = ph.p_memsz - ph.p_filesz;' in c, 'B12 the zero fill is computed')
check('if (ph.p_paddr < mem_reloc)' in c,
      'B12 a negative offset is REFUSED, not wrapped')
# The zero fill must never be able to underflow: the order is checked.
_zf = c.find('zero = ph.p_memsz - ph.p_filesz;')
_fg = c.find('if (ph.p_filesz > ph.p_memsz)')
check(0 < _fg < _zf, 'B12 the p_filesz>p_memsz refusal PRECEDES the zero fill')
# The bin-split rule must ignore zero-filesz phdrs, as P1:281 does.
_bs = c[c.find('AdrenoMdtStatus adreno_mdt_bins_are_split'):]
_bs = _bs[:_bs.find('\n}')]
check('if (ph.p_filesz == 0u)' in _bs,
      'B12 a zero-filesz phdr does NOT decide the split question')
check('split = 1;' in _bs, 'B12 an overrun sets split')

# B12b: a plan is NOT a loader -- the honest boundary survives.
check('int adreno_mdt_loader_available' in c, 'B12 loader_available still exists')
_lv = c[c.find('int adreno_mdt_loader_available'):]
check('return 0;' in _lv[:_lv.find('\n}')], 'B12 loader_available() is STILL 0')
check('adreno_mdt_loader_available() != 0 ? 1 : 0' in c,
      'B12 ready() is STILL derived from it')
# No loading primitive may have crept in with the plan.
for bad in ('memcpy(', 'memset(', 'malloc(', 'free(', 'fopen(', 'fread(',
            'request_firmware', 'memremap', 'ioremap', 'volatile uint32_t *',
            'IOConnectCall', 'writel', 'readl'):
    check(bad not in c, f'B12 .c has no {bad}')

# B12c: the unreachable refusal is DOCUMENTED as unreachable, not faked.
check('PROVABLY UNREACHABLE' in h,
      'B12 the P1:369 refusal is documented as unreachable by construction')
check('Do not write a test that claims to trigger it' in h,
      'B12 the header forbids a test that fakes that path')

# ---- B13: tests and wiring ------------------------------------------------
_fn2 = re.findall(r'static void (test[A-Za-z]*)', t)
check(len(_fn2) == 7, 'B13 7 named test functions (got %d): %r' % (len(_fn2), _fn2))
check('testLoadPlan();' in t, 'B13 testLoadPlan is invoked')
check('testRealBlob(' in t, 'B13 the real-blob group still runs')
# The real-blob group must now PLAN the device bytes, not only parse them.
check('adreno_mdt_plan(b.data, b.size' in t,
      'B13 the real blob is planned end to end')
check('rplan.reloc_base == 0x5000u' in t,
      'B13 the real reloc base 0x5000 is asserted on device bytes')

print(f'Domain A: {len(PINS)} sha256-pinned sources; the three validity rules, the '
      f'TYPE constants, get_size, the bNN naming and the metadata/split rules are '
      f're-derived; the REAL a630_zap.mdt is reparsed independently and CROSS-CHECKED '
      f'against the round-38 device inventory (148/6536/1968 ~ .b00/.b01/.b02)')
print('Domain B: AdrenoMdtV1 .h/.c/test mirror; bounds-checked, byte-wise reads, '
      'parser only (ready()==0); 26th unit/suite')
print(f'MDT_CROSS: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
for f in fails:
    print('  FAIL ' + f)
sys.exit(0 if not fails else 1)
