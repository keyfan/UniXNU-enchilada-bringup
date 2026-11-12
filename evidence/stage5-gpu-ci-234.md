# Stage 5 GPU - CI #234 (ROUND 68, CI-COMPILE-ONLY)

**Status: HOST-VERIFIED (CI compile/link only).** This is NOT a GPU driver, NOT a
device measurement. No device run was performed, no flash was performed, the
standing flash authorization (`m39510`) was NOT exercised, no MMIO was issued, and
`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area of `porting.toml` STAYS
`"host-verified"`. It does **not** unblock stage 5.

## Why this run

Round 67 added the A630 Metal publish/discovery policy contract
(`drivers/NativeAdreno/AdrenoMetalPublishV1.{h,c}`) as the 22nd unit of
`libEnchiladaAdreno.a`. The host suite, the 164-check cross verifier, and the
strict compile prove the contract locally, but a real XNU kernel build is the only
ABI authority. This run proves the 22nd object compiles and links and that all 19
new exports survive linking, with zero regression of the 21 earlier units.

## Run (observed via the REST API)

| item | value |
| --- | --- |
| repository | `keyfan/unixnu-build` (workflow `kernel.yml`) |
| run | `43181798726` (#234), `workflow_dispatch` |
| created | `2026-11-12T08:18:01Z` |
| updated | `2026-11-12T08:42:31Z` |
| status / conclusion | `completed` / `success` |
| workflow head | `edb0593a9fd6d4568c1d4e9adbb152910dc10603` |
| device ref consumed | `main` @ `8a6d81c262ef03cde34a15a6bcce23582cabb9cf` (round-67 phase-2 record, committed `08:11:03Z`, before the `08:18:01Z` dispatch) |
| job | `build`, 27 steps, 0 failed |

Inputs were the same hardcoded set used for rounds 59-67
(`defconfig=enchilada_defconfig`, `xcode=26.6`, `data_seed_run_id=41552968815`,
`skip_setup=true`, `unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11`).

## Artifacts (derived, not guessed)

- `PASS: libEnchiladaAdreno.a (22 objects, sha256 8a487d183190a276)`
  (CI #233 was `21 objects, sha256 a1d20d90bb7a75d2`).
- `kernel.macho` **126,070,784 B**, sha256 `d71d6a8b23ed83b321c40af5c68b53d9bdfec13a10f953ae141b705c8fb28acb`, magic `cf fa ed fe`
  (Mach-O 64). The sha256 **changed** vs CI #233's
  `18bcb8e25b431b228bc64645f14b38477560063153b0d560c28bcb46a91a09df` → a genuine
  relink, not a stale artifact. Delta vs CI #233: **0 B** — this run is a literal
  confirmation of the rounds 43/45/59 lesson that an extra archive object can link
  WITHOUT growing the image, so size is OBSERVED, not asserted. The load-bearing
  proof is the object count, the changed sha256, and the symbol inventory below.
- `enchilada/logs/kernel.log` is CLEAN: no `Undefined symbols`, no `Error 1`.
- `enchilada/logs/kernel-bootstrap.log` is the INTENTIONALLY failing baseline
  (still contains `Undefined symbols for architecture arm64` and
  `_native_verified_root_probe`) - retained as differential evidence.

## Symbol inventory (all present in the linked image)

19 round-67 publish/discovery exports:
`enchilada_publish_open_type_is_plugin`,
`enchilada_publish_open_type_is_handoff`,
`enchilada_publish_open_type_is_marker`,
`enchilada_publish_open_type_name`,
`enchilada_publish_is_render_server`,
`enchilada_publish_client_name_valid`,
`enchilada_publish_client_listed`,
`enchilada_publish_client_cap`,
`enchilada_publish_queue_base`,
`enchilada_publish_windows_a630`,
`enchilada_publish_windows_consistent`,
`enchilada_publish_windows_are_board_derived`,
`enchilada_publish_sheng_reference`,
`enchilada_publish_per_object_cap_bytes_shift`,
`enchilada_publish_stage_name`,
`enchilada_publish_blocker_name`,
`enchilada_publish_stage_blockers`,
`enchilada_publish_establishment_blockers`,
`enchilada_publish_frontend_allowed`.

(The three enum/struct tags `enchilada_publish_stage`, `enchilada_publish_blocker`,
`enchilada_publish_window` are NOT functions and are deliberately excluded; the
header's 19 API declarations are the authority.)

8 round-66 darwin-kmd exports retained:
`enchilada_darwin_abi_version`, `enchilada_darwin_method_is_known`,
`enchilada_darwin_method_name`, `enchilada_darwin_device_info_a630`,
`enchilada_darwin_specified_fields`, `enchilada_darwin_board_derived_fields`,
`enchilada_darwin_device_info_consistent`,
`enchilada_darwin_compiler_a630_available`.

15 earlier unit names retained (no regression), including the round-65 seam
(`enchilada_metal_abi_version`, `enchilada_metal_frontend_allowed`),
`AdrenoPowerDownV1ShouldRun`, `AdrenoPowerUpGdscContract`,
`adreno_gdsc_plan_enable`, `AdrenoKGSLSmmuGateAssess`, `AdrenoClkCtlV1`,
`AdrenoPowerUpBackendV1`, `AdrenoGpuccWindowV1`, `AdrenoCallerV1`,
`EnchiladaAdrenoProbe`, `AdrenoPowerUpV1`, `AdrenoGmuCallerV1`,
`AdrenoHfiV1`, `AdrenoFirmwareV1`.

`stage3=plan steps=%u` is present; `stage3=exec` is absent (the probe stays
observation-only). The publish module is freestanding policy code: no libc, no
MMIO, no IOKit, no power-up / EXECUTE path was compiled in.

## Independent symbol-table check (third verification)

`llvm-nm --defined-only --format=just-symbols` over both linked images:
CI #233 = **70,168** defined symbols, CI #234 = **70,187** defined symbols —
exactly **+19**, matching the 19 publish exports one-for-one.
`LOST_FROM_233 = 0` (no round-66/65/earlier export regressed out of the image).
All 21 `enchilada_(darwin|metal)_*` exports from #233 are still present.

## Two independent verifications

1. `scripts/verify-ci-234-adreno.py` - artifact-based, derives the archive sha16
   and kernel size/sha256 from the downloaded artifacts (60 checks, 0
   failures): 22 objects, sha16 changed, clean kernel.log, dirty bootstrap,
   Mach-O magic, changed sha256, all 19 + 8 + 15 symbols, stage-3 invariants.
2. `work/tmp/_verify_ci234_dotnet.ps1` - .NET `Invoke-RestMethod` over run/job/
   step-list/artifact metadata (independent network stack): `completed/success`,
   run 234, build job 27 steps 0 failed, 3 artifacts all non-empty.

## Honest boundary

CI COMPILE/LINK ONLY. No device run, no flash, `m39510` NOT exercised,
`CONFIG_XNU_NATIVE_ADRENO` remains `n`, NOT a GPU driver, no Turnip dylib
(xcrun/codesign/hdiutil need macOS), the four hard blockers of
`docs/porting/gpu.md` section 1 remain open: the A6XX_GEN1 backend, the signed
ZAP / TrustZone PAS (GPU_PAS_ID 13), the kgsl SMMU stage-1 context, and the macOS
Metal/IOSurface ABI. The A630 client GPU windows stay all-zero (board-derived,
never guessed). The `gpu` area STAYS `"host-verified"`. It does **not** unblock
stage 5.

Chain: `48327a35` (round 66 CI #233 record) -> `7a067234` (round 67 phase 1)
-> `8a6d81c2` (round 67 phase 2) -> phase 3 (this evidence + verifier)
-> phase 4 (porting.toml record).
