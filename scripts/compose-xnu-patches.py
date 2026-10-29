#!/usr/bin/env python3
"""Compose the XNU patch directory the format 1 kernel build globs.

Why this exists
---------------
Format 1 applies `patches/xnu/*.patch` from the *assembled tree*, and the tree links
`patches/xnu` to exactly one module directory.  `unixnu` resolves a device module
before the UniXNU one, so a device repository that carries its own `patches/xnu/`
**replaces** UniXNU's shared series instead of adding to it.  Nothing fails: the
build applies whatever it finds, so a port with two board patches in that directory
builds a kernel with none of the shared patches and does not run on non-Apple
hardware.

The fix is to keep the board's own patches in `patches/xnu-device/` and let this
script write `patches/xnu/` as the union: every shared patch, byte for byte, plus
the board's.  Filename order decides application order, so the board patches are
named to land where they belong (`enchilada-` sorts early, `zzv-` late).

`--write` composes, `--check` verifies without writing.  The check runs in CI
before the build and in `scripts/run_checks.py`, so drift in either direction --
a shared patch updated upstream, or a board patch added to the wrong directory --
is reported instead of silently changing the kernel.
"""
import argparse
import hashlib
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEVICE_PATCHES = ROOT / 'patches/xnu-device'
COMPOSED = ROOT / 'patches/xnu'
UNIXNU = ROOT.parent / 'UniXNU'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def shared_patches(unixnu):
    directory = unixnu / 'patches/xnu'
    if not directory.is_dir():
        raise SystemExit(f'no shared patch directory: {directory}')
    return {path.name: path for path in sorted(directory.glob('*.patch'))}


def device_patches():
    if not DEVICE_PATCHES.is_dir():
        raise SystemExit(f'no board patch directory: {DEVICE_PATCHES}')
    return {path.name: path for path in sorted(DEVICE_PATCHES.glob('*.patch'))}


def compose(unixnu, write):
    shared = shared_patches(unixnu)
    board = device_patches()
    clash = sorted(set(shared) & set(board))
    if clash:
        raise SystemExit('a board patch has the same name as a shared one, so the shared '
                         'version would be replaced: ' + ', '.join(clash))
    expected = {**shared, **board}

    if write:
        if COMPOSED.exists():
            shutil.rmtree(COMPOSED)
        COMPOSED.mkdir(parents=True)
        for name in sorted(expected):
            shutil.copy2(expected[name], COMPOSED / name)

    problems = []
    if not COMPOSED.is_dir():
        problems.append(f'{COMPOSED} does not exist; run this script with --write')
        return expected, problems

    present = {path.name: path for path in sorted(COMPOSED.glob('*.patch'))}
    missing = sorted(set(expected) - set(present))
    extra = sorted(set(present) - set(expected))
    if missing:
        problems.append('{} patch(es) are not in patches/xnu: {}'.format(
            len(missing), ', '.join(missing[:8]) + (' ...' if len(missing) > 8 else '')))
    if extra:
        problems.append('{} patch(es) in patches/xnu are neither shared nor board patches: {}'.format(
            len(extra), ', '.join(extra[:8])))
    # A shared patch copied once and then edited here would drift from UniXNU without any
    # other symptom, so compare the bytes rather than the names.
    drifted = [name for name in sorted(shared)
               if name in present and digest(present[name]) != digest(shared[name])]
    if drifted:
        problems.append('{} shared patch(es) differ from the UniXNU copies: {}'.format(
            len(drifted), ', '.join(drifted[:8])))
    return expected, problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--unixnu', default=str(UNIXNU), help='UniXNU checkout with patches/xnu')
    parser.add_argument('--write', action='store_true', help='compose patches/xnu instead of only checking it')
    args = parser.parse_args()

    expected, problems = compose(Path(args.unixnu), args.write)
    shared = shared_patches(Path(args.unixnu))
    board = device_patches()
    print('xnu patch series: {} shared + {} board = {} patch(es) in patches/xnu'.format(
        len(shared), len(board), len(expected)))
    for name in sorted(board):
        print('  board:', name)
    for problem in problems:
        print('!!', problem)
    print('{} problem(s)'.format(len(problems)))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
