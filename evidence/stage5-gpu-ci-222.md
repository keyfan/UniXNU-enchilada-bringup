# CI #222 — ROUND 47 · Date: 2026-11-11

## What ran

CI run [`42847018929`](https://github.com/keyfan/unixnu-build/actions/runs/42847018929)
(#222) on the round-46 tree `eefbdd5cf6ee334a93fe291c4e81b3fa1345c2fa` (main of
`keyfan/UniXNU-enchilada-bringup`), dispatched with the standing inputs
(`device_repository=keyfan/UniXNU-enchilada-bringup`, `device_ref=main`,
`defconfig=enchilada_defconfig`, `xcode=26.6`, `data_seed_run_id=41552968815`,
`skip_setup=true`, `unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11`).

## Observed result (two independent verification paths)

1. **`verify-ci-222-adreno.py`** (this repo, artifacts downloaded via
   `_fastart.py`, 11 checks) — **`CI222_ADRENO_VERIFY: PASS (0 failures)`**:
   - archive build log `archive-23-build-enchilada-adreno.log` contains exactly
     `PASS: libEnchiladaAdreno.a (10 objects, sha256 8f503676a8b08541)`
     (round 45 was 9 objects; the 10th object is `AdrenoGmuCallerV1.c`, wired
     into `scripts/build-enchilada-adreno.py` in round 46);
   - `kernel.macho` present, **126,038,016 B**, sha256
     `61b32f0ce819ec7d7fc4a1d4f69f9db787406bb890f15542189ff5b18584ce40`
     (observed; size grows vs CI #221's 126,017,536 B because the relink
     re-included object metadata — the archive hash changed from
     `b84486919884d060`, which per round 43 root-cause is `xcrun ar rcs`
     member metadata plus the genuinely new 10th object);
   - no `(11 objects` regression string.
2. **.NET `Invoke-RestMethod`** (independent network stack) —
   `RUN_STATUS=completed`, `RUN_CONCLUSION=success`, created
   `2026-11-10T15:41:42Z`; artifacts listed: logs 3,739,776 B,
   kernel 78,833,148 B, payload 82,992,783 B.

## Honest boundary

- CI-COMPILE-ONLY: the 10-object archive proves the caller chain
  (`AdrenoGmuCallerV1` + `AdrenoGmuBusV1` + `AdrenoGmuBootV1` + domain-state
  power gate) compiles and links inside the real XNU kernel build; it does NOT
  execute any GMU code on hardware.
- The build log holds only the single PASS line (identical to CIs #219–#221),
  so per-object compile lines are not observable; object inclusion is proven by
  the object count delta (9 → 10) plus the committed
  `scripts/build-enchilada-adreno.py` source list and the local strict clang
  compile of the same unit.
- gpu area status stays **host-verified**; device verification of the powered
  GMU path remains pending (needs CX/GX genpd + clocks on device, then the
  caller chain).

## Verification chain

`63a19ee3` (r45) → `ead5834e` → `bb1808af` → `eefbdd5c` (r46, caller chain +
record) → this CI run consumed `device_ref=main` = `eefbdd5c`.
