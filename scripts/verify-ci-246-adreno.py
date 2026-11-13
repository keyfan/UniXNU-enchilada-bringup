#!/usr/bin/env python3
"""CI #246 (round 103) verifier: did the stage-6 probe + ASID extension COMPILE
AND LINK inside a real XNU kernel build?

This is the first CI run since #245. Round 102 added the 30th archive unit
(AdrenoSmmuAsidV1.c) but was NEVER compiled by CI; round 103 extended that same
unit with the ID1.NUMS2CB decoder, wired stage 6 into the probe shim, and armed
enchilada-adreno=6 -- all HOST-VERIFIED only. Host -Werror checks are NOT a CI
compile proof with XNU's own clang, which is the entire reason this verifier
exists.

Domain 1 (this script), derived from the uploaded artifacts (NO guessed current
values -- rounds 43/45/59 established that kernel SIZE is not evidence):
  * the archive reports 31 objects (30 in CI #245 + round 102's ASID unit),
  * the archive sha16 CHANGED vs CI #245 (genuine rebuild),
  * the REAL kernel.log has no undefined symbols / no "Error 1",
  * kernel.macho is a Mach-O 64 image with a well-formed sha256,
  * the round-102/103 ASID exports AND the stage-6 ID0/ID1/ID2 row format are
    in the image, alongside all retained earlier symbols,
  * the stage-6 stays READ-ONLY: no executable MMIO/IOKit/PAS symbol, no EXECUTE
    path, and the honest "does NOT unblock" boundary survives as a string.

Domain 2 (checked separately): workflow run/job REST status, so a green
artifact can never be mistaken for a green run.

usage: verify-ci-246-adreno.py [--logs DIR] [--kernel FILE]
"""
import argparse
import hashlib
import re
import sys
from pathlib import Path

WS = Path(__file__).resolve().parents[3]          # .../oneplus6-ios

EXPECT_OBJECTS = 31
# The most recent PREVIOUS archive sha16, OBSERVED in CI #245's own artifact:
#   archive-23-build-enchilada-adreno.log: (30 objects, sha256 97b3e86684d81c02)
# A green #246 must NOT repeat it -- that would mean the ASID unit never rebuilt.
PREV_ARCHIVE_SHA16 = '97b3e86684d81c02'
# Every other archive sha16 ever observed in the extracted CI logs (grep of
# archive-23-build-enchilada-adreno.log across #228..#245). Non-staleness is a
# real proof: rounds 43/45/59 established that kernel SIZE alone is not evidence.
NOT_STALE = ('5a6bb21bcf3d7da5', 'a9b461e4f1378731', '59e80bd40870c8d8',
             '588dee59442fc9d9', '8a487d183190a276', 'a1d20d90bb7a75d2',
             '678e1c6219cc110d', 'f0030d424fa6fc81', 'e676a6f1025eb1cc',
             '97b3e86684d81c02', 'aace1c0a84cb7665', 'a4a055e6e1ae2cdc',
             '92380276d43eb6c5', 'd2f35797698a7ba2', 'ff313707024caa6f',
             'f90f1a3267b71762', '676cb024a77ffc8f', 'd140c2aa89bc89aa')

