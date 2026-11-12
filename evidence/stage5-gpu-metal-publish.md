# Stage 5 GPU — A630 Metal publish / discovery / client-window policy contract

**ROUND 67 — HOST-VERIFIED (2026-11-12).**

**Status: HOST-VERIFIED.** This is NOT a GPU driver, NOT a Metal plugin, and NOT
a device measurement. No device run, no flash, no CI-built frontend. The `gpu`
area of `porting.toml` **STAYS `"host-verified"`**. The standing flash
authorization (`m39510`) was **not** exercised. `CONFIG_XNU_NATIVE_ADRENO`
remains `n`. It does **not** unblock stage 5.

## What this round answers (user m44634 / m44638 / m44639 / m44640 / m44642 / m44646)

m44639 described the layer where *the render server gets a whole 1 GiB GPU
window and listed apps each get a Metal device with their own window and shader
cache*. Reverse-engineering the upstream establishment chain shows that this
policy splits cleanly into TWO parts:

| Part | Generation-independent? | This round |
| --- | --- | --- |
| Data-seed markers, `IOAcceleratorES -> MetalPluginName -> NSBundle -> initWithAcceleratorPort:` discovery, `--publish` gate, `backboardd` routing, per-process allow-list, user-client open-type routing, connection caps, scanout composite | **YES** | Mirrored as pure C (`AdrenoMetalPublishV1`) |
| GPU address window base/size (`kClientWindows`), the A740 kernel backend, signed ZAP, GMU, CP/PM4 framing, kgsl SMMU context | **NO — board/generation-derived** | **Refused**: all four A630 windows stay ZERO |

`docs/porting/gpu.md` section 1 is decisive: *"For another GPU generation,
implement the matching kernel backend and compiler support before selecting its
frontend."* `NativeAdrenoABI.h:127-128` is equally decisive for the windows:
*"all come from the board code, which takes them from the pinned sources ...
never from a guess."* enchilada is A630 (6xx GEN1), NOT sheng's A740.

## Why "compile" still means a pure-C archive object (CI capability check)

`work/tmp/kernel.yml` (the `keyfan/unixnu-build` macOS-26 kernel CI, sha256
`6629ee5180434b7ba8d638f50542ecd83b1f373240a0efcf4c28f7483fe7bb0b`) contains
**NO** `build-turnip` / `build-adreno-metal` / `meson` / `xcrun --sdk
iphoneos` / `codesign` / `hdiutil` / `make-gpu-addon` / `add-gpu-plugin` step.
The Metal frontend (Turnip dylib, Rust `metal2vulkan` bridge, ObjC++ plugin,
APFS Data seed) is macOS/Xcode-only and **cannot be built by this CI** or on
this Windows host. So round 67's honest "compile" target is: the pure-C policy
module joins `libEnchiladaAdreno.a`, is host-tested, and is CI link-verified.

## DOC-CONSTANT CORRECTION (carried honestly, not copied as fact)

`docs/drivers/adreno740.md:41,45` say listed clients get "256 MiB" windows.
That number is the **per-object cap** `NativeAdrenoAbiMaxBoBytesShift = 28`
(`NativeAdrenoABI.h:16`, "256MiB per object"), NOT the actual client window.
`NativeAdrenoPublish.cpp:113-114` shows slots 1-3 are `0x0AAA9000` =
178,905,088 B ≈ **170.6 MiB**. Slot 0 (render server) is `0x40000000` = 1 GiB
at `0x20000000`. Both values are exposed under unambiguous, distinct names; the
sheng windows are **provenance-only** and can never be returned as A630 values.

## Deliverables

| File | Role |
| --- | --- |
| `drivers/NativeAdreno/AdrenoMetalPublishV1.h` | 7 sha256 pins; 14 publish constants; 9 stages; 6 blockers; sheng reference windows marked provenance-only; 4 C++ + 4 C11 layout asserts; 19 APIs |
| `drivers/NativeAdreno/AdrenoMetalPublishV1.c` | freestanding C11, no libc/MMIO/IOKit; open-type routing; bounded allow-list (`" ,\\t\\r\\n"` + `*` / exact / trailing-`*`); connection cap 8; A630 **window-zero honesty gate**; 9-stage blocker map (OR `0x3f`); 7-way frontend gate |
| `drivers/NativeAdreno/tests/adreno-metal-publish-v1-test.cpp` | 7 test groups, `CHECK()` accumulates failures |
| `scripts/verify-enchilada-metal-publish.py` | method-2 cross-verifier, **164 checks** (Domain A = 7 pinned sources; Domain B = deliverable) |
| `scripts/build-enchilada-adreno.py` | 22nd archive unit + build-report source |
| `scripts/check-enchilada-adreno-host.py` | 22nd host suite |

## The window honesty gate (the load-bearing finding)

```c
void enchilada_publish_windows_a630(struct enchilada_publish_window out[4],
                                    uint32_t *specified_mask);
```

