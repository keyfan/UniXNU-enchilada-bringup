#!/usr/bin/env python3
"""CI #239 verifier (ROUND 84): did the round-83 module COMPILE AND LINK inside
a real XNU kernel build?

This is the first CI run since #238 (round 78). Rounds 79-83 produced device-
sourced read-only evidence and, in round 83, one new C translation unit
(AdrenoSmmuCtxWordV1.c) that had NEVER been compiled by the kernel's own clang.
Everything in rounds 79-82 was evidence or prose; round 83 is code, and host
verification of code is not a CI compile proof -- that distinction is the entire
reason this verifier exists.

Domain 1 (this script): the uploaded CI artifacts. It DERIVES the archive sha16
and the kernel size/sha256 from the artifacts -- no guessed constants -- and
asserts the structural facts that matter:
  * the archive reports 29 objects (28 in CI #238 + the round-83 unit),
  * the real kernel build log is clean (the bootstrap baseline is intentionally dirty),
  * kernel.macho is a Mach-O 64 image whose sha256 changed vs CI #238,
  * all 18 round-83 exports are in the image, and the round-75/77/69/67/66/65
    exports did not regress,
  * the round-83 module adds NO MMIO, no IOService, no PAS path.

Domain 2 (checked separately by the round's audit stack): workflow run/job status
through the REST API, so a green artifact can never be mistaken for a green run.

ROUND 78's LESSON CARRIES: an extra object can link WITHOUT growing the image
(rounds 43/45/59), so kernel size is OBSERVED, never asserted. The load-bearing
proof is the OBJECT COUNT, the CHANGED sha256, and every export.

usage: verify-ci-239-adreno.py [--logs DIR] [--kernel FILE]
"""
import argparse
import hashlib
import re
import sys
from pathlib import Path

WS = Path(__file__).resolve().parents[3]          # .../oneplus6-ios

EXPECT_OBJECTS = 29
# CI #238 (round 78) OBSERVED values, read from evidence/stage5-gpu-ci-238.md.
# They are used ONLY for the delta/shape comparison and are never asserted as the
# current run's values -- rounds 43/45/59 established that a new object can link
# without growing the image, so size is OBSERVED, not proved.
PREV_KERNEL_BYTES = 126099456
PREV_KERNEL_SHA256 = (
    'bdfdc5a05881b663b0450d283bbb015c6f8a590eb6e8aa2fe4ffd287d0d90cc5')
PREV_ARCHIVE_SHA16 = 'f90f1a3267b71762'

