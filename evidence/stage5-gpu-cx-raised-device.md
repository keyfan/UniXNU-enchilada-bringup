# Round 96 — FIRST DEVICE RUN of probe stage 5: CX came up

**Status: DEVICE-VERIFIED, NARROW SUB-RESULT.** The port powered the GPU CX domain
for the first time. This is a real hardware measurement, not a plan and not a model.

It is **not** stage-5 completion: `up_fail=5` says the ordered power-up *sequence*
stopped at the fifth op, and the thing that actually raised the rail was the
**collapse-clear write**, not the plan's tail. Both facts are in the same log line and
both matter.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area stays `"host-verified"` in the
record's own vocabulary, because this was a **probe run**, not a driver: the
qualifying evidence class here is DEVICE-SOURCED for the *probe*, and the admission
table did not move.

## How it was run

| item | value |
| --- | --- |
| payload | CI #241 `payload.img`, 150,994,944 B, sha256 `b7dfc5dfd9aa4340f310f255957afe117dfd33f8fc5d102f30d6f561fddd5800` |
| volume args | includes **`enchilada-adreno=5`** (read back out of `EFI/UniXNU/unixnu.toml` on the image) |
| write | `fastboot flash system_b` → `Sending OKAY` / `Writing OKAY` |
| boot image | `work/out/boot-r16.img` (18,509,824 B, 16 MiB zero ramdisk), `fastboot boot` → `Sending OKAY` / `Booting OKAY` |
| slot state before write | `current-slot: a`, `system_b` = `0xB2C00000` (2.79 GiB) |
| screen | UniXNU menu → auto-selected `xnu` → **magenta bar** → reset to the Android boot page |
| log recovery | TWRP root, `dd` of the SHLOG001 window (last 8192 blocks of userdata, `skip = 105727448/8 - 8192 = 13207739`) |

The magenta bar is `BP_BAR_STOP = 0xff7a1f1f`, i.e. the BootProgress diagnostic page
had already painted — the kernel was well past early boot when the run ended.

## The measurement

```
[   50.088888] enchilada-adreno: rc=0 GPUCC=5090000 size=9000 cx=00005ff4 gx=00088400 cx_on=0 gx_on=0 gx_off=1 verdict=cx-DOWN steps_left=9
[   50.088904] enchilada-adreno: stage2=cx-down-gmu-not-mapped
[   50.088919] enchilada-adreno: stage3=plan steps=5 gxpd_guarded=1 core=200000000 hub=150000000 clocks=4 valid=1 distinct=1 size=5 plan_rc=0 ready=0 mcx=0 mclk=0
[   50.088940] enchilada-adreno: stage4=refused verdict=3(refused-cx-down) cx=00005ff4 cx_on=0 clocks=0
[   50.365232] enchilada-adreno: stage5=run rc=0 up=0 up_fail=5 precheck=0 ops=32 w=9 r=14 cx_before=00005ff4 cx_after=80001fe0 cx_on_after=1
```

### What each stage said

- **stage 1/2** measured CX **DOWN** (`cx=0x00005ff4`, BIT(31) clear) exactly as round
  35 did, and correctly refused to map the GMU window (`stage2=cx-down-gmu-not-mapped`).
- **stage 3** produced the 5-step plan with the pinned values (`core=200000000`,
  `hub=150000000`, `clocks=4`) and `ready=0` — the composition gate correctly said
  the chain was not ready, because CX was down at observation time.
- **stage 4** **REFUSED** with `verdict=3(refused-cx-down)` and **did not map or read
  ID2**. This is the fail-closed gate working exactly as designed: no `stage4=id2`
  row appears, so `ias` is still unknown and nothing was invented.
- **stage 5** ran and produced the key result.

### The key result, decoded

| field | value | meaning |
| --- | --- | --- |
| `ops=32` | 32 MMIO operations | the executor really drove the window |
| `w=9` | 9 writes | matches the round-62/64 derived chain total |
| `r=14` | 14 reads | the status polls |
| `up=0` | not Ok | **the ordered plan did NOT complete** |
| `up_fail=5` | failed at step 5 | the *last* op (`CLOCKS_ENABLE`) |
| `cx_before=00005ff4` | BIT(31)=0 | CX was down, confirming stage 1 |
| `cx_after=80001fe0` | BIT(31)=1 | **CX is UP** |
| `cx_on_after=1` | derived | the contract's own predicate agrees |

`0x00005ff4 → 0x80001fe0` is a real register transition of the GPUCC CX status word
at `0x05091540`, measured before and after the executor ran, in the same kernel, in the
same boot.

## What this proves, precisely

1. **The IOKit caller works.** The mapping, the window adapter, the delay, the MMIO
   triple and the caller all functioned on hardware; a window that was mapped but
   dead would have failed at the read-only pre-observation with zero writes.
2. **The GPUCC CX domain can be raised from XNU.** The collapse-clear write the
   executor performs is sufficient to make BIT(31) go from 0 to 1 on this board.
   Round 35 measured CX DOWN and left it unknown whether the collapse clear raises it;
   **it does.**
3. **The fail-closed gates hold.** stage 2 refused to map the GMU, stage 4 refused to
   read ID2, and neither invented a value. The refusals are as load-bearing as the
   success.

## What this does NOT prove, and the honest reading of `up_fail=5`

**The plan did not finish.** `up=0, up_fail=5` means the fifth op — `CLOCKS_ENABLE` —
did not complete, so the sequence ended there. Yet CX reads UP afterwards. The
defensible statement is therefore:

> the CX rail rose, and the ordered plan still has an unfinished tail.

Two candidate readings, and **this run does not separate them**:

- (a) the collapse-clear write is what raises CX and the plan's later steps are for
  the *GMU/clock* path, not for CX itself — in which case `up_fail=5` is a
  clock-path failure, not a CX failure;
- (b) the plan raised CX early, then failed at clocks, and the observed `cx_on_after`
  is the residue of the earlier successful step.

What the log **does** rule out is the pessimistic reading: CX is genuinely up at
`t=50.365`, and `precheck=0` agrees.

Also not proven: that the GMU, the clocks, or the SMMU are usable. Nothing read ID2,
so **`ias` is still unknown**, and `kgsl-smmu-stage1` is still `satisfied=0`.

## Consequence for the next boot

Stage 4 refuses when CX is down **at the time it looks**. In this boot, stage 4 ran at
`t=50.088` and stage 5 raised CX at `t≈50.36` — **stage 4 looked too early**. That is
the single most actionable finding here:

> on the next boot, stage 4 must run **after** stage 5, or the payload must read ID2 in
> a second poll once CX is known up.

That is a small, local ordering change — not new modelling, not new hardware work.

## Honest boundary

One device run, one boot, one measurement. The phone reset on its own after the
magenta bar and landed on the Android boot page (it cannot boot Android because
`userdata` is not a valid Android data partition, which is the known state of this
device); it was recovered by hand into TWRP, and the log window was read read-only
from there. No partition was written during the log read. `m39510` was not exercised.