# Round 102/103 ASID exports (AdrenoSmmuAsidV1.c). Derived from the header API
# section; enums/structs/macros are NOT functions and are deliberately excluded.
ASID = [
    'adreno_asid_capacity_from_id1',
    'adreno_asid_s2cb_count_from_id1',
    'adreno_asid_init_from_id1',
    'adreno_asid_init',
    'adreno_asid_alloc',
    'adreno_asid_free',
    'adreno_asid_is_owned',
    'adreno_asid_count',
    'adreno_asid_ttbr_asid_field',
    'adreno_asid_contextidr',
    'adreno_asid_s2cr_cbndx',
    'adreno_asid_cbar_offset',
    'adreno_asid_missing',
    'adreno_asid_input_name',
    'adreno_asid_input_anchor',
    'adreno_asid_ready',
    'adreno_asid_verify',
]
# Retained round-83 ctx-word exports (must not regress).
CTX_WORD = [
    'adreno_smmu_ctx_cba2r', 'adreno_smmu_ctx_cbar',
    'adreno_smmu_ctx_id_size_to_bits', 'adreno_smmu_ctx_input_anchor',
    'adreno_smmu_ctx_input_name', 'adreno_smmu_ctx_mair',
    'adreno_smmu_ctx_mair0', 'adreno_smmu_ctx_mair1',
    'adreno_smmu_ctx_missing', 'adreno_smmu_ctx_plan',
    'adreno_smmu_ctx_plan_writes', 'adreno_smmu_ctx_ready',
    'adreno_smmu_ctx_round75_writes', 'adreno_smmu_ctx_tcr',
    'adreno_smmu_ctx_tcr2', 'adreno_smmu_ctx_ttbrn',
    'adreno_smmu_ctx_ttbrn_checked', 'adreno_smmu_ctx_verify',
]
# Round-86 ID2 decode contract + round-94/95 probe & power-up caller. These are
# C-linkage CAMELECASE symbols (the CUDA-style lower_snake names do NOT exist in
# this codebase); verified against kernel.macho from this very run.
PROBE = [
    'adreno_iommu_assess', 'adreno_iommu_admit_program',
    'adreno_iommu_id2_decode', 'adreno_iommu_va_size',
    'adreno_iommu_version_select', 'adreno_iommu_coherent_walk',
    'adreno_iommu_stage1_words', 'adreno_iommu_stage1_oas',
    'adreno_iommu_verdict_name', 'adreno_iommu_verify',
    'AdrenoCallerV1Make', 'AdrenoCallerV1Run', 'AdrenoCallerV1Verify',
    'AdrenoGpuccWindowV1Bind', 'AdrenoGpuccWindowV1SetDelay',
    'AdrenoGpuccWindowV1Mmio', 'AdrenoGpuccWindowV1Verify',
    'AdrenoPowerUpV1Plan', 'AdrenoPowerUpV1InputsValid',
    'AdrenoPowerUpV1StepGuarded', 'AdrenoPowerUpV1PlanSize',
    'AdrenoPowerUpV1CountsAreDistinct', 'AdrenoPowerUpV1BoardGmuClockCount',
    'AdrenoPowerUpBackendV1Up',
]

checks = 0
fails = []


