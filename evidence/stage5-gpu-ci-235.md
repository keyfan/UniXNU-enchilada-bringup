# Stage 5 GPU - CI #235 (ROUND 69, CI-COMPILE-ONLY)

**Status: HOST-VERIFIED (CI compile/link only).** This is NOT a GPU driver, NOT a
device measurement. No device run was performed, no flash was performed, the
standing flash authorization (`m39510`) was NOT exercised, no MMIO was issued, and
`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area of `porting.toml` STAYS
`"host-verified"`. It does **not** unblock stage 5.

## Why this run, and why it settles a round-68 risk

Round 69 added the A6XX_GEN1 backend ADMISSION contract
(`drivers/NativeAdreno/AdrenoGen1BackendV1.{h,c}`) as the 23rd unit of
`libEnchiladaAdreno.a`, plus the 23rd host suite and a 230-check cross verifier.

Round 68 changed **no kernel source**, so CI #234 rebuilt the same 22 units and
had to lean on "archive sha16 changed" and "kernel sha256 changed" as its
genuine-relink proof — while its own doc honestly recorded a **0-byte** image
delta. That is a weak footing, and the round-68 mechanics audit flagged it: a
round that adds no unit cannot demonstrate a relink structurally.

Round 69 is the first round since #234 that changes the archive, so the proof is
structural again: the object count goes **22 -> 23**. Kernel size remains
OBSERVED, never asserted — rounds 43/45/59 already established that an extra
object can link without growing the image.

## Run (observed via the REST API)

| item | value |
| --- | --- |
| repository | `keyfan/unixnu-build` (workflow `kernel.yml`) |
| run | `43201433058` (#235), `workflow_dispatch` |
| created | `2026-11-12T10:33:35Z` |
| updated | `2026-11-12T10:53:40Z` |
| status / conclusion | `completed` / `success` |
| workflow head | `edb0593a9fd6d4568c1d4e9adbb152910dc10603` |
| device ref consumed | `main` @ `33315e89a031c8426a97d00ce5c7a809ba90d380` (round-69 phase 3, committed before the dispatch) |
| job | `build`, 27 steps, 0 failed |

Inputs were the same hardcoded set used for rounds 59-68
(`defconfig=enchilada_defconfig`, `xcode=26.6`, `data_seed_run_id=41552968815`,
`skip_setup=true`, `unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11`).

**Timing note, recorded because it is checkable:** the device ref consumed is
phase 3 (`33315e89`), not phase 4 (`bbd349aa`). Phase 3 carries all five
round-69 files; phase 4 only edits `porting.toml`, which the kernel build does
not read. The run therefore compiled exactly the source under test.

## Artifacts (derived, not guessed)

- `PASS: libEnchiladaAdreno.a (23 objects, sha256 588dee59442fc9d9)`
  (CI #234 was `22 objects, sha256 8a487d183190a276`).
- `kernel.macho` **126,074,880 B**, sha256
  `d266893097d713cec699e3488e1f1a1b8df97b51815010fe5747d390ec86dda8`, magic
  `cf fa ed fe` (Mach-O 64). The sha256 **changed** vs CI #234's
  `d71d6a8b23ed83b321c40af5c68b53d9bdfec13a10f953ae141b705c8fb28acb` → a genuine
  relink, not a stale artifact. Delta vs CI #234: **+4,096 B** — this round DID
  grow the image. That is an observation about this link, not a rule; the
  load-bearing proof remains the object count, the changed sha256 and the symbol
  inventory below.
- `enchilada/logs/kernel.log` is CLEAN: no `Undefined symbols`, no `Error 1`.
- `enchilada/logs/kernel-bootstrap.log` is the INTENTIONALLY failing baseline
  (still contains `Undefined symbols for architecture arm64` and
  `_native_verified_root_probe`) — retained as differential evidence.
- `archive-23-build-enchilada-adreno.log` is exactly the single line
  `PASS: libEnchiladaAdreno.a (23 objects, sha256 588dee59442fc9d9)`.

## Symbol inventory (all present in the linked image)

16 round-69 gen1-backend exports:
`enchilada_gen1_backend_a630_pins`,
`enchilada_gen1_backend_has_gbif`,
`enchilada_gen1_backend_gbif_reason`,
`enchilada_gen1_backend_gmu_resume`,
`enchilada_gen1_backend_hub_rate_hz`,
`enchilada_gen1_backend_gmu_regions`,
`enchilada_gen1_backend_has_icache_region`,
`enchilada_gen1_backend_gmu_region_bytes`,
`enchilada_gen1_backend_sptprac`,
`enchilada_gen1_backend_pas_id`,
`enchilada_gen1_backend_pas_origin`,
`enchilada_gen1_backend_cp_ring_offsets`,
`enchilada_gen1_backend_admit`,
`enchilada_gen1_backend_prereq_name`,
`enchilada_gen1_backend_prereq_anchor`,
`enchilada_gen1_backend_verify`.

(The enums `enchilada_gen1_resume_step`, `enchilada_gen1_prereq_id` and
`enchilada_gen1_admit_verdict`, and the three structs, are NOT functions and are
deliberately excluded; the header's 16 API declarations are the authority.)

19 round-67 publish exports and 8 round-66 darwin-kmd exports retained, plus the
15 earlier unit names (no regression), including the round-65 seam
(`enchilada_metal_abi_version`, `enchilada_metal_frontend_allowed`),
`AdrenoPowerDownV1ShouldRun`, `AdrenoPowerUpGdscContract`,
`adreno_gdsc_plan_enable`, `AdrenoKGSLSmmuGateAssess`, `AdrenoClkCtlV1`,
`AdrenoPowerUpBackendV1`, `AdrenoGpuccWindowV1`, `AdrenoCallerV1`,
`EnchiladaAdrenoProbe`, `AdrenoPowerUpV1`, `AdrenoGmuCallerV1`,
`AdrenoHfiV1`, `AdrenoFirmwareV1`.

Two boundary absences, asserted rather than assumed: **no `qcom_scm` symbol and
no `adreno_zap_shader_load` symbol reached the image.** The round-69 module is
adjudication only, so a PAS or ZAP loader symbol appearing here would mean the
module had grown behaviour it must not have. `stage3=plan steps=%u` is present;
`stage3=exec` is absent (the probe stays observation-only).

## Independent symbol-table check (third verification)

`llvm-nm --defined-only --format=just-symbols` over both linked images:
CI #234 = **70,187** defined symbols, CI #235 = **70,203** defined symbols —
exactly **+16**, matching the 16 gen1-backend exports one-for-one. The 16 new
names are precisely the `_enchilada_gen1_backend_*` set, and
`LOST_FROM_234 = 0` (no round-67/66/65/earlier export regressed out of the image).

## Two independent verifications

1. `scripts/verify-ci-235-adreno.py` — artifact-based, derives the archive sha16
   and kernel size/sha256 from the downloaded artifacts (79 checks, 0 failures):
   23 objects, sha16 changed, clean kernel.log, dirty bootstrap, Mach-O magic,
   changed sha256, all 16 + 19 + 8 + 15 symbols, the two PAS/ZAP absences, and
   the stage-3 invariants.
2. The .NET `Invoke-RestMethod` audit over run/job/step-list/artifact metadata
   (independent network stack): `completed/success`, run 235, build job 27 steps
   0 failed, 3 artifacts all non-empty.

## Honest boundary

CI COMPILE/LINK ONLY. No device run, no flash, `m39510` NOT exercised,
`CONFIG_XNU_NATIVE_ADRENO` remains `n`, NOT a GPU driver, no Turnip dylib
(xcrun/codesign/hdiutil need macOS), the four hard blockers of
`docs/porting/gpu.md` section 1 remain open: the A6XX_GEN1 backend, the signed
ZAP / TrustZone PAS (GPU_PAS_ID 13), the kgsl SMMU stage-1 context, and the macOS
Metal/IOSurface ABI. The A630 client GPU windows stay all-zero (board-derived,
never guessed). The `gpu` area STAYS `"host-verified"`. It does **not** unblock
stage 5.

**The admission table is unchanged by this run** — still modeled=5, satisfied=0,
verdict BLOCKED. A compile/link proof of an adjudication module cannot satisfy a
single prerequisite, and this document does not pretend otherwise.

Chain: `8dc5c8bc` (round 68 phase 4) -> `33315e89` (round 69 phase 3) ->
`bbd349aa` (round 69 phase 4) -> phase 5 (this evidence + verifier) -> phase 6
(porting.toml CI record).
