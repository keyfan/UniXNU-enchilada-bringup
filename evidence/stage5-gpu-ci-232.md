# Stage 5 GPU — CI #232: the A630 Metal establishment-chain seam compiles and links in a real XNU build (ROUND 65, CI-COMPILE-ONLY)

**Status: HOST-VERIFIED (CI compile + link only).** No device run was performed, no
flash was performed, the standing flash authorization (`m39510`) was **not**
exercised, no MMIO was issued, and `CONFIG_XNU_NATIVE_ADRENO` remains `n`. The
`gpu` area of `porting.toml` STAYS `host-verified`.

## Why this run

ROUND 65 added `AdrenoMetalSeamV1.c` as the 20th unit of
`libEnchiladaAdreno.a`, and consolidated four previously unpushed units
(ROUND 61–64: `AdrenoClkCtlV1`, `AdrenoPowerUpBackendV1`,
`AdrenoGpuccWindowV1`, `AdrenoCallerV1`). A freestanding Windows `clang` build
and the 20-suite host test are a *different toolchain, SDK and link step* than
the macOS/Xcode kernel build, so they cannot prove that the module compiles and
links against the real XNU image. CI is the only ABI authority.

## The run

| Field | Value |
| --- | --- |
| Repository / workflow | `keyfan/unixnu-build` / `kernel.yml` |
| Run | `43142322338` (number 232), `workflow_dispatch` |
| Created / completed (UTC) | `2026-11-12T02:34:09Z` / `2026-11-12T02:53:40Z` |
| Status / conclusion | `completed` / `success` |
| Job `build` | `completed` / `success`, 27 steps, 0 failed steps |
| Workflow head | `edb0593a9fd6d4568c1d4e9adbb152910dc10603` (workflow file) |
| Device head consumed | `e2adac8efc64c78dd52fa64351fcc931fb900756` (ROUND 65 phase 2) |
| Inputs | `device_repository=keyfan/UniXNU-enchilada-bringup`, `device_ref=main`, `defconfig=enchilada_defconfig`, `xcode=26.6`, `data_seed_run_id=41552968815`, `skip_setup=true`, `unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11` |

Artifacts: `unixnu-enchilada_defconfig-logs` 3,817,543 B,
`unixnu-enchilada_defconfig-kernel` 78,844,767 B,
`unixnu-enchilada_defconfig-payload` 83,080,293 B.

## Observed results (derived from the artifacts, not guessed)

- `PASS: libEnchiladaAdreno.a (20 objects, sha256 678e1c6219cc110d)` —
  CI #231 had 15 objects `f0030d424fa6fc81`. The archive grew by the four
  consolidated units plus the ROUND 65 seam, and its sha16 changed (genuine
  rebuild, not a stale artifact).
- `kernel.macho` **126,066,688 B**, sha256
  `a69f8e7dbba316416712ff499eb291d52b65ea9a1a912a0cb46a9c7f8d6eb137`,
  Mach-O 64 magic `cf fa ed fe`. CI #231 was 126,046,208 B with sha256
  `9de56c6ac592dd53f07bae866bf8379ede11703a617439bcca5f68c10938bb8b`, so the
  kernel grew by 20,480 B and its sha256 changed (genuine relink).
  **The size delta is OBSERVED, not the proof**: ROUND 43/45/59 showed that
  additional archive objects can link without moving the image size (they fit
  pages already reached). The load-bearing evidence is the archive object count,
  the changed sha256, and the symbol inventory below.
- `enchilada/logs/kernel.log` 36,731,148 B contains **no** `Undefined symbols`
  and **no** `Error 1` — the real build is clean.
- `enchilada/logs/kernel-bootstrap.log` 37,040,302 B **still** contains
  `Undefined symbols for architecture arm64` and `_native_verified_root_probe`.
  This is the intentionally failing *baseline* built without the extra
  archives; its persistence is the differential evidence that the archive is
  what resolves those symbols.

## Linked-symbol inventory

