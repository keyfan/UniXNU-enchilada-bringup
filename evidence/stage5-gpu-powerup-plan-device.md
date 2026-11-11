# Stage 5 GPU — stage-3 ordered power-up PLAN kernel probe (ROUND 52, 2026-11-11)

DEVICE-VERIFIED NARROW SUB-RESULT. The gpu AREA status STAYS `host-verified`:
this is an observation-only 1 Hz replay of a pure `Plan()` computation, NOT a
GPU driver, NOT a powered GMU, and NOT an official completion criterion.

## What was built

`package/EnchiladaAdrenoProbe.cpp` stage 3 (`enchilada-adreno=3`) is the FIRST
real XNU kernel caller of the round-51 `AdrenoPowerUpV1` model (the 11th
archive object). It is:

* default OFF (boot-arg `enchilada-adreno` must equal 3; stages 1/2 unchanged);
* READ-ONLY: the only hardware access is the two already device-verified GPUCC
  GDSC status reads at 0x05091540 / 0x05091010 behind the 5-predicate safety
  gate; ZERO MMIO writes;
* PURE: it calls only `AdrenoPowerUpV1InputsValid`, `...CountsAreDistinct`,
  `...PlanSize`, `...Plan`, `...PreIsReady` and the three board-fact getters.
  It NEVER calls `AdrenoPowerUpV1Execute`, NEVER wires an
  `AdrenoPowerUpV1Bus`, and NEVER invokes `genpd_on` / `genpd_off` /
  `set_rate` / `clocks_enable`. No regulator or clock is changed.
* composition-honest: the `AdrenoGmuCallerV1Pre` is built from the
  DEVICE-MEASURED CX/GX words with `clocks_enabled = 0` (this port enables
  none), so `PreIsReady` is expected to refuse (`ready=0`).
* replay-safe: after the one-shot computation, the row is cached and re-emitted
  at 1 Hz from the `gDone` branch (round-49/50 ring-wrap fix), alongside the
  stage-1 and stage-2 rows.

The plan row pins the exact `a6xx_gmu.c:1033-1047` order as DATA, not as side
effects: 5 steps, guarded GX, core 200000000 Hz, A630 hub 150000000 Hz
(non-A740 branch), 4 GMU clocks (distinct from the 6 kgsl-3d0 clocks).

## CI and pre-flash verification (two independent paths, m03546)

* CI #228 run `42976742096` on `device_ref=main` concluded success (two stacks:
  poller `CI228_FINAL: 0`, .NET `DOTNET_VERIFY_CI228: PASS`; job build
  completed/success steps=27). `libEnchiladaAdreno.a` = 11 objects, sha256
  prefix `d2f35797698a7ba2`; `kernel.macho` 126,038,016 B sha256
  `3a361e3b54170e3ac8fec6cf653f6675380088a52b1866e9be7f9abc03032976`;
  no 10/12-object regression, no `Undefined symbols`, no `stage3=exec`.
  `scripts/verify-ci-228-adreno.py` -> `CI228_ADRENO_VERIFY: PASS (15 checks)`.
* Payload `work/out/payload-stage3probe.img` 150,994,944 B sha256
  `7ad5e16e6eae694020c944b4c78f70e018e78e5dbe4c0a0aef7d630f8908b16d`,
  built from the CI #228 kernel (patched kernel sha16 `007a7f5eb890`; the
  stale `console-k223.macho` cache was deleted first, flash #11 lesson).
  Args = flash #11 line with ONE variable `enchilada-adreno=2` -> `=3`,
  49 tokens / 994 of 1000 bytes, `enchilada-deadline=900`,
  `enchilada-wdog=3`.
* Pre-flash verifier 1 `work/tmp/_stage3_verify1.py` -> `VERIFY1_IMG_ROUNDTRIP_STAGE3: PASS`
  (img round-trip, exactly one `=3`, no `=2`, kernel sha == cached patched
  kernel, 3 kernel symbols). Pre-flash verifier 2
  `scripts/verify-enchilada-probe-stage3.py` -> 40/40 checks PASS (pure Plan +
  composition gate, ZERO MMIO writes). Full host suite 11 suites PASS.
