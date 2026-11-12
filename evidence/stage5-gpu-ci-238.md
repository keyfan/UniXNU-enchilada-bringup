# Stage 5 GPU - CI #238 (ROUND 78, CI-COMPILE-ONLY)

**Status: HOST-VERIFIED (CI compile/link only).** This is NOT a GPU driver, NOT a
device measurement. No device run was performed, no flash was performed, the
standing flash authorization (`m39510`) was NOT exercised, no MMIO was issued, and
`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area of `porting.toml` STAYS
`"host-verified"`. It does **not** unblock stage 5.

## Why this run

Round 77 added `AdrenoVaMapV1` as the 28th archive unit — the GPU VA aperture
DERIVATION model. It had passed the host suite and a 98-check cross-verifier, but
had never been through the kernel's own clang. This run is its ABI authority, and
the first archive change since CI #237.

## Run (observed via the REST API)

| item | value |
| --- | --- |
| repository | `keyfan/unixnu-build` (workflow `kernel.yml`) |
| run | `43252926421` (#238), `workflow_dispatch` |
| created | `2026-11-12T15:54:54Z` |
| updated | `2026-11-12T16:14:14Z` |
| status / conclusion | `completed` / `success` |
| workflow head | `edb0593a9fd6d4568c1d4e9adbb152910dc10603` |
| device ref consumed | `main` @ `7569b2cd4a1182844736e6c0cd44761ad4e24e14` (the round-78 verifier-drift fix, before the dispatch) |
| job | `build`, 27 steps, 0 failed |

## Artifacts (derived, not guessed)

- `PASS: libEnchiladaAdreno.a (28 objects, sha256 f90f1a3267b71762)`
  (CI #237 was `27 objects, sha256 a9b461e4f1378731`).
- `kernel.macho` **126,099,456 B**, sha256
  `bdfdc5a05881b663b0450d283bbb015c6f8a590eb6e8aa2fe4ffd287d0d90cc5`, magic
  `cf fa ed fe` (Mach-O 64). Changed vs CI #237's
  `40d6fe4a5fdde2cdeaf0062a3c016346df01bf3d620dff339e60cc76c450918e` → a genuine
  relink. **Size delta vs CI #237: 0 B** — the 28th object linked without growing
  the image, which is the rounds 43/45/59 observation holding again. So the
  load-bearing proof is the object count, the changed sha256 and the symbol
  inventory, exactly as those rounds established.
- `enchilada/logs/kernel.log` is CLEAN (36,731,088 B): no `Undefined symbols`, no
  `Error 1`.
- `enchilada/logs/kernel-bootstrap.log` (37,040,096 B) is the INTENTIONALLY
  failing baseline — retained as differential evidence.

A note on why the kernel size is 126,099,456 B in BOTH #237 and #238 while the
archive grew: #237 carried three new units and grew by 24,576 B over #236; #238
carries one more and grew by 0. Both are observations about particular links, not
rules — the archive object count and the symbol inventory are what carry the claim.

## Symbol inventory

All **10** `AdrenoVaMapV1` exports present:
`adreno_va_aperture`, `adreno_va_blocker_anchor`, `adreno_va_blocker_name`,
`adreno_va_clamp_start`, `adreno_va_geometry`, `adreno_va_gmu_end_is_gpu_end`,
`adreno_va_ias_is_plausible`, `adreno_va_map_ready`, `adreno_va_missing`,
`adreno_va_verify`.

Plus the 12 smmu-stage1, 12 mdt, 11 zap-auth, 11 cp-ring, 19 publish, 8 darwin-kmd
and 15 earlier exports — no regression.

## Independent symbol-table check (third verification)

`llvm-nm --defined-only --format=just-symbols` over both linked images:
CI #237 = **70,261** defined symbols, CI #238 = **70,271** — exactly **+10**, and
`LOST_FROM_237 = 0`. The 10 new names are precisely the `AdrenoVaMapV1` exports,
one-for-one.

## A verifier defect this round found before dispatching

`verify-enchilada-gen1-backend.py` — the round-69 admission-table verifier — had
gone stale: it still asserted `zap-auth modeled = 0`, `exactly six items are
modelled`, a 23-unit archive, and the old PASS-string suffix. Rounds 73, 75 and 77
had each moved the table one row and round 77 added the 27th C unit, so **four of
its checks were failing while every other verifier passed**.

That is the same class of drift as round 77's phase-5 fix, and it is worth naming
as a pattern: every round that adds a unit or moves an admission row invalidates a
handful of hard-coded counts scattered across older verifiers, and nothing catches
it except running the whole set. Fixed here to 27 units and nine modelled, with the
summary line corrected to match so the printed text cannot lie about the state.

## Two independent verifications

1. `scripts/verify-ci-238-adreno.py` — artifact-based, derives the archive sha16
   and kernel size/sha256 from the downloaded artifacts (135 checks, 0 failures):
   28 objects, sha16 changed, clean kernel.log, dirty bootstrap, Mach-O magic,
   changed sha256, all 10 va-map + 12 + 12 + 11 + 11 + 19 + 8 + 15 symbols, the
   PAS/ZAP boundary (citation vs symbol), and the stage-3 invariants.
2. The .NET `Invoke-RestMethod` audit over run/job/step-list/artifact metadata
   (independent network stack): `completed/success`, run 238, build job 27 steps
   0 failed, 3 artifacts all non-empty.

## Honest boundary

CI COMPILE/LINK ONLY. No device run, no flash, `m39510` NOT exercised,
`CONFIG_XNU_NATIVE_ADRENO` remains `n`, NOT a GPU driver, no Turnip dylib, the four
hard blockers of `docs/porting/gpu.md` section 1 remain open. The A630 client GPU
windows stay all-zero. The `gpu` area STAYS `"host-verified"`. It does **not**
unblock stage 5.

**The admission table does not move because of this run** — still modeled=9,
satisfied=0, verdict BLOCKED. Compiling a derivation model proves it is
kernel-clang clean and linkable; it does not read an SMMU register or produce a VA.

Chain: `68ac560e` (round 77 phase 5) -> `7569b2cd` (round 78 drift fix) ->
phase 3 (this evidence + verifier) -> phase 4 (porting.toml CI record).
