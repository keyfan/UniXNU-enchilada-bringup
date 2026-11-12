# Stage 5 GPU — the LIVE kgsl driver's own view of this A630 (ROUND 79)

**Status: DEVICE-SOURCED, read-only.** This is the first round that reads **the
vendor kgsl driver's own sysfs interface** on this phone, rather than the device
tree or a pulled firmware blob. It is a different and stronger class of evidence
than rounds 71 and 74: those read *static descriptions of the hardware*, this
reads **the running driver's state**.

No GPU register was read or written, no MMIO was issued, nothing was flashed,
nothing was mounted read-write. The `gpu` area of `porting.toml` **STAYS
`"host-verified"`** — see "Honest boundary" for why a readable sysfs attribute is
still not the port's own code running on the GPU.

## Session (fully read-only)

| item | value |
| --- | --- |
| device | `27f4f45d`, OnePlus A6000 / `OnePlus6` |
| environment | recovery (TWRP), `uid=0(root)` |
| operations | `ls` and `cat` over `/sys/class/kgsl/` only |
| NOT done | no mount, no write, no `echo >`, no ioctl, no MMIO, no flash, no reboot |

Importantly: **this is the kernel's kgsl driver, already bound and live.** The
interface exists because the *vendor* kernel brought the GPU up — not because
anything from this port ran.

## What the live driver reports

### Identity — and a discrepancy worth keeping

| attribute | value |
| --- | --- |
| `gpu_model` | **`Adreno630v2`** |
| `device/uevent` OF_NAME | `qcom,kgsl-3d0` |
| OF_COMPATIBLE | `qcom,kgsl-3d0`, `qcom,kgsl-3d` |
| `dev` | `233:0` |
| `usesgmem` | `1` |
| `hwcg` | `1` |

**`Adreno630v2` is a name neither pinned source contains.** The Linux v6.12
catalog has exactly ONE A630 entry (`a6xx_catalog.c:882-903`: chip ids
`0x06030001`/`0x06030002`, `ADRENO_6XX_GEN1`, `revn 630`), and the vendored Mesa
table (`freedreno_devices.85fda3b3.py`) has exactly ONE `GPUId(630)`. The "v2"
suffix is a **downstream kgsl naming convention**, not a mainline catalog
distinguisher.

This matters because round 66/69 established that A630 reuses the generation-level
facts (`ADRENO_6XX_GEN1`) and that the *board-derived* DeviceInfo fields must come
from the board, never a guess. A vendor model string that no pinned source
contains is exactly the kind of fact that **must not be mapped onto a catalog
entry by assumption** — it is recorded here as an observation, and the port keeps
using the pinned catalog's identification. Whether "v2" implies different
register/firmware behaviour is **not established by this string** and is not
claimed.

### Clocks and power levels — 7 levels, and the low end is where this boot sits

| attribute | value |
| --- | --- |
| `gpu_available_frequencies` | `710000000 675000000 596000000 520000000 414000000 342000000 257000000` |
| `num_pwrlevels` | `7` |
| `min_pwrlevel` / `max_pwrlevel` | `6` / `0` |
| `default_pwrlevel` | `6` |
| `max_gpuclk` / `max_clock_mhz` | `710000000` / `710` |
| `gpuclk` / `clock_mhz` | **`257000000` / `257`** |
| `devfreq/cur_freq` | `257000000` |
| `devfreq/governor` | **`msm-adreno-tz`** |
| `devfreq/available_frequencies` | **the same 7, but ascending** (`257..710`) — the two interfaces list one table in opposite order |

The **7-level table and the 710 MHz ceiling are board facts this port had never
recorded**. Two things follow:

- The GPU is running at the **lowest** of its seven levels (257 MHz, which is also
  `min_pwrlevel 6` and `default_pwrlevel 6`) under the `msm-adreno-tz`
  thermal-aware governor, i.e. idle. This is the expected steady state for a
  recovery environment with no rendering.
