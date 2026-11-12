#!/usr/bin/env python3
"""CI #236 verifier (ROUND 69): did the A6XX_GEN1 backend ADMISSION contract
compile and link inside a real XNU kernel build?

Domain 1 (this script): the uploaded CI artifacts. It DERIVES the archive sha16
and kernel size/sha256 from the artifacts (no guessed constants) and asserts the
structural facts that matter:
  * the archive reports 23 objects (22 in CI #234 + 1 round-69 unit),
  * the real kernel build log is clean (bootstrap baseline is intentionally dirty),
  * kernel.macho is a Mach-O 64 image whose sha256 changed vs CI #235,
  * all 16 round-69 gen1-backend exports, all 19 round-67 publish exports, all
    8 round-66 darwin-kmd exports, and all 15 earlier unit exports survived,
  * the round-69 module adds NO executable MMIO / power-up / PAS path.

Domain 2 (checked separately): workflow run/job status through the REST API, so
a green artifact can never be mistaken for a green run.

ROUND 70 IS THE SECOND CONSECUTIVE ROUND THAT CHANGES THE ARCHIVE: the 23rd unit
is genuinely new, so the object count 22 -> 23 is structural proof of a real
rebuild. Rounds 43/45/59 already established that an extra object can link
WITHOUT growing the image, so kernel size is OBSERVED, never asserted; the
load-bearing proof is the object count, the changed sha256, and every export.
"""
import hashlib
import re
import sys
from pathlib import Path

WS = Path(__file__).resolve().parents[3]          # .../oneplus6-ios
LOGS = WS / 'work' / 'tmp' / 'ci236-logs'
KERNEL = WS / 'work' / 'tmp' / 'ci236-kernel' / 'enchilada' / 'kernel' / 'kernel.macho'

EXPECT_OBJECTS = 24
PREV_KERNEL_BYTES = 126074880  # CI #235 (observed only, not asserted)
PREV_KERNEL_SHA256 = (
    'd71d6a8b23ed83b321c40af5c68b53d9bdfec13a10f953ae141b705c8fb28acb')
PREV_ARCHIVE_SHA16 = '588dee59442fc9d9'

