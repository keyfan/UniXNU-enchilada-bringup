# Stage 5 GPU - CI #233 (ROUND 66, CI-COMPILE-ONLY)

**Status: HOST-VERIFIED (CI compile/link only).** This is NOT a GPU driver, NOT a
device measurement. No device run was performed, no flash was performed, the
standing flash authorization (`m39510`) was NOT exercised, no MMIO was issued, and
`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area of `porting.toml` STAYS
`"host-verified"`. It does **not** unblock stage 5.

## Why this run

Round 66 added the A630 Darwin-KMD `tu-darwin-abi-dir` DIRECTORY CONTRACT
(`drivers/NativeAdreno/AdrenoDarwinKmdAbiV1/{NativeAdrenoABI.h,AdrenoDarwinKmdAbiV1.c}`)
as the 21st unit of `libEnchiladaAdreno.a`. The host suite and cross verifier prove
the contract locally, but a real XNU kernel build is the only ABI authority. This run
proves the 21st object compiles and links and that all 8 new exports survive linking.

## Run (observed via the REST API)

| item | value |
| --- | --- |
| repository | `keyfan/unixnu-build` (workflow `kernel.yml`) |
| run | `43158161524` (#233), `workflow_dispatch` |
| created | `2026-11-12T04:57:42Z` |
| updated | `2026-11-12T05:12:13Z` |
| status / conclusion | `completed` / `success` |
| workflow head | `edb0593a9fd6d4568c1d4e9adbb152910dc10603` |
| device ref consumed | `main` @ `6e50eea85593c998d16d4fb58adec6aa4ec4ca6a` |
| job | `build`, 27 steps, 0 failed |

Inputs were the same hardcoded set used for rounds 59-65
(`defconfig=enchilada_defconfig`, `xcode=26.6`, `data_seed_run_id=41552968815`,
`skip_setup=true`, `unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11`).

## Artifacts (derived, not guessed)

- `PASS: libEnchiladaAdreno.a (21 objects, sha256 a1d20d90bb7a75d2)`
  (CI #232 was `20 objects, sha256 678e1c6219cc110d`).
- `kernel.macho` **126,070,784 B**, sha256 `18bcb8e25b431b228bc64645f14b38477560063153b0d560c28bcb46a91a09df`, magic `cf fa ed fe`
  (Mach-O 64). The sha256 **changed** vs CI #232's
  `a69f8e7dbba316416712ff499eb291d52b65ea9a1a912a0cb46a9c7f8d6eb137` → a genuine
  relink, not a stale artifact. Delta vs CI #232: +4,096 B
  (**OBSERVED, not the proof** - rounds 43/45/59 showed extra objects can link
  without growing the image, so the load-bearing proof is the object count, the
  changed sha256, and the symbol inventory below).
- `enchilada/logs/kernel.log` is CLEAN: no `Undefined symbols`, no `Error 1`.
- `enchilada/logs/kernel-bootstrap.log` is the INTENTIONALLY failing baseline
  (still contains `Undefined symbols for architecture arm64` and
  `_native_verified_root_probe`) - retained as differential evidence.

## Symbol inventory (all present in the linked image)

8 round-66 darwin-kmd exports:
`enchilada_darwin_abi_version`, `enchilada_darwin_method_is_known`,
`enchilada_darwin_method_name`, `enchilada_darwin_device_info_a630`,
`enchilada_darwin_specified_fields`, `enchilada_darwin_board_derived_fields`,
`enchilada_darwin_device_info_consistent`,
`enchilada_darwin_compiler_a630_available`.

15 earlier unit names retained (no regression), including the round-65 seam
(`enchilada_metal_abi_version`, `enchilada_metal_frontend_allowed`),
`AdrenoMetalSeamV1`, `AdrenoCallerV1`, `AdrenoClkCtlV1`,
`AdrenoPowerUpBackendV1`, `AdrenoGpuccWindowV1`, `AdrenoPowerDownV1ShouldRun`,
`AdrenoPowerUpGdscContract`, `adreno_gdsc_plan_enable`,
`AdrenoKGSLSmmuGateAssess`, `EnchiladaAdrenoProbe`, `AdrenoPowerUpV1`,
`AdrenoGmuCallerV1`, `AdrenoHfiV1`, `AdrenoFirmwareV1`.

`stage3=plan steps=%u` is present; `stage3=exec` is absent (the probe stays
observation-only).

## Two independent verifications

1. `scripts/verify-ci-233-adreno.py` - artifact-based, derives the archive sha16
   and kernel size/sha256 from the downloaded artifacts (45-style checks, 0
   failures): 21 objects, sha16 changed, clean kernel.log, dirty bootstrap,
   Mach-O magic, changed sha256, all 8 + 15 symbols, stage-3 invariants.
2. `work/tmp/_verify_ci233_dotnet.ps1` - .NET `Invoke-RestMethod` over run/job/
   step-list/artifact metadata (independent network stack): `completed/success`,
   run 233, build job 27 steps 0 failed, 3 artifacts all non-empty.

## Honest boundary

CI COMPILE/LINK ONLY. No device run, no flash, `m39510` NOT exercised,
`CONFIG_XNU_NATIVE_ADRENO` remains `n`, NOT a GPU driver, no Turnip dylib
(xcrun/codesign/hdiutil need macOS), the four hard blockers of
`docs/porting/gpu.md` section 1 remain open. The `gpu` area STAYS
`"host-verified"`. It does **not** unblock stage 5.

Chain: `d971cc78` (round 65) -> `68b0810e` (round 66 phase 1) -> `6e50eea8`
(round 66 phase 2) -> phase 3 (this evidence + verifier) -> phase 4 (record).
