# Stage 5 GPU — A630 BOARD FACTS, READ FROM THE LIVE DEVICE TREE (ROUND 71)

**Status: DEVICE-SOURCED, read-only.** This is the first round of this port that
reads the A630 GPU board facts from **the phone's own live device tree** rather
than from a vendor DTS file checked into a repository. It is a genuine
independent confirmation of `NativeAdrenoA630HwInit.h`, `AdrenoBoardInputs.h`,
`AdrenoDomainState.h`, `AdrenoClkCtlV1.h` and `AdrenoKGSLSmmuGate.h`.

It is **not** a GPU driver result: no GPU register was read or written, no
firmware was loaded, no MMIO was issued, nothing was mounted read-write, and no
flash was performed. `CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area of
`porting.toml` **STAYS `"host-verified"`** — see "Honest boundary" for why
device-sourced facts do not promote it. It does **not** unblock stage 5.

## Session (fully read-only)

| item | value |
| --- | --- |
| device | `27f4f45d`, OnePlus A6000 / `OnePlus6`, `ro.product.device=OnePlus6` |
| environment | recovery (TWRP), `uid=0(root)` `context=u:r:su:s0` |
| kernel | `ro.build.version.release=10` |
| boot id | `bb93c06b-ea20-4073-8836-fa26c5275240` (a distinct boot from every earlier round's flash) |
| uptime at first read | 102978.33 s |
| operations | `getprop`, `ls`, `find`, `od` over `/proc/device-tree` only |
| NOT done | no mount, no write, no `dd of=`, no reboot, no flash, no BCB/misc touch, no GPU register access |

`adb` came from `C:\platform-tools\adb.exe` (1.0.41 / 37.0.0); the tool is not on
PATH in this workspace, which is why earlier rounds recorded "no device".

## Facts read, and the model each one confirms

All values below are raw big-endian bytes from `/proc/device-tree`, decoded.

| fact | device bytes | decoded | confirms |
| --- | --- | --- | --- |
| `qcom,kgsl-3d0/qcom,chipid` | `06 03 00 01` | **0x06030001** | `ENCHILADA_A630_CHIP_ID` (`NativeAdrenoABI.h`), `ADRENO_CHIP_A630_V1`, catalog `:884` |
| `qcom,kgsl-3d0/qcom,highest-bank-bit` | `00 00 00 0f` | **15** | `ENCHILADA_A630_HIGHEST_BANK_BIT`, `AdrenoMetalSeamV1.h` |
| `qcom,kgsl-3d0/qcom,ubwc-mode` | `00 00 00 02` | **2** | `ENCHILADA_A630_UBWC_MODE` |
| `qcom,kgsl-3d0/qcom,id` | `00 00 00 00` | 0 | single GPU instance |
| `qcom,kgsl-hyp/qcom,pas-id` | `00 00 00 0d` | **13** | `GPU_PAS_ID 13` — the round-69/70 value, now DEVICE-confirmed |
| `qcom,kgsl-hyp/qcom,firmware-name` | `61 36 33 30 5f 7a 61 70 00` | **"a630_zap"** | `kEnchiladaGen1ZapDtsName`, catalog `.zapfw` family |
| `qcom,kgsl-3d0/reg` | `05 00 00 00 00 04 00 00` / `05 06 10 00 00 00 08 00` / `05 09 e0 00 00 00 10 00` | **0x05000000+0x40000**, **0x05061000+0x800**, **0x0509e000+0x1000** | `ADRENO_BOARD_GPU_BASE/SIZE`, `_GPU_CX_DBGC`, `_GPU_CX_MISC` |
| `qcom,kgsl-3d0/clock-names` | NUL-separated | **core_clk, rbbmtimer_clk, mem_clk, mem_iface_clk, gmu_clk, l3_vote** (6) | `AdrenoPowerUpV1BoardGpuClockCount()` == 6 |
| `qcom,gmu/clock-names` | NUL-separated | **gmu_clk, cxo_clk, axi_clk, memnoc_clk** (4) | `AdrenoPowerUpV1BoardGmuClockCount()` == 4 |
| `qcom,gmu/reg` | `05 06 a0 00 00 03 00 00` / `0b 20 00 00 00 30 00 00` | **0x0506a000+0x30000**, **0x0b200000+0x300000** | `ADRENO_BOARD_GMU_BASE/SIZE`, PDC window |
| `qcom,gmu/compatible` | `qcom,gpu-gmu` | single string | vendor GMU node (not mainline `qcom,adreno-gmu`) |
| `arm,smmu-kgsl@5040000/reg` | `05 04 00 00 00 01 00 00` | **0x05040000+0x10000** | `ADRENO_BOARD_KGSL_SMMU_BASE/SIZE`, `AdrenoKGSLSmmuGate` |
| `qcom,gpucc@5090000/reg` | `05 09 00 00 00 00 90 00` | **0x05090000+0x9000** | `ADRENO_DOMAIN_GPUCC_BASE/SIZE`, `AdrenoGpuccWindowV1` |
| `apps-smmu@0x15000000/reg` | `15 00 00 00 00 08 00 00` … / `15 0c 20 00 00 00 00 20` | **0x15000000+0x80000** and **0x150c2000+0x20** | `ADRENO_BOARD_APPS_SMMU_BASE` (first window) |
| `qcom,kgsl-3d0/interrupts` | `00 00 00 00 00 00 01 2c 00 00 00 00` | SPI **300** (`0x12c`), level high | matches the vendor DTS `interrupts = <0 0x12c 0>` |

The node list also confirms the node **names** every earlier round argued about:
`arm,smmu-kgsl@5040000` (the vendor name the mainline dtsi does **not** contain),
`qcom,kgsl-hyp`, `qcom,kgsl-iommu`, `qcom,gmu`, `qcom,gpucc@5090000`.

## What this round establishes, and what it does not

**Establishes:** every board constant this port has been asserting since round 20
is correct **on this specific handset at this specific boot**. In particular the
two clock counts are confirmed as *different* (6 GPU / 4 GMU) on real hardware,
which was a load-bearing distinction in round 51; and `GPU_PAS_ID 13` — the value
round 69 had to derive from two Linux pins because the sheng tree has no PAS code
— is confirmed directly by `qcom,pas-id = <0x0d>` on the device.

**Does NOT establish:** any register *value*. A device tree says where a block
is and how it is wired; it says nothing about whether the block is powered, what
its status words read, or whether it responds. The A630 client GPU windows stay
ALL-ZERO in the models (round 67 gate), because a board VA map is still absent —
and nothing in `/proc/device-tree` supplies one.

**Why the area does not move:** device-sourced *static* facts are not a device
*run* of the driver. `gpu` stays `"host-verified"` because no GPU code executed:
the distinction this port has maintained since round 38 is between "an input was
read from the phone" (done here, read-only) and "the port's own code ran on the
phone" (never done; blocked on the four hard blockers).

## Reproduce

```
C:\platform-tools\adb.exe shell "od -An -tx1 -v /proc/device-tree/soc/qcom,kgsl-3d0@5000000/qcom,chipid"
C:\platform-tools\adb.exe shell "od -An -tx1 -v /proc/device-tree/soc/qcom,kgsl-hyp/qcom,pas-id"
C:\platform-tools\adb.exe shell "od -c   -v /proc/device-tree/soc/qcom,gmu/clock-names"
C:\platform-tools\adb.exe shell "od -An -tx1 -v /proc/device-tree/soc/arm,smmu-kgsl@5040000/reg"
```

Note for future rounds: TWRP's `/sbin/sh` mishandles nested single quotes, so a
`for` loop written inline over `adb shell` returns garbage (it silently produced
`;=od=-An=...` on the first attempt here). Issue one `od` per property instead.

## Honest boundary

READ-ONLY DEVICE-SOURCED STATIC FACTS. No GPU register read or write, no MMIO, no
firmware load, no PAS authentication, no SMMU programming, no flash, no reboot, no
mount. `m39510` NOT exercised. `CONFIG_XNU_NATIVE_ADRENO` remains `n`. The A630
client GPU windows stay all-zero. The four hard blockers of
`docs/porting/gpu.md` §1 remain open. The `gpu` area **STAYS `"host-verified"`**;
this does **not** unblock stage 5.

Chain: round 70 closed at `650ead4d43d429fc45c251cdac4a4e3000b3291f`;
round 71 phase 3 (CP-ring constant derivation) is
`256caedb61cf881ca683a4b29eccfa15ea62abd0`.
