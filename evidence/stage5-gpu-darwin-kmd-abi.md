# Stage 5 GPU - A630 Darwin-KMD ABI directory contract (ROUND 66, HOST-VERIFIED)

**Status: HOST-VERIFIED.** No device run, no flash, no MMIO, no firmware load,
no MTLDevice, no IOKit user client, no Turnip dylib build. `CONFIG_XNU_NATIVE_ADRENO`
remains `n`. The `gpu` area of `porting.toml` STAYS `"host-verified"`.
It does **not** unblock stage 5. Prev head: `d971cc78dc06eda2ab8b6462073c44536a94208d`.

## Why this round

Round 65 concluded "the compiler is not the hard blocker" from Mesa **main**'s
`freedreno_devices.py`. But `oneplus6-ios/work/UniXNU/scripts/data/adreno-compiler-inputs.json:5`
builds Mesa at the **exact commit** `85fda3b36405bcaea8bffc64aa4f2651dc90770d`
and applies `patches/mesa/turnip-darwin-kmd.patch`. Per m12247 ("先核验再上机"),
the claim had to be re-checked at the real build pin, and the Turnip side of the
two-layer stack had to be read rather than inferred.

## Two independent pre-verifications

1. `work/tmp/_r66_verify_head.py` (urllib + raw http.client, two network stacks):
   remote `main == d971cc78dc06eda2ab8b6462073c44536a94208d` (round-65 CI #232 p4),
   parent `9701e0f1…`. `R66_HEAD_VERIFY: PASS`.
2. Pinned Mesa re-check: TWO `web_fetch` reads of the SAME commit through two
   different GitLab URL shapes both returned HTTP 200 with real bytes:
   - `https://gitlab.freedesktop.org/mesa/mesa/-/raw/85fda3b36405bcaea8bffc64aa4f2651dc90770d/src/freedreno/common/freedreno_devices.py`
   - `https://gitlab.freedesktop.org/api/v4/projects/mesa%2Fmesa/repository/files/src%2Ffreedreno%2Fcommon%2Ffreedreno_devices.py/raw?ref=85fda3b36405bcaea8bffc64aa4f2651dc90770d`
   Both contain `add_gpus([ GPUId(630), ], A6xxGPUInfo( CHIP.A6XX,
   [a6xx_base, a6xx_gen1], num_ccu = 2, ... highest_bank_bit = 15,
   macrotile_mode = 0, ... ))` and `a6xx_gen1 = GPUProps(reg_size_vec4 = 96,
   instr_cache_size = 64, ...)`. A740 is a DIFFERENT generation
   (`GPUId(740)` / `0x43050a01` / `CHIP.A7XX` / `[a7xx_base, a7xx_gen2]` /
   `num_ccu = 6` / `highest_bank_bit = 16`). The A630 compiler support is
   therefore present at the exact commit UniXNU builds, not just on main.
   The Python/urllib egress is blocked by GitLab's Anubis anti-bot HTML
   (7434 B `<title>Making sure you're not a bot!</title>`) for both shapes;
   that failure is recorded, not hidden. The Chromium mirror, raw.githubusercontent,
   jsdelivr and `?ref_type=heads` variants all failed (fetch error / timeout /
   404). The real bytes were recovered from the `web_fetch` spill and pinned
   locally (`work/tmp/_r66_pin_setup.py`, `R66_PIN_SETUP: PASS`).

## What `tu-darwin-abi-dir` actually is (source read, not inferred)

`oneplus6-ios/work/UniXNU/patches/mesa/turnip-darwin-kmd.patch`:
- `:15-20` declares `option('tu-darwin-abi-dir', type : 'string', value : '',
  description : 'Directory holding NativeAdrenoABI.h for the Turnip darwin
  kernel backend (freedreno-kmds=darwin)')`.
- `:52-59` in the `freedreno_kmds.contains('darwin')` branch adds
  `-DTU_HAS_DARWIN`, `libtu_files += files('tu_knl_darwin.cc')`,
  `libtu_includes += include_directories(get_option('tu-darwin-abi-dir'),
  is_system : false)`, the IOKit/CoreGraphics/Foundation/QuartzCore frameworks,
  and `-Wl,-undefined,dynamic_lookup`.
- `:290` adds exactly one project include: `+#include "NativeAdrenoABI.h"`.
  The backend consumes ~19 `NativeAdrenoAbi*` records, checks
  `dw.params.abi_version != NativeAdrenoAbiVersion` (`:411`), and splits
  submissions at `NativeAdrenoAbiMaxBuffers` (`:1096`, `:1152`).
- `grep` of the patch finds NO `freedreno_devices` / `GPUId(630)` /
  `a6xx_gen1` / `CHIP.A6XX` edit: the Darwin KMD patch does not touch the device
  table. A630 support comes from Mesa itself.

`oneplus6-ios/work/fajita-out/fajita/tree/scripts/build-turnip-darwin.py:41-45`
passes `-Dfreedreno-kmds=darwin` and
`-Dtu-darwin-abi-dir=<ROOT>/drivers/Adreno740`. **Conclusion (load-bearing):**
the option is a DIRECTORY CONTRACT requiring exactly ONE header named
`NativeAdrenoABI.h`; the other 30 `NativeAdreno*.h` files in
`drivers/Adreno740/` are kernel-backend internals, NOT compiler inputs. The
wire ABI is generation-independent, so the A630 board may publish the SAME
symbol names and record layouts the A740 board does.

## Deliverable (pure C, freestanding, CI-compilable)

| File | Purpose |
| --- | --- |
| `drivers/NativeAdreno/AdrenoDarwinKmdAbiV1/NativeAdrenoABI.h` | Drop-in candidate for `-Dtu-darwin-abi-dir=`: mirrors upstream `NativeAdrenoABI.h` in all 8 constants, 16 method names/values, 20 struct names, 16 `static_assert` sizes (enforced in BOTH the C++ and C11 branches), plus the A630 board pins and the field-mask API. |
| `drivers/NativeAdreno/AdrenoDarwinKmdAbiV1/AdrenoDarwinKmdAbiV1.c` | Freestanding (no libc) implementation of the 6 enchilada-prefixed APIs. |
| `drivers/NativeAdreno/tests/adreno-darwin-kmd-abi-v1-test.cpp` | 6 host test groups. |
| `scripts/verify-enchilada-darwin-kmd-abi.py` | Method-2 cross-verifier, 176 checks. |
| `scripts/build-enchilada-adreno.py`, `scripts/check-enchilada-adreno-host.py` | Wired as the 21st archive object / 21st host suite. |

## A630 DeviceInfo: only sha256-pinned fields, never a guess

Upstream `NativeAdrenoABI.h:127-128`: "Fixed facts of the mounted GPU that the
Vulkan driver needs and cannot derive: all come from the board code, which
takes them from the pinned sources (a6xx catalog, UBWC config, the programmed
UCHE ranges), never from a guess."

| Field | Value | Source |
| --- | --- | --- |
| `chip_id` | `0x06030001` | DTB `:23641`, catalog `:883-886` |
| `chip_id` alt | `0x06030002` | catalog `:885` |
| `gmem_bytes` | `0x00100000` (SZ_1M) | catalog `:893` |
| `highest_bank_bit` | `15` | DTB `:23647` (0x0f) AND pinned Mesa `highest_bank_bit = 15` |
| `macrotile_mode` | `0` (four channel) | pinned Mesa `macrotile_mode = 0` |
| `ubwc_mode` | `2` (raw board fact, NOT a DeviceInfo field) | DTB `:23649` |
| `zap_pas_id` | `13` | DTB `:23612` (0x0d) |

`ENCHILADA_A630_SPECIFIED_FIELDS = 0x143` (bits 0,1,6,8);
`ENCHILADA_A630_BOARD_DERIVED_FIELDS = 0x6bc` (the other 7 fields). The
producer zeroes the whole 64 B record and fills ONLY those 4 fields;
`enchilada_darwin_device_info_consistent()` refuses a wrong mask, a wrong pin,
a NULL record, or ANY fabricated board-derived field (all 8 remaining fields
must be zero). This is stricter than round 65's 3-field version: the pinned
Mesa device table now supplies `macrotile_mode` too.

## Seven sha256-pinned upstream sources (Domain A)

| Pin | File | Bytes | sha256 |
| --- | --- | --- | --- |
| P1 | `work/UniXNU/drivers/Adreno740/NativeAdrenoABI.h` | 7733 | `20ac9298bb390d86788c1fc58155e1f9581ffa942e222752efc95c67cb5670d2` |
| P2 | `scripts/data/freedreno_devices.85fda3b3.py` (vendored, TRACKED) | 53914 | `3a343e614b30509a01db4a22380eaa4604abafbb59552cd8b177c5912b29aef6` |
| P3 | `notes/mu-silicium/enchilada.dts` | 784857 | `e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275` |
| P4 | `.cache/adreno-a630-source/.../a6xx_catalog.c` | 51692 | `6d9061ee08308126adc7b7250810b0223ee01c678e7afab6eb1738e53524c24c` |
| P5 | `work/UniXNU/scripts/data/adreno-compiler-inputs.json` | 2550 | `04089993e4f4d268b660bb0e6eba3d202225ab93389b8e68d67e10244381dcad` |
| P6 | `work/UniXNU/patches/mesa/turnip-darwin-kmd.patch` | 56241 | `7f4690131a05133547af55efda9ed9b4fcd10d8b5e420cccbcfa64b97eda8911` |
| P7 | `work/fajita-out/fajita/tree/scripts/build-turnip-darwin.py` | 7931 | `5db3832ee578d85f82840ef218a0450430bf004641526c0932a3e9d7c2999a53` |

P2 is the real byte content at Mesa commit `85fda3b36405bcaea8bffc64aa4f2651dc90770d`,
recovered from the `web_fetch` spill (the earlier anti-bot garbage file was
overwritten; `R66_PIN_SETUP: PASS` with 2 independent verifications). It is
vendored into `scripts/data/` (TRACKED) rather than left in the gitignored
`.cache/`: GitLab's Anubis anti-bot challenge blocks plain urllib re-fetch, so
a pushed verification command must be reproducible from the repository alone.
`Copy-Item` byte-for-byte, SHA256 identical, pure LF.

## Three independent verifications (all PASS)

1. `work/tmp/_r66_strict.py`: strict `-Wconversion -Wsign-conversion -Wshadow
   -pedantic -Werror` freestanding C11 compile → arm64 Mach-O object, magic
   `cf fa ed fe`, 2232 B; strict C++17 host link + run →
   `PASS: enchilada A630 Darwin-KMD ABI directory contract (6 test groups, 0
   failures)`. `R66_STRICT: PASS`.
2. `scripts/verify-enchilada-darwin-kmd-abi.py` →
   `DARWIN_KMD_ABI_CROSS: PASS (176 checks, 0 failures)`. Domain A re-derives
   constants/methods/structs/layouts from P1, the A630 facts from P2/P3/P4/P5,
   and the ONE-HEADER directory contract from P6/P7; Domain B audits the new
   `.h/.c/test` against them. One checker defect was found and fixed: a fixed
   +/- window around `0x06030001` also caught the neighbouring A615/A640
   entries (which DO set `.address_space_size`), so the slice is now bounded by
   the entry's own `.chip_ids` markers.
3. `scripts/check-enchilada-adreno-host.py` → **21 suites PASS**, tail
   `PASS: enchilada A630 catalog/.../metal-seam-v1/darwin-kmd-abi-v1 host
   tests`. Record checker → `enchilada: 15 structured areas checked`.

`R66_THREE_VERIFICATIONS: PASS`.

## Honest boundary

This is a **header/directory contract**, NOT a GPU driver and NOT a compiled
Turnip dylib. It does not map GPU registers, allocate BOs, translate IOVA,
submit IBs, load firmware, publish an `MTLDevice`, or open an IOKit user
client. Building the real Turnip darwin-KMD dylib needs `xcrun --sdk iphoneos`,
`-fobjc-arc`, Apple frameworks, `codesign` and `hdiutil` — all macOS/Xcode,
absent locally. The four hard blockers of `docs/porting/gpu.md` section 1
remain open: (1) an A6XX_GEN1 kernel backend, (2) signed ZAP / TrustZone PAS,
(3) the kgsl SMMU stage-1 context, (4) the selected OS Metal/IOSurface ABI.
No device run was performed, no flash was performed, the standing flash
authorization (`m39510`) was NOT exercised. The `gpu` area STAYS
`"host-verified"`.
