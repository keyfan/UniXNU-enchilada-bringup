# Stage 5 GPU — device-sourced A630 firmware inventory (round 38, host-verified)

## What this is

A **host-verified record of a device-sourced, strictly read-only extraction** of the
A630 GPU firmware already present on the enchilada's own `vendor_a` partition.
It closes the previously recorded blocker *"the 3 vendor-signed ZAP blobs are
absent"* **at the file-presence level only** — the blobs were never missing from
the device; they were missing from our workspace tree and from any canonical
public mirror. They are now located, hashed, and structurally verified.

This is **NOT** a device-verified GPU driver result: no GPU register was read or
written, no firmware was loaded, no driver ran, and the gpu area stays
`host-verified`.

## Method (one reversible read-only session, 2026-11-10)

Device `27f4f45d` (OnePlus6 enchilada), TWRP 3.4.0-3 recovery, `uid=0(root)`,
Linux 4.9.179-perf+. Safety contract (all verified in the transcript):
`mount -t ext4 -o ro /dev/block/by-name/vendor_a /vendor` only; confirmed
`VENDOR_MOUNTED_BEFORE=False` and `(ro,seclabel,relatime)` after mounting; all
files copied out read-only via `adb pull`; `umount` in `finally`;
`VENDOR_MOUNTED_AFTER_RESTORE=False`, `STATE_RESTORED=True`. **No flash, no
reboot, no `dd of=`, no misc/BCB change, no rw remount, no file creation on
device.**

Two earlier read-only `ls` passes had found `/vendor` and `/firmware` *empty*;
that was inconclusive, not negative — the partitions were not mounted in TWRP
(`mount | grep vendor` printed nothing; `/vendor` contained only `etc`, `lib64`,
`manifest.xml`). After the single ro mount, `/vendor/firmware/` contained the
full A630 set. `/vendor/firmware_mnt/image/` is empty (blobs live in
`/vendor/firmware/`).

## The 7 files (sizes recorded from device; bytes NOT committed)

Proprietary bytes stay out of the repository (gitignored
`work/tmp/a630-device-fw/`); `scripts/data/adreno-a630-device-firmware.json` is
the durable manifest.

| file | bytes | sha256 (16) | md5 (8) | role |
|---|---|---|---|---|
| a630_gmu.bin | 32768 | b07f90299ff2ae33 | 1ade6a65 | GMU fw, legacy flat ITCM, exactly 0x8000 |
| a630_sqe.fw | 31980 | 34edacb6fdedfbfc | 02d7f5fb | SQE microcode |
| a630_zap.b00 | 148 | b2975f6a4c28a981 | d1d3565c | PIL segment ph[0] |
| a630_zap.b01 | 6536 | d2ecdfe6fc83745b | bbbd0a17 | PIL segment ph[1] |
| a630_zap.b02 | 1968 | 519b5538809f54ee | 81b15d47 | PIL segment ph[2] |
| a630_zap.elf | 14256 | c9751419b74e0c53 | a0d5e1db | full ZAP ELF image |
| a630_zap.mdt | 6684 | e0dffc7e45dfb05c | 3fa0dfb5 | ZAP MDT (the file `.zapfw` names) |

## Two independent verifications (m03546)

1. **Device-side**: a *fresh, independent* reversible ro mount + on-device
   `md5sum a630_*` — all 7 device md5s equal the host md5 of the pulled bytes;
   mount state restored afterwards.
2. **Host-side**: sha256+md5 of every pulled file recomputed and compared to the
   manifest, plus ELF/PIL structural parsing and cross-checks against the
   sha-pinned Linux v6.12 sources and the public-mirror pins.

`python -X utf8 scripts/verify-enchilada-device-firmware.py` →
`ENCHILADA_DEVICE_FIRMWARE_VERIFY: PASS (102 checks, manifest + pinned upstream
+ pulled-bytes domains agree)`.

## ELF/PIL structure (device-corrected)

Both `.mdt` and `.elf` are **ELF32, `e_machine=0xa4` (EM_HEXAGON, 164)** —
a Qualcomm Hexagon DSP image, **not** AARCH64 (0xb7). Confirmed against Linux
v6.12 `include/uapi/linux/elf-em.h` (`EM_HEXAGON 164`, `EM_AARCH64 183`,
`EM_ARM 40`; GitHub contents API, blob sha `ef38c2bc5ab7…`). An earlier checker
asserted AARCH64 and failed; that was a **checker defect**, not a firmware
defect — the corrected (stronger) check ties every `.bNN` size to the ELF
program header: `phoff=52, phentsize=32, phnum=3, p_filesz=[148, 6536, 1968]`,
`.bNN` sizes == `ph[i].p_filesz`, first 32 bytes of `.mdt` == `.b00`.

## Upstream contract (sha-pinned Linux v6.12)

- `a6xx_catalog.c` pins `.zapfw = "a630_zap.mdt"` — **the device HAS the .mdt**.
- `a6xx_catalog.c` lists `MODULE_FIRMWARE("qcom/a630_zap.mbn")` — the device has
  **no** `.mbn`; it carries the signed split chain `.mdt/.b00/.b01/.b02`.
- `a6xx_gpu.c` pins `#define GPU_PAS_ID 13` (TrustZone PAS id for ZAP).
- `a6xx_gmu.c` pins the legacy GMU gate (`fw_image->size > 0x8000` → reject) —
  the device `a630_gmu.bin` is exactly 0x8000 and passes.

## Honest deltas vs the public mirror (`scripts/data/adreno-a630-firmware.json`)

The one reachable public mirror (endlessm/linux-firmware@master, not canonical)
differs from the device builds: GMU same size, different build; SQE different
size; ZAP `.elf` 14256 B vs public `.mbn` 14256 B, same size, different content.
The **device** blobs are the ones an on-target loader must use; the public pins
remain useful only as structural references.

## Honest boundary (what this does NOT unblock)

- TrustZone PAS authentication (`qcom_scm_pas_auth_and_reset`, GPU_PAS_ID 13) —
  unimplemented.
- An A6XX_GEN1 kernel backend caller with `CONFIG_XNU_NATIVE_ADRENO=y` —
  unimplemented.
- Full power-up sequence: CX/GX genpd + 6 clocks + RPMh/PDC (round 35's
  device probe measured CX DOWN / GX OFF) — unimplemented.
- Real device kgsl SMMU stage-1 context — unimplemented.

gpu area status **stays `host-verified`**. One unit, one read-only
recovery-session extraction.

## Evidence trail

- Manifest: `scripts/data/adreno-a630-device-firmware.json`
- Verifier: `scripts/verify-enchilada-device-firmware.py` (Domain A manifest +
  pinned upstream; Domain B pulled bytes + PIL structure)
- Pulled blobs: `work/tmp/a630-device-fw/` (gitignored, proprietary bytes stay
  out of the repo)
- Prior narrow device result: `evidence/stage5-gpu-power-domain-device.md`
  (round 35, GPUCC probe)
