# Stage 5 GPU — the /dev/mem ceiling on `ias` (ROUND 85, DEVICE-SOURCED)

**Status: DEVICE-SOURCED, READ-ONLY. NEGATIVE RESULT.** This round was authorised
to act on hardware (standing permission) and spent that authorisation measuring
what a device-side action can and cannot now achieve. It did **not** read ID2, did
**not** power CX, did **not** program a context, and did **not** prove the SMMU
works. It established a **hard ceiling** and left the device exactly as it found it.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area **STAYS `"host-verified"`**.
No flash, `m39510` NOT exercised, no MMIO write, no reboot. It does **not** unblock
stage 5.

## Why this round

Rounds 79–84 closed the modelling column: nine of twelve admission prerequisites
are modelled, and CI #239 proved the last code module compiles and links. The
remaining work is **execution**, and round 77 proved the `ias` input comes from a
runtime read of `ID2.UBS` at `arm-smmu.c:1904-1905`. The obvious question a device
round should answer is therefore: *can `ID2` be read on this phone?*

This round answers that, and the answer is no — for a reason that is structural,
not procedural.

## Finding 1 — the debugfs surface is unchanged, and still lacks `ias`

debugfs mounted briefly, read, then unmounted with the removal **verified**
(`BEFORE_MOUNTS=0`, `MOUNT_RC=0`, `UMNT_RC=0`, `AFTER_MOUNTS=0`,
`AFTER_ENTRIES=0`).

| observation | value |
| --- | --- |
| `/sys/kernel/debug/iommu/io-pgtable/pages` | **92** (re-confirms round 82 exactly) |
| `/sys/kernel/debug/iommu/` tree | `io-pgtable`, `tests` — **nothing else** |
| address-size / `va_size` / `ID2` nodes | **none** (`find -maxdepth 2` returns only `pgtable/pages`) |
| kgsl-specific iommu node | **none** (the two `tests/` entries are both `apps_iommu_*`) |
| `/sys/bus/platform/drivers/arm-smmu/` | still **both** `5040000.arm,smmu-kgsl` and `15000000.apps-smmu` |
| kgsl SMMU `modalias` | `of:Narm,smmu-kgslT<NULL>Cqcom,smmu-v2` (unchanged) |

So the page tables exist and the driver is bound — round 82's correction stands —
but **debugfs exposes no input-address size anywhere**, which is precisely the
value round 77 showed cannot be derived statically. The 92 pages tell you tables
exist; they do not tell you how wide the VA space is.

## Finding 2 — the decisive one: `CONFIG_DEVMEM is not set`

The read-only route to `ID2` at `0x05040028` would be `/dev/mem`. This kernel
does not provide it:

```
# CONFIG_DEVMEM is not set
# CONFIG_STRICT_DEVMEM is not set
```

The distinction matters and is the whole point of the finding. `STRICT_DEVMEM`
being unset would mean devmem is *present but unrestricted*; **`DEVMEM` itself
being unset means the char device does not exist at all**, and a node created with
`mknod` is accepted by the filesystem but every access fails:

```
mknod /dev/mem c 1 1        -> accepted, `test -e /dev/mem` says YES
dd if=/dev/mem bs=4 skip=... -> dd: /dev/mem: No such device or address   (ENXIO)
```

That `ENXIO` is the kernel saying the driver behind major 1 minor 1 was never
compiled in. It cannot be fixed with root, with an SELinux context, or with a
different mknod mode — the only fix is a **different kernel**. This closes the last
read-only route to `ias` on a stock boot, and it is a hard exclusion rather than a
permission problem.

The node this round created was removed and its absence asserted; the final mount
count is **0**.

## What this means for the strict order

The order does not change, but its cost does, and that is worth stating plainly:

1. **CX must still come up first.** Unchanged.
2. **`ID2` cannot be read from userspace on this kernel.** So `ias` cannot be
   obtained by a probe, which means the stage-1 context words derived in round 83
   cannot be completed **from userspace**. They can only be completed by code
   running **in the kernel** — i.e. by XNU driving the SMMU itself, which is
   exactly what `prerequisite kgsl-smmu-stage1` means and why it is `satisfied=0`.
3. **A TWRP-side SMMU probe is not a shortcut.** It would need either a kernel
   with `DEVMEM=y` or a kernel module; both are new kernels, not new commands.

In other words: this round converts "we have not read `ias` yet" into "`ias` is
**not readable** from this boot image", which is a stronger and more useful
statement. It removes a false hope rather than creating one.

## What was NOT done, deliberately

- **No ID2 read.** Not possible (see Finding 2). This is the measurement that
  would have closed the round's most valuable open input.
- **No GPUCC GDSC status read.** The round-28 gated read of `0x05091540` /
  `0x05091010` also requires `/dev/mem`, so it is blocked by the same ceiling. It
  was not attempted after Finding 2 made it provably impossible.
- **No KGSL submission**, no ioctl, no snapshot buffer, no mount left behind, no
  file written to persistent storage.
- **Nothing was inferred from the vanished boot log.** `dmesg` holds only ~2 min
  of wall clock (`uptime` 151128 s, ring buffer starting at the same battery-vote
  lines), so the `arm-smmu` probe messages from boot — which *would* have printed
  `Stage-1: N-bit VA -> N-bit IPA` at `arm-smmu.c:1940` — are gone. Counting zero
  `smmu` lines in `dmesg` is therefore **not evidence of absence**; only the
  config and the failed read are. That distinction is stated rather than glossed.

## Honest statement

The port now knows, by measurement, that the single most valuable missing input
(`ias`) **cannot be obtained from this device image by any userspace route**. That
does not satisfy a prerequisite, does not move the admission table
(**modeled = 9 / satisfied = 0 / BLOCKED**), and does not bring the first render
closer. It does mean the next real step is unavoidably an XNU-side SMMU driver
plus a kernel that can be probed — not another survey.

Verifier: `scripts/verify-enchilada-devmem-ceiling.py` — re-reads the live phone,
asserts the 92 pages and the absent address-size nodes, asserts `CONFIG_DEVMEM is
not set` and that a read fails, asserts the kgsl SMMU is still bound, and asserts
the mount count returns to 0.
