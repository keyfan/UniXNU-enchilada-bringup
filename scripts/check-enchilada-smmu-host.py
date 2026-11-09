#!/usr/bin/env python3
"""Build and run the host test of the enchilada stage-1 READ-ONLY appSMMU
survey. One pure-C core is compiled as C11 and asserted from C++17 with the
address and undefined-behaviour sanitizers when available:

  * SmmuSurvey - reads the SMMUv2 identification registers, derives the slot /
                 bank / global-page geometry, refuses anything it has not seen
                 on this device (64K padding, zero banks, a window that cannot
                 hold page tables), then scans every stream-match slot to
                 report which entries are valid, what they map, and where the
                 UFS (0x60) and DWC3 (0x740) streams landed.

The fake register file in the test is driven only through the public read32
callback, so the test also proves the survey performs no writes and reads no
S2CR for a free slot. This is a HOST-VERIFIED observation core: it does not
program a context, does not enable a bank, and cannot recover a bus abort.

  scripts/check-enchilada-smmu-host.py [--cc clang] [--cxx clang++] [--out DIR]
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SMMU = ROOT / 'drivers/NativeSmmu'
TESTS = [
    ('smmu-survey-test', [SMMU / 'tests/smmu-survey-test.cpp'], ['SmmuSurvey']),
]
SOURCES = {
    'SmmuSurvey': SMMU / 'SmmuSurvey.c',
}


def sanitizers(cxx, out):
    probe = out / 'probe.cpp'
    probe.write_text('int main() { return 0; }\n')
    flags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    ok = subprocess.run([cxx, *flags, str(probe), '-o', str(out / 'probe')], capture_output=True).returncode == 0
    return flags if ok else []


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cc', default=shutil.which('clang') or 'cc')
    parser.add_argument('--cxx', default=shutil.which('clang++') or 'c++')
    parser.add_argument('--out')
    args = parser.parse_args()
    out = Path(args.out or tempfile.mkdtemp(prefix='enchilada-smmu-host-'))
    out.mkdir(parents=True, exist_ok=True)
    san = sanitizers(args.cxx, out)
    common = ['-g', '-O1', '-Wall', '-Wextra', '-Werror', *san, '-I', str(SMMU)]
    objects = {}
    for name, source in SOURCES.items():
        objects[name] = out / f'{name}.o'
        subprocess.run([args.cc, '-std=c11', *common, '-c', str(source), '-o', str(objects[name])], check=True)
    for name, sources, libs in TESTS:
        binary = out / name
        subprocess.run([args.cxx, '-std=c++17', *common, *map(str, sources),
                        *(str(objects[lib]) for lib in libs), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        if result.returncode:
            sys.stderr.write(result.stderr)
            raise SystemExit(f'FAIL: {name} ({result.returncode})')
    print(f'PASS: enchilada appSMMU read-only survey host tests ({"sanitized" if san else "without sanitizers"})')


if __name__ == '__main__':
    main()
