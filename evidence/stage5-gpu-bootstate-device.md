# ROUND 49/50 · Date: 2026-11-11 · Conditional GMU boot-state probe, stage 2 — DEVICE-VERIFIED (flash #10 + #11)

**What was built.** Stage 2 of `package/EnchiladaAdrenoProbe.cpp` (`enchilada-adreno=2`): the
probe gates a single GMU MMIO observation on the stage-1 CX power-domain verdict. CX down →
`stage2=cx-down-gmu-not-mapped` (never maps the GMU window — the expected steady state per
round 35). CX up → gate-check the GMU window (`ADRENO_BOARD_GMU_BASE 0x506a000`,
`ADRENO_BOARD_GMU_SIZE 0x30000`), map it once, read `ADRENO_GMU_BOOT_V1_GENERAL_7` (word
0x51cc, byte 0x14730), dsb, and decode warm/cold via `AdrenoGmuBootV1BootState`. The
observation is strictly READ-ONLY: zero MMIO writes in the stage-2 path (shim-checked
`PROBE_ZERO_WRITE`).

**Ring-wrap replay fix (flash #10 → #11 lesson).** Flash #10 reproduced the stage-1 verdict
on a new boot (604 identical replay rows) but its one-shot live stage-2 line (t≈295) wrapped
out of the 128 KiB msgbuf ring before retention began (first content t=323). Fix: the stage-2
row is now cached in `gGmuReplay` and replayed 1 Hz alongside the stage-1 row (`gDone`
branch, two IOLog calls per tick). Same failure class as the gauge240 saga (b267/b270:
"one-shot early IOLog is unrecoverable → use 1 Hz replay").

**Device-verified result (flash #11, CI #226 kernel, replay fix).** New boot
`boot_id 0x20c89316` (550015766 — new vs 0x205050d2/0x20df20d6/0x20e344b3), uptime 899 s,
write_errors 0. Recovered via the manual TWRP dd window procedure
(`dd bs=4096 skip=13207739 count=8192` on userdata) and decoded
(`read-enchilada-log.py --decode` → `work/log/stage2probe/untagged-20c89316`):

- stage-1 row ×410: `enchilada-adreno: rc=0 GPUCC=5090000 size=9000 cx=00005ff4 gx=00088400 cx_on=0 gx_on=0 gx_off=1 verdict=cx-DOWN steps_left=9 (replay t=490..899 attempt=1)`
- stage-2 row ×410: `enchilada-adreno: stage2=cx-down-gmu-not-mapped (replay t=490..899 attempt=1)`
- rows strictly consecutive, paired ticks, identical payloads (modulo the tick counter);
  ring-wrap interleaving (userspace lines glued onto replay rows) handled by the verifiers.
- `stage2=gmu-read` absent (CX still down → gate refused, as designed); `stage2=gmu-write`
  absent anywhere (zero-write contract held on hardware).

**Two verifications (m03546).**
1. `STAGE2_DEVICE_VERIFY_1: PASS (12 checks, 0 failures)` — raw-binary domain
   (`scripts/verify-stage2-device-1.py`): stage-1 line regex fields, either-branch stage-2
   line, header magic/version/write_errors, NEW boot_id, no `stage2=gmu-write` in raw bytes.
2. `STAGE2_DEVICE_VERIFY_2: PASS (16 checks, 0 failures)` — decoded-text domain
   (`scripts/verify-stage2-device-2.py`): header key=value, per-line walk, statistical
   invariants across all replays (identical payload + consecutive paired ticks), exactly one
   stage-2 branch, zero-write property verified FROM SOURCE (no wr32 between gate and read).

**Flash chain (honest).** flash #10 (CI #225 kernel `1b43c2ef…`, img `f68fb594…`, boot
0x20df20d6): stage-1 reproduced, stage-2 row lost to ring wrap — a REAL defect found by the
device, fixed in code (replay), re-verified on the next flash. flash #11 (CI #226 kernel
`5e427af7…`, img `e924571b…`, boot 0x20c89316): both rows replay ×410. Stale-cache defect
caught before flash #11 (`make_variants.build_kernel` reuses a cached patched kernel by
filename; sha16 printed by the build is the check).

**Honest boundary.** gpu area STAYS **host-verified** with this as a device-verified narrow
sub-result: the probe is observation-only, CX/GX are still DOWN (no power-up attempt), and
the powered-GMU boot chain (`AdrenoHfiV1/FirmwareV1/BootV1/BusV1/CallerV1`) remains
compiled-not-executed (CONFIG_XNU_NATIVE_ADRENO=n; needs a CX/GX genpd + clocks backend in
the XNU kernel, then a kgsl SMMU stage-1 context — future work). No flash of the general
system; probe payloads only, auto-return to TWRP verified twice.