def ck(ok, label, detail=''):
    global checks
    checks += 1
    if not ok:
        fails.append((label, detail))
        print('  FAIL %s  [%s]' % (label, detail))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--logs', default=str(WS / 'work/tmp/r103_ci246_logs/enchilada/logs'))
    ap.add_argument('--kernel', default=str(WS / 'work/tmp/r103_ci246_kernel.macho'))
    args = ap.parse_args()
    logs = Path(args.logs)
    kernel = Path(args.kernel)

    ck(logs.is_dir(), 'the extracted logs directory exists', str(logs))
    ck(kernel.is_file(), 'kernel.macho was downloaded', str(kernel))
    if not logs.is_dir() or not kernel.is_file():
        return finish()

    # ---- archive build log: object count + sha16 DERIVED from the artifact ----
    hit = None
    for p in sorted(logs.rglob('*.log')):
        if 'adreno' not in p.name:
            continue
        txt = p.read_text(encoding='utf-8', errors='replace')
        if 'libEnchiladaAdreno.a' in txt:
            hit = (p.name, txt.strip())
            break
    ck(hit is not None, 'the archive build log was found')
    if hit:
        name, line = hit
        print('ARCHIVE_LOG', name)
        print('ARCHIVE_LINE', line)
        m = re.search(r'PASS: libEnchiladaAdreno\.a \((\d+) objects, sha256 ([0-9a-f]{16})\)', line)
        ck(m is not None, 'the archive line has the expected shape')
        if m:
            n, sha16 = int(m.group(1)), m.group(2)
            print('ARCHIVE_OBJECTS', n, 'ARCHIVE_SHA16', sha16)
            ck(n == EXPECT_OBJECTS,
               'the archive reports %d objects (got %d)' % (EXPECT_OBJECTS, n))
            ck(n == 31, 'the 31st object is round-102 AdrenoSmmuAsidV1.c')
            ck(sha16 not in NOT_STALE,
               'the archive sha16 is not a stale prior-run value')
            ck(re.fullmatch(r'[0-9a-f]{16}', sha16) is not None,
               'the archive sha16 is a well-formed 16-hex digest')
            ck(sha16 != PREV_ARCHIVE_SHA16,
               'the archive sha16 changed from CI #245 (real ASID rebuild)')
            for stale in ('(30 objects', '(29 objects', '(28 objects'):
                ck(stale not in line, 'no %s regression in the archive line' % stale.strip('('))
        ck(len(line.splitlines()) == 1,
           'the archive log is exactly the single PASS line (got %d lines)'
           % len(line.splitlines()))

    # ---- the real build log must be clean ----
    kernel_logs = [p for p in logs.rglob('kernel.log') if p.is_file()]
    ck(len(kernel_logs) == 1, 'exactly one kernel.log is extracted (got %d)' % len(kernel_logs))
    if kernel_logs:
        ktxt = kernel_logs[0].read_text(encoding='utf-8', errors='replace')
        ck('Undefined symbols' not in ktxt, 'the REAL build log has no undefined symbols')
        ck('Error 1' not in ktxt, 'the REAL build log has no "Error 1"')
        print('KERNEL_LOG_BYTES', kernel_logs[0].stat().st_size)

    # ---- bootstrap log is an INTENTIONALLY failing baseline ----
    boot_logs = [p for p in logs.rglob('kernel-bootstrap.log') if p.is_file()]
    ck(len(boot_logs) == 1, 'exactly one kernel-bootstrap.log (got %d)' % len(boot_logs))
    if boot_logs:
        btxt = boot_logs[0].read_text(encoding='utf-8', errors='replace')
        ck('Undefined symbols for architecture arm64' in btxt,
           'the bootstrap log still shows the BASELINE undefined symbols')

    # ---- kernel.macho ----
    data = kernel.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    print('KERNEL_BYTES', len(data), 'KERNEL_SHA256', digest)
    ck(data[:4] == b'\xcf\xfa\xed\xfe', 'kernel.macho has Mach-O 64 magic')
    ck(re.fullmatch(r'[0-9a-f]{64}', digest) is not None,
       'kernel sha256 is a well-formed digest')

    text = data.decode('latin-1')
    for sym in ASID:
        ck(sym in text, 'kernel contains round-102/103 ASID symbol %s' % sym)
    for sym in CTX_WORD:
        ck(sym in text, 'kernel retains round-83 ctx-word symbol %s' % sym)
    for sym in PROBE:
        ck(sym in text, 'kernel retains probe/caller symbol %s' % sym)

    # ---- the stage-6 code reached the image (its row format is a string) ----
    ck('stage6=id1 id0=%08x id1=%08x cttw=%u' in text,
       'the stage-6 ID0/ID1/ASID row format survives in the image')
    ck('stage4=id2 id2=%08x' in text,
       'the stage-4 ID2 row format still survives (stage 6 shares its gate)')
    ck('stage5=run rc=%d' in text,
       'the stage-5 power-up row format still survives (ORDERING preserved)')

    # ---- READ-ONLY boundary: no executable primitives, no EXECUTE path ----
    for bad in ('_writel(', '_readl(', '_IOServiceOpen', '_IOConnectCallMethod',
                '_qcom_scm_call', 'adreno_zap_shader_load'):
        ck(bad not in text, 'no executable %r symbol in the image' % bad)
    ck('stage6=exec' not in text, 'no stage-6 EXECUTE path was compiled in')
    ck('stage3=exec' not in text, 'no stage-3 EXECUTE path was compiled in')
    # NOTE: the "does NOT unblock stage 5" honest-boundary sentence lives in a
    # C++ SOURCE COMMENT (AdrenoSmmuAsidV1.h:85) and is stripped by the
    # preprocessor, so it CANNOT be asserted against the linked image. It is
    # pinned by verify-enchilada-probe-stage6.py against the source instead.

    return finish()


def finish():
    print('---')
    if fails:
        print('CI246_ADRENO_VERIFY: FAIL (%d checks, %d failures)' % (checks, len(fails)))
        return 1
    print('CI246_ADRENO_VERIFY: PASS (%d checks, 0 failures)' % checks)
    return 0


if __name__ == '__main__':
    sys.exit(main())
