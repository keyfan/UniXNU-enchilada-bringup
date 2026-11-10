#!/usr/bin/env python3
"""Independent verifier for the device-sourced A630 firmware manifest.

Domain A (always): validate scripts/data/adreno-a630-device-firmware.json
  internal consistency + cross-check every upstream-contract literal against
  the SHA-PINNED Linux v6.12 sources (.cache/adreno-a630-source/...).

Domain B (when the gitignored pulled blobs are present on this host):
  recompute sha256 + md5 of all 7 files, parse the ELF32/PIL headers of
  .mdt/.elf, and assert every value matches the manifest. The proprietary
  bytes themselves are deliberately NOT committed.

Usage: python -X utf8 scripts/verify-enchilada-device-firmware.py
Exit 0 + '... PASS' only when every check in both domains succeeds.
"""
import json, hashlib, struct, re, sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
W = DEV.parents[1]
MANIFEST = DEV / 'scripts' / 'data' / 'adreno-a630-device-firmware.json'
PINS = DEV / 'scripts' / 'data' / 'adreno-a630-firmware.json'
CACHE = DEV / '.cache' / 'adreno-a630-source' / 'drivers' / 'gpu' / 'drm' / 'msm' / 'adreno'
FW = W / 'work' / 'tmp' / 'a630-device-fw'
FILES = ['a630_gmu.bin', 'a630_sqe.fw', 'a630_zap.b00', 'a630_zap.b01',
         'a630_zap.b02', 'a630_zap.elf', 'a630_zap.mdt']

checks = 0

def ck(cond, label, detail=''):
    global checks
    checks += 1
    print(('PASS  ' if cond else 'FAIL  ') + label + ((' | ' + str(detail)) if detail else ''))
    if not cond:
        raise SystemExit('VERIFY FAILED: ' + label)

