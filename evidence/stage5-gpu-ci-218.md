# Stage 5 GPU — CI #218 proves the round-31 A630 legacy HFI v1 module is compiled by the kernel

**Status: HOST-VERIFIED (CI build + link). NOT a device measurement, NOT a driver.**
Round 32 (2026-11-10). This document closes the single pending item from Round 31:
"the module is wired into libEnchiladaAdreno.a but a green run for THIS object is
not yet observed". m13019 requires each item be CI-compiled; m03546 requires two
independent verifications; m12247 requires verify-before-flash (no flash was done,
the device is disconnected).

## Why this matters

Round 29 wired four NativeAdreno C modules into a `-force_load` kernel archive;
Round 30 (CI #217, run `42726356348`) observed that archive build+link with
**5 objects**. Round 31 added a **6th object**, `drivers/NativeAdreno/AdrenoHfiV1.c`
— the A630 legacy GMU HFI v1 wire-protocol transport — and dispatched CI #218
(run `42743495901`). A host-only type-check and an arm64 Mach-O pre-check are NOT
proof the kernel's own clang accepts the file. This round observes that proof.

## The run (independent .NET + urllib head confirmations)

- CI run `42743495901` (#218), `keyfan/unixnu-build`, `head_branch=main`,
  `status=completed`, `conclusion=success`, created `2026-11-10T02:32:44Z`,
  updated `2026-11-10T02:47:45Z`. Build kernel step went `in_progress` → success
  (step 15 of 46). Device repo `device_ref=main` at the round-31 phase-2 head
  `bce4c5886b71c4c057b7126b3b67f3ad34a2f780`.
- 3 artifacts uploaded: `unixnu-enchilada_defconfig-{logs,kernel,payload}`.

## Verification 1 — `scripts/verify-ci-218-hfi-v1.py` (two independent artifact domains, 35/35 PASS)

Reproduce: `python -X utf8 scripts/verify-ci-218-hfi-v1.py 42743495901`
→ `CI218_HFI_V1_VERIFY: PASS (35 checks, two independent artifacts agree)`

- **Domain A (`-logs`)**: `enchilada/logs/archive-23-build-enchilada-adreno.log`
  is exactly `PASS: libEnchiladaAdreno.a (6 objects, sha256 c39ed4de2c954b34)`
  — **6 objects, not the 5 of CI #217**. All 9 `build-enchilada-` logs report
  success (7 force-loaded + 2 exempt), 23 archive logs have no hard failure
  marker, `kernel.log` is 36,731,148 chars with no `Undefined symbols`. An
  assumption check confirms the `-logs` artifact has NO `.json` build reports
  (verified against cached `ci217-logs.zip`) — so the per-object proof lives in
  Domain B, not in the logs.
- **Domain B (`-kernel`)**: `enchilada/kernel/kernel.macho` is a 126,017,536-byte
  Mach-O 64 (`0xfeedfacf`) containing all **nine** `_AdrenoHfiV1*` symbols
  (`InitializeStopped`, `MakeHeader`, `Send`, `Poll`, `Exchange`, `StartupTable`,
  `StartupStepCount`, `StepName`, `IsLegacyStart`) each exactly once, plus the
  legacy step-name strings `PERF_TABLE_V1`, `FW_VERSION`, `BW_TABLE`, the archive
  name, the `enchilada-adreno` boot-arg and `_enchilada_adreno_poll`. A symbol
  with no kernel caller can still be linked via `-force_load`; its presence is
  decisive proof the kernel clang compiled this exact source. The four older
  module prefixes remain (no regression).

## Verification 2 — `scripts/verify-ci-218-differential.py` (differential vs the known-negative CI #217 baseline, 24/24 PASS)

Reproduce: `python -X utf8 scripts/verify-ci-218-differential.py 42726356348 42743495901`
→ `CI218_DIFFERENTIAL_VERIFY: PASS (24 checks, new evidence discriminates against the CI #217 baseline)`

Verification 1 alone could pass on a stale artifact. This second, independent
domain proves the checks DISCRIMINATE by running them against CI #217 (the
round-30 tree, 5 objects, NO `AdrenoHfiV1.c`):

| Check | CI #217 (baseline) | CI #218 (new) |
|---|---|---|
| adreno archive PASS line | `5 objects, sha256 24b9b6eec7f70413` | `6 objects, sha256 c39ed4de2c954b34` |
| all 9 `_AdrenoHfiV1*` symbols in `kernel.macho` | 0 | 1 each |
| `PERF_TABLE_V1` / `FW_VERSION` strings | 0 | 1 each |
| kernel.macho size | 126,013,440 B | 126,017,536 B (+4,096 B) |
| 4 older module prefixes + `_enchilada_adreno_poll` | present | present (non-regression) |

If the same checks passed on both runs, the "evidence" would be vacuous; they do
not. The archive sha changed, the image grew by exactly one object, and every
new symbol is absent in the baseline and present in the new image.

## Honest boundary (unchanged)

- This is an OBSERVED CI BUILD + LINK, not a device measurement. gpu area stays
  `host-verified`.
- `AdrenoHfiV1.c` still has **NO kernel caller**, no coherent 3-page HFI shared
  extent, no GMU IOVA, no notify interrupt wiring, no firmware blob, no real HFI
  exchange. `CONFIG_XNU_NATIVE_ADRENO=n` is unchanged; there is still no
  A6XX_GEN1 backend, no CP ring, no GMU/HFI start, no firmware load.
- `force_load` + `weak_import` means the kernel will not fail if the probe never
  runs; the `enchilada-adreno=1` boot-arg was NOT consumed.
- Device is DISCONNECTED (Round 22 onward: `Get-CimInstance Win32_SerialPort`=0,
  `.NET SerialPort.GetPortNames()`=0, PnP COM3/4/5 all `present=False`, no
  SERIALCOMM registry key). Nothing was flashed, no register was read, no GMU
  started.
- The Round 22/28 power-domain prohibitions on `0x5000000` / `0x506a000` /
  `0x5040000` are UNCHANGED; the first legal device action remains the single
  gated read of `0x05091540` (CX) / `0x05091010` (GX), behind a boot-arg and a
  watchdog. The 3 vendor-signed ZAP blobs (`a630_zap.mdt/.b00/.b01`,
  `GPU_PAS_ID 13`) are still absent.

## Reproduce the whole chain

```
python -X utf8 scripts/verify-ci-218-hfi-v1.py 42743495901
python -X utf8 scripts/verify-ci-218-differential.py 42726356348 42743495901
python -X utf8 scripts/verify-ci-217-adreno.py 42726356348     # round-30 baseline
python -X utf8 scripts/verify-enchilada-hfi-v1.py               # round-31 source cross
```

Artifacts cached under `work/tmp/ci{217,42743495901}-{logs,kernel}.zip`
(`[IO.Compression.ZipFile]` / Python `zipfile`; `Expand-Archive` is broken).
GitHub API needs `$env:NO_PROXY='*'` and the `Bearer` token from
`oneplus6-ios/work/github-token.txt`; the `/zip` 302 to Azure requires dropping
the Authorization header on redirect.
