#!/usr/bin/env python3
"""Build and run the host test of the enchilada DSI DCS backlight transport. The packet
builder is pure byte manipulation (Linux v6.12 drm_mipi_dsi.c / mipi_display.h rules), so
it is compiled as C11 and asserted from C++17 with the address and undefined-behaviour
sanitizers when the compiler has them. No hardware or IOKit is involved: this is stage-3
layer 1, locally verifiable before the MDSS DSI0 command-channel driver and the
AppleARMBacklight-facing IOService.

  scripts/check-enchilada-dsi-backlight-host.py [--cc clang] [--cxx clang++] [--out DIR]
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DSI = ROOT / 'drivers/NativeDsiBacklight'
TESTS = [
    ('dsi-dcs-test', [DSI / 'tests/dsi-dcs-test.cpp'], ['DsiDcs']),
]
SOURCES = {'DsiDcs': DSI / 'DsiDcs.c'}


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
    out = Path(args.out or tempfile.mkdtemp(prefix='enchilada-dsi-backlight-host-'))
    out.mkdir(parents=True, exist_ok=True)
    san = sanitizers(args.cxx, out)
    common = ['-g', '-O1', '-Wall', '-Wextra', '-Werror', *san, '-I', str(DSI)]
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
    print(f'PASS: enchilada DSI DCS backlight host tests ({"sanitized" if san else "without sanitizers"})')


if __name__ == '__main__':
    main()
