# UniXNU-enchilada

Device repository for the OnePlus 6 (enchilada, SDM845). It holds the board file, kernel defconfig and device scripts.

- [Documentation](https://unixnu.com)
- [Porting guide](https://unixnu.com/porting/)
- [Bring-up order](https://unixnu.com/porting/bring-up)

Build with the `unixnu` driver of [UniXNU](https://github.com/UniXNU/UniXNU).

This repository was derived from [UniXNU-fajita](https://github.com/johnpanos/UniXNU-fajita)
(OnePlus 6T, same SoC) and keeps that port's drivers, kits and scripts, retargeted to the
OnePlus 6. The device facts it relies on come from the mainline `sdm845-oneplus-enchilada.dts`,
from `Resources/DTBs/enchilada.dts` of Mu-Silicium and from the stock boot image header.

## State

| Stage | State |
| --- | --- |
| UEFI firmware and UniXNU menu | **Works on this phone.** Two images were booted with `fastboot boot` on 2026-10-28. (a) Mu-Silicium's own `Mu-enchilada.img` brought up USB as `USB\VID_05C6&PID_F000` and then fell to EDL 9008 -- it carries no payload volume (its ramdisk is five bytes). (b) The image built here, from the firmware volume built here, with no ramdisk: **the panel showed the UniXNU menu**, `no boot entry found` in red, `drivers 0 loaded / 0 failed`. `UniXNUDxe` writes `BootOrder`, BDS starts `UniXNU.efi`, the menu draws and takes input, and firmware `DEBUG` text lands on the panel. |
| Payload, ADT, kernel | **Built here on 2026-10-28.** The kernel came from a CI artifact (this host cannot link a kernel Mach-O into the address xnu expects -- see 3.27 of the manual): 51822592 bytes, sha256 `6208a5c6...`, `__TEXT` at `0xfffffff007004000`, entry `0xfffffff0073ec488`, matching what `board.toml` records. The payload volume and two boot images were built from it. **Not yet booted to a kernel**: a 192 MiB ramdisk never reached the firmware, so `size_mib` is 64 now and the size sweep is pending a physical reset of the phone |
| Userdata | **built here on 2026-10-28**: `userdata-root.img`, 9382658048 bytes = 8.74 GiB, sha256 `fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`, two windows (`OS` 6077580800 at `0x100000`, `SystemOS` 3303014400 at `0x16a600000`) in the measured 50.41 GiB partition. Not flashed and not read by any kernel: `hardware_tested: false`, because there is no NativeUfs in the build yet. See `notes/userdata-build.md` in the port working directory |
| Touch, display, charging, GPU | see `NOTES-touch.md`. The touch pins and I2C address in `package/EnchiladaTouchBoard.h` are now confirmed against the vendor `dtbo` overlay (see *Touch*), and nothing has run on hardware yet |

Every value in `board.toml` that is marked as measured must be re-measured on the actual phone
with `scripts/dump-device-facts.sh`; the values inherited from fajita are not valid for another
device (the dipper port's documentation is explicit that GPT LBAs must never be copied between
devices).

The same holds for the comments. This repository was derived from UniXNU-fajita, and the first
derivation renamed `fajita` to `enchilada` inside the comments too, which turned the 6T's
measurements into apparent measurements of this board — "every run froze ~2 minutes in",
"143-285 ms per swap", the `int-NNN` issue numbers, `sCR0 = 0x002f0406`. None of that has been
observed here. The files that carry such observations now open with a `PROVENANCE` banner naming
the 6T, and `scripts/check_hardware_claims.py` fails on any line that asserts an observation
without either naming the device or sitting in a file with such a banner. If you add a comment
about something you measured on this phone, name the phone in the line — that is what makes the
next reader able to tell your measurement from an inherited one.

## Firmware

Mu-Silicium ships an enchilada platform package in `Platforms/OnePlus/enchiladaPkg`, so this
repository does not derive one from another device. `scripts/make-enchilada-uefi.py` patches
that package in place, idempotently, in a Mu-Silicium checkout:

| Item | Value |
| --- | --- |
| Platform package | `Platforms/OnePlus/enchiladaPkg` (Mu-Silicium) |
| Panel | `samsung,sofef00` / AMS628NW01, 1080x2280 |
| ABL board ID | `<8 0 17819 22>` |
| Display driver | Qualcomm DisplayDxe, the OnePlus 6 vendor binary (`USE_CUSTOM_DISPLAY_DRIVER = 1`) |
| Vendor DXE binaries | `Binaries/enchilada`, 216 files; `Binaries/generic`, 14 |
| Memory map, FD base and size | `0x9fc00000`, `0x300000` |

What the script adds is the UniXNU boot path, as in uefi-sheng: `UniXNU.efi`,
`UniXNUDxe`, `DarwinBoot.efi` and `EnchiladaBootMemoryDxe` in the DSC, FDF and DXE list,
plus the `UniXNULib` and `FdtLib` library classes. It also patches the shared
`BootShim/BootShim.S` and `SiliciumPkg` SEC with the uefi-sheng fixes: the ARM64 header size
covers the shim and the firmware volume, the boot CPU's `x0` is carried into a HOB, and the
copied firmware volume is cleaned and the instruction cache invalidated before the jump.

`DarwinBoot.efi` is the one part of that list that is easy to lose and impossible to notice.
`board.toml` says `[payload] loader = "firmware"`, so `payload.py` writes no `[[driver]]`
section into `unixnu.toml`: UniXNU looks for `gUniXNULoaderProtocolGuid` and DarwinBoot is the
only module that installs it. Leave it out of the DSC and the boot manager prints
`xnu: no loader protocol installed; UniXNU does not provide one` and stops, after the firmware
menu has already worked. `scripts/check_firmware_assembly.py` now compares the two sides — the
`loader` value against the modules that actually `PRODUCES` that protocol — because a firmware
built without it looks healthy until it is on the phone.

```sh
git clone --recurse-submodules https://github.com/Project-Silicium/Mu-Silicium ../Mu-Silicium
python3 scripts/make-enchilada-uefi.py ../Mu-Silicium
dtc -I dts -O dtb -o ../Mu-Silicium/Resources/DTBs/enchilada.dtb ../Mu-Silicium/Resources/DTBs/enchilada.dts
```

The script compiles the device tree when `dtc` is on `PATH`. Every edit carries its own marker,
so a checkout patched by an older version of the script picks up what has been added since
without applying anything twice: the run reports `already patched`, or `patched (N edit(s)
applied)`.

`--recurse-submodules` is not optional. The OnePlus 6 platform does not compile the Qualcomm
DXE drivers: its DSC, FDF and `Include/*.inc` name 79 prebuilt `.efi`/`.inf`/`.depe` files
under `Binaries/enchilada` and `Binaries/generic`, which live in the `Binaries` submodule
(Project-Silicium/Device-Binaries). A plain `git clone` leaves that directory empty and the
EDK II build stops partway through without naming the file it wanted.
`scripts/check_vendor_binaries.py` resolves all 79 references and reports which are missing.


## Stock boot image

`[bootimg.image]` in `board.toml` holds the header of the boot image OnePlus ships for this
phone: header version 0, page size 4096, base 0, kernel offset `0x8000`, ramdisk offset
`0x1000000`, tags offset `0x100`.

Those numbers are measured, not inherited, and measured twice from two different images. The
phone's own `boot_a` partition (`dd` from a recovery-mode adbd, first page in
`notes/device-live/root-dump/boot_a-header.bin`) reads `kernel_addr 0x8000`,
`ramdisk_addr 0x1000000`, `tags_addr 0x100`, `pagesize 4096`, `header_version 1`,
`kernel_size 13436699`, `ramdisk_size 18376167`. The LineageOS 22.2 nightly for enchilada
(20261021) carries the same layout — 67108864 bytes, sha256
`0e9ae8349ee06e577b120728e5516283c21f0622a6e861505e2a36139dcaf2fc`, `kernel_size 14687937`,
`ramdisk_size 16340632`. Both agree on all four load addresses. `header_version` stays 0 on
purpose: version 1 images were refused while version 0 ones loaded on the sibling port, and
postmarketOS also ships a version 0 image.

The two `ramdisk_size` figures are worth reading together: 17.5 MiB and 15.6 MiB, both real, and
the payload volume this port builds is 192 MiB — about eleven times either. That is the concrete
basis for "the ABL refuses large ramdisks" on this board rather than a rule copied from the 6T.

That same image carries three device trees appended to the kernel. The third one
(`SDM845 v2.1 SoC`, `qcom,msm-id = <0x141 0x20001>`, 415955 bytes) is the one this phone boots,
and its `/reserved-memory`, `/cpus/*` and GIC registers agree with `Resources/DTBs/enchilada.dts`
and with `scripts/make-enchilada-adt.py` — so the values in `board.toml` are this board's own,
not the 6T's. `scripts/check_stock_reserved.py` compares that stock tree against the UEFI memory
map and lists every node where it and the shipped copy disagree (four: `cdsp_region`,
`mba_region`, `slpi_region`, `pil_spss_region`).

`scripts/fetch-enchilada-boot.py --info` reads the same header out of an OxygenOS full OTA with
HTTP range requests and checks it against the payload manifest. The image is written to
`.cache/firmware/enchilada/boot.img`, which Git ignores.

## Touch

The touch controller is not in the device tree appended to `boot.img`: there `i2c@a90000` is
`status = "disabled"` and carries no touch node at all. OnePlus keeps the panel and touch
configuration in the `dtbo` partition as an overlay, so the pins can only be confirmed from there.
The same LineageOS build's `dtbo.img` (8388608 bytes, sha256
`278e5e5fc7bdd17cd2aec659d1ea3b7645910bc8ca26e08569d8b4ce889ca6d1`) holds 32 entries, eight of
which are real device trees. They carry no `oneplus,enchilada` compatible — the model names are
Qualcomm's internal ones (`MTP PVT`, `MTP DVT`, ...) — so the board is identified by
`qcom,board-id`. This phone is entry 22, `qcom,board-id = <0x8 0x0 0x459b 0x16>`.

That overlay enables `/fragment@67/__overlay__/synaptics-rmi-ts@20` with
`compatible = "HWK,synaptics,s3320"`, `synaptics,reset-gpio = <&tlmm 99 0>`,
`synaptics,irq-gpio = <&tlmm 125 0x2008>` and `synaptics,1v8-gpio = <&tlmm 88 0>` at I2C address
`0x20`. Those are exactly the values `package/EnchiladaTouchBoard.h` hardcodes, and
`scripts/check_kit_constants.py` asserts it against the overlay and the base tree. The pins are
SoC-level, not board-level: the 6T's kit differs only in the panel height.

One trap the overlay sets: its `synaptics,display-coords` and `synaptics,panel-coords` both say
1080x2160, which is an 18:9 value left over from an earlier OnePlus. This panel is 1080x2280, so
`FTB_PANEL_H` must stay 2280 and the real maximum has to come from the F12 query at run time.

## Boot arguments

`board.toml` `[payload].kernel_args` is the only place the kernel command line is written, and
three different parts of the tree read it: this repository's `package/` (the device kit), its
`drivers/` and `kits/`, and UniXNU's `patches/xnu/`. There are two kinds of read, and they mean
opposite things:

* a **tunable** — `PE_parse_boot_argn("x", &v, ...)` where `v` was already initialised to the value
  the code wants. Absence is the design.
* a **gate** — the result makes the surrounding code `return` before doing any work. Absence means
  the driver never starts, silently, with every earlier stage looking healthy.

Every driver here is staged bring-up and default-off on purpose, so a gate that is off is not a
defect — it is the state of the port. The defect worth catching is the incoherent combination: a
tunable on the command line belonging to a file whose gate is off, so the number is parsed by code
that never runs. `scripts/check_kit_boot_args.py` (in the port working directory) reports the whole
inventory and fails on exactly that. It currently reads:

```
device: 12 gate(s) (10 on, 2 off), 82 tunable(s)
patches: 7 gate(s) (3 on, 4 off), 6 tunable(s)
command line: 47 entries, 977 bytes of 1000 (23 left)
off gates: enchilada-touch, offline-keystore
```

The two device gates that are off are deliberate:

* `enchilada-touch` — the four-stage touch bring-up (1 read-only diagnostics, 2 clocks and reset,
  3 PDT/F12 identity, 4 publish contacts). The first boot does not need it, and `enchilada-touch=1`
  costs 18 of the 23 remaining bytes. Nothing else on the line belongs to
  `EnchiladaTouchProbe.cpp`, so leaving the gate off costs nothing silently.
* `offline-keystore` — loads `com.apple.driver.AppleSEPKeyStore`, which is step five of the
  bring-up. `offline-key-events` is the gate that is on, and it is enough for the keys.

Note that `PE_parse_boot_argn` returns a boolean *and* parses, so `if (!PE_parse_boot_argn(...))`
reads as "the argument is absent"; the same call without the `!` cannot be a gate at all. That
distinction is what separates the 12 gates from the 82 tunables, and getting it wrong in either
direction produces a check that is either useless or permanently red.

## Build

Run these commands from the UniXNU checkout. The device repository is `../UniXNU-enchilada`.

```sh
./unixnu defconfig enchilada_defconfig -D ../UniXNU-enchilada
./unixnu fetch
./unixnu build package firmware --uefi ../Mu-Silicium
./unixnu bootimg --uefi ../Mu-Silicium --dtb ../Mu-Silicium/Resources/DTBs/enchilada.dtb
```

The kernel itself needs Xcode or the Linux toolchain. `.github/workflows/kernel.yml`
builds it on a macOS runner and produces `kernel.macho` and `payload.img` as artifacts.

## Checks

Everything that can be verified without Xcode, Linux tools or the phone is in one script,
and it is what to run before pushing:

```sh
python3 scripts/check_device_repo.py          # board.toml, configs, scripts, patches
python3 scripts/check_vendor_binaries.py      # all 79 Binaries/ references of enchiladaPkg
python3 scripts/check_memory_map.py           # reserved-memory against the UEFI memory map
python3 scripts/check-enchilada.py            # the board preflight the firmware step runs
python3 scripts/selftest-adt.py               # the device tree XNU needs
```

`scripts/check_kit_constants.py` and `scripts/check_stock_reserved.py` live in the port working
directory rather than here, because they compare against the stock device trees downloaded from
the official build (see *Stock boot image* and *Touch*). `scripts/check_firmware_assembly.py`
lives there too: it needs a patched Mu-Silicium checkout, and it resolves the whole DSC/FDF module
graph — including the `loader = "firmware"` check above — without running the EDK II build.
`scripts/check_kit_boot_args.py` lives there as well, for the same reason as the two device trees:
it reads this repository's sources, the XNU patch series and the command line together.
`scripts/check_hardware_claims.py` lives there too: it reads every comment in this repository, so it
needs both this repository and its sibling ports to tell an observation from a claim.
`scripts/check_workflow_paths.py` and `scripts/check_workflow_commands.py` also live there, because
they read `.github/workflows/` here and hand each `./unixnu` line to UniXNU's own argparse.
`scripts/check_defconfig_symbols.py` lives there as well: it follows `xnu_env()` in
`scripts/unixnu/kernel.py`, which turns every `CONFIG_XNU_X` of `configs/enchilada_defconfig` into
an `XNU_X` environment variable that `scripts/build-xnu-4k-baseline.sh` reads, and fails when a
switch nothing reads is set. `scripts/check_boot_image_budget.py` lives there too: it reads
`partition-size:boot_a` (64 MiB) and `max-download-size` (768 MiB) out of the phone's own
`fastboot getvar all` dump and compares them with `[payload] size_mib` (192 MiB), which is why this
port is booted with `fastboot boot` rather than flashed to `boot_a`. `scripts/check_root_manifest.py`
lives there as well: it reimplements the `SHNGROOT` rules of `drivers/NativeUfs/RootLayout.h` and
runs the header that `unixnu build userdata` writes through them, because that header's only reader
is the kernel and a mismatch shows up as a dead boot rather than a host error.
`scripts/check_lzfse.py` lives there too: `unixnu build userdata` decodes the OS disk image through
an external `lzfse -decode` (`scripts/unixnu/udif.py:90-92`), which macOS and Linux get from Apple's
tooling and Windows does not have at all, so the check round-trips a known payload through whatever
decoder is on this machine and says so plainly when there is none.
`scripts/check_mu_patches.py` lives there as well, and it exists because a firmware built without
Mu-Silicium's own patches still builds, still comes out at exactly `0x300000` bytes, and still runs:
`build_uefi.py:508-511` applies four patches from `Resources/MuPatches/` to `Mu_Basecore` before the
build and reverses them at `:487-489`, and nothing in the output records that. The check asserts that
the local build applies the four that still apply, reverses them in the opposite order, and skips
`AsmMacroLib.patch` only because `MdePkg/Include/AArch64/AsmMacroLib.h` carries the same fix in a
newer form.
`scripts/check_built_fv.py` answers the next question along: whether the volume this machine just
built actually contains the four modules the port adds. That is not visible in the size — a module
named in the DSC and the FDF but left out of the component list GenFds walks still produces a
correct 3 MiB volume and a firmware that boots to a dead end, which is what section 3.14 of the
manual records. The names are searched for in their UTF-16LE form because the ASCII form appears
dozens of times in CodeView build paths whether or not the module is there; `test_built_fv.py`
pins that down by erasing one module at a time and asserting the ASCII hits survive.
`scripts/check_text_encoding.py` lives there too, and it is there because this manual was once
destroyed: a bare `Get-Content`/`Set-Content` round trip decoded a UTF-8 file through cp936, wrote
back valid UTF-8, and lost 2254 bytes to `?` with nothing reporting an error. The guard counts euro
signs (how cp936 renders a lone `0x80` continuation byte) and the ratio of `?` to CJK characters, and
`test_text_encoding.py` proves both directions: the damaged sample is caught, and three genuine
Chinese documents are left alone. `scripts/assemble_manual.py` exists for the same reason — the manual
is assembled from `work/manual-parts/` rather than edited whole, so a bad round trip can only damage
one part, and `--check` catches a part edited without reassembling as well as a missing section.
`scripts/check_kernel_linker.py` and `test_kernel_linker.py` live there as well, and they exist
because the reason recorded upstream for the kernel step needing a Mac is not the real reason.
`scripts/linux/setup-toolchain.py:246` says "ld64.lld rejects -kernel", but `makedefs/MakeInc.def:653`
shows xnu never passes `-kernel` — the flag comes from `-fapple-kext`, which is a clang flag. The real
problem is placement, and `scripts/probe_arm64_link.py` links the actual ARM64 kernel flag set
(`LDFLAGS_KERNEL_GENARM64` + `LDFLAGS_KERNEL_SEGARM64` + `LDFLAGS_KERNEL_GEN`) to show it: the link
**succeeds**, 65808 bytes with the right magic and the segments renamed as asked, while `-image_base`,
`-segaddr`, `-segment_order` and `-static` are each answered with `warning: Option ... is not yet
implemented. Stay tuned...` and exit 0. `__TEXT` lands at `0x0` instead of `ARM64_LINK_ADDR`
(`0xfffffff007004000`, MakeInc.def:333). That is fatal rather than inconvenient because xnu computes
its slide from a compile-time constant, not from the linked image — `osfmk/arm/arm_init.c:275`,
`slide = (uintptr_t)k_mh - VM_KERNEL_LINK_ADDRESS`, where `VM_KERNEL_LINK_ADDRESS` is
`-DVM_KERNEL_LINK_ADDRESS=$(ARM64_LINK_ADDR)` at MakeInc.def:343. A kernel built here would be off by
`0xfffffff007004000` from its own header, and `DarwinBoot` cannot fix that: it moves a kernel to
wherever it was loaded (`DarwinBoot.c:165-170`, `:432`, `:618`, `:624`) but the kernel's own idea of
where it lives is compiled in. The check states the current answer and fails in both directions —
when a linker can place the kernel, and when the recorded landing address changes — because otherwise
"passes" is the uninteresting state and nobody would ever see it fire.
`scripts/check_manual_appendix.py` closes the last gap: appendix A of the manual lists every check by
number, and it drifted twice, once with rows invented from memory and once left behind when a check
was added. `rebuild_appendix.py` generates it from `run_checks.py`, and the check refuses a manual
that disagrees.
`scripts/check_payload_volume.py` lives there too, and it covers the one stage between the kernel and
the boot image that needs no kernel at all: `scripts/unixnu/payload.py:84` writes the FAT32 volume with
`mformat`/`mcopy`, whose own advice is `brew install mtools`. `scripts/install_mtools.py` unpacks the
MSYS2 mingw64 packages instead, and it installs `libiconv-2.dll` with them because without that DLL
every mtools binary exits `0xC0000135` and prints nothing — the failure looks like a missing tool, not
a missing library. The check writes a volume with `payload.py`'s own flags, reads it back with `minfo`,
and copies a nested `EFI/UniXNU/Payloads/` tree and compares it byte for byte with `mtype`, because a
recursive copy is exactly what puts the payload on the volume.
`scripts/check_workflow_artifacts.py` covers the delivery end: it reads the workflow and requires
every file an output flag writes to be carried by an `upload-artifact` step, and no two output flags
to share a directory. Both rules exist because this workflow built two boot images and uploaded one
of them, and because `bootimg.py:114` writes `boot.json` beside the image it just built, so two
images in one directory leave one report standing for both. The second rule is not decorative: the
image that went missing was the no-ramdisk fallback, which is exactly the one nobody had tested.
`python scripts/run_checks.py` runs all fifty.

`check_memory_map.py` and `check-enchilada.py` answer the one question that decides whether
the firmware can hand memory to XNU: every reservation the device tree declares must be
reserved in the platform memory map, and the only region UEFI may hand out as free RAM is
`DXE Heap`. On this board that holds — 6 fixed reservations, 65 descriptors, one `Conv`
descriptor that overlaps no reserved region — and the preflight reports 0 errors and 6
warnings, all of them expected (three regions the firmware clips out of its own map, one
partly covered, two nested MMIO windows).

One overlap shows up in the LineageOS device tree, and the phone's own tree settles it: that image
reserves `pil_spss_region` at `0x97c00000` (1 MiB) — the SPSS PIL image XBL loads — and the UEFI
`DXE Heap` starts at exactly `0x97C00000` and runs for 87 MiB. But the live device tree pulled from
this phone in recovery (`notes/device-live/root-dump/live-fdt.dtb`, 556642 bytes, 3259 nodes) puts
that region at `0x97b00000`, one MiB lower, and nothing overlaps the heap there — which is also why
the copy Mu-Silicium ships agrees with it. LineageOS simply carries a newer firmware layout than
OxygenOS 10 does. `scripts/check_stock_reserved.py` reports the LineageOS pair as tolerated rather
than as a failure, and fails on any *other* reserved region that ends up inside allocatable memory;
`scripts/check_live_gpt.py` does the same comparison against the phone's own tree and finds no
overlap at all. If a firmware update ever moves the region back, the fix is to raise the heap to
`0x97D00000` and shrink it by 1 MiB in `MemoryMapLib.c`, which changes the continuous window
DarwinBoot hands to XNU; `board.toml`'s `owned_memory_size` would follow.

## Data volume seed

The writable Data volume (`/private/var`) comes from a RAM disk seed made to `data-seed.json`:
one APFS container at offset 0 holding one case-sensitive, unencrypted volume named Data with
no role, plus the listed directories with their owners and modes. On Linux,
`scripts/make-data-seed-linux.py` builds it without root, FUSE or a kernel module. mkapfs from
[apfsprogs](https://github.com/linux-apfs/apfsprogs) formats the image. The script then writes
the directories into the volume's catalog itself and checks the result with apfsck and its own
read-back. Last, it packs the image with UniXNU's `packed_ramdisk.py`. `--fetch-apfsprogs`
clones a pinned apfsprogs commit to `.cache/apfsprogs` and builds it there; `--apfsprogs DIR`
uses a build tree you already have.

```sh
python3 -I scripts/make-data-seed-linux.py \
  --packer ../UniXNU/drivers/DarwinBoot/Test/packed_ramdisk.py --fetch-apfsprogs \
  --volume-uuid 0E1D5C2A-7B3F-4C88-9A61-2F4D8E0B6C17 --container-uuid 5A9C3E71-D204-4B6F-8E15-C7A0B9F24D38 \
  --timestamp 1792540800
```

The script writes `.cache/data-seed/data-linux.dbr` and `data-linux.json`; `--keep-raw` keeps
`data-linux.raw`. Without the UUID options it picks random UUIDs. Either way it prints the
volume UUID, which goes to `CONFIG_XNU_OWNER_DATA_VOLUME_UUID`. With fixed UUIDs, a fixed
`--timestamp` (or `SOURCE_DATE_EPOCH`) and the same apfsprogs commit, the image is
bit-identical on every run. `--apfs-dump DIR` (a build directory with apfs-fuse's `apfsutil`
and `apfs-dump-quick`) adds a read-back by a second APFS implementation.
`scripts/make-data-seed-macos.py` is the macOS equivalent.

## Bring-up on the phone

- **Measured on this phone (2026-10-28): a healthy slot does not need to be switched away from.**
  Slot a reported `slot-successful:a: yes` and `slot-retry-count:a: 7`, the boot partition held the
  stock Android boot image, and `fastboot boot` succeeded without touching the slot. The advice
  below about `fastboot set_active b` comes from the fajita and dipper ports, where the active slot
  was repeatedly left unbootable during bring-up. Check `fastboot getvar slot-successful:a` and
  `slot-retry-count:a` first; only switch slots if the ABL answers "Load Error".
- **One `fastboot boot` per bootloader entry.** The first attempt after entering fastboot is
  accepted; a second one in the same session fails at "Load Error" even for a 1.7 MiB image. So a
  refusal means the session is spent, not that the image is bad: run `fastboot reboot-bootloader`
  and try once more before changing anything. This also means the fajita port's advice to keep the
  boot partitions empty and to `set_active b` before every attempt does not apply here -- on
  2026-10-28 `boot_a` held the stock Android boot image and `fastboot boot` still worked.
- **The ABL accepts a large ramdisk; what it does not accept is a second `fastboot boot` in one
  bootloader session.** Measured on this phone on 2026-10-28: a 203 MiB image carrying the whole
  192 MiB payload volume as its ramdisk was sent and booted, and so was an 8 MiB one; repeating
  either without leaving fastboot first failed at "Load Error". So the first route -- the payload
  volume as the boot image's ramdisk -- is the one to try, and `unixnu bootimg` picks it up by
  default. The fallback, for a refusal that turns out to be about size after all, is to flash the
  volume to a partition that is unused while the other slot is active and build the image with
  `--ramdisk none`: `fastboot flash <partition> out/enchilada/payload/payload.img`. UFSDxe,
  PartitionDxe and Fat expose the volume and UniXNU finds `\EFI\UniXNU\unixnu.toml` on it either
  way. `notes/device-live/payload-boot.md` has the seven attempts behind this.
- **Loading is not running: the ramdisk size decides whether the firmware survives the handoff.**
  Same firmware volume, three ramdisks, measured 2026-10-28 (`notes/device-live/ramdisk-budget.md`):
  no ramdisk reaches the UniXNU menu on screen; 8 MiB brings the firmware up far enough to answer
  on USB as `VID_05C6:F000`; 192 MiB is accepted by the ABL and then never appears on USB at all,
  and the phone falls back to fastboot. `[payload] size_mib` is therefore 64, not 192 -- the
  kernel plus its ADT take 49.4 MiB, and 64 MiB is also `partition-size:boot_a`, the first size
  where both routes are open. `scripts/probe_ramdisk_budget.ps1` walks the sizes in between and
  reports, for each, whether `F000` appeared -- an outcome that needs no eyes on the screen.
- **A `fastboot` session that has carried one large image can stop carrying anything, and the
  device still looks connected.** After the 203 MiB boot, `fastboot devices` kept listing
  `27f4f45d` and `pnputil` kept showing the interface as started, but every transfer failed with
  `Write to device failed (no link)` -- including `getvar`. `reboot-bootloader` and restarting the
  adb server did not clear it. Send a `getvar` before believing any `boot` refusal, and reset
  physically (Power + Volume Up for 15-20 s) when the bulk endpoint is gone.
- Nothing charges the battery while UEFI or XNU runs. Keep test sessions short.
- **A boot that ends in EDL 9008 cannot be recovered in software from a PC.** The phone answers
  neither `fastboot` nor `adb` there, and a firehose programmer is not part of this repository.
  Hold Power + Volume Up for 15-20 s to reset it; if that fails, add Volume Down. `fastboot boot`
  writes no partition, so nothing is lost by a boot that ends this way.
- `scripts/patch-xnu-entry-probe.py --reset-after SYMBOL --in FUNCTION` puts a timed PSCI
  reset after a call in the kernel; a reset back to fastboot means the kernel got past it.
  This is how the sibling port bisected its first boot.

## Boot

```sh
fastboot boot out/enchilada/boot/boot.img
```

`fastboot boot` is temporary and leaves Android and every partition unchanged.

The vendor image is a useful first step, and it is the one that was run on this phone on
2026-10-28 (`Mu-enchilada.img`, 1574912 bytes, sha256 `f60d7c77...`): it came up far enough to
enumerate USB mass storage as `VID_05C6 PID_F000`, then the phone dropped off the bus and returned
as EDL 9008. That image carries no payload volume -- its ramdisk is five bytes -- so it had
nothing to boot. Use the image built here instead; it carries UniXNU.efi and the payload volume.

An image built here was booted on the same phone the same day: 203 MiB, the whole payload volume as
its ramdisk, `Sending OKAY` / `Booting OKAY`. It then never appeared on USB and the phone came back
in fastboot, so that size does not survive the handoff -- see `notes/device-live/ramdisk-budget.md`
and the ramdisk bullets above. `size_mib` is 64 now for that reason.

**The screen works, and it is the best channel this board has.** An image with no ramdisk was
booted on 2026-10-28 and the panel showed the UniXNU menu:

```
UniXNU
version 0.1.0
built-in configuration

1 Return to firmware    2 Restart    3 Power off    4 Tools
Booting Return to firmware in 9 s
Up/Down: select   Enter: boot   Esc: stop timer
no boot entry found
drivers 0 loaded / 0 failed    kits 0    acpi 0
```

So the chain from `UniXNUDxe` writing `BootOrder`, through BDS starting `UniXNU.efi`, to a drawn
menu and working input is complete on this board. `no boot entry found` is the expected answer for
an image with an empty ramdisk: there is no payload volume to read, so the built-in configuration
is used. `DEBUG` text from the firmware also lands on the panel, which is how the boot sequence can
be followed without a UART.
