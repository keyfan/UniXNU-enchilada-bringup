#!/usr/bin/env python3
"""Bind the concrete block driver to the qualified original ordinary iOS class."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'kits/XnuPrelink'))
import native_prelink as np
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--component', choices=['VirtioMmioBlock', 'RamBlock', 'NativeVirtioBlock', 'NativeRamBlock'], default='VirtioMmioBlock')
parser.add_argument('--directory', type=Path, default=ROOT / 'build/virtio-mmio-block')
args = parser.parse_args()
out = args.directory.resolve()
audit = json.loads((ROOT / 'build/storage-abi/interface-audit.json').read_text())
canonical = {'NativeVirtioBlock': 'VirtioMmioBlock', 'NativeRamBlock': 'RamBlock'}.get(args.component, args.component)
component = {'VirtioMmioBlock': 'NativeVirtioBlock', 'RamBlock': 'NativeRamBlock'}[canonical]
mangled = str(len(component)) + component
obj_path = out / (component + '.o')
obj = obj_path.read_bytes()
image = np.macho(obj)
if not audit['live_native_4k'] or audit['method_slots'] != 190 or not audit['abstract_complete_destructors_trap']:
    raise SystemExit('First qualify original live storage metadata and its real base functions')
offset = next(at for cmd, at, size in image['commands'] if cmd == 2)
sy, count, st, _ = struct.unpack_from('<4I', obj, offset + 8)
symbols = []
for i in range(count):
    ix, kind, _, _, value = struct.unpack_from('<IBBHQ', obj, sy + i * 16)
    symbols.append((obj[st + ix:obj.index(b'\0', st + ix)].decode(), value, kind))
vtable = next(v for name, v, kind in symbols if name == '__ZTV' + mangled)
relocations = {}
for segment in image['segments']:
    for section in segment['sections']:
        if section['va'] <= vtable < section['va'] + section['size']:
            ro, rc = struct.unpack_from('<II', obj, section['command_offset'] + 56)
            for i in range(rc):
                address, word = struct.unpack_from('<II', obj, ro + i * 8)
                if word >> 27 & 1 and word >> 28 == 0 and word >> 25 & 3 == 3:
                    relocations[section['va'] + address] = symbols[word & 0xffffff][0]
compiled = [relocations.get(vtable + 16 + i * 8) for i in range(190)]
if not all(compiled) or vtable + 16 + 190 * 8 in relocations or '___cxa_pure_virtual' in compiled:
    raise SystemExit('Concrete block class has missing/additional/pure virtual methods')
all_names = list(dict.fromkeys(compiled + [r['reference_name'] for r in audit['methods']]))
demangled = subprocess.check_output(['xcrun', 'c++filt'], input='\n'.join(n[1:] for n in all_names) + '\n', text=True).splitlines()
readable = dict(zip(all_names, demangled))
def demangle(names):
    if not names:
        return {}
    text = subprocess.check_output(['xcrun', 'c++filt'], input='\n'.join(n[1:] for n in names) + '\n', text=True)
    return dict(zip(names, text.splitlines()))


overrides = 0
problems = []
for row, actual in zip(audit['methods'], compiled):
    expected = row['reference_name']
    own = actual.startswith(('__ZN' + mangled, '__ZNK' + mangled))
    if expected == '___cxa_pure_virtual':
        # Two different things read the same in the audit. A slot whose pointer resolves to an export
        # named ___cxa_pure_virtual is a real BRK trap the concrete class must satisfy. A slot whose
        # pointer lands in the original kext's own text has no name at all (kernel_names is empty):
        # that is a base-class virtual the original inherited, and inheriting it is the correct answer,
        # so the genuine base import is checked by the qualified-import test below instead.
        if row['kernel_names']:
            if not own:
                problems.append(f'slot {row["slot"]}: the original traps here, we dispatch to {actual}')
    elif actual != expected:
        if not own or (row['slot'] not in (0, 1) and readable[actual].split('::', 1)[1] != readable[expected].split('::', 1)[1]):
            problems.append(f'slot {row["slot"]}: expected {expected}, we dispatch to {actual}')
    overrides += own
if problems:
    # Every mismatch at once: the original leaves some slots to the concrete class and implements others
    # itself, so which slots a new transport must add is only visible in the full list.
    names = demangle([line.rsplit(' ', 1)[-1] for line in problems])
    report = '\n'.join(f'  {line}\n      {names.get(line.rsplit(" ", 1)[-1], "")}' for line in problems)
    raise SystemExit(f'{len(problems)} concrete block slots disagree with the original:\n{report}')
kernel_path = ROOT / 'build/xnu-prelink/native-apfs-load-positive.macho'
# The binding values come from the recorded audit; the candidate only proves the audit's identity. Build products are pruned, so a
# missing candidate is accepted (the audit JSON is the pinned record) while a present one must still match.
if kernel_path.exists() and hashlib.sha256(kernel_path.read_bytes()).hexdigest() != audit['candidate_sha256']:
    raise SystemExit('Original binding input identity changed')
# A slot the original leaves unnamed holds a base-class implementation the subclass inherits, so the
# name to bind is whatever our own object references there, not the audit's placeholder. Slots that
# really resolve to ___cxa_pure_virtual are traps and must never be bound.
possible = {}
for row, actual in zip(audit['methods'], compiled):
    if not row['kernel_names']:
        possible[actual] = int(row['candidate_target'], 16)
possible.update({'__ZN20IOBlockStorageDeviceC2EPK11OSMetaClass': int(audit['original_protected_constructor'], 16),
                 '__ZN20IOBlockStorageDeviceD2Ev': int(audit['original_base_destructor'], 16),
                 '__ZN20IOBlockStorageDevice10gMetaClassE': int(audit['original_metaclass'], 16),
                 '__ZTV20IOBlockStorageDevice': int(audit['vtable'], 16)})
undefined = [name for name, value, kind in symbols if kind == 1 and ('IOBlockStorageDevice' in name)]
if any(name not in possible for name in undefined):
    raise SystemExit('Unqualified original storage import')
# Abstract D1/D0 are BRK traps. Only actual base D2 is appropriate for this subclass.
if any(re.search(r'IOBlockStorageDeviceD[01]Ev$', n) for n in undefined):
    raise SystemExit('Concrete driver must not invoke abstract complete/deleting destruction')
bindings = {name: possible[name] for name in undefined}
assembly = out / 'original-bindings.S'
assembly.write_text(''.join(f'.globl {name}\n.set {name}, {value:#x}\n' for name, value in sorted(bindings.items())))
subprocess.run(['xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-Werror', '-Wno-incompatible-sysroot',
                '-c', assembly, '-o', out / 'original-bindings.o'], check=True)
archive = out / ('lib' + canonical + '.a')
archive.unlink(missing_ok=True)
subprocess.run(['xcrun', 'ar', 'rcs', archive, obj_path, out / 'original-bindings.o'], check=True)
(out / 'bindings.json').write_text(json.dumps(dict(original_candidate_sha256=audit['candidate_sha256'],
    compiled_object_sha256=hashlib.sha256(obj).hexdigest(), virtual_methods_checked=190,
    concrete_overrides=overrides, unresolved_original_imports=0,
    bindings={name: hex(value) for name, value in bindings.items()}, original_code_modified=False,
    actual_io_qualified=False), indent=2) + '\n')
print(f'PASS: 190 concrete slots, {overrides} overrides and {len(bindings)} genuine original storage imports')
print('Actual device I/O and filesystem integration remain unqualified by this compile/ABI audit.')