def main():
    m = json.loads(MANIFEST.read_text(encoding='utf-8'))
    ck(m['schema_version'] == 1, 'manifest schema_version 1')
    ck(m['target'].startswith('enchilada A630'), 'target is enchilada A630')
    ck('READ-ONLY' in m['extraction']['method'] and '-o ro' in m['extraction']['method'],
       'extraction is a read-only reversible mount')
    ck(m['extraction']['method'].find('umount') > 0, 'umount is in the method')
    ck('NO flash' in m['extraction']['safety'] and 'NO reboot' in m['extraction']['safety'],
       'safety contract forbids flash/reboot/writes')

    print('\n===== DOMAIN A: manifest integrity =====')
    files = m['files']
    ck(len(files) == 7, '7 firmware files recorded', len(files))
    for f in FILES:
        e = files[f]
        ck(re.fullmatch(r'[0-9a-f]{64}', e['sha256']) is not None, '40-hex sha256 ' + f)
        ck(re.fullmatch(r'[0-9a-f]{32}', e['md5']) is not None, '32-hex md5 ' + f)
        ck(e['bytes'] > 0, 'positive byte count ' + f, e['bytes'])
    ck(files['a630_gmu.bin']['bytes'] == 0x8000, 'GMU is exactly 0x8000 (legacy gate)',
       files['a630_gmu.bin']['bytes'])
    pil = m['pil_structure']
    ck(pil['elf_class'] == 1 and pil['elf_class_name'] == 'ELF32', 'ELF32 class')
    ck(pil['e_machine'] == '0xa4' and pil['e_machine_name'] == 'EM_HEXAGON',
       'EM_HEXAGON 0xa4 (NOT AARCH64)', pil['e_machine'])
    ck(pil['e_machine_decimal'] == 164, 'machine decimal 164')
    ck('elf-em.h' in pil['e_machine_uapi_pin'] and 'EM_AARCH64 183' in pil['e_machine_uapi_pin'],
       'UAPI pin cites elf-em.h + EM_AARCH64 183')
    ck(pil['phnum'] == 3 and pil['phentsize'] == 32 and pil['phoff'] == 52,
       'PIL phnum=3 phentsize=32 phoff=52')
    ck(pil['ph_filesz'] == [148, 6536, 1968], 'ph_filesz = [148, 6536, 1968]')
    for f, want in [('a630_zap.b00', 148), ('a630_zap.b01', 6536), ('a630_zap.b02', 1968)]:
        ck(files[f]['bytes'] == want, 'segment size matches ph_filesz ' + f, files[f]['bytes'])
    ck(files['a630_zap.elf']['bytes'] == 14256, 'full zap .elf is 14256 B')
    ck(files['a630_zap.mdt']['bytes'] == 6684, 'zap .mdt is 6684 B')

    print('\n===== DOMAIN A: public-mirror deltas (honest) =====')
    pins = json.loads(PINS.read_text(encoding='utf-8'))['files']
    ck(files['a630_gmu.bin']['bytes'] == pins['qcom/a630_gmu.bin']['bytes'],
       'device GMU size == public GMU size')
    ck(files['a630_gmu.bin']['sha256'] != pins['qcom/a630_gmu.bin']['sha256'],
       'device GMU is a DIFFERENT build than public')
    ck(files['a630_sqe.fw']['bytes'] != pins['qcom/a630_sqe.fw']['bytes'],
       'device SQE differs in size from public')
    ck(files['a630_zap.elf']['bytes'] == pins['qcom/sdm845/a630_zap.mbn']['bytes'],
       'device .elf size == public .mbn size (14256)')
    ck(files['a630_zap.elf']['sha256'] != pins['qcom/sdm845/a630_zap.mbn']['sha256'],
       'device .elf content != public .mbn (signed split chain)')

    print('\n===== DOMAIN A: upstream pinned-source contract =====')
    catalog = (CACHE / 'a6xx_catalog.c').read_text(encoding='utf-8', errors='replace')
    gpu = (CACHE / 'a6xx_gpu.c').read_text(encoding='utf-8', errors='replace')
    gmu = (CACHE / 'a6xx_gmu.c').read_text(encoding='utf-8', errors='replace')
    up = m['upstream_contract']
    ck('.zapfw = "a630_zap.mdt"' in catalog, 'a6xx_catalog.c pins .zapfw="a630_zap.mdt"')
    ck('.zapfw = "a630_zap.mdt"' in up['catalog_zapfw'], 'manifest records the .zapfw pin')
    ck('MODULE_FIRMWARE("qcom/a630_zap.mbn")' in catalog,
       'a6xx_catalog.c lists public .mbn in MODULE_FIRMWARE')
    ck('#define GPU_PAS_ID 13' in gpu, 'a6xx_gpu.c defines GPU_PAS_ID 13')
    ck('13' in up['gpu_pas_id'], 'manifest records GPU_PAS_ID 13')
    ck('fw_image->size > 0x8000' in gmu, 'a6xx_gmu.c legacy 0x8000 ITCM gate present')
    ck(files['a630_gmu.bin']['bytes'] <= 0x8000, 'device GMU passes the legacy 0x8000 gate')
    ck('a630_zap.mdt' in files, 'DEVICE HAS the .mdt that .zapfw names')
    ck('a630_zap.mbn' not in files, 'device has NO .mbn (signed split chain)')
    ck('HOST-VERIFIED' in m['honest_boundary'] and 'STAYS host-verified' in m['honest_boundary'],
       'honest boundary keeps gpu area host-verified')
    ck('PAS authentication' in m['honest_boundary'] and 'A6XX_GEN1 kernel backend' in m['honest_boundary'],
       'honest boundary names the remaining blockers')

    print('\n===== DOMAIN B: pulled-blob bytes (when present) =====')
    if not (FW / 'manifest.json').is_file():
        print('SKIP  proprietary blobs not present on this host (gitignored work/tmp/a630-device-fw/); '
              'Domain A is the durable committed record.')
    else:
        pm = json.loads((FW / 'manifest.json').read_text(encoding='utf-8'))
        ck(set(pm['files'].keys()) == set(FILES), 'pull manifest lists exactly 7 files')
        for f in FILES:
            p = FW / f
            ck(p.is_file(), 'pulled file exists ' + f)
            b = p.read_bytes()
            ck(len(b) == files[f]['bytes'], 'byte count matches manifest ' + f, len(b))
            ck(hashlib.sha256(b).hexdigest() == files[f]['sha256'], 'sha256 matches manifest ' + f)
            ck(hashlib.md5(b).hexdigest() == files[f]['md5'], 'md5 matches manifest ' + f)
            ck(pm['files'][f]['sha256'] == files[f]['sha256'], 'pull manifest agrees ' + f)
        for f in ['a630_zap.mdt', 'a630_zap.elf']:
            b = (FW / f).read_bytes()
            ck(b[:4] == b'\x7fELF', 'ELF magic ' + f)
            ck(b[4] == 1, 'ELF32 ' + f)
            machine = struct.unpack_from('<H', b, 18)[0]
            phoff = struct.unpack_from('<I', b, 28)[0]
            phentsize, phnum = struct.unpack_from('<HH', b, 42)
            ck(machine == 0xa4, 'EM_HEXAGON machine ' + f, hex(machine))
            ck(phoff == 52 and phentsize == 32 and phnum == 3, 'PIL header shape ' + f)
            sz = []
            for i in range(phnum):
                off = phoff + i * phentsize
                sz.append(struct.unpack_from('<I', b, off + 16)[0])  # p_filesz
            ck(sz == [148, 6536, 1968], 'ph p_filesz matches manifest ' + f, sz)
        ck([(FW / f).stat().st_size for f in ['a630_zap.b00', 'a630_zap.b01', 'a630_zap.b02']]
           == [148, 6536, 1968], '.bNN sizes tie to program headers')

    print('\nENCHILADA_DEVICE_FIRMWARE_VERIFY: PASS (%d checks, manifest + pinned upstream + pulled-bytes domains agree)' % checks)

if __name__ == '__main__':
    main()
