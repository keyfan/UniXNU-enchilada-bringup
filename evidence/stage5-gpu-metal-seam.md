# Stage 5 GPU — A630 Metal establishment-chain seam (ROUND 65)

**Status: HOST-VERIFIED.** This document records a freestanding C module and two
independent host verifications. No device run was performed, no flash was
performed, the standing flash authorization was NOT exercised, no MMIO was
issued, no firmware was loaded, and `CONFIG_XNU_NATIVE_ADRENO` remains `n`.
The `gpu` area of `porting.toml` STAYS `host-verified`.

## Why this round exists

The user asked (m44634/m44638/m44639/m44640/m44642/m44646) to reverse-engineer
how upstream UniXNU **establishes** its Metal GPU driver, study it broadly, and
compile a Metal driver for this device (OnePlus 6 / enchilada / SDM845 / Adreno
A630) with a readiness to flash. Two read-only research passes and a direct
read of the authoritative sources established the following.

The upstream Metal stack is TWO layers, not one:

1. **Generation-independent** — a versioned kernel/user-client wire ABI, the
   Metal frontend (metal2vulkan → Turnip → NIR/IR3), the plugin discovery
   contract, the owner-trust module, and the payload/seed machinery.
2. **A740-specific** — the kernel backend `Adreno740`, the three signed
   firmware blobs (`a740_sqe.fw`, `gmu_gen70200.bin`, `a740_zap.mbn`), the
   A7xx hw_init/hwcg/protect tables, 3 MiB GMEM, `chip_id 0x43050a01`,
   A7xx CP framing, `highest_bank_bit 16`, `ubwc_version 4`.

`docs/porting/gpu.md:27-28` is decisive: *"For another GPU generation,
implement the matching kernel backend and compiler support before selecting
its frontend."* enchilada is **A630 / A6XX_GEN1**, not A740 / A7XX_GEN2, so
copying the sheng `libAdreno740.a` is not a valid port.

**The compiler is not the hard blocker.** Mesa Turnip upstream
(`src/freedreno/common/freedreno_devices.py`) already lists A630:
`add_gpus([GPUId(630)], A6xxGPUInfo(CHIP.A6XX, [a6xx_base, a6xx_gen1,
GPUProps(QCTDD04536579 = True)], num_ccu = 2, ... highest_bank_bit = 15,
macrotile_mode = 0, ...))`. `freedreno_dev_info.h` references
`a630_sqe.fw` three times and documents `CHIP.A6XX` GEN1 quirks. IR3 is the
shared compiler for both generations.

**The hard blockers remain** (measured in earlier rounds, unchanged here):
an A6XX_GEN1 kernel backend (legacy GMU HFI v1 + SPTPRAC + RPMh/PDC + kgsl
SMMU v2 context + CP/PM4), TrustZone PAS authentication of the signed ZAP
firmware (the 7 vendor blobs are present on the device, see
`evidence/stage5-gpu-device-firmware.md`, but none are authenticated into a
running GPU), a real stage-1 SMMU context for the kgsl bank
(`arm,smmu-kgsl@5040000`, phandle `0x1ac`, single-cell SIDs 0/2,1/4/5), and
a selected OS Metal/IOSurface ABI. Additionally every Mach-O/codesign/
hdiutil/xcrun step of the frontend build is macOS-Xcode-bound and cannot run
in this Windows workspace.

Therefore the honest "compile" deliverable of this round is **not** a runnable
Metal driver. It is the generation-independent **seam** that any A630 Metal
port must satisfy, expressed as pure C11, freestanding, compilable into
`libEnchiladaAdreno.a` and verifiable by the existing CI — with the A630 board
facts pinned to sources and the establishment chain's blockers stated
explicitly so no future round can mistake "the ABI header compiles" for "the
GPU works".

## What was built

| File | Role |
| --- | --- |
| `drivers/NativeAdreno/AdrenoMetalSeamV1.h` | Mirrors the upstream generation-independent wire ABI; 19 `struct enchilada_metal_abi_*` records + 20 `static_assert`s (the 16 upstream ones plus 4 reply-record layouts); the 16 methods `Query=1..BoImport=16`; A630 pins; 9 establishment stages; 5 blockers; 14 APIs. |
| `drivers/NativeAdreno/AdrenoMetalSeamV1.c` | Implements all 14 APIs with no libc (`zero_bytes` is a local loop); emits only 3 pinned DeviceInfo fields; refuses unknown ABI versions/flags; maps each stage to its honest blockers; returns `frontend_allowed()==0` for enchilada today. |
| `drivers/NativeAdreno/tests/adreno-metal-seam-v1-test.cpp` | 6 groups, 0 failures: wire-ABI mirror, layouts, params validation, A630 DeviceInfo, establishment chain, frontend gate. |
| `scripts/verify-enchilada-metal-seam.py` | Independent cross-verifier (method 2 of 2): re-derives Domain A from four SHA256-pinned sources, then checks Domain B (the module) against it. |
| `scripts/build-enchilada-adreno.py` | Wires `AdrenoMetalSeamV1.c` as the 20th archive unit and adds it to the build-report source set. |
| `scripts/check-enchilada-adreno-host.py` | Wires the 20th host suite. |

