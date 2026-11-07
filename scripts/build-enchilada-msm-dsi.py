#!/usr/bin/env python3
"""Build libEnchiladaMsmDsi.a: the enchilada MDSS DSI0 command-channel transport
(drivers/NativeMsmDsi/MsmDsiCmd.c), its DCS packet builder
(drivers/NativeDsiBacklight/DsiDcs.c), the read-only runtime session core
(drivers/NativeMsmDsi/MsmDsiSession.c), the read-only SMMU survey
(drivers/NativeMsmDsi/MsmDsiSmmu.c), the stage-3 layer-3a brightness-transmit
orchestrator (drivers/NativeMsmDsi/MsmDsiBrightness.c), the stage-3 layer-3d OS-facing
brightness CONTROL PLANE (drivers/NativeMsmDsi/MsmDsiControl.c), and the thin 1 Hz C++
probe shim (package/EnchiladaBacklightProbe.cpp == kits/enchilada/ in the build tree).

Runs as a [kernel] archives builder, after the bootstrap pass left the compile records.
Mixed C/C++ build, mirroring scripts/build-enchilada-touch.py: the C++ probe uses the
flags of iokit/DEVELOPMENT/IOPlatformExpert.cpo.json (no -mgeneral-regs-only), the six
pure-C units use those of osfmk/DEVELOPMENT/startup.o.json + -mgeneral-regs-only.
MsmDsiControl.c is the layer-3d(alpha) host-verifiable core: the 0..1023 clamp (vs
sheng 2047) and the 16.16 fade state machine. The layer-3d(beta) IOService class that
calls it still needs a late-object objcopy step (build-native-legacy-display.py:315).

Stage-3 layer 2.5 is deliberately READ-ONLY ("xian heyan zai shangji"): the probe maps
DSI0 at 0xae94004 via the device-proven armcpu_map_physical, reads seven live config
registers, and publishes them into the strong native_backlight_stats[10] so the L st=
row becomes non-zero on device -- WITHOUT writing to the controller or triggering DMA.
It is gated by enchilada-backlight=1 (default off) and called 1 Hz from
package/BootProgress.c monitor(); enchilada-backlight=2 is the read-only SMMU survey
(CI #209, DEVICE-VERIFIED Bypass). enchilada-backlight=3 (layer 3b) is the FIRST WRITE:
it allocates a 32-bit-addressable IOBufferMemoryDescriptor DMA buffer and calls the
host-verified MsmDsiBrightness.c orchestrator once, with dc civac + dsb sy clean.
The shim now includes <IOKit/IOBufferMemoryDescriptor.h> but defines no IOService
subclass, so no late-object objcopy is required. See drivers/NativeMsmDsi/README.md.

The archive is force-loaded into the kernel by the
-Wl,-force_load,{tree}/build/enchilada-msm-dsi/libEnchiladaMsmDsi.a entry in
configs/enchilada_defconfig (CONFIG_XNU_EXTRA_LD_ARCHIVES), guarded by
scripts/check-enchilada-archives-linked.py. board.toml [kernel] archives alone only
orders the build; it does NOT force-load (see kernel.py archive_plan / build-xnu-4k-baseline.sh:755).
The strong native_backlight_stats[] and enchilada_backlight_poll() take precedence over
the weak stand-ins of package/BootProgressStubs.c (libBootProgress.a). The kernel build
does not track this archive (extra archives carry no make dependency), so a changed
archive removes the linked kernel to make the next link use it.
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
    cpp_rec = record('iokit/DEVELOPMENT/IOPlatformExpert.cpo.json')
    c_rec = record('osfmk/DEVELOPMENT/startup.o.json')
    units = [
        (cpp_rec, KIT / 'EnchiladaBacklightProbe.cpp', []),
        (c_rec, MSM / 'MsmDsiSession.c', ['-mgeneral-regs-only']),
        (c_rec, MSM / 'MsmDsiSmmu.c', ['-mgeneral-regs-only']),
        (c_rec, MSM / 'MsmDsiCmd.c', ['-mgeneral-regs-only']),
        (c_rec, MSM / 'MsmDsiBrightness.c', ['-mgeneral-regs-only']),
        (c_rec, MSM / 'MsmDsiControl.c', ['-mgeneral-regs-only']),
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
    sources = [KIT / 'EnchiladaBacklightProbe.cpp', MSM / 'MsmDsiSession.c', MSM / 'MsmDsiSession.h',
               MSM / 'MsmDsiSmmu.c', MSM / 'MsmDsiSmmu.h',
               MSM / 'MsmDsiCmd.c', MSM / 'MsmDsiCmd.h',
               MSM / 'MsmDsiBrightness.c', MSM / 'MsmDsiBrightness.h',
               MSM / 'MsmDsiControl.c', MSM / 'MsmDsiControl.h',
               DCS / 'DsiDcs.c', DCS / 'DsiDcs.h']
    (OUT / 'build-report.json').write_text(json.dumps({
        'archive_sha256': digest,
        'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        'hardware_execution': False,
    }, indent=2) + '\n')
    print(f'PASS: libEnchiladaMsmDsi.a ({len(objects)} objects, sha256 {digest[:16]})')


if __name__ == '__main__':
    main()
