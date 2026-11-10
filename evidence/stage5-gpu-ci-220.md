# Stage 5 GPU — CI #220 (round 43): kernel compile of the unchanged round-42 Adreno tree

## What ran

- Workflow `keyfan/unixnu-build` "Build kernel (with artifacts)" run **42810794944** (#220),
  `workflow_dispatch`, created 2026-11-10T11:50:54Z, completed **success** 12:06:04Z
  (run_attempt 1; job `build` success — verified via raw http.client AND .NET Invoke-RestMethod).
- Source: device repo `keyfan/UniXNU-enchilada-bringup` `main` @ `2692d53d9d3fa0696f4d920c000bd975622d0b4c`
  (round-42 phase-2 head); `unixnu_ref` pinned `8ddccb1e42d0063a2e613f897155c83994f60b11`;
  `defconfig=enchilada_defconfig`, `xcode=26.6`.

## Result

- `archive-23-build-enchilada-adreno.log`: `PASS: libEnchiladaAdreno.a (7 objects, sha256 04c763f8e5837408)`.
- `kernel.macho`: 126017536 B, sha256 `16a9653924722978a6999b7b35b59f1ace7e6f3e6e8553d5a88d4ff4588aab59`
  (CI #219: `f12a214e7b15a5a8…`, SAME size — delta 0).

## Why the archive sha changed at the same object count (root cause, not hand-waved)

1. All **14 compiled inputs** (7 units: `EnchiladaAdrenoProbe.cpp`, `AdrenoDomainState.c`,
   `AdrenoPowerGate.c`, `AdrenoBoardInputs.c`, `AdrenoCatalog.c`, `AdrenoHfiV1.c`,
   `AdrenoFirmwareV1.c`; their headers; `NativeAdrenoA630HwInit.h`) are **blob-IDENTICAL**
   between CI #219-era head `67afb565` and CI #220 source `2692d53d`
   (remote contents API, `CHANGED_COUNT 0`).
2. `scripts/build-enchilada-adreno.py` compiles with the recorded XNU compile flags
   (`iokit/IOPlatformExpert.cpo.json`, `osfmk/startup.o.json`); `unixnu_ref` pinned and unchanged.
3. `xcrun ar rcs` stores per-member mtime/uid/gid headers → archive BYTES are
   nondeterministic across runs even with byte-identical objects.
4. `build-enchilada-adreno.py:101-106` deletes the unstripped kernel on any archive-byte
   change and relinks → explains the same-size, different-sha `kernel.macho`.

## Verification (two independent domains)

1. `scripts/verify-ci-220-adreno.py` (committed this round): reads the downloaded logs zip +
   kernel zip → `CI220_ADRENO_VERIFY: PASS (3 checks)` (archive line exact match, size unchanged,
   no 8th object).
2. .NET `Invoke-RestMethod` run/job conclusion query: `RUN_STATUS=completed CONCLUSION=success`,
   `JOB=build STATUS=completed CONCLUSION=success`.

## Honest boundary

- CI compile green only; the archive object COUNT is unchanged (7; `AdrenoGmuBootV1.c` is a
  host-verified model, intentionally NOT a CI object — the 8th host-test registration is
  host-runner-only).
- No device run; gpu area stays `host-verified`.
