#!/usr/bin/env python3
"""Build and run the host test of the enchilada MDSS DSI0 command-channel transport.
The transmit path is pure C over an injected register bus (Linux v6.12
drivers/gpu/drm/msm/dsi/dsi_host.c + registers/display/dsi.xml rules), so it is
compiled as C11 and asserted from C++17 with the address and undefined-behaviour
sanitizers when the compiler has them. No hardware or IOKit is involved: this is
stage-3 layer 2, locally verifiable before the runtime IOService (layer 3).

  scripts/check-enchilada-msm-dsi-host.py [--cc clang] [--cxx clang++] [--out DIR]
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DSI = ROOT / 'drivers/NativeMsmDsi'
DCS = ROOT / 'drivers/NativeDsiBacklight'
TESTS = [
    ('msm-dsi-cmd-test', [DSI / 'tests/msm-dsi-cmd-test.cpp'], ['MsmDsiCmd', 'DsiDcs']),
    ('msm-dsi-session-test', [DSI / 'tests/msm-dsi-session-test.cpp'], ['MsmDsiSession', 'MsmDsiCmd', 'DsiDcs']),
]
SOURCES = {
    'MsmDsiCmd': DSI / 'MsmDsiCmd.c',
    'MsmDsiSession': DSI / 'MsmDsiSession.c',
    'DsiDcs': DCS / 'DsiDcs.c',
}
INCLUDES = ['-I', str(DSI), '-I', str(DCS)]


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
    out = Path(args.out or tempfile.mkdtemp(prefix='enchilada-msm-dsi-host-'))
    out.mkdir(parents=True, exist_ok=True)
    san = sanitizers(args.cxx, out)
    common = ['-g', '-O1', '-Wall', '-Wextra', '-Werror', *san, *INCLUDES]
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
    print(f'PASS: enchilada MDSS DSI0 command-channel + read-only session host tests ({"sanitized" if san else "without sanitizers"})')


if __name__ == '__main__':
    main()
