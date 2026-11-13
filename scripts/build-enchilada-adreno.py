#!/usr/bin/env python3
"""Build libEnchiladaAdreno.a: the A630 GPU domain-state probe (package/EnchiladaAdrenoProbe.cpp,
enchilada-adreno=<n>, default off) plus the six freestanding C decision/transport modules, against the
recorded XNU compile flags (those of iokit/IOPlatformExpert.cpo for C++, osfmk/startup.o for C, as
build-enchilada-smmu.py).

WHY THIS ARCHIVE EXISTS: until this script, the NativeAdreno C modules (AdrenoCatalog.c,
AdrenoBoardInputs.c, AdrenoPowerGate.c, AdrenoDomainState.c, and later AdrenoHfiV1.c /
AdrenoFirmwareV1.c) and NativeAdrenoA630HwInit.h were built by NOTHING. scripts/check-enchilada-adreno-host.py
compiled them on the build host only, so the kernel CI never saw them; a header that is wrong for the
kernel's clang or an object that cannot link would have been discovered only after a flash. This makes
them CI artifacts, exactly like the six other -force_load'ed enchilada archives. AdrenoHfiV1.c (round 31)
is the A630 LEGACY GMU HFI v1 transport model; AdrenoFirmwareV1.c (round 33) is the A630 LEGACY GMU
firmware-load contract. Both are freestanding and compiled here so CI verifies their kernel-clang
compilability, even though no kernel caller exists yet.

Linked with -force_load through CONFIG_XNU_EXTRA_LD_ARCHIVES. It defines enchilada_adreno_poll, which
takes precedence over the weak_import stand-in in package/BootProgress.c. A changed archive removes the
linked kernel so that the next link uses it (the extra archives carry no make dependency).

The C modules are freestanding (no libc, no constructors, no MMIO): AdrenoDomainState.c holds the
only register arithmetic, and the MMIO lives in the probe shim, not in any of them.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/enchilada-adreno'
OBJ_ROOT = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
KIT = ROOT / 'kits/enchilada'
GPU = ROOT / 'drivers/NativeAdreno'
WARNINGS = ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-c99-designator', '-Wno-reorder-init-list',
            '-Wno-deprecated-enum-enum-conversion', '-Wno-unnecessary-virtual-specifier', '-Wno-unknown-warning-option',
            '-Wno-missing-designated-field-initializers']
INCLUDES = ['-I', str(KIT), '-I', str(GPU), '-I', str(ROOT / '.cache/xnu-work/osfmk')]


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
    # The order is the probe first, then the decision/transport modules in the
    # order the probe uses them; AdrenoHfiV1.c (round 31) and AdrenoFirmwareV1.c
    # (round 33) are legacy models with no kernel caller yet, but MUST still be
    # compiled by kernel CI.
    units = [
        (cpp_rec, KIT / 'EnchiladaAdrenoProbe.cpp', []),
        (c_rec, GPU / 'AdrenoDomainState.c', ['-mgeneral-regs-only']),
        (c_rec, GPU / 'AdrenoPowerGate.c', ['-mgeneral-regs-only']),
        (c_rec, GPU / 'AdrenoBoardInputs.c', ['-mgeneral-regs-only']),
        (c_rec, GPU / 'AdrenoCatalog.c', ['-mgeneral-regs-only']),
        (c_rec, GPU / 'AdrenoHfiV1.c', ['-mgeneral-regs-only']),
        (c_rec, GPU / 'AdrenoFirmwareV1.c', ['-mgeneral-regs-only']),
        (c_rec, GPU / 'AdrenoGmuBootV1.c', ['-mgeneral-regs-only']),
        (c_rec, GPU / 'AdrenoGmuBusV1.c', ['-mgeneral-regs-only']),
        (c_rec, GPU / 'AdrenoGmuCallerV1.c', ['-mgeneral-regs-only']),
        (c_rec, GPU / 'AdrenoPowerUpV1.c', ['-mgeneral-regs-only']),  # round 51
        (c_rec, GPU / 'AdrenoKGSLSmmuGate.c', ['-mgeneral-regs-only']),  # round 53
        (c_rec, GPU / 'AdrenoGdscDecode.c', ['-mgeneral-regs-only']),  # round 56
        (c_rec, GPU / 'AdrenoPowerUpGdsc.c', ['-mgeneral-regs-only']),  # round 58
        (c_rec, GPU / 'AdrenoPowerDownV1.c', ['-mgeneral-regs-only']),  # round 59
        (c_rec, GPU / 'AdrenoClkCtlV1.c', ['-mgeneral-regs-only']),  # round 61
        (c_rec, GPU / 'AdrenoPowerUpBackendV1.c', ['-mgeneral-regs-only']),  # round 62
        (c_rec, GPU / 'AdrenoGpuccWindowV1.c', ['-mgeneral-regs-only']),  # round 63
        (c_rec, GPU / 'AdrenoCallerV1.c', ['-mgeneral-regs-only']),  # round 64
        (c_rec, GPU / 'AdrenoMetalSeamV1.c', ['-mgeneral-regs-only']),  # round 65
        (c_rec, GPU / 'AdrenoDarwinKmdAbiV1' / 'AdrenoDarwinKmdAbiV1.c', ['-mgeneral-regs-only']),  # round 66
        (c_rec, GPU / 'AdrenoMetalPublishV1.c', ['-mgeneral-regs-only']),  # round 67
        (c_rec, GPU / 'AdrenoGen1BackendV1.c', ['-mgeneral-regs-only']),  # round 69
        (c_rec, GPU / 'AdrenoCpRingV1.c', ['-mgeneral-regs-only']),  # round 70
        (c_rec, GPU / 'AdrenoZapAuthV1.c', ['-mgeneral-regs-only']),  # round 73
        (c_rec, GPU / 'AdrenoMdtV1.c', ['-mgeneral-regs-only']),  # round 74
        (c_rec, GPU / 'AdrenoSmmuStage1V1.c', ['-mgeneral-regs-only']),  # round 75
        (c_rec, GPU / 'AdrenoVaMapV1.c', ['-mgeneral-regs-only']),  # round 77
        (c_rec, GPU / 'AdrenoSmmuCtxWordV1.c', ['-mgeneral-regs-only']),  # round 83
        (c_rec, GPU / 'AdrenoSmmuAsidV1.c', ['-mgeneral-regs-only']),  # round 102
        (c_rec, GPU / 'AdrenoIommuProbeV1.c', ['-mgeneral-regs-only']),  # round 86
    ]
    objects, commands = [], {}
    for rec, source, extra in units:
        obj = OUT / (source.stem + '.o')
        command = command_for(rec, source, obj) + WARNINGS + extra + INCLUDES
        commands[source.name] = command
        result = subprocess.run(command, cwd=rec['directory'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=180)
        (OUT / (source.stem + '.log')).write_text(result.stdout)
        if result.returncode:
            raise SystemExit(result.stdout[-4000:])
        check_object(obj)
        objects.append(obj)
    (OUT / 'compile-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    archive = OUT / 'libEnchiladaAdreno.a'
    previous = hashlib.sha256(archive.read_bytes()).hexdigest() if archive.is_file() else None
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', str(archive), *map(str, objects)], check=True, timeout=60)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != previous:
        (OBJ_ROOT / 'mach.development.bcm2837.unstripped.noctf').unlink(missing_ok=True)
    sources = [KIT / 'EnchiladaAdrenoProbe.cpp']
    for stem in ('AdrenoDomainState', 'AdrenoPowerGate', 'AdrenoBoardInputs', 'AdrenoCatalog',
                 'AdrenoHfiV1', 'AdrenoFirmwareV1', 'AdrenoGmuBootV1', 'AdrenoGmuBusV1',
                 'AdrenoGmuCallerV1', 'AdrenoPowerUpV1', 'AdrenoKGSLSmmuGate',
                 'AdrenoGdscDecode', 'AdrenoPowerUpGdsc', 'AdrenoPowerDownV1',
                 'AdrenoClkCtlV1', 'AdrenoPowerUpBackendV1', 'AdrenoGpuccWindowV1',
                 'AdrenoCallerV1', 'AdrenoMetalSeamV1', 'AdrenoMetalPublishV1',
                 'AdrenoGen1BackendV1', 'AdrenoCpRingV1', 'AdrenoZapAuthV1',
                 'AdrenoMdtV1', 'AdrenoSmmuStage1V1',
                 'AdrenoVaMapV1', 'AdrenoSmmuCtxWordV1', 'AdrenoIommuProbeV1'):
        sources += [GPU / f'{stem}.c', GPU / f'{stem}.h']
    # Round 66: the ABI directory contract lives in its own subdirectory.
    sources += [GPU / 'AdrenoDarwinKmdAbiV1' / 'AdrenoDarwinKmdAbiV1.c',
                GPU / 'AdrenoDarwinKmdAbiV1' / 'NativeAdrenoABI.h']
    sources.append(GPU / 'NativeAdrenoA630HwInit.h')
    (OUT / 'build-report.json').write_text(json.dumps({
        'archive_sha256': digest,
        'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        'hardware_execution': False,
    }, indent=2) + '\n')
    print(f'PASS: libEnchiladaAdreno.a ({len(objects)} objects, sha256 {digest[:16]})')


if __name__ == '__main__':
    main()