### The wire ABI is generation-independent

`oneplus6-ios/work/UniXNU/drivers/Adreno740/NativeAdrenoABI.h` says of itself
(lines 6-12): *"Versioned kernel/user-client wire ABI ... NOT the Metal, IOKit
or Turnip ABI and it does not describe an ioctl ... Unknown versions, methods
and flags are refused rather than ignored."* It is the contract Turnip compiles
against via `-Dtu-darwin-abi-dir=.../drivers/Adreno740`. Its constants —
`NativeAdrenoAbiVersion=2`, `MaxBuffers=128`, `MaxBoBytesShift=28` (256 MiB
per object), `MetadataBytes=64`, `MaxQueues=8`, `ParamsHostImport=1`, methods
1..16, and the 16 layout `static_assert`s — carry **no** A740 register,
firmware, GMEM, or chip-id value. The module mirrors them byte-for-byte in
layout while renaming the types to the `enchilada_metal_abi_*` namespace.

### The DeviceInfo fields come from the board, never a guess

`NativeAdrenoABI.h:127-128`: *"Fixed facts of the mounted GPU that the Vulkan
driver needs and cannot derive: all come from the board code, which takes them
from the pinned sources (a6xx catalog, UBWC config, the programmed UCHE
ranges), never from a guess."* `NativeAdrenoAbiDeviceInfo` has 12 fields.
Only three are **specified** by the pinned sources for enchilada today:

| Field | Value | Pinned source |
| --- | --- | --- |
| `chip_id` | `0x06030001` | `a6xx_catalog.c:883` `ADRENO_CHIP_IDS(0x06030001, 0x06030002)` AND DTB `:23641 qcom,chipid = <0x6030001>` |
| `gmem_bytes` | `0x00100000` (SZ_1M) | `a6xx_catalog.c` A630 `.gmem = SZ_1M` |
| `highest_bank_bit` | `15` | DTB `:23647 qcom,highest-bank-bit = <0x0f>` |

The other nine (`gmem_base`, `uche_trap_base`, `va_start`, `va_size`,
`ubwc_swizzle`, `macrotile_mode`, `ubwc_version`, `flags`, `reserved`) are
**board-derived at runtime** from the programmed UCHE ranges and the selected
OS ABI. `enchilada_metal_device_info_a630()` zeroes the whole 64-byte record
and fills ONLY those three, reporting `specified_mask = 0x43` (bits 0, 1, 6).
`enchilada_metal_device_info_consistent()` rejects any record that invents a
value for any of the eight board-derived fields (`BOARD_DERIVED_FIELDS =
DEVICE_FIELD_ALL & ~A630_SPECIFIED_FIELDS = 0x7bc`). This is the load-bearing
safety property: a future round cannot silently fabricate GMEM/UBWC/VA values
and pass this module's own consistency check.

Other A630 pins carried by the header: `chip_id_alt 0x06030002`,
`ubwc_mode 2` (DTB `:23649 qcom,ubwc-mode = <0x02>`), `zap_pas_id 13`
(DTB `:23612 qcom,pas-id = <0x0d>`), `zapfw "a630_zap.mdt"`
(catalog), `fw a630_sqe.fw / a630_gmu.bin` (catalog), `ADRENO_6XX_GEN1`,
`revn 630`, and **no** `address_space_size` in the A630 catalog entry.

### The establishment chain and its blockers

`stage_blockers()` maps the nine stages reverse-engineered from the upstream
build scripts to the five blocker bits honestly:

| # | Stage | Blockers today |
| --- | --- | --- |
| 1 | Fetch (SHA-pinned inputs) | none — pure Python, portable |
| 2 | metal2vulkan Rust bridge staticlib | macOS toolchain (Rust 1.98 / Apple target / LLVM>=21) |
| 3 | Mesa Turnip darwin-KMD dylib | macOS toolchain + selected OS ABI |
| 4 | owner-signed plugin bundle + daemon | macOS toolchain (codesign/CDHash) |
| 5 | owner trust module | none — pure Python, portable |
| 6 | payload seeding (`.dbr` / `NGPUADD2`) | none — pure Python, portable |
| 7 | kernel backend `Adreno740` → force_load | GEN1 backend + signed ZAP/PAS + kgsl SMMU context |
| 8 | staged IOAccelerator accelerator | GEN1 backend + selected OS ABI |
| 9 | backboardd discovery (`.publish`/`.clients`) | selected OS ABI + macOS toolchain |

