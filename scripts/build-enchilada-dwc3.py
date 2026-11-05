#!/usr/bin/env python3
"""Compile the enchilada NativeDwc3 USB device-mode driver against this source
kernel's actual ARM64/iOS 17 IOKit ABI and audit the resulting Mach-O objects, the
way scripts/build-arm-driver.py does for the other built-in drivers.

Output (ignored, never committed): build/enchilada-dwc3/libNativeDwc3.a,
force-loaded into the kernel through CONFIG_XNU_EXTRA_LD_ARCHIVES (board.toml /
enchilada_defconfig). The driver is default-off; it reads nothing unless
native-dwc3-probe=1 is set, and it writes nothing below native-dwc3-probe=3.
"""
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
# Every translation unit of the driver. Stage B lives in its own file so the
# read-only probe stays readable and so a stage-B build failure cannot be mistaken
# for a probe one.
SOURCES = [
    ROOT / 'drivers/NativeDwc3/NativeDwc3.cpp',
    ROOT / 'drivers/NativeDwc3/Dwc3Gadget.cpp',
]
OUT = ROOT / 'build/enchilada-dwc3'
OUT.mkdir(parents=True, exist_ok=True)

# The recorded compile command of a representative IOKit C++ translation unit: the
# same recipe build-arm-driver.py replays, so the driver is built with the exact
# flags, defines and header set of this kernel's IOKit.
recipe = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
record = json.loads(recipe.read_text().strip().rstrip(','))


def command_for(source, dest):
    command = []
    index = 0
    arguments = record['arguments']
    while index < len(arguments):
        value = arguments[index]
        if value == record['file']:
            command.append(str(source))
        elif value == '-o':
            command.extend(['-o', str(dest)])
            index += 1
        elif value == '-Xclang' and arguments[index + 1].startswith('-W'):
            index += 1
        elif not value.startswith('-W'):
            command.append(value)
        index += 1
    command.extend(['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-c99-designator', '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
                    '-Wno-unnecessary-virtual-specifier', '-Wno-unknown-warning-option',
                    '-Wno-missing-designated-field-initializers',
                    '-mgeneral-regs-only',
                    '-I', str(ROOT / '.cache/xnu-work/osfmk'),
                    '-I', str(ROOT / 'drivers/NativeDwc3'),
                    '-I', str(ROOT / 'drivers/NativeUfs')])
    return command


def audit(dest):
    """ARM64 (cpu 0x100000c, subtype 0), iOS deployment (LC_BUILD_VERSION platform
    2, min 0x110000 = 17.0) -- the same checks build-arm-driver.py applies."""
    data = dest.read_bytes()
    magic, cpu, subtype, kind, count, size = struct.unpack_from('<6I', data)
    if (magic, cpu, subtype, kind) != (0xfeedfacf, 0x100000c, 0, 1):
        raise SystemExit(f'Unexpected {dest.name} architecture')
    offset = 32
    deployment = None
    for _ in range(count):
        cmd, length = struct.unpack_from('<II', data, offset)
        if cmd == 0x32:   # LC_BUILD_VERSION
            deployment = struct.unpack_from('<II', data, offset + 8)
        offset += length
    if deployment != (2, 0x110000):
        raise SystemExit(f'Unexpected {dest.name} deployment {deployment}')


objects = []
for source in SOURCES:
    dest = OUT / (source.stem + '.o')
    command = command_for(source, dest)
    (OUT / f'compile-command-{source.stem}.json').write_text(json.dumps(command, indent=2) + '\n')
    result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (OUT / f'compile-{source.stem}.log').write_text(result.stdout)
    if result.returncode:
        # Print the error lines first: the tail is usually warning noise, and the
        # caller only shows the last few lines of a build log.
        errors = [line for line in result.stdout.splitlines() if 'error:' in line]
        for line in errors[:20]:
            print(line)
        raise SystemExit(result.stdout[-3500:])
    audit(dest)
    objects.append(dest)

archive = OUT / 'libNativeDwc3.a'
archive.unlink(missing_ok=True)
subprocess.run(['xcrun', 'ar', 'rcs', str(archive)] + [str(o) for o in objects], check=True)
print(f'NativeDwc3: -Werror build and ARM64/iOS 17 object audit passed ({len(objects)} objects)')