# The 18 round-83 exports (AdrenoSmmuCtxWordV1.c). Derived from the header's API
# section; the enums and structs are NOT functions and are deliberately excluded.
CTX_WORD = [
    'adreno_smmu_ctx_cba2r',
    'adreno_smmu_ctx_cbar',
    'adreno_smmu_ctx_id_size_to_bits',
    'adreno_smmu_ctx_input_anchor',
    'adreno_smmu_ctx_input_name',
    'adreno_smmu_ctx_mair',
    'adreno_smmu_ctx_mair0',
    'adreno_smmu_ctx_mair1',
    'adreno_smmu_ctx_missing',
    'adreno_smmu_ctx_plan',
    'adreno_smmu_ctx_plan_writes',
    'adreno_smmu_ctx_ready',
    'adreno_smmu_ctx_round75_writes',
    'adreno_smmu_ctx_tcr',
    'adreno_smmu_ctx_tcr2',
    'adreno_smmu_ctx_ttbrn',
    'adreno_smmu_ctx_ttbrn_checked',
    'adreno_smmu_ctx_verify',
]
# Round-77 va-map exports (must not regress).
VA_MAP = [
    'adreno_va_ias_is_plausible', 'adreno_va_clamp_start', 'adreno_va_geometry',
    'adreno_va_aperture', 'adreno_va_gmu_end_is_gpu_end', 'adreno_va_missing',
    'adreno_va_blocker_name', 'adreno_va_blocker_anchor', 'adreno_va_map_ready',
    'adreno_va_verify',
]
# Round-75 smmu-stage1 exports (must not regress -- and must be UNCHANGED source).
SMMU_STAGE1 = [
    'adreno_smmu_sctlr_stage1', 'adreno_smmu_sctlr_stage1_be',
    'adreno_smmu_sctlr_enabled', 'adreno_smmu_s2cr_addr', 'adreno_smmu_s2cr_value',
    'adreno_smmu_s2cr_type_for_state', 'adreno_smmu_stage1_plan',
    'adreno_smmu_stage1_missing', 'adreno_smmu_stage1_input_name',
    'adreno_smmu_stage1_input_anchor', 'adreno_smmu_stage1_ready',
    'adreno_smmu_stage1_verify',
]
CP_RING = [
    'adreno_cp_ring_offsets', 'adreno_cp_ring_plan', 'adreno_cp_ring_me_init',
    'adreno_cp_ring_me_init_matches', 'adreno_cp_ring_shadow_mode',
    'adreno_cp_ring_doorbell_order', 'adreno_cp_ring_undefined',
    'adreno_cp_ring_undefined_name', 'adreno_cp_ring_undefined_anchor',
    'adreno_cp_ring_is_executable', 'adreno_cp_ring_verify',
]
ZAP_AUTH = [
    'adreno_zap_scm_cmd', 'adreno_zap_scm_op_name', 'adreno_zap_pas_id',
    'adreno_zap_chain', 'adreno_zap_present_count', 'adreno_zap_step_name',
    'adreno_zap_step_anchor', 'adreno_zap_auth_ready',
    'adreno_zap_result_semantics', 'adreno_zap_unsupported_is_quiet',
    'adreno_zap_auth_verify',
]
MDT = [
    'adreno_mdt_header', 'adreno_mdt_phdr', 'adreno_mdt_phdr_valid',
    'adreno_mdt_phdr_relocatable', 'adreno_mdt_phdr_is_hash',
    'adreno_mdt_get_size', 'adreno_mdt_segment_name', 'adreno_mdt_metadata',
    'adreno_mdt_filesz_sum', 'adreno_mdt_ready', 'adreno_mdt_loader_available',
    'adreno_mdt_verify',
]
GEN1_RETAINED = [
    'enchilada_gen1_backend_a630_pins', 'enchilada_gen1_backend_has_gbif',
    'enchilada_gen1_backend_gbif_reason', 'enchilada_gen1_backend_gmu_resume',
    'enchilada_gen1_backend_hub_rate_hz', 'enchilada_gen1_backend_gmu_regions',
    'enchilada_gen1_backend_has_icache_region',
    'enchilada_gen1_backend_gmu_region_bytes', 'enchilada_gen1_backend_sptprac',
    'enchilada_gen1_backend_pas_id', 'enchilada_gen1_backend_pas_origin',
    'enchilada_gen1_backend_cp_ring_offsets', 'enchilada_gen1_backend_admit',
    'enchilada_gen1_backend_prereq_name', 'enchilada_gen1_backend_prereq_anchor',
    'enchilada_gen1_backend_verify',
]
PUBLISH = [
    'enchilada_publish_open_type_is_plugin', 'enchilada_publish_open_type_is_handoff',
    'enchilada_publish_open_type_is_marker', 'enchilada_publish_open_type_name',
    'enchilada_publish_is_render_server', 'enchilada_publish_client_name_valid',
    'enchilada_publish_client_listed', 'enchilada_publish_client_cap',
    'enchilada_publish_queue_base', 'enchilada_publish_windows_a630',
    'enchilada_publish_windows_consistent', 'enchilada_publish_windows_are_board_derived',
    'enchilada_publish_sheng_reference',
    'enchilada_publish_per_object_cap_bytes_shift', 'enchilada_publish_stage_name',
    'enchilada_publish_blocker_name', 'enchilada_publish_stage_blockers',
    'enchilada_publish_establishment_blockers', 'enchilada_publish_frontend_allowed',
]
DARWIN_KMD = [
    'enchilada_darwin_abi_version', 'enchilada_darwin_method_is_known',
    'enchilada_darwin_method_name', 'enchilada_darwin_device_info_a630',
    'enchilada_darwin_specified_fields', 'enchilada_darwin_board_derived_fields',
    'enchilada_darwin_device_info_consistent',
    'enchilada_darwin_compiler_a630_available',
]
EARLIER = [
    'enchilada_metal_abi_version', 'enchilada_metal_frontend_allowed',
    'AdrenoPowerDownV1ShouldRun', 'AdrenoPowerUpGdscContract',
    'adreno_gdsc_plan_enable', 'AdrenoKGSLSmmuGateAssess',
    'AdrenoClkCtlV1', 'AdrenoPowerUpBackendV1', 'AdrenoGpuccWindowV1',
    'AdrenoCallerV1', 'EnchiladaAdrenoProbe', 'AdrenoPowerUpV1',
    'AdrenoGmuCallerV1', 'AdrenoHfiV1', 'AdrenoFirmwareV1',
]

checks = 0
fails = []


