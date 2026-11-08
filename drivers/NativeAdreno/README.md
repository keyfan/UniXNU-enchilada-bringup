# NativeAdreno — enchilada stage 5 (GPU) feasibility study (HOST-VERIFIED)

**This is not a GPU driver.** It is a small, freestanding, pure-C encoding of the
authoritative stage-5 hardware/driver facts plus classification and gating
functions that a future A630 kernel backend could call. It maps nothing, writes
no register, loads no firmware, submits no command, and is **not linked into the
kernel**. It exists so the stage-5 decision is recorded in tested code and can be
host-verified before any ABI-sensitive work.

## Why this module exists

`porting.toml` gpu area was `planned` with sheng's six A740/SM8550 components.
Three independent research passes (2026-11-08) established that the full
stage-5 Metal path is **blocked on this device now**:

| # | Source | Load-bearing fact |
|---|--------|-------------------|
| 1 | Linux v6.12 `drivers/gpu/drm/msm/adreno/a6xx_catalog.c` | A630 is `ADRENO_6XX_GEN1`, `revn=630`, `gmem=SZ_1M`, fw `{a630_sqe.fw, a630_gmu.bin}`, `zapfw=a630_zap.mdt`. A740 is `ADRENO_7XX_GEN2`, chipid `0x43050a01`, `gmem=3*SZ_1M`, `gmu_chipid=0x7020100`, `address_space_size=SZ_16G`, fw `{a740_sqe.fw, gmu_gen70200.bin}`. |
| 1b | Linux v6.12 `drivers/gpu/drm/msm/adreno/a6xx_gmu.c` | `a6xx_gmu_init()`: `} else if (adreno_is_a630_family(adreno_gpu)) { /* HFI v1, has sptprac */ gmu->legacy = true; ...` → A630 = legacy GMU (HFI v1 / SPTPRAC), not the GEN2 GMU. |
| 2 | vendor DTB `Mu-Silicium/Resources/DTBs/enchilada.dts` | `:23632 qcom,kgsl-3d0@5000000 reg=<0x5000000 0x40000 ...>`, `qcom,chipid=<0x6030001>`, 6 clocks incl `gmu_clk`, `qcom,gmu` present; `:23610 qcom,kgsl-hyp qcom,firmware-name="a630_zap"`. |
| 3 | `https://unixnu.com/zh/porting/gpu` §1 | 逐字：「其他 GPU 代际需要先实现对应内核后端和编译器支持，再选择前端」 — other GPU generations need a matching-generation kernel backend + compiler *before* a Metal frontend. sheng's six archives are A740-only. |
| 3b | workspace inventory | All proprietary A630 firmware (`a630_zap.mbn`, `a630_gmu.bin`, `a630_sqe.fw` + mdt/b00/b01) **absent**; `CONFIG_XNU_NATIVE_ADRENO=n`; no `[gpu]` section in `board.toml`; `sheng-gpu-addon=1` carries only a wallpaper PNG. |
| 3c | 246 decoded device logs | GPU-driver literals all zero (`IOGPU`/`IOAccelerator`/`AGX`/`Metal`/`[gpu]`/`CoreImage`/`mach_vm_allocate_kernel`/`Adreno`/`kgsl`/`GMU` = 0). `G st=` rows are DWC3 USB-gadget state, not GPU. The earlier "SpringBoard CoreImage GLContext crash" is an external diag2-k158 inference with **no literal log evidence**; real CRASH rows are `PerfPowerServices sig6`. |

**Decision:** no A630 Metal path is feasible in this environment today. Rather
than copy sheng's cross-generation archives (which the official guide forbids),
this module records the A630 identity, the exact firmware set required, and a
`adreno_catalog_assess()` gate that returns `ADRENO_BLOCK_FIRMWARE |
ADRENO_BLOCK_BACKEND` for enchilada and `ADRENO_FEASIBLE` only for a
matching-generation backend with all firmware present.

The exact chip-id **register** offset is deliberately NOT asserted: the
`a6xx.xml` register database has no `CHIP_ID` field. `qcom,chipid=<0x6030001>`
is a DT property, not a proven register value; a future device probe must source
the offset before reading it.

## Files

- `AdrenoCatalog.h` / `AdrenoCatalog.c` — catalog + classification + gating.
- `tests/adreno-catalog-test.cpp` — 10 host test groups (A630/A740 lookup,
  cross-generation gap, unknown-chip refusal, firmware names/masks, blocked
  assessment, A740 reference feasible case, DT chipid + kgsl window).
- `scripts/check-enchilada-adreno-host.py` — C11 + C++17 host runner, mirrors
  `scripts/check-enchilada-dsi-backlight-host.py` (sanitizers when available).

## Verification (HOST-VERIFIED)

```
# from oneplus6-ios/work/UniXNU-enchilada, TMP/TEMP=work/tmp/ctmp,
# PATH += oneplus6-ios/tools/llvm-mingw/bin
python scripts/check-enchilada-adreno-host.py \
    --cc oneplus6-ios/tools/llvm-mingw/bin/clang.exe \
    --cxx oneplus6-ios/tools/llvm-mingw/bin/clang++.exe \
    --out work/tmp/adreno-host-out
```

Strict compile (independent verification):

```
clang -std=c11 -Wall -Wextra -Werror -pedantic \
    -Wconversion -Wsign-conversion -Wshadow -O2 \
    -c drivers/NativeAdreno/AdrenoCatalog.c -o /tmp/AdrenoCatalog.o
```

Both must pass. This is a **host** claim only: no CI compile, no flash, no
device verification. `porting.toml` gpu area is `host-verified` with a `host`
evidence entry; `evidence/stage5-gpu-feasibility.md` records the audit.

## Proves / Does NOT prove

- **Proves:** A630 vs A740 are different generations with different
  firmware/GMU/GMEM/address-space; the three required proprietary blobs are
  absent; under the official guide a matching-gen backend is mandatory; the
  catalog + gate encode this faithfully (10 host tests).
- **Does NOT prove:** any GPU register value on the device, any firmware load,
  GMU/HFI start, command submission, Metal/IOGPU ABI behaviour, or any rendered
  image. No optical/device readback was performed. None of the six sheng GPU
  components are implemented.

## Next action (stage 5)

To make stage 5 device-verifiable one would first need: (a) the three signed
A630 firmware blobs from a matching OS/firmware set; (b) an A6XX_GEN1 kernel
backend (legacy GMU HFI v1 + SPTPRAC + RPMh/PDC power sequence + kgsl SMMU v2
context + CP/PM4 submission) — a new backend, not a port of sheng's A740; and
(c) a selected OS Metal/IOSurface ABI + Turnip/AirNir compiler support. Only
then could a read-only chipid probe be flashed, followed by GMU bring-up and a
known-image render. Until (a)–(c) exist, stage 5 stays `host-verified` and
`blocked on firmware + backend`.
