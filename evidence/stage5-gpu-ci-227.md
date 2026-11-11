# CI #227 — ROUND 51 · Date: 2026-11-11

## What ran

CI run [`42967214353`](https://github.com/keyfan/unixnu-build/actions/runs/42967214353)
(#227) on the round-51 record tree `be08e3183e9847f5570987fd8acd43c34686aff6`
(main of `keyfan/UniXNU-enchilada-bringup`), dispatched with the standing inputs
(`device_repository=keyfan/UniXNU-enchilada-bringup`, `device_ref=main`,
`defconfig=enchilada_defconfig`, `xcode=26.6`, `data_seed_run_id=41552968815`,
`skip_setup=true`, `unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11`).
Created `2026-11-11T05:39:47Z`; macOS Xcode 26.6 runner, single `build` job of
27 steps. `HEAD_SHA=edb0593a9fd6d4568c1d4e9adbb152910dc10603` is the
workflow-dispatch repo (`keyfan/unixnu-build`) head, not the device repo head —
the device tree is consumed through `device_ref=main`.

## Observed result (two independent verification paths)

1. **`scripts/verify-ci-227-adreno.py`** (this repo; artifacts downloaded via
   `_fastart.py` in parallel) — **`CI227_ADRENO_VERIFY: PASS (0 failures)`**,
   12 checks:
   - archive build log contains exactly
     `PASS: libEnchiladaAdreno.a (11 objects, sha256 61b100b4355376f8)`
     (round 47/CI #222 was 10 objects; the 11th object is `AdrenoPowerUpV1.c`,
     wired into `scripts/build-enchilada-adreno.py` in round 51);
   - `kernel.macho` present, **126,038,016 B**, sha256
     `b5f136e948b71828a4a551854149ae36805f01813968762c7fc398c5271f0c5e`
     (observed; size UNCHANGED vs CI #222's 126,038,016 B, sha CHANGED from
     `61b32f0ce819…` — the new object links without growing the image; per the
     round-43 root cause the archive/kernel sha delta is `xcrun ar rcs` member
     metadata plus the genuinely new object, not a source regression);
   - no `(10 objects` / `(12 objects` regression string.
2. **.NET `Invoke-RestMethod`** (independent network stack) —
   `RUN_STATUS=completed`, `RUN_CONCLUSION=success`, `JOB=build
   completed/success steps=27`; artifacts: logs 3,823,170 B,
   kernel 78,833,673 B, payload 83,076,388 B.

## Honest boundary

- CI-COMPILE-ONLY: the 11-object archive proves the round-51 ordered genpd +
  clock power-up model (`AdrenoPowerUpV1`) compiles and links inside the real
  XNU kernel build; it does NOT execute any genpd/clock/GMU code on hardware.
- The build log holds only the single PASS line (identical to CIs #219–#222),
  so per-object compile lines are not observable; object inclusion is proven by
  the object count delta (10 → 11) plus the committed
  `scripts/build-enchilada-adreno.py` source list and the local strict clang
  compile of the same unit (`STRICT_C_EXIT=0`, 2072 B object).
- gpu area status stays **host-verified**; device verification of the powered
  GMU path remains pending (needs the real XNU backend that calls this model's
  function pointers, then kgsl SMMU stage-1 context and the GMU boot chain).
  No flash was performed for this item (m39510 standing authorization not
  exercised — a freestanding model has no kernel caller, so a flash would have
  no observable effect).

## Verification chain

`76679d4e` (r49/50) → `464074d8` (r51 phase 1: model + tests + verifier + doc)
→ `be08e318` (r51 phase 2: porting.toml record) → this CI run consumed
`device_ref=main` = `be08e318`.
