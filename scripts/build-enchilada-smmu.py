#!/usr/bin/env python3
"""Build libEnchiladaSmmu.a: the READ-ONLY stage-1 appSMMU survey
(package/EnchiladaSmmuProbe.cpp + the freestanding pure-C core in
drivers/NativeSmmu) plus the READ-ONLY SUBSET of the upstream ArmSmmu4K kit,
against the recorded XNU compile flags.

Runs as a [kernel] archives builder, after the bootstrap pass left the compile
records. The C++ shim is compiled with the flags of
iokit/IOPlatformExpert.cpo.json (as build-enchilada-touch.py does), the C source
with those of osfmk/DEVELOPMENT/startup.o.json plus -mgeneral-regs-only. Its
core needs no libc (freestanding C11).

The ArmSmmu4K kit was declared in porting.toml's memory area as the intended
component but had never been linked (the record said so honestly). Four of its
units are built here -- ArmSmmu4K.c, ArmSmmu4KContext.c, ArmSmmu4KStreams.c and
ArmSmmu4KProgram.c -- because they form a CLOSED SET: compiled locally, the only
undefined ArmSmmu4K symbols they leave are each other's. ArmSmmu4KLive.c,
ArmSmmu4KAudit.c, ArmSmmu4KTableMemory.c and ArmSmmu4KMmio.c are deliberately
excluded; none adds a check this probe reports, and adding them would widen the
linked surface for no evidence.

The kit is used for exactly one read-only entry point,
ArmSmmu4KCheckFaultedStreams, which answers a question the hand-rolled scan
cannot: whether the target streams are in a state where a stage-1 context could
legally be programmed at all. Its documented precondition is that the target
S2CRs already FAULT, so a board running entirely on firmware bypass answers
ArmSmmu4KBusy. On device that verdict is a gate on the NEXT step, not a failure
of this one.

READ-ONLY is enforced structurally, not by comment: the ArmSmmu4KIo the shim
hands over has write32/write64 that increment a counter and return failure
WITHOUT touching the mapping. The verdict line publishes that counter as w=, so
the device itself reports how many stores the kit attempted. Local replay of the
flash-#7 register state gives writes=0 with a byte-identical register array.

Unlike build-enchilada-gauge.py this archive compiles NO shared transport: the
survey talks to the SMMU window only through a read callback it is handed, so
there is no GeniI2c-style object that could collide with another -force_load'ed
archive. The only external it needs is armcpu_map_physical, which it declares
weak_import and resolves from the ArmCpu archive.

The archive is linked with -force_load through CONFIG_XNU_EXTRA_LD_ARCHIVES; it
defines enchilada_smmu_poll(), which takes precedence over the (deliberately
absent) weak stub -- see package/EnchiladaSmmuProbe.cpp.

The kernel build does not track this archive, so a changed archive removes the
linked kernel to make the next link use it (same as build-enchilada-touch.py).
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/enchilada-smmu'
OBJ_ROOT = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
KIT = ROOT / 'kits/enchilada'            # the device repo's package/ is materialized here in CI
SMMU = ROOT / 'drivers/NativeSmmu'
KIT_SMMU = ROOT / 'kits/ArmSmmu4K'       # the upstream 4 KiB SMMU translator (unchanged sources)
WARNINGS = ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-c99-designator', '-Wno-reorder-init-list',
            '-Wno-deprecated-enum-enum-conversion', '-Wno-unnecessary-virtual-specifier', '-Wno-unknown-warning-option',
            '-Wno-missing-designated-field-initializers']
INCLUDES = ['-I', str(KIT), '-I', str(SMMU), '-I', str(KIT_SMMU), '-I', str(ROOT / '.cache/xnu-work/osfmk')]


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
        (cpp_rec, KIT / 'EnchiladaSmmuProbe.cpp', []),
        (c_rec, SMMU / 'SmmuSurvey.c', ['-mgeneral-regs-only']),
        # The upstream 4 KiB translator, read-only subset ONLY. These four are a
        # closed set (verified locally: the only undefined symbols they leave are
        # satisfied inside it), and the two that the probe calls -- Streams and
        # Program -- touch the IO through io->write32/write64 exclusively, which
        # the shim hands over as NULL-returning functions. ArmSmmu4KLive.c,
        # ArmSmmu4KAudit.c and ArmSmmu4KTableMemory.c are deliberately NOT built:
        # they add no check this probe reports and would pull in more surface.
        (c_rec, KIT_SMMU / 'ArmSmmu4K.c', ['-mgeneral-regs-only']),
        (c_rec, KIT_SMMU / 'ArmSmmu4KContext.c', ['-mgeneral-regs-only']),
        (c_rec, KIT_SMMU / 'ArmSmmu4KStreams.c', ['-mgeneral-regs-only']),
        (c_rec, KIT_SMMU / 'ArmSmmu4KProgram.c', ['-mgeneral-regs-only']),
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
    archive = OUT / 'libEnchiladaSmmu.a'
    previous = hashlib.sha256(archive.read_bytes()).hexdigest() if archive.is_file() else None
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', str(archive), *map(str, objects)], check=True, timeout=60)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != previous:
        (OBJ_ROOT / 'mach.development.bcm2837.unstripped.noctf').unlink(missing_ok=True)
    sources = [KIT / 'EnchiladaSmmuProbe.cpp', SMMU / 'SmmuSurvey.c', SMMU / 'SmmuSurvey.h',
               KIT_SMMU / 'ArmSmmu4K.c', KIT_SMMU / 'ArmSmmu4K.h',
               KIT_SMMU / 'ArmSmmu4KContext.c', KIT_SMMU / 'ArmSmmu4KContext.h',
               KIT_SMMU / 'ArmSmmu4KStreams.c', KIT_SMMU / 'ArmSmmu4KStreams.h',
               KIT_SMMU / 'ArmSmmu4KProgram.c', KIT_SMMU / 'ArmSmmu4KProgram.h']
    (OUT / 'build-report.json').write_text(json.dumps({
        'archive_sha256': digest,
        'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        'hardware_execution': False,
    }, indent=2) + '\n')
    print(f'PASS: libEnchiladaSmmu.a ({len(objects)} objects, sha256 {digest[:16]})')


if __name__ == '__main__':
    main()
