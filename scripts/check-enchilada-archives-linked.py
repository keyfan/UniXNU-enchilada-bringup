#!/usr/bin/env python3
"""Guard: every enchilada-specific runtime archive that board.toml builds must also be
force-loaded by configs/enchilada_defconfig, or its objects (and any strong symbols such
as a future native_backlight_stats[]) are silently dropped by the static kernel link.

Why this guard exists
---------------------
board.toml [kernel] archives only decides the BUILD ORDER: work/UniXNU/scripts/unixnu/
kernel.py archive_plan() returns each entry as a ('script', entry) pair, and build_archives()
just runs `python3 <script>`. It does NOT add -force_load. The kernel link
(work/UniXNU/scripts/build-xnu-4k-baseline.sh:755) appends ${XNU_EXTRA_LD_ARCHIVES} to
KERNEL_EXTRA_ARCHIVES, and kernel.py xnu_env() maps CONFIG_XNU_EXTRA_LD_ARCHIVES ->
XNU_EXTRA_LD_ARCHIVES while substituting {tree} with the build tree path. Weak references
do not pull archive members, so a board archive that is built but omitted from this defconfig
line is dead code (CI-compiled, never linked). This is exactly how the stage-3 layer-2
libEnchiladaMsmDsi.a was left unlinked until this fix.

Intentional exceptions (linked by their own config key / the baseline kernel script, NOT
by -force_load in this line):
  * build-enchilada-storage.py        -> CONFIG_XNU_NATIVE_VIRTIO_BLOCK_ARCHIVE
  * build-enchilada-verified-root.py  -> CONFIG_XNU_VERIFIED_ROOT_ARCHIVE
  * build-boot-progress.py (libBootProgress.a) and build-native-legacy-display.py
    (libNativeLegacyDisplay.late.a)  -> build-xnu-4k-baseline.sh:752 and
    CONFIG_XNU_NATIVE_LEGACY_DISPLAY respectively.
  * build-arm-driver.py / build-corecrypto-14.py / build-mockfs-fixture.py / etc. are
    pulled in by strong symbol references or their own config keys.

The enchilada-specific default-off probe/transport archives carry no such strong external
reference; -force_load is the only thing that links them. Add any new
build-enchilada-*.py runtime archive to the defconfig line, or to EXEMPT_BUILDERS with a
documented reason. Run: python scripts/check-enchilada-archives-linked.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFCONFIG = ROOT / 'configs/enchilada_defconfig'
BOARD = ROOT / 'board.toml'

# Enchilada builders whose archives are linked through a dedicated config key, not -force_load.
EXEMPT_BUILDERS = {
    'build-enchilada-storage.py',        # CONFIG_XNU_NATIVE_VIRTIO_BLOCK_ARCHIVE
    'build-enchilada-verified-root.py',  # CONFIG_XNU_VERIFIED_ROOT_ARCHIVE
}


def main():
    text = DEFCONFIG.read_text()
    m = re.search(r'^CONFIG_XNU_EXTRA_LD_ARCHIVES="([^"]*)"', text, re.M)
    if not m:
        raise SystemExit('FAIL: CONFIG_XNU_EXTRA_LD_ARCHIVES not found in ' + str(DEFCONFIG))
    loads = re.findall(r'-Wl,-force_load,([^\s]+)', m.group(1))
    problems = []
    for p in loads:
        if not p.startswith('{tree}/'):
            problems.append('-force_load path is not {tree}-relative: ' + p)
        if not p.endswith('.a'):
            problems.append('-force_load target is not an archive: ' + p)
    duplicates = sorted({p for p in loads if loads.count(p) > 1})
    if duplicates:
        problems.append('duplicate -force_load entries: ' + ', '.join(duplicates))

    board = BOARD.read_text()
    builders = sorted(set(re.findall(r'"(build-enchilada-[^"/ ]+\.py)', board)))
    covered = 0
    for b in builders:
        if b in EXEMPT_BUILDERS:
            continue
        covered += 1
        directory = 'build/' + b[len('build-'):-len('.py')] + '/'
        if not any(('{tree}/' + directory) in p for p in loads):
            problems.append(f'board.toml builds {b} but CONFIG_XNU_EXTRA_LD_ARCHIVES '
                            f'has no -force_load for {directory}')

    if problems:
        for p in problems:
            print('FAIL:', p)
        raise SystemExit(1)
    print(f'PASS: {len(loads)} force-loaded archives, all {{tree}}-relative; '
          f'{covered} enchilada runtime builders covered')
    for p in sorted(loads):
        print('  -Wl,-force_load,' + p)


if __name__ == '__main__':
    main()