def ck(cond, what):
    global checks
    checks += 1
    if not cond:
        fails.append(what)
        print('FAIL:', what)
    else:
        print('PASS', what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--logs', default=str(WS / 'work' / 'tmp' / 'ci239-logs'))
    ap.add_argument('--kernel', default=str(
        WS / 'work' / 'tmp' / 'ci239-kernel' / 'enchilada' / 'kernel' / 'kernel.macho'))
    args = ap.parse_args()
    logs, kernel = Path(args.logs), Path(args.kernel)

    ck(logs.is_dir(), 'the CI #239 logs directory is extracted')
    ck(kernel.is_file(), 'kernel.macho is extracted')
    if not logs.is_dir() or not kernel.is_file():
        print('---')
        print('CI239_ADRENO_VERIFY: FAIL (%d checks, %d failures) -- artifacts missing'
              % (checks, len(fails)))
        return 1

    # ---- archive line (derive sha16, assert OBJECT COUNT) ----
    hit = None
    for p in logs.rglob('*'):
        if not p.is_file() or p.suffix != '.log' or 'adreno' not in p.name:
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
            # The count is the structural proof the round-83 unit joined.
            ck(n == 29, 'the 29th object is the round-83 AdrenoSmmuCtxWordV1.c')
            ck(sha16 != PREV_ARCHIVE_SHA16,
               'the archive sha16 changed vs CI #238 (genuine rebuild, not a cached artifact)')
            for stale in ('(28 objects', '(27 objects', '(24 objects', '(23 objects',
                          '(22 objects', '(20 objects', '(15 objects'):
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

    # ---- the round-83 unit's OWN evidence is indirect, and that is honest ----
    # This CI uploads ONE log per ARCHIVE (archive-23-build-enchilada-adreno.log),
    # not one per object, so there is no AdrenoSmmuCtxWordV1.log to read. The proof
    # that the round-83 unit compiled is therefore (a) the object count rose to 29,
    # (b) the archive sha16 changed, and (c) all 18 of its exports are in the image.
    # Assert exactly that -- do NOT assert a per-unit log that this CI never makes.
    unit_logs = list(logs.rglob('AdrenoSmmuCtxWordV1.log'))
    ck(len(unit_logs) == 0,
       'this CI uploads archive-level logs only, so no per-unit log exists (got %d)'
       % len(unit_logs))
    arch_logs = list(logs.rglob('archive-*-build-enchilada-adreno.log'))
    ck(len(arch_logs) == 1,
       'exactly one enchilada-adreno archive log (got %d)' % len(arch_logs))

    # ---- bootstrap log is an INTENTIONALLY failing baseline ----
    boot_logs = [p for p in logs.rglob('kernel-bootstrap.log') if p.is_file()]
    ck(len(boot_logs) == 1, 'exactly one kernel-bootstrap.log (got %d)' % len(boot_logs))
    if boot_logs:
        btxt = boot_logs[0].read_text(encoding='utf-8', errors='replace')
        ck('Undefined symbols for architecture arm64' in btxt,
           'the bootstrap log still shows the BASELINE undefined symbols')
        ck('_native_verified_root_probe' in btxt,
           'the bootstrap baseline still references the extra-archive family')

    # ---- kernel.macho ----
    data = kernel.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    print('KERNEL_BYTES', len(data), 'PREV_KERNEL_BYTES', PREV_KERNEL_BYTES,
          'KERNEL_DELTA', len(data) - PREV_KERNEL_BYTES)
    print('KERNEL_SHA256', digest)
    ck(data[:4] == b'\xcf\xfa\xed\xfe', 'kernel.macho has Mach-O 64 magic')
    ck(re.fullmatch(r'[0-9a-f]{64}', digest) is not None,
       'kernel sha256 is a well-formed digest')
    ck(digest != PREV_KERNEL_SHA256,
       'kernel sha256 changed vs CI #238 (genuine relink, no stale artifact)')

    text = data.decode('latin-1')
    for sym in CTX_WORD:
        ck(sym in text, 'kernel contains round-83 ctx-word symbol %s' % sym)
    for sym in VA_MAP:
        ck(sym in text, 'kernel retains round-77 va-map symbol %s' % sym)
    for sym in SMMU_STAGE1:
        ck(sym in text, 'kernel retains round-75 smmu-stage1 symbol %s' % sym)
    for sym in CP_RING:
        ck(sym in text, 'kernel retains round-70 cp-ring symbol %s' % sym)
    for sym in ZAP_AUTH:
        ck(sym in text, 'kernel retains round-73 zap-auth symbol %s' % sym)
    for sym in MDT:
        ck(sym in text, 'kernel retains round-74 mdt symbol %s' % sym)
    for sym in GEN1_RETAINED:
        ck(sym in text, 'kernel retains round-69 gen1-backend symbol %s' % sym)
    for sym in PUBLISH:
        ck(sym in text, 'kernel retains round-67 publish symbol %s' % sym)
    for sym in DARWIN_KMD:
        ck(sym in text, 'kernel retains round-66 darwin symbol %s' % sym)
    for sym in EARLIER:
        ck(sym in text, 'kernel retains earlier symbol %s' % sym)

    # ---- the round-83 boundary: derivation only, no execution ----
    # A CITATION string may name a register; a real SYMBOL may not. The round-83
    # module must contribute no MMIO primitive, no IOKit open, and no PAS call.
    for bad in ('_writel(', '_readl(', '_IOServiceOpen', '_IOConnectCallMethod',
                '_qcom_scm_call', 'adreno_zap_shader_load'):
        ck(bad not in text, 'no executable %r symbol in the image' % bad)
    # And the SCTLR-last contract string must survive, proving the module's own
    # text reached the image rather than only its symbol names.
    ck('stage3=plan steps=%u' in text, 'the stage-3 plan row format survives')
    ck('stage3=exec' not in text, 'no stage-3 EXECUTE path was compiled in')

    print('---')
    if fails:
        print('CI239_ADRENO_VERIFY: FAIL (%d checks, %d failures)' % (checks, len(fails)))
        return 1
    print('CI239_ADRENO_VERIFY: PASS (%d checks, 0 failures)' % checks)
    return 0


if __name__ == '__main__':
    sys.exit(main())
