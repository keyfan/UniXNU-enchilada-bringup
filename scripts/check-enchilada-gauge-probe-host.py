#!/usr/bin/env python3
"""Build and run the host tests of the enchilada stage-7 READ-ONLY bq27xxx
gauge probe. Two pure-C cores are compiled as C11 and asserted from C++17 with
the address and undefined-behaviour sanitizers when available:

  * BqFuelGauge   - the already host-verified byte-level decoder (two register
                    maps, valid/missing state, feasibility gate).
  * BqGaugeProbe  - the READ-ONLY orchestrator: read both register maps over an
                    abstract I2C read callback, score by physical plausibility,
                    select the chip, format a one-line verdict. NO I2C writes,
                    NO gauge CONTROL/CONFIG writes, NO data-flash writes.

The fake I2C slave in the test proves the read-only contract and that no single
register-map source is trusted (vendor bq27541 vs mainline bq27411 conflict is
resolved empirically by the device log). No IOKit, no MMIO, no firmware: this
is a HOST-VERIFIED decision core, not a battery driver.

  scripts/check-enchilada-gauge-probe-host.py [--cc clang] [--cxx clang++] [--out DIR]
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BAT = ROOT / 'drivers/NativeBattery'
TESTS = [
    ('bq-fuel-gauge-test', [BAT / 'tests/bq-fuel-gauge-test.cpp'], ['BqFuelGauge']),
    ('bq-gauge-probe-test', [BAT / 'tests/bq-gauge-probe-test.cpp'], ['BqGaugeProbe', 'BqFuelGauge']),
]
SOURCES = {
    'BqFuelGauge': BAT / 'BqFuelGauge.c',
    'BqGaugeProbe': BAT / 'BqGaugeProbe.c',
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
    out = Path(args.out or tempfile.mkdtemp(prefix='enchilada-gauge-probe-host-'))
    out.mkdir(parents=True, exist_ok=True)
    san = sanitizers(args.cxx, out)
    common = ['-g', '-O1', '-Wall', '-Wextra', '-Werror', *san, '-I', str(BAT)]
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
    print(f'PASS: enchilada bq27xxx read-only gauge probe host tests ({"sanitized" if san else "without sanitizers"})')


if __name__ == '__main__':
    main()
