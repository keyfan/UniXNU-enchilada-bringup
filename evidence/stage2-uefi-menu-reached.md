# Stage 5 — the UniXNU menu was reached on hardware, and why it then found nothing

**Status: DEVICE-VERIFIED, NARROW SUB-RESULT.** A real phone booted our own
`boot-r16.img` far enough to draw the **UniXNU menu on its panel**. It then counted
down and handed back to Android. That is a genuine device result and it is *not* a
step-5 unblock: the menu said **`drivers 0 loaded / 0 failed`** and **`no boot entry
found`**, and this note explains exactly why, from measurement rather than guess.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area **STAYS `"host-verified"`**.
No flash of a boot partition, `m39510` NOT exercised, no MMIO. **It does not unblock
stage 5.**

## What was actually booted

| item | value |
| --- | --- |
| host command | `fastboot boot work/out/boot-r16.img` |
| result | `Sending 'boot.img' (18076 KB) OKAY` / `Booting OKAY` |
| image | 18,509,824 B, sha256 `4e9e2417585112167749f98f287693d87ccfafcce649f8d3ea7a7e1218f4f8c9` |
| kernel | 1,725,666 B, gzip-wrapped FV, sha256 `2e60fdee5e0552f7…` |
| ramdisk | 16,777,216 B, **all zero** |
| screen | **UniXNU menu** drawn, countdown, then handed back to Android |

`fastboot boot` writes no partition. Android and every partition are unchanged.

## The two observations that matter, and the mechanism behind them

The menu showed `drivers 0 loaded / 0 failed`. The zero is real, and it is not a
misconfiguration: **the firmware volume this image carries does not contain the
storage stack at all.**

The FV (`2e60fdee…`, 3,145,920 B when decompressed) is byte-identical to
`work/out/boot-108.img`'s FV. Its contents:

| module | occurrences in the FV |
| --- | --- |
| `UFSDxe` | **0** |
| `PartitionDxe` | **0** |
| `Fat` | **0** |
| `UniXNU` | 1 (a PDB path string only) |

And the platform descriptor that decides what goes into the volume says the same
thing: `Platforms/OnePlus/enchiladaPkg/enchilada.fdf` (4,709 B) contains **11 `INF`
statements and zero mentions of `UFSDxe`, `PartitionDxe` or `Fat`**. The three
driver sources *do* exist in the tree
(`work/Mu-Silicium-full/Binaries/enchilada/QcomPkg/Drivers/{UFSDxe,PartitionDxe}`,
`…/FatPkg`), they were simply never pulled into the FDF.

So the chain is:

1. UniXNU starts (the EFI application embedded in the FV),
2. it enumerates UEFI drivers — there are none to load, hence `0 loaded / 0 failed`,
3. with no `UFSDxe` there is no block device for `system_b`,
4. with no `PartitionDxe`/`Fat` there is no way to read a partition's filesystem,
5. so the payload volume on `system_b` is invisible and the menu reports
   **`no boot entry found`**,
6. the built-in menu then boots "Return to firmware" on its countdown, which lands
   in Android.

## Why 16 MiB of zeroes, and not no ramdisk

`EnchiladaBootMemoryDxe`'s `PublishInitrd()` only publishes a block device when
`/chosen` carries `linux,initrd-start` / `linux,initrd-end`; with no initrd it logs
`no initrd in /chosen`. Measured consequence: the **no-ramdisk** image reaches
`VID_05C6:F000` but never draws a menu, while a **16 MiB all-zero** ramdisk does
draw it. The zeroes are not a payload — they exist solely to make the property
present so the initrd block device is published at all.

That also explains the size ceiling recorded earlier: the reserved window is
`0x8454D000-0x856FEF79` ≈ 17.7 MiB, so 16 MiB fits and **32 MiB resets the SoC
into EDL 9008**. The ramdisk route can never carry the 49.4 MiB payload, which is
why the volume belongs on a partition.

## What is verified, and what is not

**Verified on hardware this round:** the unlocked ABL accepts `fastboot boot` of an
unsigned image; our own build executes on this SoC; the panel is a working output
channel; UniXNU reaches its menu and draws it; and the countdown hands back to
Android cleanly, leaving the phone recoverable.

**Not verified, and now known to be blocked by a build defect rather than by
hardware:** that UniXNU can read the payload volume. It cannot, because the FV has
no storage stack. `system_b` was written with the CI #239 payload and read back
byte-for-byte (`eb 58 90 4d 54 4f 4f 34 30 35 30 …` matches the local image's first
16 bytes), so **the volume is there and correct; the firmware simply cannot see
it.**

## The next action, stated precisely

The fix is in the build, not on the phone: add `UFSDxe`, `PartitionDxe` and
`FatPkg` to `enchiladaPkg/enchilada.fdf` (and their `[Components]` entries to
`enchilada.dsc`), rebuild the FV, wrap it in a 16 MiB-ramdisk boot image, and boot
it again. Nothing else needs to change — `system_b` already holds the right volume,
and this must be repeated only if the payload changes.

**No further device write is required for that experiment**, which is why it is
worth doing next: the remaining unknown is firmware composition, and it can be
tested with the same `fastboot boot` that just worked.

## Honest boundary

This is a narrow sub-result on the boot path, not stage 5. It proves the UEFI
platform and our build reach the panel; it proves nothing about XNU, the GPU, the
SMMU, or rendering. The GPU area's admission table is unchanged at
**modeled = 9 / satisfied = 0 / BLOCKED**, and the first honest GPU device action
remains the round-28 gated GPUCC read, which still needs a kernel-side MMIO path
that does not exist yet.
