# ROUND 45 (2026-11-10) — CI #221 on the round-44 tree: 9-object archive, size-stable kernel

## What ran

Kernel CI run `42825411641` (keyfan/unixnu-build #221), dispatched on the
round-44 head `500183a7188cd1933f069f3d9337eccb37a4604f` (device_ref=main,
defconfig=enchilada_defconfig, xcode=26.6, data_seed_run_id=41552968815,
skip_setup=true, unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11).
Run created 2026-11-10T13:32:16Z, completed success (~21 min).

## Result

- `PASS: libEnchiladaAdreno.a (9 objects, sha256 b84486919884d060)` — the two
  round-44 units (`AdrenoGmuBootV1.c`, `AdrenoGmuBusV1.c`) are now CI objects
  8+9 (CI #220 was 7 objects, sha256 04c763f8e5837408).
- kernel.macho: 126017536 B, sha256
  `14f83b37863e965da9c71afe6dcf27b0a922d15186c6a519243c99b97956c043`
  (CI #220 was the SAME SIZE 126017536 B, sha256 `16a96539…`, bytes differ).
  The two new objects link WITHOUT growing the image — size delta 0.
- No "10 objects".

## Two independent verifications (m03546)

1. `scripts/verify-ci-221-adreno.py` → `CI221_ADRENO_VERIFY: PASS (3 checks)`
   (re-reads the downloaded logs zip + kernel zip, no cached state):
   A archive line prefix `…(9 objects, sha256 b84486919884d060`; B size
   unchanged 126017536 B; C no 10th object.
2. Independent byte-level differential (separate ad-hoc script, same zips):
   CI #220 kernel 126017536 B / `16a9653924722978…` vs CI #221 kernel
   126017536 B / `14f83b37863e965da9…`, `SAME_BYTES False` — a real rebuild
   with the two new objects, size-stable.

## Honest boundary

- The 9-object archive is a CI-compile result ONLY: the two new modules have
  NO kernel caller, map no MMIO, drive no clock/regulator, and have never run
  on hardware. `CONFIG_XNU_NATIVE_ADRENO=n` unchanged.
- kernel.macho SIZE did not change (+0 B, not the +8192 B sketched in the
  round-44 plan); the archive-sha delta (04c763f8 → b8448691) matches the two
  added objects.
- gpu AREA STAYS host-verified. No device run, no GPUCC read, no firmware
  load, no HFI exchange.
- build-report.json is NOT uploaded in the CI logs artifact, so its
  source_sha256/hardware_execution fields cannot be verified from CI; the
  equivalent fact (the two sources compile into the archive) is proven by the
  9-object count itself.
