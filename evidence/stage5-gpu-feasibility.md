# Stage 5 (GPU) host evidence: A630 identification + feasibility audit

**Status: HOST-VERIFIED only. No CI compile, no flash, no device verification,
no firmware load, no register access, no Metal/IOGPU ABI work.**

- Date: 2026-11-08
- Device: OnePlus 6 (enchilada, SDM845), iPadOS 17.7.11 (21H461, iPad7,1)
- Record parent commit at time of writing: `d83634af814ca3439d2512b8886801c57a2d3279`
- Module: `drivers/NativeAdreno/` (`AdrenoCatalog.h/.c`, `tests/adreno-catalog-test.cpp`,
  `README.md`) + `scripts/check-enchilada-adreno-host.py`
- Host runner command (verification 1):
  `python scripts/check-enchilada-adreno-host.py --cc oneplus6-ios/tools/llvm-mingw/bin/clang.exe --cxx oneplus6-ios/tools/llvm-mingw/bin/clang++.exe --out work/tmp/adreno-host-out`
- Independent verification 2 (strict compile + byte dump):
  `clang -std=c11 -Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion -Wshadow -O2 -c drivers/NativeAdreno/AdrenoCatalog.c` → exit 0;
  linked dump printed `chip=06030001 gen=1 revn=630 gmem=00100000 legacy=1`,
  `assess(A630, fw=0, backend=0)=06`, `assess(A740, fw=ALL, backend=1)=00`.

## Why this audit was done

`porting.toml` gpu area was `planned`, listing sheng's six A740/SM8550 GPU
components (`Adreno740`, `AdrenoMetal`, `AdrenoFirmware`, `AdrenoPM4`, `AirNir`,
`Sm8550Platform`) and an old `next_action` that asked for an A630-vs-A740
register/firmware inventory before copying any sheng archive. Per the standing
constraints ("每步两次验证、先核验再上机"), three independent research passes
were completed **before** any code or flash.

## Three independent research passes

| Pass | Source | Key finding |
|------|--------|-------------|
| 1 | Linux v6.12 `drivers/gpu/drm/msm/adreno/a6xx_catalog.c` | A630: chipids `0x06030001/0x06030002`, `family=ADRENO_6XX_GEN1`, `revn=630`, `gmem=SZ_1M`, fw `{a630_sqe.fw, a630_gmu.bin}`, `zapfw=a630_zap.mdt`, `gmu_cgc_mode=0x00020202`. A740: chipid `0x43050a01 /* C510v2 */`, `family=ADRENO_7XX_GEN2`, `gmem=3*SZ_1M`, `gmu_chipid=0x7020100`, `address_space_size=SZ_16G`, fw `{a740_sqe.fw, gmu_gen70200.bin}`. |
| 1b | Linux v6.12 `drivers/gpu/drm/msm/adreno/a6xx_gmu.c` | `a6xx_gmu_init()`: `} else if (adreno_is_a630_family(adreno_gpu)) { /* HFI v1, has sptprac */ gmu->legacy = true; ...` → A630 uses a **legacy GMU** (HFI v1 + SPTPRAC), not the GEN2 GMU. GMU aspace is `0x0–0x80000000`. |
| 2 | vendor DTB `Mu-Silicium/Resources/DTBs/enchilada.dts` | `:23632 qcom,kgsl-3d0@5000000`: `reg=<0x5000000 0x40000 0x5061000 0x800 0x509e000 0x1000>`, `qcom,chipid=<0x6030001>`, 6 clocks incl `gmu_clk`, `qcom,gmu` present, IRQ 300. `:23610 qcom,kgsl-hyp qcom,firmware-name="a630_zap"`. `arm,smmu-kgsl@5040000`, `qcom,gpucc@5090000`. |
| 3 | Official guide `https://unixnu.com/zh/porting/gpu` §1 | 逐字「其他 GPU 代际需要先实现对应内核后端和编译器支持，再选择前端」 — other GPU generations need a matching-generation kernel backend + compiler *before* selecting a Metal frontend. §2 requires three signed firmware blobs; §3/§4 describe the A740-only `libAdreno740.a` + accelerator + Turnip/AirNir/Metal plugin flow. |
| 3b | Workspace inventory (`analyze_gpu_local.py`) | `CONFIG_XNU_NATIVE_ADRENO=n` (`configs/enchilada_defconfig:40`), `CONFIG_XNU_NATIVE_MOBILE_DISPLAY=n`, `CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y`; no `[gpu]` section in `board.toml` (only `:113 "com.apple.iokit.IOGPUFamily"` kext + `:389 sheng-gpu-addon=1` which is a wallpaper PNG); all 7 A630 firmware names (`a630_zap.mbn/mdt/b00/b01`, `a630_gmu.bin`, `a630_sqe.fw/mdt`) **absent**; 3 GPU-named local files (2 XNU patches + `NativeGpuTrace.h`), none A630-specific. |
| 3c | 246 decoded device logs (`analyze_gpu_logs.py`) | GPU-driver literals all zero: `IOGPU`/`IOAccelerator`/`AGX`/`IOGPUFamily`/`Metal`/`[gpu]`/`CoreImage`/`GLContext`/`mach_vm_allocate_kernel`/`Adreno`/`kgsl`/`GMU`/`SPTPRAC` = 0. The only `G st=` rows are DWC3 USB-gadget state (`dcfg/dctl/dsts`), not GPU. `a630`/`GPU` "hits" are backtrace hex noise (`1a630f174`). |

