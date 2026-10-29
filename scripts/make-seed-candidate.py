"""Rebuild the prelink plan beside an already-prelinked kernel.

The archive builders run before `unixnu build prelink` has produced a kernel of this build,
so on a tree that has never been linked they have no reference candidate to audit their vtable
slots against.  A prelinked image from an earlier build carries the same original Apple
components at the same addresses (they all come from the pinned kernelcache), so its Mach-O can
stand in -- but every consumer also reads a `<candidate>.json` plan beside it (extents,
original_executable_sha256, components).  This rebuilds that plan from the image's own
_PrelinkInfoDictionary.

The bindings that name our own kernel's addresses are only correct for the kernel they were
read from, which is why the caller runs the kernel step twice: the first pass seeds the
candidate, the second re-derives every binding against the kernel this build actually produced.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'kits' / 'XnuPrelink'))
from native_prelink import backed_offset, macho, unserialize_xml  # noqa: E402


def prelink_entries(image, data):
    """The _PrelinkInfoDictionary the kernel lists its bundled kexts in."""
    for segment in image['segments']:
        if segment['name'] != '__PRELINK_INFO' or not segment['filesize']:
            continue
        at = backed_offset(image['segments'], segment['va'], segment['filesize'], len(data))
        fields = unserialize_xml(data[at:at + segment['filesize']])
        entries = fields.get('_PrelinkInfoDictionary') or []
        return [entries] if isinstance(entries, dict) else entries
    raise SystemExit('image has no __PRELINK_INFO dictionary')


def bundled_modules(image, data, entries):
    """Yield (identifier, macho image) for every bundled kext that has its own Mach-O."""
    for entry in entries:
        identifier = entry.get('CFBundleIdentifier')
        address = entry.get('_PrelinkExecutableLoadAddr') or 0
        size = entry.get('_PrelinkExecutableSize') or 0
        if not identifier or not address or not size:
            continue
        at = backed_offset(image['segments'], address, 32, len(data))
        try:
            module = macho(data, at)
        except ValueError:
            continue
        if module['kind'] == 11:
            yield identifier, module


def extraction(image, data, segment):
    """The bytes of one segment, resolved through the image's own segment table."""
    at = backed_offset(image['segments'], segment['va'], segment['filesize'], len(data))
    return data[at:at + segment['filesize']]


def build_plan(image, data):
    """Describe the image the way emit_prelink.py describes a fresh candidate."""
    components = []
    extents = []
    executables = {}
    for identifier, module in bundled_modules(image, data, prelink_entries(image, data)):
        components.append(identifier)
        for segment in module['segments']:
            if not segment['size']:
                continue
            extents.append({'module': identifier, 'segment': segment['name'],
                            'va': segment['va'], 'size': segment['size']})
            if segment['name'] == '__TEXT_EXEC' and segment['filesize']:
                executables[identifier] = hashlib.sha256(extraction(image, data, segment)).hexdigest()
    return {
        'components': sorted(components),
        'codeless_components': [],
        'kernel_interfaces': [],
        'extents': extents,
        'original_executable_sha256': executables,
        'candidate_sha256': hashlib.sha256(data).hexdigest(),
        'candidate_bytes': len(data),
        'source_inputs_modified': False,
        'runtime_qualified': False,
        'loadable': False,
        'synthesised_by': 'scripts/make-seed-candidate.py: plan rebuilt from __PRELINK_INFO',
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', required=True, type=Path,
                        help='prelinked kernel.macho to describe')
    parser.add_argument('--output', type=Path,
                        help='plan to write; defaults to <image>.json')
    parser.add_argument('--copy-image-to', type=Path,
                        help='also place the image under this name, keeping the plan beside it')
    args = parser.parse_args()

    data = args.image.read_bytes()
    image = macho(data)
    if image['kind'] != 2:
        raise SystemExit(f'{args.image}: kind {image["kind"]} is not a kernel executable')
    plan = build_plan(image, data)
    output = args.output or args.image.with_suffix('.json')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
    print(f'{output}: {len(plan["components"])} components, {len(plan["extents"])} extents, '
          f'{len(plan["original_executable_sha256"])} executable digests')
    if args.copy_image_to:
        args.copy_image_to.parent.mkdir(parents=True, exist_ok=True)
        args.copy_image_to.write_bytes(data)
        print(f'{args.copy_image_to}: {len(data)} bytes')


if __name__ == '__main__':
    main()
