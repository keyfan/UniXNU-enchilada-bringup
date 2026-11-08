#!/usr/bin/env python3
"""Build libEnchiladaGauge.a: the READ-ONLY stage-7 bq27xxx fuel-gauge probe
(package/EnchiladaGaugeProbe.cpp + the freestanding pure-C core in
drivers/NativeBattery) against the recorded XNU compile flags.

Runs as a [kernel] archives builder, after the bootstrap pass left the compile
records. The C++ shim is compiled with the flags of
iokit/IOPlatformExpert.cpo.json (as build-enchilada-touch.py does), the two C
sources with those of osfmk/DEVELOPMENT/startup.o.json plus
-mgeneral-regs-only. Its core needs no libc (freestanding C11).

The GENI I2C engine (drivers/NativeGeniI2c/GeniI2c.c) is DELIBERATELY NOT
compiled into this archive: build-enchilada-touch.py already compiles that same
object into libEnchiladaTouch.a and BOTH archives are -force_load'ed into the
same kernel. Compiling it twice would make ld64 report duplicate external
symbols (_geni_i2c_init, _geni_i2c_set_clock, _geni_i2c_write_read, ...).
The gauge shim keeps only the GeniI2c.h declarations and resolves those symbols
from the force-loaded, DEVICE-VERIFIED libEnchiladaTouch.a transport.

The archive is linked with -force_load through CONFIG_XNU_EXTRA_LD_ARCHIVES; it
defines enchilada_gauge_poll(), which takes precedence over the (deliberately
absent) weak stub -- see package/EnchiladaGaugeProbe.cpp.

The kernel build does not track this archive, so a changed archive removes the
linked kernel to make the next link use it (same as build-enchilada-touch.py).
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/enchilada-gauge'
OBJ_ROOT = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
KIT = ROOT / 'kits/enchilada'            # the device repo's package/ is materialized here in CI
I2C = ROOT / 'drivers/NativeGeniI2c'
BAT = ROOT / 'drivers/NativeBattery'
WARNINGS = ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-c99-designator', '-Wno-reorder-init-list',
            '-Wno-deprecated-enum-enum-conversion', '-Wno-unnecessary-virtual-specifier', '-Wno-unknown-warning-option',
            '-Wno-missing-designated-field-initializers']
INCLUDES = ['-I', str(KIT), '-I', str(I2C), '-I', str(BAT), '-I', str(ROOT / '.cache/xnu-work/osfmk'),
            '-I', str(ROOT / 'drivers/NativeMobileDisplay')]


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
    cpp_rec, c_rec = record('iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'), record('osfmk/DEVELOPMENT/startup.o.json')
    units = [
        (cpp_rec, KIT / 'EnchiladaGaugeProbe.cpp', []),
        (c_rec, BAT / 'BqFuelGauge.c', ['-mgeneral-regs-only']),
        (c_rec, BAT / 'BqGaugeProbe.c', ['-mgeneral-regs-only']),
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
    archive = OUT / 'libEnchiladaGauge.a'
    previous = hashlib.sha256(archive.read_bytes()).hexdigest() if archive.is_file() else None
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', str(archive), *map(str, objects)], check=True, timeout=60)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != previous:
        (OBJ_ROOT / 'mach.development.bcm2837.unstripped.noctf').unlink(missing_ok=True)
    sources = [KIT / 'EnchiladaGaugeProbe.cpp', KIT / 'EnchiladaGaugeBoard.h',
               BAT / 'BqFuelGauge.c', BAT / 'BqFuelGauge.h', BAT / 'BqGaugeProbe.c', BAT / 'BqGaugeProbe.h']
    (OUT / 'build-report.json').write_text(json.dumps({
        'archive_sha256': digest,
        'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        'hardware_execution': False,
    }, indent=2) + '\n')
    print(f'PASS: libEnchiladaGauge.a ({len(objects)} objects, sha256 {digest[:16]})')


if __name__ == '__main__':
    main()
