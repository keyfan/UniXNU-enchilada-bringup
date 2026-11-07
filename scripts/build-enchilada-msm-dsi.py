#!/usr/bin/env python3
"""Build libEnchiladaMsmDsi.a: the enchilada MDSS DSI0 command-channel transport
(drivers/NativeMsmDsi/MsmDsiCmd.c) together with the DCS packet builder it consumes
(drivers/NativeDsiBacklight/DsiDcs.c), against the recorded XNU compile flags.

Runs as a [kernel] archives builder, after the bootstrap pass left the compile records.
Both units are pure C: MsmDsiCmd.c turns a layer-1 dsi_dcs_packet into the MSM-specific
CMD DMA buffer and emits the software-trigger register sequence (Linux v6.12
drivers/gpu/drm/msm/dsi/dsi_host.c dsi_cmd_dma_add / msm_dsi_host_cmd_xfer_commit),
DsiDcs.c builds the DCS packets (stage-3 layer 1). They are compiled with the flags of
osfmk/DEVELOPMENT/startup.o plus -mgeneral-regs-only (as sheng's GENI SPI and the
touch archive's C objects). The archive is linked with -force_load through
CONFIG_XNU_EXTRA_LD_ARCHIVES.

There is deliberately no runtime probe in this archive yet: the panel is already lit by
the UEFI GOP framebuffer, so a cold-boot full-controller reprogram would disturb the
scanout. The objects here are the per-transfer send path that the layer-3
AppleARMBacklight-facing IOService will call; until that probe exists the objects are
compiled (CI evidence) but not invoked. The kernel build does not track this archive
(the extra archives carry no make dependency), so a changed archive removes the linked
kernel to make the next link use it.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/enchilada-msm-dsi'
OBJ_ROOT = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
KIT = ROOT / 'kits/enchilada'
MSM = ROOT / 'drivers/NativeMsmDsi'
DCS = ROOT / 'drivers/NativeDsiBacklight'
WARNINGS = ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-c99-designator', '-Wno-reorder-init-list',
            '-Wno-deprecated-enum-enum-conversion', '-Wno-unnecessary-virtual-specifier', '-Wno-unknown-warning-option',
            '-Wno-missing-designated-field-initializers']
INCLUDES = ['-I', str(KIT), '-I', str(MSM), '-I', str(DCS), '-I', str(ROOT / '.cache/xnu-work/osfmk')]


def record(relative):
    return json.loads((OBJ_ROOT / relative).read_text().strip().rstrip(','))


def command_for(rec, source, obj):
    """The recorded compile with another source and object, and without the record's own warnings."""
    command, args, i = [], rec['arguments'], 0
    while i < len(args):
        arg = args[i]
        if arg == rec['file']:
            command.append(str(source))
        elif arg in ('-o', '-MF', '-MT', '-MJ'):
            if arg == '-o':
                command += ['-o', str(obj)]
            i += 1
        elif arg == '-Xclang' and args[i + 1].startswith('-W'):
            i += 1
        elif not arg.startswith('-W'):
            command.append(arg)
        i += 1
    return command


def check_object(path):
    data = path.read_bytes()
    magic, cpu, subtype, kind = struct.unpack_from('<4I', data)
    if (magic, cpu, subtype, kind) != (0xfeedfacf, 0x100000c, 0, 1):
        raise SystemExit(f'{path.name}: not an arm64 Mach-O object')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    c_rec = record('osfmk/DEVELOPMENT/startup.o.json')
    units = [
        (c_rec, MSM / 'MsmDsiCmd.c', ['-mgeneral-regs-only']),
        (c_rec, DCS / 'DsiDcs.c', ['-mgeneral-regs-only']),
    ]
    objects, commands = [], {}
    for rec, source, extra in units:
        obj = OUT / (source.stem + '.o')
        command = command_for(rec, source, obj) + WARNINGS + extra + INCLUDES
        commands[source.name] = command
        result = subprocess.run(command, cwd=rec['directory'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
        (OUT / (source.stem + '.log')).write_text(result.stdout)
        if result.returncode:
            raise SystemExit(result.stdout[-4000:])
        check_object(obj)
        objects.append(obj)
    (OUT / 'compile-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    archive = OUT / 'libEnchiladaMsmDsi.a'
    previous = hashlib.sha256(archive.read_bytes()).hexdigest() if archive.is_file() else None
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', str(archive), *map(str, objects)], check=True, timeout=60)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != previous:
        (OBJ_ROOT / 'mach.development.bcm2837.unstripped.noctf').unlink(missing_ok=True)
    sources = [MSM / 'MsmDsiCmd.c', MSM / 'MsmDsiCmd.h', DCS / 'DsiDcs.c', DCS / 'DsiDcs.h']
    (OUT / 'build-report.json').write_text(json.dumps({
        'archive_sha256': digest,
        'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        'hardware_execution': False,
    }, indent=2) + '\n')
    print(f'PASS: libEnchiladaMsmDsi.a ({len(objects)} objects, sha256 {digest[:16]})')


if __name__ == '__main__':
    main()