## Honest correction of a prior finding

The gpu area's earlier `findings` said SpringBoard crashed inside
`CI::GLContext::GLContext` with `mach_vm_allocate_kernel` failure (external
diag2-k158 triage). **This has no literal evidence in 246 decoded logs** —
`CoreImage`/`GLContext`/`mach_vm_allocate_kernel`/`vm_map_enter` are all zero.
The actual `CRASH` rows are `PerfPowerServices sig6` (SIGABRT). We keep that
entry marked `[HISTORICAL, unverified external inference]` for traceability and
do **not** claim the crash was reproduced or resolved. This matches the
stage-4 `evidence/stage4-welcome.md` conclusion.

## Decision (load-bearing)

The full stage-5 Metal path is **BLOCKED on this device now**:

1. A630 (6xx GEN1, legacy GMU HFI v1 / SPTPRAC / RPMh) and A740 (7xx GEN2)
   differ in firmware, GMU, protect table, GMEM and address space → sheng's six
   archives cannot be reused; the official guide forbids a frontend before a
   matching-gen backend + compiler.
2. Three proprietary A630 firmware blobs are absent from the workspace.
3. iOS Metal/IOGPU ABI is closed-source; this host has no macOS/Xcode (CI is
   the only ABI authority, cf. CI #207 / #211 lessons).
4. 246 device logs confirm no GPU driver is present today.

Instead of copying cross-generation code, `drivers/NativeAdreno` records the
A630 identity, the exact firmware set, and an `adreno_catalog_assess()` gate:
enchilada returns `ADRENO_BLOCK_FIRMWARE | ADRENO_BLOCK_BACKEND` (`0x06`), and
`ADRENO_FEASIBLE` (`0x00`) only for a matching-generation backend with all
firmware present (the A740/SM8550 reference case). The exact chip-id **register**
offset is deliberately NOT asserted: `a6xx.xml` has no `CHIP_ID` field, so
`qcom,chipid=<0x6030001>` is treated as a DT property, not a proven register.

## Two verifications (both PASS)

1. `check-enchilada-adreno-host.py` → `PASS: enchilada A630 identification +
   feasibility study (10 test groups)` + `PASS: enchilada A630 catalog host
   tests (without sanitizers)`, `HOST_TEST_EXIT=0`.
2. Strict `-Wconversion -Wsign-conversion -Wshadow -pedantic` C11 compile
   `STRICT_C_EXIT=0` + independent `adreno-dump` byte/verdict dump
   (`chip=06030001 gen=1 revn=630 gmem=00100000 legacy=1`, `assess=06/00`),
   `DUMP_RUN_EXIT=0`.

## Proves / Does NOT prove

- **Proves:** A630 vs A740 are different generations with different
  firmware/GMU/GMEM/address-space; the three proprietary blobs are absent;
  under the official guide a matching-gen backend is mandatory; the catalog +
  gate encode this faithfully (10 host tests, strict compile).
- **Does NOT prove:** any GPU register value read on the device, any firmware
  load, GMU/HFI start, command submission, Metal/IOGPU ABI behaviour, or any
  rendered image. No CI compile, no flash, no optical/device readback. None of
  the six sheng GPU components are implemented; `status` is `host-verified`,
  not `device-verified`.

## Next action (stage 5)

To become device-verifiable, stage 5 first needs: (a) the three signed A630
firmware blobs from a matching firmware set; (b) a new A6XX_GEN1 kernel backend
(legacy GMU HFI v1 + SPTPRAC + RPMh/PDC power sequence + kgsl SMMU v2 context
+ CP/PM4 submission) — not a port of sheng's A740; (c) a selected OS
Metal/IOSurface ABI + Turnip/AirNir compiler support. Only then could a
read-only chipid probe be flashed (after sourcing the exact register offset),
followed by GMU bring-up and a known-image render. Until (a)–(c) exist, stage 5
stays `host-verified` and blocked on firmware + backend; strict 8-stage order
then continues with stage 6 (network) / the remaining areas.
