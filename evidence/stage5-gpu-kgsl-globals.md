# Stage 5 GPU — the vendor kgsl driver has allocated its GPU objects but assigned ZERO IOVAs (ROUND 80)

**Status: DEVICE-SOURCED, from a briefly-mounted debugfs. READ-ONLY in content.**

This round mounted `debugfs` for the single purpose of reading the kgsl driver's
global-object table, then **unmounted it and verified the mount state was restored
exactly**. Round 79 named this surface as the next thing to read; this round read
it, and what it shows is the strongest direct evidence yet for why the GPU VA map
is not obtainable.

No GPU register was read or written, no MMIO was issued, nothing was flashed,
nothing was mounted read-write, and no file was created on the device.

## The mount, and its exact before/after state

| phase | observed |
| --- | --- |
| **before** | `mount \| grep -c debugfs` → **`0`**; `ls /sys/kernel/debug \| wc -l` → **`0`** |
| mount | `mount -t debugfs none /sys/kernel/debug` → `RC=0`; result `none on /sys/kernel/debug type debugfs (rw,seclabel,relatime)` |
| reads | `ls` and `cat` under `/sys/kernel/debug/kgsl` only |
| **after** | `umount /sys/kernel/debug` → `UMNT_RC=0`; `mount \| grep -c debugfs` → **`0`**; `ls /sys/kernel/debug \| wc -l` → **`0`** |

The device is byte-for-byte in the state it started in. The only thing that
changed during the window was a mount that has been removed.

## What the driver reports

### `active_cnt = 0`, and `ctx/` is empty

The per-device debugfs directory exposes `active_cnt`, five `log_level_*` knobs
(all `3`), `profiling`, and a `ctx` directory that **contains no entries**. So
the kgsl driver holds **zero active kernel contexts** on this boot.

### The global-object table — every IOVA is zero

`/sys/kernel/debug/kgsl/globals`, 17 entries, format `start-end size name`:

```
0x0000000000000000-0x0000000000000000             4096 setstate
0x0000000000000000-0x0000000000000000           262144 gpu-qdss
0x0000000000000000-0x0000000000000000            32768 memstore
0x0000000000000000-0x0000000000000000             4096 scratch
0x0000000000000000-0x0000000000000000             4096 pagetable_desc
0x0000000000000000-0x0000000000000000             4096 profile_desc
0x0000000000000000-0x0000000000000000            32768 ringbuffer
0x0000000000000000-0x0000000000000000             4096 pagetable_desc
0x0000000000000000-0x0000000000000000             4096 profile_desc
0x0000000000000000-0x0000000000000000            32768 ringbuffer
0x0000000000000000-0x0000000000000000             4096 pagetable_desc
0x0000000000000000-0x0000000000000000             4096 profile_desc
0x0000000000000000-0x0000000000000000            32768 ringbuffer
0x0000000000000000-0x0000000000000000             4096 pagetable_desc
0x0000000000000000-0x0000000000000000             4096 profile_desc
0x0000000000000000-0x0000000000000000            32768 ringbuffer
0x0000000000000000-0x0000000000000000           196608 profile
```

Two separate facts live in that table, and they must not be conflated:

**Fact 1 — the objects EXIST.** The driver has allocated `setstate`, `gpu-qdss`,
`memstore`, `scratch`, and **four** complete ring sets (`pagetable_desc` +
`profile_desc` + `ringbuffer` each), plus a 196,608-byte `profile` buffer. Four
ring sets means four rings were instantiated. The allocation path works on this
device.

**Fact 2 — every one of them has IOVA `0x0-0x0`.** Not one object carries a
non-zero GPU virtual address. The sizes are real; the addresses are all zero.

### `strict_memory` is `0`

`/sys/kernel/debug/kgsl/debug/strict_memory` reads `0`, i.e. the driver's strict
memory accounting is **off** — so the table's zero IOVAs are not a display or
accounting artefact of that mode.

## What this establishes, and what it does not

**Establishes — this is the point of the round:** on this boot the vendor driver
allocated GPU objects and **assigned no GPU virtual addresses to any of them**.
That is a *measured* confirmation of two things this port had only reasoned about:

- Round 77 derived that the GPU VA aperture comes from the SMMU's `ID2.UBS` field
  read at probe, and that without a powered, programmed kgsl SMMU there is no
  aperture to speak of. The all-zero table is what "no aperture in use" looks like
  from the driver's own side.
- Round 53 measured the target streams still in the firmware **BYPASS** handoff
  (`Busy=10`, `id0=4c017e4c`). A stream in bypass never reaches translation, which
  is consistent with objects that were allocated but never mapped.

Combined with round 79's `reset_count = 0`, `gpu_clock_stats` all zero and
`gpu_busy_percentage = 0`, the picture is now complete and independent: **the GPU
is bound, its driver allocated everything it needs, and it has never been asked to
run anything.**

**Does NOT establish:** why the IOVAs are zero. Two explanations are consistent
with the data — (a) the driver defers IOVA assignment until a context is submitted
work, and nothing has been submitted; (b) the kgsl SMMU stage-1 context was never
programmed on this boot. **This observation cannot distinguish them**, and this
document does not claim it can. Deciding between them would need a submission
attempt, which is a state change and would need its own explicit authorization.

Also **not established:** that the A630 GPU VA aperture is any particular value.
The table says nothing about where GPU addresses *would* be; it says only that
none are currently in use. The round-77 derivation (and its three blockers) is
unchanged.

## Why this matters for the port

Rounds 69-79 kept recording `board-va-map` as "the missing input". This round
shows what that absence looks like at runtime: **four rings and eleven other
global objects, every single one at IOVA zero.** A port that wanted to copy a
working VA layout from the vendor driver would find nothing to copy — because on
this boot the vendor driver is not using any GPU virtual addresses either.

That is a sharper statement of the blocker than "it is absent": the reference
implementation is itself sitting at zero.

## Reproduce

```
C:\platform-tools\adb.exe shell "mount -t debugfs none /sys/kernel/debug"
C:\platform-tools\adb.exe shell "cat /sys/kernel/debug/kgsl/kgsl-3d0/active_cnt"
C:\platform-tools\adb.exe shell "cat /sys/kernel/debug/kgsl/globals"
C:\platform-tools\adb.exe shell "cat /sys/kernel/debug/kgsl/debug/strict_memory"
C:\platform-tools\adb.exe shell "umount /sys/kernel/debug"
```

## Honest boundary

DEVICE-SOURCED DRIVER INTERNAL STATE, read through a briefly-mounted debugfs that
was then removed and verified removed. No GPU register read or write, no MMIO, no
ioctl, no submission, no firmware load, no PAS, no SMMU programming, no flash, no
reboot, no read-write mount, no file created on the device. `m39510` NOT exercised.
`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The A630 client GPU windows stay all-zero.
The four hard blockers of `docs/porting/gpu.md` §1 remain open. The `gpu` area
**STAYS `"host-verified"`**. It does **not** unblock stage 5.

Chain: round 79 closed at `542b880b84f92fbb474dc2daaa5cd0eb0e417b99`.
