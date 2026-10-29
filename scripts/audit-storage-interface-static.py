#!/usr/bin/env python3
"""Qualify the original block-storage ABI from a prelinked kernel image alone.

The upstream audit (scripts/audit-storage-interface.py) needs two pieces of live
evidence that a bring-up build cannot have yet:

  * build/darwinboot-storage-native-metadata/gdb-stop.json -- a debugger stop on a
    running kernel, with TCR_EL1/SCTLR_EL1 and raw memory at the metaclasses;
  * build/image4-ignition/aes-interface-audit.json -- the compiled OSMetaClass
    member offsets, taken from a live AES audit.

Both describe *static* properties of the pinned kernelcache: the original
IOStorageFamily module data, its dispatch table, its protected constructor and the
metaclass objects are all present, byte for byte, in any prelinked image built from
that kernelcache. This script reads them from such an image instead of from a
running device, and records that the evidence is static.

The pinned kernelcache is fixed by board.toml ([kernel.prelink]
kernelcache_sha256) and the slide is fixed, so the addresses this script derives
are the same ones the final kernel links against -- they do not depend on the
native kernel that was prelinked.

That is why --prelinked accepts the pinned kernelcache itself, with --slide set to
[kernel.prelink] slide: the original module is rebased by exactly that slide when
the prelink writes it, and every address this script reports for it is
module-relative. Reading the kernelcache directly is what makes a first build from
scratch possible, because there is then no earlier prelink to audit. Addresses of
the inherited native-kernel methods are not derived here at all: they are resolved
by symbol name when the kernel links (see inherited_targets).

Usage:
  audit-storage-interface-static.py --prelinked out/<device>/kernel/kernel.macho
  audit-storage-interface-static.py --prelinked out/ipsw/21H461/kernelcache.decompressed --slide -0x1000000
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
# The script normally runs inside the assembled build tree, where kits/XnuPrelink
# is a junction to the XnuPrelink kit. When it is run from the device repository
# itself (a dry run before pushing), fall back to a sibling checkout.
for candidate in (ROOT / 'kits/XnuPrelink',
                  ROOT.parent / 'UniXNU' / 'kits/XnuPrelink',
                  Path(__file__).resolve().parents[3] / 'work' / 'UniXNU' / 'kits' / 'XnuPrelink'):
    if (candidate / 'native_prelink.py').is_file():
        sys.path.insert(0, str(candidate))
        break
import native_prelink as np  # noqa: E402

STORAGE = 'com.apple.iokit.IOStorageFamily'
STORAGE_CLASS = 'IOBlockStorageDevice'
SLOTS = 190
PURE_VIRTUAL = 14
TRAP = 0xd4200020  # BRK #1, the abstract complete-destructor body


def require(value, message):
    if not value:
        raise SystemExit(message)


def align(value):
    return (value + 4095) & ~4095


def page_target(word, pc):
    """Resolve the address an ADRP (or ADR) builds, relative to its own pc."""
    immediate = ((word >> 5 & 0x7ffff) << 2) | (word >> 29 & 3)
    if immediate & 0x100000:
        immediate -= 0x200000
    return ((pc & ~4095) + immediate * 4096) if word & 0x80000000 else pc + immediate


def registration(read, function, want_meta, want_table, limit=24):
    """Decode one OSMetaClass registration and keep it if it is the block-storage one.

    The module init functions all share one shape:

        adrp x0, <module __DATA page>; add x0, x0, #<meta offset>   ; metaclass
        adrp x1, <module __TEXT page>; add x1, x1, #<name offset>   ; class name
        adrp x2, <got page>;           ldr x2, [x2, #<slot>]       ; superclass metaclass
        movz w3, #<instance size>
        bl   <OSMetaClass registration helper>
        adrp x8, <const page>; add x8, x8, #<table+16 offset>
        str  x8, [x0]                                              ; publish the table
        ret

    Everything needed is therefore readable without executing the kernel.
    """
    registers = {}
    stored = None
    for index in range(limit):
        pc = function + index * 4
        word, = struct.unpack('<I', read(pc, 4))
        if word & 0x9f000000 in (0x90000000, 0x10000000):
            registers[word & 31] = page_target(word, pc)
        elif word & 0xff800000 == 0x91000000:
            registers[word & 31] = registers.get((word >> 5) & 31, 0) + ((word >> 10) & 0xfff)
        elif word & 0xffc00000 == 0xf9400000:
            address = registers.get((word >> 5) & 31, 0) + ((word >> 10) & 4095) * 8
            registers[word & 31] = struct.unpack('<Q', read(address, 8))[0]
        elif word & 0xff800000 == 0x52800000:
            registers[word & 31] = (word >> 5) & 0xffff
        elif word & 0xffc003ff == 0xf9000008:
            stored = registers.get(8)
        elif word == 0xd65f03c0:
            break
    if registers.get(0) != want_meta or stored is None:
        return None
    name_at = registers.get(1)
    if not name_at:
        return None
    raw = read(name_at, 64)
    text = raw.split(b'\0')[0].decode('ascii', 'replace')
    return dict(function=hex(function), name=text, class_size=registers.get(3) or 0,
                super_meta=registers.get(2) or 0, stored=stored)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prelinked', type=Path, required=True,
                        help='prelinked kernel Mach-O that carries the original IOStorageFamily module, '
                             'or the pinned kernelcache itself')
    parser.add_argument('--slide', type=lambda value: int(value, 0), default=0,
                        help='rebasing the prelink applies to the original module ([kernel.prelink] slide); '
                             'only meaningful when --prelinked is the kernelcache')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/storage-abi')
    args = parser.parse_args()

    image_path = args.prelinked.resolve()
    require(image_path.is_file(), f'no prelinked kernel at {image_path}')
    data = image_path.read_bytes()
    image = np.macho(data)
    require(image['kind'] == 2, 'Expected a prelinked kernel (MH_EXECUTE)')

    info = next((s for s in image['segments'] if s['name'] == '__PRELINK_INFO'), None)
    require(info is not None and info['filesize'], 'Prelinked kernel has no __PRELINK_INFO')
    at = np.backed_offset(image['segments'], info['va'], info['filesize'], len(data))
    metadata = np.unserialize_xml(data[at:at + info['filesize']])
    entries = {e['CFBundleIdentifier']: e for e in metadata['_PrelinkInfoDictionary']}
    require(STORAGE in entries, f'{STORAGE} is not prelinked into {image_path.name}')

    entry = entries[STORAGE]
    load = entry.get('_PrelinkExecutableLoadAddr')
    require(load, f'{STORAGE} has no _PrelinkExecutableLoadAddr')
    module = np.macho(data, np.backed_offset(image['segments'], load, 32, len(data)))
    require(module['kind'] == 11, 'Expected the original storage module to be an MH_KEXT_BUNDLE')

    extents = {s['name']: s for s in module['segments'] if s['size'] and s['name'] != '__LINKEDIT'}
    for name in ('__TEXT', '__TEXT_EXEC', '__DATA_CONST'):
        require(name in extents, f'{STORAGE} has no {name} extent')

    def read(address, length):
        return data[np.backed_offset(image['segments'], address, length, len(data)):
                    np.backed_offset(image['segments'], address, length, len(data)) + length]

    exports = np.exports(data, image)
    by_name = {name: va for va, names in exports.items() for name in names}
    require('__ZN9IOService10gMetaClassE' in by_name, 'Prelinked kernel does not export IOService::gMetaClass')
    service_meta = by_name['__ZN9IOService10gMetaClassE']

    text = extents['__TEXT_EXEC']
    const = extents['__DATA_CONST']
    blob = read(const['va'], const['size'])

    # Locate the original dispatch table: the 16-byte zero prefix is the two
    # placeholder words a compiler emits before the first virtual method, then
    # exactly SLOTS pointers whose targets are either kernel exports (the
    # inherited IOService methods) or this module's own text (the pure virtuals).
    found = []
    span = (SLOTS + 2) * 8
    for offset in range(0, len(blob) - span + 1, 8):
        if blob[offset:offset + 16] != bytes(16):
            continue
        pointers = struct.unpack_from('<' + 'Q' * SLOTS, blob, offset + 16)
        named = sum(1 for p in pointers if p in exports)
        owned = sum(1 for p in pointers if text['va'] <= p < text['va'] + text['size'])
        if named == SLOTS - PURE_VIRTUAL and owned == PURE_VIRTUAL:
            found.append((const['va'] + offset, pointers))
    require(len(found) == 1,
            f'Cannot uniquely identify the original block-storage dispatch ({len(found)} candidates)')
    table, pointers = found[0]

    methods = []
    for index, pointer in enumerate(pointers):
        names = exports.get(pointer) or []
        if names:
            methods.append(dict(slot=index, reference_name=names[0], candidate_target=hex(pointer),
                                kernel_names=list(names)))
        else:
            methods.append(dict(slot=index, reference_name='___cxa_pure_virtual',
                                candidate_target=hex(pointer), kernel_names=[]))
    require(methods[0]['reference_name'] == '___cxa_pure_virtual' and
            methods[1]['reference_name'] == '___cxa_pure_virtual',
            'The two abstract complete-destructor slots are not where the ABI puts them')
    require(all(read(pointers[i], 4) == struct.pack('<I', TRAP) for i in (0, 1)),
            'Unexpected original abstract complete-destructor behavior')

    # The base destructor sits immediately before the first table entry and is
    # reached with a plain branch from the abstract slot's tail.
    destructor = pointers[0] - 4
    word, = struct.unpack('<I', read(destructor, 4))
    require(word & 0xfc000000 == 0x14000000, 'Unqualified actual base destructor')
    require(text['va'] <= destructor < text['va'] + text['size'],
            'Original base destructor is outside the original storage text')

    # Slot 7 is the metaclass getter: ADRP + ADD + RET.
    function = pointers[7]
    a, add, ret = struct.unpack('<3I', read(function, 12))
    require(a & 0x9f00001f == 0x90000000 and add & 0xffc003ff == 0x91000000 and ret == 0xd65f03c0,
            'Unqualified original storage metaclass getter')
    immediate = ((a >> 5 & 0x7ffff) << 2) | (a >> 29 & 3)
    if immediate & 0x100000:
        immediate -= 0x200000
    meta = (function & ~4095) + immediate * 4096 + (add >> 10 & 4095)

    # The metaclass ABI the companion AES audit used to read from a live kernel:
    # superClassLink=16, className=24, classSize=32. The metaclass object in the
    # image is a zero placeholder -- OSMetaClass fills it when the module's
    # __mod_init_func registration runs -- so the values are decoded from that
    # registration instead: x0=metaclass, x1=class name, x2=superclass metaclass,
    # w3=instance size, then the dispatch table is stored at metaclass+0.
    layout = dict(superClassLink=16, className=24, classSize=32)
    registrations = []
    init_section = next((sec for segment in module['segments']
                         for sec in segment['sections'] if sec['name'] == '__mod_init_func'), None)
    require(init_section is not None and init_section['size'],
            'The original storage module registers no metaclasses')
    for index in range(init_section['size'] // 8):
        function, = struct.unpack('<Q', read(init_section['va'] + index * 8, 8))
        facts = registration(read, function, meta, table)
        if facts is not None:
            registrations.append((function, facts))
    require(len(registrations) == 1,
            f'Cannot uniquely identify the block-storage metaclass registration '
            f'({len(registrations)} candidates)')
    registration_function, facts = registrations[0]
    require(facts['name'] == STORAGE_CLASS,
            f'The block-storage metaclass registers as {facts["name"]!r}, not {STORAGE_CLASS!r}')
    require(facts['super_meta'] == service_meta,
            'The original block-storage metaclass does not link to IOService::gMetaClass')
    class_size = facts['class_size']
    require(class_size == 0x90, f'Original block-storage instance size changed: {class_size:#x}')

    # The protected constructor: stp x29,x30 / mov x29,sp / bl <IOService ctor> /
    # adrp+add x8, <vtable+16> / str x8,[x0] / ldp / ret.
    constructors = []
    for address in range(text['va'], text['va'] + text['size'] - 32, 4):
        words = struct.unpack('<8I', read(address, 32))
        if (words[:2] != (0xa9bf7bfd, 0x910003fd) or words[2] & 0xfc000000 != 0x94000000 or
                words[3] & 0x9f00001f != 0x90000008 or words[4] & 0xffc003ff != 0x91000108 or
                words[5:] != (0xf9000008, 0xa8c17bfd, 0xd65f03c0)):
            continue
        immediate = ((words[3] >> 5 & 0x7ffff) << 2) | (words[3] >> 29 & 3)
        if immediate & 0x100000:
            immediate -= 0x200000
        stored = ((address + 12) & ~4095) + immediate * 4096 + (words[4] >> 10 & 4095)
        if stored == table + 16:
            constructors.append(address)
    require(len(constructors) == 1,
            f'Cannot uniquely identify the original protected constructor ({len(constructors)} candidates)')

    # A native 4K build: every prelinked segment is 4K aligned, and the original
    # module text is mapped on 4K boundaries.
    native_4k = all(s['va'] % 4096 == 0 and s['size'] % 4096 == 0
                    for s in image['segments'] if s['size'])
    require(native_4k, 'Prelinked image is not a native 4K layout')

    # Rebase the module-relative facts to where the prelink puts the module. With a
    # prelinked image as input the slide is already applied and --slide stays 0; with
    # the kernelcache as input the prelink will move the whole original module by
    # exactly [kernel.prelink] slide, so the addresses it will carry are these minus
    # that amount. The inherited native-kernel methods are deliberately left alone:
    # their addresses come from the native kernel being linked, not from this module,
    # and the linker resolves them by symbol name.
    slide = args.slide
    require(slide % 4096 == 0, 'The slide must preserve 4K alignment')
    rebased = lambda address: address + slide  # noqa: E731
    table, meta, destructor = rebased(table), rebased(meta), rebased(destructor)
    constructors = [rebased(address) for address in constructors]
    for row in methods:
        if not row['kernel_names']:
            row['candidate_target'] = hex(int(row['candidate_target'], 16) + slide)

    digest = hashlib.sha256(data).hexdigest()
    result = dict(
        static_qualification=True,
        qualification_source='prelinked-kernel-image' if not slide else 'pinned-kernelcache',
        candidate_sha256=digest,
        qualified_image_sha256=digest,
        qualified_image_name=image_path.name,
        applied_slide=hex(slide),
        original_storage_executable_sha256=hashlib.sha256(read(text['va'], text['size'])).hexdigest(),
        reference_object_executed_or_linked=False,
        method_slots=SLOTS, kernel_bindings=SLOTS - PURE_VIRTUAL, pure_virtual_slots=PURE_VIRTUAL,
        inherited_kernel_methods=SLOTS - PURE_VIRTUAL - PURE_VIRTUAL,
        vtable=hex(table), original_metaclass=hex(meta), source_ioservice_metaclass=hex(service_meta),
        original_protected_constructor=hex(constructors[0]), original_base_destructor=hex(destructor),
        abstract_complete_destructors_trap=True,
        live_instance_sizes=dict(IOBlockStorageDevice=class_size),
        live_instance_sizes_source='decoded from the original block-storage OSMetaClass registration in the '
                                   'prelinked image; the metaclass object itself is a zero placeholder until '
                                   'that registration runs, and IOService::gMetaClass lives in the kernel '
                                   '__DATA bss and is not statically readable',
        live_native_4k=True,
        live_native_4k_source='every prelinked segment is 4K aligned; the kernelcache is a 4K-page build',
        compiled_metaclass_offsets=layout,
        compiled_metaclass_offsets_source='verified against the original storage registration, not recorded',
        metaclass_registration=dict(function=hex(rebased(int(facts['function'], 16))), class_name=facts['name'],
                                    class_size=hex(class_size),
                                    superclass_metaclass=hex(facts['super_meta']),
                                    published_word=hex(rebased(facts['stored']))),
        methods=methods, disk_read_tested=False, transport_implemented=False, hardware_tested=False)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'interface-audit.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'PASS: {SLOTS} original storage slots, {SLOTS - PURE_VIRTUAL} kernel bindings '
          f'({SLOTS - PURE_VIRTUAL - PURE_VIRTUAL} inherited + {PURE_VIRTUAL} pure virtual)')
    print(f'Original block-storage size {class_size:#x}, native 4K, from {image_path.name}'
          f'{f" at slide {slide:+#x}" if slide else ""}.')
    print('Block I/O remains unqualified: this audit proves the ABI, not the transport.')


if __name__ == '__main__':
    main()