- The DTB's `qcom,gpu-pwrlevels` list (which round 51 read for clock *names*) and
  the driver's runtime table agree at the endpoints, which is a cross-check the
  port did not previously have.

### Activity — the GPU is idle but live

| attribute | value | reading |
| --- | --- | --- |
| `gpu_busy_percentage` | `0 %` | no rendering in recovery |
| `gpubusy` | `0 0` | busy=0, total=0 |
| `gpu_clock_stats` | `0 0 0 0 0 0 0` | **zero time at every level** |
| `ifpc` / `ifpc_count` | `1` / `0x0` | inter-frame power collapse **enabled**, never entered |
| `sptp_pc` | `1` | SPTP power collapse enabled |
| `popp` | `0` | no power-optimised perf policy in use |
| `idle_timer` | `80` | 80 ms before idling |
| `reset_count` | `0` | **the GPU has never been reset on this boot** |
| `preempt_level` / `preemption` | `0x1` / `0` | preemption supported, not exercised |
| `power/runtime_status` | `unsupported` | this kernel has no runtime PM accounting for it |
| `power/runtime_active_time` / `_suspended_time` | `0` / `0` | consistent with the above |

`reset_count = 0` and `gpu_clock_stats` all zero together say the GPU has been
**bound and idle**, never driven. That is the strongest evidence yet that this
device's GPU is reachable and healthy — and simultaneously that **nothing has
exercised it**, which is exactly the state every earlier round described.

## What this changes, and what it does not

**Changes:** the port now has, for the first time, the vendor driver's own
identity string, the real 7-level frequency table, and a demonstrated idle-but-live
GPU with an enabled-ifpc/sptp_pc configuration. Rounds 71/74 could only describe
the hardware from the device tree and from pulled blobs; this is the running
driver agreeing with those descriptions and adding the clock table.

**Does NOT change:** `gpu_busy_percentage = 0`, `gpu_clock_stats` all zero and
`reset_count = 0` confirm the GPU has **never actually rendered anything** on this
boot. Reading a sysfs attribute is not issuing MMIO, and it is certainly not this
port's own code executing on the GPU. The four hard blockers of
`docs/porting/gpu.md` §1 remain open. The `gpu` area **STAYS `"host-verified"`**.

## Reproduce

```
C:\platform-tools\adb.exe shell "cat /sys/class/kgsl/kgsl-3d0/gpu_model"
C:\platform-tools\adb.exe shell "cat /sys/class/kgsl/kgsl-3d0/gpu_available_frequencies"
C:\platform-tools\adb.exe shell "cat /sys/class/kgsl/kgsl-3d0/reset_count"
C:\platform-tools\adb.exe shell "cat /sys/class/kgsl/kgsl-3d0/gpu_clock_stats"
```

## Verification

`scripts/verify-enchilada-kgsl-sysfs.py` re-reads all 28 attributes from the live
phone on every run and confronts them with both pinned sources:

- Domain A re-reads every attribute and compares it with the values recorded here.
- Domain B proves the `Adreno630v2` string is **absent from both** the pinned
  catalog and the vendored Mesa table, and that the document records the v2 suffix
  as a downstream naming convention without claiming what it implies.

`KGSL_SYSFS_VERIFY: PASS (61 checks, 0 failures)`.

## Honest boundary

READ-ONLY DEVICE-SOURCED DRIVER STATE. No GPU register read or write, no MMIO, no
ioctl, no firmware load, no PAS, no SMMU programming, no flash, no reboot, no
mount. `m39510` NOT exercised. `CONFIG_XNU_NATIVE_ADRENO` remains `n`. The A630
client GPU windows stay all-zero. The four hard blockers remain open. The `gpu`
area **STAYS `"host-verified"`**; this does **not** unblock stage 5.

Chain: round 78 closed at `494068ebfaa22ddd9d00cc45deb4f84039ea61eb`.
