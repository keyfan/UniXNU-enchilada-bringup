#!/usr/bin/env python3
"""Build and run the host test of the enchilada stage-5 (GPU) A630
identification / feasibility study. The catalog is pure byte manipulation of
Linux v6.12 a6xx_catalog.c / a6xx_gmu.c + the vendor DTB facts, so it is
compiled as C11 and asserted from C++17 with the address and undefined-behaviour
sanitizers when available. No hardware, no IOKit, no firmware, no kernel linkage:
this is a HOST-VERIFIED decision record, not a GPU driver.

  scripts/check-enchilada-adreno-host.py [--cc clang] [--cxx clang++] [--out DIR]
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GPU = ROOT / 'drivers/NativeAdreno'
TESTS = [
    ('adreno-catalog-test', [GPU / 'tests/adreno-catalog-test.cpp'], ['AdrenoCatalog']),
    ('adreno-a630-hwinit-test', [GPU / 'tests/adreno-a630-hwinit-test.cpp'], []),
    ('adreno-board-inputs-test', [GPU / 'tests/adreno-board-inputs-test.cpp'], ['AdrenoBoardInputs']),
    ('adreno-power-gate-test', [GPU / 'tests/adreno-power-gate-test.cpp'], ['AdrenoPowerGate']),
    ('adreno-domain-state-test', [GPU / 'tests/adreno-domain-state-test.cpp'], ['AdrenoDomainState']),
    ('adreno-hfi-v1-test', [GPU / 'tests/adreno-hfi-v1-test.cpp'], ['AdrenoHfiV1']),
    ('adreno-firmware-v1-test', [GPU / 'tests/adreno-firmware-v1-test.cpp'], ['AdrenoFirmwareV1']),
    ('adreno-gmu-boot-v1-test', [GPU / 'tests/adreno-gmu-boot-v1-test.cpp'], ['AdrenoGmuBootV1']),
    ('adreno-gmu-bus-v1-test', [GPU / 'tests/adreno-gmu-bus-v1-test.cpp'], ['AdrenoGmuBusV1', 'AdrenoGmuBootV1']),
    ('adreno-gmu-caller-v1-test', [GPU / 'tests/adreno-gmu-caller-v1-test.cpp'], ['AdrenoGmuCallerV1', 'AdrenoGmuBusV1', 'AdrenoGmuBootV1', 'AdrenoDomainState']),
    ('adreno-power-up-v1-test', [GPU / 'tests/adreno-power-up-v1-test.cpp'], ['AdrenoPowerUpV1', 'AdrenoGmuCallerV1', 'AdrenoGmuBusV1', 'AdrenoGmuBootV1', 'AdrenoDomainState']),
    ('adreno-kgsl-smmu-gate-test', [GPU / 'tests/adreno-kgsl-smmu-gate-test.cpp'], ['AdrenoKGSLSmmuGate', 'AdrenoBoardInputs', 'AdrenoDomainState']),
    ('adreno-gdsc-decode-test', [GPU / 'tests/adreno-gdsc-decode-test.cpp'], ['AdrenoGdscDecode', 'AdrenoDomainState']),
    ('adreno-power-up-gdsc-test', [GPU / 'tests/adreno-power-up-gdsc-test.cpp'], ['AdrenoPowerUpGdsc', 'AdrenoPowerUpV1', 'AdrenoGdscDecode', 'AdrenoDomainState']),
    ('adreno-power-down-v1-test', [GPU / 'tests/adreno-power-down-v1-test.cpp'], ['AdrenoPowerDownV1', 'AdrenoPowerUpV1', 'AdrenoPowerUpGdsc', 'AdrenoGdscDecode', 'AdrenoDomainState']),
    ('adreno-clk-ctl-v1-test', [GPU / 'tests/adreno-clk-ctl-v1-test.cpp'], ['AdrenoClkCtlV1']),
    ('adreno-power-up-backend-v1-test', [GPU / 'tests/adreno-power-up-backend-v1-test.cpp'], ['AdrenoPowerUpBackendV1', 'AdrenoPowerUpV1', 'AdrenoPowerUpGdsc', 'AdrenoGdscDecode', 'AdrenoClkCtlV1', 'AdrenoDomainState']),
    ('adreno-gpucc-window-v1-test', [GPU / 'tests/adreno-gpucc-window-v1-test.cpp'], ['AdrenoGpuccWindowV1', 'AdrenoPowerUpBackendV1', 'AdrenoPowerUpV1', 'AdrenoPowerUpGdsc', 'AdrenoGdscDecode', 'AdrenoClkCtlV1', 'AdrenoDomainState', 'AdrenoGmuCallerV1', 'AdrenoGmuBootV1', 'AdrenoGmuBusV1', 'AdrenoBoardInputs', 'AdrenoPowerGate', 'AdrenoHfiV1']),
    ('adreno-caller-v1-test', [GPU / 'tests/adreno-caller-v1-test.cpp'], ['AdrenoCallerV1', 'AdrenoGpuccWindowV1', 'AdrenoPowerUpBackendV1', 'AdrenoPowerUpV1', 'AdrenoPowerUpGdsc', 'AdrenoGdscDecode', 'AdrenoClkCtlV1', 'AdrenoDomainState', 'AdrenoGmuCallerV1', 'AdrenoGmuBootV1', 'AdrenoGmuBusV1', 'AdrenoBoardInputs', 'AdrenoPowerGate', 'AdrenoHfiV1']),
    ('adreno-metal-seam-v1-test', [GPU / 'tests/adreno-metal-seam-v1-test.cpp'], ['AdrenoMetalSeamV1']),
    ('adreno-darwin-kmd-abi-v1-test', [GPU / 'tests/adreno-darwin-kmd-abi-v1-test.cpp'], ['AdrenoDarwinKmdAbiV1']),
    ('adreno-metal-publish-v1-test', [GPU / 'tests/adreno-metal-publish-v1-test.cpp'], ['AdrenoMetalPublishV1']),
    ('adreno-gen1-backend-v1-test', [GPU / 'tests/adreno-gen1-backend-v1-test.cpp'], ['AdrenoGen1BackendV1']),
]
SOURCES = {
    'AdrenoCatalog': GPU / 'AdrenoCatalog.c',
    'AdrenoBoardInputs': GPU / 'AdrenoBoardInputs.c',
    'AdrenoPowerGate': GPU / 'AdrenoPowerGate.c',
    'AdrenoDomainState': GPU / 'AdrenoDomainState.c',
    'AdrenoHfiV1': GPU / 'AdrenoHfiV1.c',
    'AdrenoFirmwareV1': GPU / 'AdrenoFirmwareV1.c',
    'AdrenoGmuBootV1': GPU / 'AdrenoGmuBootV1.c',
    'AdrenoGmuBusV1': GPU / 'AdrenoGmuBusV1.c',
    'AdrenoGmuCallerV1': GPU / 'AdrenoGmuCallerV1.c',
    'AdrenoPowerUpV1': GPU / 'AdrenoPowerUpV1.c',
    'AdrenoKGSLSmmuGate': GPU / 'AdrenoKGSLSmmuGate.c',
    'AdrenoGdscDecode': GPU / 'AdrenoGdscDecode.c',
    'AdrenoPowerUpGdsc': GPU / 'AdrenoPowerUpGdsc.c',
    'AdrenoPowerDownV1': GPU / 'AdrenoPowerDownV1.c',
    'AdrenoClkCtlV1': GPU / 'AdrenoClkCtlV1.c',
    'AdrenoPowerUpBackendV1': GPU / 'AdrenoPowerUpBackendV1.c',
    'AdrenoGpuccWindowV1': GPU / 'AdrenoGpuccWindowV1.c',
    'AdrenoCallerV1': GPU / 'AdrenoCallerV1.c',
    'AdrenoMetalSeamV1': GPU / 'AdrenoMetalSeamV1.c',
    'AdrenoDarwinKmdAbiV1': GPU / 'AdrenoDarwinKmdAbiV1' / 'AdrenoDarwinKmdAbiV1.c',
    'AdrenoMetalPublishV1': GPU / 'AdrenoMetalPublishV1.c',
    'AdrenoGen1BackendV1': GPU / 'AdrenoGen1BackendV1.c',
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
    out = Path(args.out or tempfile.mkdtemp(prefix='enchilada-adreno-host-'))
    out.mkdir(parents=True, exist_ok=True)
    san = sanitizers(args.cxx, out)
    common = ['-g', '-O1', '-Wall', '-Wextra', '-Werror', *san, '-I', str(GPU)]
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
    print(f'PASS: enchilada A630 catalog/hwinit/board-input/power-gate/domain-state/hfi-v1/firmware-v1/gmu-boot-v1/gmu-bus-v1/gmu-caller-v1/power-up-v1/kgsl-smmu-gate/gdsc-decode/power-up-gdsc/power-down-v1/clk-ctl-v1/power-up-backend-v1/gpucc-window-v1/caller-v1/metal-seam-v1/darwin-kmd-abi-v1/metal-publish-v1/gen1-backend-v1 host tests ({"sanitized" if san else "without sanitizers"})')


if __name__ == '__main__':
    main()
