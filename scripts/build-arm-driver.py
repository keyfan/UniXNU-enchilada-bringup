#!/usr/bin/env python3
# From UniXNU-sheng scripts/build-arm-driver.py: compiles one built-in XNU driver against the recorded XNU compile flags.
"""Compile the generic platform expert against the actual source-kernel ABI."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('component', choices=['ArmVirtPlatform', 'ArmDTPlatform', 'ArmCpu', 'NativeAES', 'NativeVirtioBlock', 'NativeRamBlock', 'NativeMobileDisplay', 'NativeVirtioConsole', 'NativeLegacyDisplay', 'NativeGOPPresenter', 'NativeOfflineGPU', 'NativeOfflineAccelerator', 'NativeIOGPUProviderProbe', 'NativeUfs', 'NativeHidInput', 'NativeKeyStoreEvents', 'NativeAdreno'])
parser.add_argument('--negative-aes-kat', action='store_true', help='Isolated own-source wrong-answer refusal control; never changes original inputs')
parser.add_argument('--verified-root', action='store_true', help='Authenticate all actual disk reads against an independently signed immutable policy')
parser.add_argument('--verified-root-bundle', action='store_true', help='Select exactly one capacity-matching policy per device, then independently authenticate it')
parser.add_argument('--negative-verified-block', action='store_true', help='Negative own-buffer control: corrupt only the completed DMA snapshot')
args = parser.parse_args()
if args.verified_root_bundle and not args.verified_root:
    parser.error('A policy bundle requires verified block mode')
if args.verified_root_bundle and args.negative_verified_block:
    parser.error('Select either bundle policy qualification or the isolated DMA snapshot fault')
if (args.verified_root or args.negative_verified_block) and args.component != 'NativeVirtioBlock':
    parser.error('Verified block mode requires the concrete read-only block transport')
if args.negative_verified_block and not args.verified_root:
    parser.error('The isolated input fault requires verified block mode')
component = args.component
recipe = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
record = json.loads(recipe.read_text().strip().rstrip(','))
# The source directory is components.toml's "path" and the file its "source_files": for the components
# this board renames, the directory is the canonical one (drivers/SoftwareAES holds NativeAES.cpp), so
# looking under the legacy name would find nothing.
SOURCE_DIRS = {'NativeAES': 'SoftwareAES', 'NativeVirtioBlock': 'VirtioMmioBlock',
               'NativeVirtioConsole': 'VirtioMmioConsole', 'NativeKeyStoreEvents': 'AppleKeyStoreState',
               'NativeLegacyDisplay': 'NativeMobileDisplay', 'NativeGOPPresenter': 'NativeMobileDisplay',
               'NativeOfflineGPU': 'NativeMetal', 'NativeOfflineAccelerator': 'NativeMetal',
               'NativeIOGPUProviderProbe': 'NativeMetal'}
SOURCE_FILES = {'NativeAdreno': 'NativeAdrenoService.cpp'}
source = (ROOT / 'drivers' / SOURCE_DIRS.get(component, component)
          / SOURCE_FILES.get(component, component + '.cpp'))
# Output directory and archive name follow components.toml's outputs/legacy_outputs: the linker in
# scripts/build-xnu-4k-baseline.sh names the canonical paths (build/sheng-ufs/libShengUfs.a,
# build/ram-block/libRamBlock.a, build/software-aes/libSoftwareAES.a, ...), not the legacy spellings.
CANONICAL = {'NativeAES': 'SoftwareAES', 'NativeVirtioBlock': 'VirtioMmioBlock',
             'NativeVirtioConsole': 'VirtioMmioConsole', 'NativeRamBlock': 'RamBlock',
             'NativeUfs': 'ShengUfs', 'NativeKeyStoreEvents': 'AppleKeyStoreState'}
OUT_DIRS = {'ArmVirtPlatform': 'armvirt-platform', 'ArmDTPlatform': 'armdt-platform', 'ArmCpu': 'armcpu',
            'NativeAES': 'software-aes', 'NativeVirtioBlock': 'virtio-mmio-block', 'NativeRamBlock': 'ram-block',
            'NativeUfs': 'sheng-ufs', 'NativeMobileDisplay': 'native-mobile-display',
            'NativeVirtioConsole': 'virtio-mmio-console', 'NativeLegacyDisplay': 'native-legacy-display',
            'NativeGOPPresenter': 'native-gop-presenter', 'NativeOfflineGPU': 'native-offline-gpu',
            'NativeOfflineAccelerator': 'native-offline-accelerator', 'NativeIOGPUProviderProbe': 'native-original-provider',
            'NativeHidInput': 'native-hid-input', 'NativeKeyStoreEvents': 'apple-key-store-state',
            'NativeAdreno': 'native-adreno'}
out = ROOT / 'build' / OUT_DIRS[component]
if args.verified_root:
    out = ROOT / ('build/native-virtio-block-verified-fault' if args.negative_verified_block else 'build/native-virtio-block-verified')
    if args.verified_root_bundle:
        out = ROOT / 'build/native-virtio-block-verified-bundle'
out.mkdir(parents=True, exist_ok=True)
if args.negative_aes_kat:
    if component != 'NativeAES':
        parser.error('The own-source KAT control requires NativeAES')
    out = ROOT / 'build/native-aes-kat-negative'
    out.mkdir(parents=True, exist_ok=True)
    text = source.read_text()
    marker = '0x76,0x49,0xab,0xac'
    if text.count(marker) != 1:
        raise SystemExit('The exact own-source NIST answer was not uniquely identified')
    (out / 'OriginalAESABI.h').write_bytes(source.with_name('OriginalAESABI.h').read_bytes())
    source = out / 'NativeAES.cpp'
    source.write_text(text.replace(marker, '0x77,0x49,0xab,0xac'))
destination = out / (component + '.o')
arguments = record['arguments']
command = []
index = 0
while index < len(arguments):
    value = arguments[index]
    if value == record['file']:
        command.append(str(source))
    elif value == '-o':
        command.extend(['-o', str(destination)])
        index += 1
    elif value == '-Xclang' and arguments[index + 1].startswith('-W'):
        index += 1
    elif not value.startswith('-W'):
        command.append(value)
    index += 1
command.extend(['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-Wno-c99-designator', '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
                '-Wno-unnecessary-virtual-specifier',
                # Upstream clang's -Wextra has it; XNU's kalloc_type macros trip it (the kernel build disables it too). Older
                # Apple clang does not know it, hence -Wno-unknown-warning-option first.
                '-Wno-unknown-warning-option', '-Wno-missing-designated-field-initializers', '-I', str(ROOT / '.cache/xnu-work/osfmk')])
if component in ('NativeVirtioBlock', 'NativeRamBlock', 'NativeUfs'):
    command += ['-I', str(ROOT / 'build/storage-abi/include')]
    if args.verified_root:
        command += ['-I', str(ROOT / 'kits/VerifiedRoot/include'), '-DNATIVE_VERIFIED_BLOCK=1']
    if args.negative_verified_block:
        command += ['-DNATIVE_VERIFIED_BLOCK_INJECT_FAULT=1']
    if args.verified_root_bundle:
        command += ['-DNATIVE_VERIFIED_BLOCK_MULTI_POLICY=1']
if component == 'NativeUfs':
    command += ['-I', str(ROOT / 'kits/VerifiedRoot/include'), '-I', str(ROOT / 'drivers/VirtioMmioBlock'),
                '-DNATIVE_VERIFIED_BLOCK=1', '-DNATIVE_VERIFIED_BLOCK_MULTI_POLICY=1', '-mgeneral-regs-only']
if component=='NativeOfflineGPU':
    command += ['-I',str(ROOT/'drivers/VirtioMmioConsole')]
if component in ('NativeLegacyDisplay','NativeHidInput'):
    command += ['-I', str(out)]
if component == 'NativeAdreno':
    command += ['-I', str(ROOT / 'kits/ArmSmmu4K'), '-I', str(ROOT / 'kits/AdrenoPM4'),
                '-I', str(ROOT / 'kits/sheng'), '-I', str(ROOT / 'kits/AdrenoFirmware'),
                '-I', str(ROOT / 'build/sheng-gmu')]
(out / 'compile-command.json').write_text(json.dumps(command, indent=2) + '\n')
result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
(out / 'compile.log').write_text(result.stdout)
if result.returncode:
    raise SystemExit(result.stdout[-3500:])
data = destination.read_bytes()
magic, cpu, subtype, kind, count, size = struct.unpack_from('<6I', data)
if (magic, cpu, subtype, kind) != (0xfeedfacf, 0x100000c, 0, 1):
    raise SystemExit('Unexpected platform object architecture')
offset = 32
deployment = None
for _ in range(count):
    cmd, length = struct.unpack_from('<II', data, offset)
    if cmd == 0x32:
        deployment = struct.unpack_from('<II', data, offset + 8)
    offset += length
if deployment != (2, 0x110000):
    raise SystemExit(f'Unexpected platform deployment {deployment}')
archive = out / ('lib' + CANONICAL.get(component, component) + '.a')
archive.unlink(missing_ok=True)
subprocess.run(['xcrun', 'ar', 'rcs', archive, destination], check=True)
# scripts/build-xnu-4k-baseline.sh re-checks the compiled object beside the archive by stripping the
# "lib" prefix off the archive name (build/ram-block/libRamBlock.a -> RamBlock.o), while
# build-native-virtio-bindings.py and build-native-aes-bindings.py read it under the runtime class name.
# Keep one byte-identical copy per spelling so both readers find the same object.
canonical = CANONICAL.get(component, component)
if canonical != component:
    alias = out / (canonical + '.o')
    if not alias.exists() or alias.read_bytes() != data:
        alias.write_bytes(data)
print(f'{component}: -Werror build and actual ARM64/iOS 17 object audit passed')