All 14 ROUND 65 exports survived into the image:
`enchilada_metal_abi_version`, `enchilada_metal_method_is_known`,
`enchilada_metal_method_name`, `enchilada_metal_abi_params_valid`,
`enchilada_metal_device_info_a630`,
`enchilada_metal_device_info_specified_fields`,
`enchilada_metal_device_info_board_derived_fields`,
`enchilada_metal_device_info_consistent`, `enchilada_metal_stage_name`,
`enchilada_metal_blocker_name`, `enchilada_metal_stage_blockers`,
`enchilada_metal_establishment_blockers`,
`enchilada_metal_compiler_a630_available`,
`enchilada_metal_frontend_allowed`.

All 13 earlier unit names also survived (`AdrenoPowerDownV1ShouldRun`,
`AdrenoPowerUpGdscContract`, `adreno_gdsc_plan_enable`,
`AdrenoKGSLSmmuGateAssess`, `AdrenoClkCtlV1`, `AdrenoPowerUpBackendV1`,
`AdrenoGpuccWindowV1`, `AdrenoCallerV1`, `EnchiladaAdrenoProbe`,
`AdrenoPowerUpV1`, `AdrenoGmuCallerV1`, `AdrenoHfiV1`,
`AdrenoFirmwareV1`). `stage3=plan steps=%u` is present and `stage3=exec` is
absent — the probe remains the observation-only stage path.

## Two independent verifications (m03546)

1. **Domain 1 — artifacts**: `scripts/verify-ci-232-adreno.py` derives the
   archive sha16 and the kernel size/sha256 from the downloaded artifacts and
   asserts 20 objects, the archive-sha change, exactly one clean `kernel.log`,
   exactly one intentionally-dirty `kernel-bootstrap.log`, the Mach-O magic, the
   kernel-sha change, all 27 symbols, and the two stage-3 string conditions.
   Result: `CI232_ADRENO_VERIFY: PASS (45 checks, 0 failures)`.
   The verifier's symbol list was separately cross-checked against the real
   headers/sources (`_r65_sym_exports.py`: 14 metal-seam exports declared +
   defined, 13 earlier unit names present, `R65_SYM_EXPORTS: PASS`).
   A pre-run correction removed a `kernel.macho GREW` assertion: size alone can
   neither prove nor disprove a link (ROUND 43/45/59 lesson); replaced with the
   stronger `sha256 != CI #231` relink check.
2. **Domain 2 — workflow**: `work/tmp/_verify_ci232_dotnet.ps1` reads the
   run/job/artifact metadata through `.NET Invoke-RestMethod` (a different
   network stack from the urllib pusher). Result:
   `DOTNET_VERIFY_CI232: PASS (14 checks, 0 failures)` — `completed/success`,
   `workflow_dispatch`, run number 232, expected workflow head, build job
   success with 27 steps and 0 failed steps, 3 artifacts, kernel > 70 MB.
   The first version of this script returned 404 on a malformed `/jobs` URI;
   it was rewritten to the proven `_verify_ci231_dotnet.ps1` shape before any
   result was claimed.

## Honest boundary

This is a **CI compile/link proof only**, exactly like CI #218/#219/#220/#221/
#222/#223/#230/#231. It proves the ROUND 65 seam is a well-formed C object that
survives the real XNU link. It does **not** prove that a Metal device is
published, that any A6XX_GEN1 kernel backend exists, that signed ZAP/PAS, a
kgsl-SMMU context, or an OS Metal/IOSurface ABI are available. Per
`docs/porting/gpu.md:27-28` a frontend may be selected only *after* a matching
backend and compiler support exist. `AdrenoMetalSeamV1` is a wire-ABI /
establishment-chain model, **not a GPU driver**: no MMIO, no BO/IOVA objects,
no firmware load, no `MTLDevice`, no user client. It does **not** unblock
stage 5. No device run, no flash, no trust-cache/AMFI change. The `gpu` area
STAYS `host-verified`.

Chain: `7b630048` (ROUND 60) → `d131433a` (ROUND 65 phase 1) → `e2adac8e`
(ROUND 65 phase 2) → this CI record.