* Both device verifiers were dry-run against a SYNTHETIC log first; one
  checker defect was found and fixed (`src.find("gDone = true")` matched an
  early-return branch -> `rfind`), then 25 + 35 synthetic checks PASS.

## Device result — flash #12 (m39510 standing authorization, USED this round)

`test-cycle.py stage3probe --wait-return 900`: boot-recovery misc 0.052 s,
system_b payload 4.878 s, fastboot boot boot-108.img 0.144 s; the 900 s
wait expired before the watchdog auto-return (expected), hostwatch captured
720 s, and the phone returned to TWRP recovery on its own at 15:50:36.
Strictly READ-ONLY recovery: `dd if=/dev/block/by-name/userdata bs=4096
skip=13207739 count=8192` -> 33,554,432 B window -> `read-enchilada-log.py
--decode` -> `work/log/stage3probe/untagged-2101566c`.

Header: `magic=SHLOG001`, `version=4`, **`boot_id=0x2101566c` (553735788,
NEW; flash #11 was 0x20c89316)**, `uptime_s=899`, `slots_written=1731`,
`write_errors=0`, `state=2`.

THREE paired 1 Hz replay rows, 176 each, ticks 724..899 (strictly
consecutive, t2==t1 and t3==t1):

```
enchilada-adreno: rc=0 GPUCC=5090000 size=9000 cx=00005ff4 gx=00088400 cx_on=0 gx_on=0 gx_off=1 verdict=cx-DOWN steps_left=9 (replay t=N attempt=1)
enchilada-adreno: stage2=cx-down-gmu-not-mapped (replay t=N attempt=1)
enchilada-adreno: stage3=plan steps=5 gxpd_guarded=1 core=200000000 hub=150000000 clocks=4 valid=1 distinct=1 size=5 plan_rc=0 ready=0 mcx=0 mclk=0 (replay t=N attempt=1)
```

`mcx=0` reproduces the round-35 CX-DOWN steady state; `ready=0` is the honest
composition gate (no clocks enabled); no `stage3=exec`, no GMU-read branch,
no genpd/clock callback line anywhere.

## Two independent DEVICE verifications (m03546)

* `scripts/verify-stage3-device-1.py` -> `STAGE3_DEVICE_VERIFY_1: PASS
  (25 checks, 0 failures)` — raw binary + header + line scan, expected plan
  fields, NEW boot_id, no exec/callback lines.
* `scripts/verify-stage3-device-2.py` -> `STAGE3_DEVICE_VERIFY_2: PASS
  (35 checks, 0 failures)` — decoded text with a DIFFERENT method: payload
  identity after stripping `(replay t=N attempt=1)`, consecutive 1 Hz ticks,
  paired S1/S2/S3, and the comment-stripped source segment proving the
  stage-3 block calls ONLY the 8 pure helpers.

## Honest boundary (load-bearing)

This is ONE physical boot plus a READ-ONLY 1 Hz text replay, NOT 176
independent measurements. It proves the round-51 ordered power-up MODEL is
linked into the shipped XNU kernel and that its pure `Plan()` emits the exact
5-op upstream order on the real machine. It does NOT power the GPU, enable a
clock or regulator, load GMU firmware, start HFI, program the kgsl SMMU
stage-1 context, submit a command or render an image. `CONFIG_XNU_NATIVE_ADRENO`
remains `n`, the gpu area `status` STAYS `host-verified`, and the official
completion criteria remain unmet. Remaining strict-order work: a real XNU
backend wiring the genpd/clock callbacks and INSTANTIATING the chain on a
POWERED GMU, TrustZone PAS auth (GPU_PAS_ID 13), the kgsl SMMU stage-1
context at 0x5040000 (streams must first fault), then network/audio/cellular
device verification.

Verification chain (remote): `70ce497a` (round 51 p2) -> `56d22a3a`
(round 52 phase 1: stage-3 probe + host verifier) -> this record.