The OR of all nine is `0x1f` = `TODAY_BLOCKERS`: MacOSToolchain(1) |
Gen1Backend(2) | SignedZap(4) | SmmuContext(8) | OsAbi(0x10).
`compiler_a630_available()` returns 1 (Mesa `GPUId(630)`/`a6xx_gen1`), but
`frontend_allowed(gen1_backend, compiler, signed_zap, smmu_context, os_abi)`
requires ALL FIVE prerequisites and returns **0** for enchilada today
`(0,1,0,0,0)`. That is the honest answer to "can we build the Metal frontend
now": no.

### Window constants — correcting a common misreading

The user's description (m44639) of a 1 GiB render-server window and 256 MiB
per-app windows is consistent with `docs/drivers/adreno740.md:45`, but the
exact values live in `NativeAdrenoPublish.cpp:112-114`: slot 0 is
`{0x20000000, 0x40000000}` = 1 GiB, and slots 1-3 are `0x0AAA9000`
(~170.6 MiB) each inside a 2 GiB aperture `0x20000000..0x80000000`. The
"256 MiB" is `NativeAdrenoABI.h:16` **`MaxBoBytesShift = 28` (per-object
bound)**, not a per-app window. These are A740 board-policy values; the
equivalent A630/SDM845 window policy is not derived by this round.

## Two independent verifications (m03546)

1. **Strict freestanding compile + C++ host test.** `clang -std=c11 -target
   aarch64-apple-darwin -ffreestanding -fno-builtin -O1 -Wall -Wextra -Werror
   -Wconversion -Wsign-conversion -Wshadow -pedantic` produced a 4176-byte
   arm64 Mach-O (magic `cf fa ed fe`); the host test printed
   `PASS: enchilada A630 Metal establishment-chain seam (6 test groups, 0
   failures)` with exit 0. Two genuine defects were caught here and fixed:
   an `int`-to-`unsigned` sign-conversion in the mask macros (C enum
   enumerators are `int`, so `~SPECIFIED_FIELDS` was signed), and wrong test
   literals (the module was self-consistent at `0x43`/`0x7bc`; the TEST had
   assumed `0x7`/`0x7f8`).
2. **`scripts/verify-enchilada-metal-seam.py`** — domain A re-derives
   everything from four SHA256-pinned sources without reading the module:
   `NativeAdrenoABI.h` (7733 B, `20ac9298bb390d86788c1fc58155e1f9581ffa942e222752efc95c67cb5670d2`),
   `a6xx_catalog.c` (51692 B, `6d9061ee08308126adc7b7250810b0223ee01c678e7afab6eb1738e53524c24c`),
   `enchilada.dts` (784857 B, `e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275`),
   and `docs/porting/gpu.md` §1. Domain B then checks the module's constants,
   16 methods, 12 DeviceInfo fields, 20 asserts, six A630 pins, mask
   arithmetic, 9 stages, 5 blockers, `0x1f`, the `.c` honesty (exactly 3
   fields filled, all 8 derived fields rejected, params gated, compiler=1,
   five-way frontend gate), and the 6 test groups. Result:
   `METAL_SEAM_CROSS: PASS (80 checks, 0 failures)`. Three verifier defects
   of my own were found and fixed during this run (a doubled path prefix, a
   line-wrapped prose quote, and the final enum member having no trailing
   comma) — none were module defects.

Also re-run: the full 20-suite `check-enchilada-adreno-host.py` (0 failures,
explicit clang toolchain) and `porting-record.py check --device <DEV>` →
`enchilada: 15 structured areas checked`.

## Honest boundary

- This is **not a GPU driver** and **not a Metal frontend**. It is a C ABI
  seam: it maps registers to no hardware, submits no commands, allocates no
  BOs, maps no IOVA, powers no domain, loads no firmware, publishes no
  `MTLDevice`, and is not reachable from any IOKit user client.
- It does **not** unblock stage 5. TrustZone PAS, the A6XX_GEN1 kernel
  backend, the kgsl SMMU context, and the OS Metal/IOSurface ABI are still
  open. `CONFIG_XNU_NATIVE_ADRENO=n` is unchanged.
- The archive has NOT been rebuilt locally: this Windows workspace lacks the
  recorded XNU `ios-obj` tree. Only the macOS CI (`keyfan/unixnu-build`,
  Xcode 26.6) can link it; a CI run proving the 20th object links is the next
  verification step, not a device result.
- No device run, no flash, no CI result is claimed here. The `gpu` area
  stays `host-verified`.
- Prior rounds 61-64 accumulated in the local index (ClkCtlV1,
  PowerUpBackendV1, GpuccWindowV1, CallerV1) are consolidated in the same
  two-phase push; they were all host-verified with passing independent
  verifiers and contain no device-verified overclaim.

## Chain

Previous remote head: `7b6300482e1059ab6606b47fa869fd9f80bed35e` (ROUND 60,
CI #231 15-object green build). This round adds the 20th archive unit and
the 20th host suite.
