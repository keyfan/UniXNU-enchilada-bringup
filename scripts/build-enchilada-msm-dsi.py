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
Mixed C/C++ build, mirroring scripts/build-enchilada-touch.py: the C++ units use the
flags of iokit/DEVELOPMENT/IOPlatformExpert.cpo.json (no -mgeneral-regs-only), the six
pure-C units use those of osfmk/DEVELOPMENT/startup.o.json + -mgeneral-regs-only.

Stage-3 layer 3d (beta) adds a SECOND C++ object, package/EnchiladaBacklight.cpp, which
defines the OS-facing EnchiladaBacklight : IOService (metaclass impersonation,
IODisplayParameters.brightness {0..1023}, setProperties() from backboardd). Because that
object has a C++ static initializer (__GLOBAL__sub_I_EnchiladaBacklight.cpp) that must run
at a CONTROLLED time (between preModLoad and postModLoad, driven by the 1 Hz mode-4 poll),
it is staged by late_init(): llvm-objcopy --remove-section=__DATA,__mod_init_func
--globalize-symbol=__GLOBAL__sub_I_EnchiladaBacklight.cpp, exactly like
scripts/build-native-legacy-display.py:315-321. The __text section bytes are compared
before/after to prove the staging changed no owned instructions. The .late.o replaces the
plain .o as the archive member.

Stage-3 layer 2.5 is deliberately READ-ONLY ("xian heyan zai shangji"): the probe maps
DSI0 at 0xae94004 via the device-proven armcpu_map_physical, reads seven live config
registers, and publishes them into the strong native_backlight_stats[10] so the L st=
row becomes non-zero on device -- WITHOUT writing to the controller or triggering DMA.
It is gated by enchilada-backlight=1 (default off) and called 1 Hz from
package/BootProgress.c monitor(); enchilada-backlight=2 is the read-only SMMU survey
(CI #209, DEVICE-VERIFIED Bypass). enchilada-backlight=3 (layer 3b) is the FIRST WRITE:
it allocates a 32-bit-addressable IOBufferMemoryDescriptor DMA buffer and calls the
host-verified MsmDsiBrightness.c orchestrator once, with dc civac + dsb sy clean.
enchilada-backlight=4 (layer 3d beta) is the OS-facing service that closes the loop from
an OS brightness setProperties() request to that same DEVICE-VERIFIED transmit path.
Modes 1/2/3 bodies stay byte-for-byte unchanged (all device-verified).

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
import os
from pathlib import Path
import shutil
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


def text_bytes(data):
    """Return the __TEXT,__text section bytes of an arm64 Mach-O object (native_prelink.macho shape)."""
    magic, cpu, subtype, kind, ncmds, sizeofcmds = struct.unpack_from('<6I', data, 0)
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<II', data, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            nsects = struct.unpack_from('<I', data, off + 64)[0]
            sec = off + 72
            for _s in range(nsects):
                sectname = data[sec:sec + 16].rstrip(b'\0')
                segname = data[sec + 16:sec + 32].rstrip(b'\0')
                if segname == b'__TEXT' and sectname == b'__text':
                    size, fileoff = struct.unpack_from('<Q', data, sec + 40)[0], struct.unpack_from('<I', data, sec + 48)[0]
                    return data[fileoff:fileoff + size]
                sec += 80
        off += cmdsize
    raise SystemExit('object has no __TEXT,__text section')


def find_objcopy():
    """Same lookup as build-native-legacy-display.py:70-82 (Homebrew llvm on macOS CI)."""
    objcopy = shutil.which('llvm-objcopy')
    if not objcopy and shutil.which('brew'):
        prefix = subprocess.check_output(['brew', '--prefix', 'llvm'], text=True).strip()
        objcopy = str(Path(prefix) / 'bin/llvm-objcopy')
    if not objcopy or not Path(objcopy).is_file():
        raise SystemExit('llvm-objcopy not found on PATH (or via brew --prefix llvm)')
    return objcopy


def late_init(name):
    """Drop the object's static-initializer registration, globalize the init symbol, and
    prove the owned __text instructions are unchanged. Mirror
    scripts/build-native-legacy-display.py:315-321. Returns the staged .late.o path."""
    source, late = OUT / f'{name}.o', OUT / f'{name}.late.o'
    objcopy = find_objcopy()
    subprocess.run([objcopy, '--remove-section=__DATA,__mod_init_func',
                    f'--globalize-symbol=__GLOBAL__sub_I_{name}.cpp', str(source), str(late)],
                   check=True, timeout=120)
    check_object(late)
    if text_bytes(source.read_bytes()) != text_bytes(late.read_bytes()):
        raise SystemExit(f'{name}: __text changed by late-init staging')
    return late


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cpp_rec = record('iokit/DEVELOPMENT/IOPlatformExpert.cpo.json')
    c_rec = record('osfmk/DEVELOPMENT/startup.o.json')
    units = [
        (cpp_rec, KIT / 'EnchiladaBacklightProbe.cpp', []),
        (cpp_rec, KIT / 'EnchiladaBacklight.cpp', []),          # layer 3d beta: the IOService object
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

    # EnchiladaBacklight.cpp carries a C++ static initializer that the mode-4 poll runs
    # between preModLoad/postModLoad. Stage it so it is NOT auto-run at load time, and
    # swap the staged .late.o into the archive member list (build-native-legacy-display.py:389).
    staged = late_init('EnchiladaBacklight')
    objects = [staged if o.name == 'EnchiladaBacklight.o' else o for o in objects]

    archive = OUT / 'libEnchiladaMsmDsi.a'
    previous = hashlib.sha256(archive.read_bytes()).hexdigest() if archive.is_file() else None
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', str(archive), *map(str, objects)], check=True, timeout=60)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != previous:
        (OBJ_ROOT / 'mach.development.bcm2837.unstripped.noctf').unlink(missing_ok=True)
    sources = [KIT / 'EnchiladaBacklightProbe.cpp', KIT / 'EnchiladaBacklight.cpp',
               MSM / 'MsmDsiSession.c', MSM / 'MsmDsiSession.h',
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
