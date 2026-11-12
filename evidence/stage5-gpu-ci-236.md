# Stage 5 GPU - CI #236 (ROUND 70, CI-COMPILE-ONLY)

**Status: HOST-VERIFIED (CI compile/link only).** This is NOT a GPU driver, NOT a
device measurement. No device run was performed, no flash was performed, the
standing flash authorization (`m39510`) was NOT exercised, no MMIO was issued, and
`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area of `porting.toml` STAYS
`"host-verified"`. It does **not** unblock stage 5.

## Why this run

Round 70 added the A630 CP ring init/doorbell contract
(`drivers/NativeAdreno/AdrenoCpRingV1.{h,c}`) as the 24th unit of
`libEnchiladaAdreno.a`, plus the 24th host suite and a 139-check cross verifier.

The load-bearing question this run answers is narrow and structural: **does the
new unit compile under the kernel's own clang and survive linking**, and did any
earlier export regress? Round 70 also moved the admission row `cp-ring`
modeled 0 -> 1, which is a *source* change in an already-linked unit, so this run
also proves that edit did not disturb the 23 units before it.

## Run (observed via the REST API)

| item | value |
| --- | --- |
| repository | `keyfan/unixnu-build` (workflow `kernel.yml`) |
| run | `43208981056` (#236), `workflow_dispatch` |
| created | `2026-11-12T11:25:06Z` |
| updated | `2026-11-12T11:54:06Z` |
| status / conclusion | `completed` / `success` |
| workflow head | `edb0593a9fd6d4568c1d4e9adbb152910dc10603` |
| device ref consumed | `main` @ `134c7a493e57b2a897c2794a35d01fc89ae7a227` (round-70 phase 3, committed before the dispatch) |
| job | `build`, 27 steps, 0 failed |

Inputs were the same hardcoded set used for rounds 59-69
(`defconfig=enchilada_defconfig`, `xcode=26.6`, `data_seed_run_id=41552968815`,
`skip_setup=true`, `unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11`).

**Timing note:** the device ref consumed is phase 3 (`134c7a49`), which carries
the new module and the admission-row edit. Phase 4 only edits `porting.toml`,
which the kernel build does not read.

## Artifacts (derived, not guessed)

- `PASS: libEnchiladaAdreno.a (24 objects, sha256 59e80bd40870c8d8)`
  (CI #235 was `23 objects, sha256 588dee59442fc9d9`).
- `kernel.macho` **126,074,880 B**, sha256
  `9bd2d93910192a9c5d24c9aa9f297ebec85ce934674d50e5c38eaff9719b609e`, magic
  `cf fa ed fe` (Mach-O 64). The sha256 **changed** vs CI #235's
  `d266893097d713cec699e3488e1f1a1b8df97b51815010fe5747d390ec86dda8` → a genuine
  relink. Delta vs CI #235: **0 B** — the image did not grow, which rounds
  43/45/59 already established is possible and which is therefore recorded as an
  OBSERVATION, never as proof. The proof is the object count, the changed
  sha256, and the symbol inventory below.
- `enchilada/logs/kernel.log` is CLEAN (36,731,115 B): no `Undefined symbols`, no
  `Error 1`.
- `enchilada/logs/kernel-bootstrap.log` (37,040,087 B) is the INTENTIONALLY
  failing baseline (still contains `Undefined symbols for architecture arm64` and
  `_native_verified_root_probe`) — retained as differential evidence.
- `archive-23-build-enchilada-adreno.log` is exactly the single line
  `PASS: libEnchiladaAdreno.a (24 objects, sha256 59e80bd40870c8d8)`.
  (The archive step keeps its historical ordinal 23 because it is one *archive*
  recipe in `board.toml`; the object count inside it is what moved, 23 -> 24.)

## Symbol inventory (all present in the linked image)

11 round-70 cp-ring exports:
`adreno_cp_ring_offsets`,
`adreno_cp_ring_plan`,
`adreno_cp_ring_me_init`,
`adreno_cp_ring_me_init_matches`,
`adreno_cp_ring_shadow_mode`,
`adreno_cp_ring_doorbell_order`,
`adreno_cp_ring_undefined`,
`adreno_cp_ring_undefined_name`,
`adreno_cp_ring_undefined_anchor`,
`adreno_cp_ring_is_executable`,
`adreno_cp_ring_verify`.

(The enums `adreno_cp_ring_step`, `adreno_cp_ring_undefined` and
`AdrenoCpRingShadowMode`, and the struct `adreno_cp_ring_step_desc`, are NOT
functions and are deliberately excluded; the header's 11 API declarations are the
authority. `adreno_cp_ring_undefined` appears in both the enum and the function
name; the symbol check matches the function.)

16 round-69 gen1-backend exports, 19 round-67 publish exports, 8 round-66
darwin-kmd exports and the 15 earlier unit names are all retained — no
regression, which matters here because round 70 edited
`AdrenoGen1BackendV1.c` (the `cp-ring` admission row) as well as adding a unit.

Two boundary absences, asserted rather than assumed: **no `qcom_scm` symbol and
no `adreno_zap_shader_load` symbol reached the image.** `adreno_cp_ring_is_executable()`
is a pure predicate returning 0 — a ZAP loader or PAS call appearing here would
mean the module had grown behaviour it must not have. `stage3=plan steps=%u` is
present; `stage3=exec` is absent (the probe stays observation-only).

## Independent symbol-table check (third verification)

`llvm-nm --defined-only --format=just-symbols` over both linked images:
CI #235 = **70,203** defined symbols, CI #236 = **70,214** defined symbols —
exactly **+11**, matching the 11 cp-ring exports one-for-one. The 11 new names are
precisely the `_adreno_cp_ring_*` set, and `LOST_FROM_235 = 0`.

## Two independent verifications

1. `scripts/verify-ci-236-adreno.py` — artifact-based, derives the archive sha16
   and kernel size/sha256 from the downloaded artifacts (90 checks, 0 failures):
   24 objects, sha16 changed vs #235, clean kernel.log, dirty bootstrap, Mach-O
   magic, changed sha256, all 11 + 16 + 19 + 8 + 15 symbols, the two PAS/ZAP
   absences, and the stage-3 invariants.
2. The .NET `Invoke-RestMethod` audit over run/job/step-list/artifact metadata
   (independent network stack): `completed/success`, run 236, build job 27 steps
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

**The admission table does not move because of this run** — still modeled=6,
satisfied=0, verdict BLOCKED. A compile/link proof of an order-and-payload
contract cannot satisfy a prerequisite, and this document does not pretend
otherwise. What the run does settle is that the cp-ring unit is kernel-clang
clean, links, and did not disturb its 23 predecessors.

Chain: `abbfb134` (round 69 phase 7) -> `134c7a49` (round 70 phase 3) ->
`0188e9aa` (round 70 phase 4) -> phase 5 (this evidence + verifier) -> phase 6
(porting.toml CI record).