For A630 all four `{start, bytes}` slots are **ZERO** and the specified mask is
**ZERO** (`kEnchiladaPublishA630WindowsSpecifiedMask = 0u`). Any nonzero
window, any nonzero mask, or a NULL array is refused by
`enchilada_publish_windows_consistent()`. The four sheng windows are reachable
only through the explicitly named `enchilada_publish_sheng_reference(slot, ...)`
provenance accessor. This is the direct implementation of "never from a guess":
until an A630 board VA map and a validated kgsl SMMU stage-1 context exist, no
GPU address may be emitted.

## Establishment chain (9 stages, 6 blockers, OR = 0x3f today)

| Stage | Blockers today |
| --- | --- |
| seed markers | macOS toolchain (`hdiutil`/`codesign`) |
| discovery | macOS toolchain + OS Metal/IOSurface ABI |
| publish gate | macOS toolchain + OS ABI |
| render-server device | GEN1 backend + OS ABI + macOS toolchain |
| **client allow-list** | **none — pure portable policy, implemented here** |
| client device | GEN1 backend + OS ABI + macOS toolchain |
| kernel user client | GEN1 backend + signed ZAP + SMMU context + OS ABI |
| GPU window scope | GEN1 backend + SMMU context + **board VA map** |
| scanout composite | GEN1 backend + OS ABI + macOS toolchain |

`enchilada_publish_frontend_allowed(gen1, compiler, zap, smmu, osabi, macos,
board_va_map)` requires all SEVEN prerequisites. enchilada today has only
`compiler = 1` (pinned Mesa `85fda3b36405bcaea8bffc64aa4f2651dc90770d` already
carries `GPUId(630)` / `CHIP.A6XX` / `a6xx_gen1`, proved in round 66), so the
gate returns **0**.

## Provenance (7 sha256 pins, re-checked by the cross-verifier)

| Pin | File | Bytes | sha256 |
| --- | --- | --- | --- |
| P1 | `work/UniXNU/drivers/Adreno740/NativeAdrenoPublish.cpp` | 48729 | `013e43c09db8e5d3e444a3a0f1f56d14e381df47097689478811b20a162f5183` |
| P2 | `work/UniXNU/drivers/AdrenoMetal/NativeAdrenoMetalPlugin.mm` | 26233 | `5105c1627020da7d404c7f7454f1d4887c09c3273fc54991c47f5d64e68e3454` |
| P3 | `work/UniXNU/docs/drivers/adreno740.md` | 7290 | `d7dec513732a66dbc0ffdd1ee707b15b71c4604664c0c09eb44fc658596f9bd3` |
| P4 | `work/UniXNU/scripts/add-gpu-plugin-data.py` | 6777 | `51393cf9ec91faf885cca08658076654ad4f8409dacaa43c2b90c403292f8b49` |
| P5 | `work/UniXNU/drivers/Adreno740/NativeAdrenoABI.h` | 7733 | `20ac9298bb390d86788c1fc58155e1f9581ffa942e222752efc95c67cb5670d2` |
| P6 | `work/UniXNU/docs/porting/gpu.md` | 7861 | `521486857e22068cc8a87a604ab075c33d0b21d61476495ebfc0f19e5d4da7f4` |
| P7 | `work/tmp/kernel.yml` | 14554 | `6629ee5180434b7ba8d638f50542ecd83b1f373240a0efcf4c28f7483fe7bb0b` |

## Three independent verifications (all PASS)

1. **Strict freestanding compile**: `clang -std=c11 -ffreestanding -fno-builtin
   -O1 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow
   -pedantic -target aarch64-apple-darwin` → arm64 Mach-O object, 4488 B,
   magic `cf fa ed fe`; native COFF rebuild + C++17 host link/run →
   `PASS: enchilada A630 Metal publish/discovery policy contract (7 test
   groups, 0 failures)`. (One defect caught and fixed: the header needed
   `extern "C"` guards for the C++ host link.)
2. **Cross-verifier**: `METAL_PUBLISH_CROSS: PASS (164 checks, 0 failures)` —
   Domain A re-derives constants / allow-list / docs / CI-capability from the 7
   sha256-pinned sources; Domain B asserts the deliverable mirrors them, keeps
   the sheng windows provenance-only, and enforces the window-zero gate.
3. **Full host suite + record checker**: 22 suites PASS (tail `.../
   metal-seam-v1/darwin-kmd-abi-v1/metal-publish-v1 host tests (without
   sanitizers)`), and `enchilada: 15 structured areas checked`.

## Honest boundary / remaining hard blockers

NOT a GPU driver. No MMIO, no BO / IOVA / queue / fence / shader cache /
MTLDevice / user client, no Turnip dylib, no Data seed. xcrun / codesign /
hdiutil need macOS. Four hard blockers from earlier rounds remain open:
(1) A6XX_GEN1 kernel backend instantiation (legacy GMU HFI v1 / SPTPRAC /
CP-PM4); (2) signed ZAP / TrustZone PAS (`GPU_PAS_ID 13`); (3) kgsl SMMU
stage-1 context (`arm,smmu-kgsl@5040000`, single-cell SIDs 0/2,1/4/5, must
first be made to fault); (4) macOS frontend + selected OS Metal/IOSurface ABI.
The `gpu` area **STAYS `"host-verified"`**; no device run, no flash.

Chain previous head: `48327a35465b2ce18200d2f17fa083864cd891ab`
(round 66 / CI #233 phase 5).
