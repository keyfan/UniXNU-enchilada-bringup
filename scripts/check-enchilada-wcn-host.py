#!/usr/bin/env python3
"""Build and run the host test of the enchilada stage-6 (network) WCN3990
identification / feasibility study. The catalog is pure byte manipulation of
Linux v6.12 ath10k snoc.c / hw.h / qmi.c + the vendor DTB + the official
network guide, so it is compiled as C11 and asserted from C++17 with the
address and undefined-behaviour sanitizers when available. No hardware, no
IOKit, no firmware, no kernel linkage: this is a HOST-VERIFIED decision
record, not a Wi-Fi driver.

  scripts/check-enchilada-wcn-host.py [--cc clang] [--cxx clang++] [--out DIR]
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WCN = ROOT / 'drivers/NativeWcn'
TESTS = [
    ('wcn-chip-test', [WCN / 'tests/wcn-chip-test.cpp'], ['WcnChip']),
]
SOURCES = {'WcnChip': WCN / 'WcnChip.c'}


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
    out = Path(args.out or tempfile.mkdtemp(prefix='enchilada-wcn-host-'))
    out.mkdir(parents=True, exist_ok=True)
    san = sanitizers(args.cxx, out)
    common = ['-g', '-O1', '-Wall', '-Wextra', '-Werror', *san, '-I', str(WCN)]
    objects = {}
    for name, source in SOURCES.items():
        objects[name] = out / f'{name}.o'
        subprocess.run([args.cc, '-std=c11', *common, '-c', str(source), '-o', str(objects[name])], check=True)
    for name, sources, libs in TESTS:
        binary = out / name
        subprocess.run([args.cxx, '-std=c++17', *common, *map(str, sources), *(str(objects[lib]) for lib in libs), '-o', str(binary)],
                       check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        if result.returncode:
            sys.stderr.write(result.stderr)
            raise SystemExit(f'FAIL: {name} ({result.returncode})')
    print(f'PASS: enchilada WCN3990 catalog host tests ({"sanitized" if san else "without sanitizers"})')


if __name__ == '__main__':
    main()
