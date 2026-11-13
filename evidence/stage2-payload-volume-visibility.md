# Round 93 — why did the UniXNU menu say `no boot entry found`?

**Status: DIAGNOSED, READ-ONLY.** The payload volume and the firmware are both
correct. The failure is a *volume-visibility* problem at the point UniXNU looks for
its configuration, and this note derives it from code and from the image bytes
rather than guessing.

No partition was written, no flash was performed, `m39510` NOT exercised.

## 1. The payload volume is a valid FAT32 volume (measured)

`payload.img`, 150,994,944 B, parsed from its own BPB:

| field | value |
| --- | --- |
| jump + OEM | `eb 58 90` + `MTOO4050` (the OEM field is just a label string) |
| bytes/sector, sectors/cluster | 512, 4 → **2048 B/cluster** |
| reserved sectors | 32 |
| number of FATs | 2 (both verified byte-identical on the phone) |
| FAT size | 574 sectors each |
| total sectors | 294,912 |
| root cluster | 2 |
| volume label | `ENCHILADAES` |
| FS type string | **`FAT32`** |

**Earlier rounds called this an "Apple `MTOO` container" and doubted whether `Fat.inf`
could read it. That was wrong.** `MTOO4050` occupies the OEM-name field that every
FAT BPB has; the volume is plain FAT32 and the header is an ordinary `eb 58 90` jump.
Nothing here is Apple-specific.

## 2. The volume contains exactly the files UniXNU wants (measured)

Walking the FAT32 tree yields 16 entries; the payload tree is:

```
/EFI/UniXNU/unixnu.toml                    1,337 B
/EFI/UniXNU/Payloads/kernel.macho      126,103,552 B
/EFI/UniXNU/Payloads/kernel.macho.adt       13,716 B
/EFI/UniXNU/Payloads/kernel.macho.trustcache 61,356 B
/EFI/UniXNU/Payloads/DarwinBoot.efi         28,160 B
/EFI/UniXNU/Payloads/scratch-data.img      262,288 B
```

`kernel.macho` is **126,103,552 B**, which is exactly the CI #239 kernel image size —
so the volume carries the current kernel, not a stale one.

`unixnu.toml` is a real two-entry config:

```toml
version = 1
[boot]
timeout = 3
default = "xnu"
[[entry]]
id = "xnu"
title = "enchilada first boot: XNU to console"
type = "xnu"
path = "Payloads/kernel.macho"
initrd = "Payloads/scratch-data.img"
args = "..."
[[entry]]
id = "reset"
type = "action"
action = "reset"
```

`default = "xnu"` names an entry that exists, so the config is self-consistent.

## 3. The phone's copy is byte-identical (measured)

Read back from `/dev/block/bootdevice/by-name/system_b`:

- BPB first 90 bytes match `payload.img` byte for byte: `eb 58 90` + `MTOO4050`,
  512 B/sector, 4 sec/cluster, 2 FATs, 574-sector FATs, label `ENCHILADAES`,
  `FAT32` type string.
- Both FAT copies read identically (`f8 ff ff 0f ff ff ff 0f ...`), so the volume is
  not half-written or corrupt.
- The root directory at sector 1180 (`reserved 32 + 2×574`, cluster 2) reads
  `ENCHILADAES` and the `EFI` directory entry.

So **the volume on `system_b` is valid, complete and readable** — if anything can
mount it.

## 4. What `no boot entry found` actually means (from the code)

`UniXNULib/Config.c`:

- `UniXNUConfigLoad()` (:1548-1563) first tries the volume UniXNU was **started from**
  (`Self->DeviceHandle`), then falls back to enumerating **every**
  `SimpleFileSystem` handle.
- `TryVolume()` calls `UniXNUReadFile(Volume, PcdUniXNUConfigPath, ...)`.
  `PcdUniXNUConfigPath` is `L"\\EFI\\UniXNU\\unixnu.toml"` (`UniXNUPkg.dec`) —
  exactly the path present on the volume.
- If **no** volume yielded that file, the code falls back to the built-in config
  (:1567-1596) and then calls `AddFallbackLoaders()`. That function (:1585) sets the
  notice **`no boot entry found`** only when it found **zero** bootable loaders.

Therefore the message means: **UniXNU could not read `\EFI\UniXNU\unixnu.toml` off
any block device it could enumerate.** It is not a parse error (a parse error keeps
its own `Config->Error` text) and it is not a missing file *on the volume* — the
file is there.

## 5. So the remaining unknown is block-device publication, not content

Given §1-§3, the volume is correct; given §4, UniXNU saw no filesystem containing
the config. The gap is therefore between the UFS controller driver and a mounted
FAT32 `SimpleFileSystem` handle on `system_b`.

Two candidates remain, and this round does **not** separate them:

1. **`UFSDxe` did not produce a usable block device in time / at all** under this
   firmware's DXE dispatch, so `PartitionDxe` had nothing to partition and `Fat.inf`
   had nothing to mount. The drivers are present in the FV (verified), so this would
   be a dispatch/dependency-ordering problem, not a build problem.
2. **The volume was produced but not connected**: a `SimpleFileSystem` handle exists
   for some other device (e.g. the boot-image ramdisk) while `system_b` stayed
   unexposed.

The distinguishing observation is the firmware's own DXE log — which this boot did
not capture, because the panel showed only the menu.

## 6. What this rules out

- **Not** a FAT/format problem (`MTOO` was a red herring; it is valid FAT32).
- **Not** a missing-config problem (the file is present at the exact PCD path).
- **Not** a stale payload (the kernel on the volume is the CI #239 image).
- **Not** a corrupt write (`system_b` reads back identical, both FATs consistent).
- **Not** a missing UFS driver in the firmware (all 33 Qcom drivers verified present
  in the decompressed FV; an earlier round's claim that `UFSDxe` was absent came from
  a faulty FV parser and is retracted here).

## 7. Next measurement

Boot the same image with the firmware's `DEBUG` output captured — the diagnosis
hinges on whether `UFSDxe` binds and whether `PartitionDxe`/`Fat` produce a
`SimpleFileSystem` on it. `notes/handoff-2026-10-29.md` already describes the
UFS-log window route for exactly this class of question; that is the cheapest next
step and it needs no partition write.