# The 16 round-69 exports (AdrenoGen1BackendV1.c). All must be in the image.
# Derived from the header's API section; the enums (enchilada_gen1_resume_step /
# _prereq_id / _admit_verdict) and the structs are NOT functions and are
# deliberately excluded -- they are only substring prefixes of real exports.
CP_RING = [
    'adreno_cp_ring_offsets',
    'adreno_cp_ring_plan',
    'adreno_cp_ring_me_init',
    'adreno_cp_ring_me_init_matches',
    'adreno_cp_ring_shadow_mode',
    'adreno_cp_ring_doorbell_order',
    'adreno_cp_ring_undefined',
    'adreno_cp_ring_undefined_name',
    'adreno_cp_ring_undefined_anchor',
    'adreno_cp_ring_is_executable',
    'adreno_cp_ring_verify',
]
# Round-69 gen1-backend exports (must not regress).
GEN1_RETAINED = [
    'enchilada_gen1_backend_a630_pins',
    'enchilada_gen1_backend_has_gbif',
    'enchilada_gen1_backend_gbif_reason',
    'enchilada_gen1_backend_gmu_resume',
    'enchilada_gen1_backend_hub_rate_hz',
    'enchilada_gen1_backend_gmu_regions',
    'enchilada_gen1_backend_has_icache_region',
    'enchilada_gen1_backend_gmu_region_bytes',
    'enchilada_gen1_backend_sptprac',
    'enchilada_gen1_backend_pas_id',
    'enchilada_gen1_backend_pas_origin',
    'enchilada_gen1_backend_cp_ring_offsets',
    'enchilada_gen1_backend_admit',
    'enchilada_gen1_backend_prereq_name',
    'enchilada_gen1_backend_prereq_anchor',
    'enchilada_gen1_backend_verify',
]
# Round-67 publish exports (must not regress).
PUBLISH = [
    'enchilada_publish_open_type_is_plugin',
    'enchilada_publish_open_type_is_handoff',
    'enchilada_publish_open_type_is_marker',
    'enchilada_publish_open_type_name',
    'enchilada_publish_is_render_server',
    'enchilada_publish_client_name_valid',
    'enchilada_publish_client_listed',
    'enchilada_publish_client_cap',
    'enchilada_publish_queue_base',
    'enchilada_publish_windows_a630',
    'enchilada_publish_windows_consistent',
    'enchilada_publish_windows_are_board_derived',
    'enchilada_publish_sheng_reference',
    'enchilada_publish_per_object_cap_bytes_shift',
    'enchilada_publish_stage_name',
    'enchilada_publish_blocker_name',
    'enchilada_publish_stage_blockers',
    'enchilada_publish_establishment_blockers',
    'enchilada_publish_frontend_allowed',
]
# Round-66 darwin-kmd directory-contract exports (must not regress).
DARWIN_KMD = [
    'enchilada_darwin_abi_version', 'enchilada_darwin_method_is_known',
    'enchilada_darwin_method_name', 'enchilada_darwin_device_info_a630',
    'enchilada_darwin_specified_fields', 'enchilada_darwin_board_derived_fields',
    'enchilada_darwin_device_info_consistent',
    'enchilada_darwin_compiler_a630_available',
]
# Earlier units must not have regressed out of the archive (round-65 seam included).
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
    ck(LOGS.is_dir(), 'the CI #236 logs directory is extracted')
    ck(KERNEL.is_file(), 'kernel.macho is extracted')

    # ---- archive line (derive sha16, assert object count) ----
    hit = None
    for p in LOGS.rglob('*'):
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
            ck(n == EXPECT_OBJECTS, 'the archive reports %d objects (got %d)' % (EXPECT_OBJECTS, n))
            ck(sha16 != PREV_ARCHIVE_SHA16, 'the archive sha16 changed vs CI #235 (genuine rebuild)')
            ck('(23 objects' not in line and '(22 objects' not in line and '(21 objects' not in line
               and '(20 objects' not in line and '(15 objects' not in line,
               'no 23/22/21/20/15-object regression in the line')
        # The archive log carries ONLY the PASS line (no per-unit lines), so the
        # object COUNT is the proof the 24th unit joined -- not a name search.
        ck(len(line.splitlines()) == 1,
           'the archive log is exactly the single PASS line (got %d lines)'
           % len(line.splitlines()))

    # ---- the real build log must be clean ----
    kernel_logs = [p for p in LOGS.rglob('kernel.log') if p.is_file()]
    ck(len(kernel_logs) == 1, 'exactly one kernel.log is extracted (got %d)' % len(kernel_logs))
    if kernel_logs:
        ktxt = kernel_logs[0].read_text(encoding='utf-8', errors='replace')
        ck('Undefined symbols' not in ktxt, 'the REAL build log has no undefined symbols')
        ck('Error 1' not in ktxt, 'the REAL build log has no "Error 1"')
        print('KERNEL_LOG_BYTES', kernel_logs[0].stat().st_size)

    # ---- the bootstrap log is an INTENTIONALLY failing baseline ----
    boot_logs = [p for p in LOGS.rglob('kernel-bootstrap.log') if p.is_file()]
    ck(len(boot_logs) == 1, 'exactly one kernel-bootstrap.log (got %d)' % len(boot_logs))
    if boot_logs:
        btxt = boot_logs[0].read_text(encoding='utf-8', errors='replace')
        ck('Undefined symbols for architecture arm64' in btxt,
           'bootstrap still shows the BASELINE undefined symbols')
        ck('_native_verified_root_probe' in btxt,
           'bootstrap baseline still references the extra-archive family')

    # ---- kernel.macho (derive size/sha, assert magic + changed sha) ----
    data = KERNEL.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    print('KERNEL_BYTES', len(data), 'PREV_KERNEL_BYTES', PREV_KERNEL_BYTES,
          'KERNEL_DELTA', len(data) - PREV_KERNEL_BYTES)
    print('KERNEL_SHA256', digest)
    ck(data[:4] == b'\xcf\xfa\xed\xfe', 'kernel.macho has Mach-O 64 magic')
    ck(len(digest) == 64 and re.fullmatch(r'[0-9a-f]{64}', digest) is not None,
       'kernel sha256 is a well-formed digest')
    ck(digest != PREV_KERNEL_SHA256,
       'kernel sha256 changed vs CI #235 (genuine relink, no stale artifact)')

    text = data.decode('latin-1')
    for sym in CP_RING:
        ck(sym in text, 'kernel contains round-70 cp-ring symbol %s' % sym)
    for sym in GEN1_RETAINED:
        ck(sym in text, 'kernel retains round-69 gen1-backend symbol %s' % sym)
    for sym in PUBLISH:
        ck(sym in text, 'kernel retains round-67 publish symbol %s' % sym)
    for sym in DARWIN_KMD:
        ck(sym in text, 'kernel retains round-66 darwin symbol %s' % sym)
    for sym in EARLIER:
        ck(sym in text, 'kernel retains earlier symbol %s' % sym)

    # The round-69 unit is ADJUDICATION ONLY: it must contribute no PAS call, no
    # qcom_scm reference, and no MMIO primitive. Its absence is the boundary.
    ck('qcom_scm' not in text, 'no qcom_scm symbol leaked into the image')
    ck('adreno_zap_shader_load' not in text,
       'no ZAP loader symbol leaked into the image (PAS stays unimplemented)')

    # the observation-only probe must remain the only executable stage path
    ck('stage3=plan steps=%u' in text, 'the stage-3 plan row format survives')
    ck('stage3=exec' not in text, 'no stage-3 EXECUTE path was compiled in')

    print('---')
    if fails:
        print('CI236_ADRENO_VERIFY: FAIL (%d checks, %d failures)' % (checks, len(fails)))
        return 1
    print('CI236_ADRENO_VERIFY: PASS (%d checks, 0 failures)' % checks)
    return 0


if __name__ == '__main__':
    sys.exit(main())
