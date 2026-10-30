#!/usr/bin/env python3
"""Build the trust cache bundle the payload hands to DarwinBoot and AMFI.

The bundle carries the two unchanged original Apple trust modules of the
target IPSW, byte for byte:

  module 0 = StaticTrustCache        (Firmware/044-76040-038.dmg.trustcache)
  module 1 = Cryptex1,SystemTrustCache (Firmware/044-76034-038.dmg.trustcache)

Both arrive wrapped in an IM4P container; the payload of that container is the
version-2 trust module itself. The concatenated bundle is what DarwinBoot
validates in ValidTrustCaches() (drivers/DarwinBoot/DarwinBootPkg/Driver/
DarwinBoot.c:277) and copies to the /chosen/memory-map/TrustCache placeholder.

Why the original modules and not a rebuilt one: the 21H461 system binaries are
ad-hoc signed without a ticket, so AMFI can only resolve them from a trust
cache. `sbin/launchd` (CDHash 1d52c69747dffc4b1937d241bdbc9cee3b22df18) and
`usr/lib/dyld` (CDHash b479f90e05545314cb843410c3d98ef84d926710) are both
already inside the static module, which is why the stock device needs no
first-boot signature work and why this bundle is enough.

board.toml [payload] trust_cache_sha256 is an allow-list, not a single pin: the
payload step accepts any bundle whose sha256 appears in it. This bundle is the
first entry,

  7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7

so the check below compares the result against that list rather than against a
hard-coded digest. (The second entry, 8900d6a9..., is the same System trust
cache in the wrapper the Cryptex stage uses; it is not reproducible by
concatenating these two modules, and the payload never needs it here.)

Usage (the components are fetched by `./unixnu ipsw fetch`):

  python scripts/make-enchilada-trust-bundle.py
  python scripts/make-enchilada-trust-bundle.py --check
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[0]
# Locally the UniXNU checkout is a sibling of this device repository; in CI the two are a checkout and its
# `device/` subdirectory, which --unixnu (or UNIXNU_ROOT) passes explicitly.
UNIXNU = Path(os.environ.get('UNIXNU_ROOT') or ROOT.parent / 'UniXNU')
CACHE = UNIXNU / 'out' / 'ipsw' / '21H461'

PIN_STATIC = 'da88460e6e513438d5307e5c526f76497a74754476a24da29f239f0c90120fc2'
PIN_SYSTEM = 'd121ba89167e03aafd00fa687c30cd8ebc4ccafd10c1408adb2ba3de12403651'

STATIC = '044-76040-038.dmg.trustcache'
SYSTEM = '044-76034-038.dmg.trustcache'

# The binaries init execs first. Both CDHashes are in the static module.
TRUSTED_NAMES = (
    ('launchd', '1d52c69747dffc4b1937d241bdbc9cee3b22df18'),
    ('dyld', 'b479f90e05545314cb843410c3d98ef84d926710'),
)


def der(data, at):
    """Read one DER TLV; return (tag, body_start, body_length)."""
    if at + 2 > len(data):
        raise SystemExit('truncated DER')
    tag, size = data[at], data[at + 1]
    at += 2
    if size & 0x80:
        count = size & 0x7F
        if not 0 < count <= 4 or at + count > len(data):
            raise SystemExit('invalid DER length')
        size = int.from_bytes(data[at:at + count], 'big')
        at += count
    if at + size > len(data):
        raise SystemExit('DER body runs past the input')
    return tag, at, size


def im4p_payload(data, name):
    """The fourth DER item of an IM4P is the payload (the trust module)."""
    tag, start, length = der(data, 0)
    if tag != 0x30:
        raise SystemExit(f'{name}: not a DER SEQUENCE')
    at, items = start, []
    while at < start + length:
        _, body, size = der(data, at)
        items.append(data[body:body + size])
        at = body + size
    if len(items) < 4 or items[0] != b'IM4P':
        raise SystemExit(f'{name}: not an IM4P container')
    return items[3], items[1].decode('ascii', 'replace')


def module(data, name, pin):
    """Validate one version-2 trust module and return its entries."""
    if len(data) < 24:
        raise SystemExit(f'{name}: shorter than a module header')
    version, = struct.unpack_from('<I', data, 0)
    if version != 2:
        raise SystemExit(f'{name}: trust module version {version}, expected 2')
    count, = struct.unpack_from('<I', data, 20)
    if len(data) != 24 + count * 24:
        raise SystemExit(f'{name}: {len(data)} bytes for {count} entries, expected {24 + count * 24}')
    digest = hashlib.sha256(data).hexdigest()
    if pin and digest != pin:
        raise SystemExit(f'{name}: sha256 {digest} is not the pinned {pin}')
    entries = [data[24 + 24 * i:24 + 24 * i + 20] for i in range(count)]
    for index, entry in enumerate(entries):
        kind = data[24 + 24 * index + 20]
        if not 1 <= kind <= 4:
            raise SystemExit(f'{name}: entry {index} has hash type {kind}')
        if index and entries[index - 1] >= entry:
            raise SystemExit(f'{name}: entries {index - 1} and {index} are not in ascending order')
    return entries, digest


def allowed_digests():
    """The board's allow-list, read from board.toml so the two cannot drift."""
    import tomllib
    board = ROOT / 'board.toml'
    with board.open('rb') as handle:
        config = tomllib.load(handle)
    allowed = config.get('payload', {}).get('trust_cache_sha256') or []
    if not allowed:
        raise SystemExit(f'{board} [payload] trust_cache_sha256 is empty; the payload would reject the bundle')
    return [digest.lower() for digest in allowed]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--unixnu', type=Path, default=None,
                        help='the UniXNU checkout whose out/ipsw cache holds the components '
                             '(default: $UNIXNU_ROOT or the sibling work/UniXNU)')
    parser.add_argument('--cache', type=Path, default=None, help=f'IPSW component cache (default: <unixnu>/out/ipsw/21H461)')
    parser.add_argument('--output', type=Path, default=None,
                        help='bundle to write (default: <unixnu>/out/enchilada-trust.bundle, the build tree '
                             'the payload step reads)')
    parser.add_argument('--check', action='store_true', help='verify the inputs and an existing bundle, write nothing')
    args = parser.parse_args()

    cache = args.cache or (Path(args.unixnu) / 'out' / 'ipsw' / '21H461' if args.unixnu else CACHE)
    output = args.output or (Path(args.unixnu) / 'out' / 'enchilada-trust.bundle' if args.unixnu
                             else CACHE.parents[1] / 'enchilada-trust.bundle')

    modules, names = [], []
    for file_name, pin in ((STATIC, PIN_STATIC), (SYSTEM, PIN_SYSTEM)):
        path = cache / file_name
        if not path.is_file():
            raise SystemExit(f'missing {path}; run "./unixnu ipsw fetch StaticTrustCache Cryptex1,SystemTrustCache"')
        body, kind = im4p_payload(path.read_bytes(), file_name)
        entries, digest = module(body, file_name, pin)
        modules.append(body)
        names.append((kind, file_name, len(body), digest, len(entries)))

    bundle = struct.pack('<3I', 2, 12, 12 + len(modules[0])) + b''.join(modules)
    digest = hashlib.sha256(bundle).hexdigest()
    allowed = allowed_digests()
    if digest not in allowed:
        raise SystemExit(f'bundle sha256 {digest} is not one of the board trust caches: {", ".join(allowed)}')

    static_entries = [modules[0][24 + 24 * i:24 + 24 * i + 20]
                      for i in range(struct.unpack_from('<I', modules[0], 20)[0])]
    present = {name: bytes.fromhex(cdhash) in static_entries for name, cdhash in TRUSTED_NAMES}
    missing = [name for name, found in present.items() if not found]
    if missing:
        raise SystemExit(f'the static module does not trust {", ".join(missing)}; the bundle cannot help init')

    report = {
        'modules': [dict(container=kind, member=file_name, bytes=size, sha256=sha, entries=count)
                    for kind, file_name, size, sha, count in names],
        'bundle_bytes': len(bundle),
        'bundle_sha256': digest,
        'allow_list': allowed,
        'trusted': {name: present[name] for name, _ in TRUSTED_NAMES},
        'structure': 'count=2, offset table, two version-2 modules (DarwinBoot.ValidTrustCaches)',
    }

    if args.check:
        if not output.is_file():
            raise SystemExit(f'--check needs an existing bundle at {output}')
        actual = hashlib.sha256(output.read_bytes()).hexdigest()
        if actual != digest:
            raise SystemExit(f'{output} is {actual}, the inputs build {digest}')
        print(json.dumps({**report, 'checked': str(output)}, indent=2))
        return

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(bundle)
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    print(f'\nwrote {output}')
    print('payload: ./unixnu build payload --trust-cache ' + str(output))


if __name__ == '__main__':
    main()
