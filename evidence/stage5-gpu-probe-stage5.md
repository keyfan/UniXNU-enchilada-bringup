# Round 95 — stage 5: the IOKit caller that finally connects the power-up chain

**Status: KERNEL PROBE, HOST-VERIFIED + ARMED, NOT YET RUN ON HARDWARE.**

Rounds 51/56/57/58/59/61/62 derived the ordered CX/GX/clock plan; round 62 gave it
an **executor**; round 64 added a **caller** that brackets the executor with a
read-only pre-observation. Every one of those layers spoke through **callbacks** —
and nothing in this tree ever connected them to a mapped window. Stage 5 is that
connection, and it is the **only stage of this probe that writes**.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area **STAYS `"host-verified"`**.
No flash, `m39510` NOT exercised. It does **not** unblock stage 5.

## Why stage 5 had to exist

`AdrenoPowerUpBackendV1`'s own header states the gap: it "performs MMIO *through the
caller's callbacks* and does nothing else: it maps nothing, owns no window, enables
no regulator, runs no TrustZone PAS, and has no IOKit caller of its own." Round 64's
`AdrenoCallerV1` has the same boundary. So the executor was real and **unreachable**:
arming it required a caller that owns a mapped GPUCC window and supplies a delay,
and no such caller was written until now.

## What stage 5 does

1. **Re-checks the same gate stages 1–4 use.** `gpucc_window_is_safe()` must pass,
   and the window is bound only *after* it does. A probe that trusted an earlier
   stage's opinion of the mapping would be one refactor away from writing through a
   wrong base.
2. **Binds the window adapter** (`AdrenoGpuccWindowV1Bind`) over the mapping stages
   1–4 already hold, and **supplies a real delay** (`AdrenoGpuccWindowV1SetDelay` →
   `IODelay`). The window refuses an MMIO triple with a NULL delay for a stated
   reason: the GDSC plan genuinely waits between its collapse-clear write and the
   status poll. `IODelay` is the kernel's own busy-wait, **not** a spin loop
   calibrated by guesswork — the distinction the window header draws explicitly.
3. **Obtains the MMIO triple** from the window (bounded, gated), so every write goes
   through `AdrenoGpuccWindowV1`'s bounds rather than a raw pointer.
4. **Runs the caller** with board facts, not magic: `armed = 1`, `gxpd_present = 1`,
   `rcg_enabled = 0` (this port has never started `gmu_clk_src`),
   `core_rate_hz = 200000000` (the pinned `a6xx_gmu.c` value), `strict_gx = 0`
   (GX-OFF is the measured steady state).
5. **Reports three independent verdicts separately**, which is the whole point:
   - `up_status` — did the five ordered ops execute?
   - `precheck_status` — does the CX word now say the domain is up?
   - `cx_before` / `cx_after` / `cx_on_after` — the two status words, so the delta is
     visible and cannot be inferred from a success flag.

## The distinction that matters most

`Up()` succeeding means **the writes happened**. Whether CX is now **ON** is a
different question, answered by re-reading the status word. Round 35 measured
`cx=0x00005ff4` with BIT(31) clear, and **nothing proves the collapse clear raises
it on this boot path**. So a run that reports `up=Ok` with `cx_on_after=0` is a
perfectly possible outcome, and collapsing those two into one "success" is exactly
how a port invents a fact. The row keeps them apart.

## Safety properties this stage preserves

- **No direct store.** The probe still contains no `*ptr = …` anywhere; every write
  goes through the window adapter's callback. The verifier asserts this by parsing
  the comment-stripped source, and separately asserts the only pointer casts in the
  file remain the two `volatile` window types.
- **No invented delay loop.** A calibrated spin-wait would be worse than none.
- **No direct `write32` call.** `mmio.write32(` never appears; the triple is passed
  by pointer into the caller.
- **Default-off.** `enchilada-adreno=5` arms it; absent = no-op; the mask is
  `stage < 1u || stage > 5u`, so a future stage cannot slip in unnoticed.

## Wiring

`board.toml`: `enchilada-adreno=4` → `enchilada-adreno=5` (same byte count, the
joined command line stays **970/1000**).

## Verification

| check | result |
| --- | --- |
| `check-enchilada-adreno-probe.py` | **18/18** (strict C++17 arm64, callback-only writes, no unexpected undefined symbols) |
| `verify-enchilada-probe-stage5.py` | **38/38** |
| `verify-enchilada-probe-stage4.py` | updated to the monotonic mask |
| `verify-enchilada-probe-stage3.py` | **40/40** (mask shape + "every cast is volatile") |
| `verify-gpuprobe-device-2.py` | refined: the literal `"WRITE" not in src` test could not survive stage 5, so it now asserts the *precise* property (no direct store, no direct `write32` call, window adapter present) |
| full tree | **63/64**; the 64th (`verify-enchilada-gmu-caller-v1-record.py`) needs a 40-hex revision by design |

## Honest boundary

**This has not been flashed or run.** It has never written a register. When it does
run, three outcomes are possible and the row distinguishes them:

- `up=Ok, cx_on_after=1` — the domain came up. That would be the first time this
  port powered the GPU, and it would unblock stage 4's ID2 read on the next boot.
- `up=Ok, cx_on_after=0` — the five ops executed and **the rail did not rise**. This
  is a real hardware finding, not a bug in the plan.
- `up=<not Ok>` — an op was refused; `up_fail_index` names which.

Even the best outcome does **not** satisfy `kgsl-smmu-stage1`: that still needs an
ASID allocator and a populated LPAE table. The admission table remains
**modeled = 9 / satisfied = 0 / BLOCKED**.

The round-22/28 prohibition is unchanged and is **not** enforced here by
construction: this stage writes `gpu_cx_gdsc`'s own control register, which lives in
always-on GPUCC and is exactly how a collapsed domain is raised. The gate that must
run *before* any GPU/GMU/SMMU window read is still `AdrenoGmuCallerV1Precheck`.
