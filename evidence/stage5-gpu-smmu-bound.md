# Stage 5 GPU — the kgsl SMMU IS bound, and 92 page-table pages already exist (ROUND 82)

**Status: DEVICE-SOURCED, from a briefly-mounted debugfs. READ-ONLY in content.**

This round corrects a claim the port has carried since round 75. Round 75's
`AdrenoSmmuStage1V1` recorded **I1 "no page table"** and **I3 "no ASID"** as
absences. Reading the SMMU's own debugfs and sysfs on this phone shows that
**the Linux `arm-smmu` driver is bound to the kgsl SMMU and is already holding 92
page-table pages.** The absence is real *for this port*, but the wording "no page
table exists" is wrong about the device, and that distinction matters.

No GPU register was read or written, no MMIO was issued, nothing was flashed,
nothing was mounted read-write, and no file was created on the device.

## The mount, and its exact before/after state

| phase | observed |
| --- | --- |
| **before** | `mount \| grep -c debugfs` → **`0`**; `/sys/kernel/debug` entries → **`0`** |
| mount | `mount -t debugfs none /sys/kernel/debug` → `RC=0` |
| reads | `ls`/`cat` under `/sys/kernel/debug/iommu`, `/sys/kernel/debug/kgsl`, and `/sys/devices/platform/soc/5040000.arm,smmu-kgsl` |
| **after** | `umount /sys/kernel/debug` → `UMNT_RC=0`; mounts **`0`**, entries **`0`** |

The device is byte-for-byte in its starting state.

## What the device shows

### The kgsl SMMU is BOUND to a real `arm-smmu` driver

```
/sys/bus/platform/drivers/arm-smmu/5040000.arm,smmu-kgsl
/sys/bus/platform/drivers/arm-smmu/15000000.apps-smmu
```

The vendor kernel ships the mainline `arm-smmu` driver and has bound it to
**both** SMMUs, including the dedicated kgsl one at `0x5040000` — the exact
device round 53's gate and round 75's word model were written about.

Its identity, read from `modalias`:

```
of:Narm,smmu-kgslT<NULL>Cqcom,smmu-v2
```

That confirms the compatible string this port derived in round 21 is what the
device actually matches on.

### 92 page-table pages already exist

```
/sys/kernel/debug/iommu/io-pgtable/pages   ->  92
```

The `io-pgtable` framework reports **92 live page-table pages**. This is a
direct, measured refutation of the phrasing "this port allocates and walks no
LPAE table" being read as "no LPAE table exists anywhere".

### Runtime PM says `unsupported`

Both the GPU (round 79) and the kgsl SMMU report:

```
power/runtime_status        unsupported
power/control               auto
power/runtime_active_time   0
power/runtime_suspended_time 0
```

So this kernel has no runtime-PM accounting for either device. That is why the
active/suspended counters are zero on both — **not** because they have never run.
Round 79 already flagged this for the GPU; it is now confirmed for the SMMU too,
which means those zero counters must not be read as "never powered".

### What is NOT there

`/sys/kernel/debug/iommu/tests/` contains only the two
`apps_iommu_*_test_device` entries. There is **no kgsl-specific test or context
dump** — the kgsl SMMU appears in the driver bind list and contributes to the
page count, but exposes no per-context view of its own here. There is also no
`/sys/kernel/debug/iommu/kgsl*` node.

## The correction, stated precisely

Round 75's four inputs are unchanged as *port* blockers, but two of them were
described too strongly:

| # | round-75 wording | what this round measured |
| --- | --- | --- |
| I1 | "no page table" | **92 page-table pages exist**, held by the bound `arm-smmu` driver |
| I2 | "no ASID allocator" | still accurate *for this port*; the vendor kernel's `arm-smmu` does allocate ASIDs, but no per-context view is exposed here to observe it |
| I3 | "streams do not fault" | unchanged — round 53 measured `Busy=10` / `id0=4c017e4c` in the firmware BYPASS handoff |
| I4 | "CX down" | unchanged — round 35 measured `cx=0x00005ff4` with BIT(31) clear |

The honest statement is therefore: **the port has no page table and no ASID
allocator; the DEVICE has both, inside a driver this port cannot use.** Those are
different claims and only the second one is supported by measurement.

This also explains round 80's all-zero IOVA table in a new light: the tables
existing while every kgsl object sits at IOVA zero is consistent with I3 — the
streams are in bypass, so nothing is being translated even though a translation
regime is present and populated.

## Reproduce

```
C:\platform-tools\adb.exe shell "mount -t debugfs none /sys/kernel/debug"
C:\platform-tools\adb.exe shell "cat /sys/kernel/debug/iommu/io-pgtable/pages"
C:\platform-tools\adb.exe shell "ls /sys/bus/platform/drivers/arm-smmu/"
C:\platform-tools\adb.exe shell "cat /sys/devices/platform/soc/5040000.arm,smmu-kgsl/modalias"
C:\platform-tools\adb.exe shell "umount /sys/kernel/debug"
```

## Verification

`scripts/verify-enchilada-smmu-bound.py` re-reads the device on every run —
mounting debugfs, reading `io-pgtable/pages`, the `arm-smmu` bind list, the kgsl
SMMU `modalias` and its runtime-PM state, and then **always unmounting in a
`finally` block** and asserting the mount count returned to its pre-round value.
It also proves the correction is a *wording* fix: round 75's four blocker
identifiers are still declared in `AdrenoSmmuStage1V1.h` and its `ready()` is
still derived, so nothing was reversed.

`SMMU_BOUND_VERIFY: PASS (35 checks, 0 failures)`.

## Honest boundary

DEVICE-SOURCED DRIVER STATE, read through a briefly-mounted debugfs that was then
removed and verified removed. No GPU register read or write, no MMIO, no ioctl,
no submission, no firmware load, no PAS, no SMMU programming, no flash, no reboot,
no read-write mount, no file created. `m39510` NOT exercised.
`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The A630 client GPU windows stay all-zero.
The four hard blockers of `docs/porting/gpu.md` §1 remain open. The `gpu` area
**STAYS `"host-verified"`**. It does **not** unblock stage 5.

Chain: round 81 closed at `a469caef78f2dd52962c01d13c684fa0c88a16a7`.
