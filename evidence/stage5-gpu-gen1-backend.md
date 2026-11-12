# Stage 5 GPU — A6XX_GEN1 BACKEND ADMISSION contract (ROUND 69)

**Status: HOST-VERIFIED. ADJUDICATION ONLY.** This is NOT a GPU driver. It is a
freestanding C module that states, in one tested place, the twelve prerequisites
an A6XX_GEN1 (Adreno A630 / SDM845 / enchilada) kernel backend must satisfy,
cites the pinned source line for each, and records which are modelled in this
tree today versus satisfied by anything that can run on the phone.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area of `porting.toml`
**STAYS `"host-verified"`**. No device run was performed, no flash was
performed, the standing flash authorization (`m39510`) was NOT exercised, no
MMIO was issued, and the A630 client GPU windows stay ZERO. It does **not**
unblock stage 5.

## Why this round, and why it is not a thirteenth model

Rounds 20-68 produced twenty-two derived contracts, all host-verified. Round
68's `next_action` therefore reads the same way it has since round 42:

> (1) the A6XX_GEN1 kernel backend that INSTANTIATES the derived plans behind
> full CX/GX genpd + 6 clocks + RPMh/PDC and TrustZone PAS auth for
> `GPU_PAS_ID 13`; (2) the real kgsl SMMU stage-1 context at 0x5040000 only
> AFTER a powered CX; (3) the board-derived GPU VA map; (4) the OS
> Metal/IOSurface ABI and the macOS frontend build; (5) device verification.

Nothing in the tree said how far along (1) actually is. A reader of the
round-42..67 evidence can reasonably conclude "the plans exist, so the backend
is nearly done". It is not. This round makes the gap machine-checkable:

| | modelled in this tree | satisfied by a device-capable backend |
| --- | --- | --- |
| count | **5 of 12** | **0 of 12** |
| verdict | — | **BLOCKED** |

`modeled != satisfied`, and the module refuses to conflate them: they are two
independent columns, set independently, and a future round can move one without
silently moving the other.

## What this round ADDS (it closes a real gap in the argument, not the constant)

Round 68's next_action names "TrustZone PAS auth for `GPU_PAS_ID 13`". Earlier
rounds could not cite a line for `13` from the `work/UniXNU` (A740/sheng)
checkout — and correctly so. That checkout contains **no PAS concept in code at
all**: `drivers/Adreno740` has zero occurrences of `pas` / `PAS` / `trustzone` /
`zap` in code, and the value `13` appears there only as a PWRUP struct byte
offset (`NativeAdrenoPwrup.cpp:41`).

The constant itself is already asserted locally in three headers —
`AdrenoBoardInputs.h:90`, `AdrenoFirmwareV1.h:81`, `AdrenoMetalSeamV1.h:240` —
and is derivable from two independent pins:

| pin | line | text |
| --- | --- | --- |
| Linux v6.12 `a6xx_gpu.c` | `:17` | `#define GPU_PAS_ID 13` |
| Linux v6.12 `a6xx_gpu.c` | `:819` | `adreno_zap_shader_load(gpu, GPU_PAS_ID)` |
| `enchilada.dts` | `:23612` | `qcom,pas-id = <0x0d>;` (`0x0d == 13`) |
| `enchilada.dts` | `:23613` | `qcom,firmware-name = "a630_zap";` |

What no earlier round did is draw the consequence, and that is what is
load-bearing: knowing the constant is **not** the same as having the step. An
A630 backend **cannot inherit** sheng's PAS handling, because sheng's tree has
none to inherit; PAS must be implemented fresh against `qcom_scm`. That is why
prerequisite `ZAP_AUTH` is recorded as `modeled = 0` *and* `satisfied = 0` —
not "partly done", but absent. The cross-verifier now also asserts that the
five local copies of the constant agree with each other and with both pins, so
the value cannot silently drift in one header.

## The twelve prerequisites, adjudicated

| # | id | modelled | satisfied | pinned anchor |
| --- | --- | --- | --- | --- |
| 0 | `power-path` | 1 | 0 | `a6xx_gmu.c:1033-1052`; `gpucc-sdm845.c:119-139` |
| 1 | `gpucc-windows` | 1 | 0 | `enchilada.dts:23632-23657` (6 clk), `:23889-23890` (4 clk) |
| 2 | `gmu-cold-boot` | 1 | 0 | `a6xx_gmu.c:1066-1068` legacy cold/warm |
| 3 | `hfi-v1-transport` | 1 | 0 | `a6xx_hfi.c:59`, `:90-92`; `a6xx_gmu.c:1736-1738` |
| 4 | `firmware-blobs` | 1 | 0 | `a6xx_catalog.c:889-892`, `:897` |
| 5 | `zap-auth` | **0** | 0 | `a6xx_gpu.c:17`+`:819`; `enchilada.dts:23612-23613` |
| 6 | `kgsl-smmu-stage1` | **0** | 0 | `enchilada.dts:23848-23872` (kgsl-smmu-v2 @ 0x5040000) |
| 7 | `cp-ring` | **0** | 0 | `a6xx.xml:2281-2285` |
| 8 | `board-va-map` | **0** | 0 | `NativeAdrenoABI.h:127-128` "never from a guess" |
| 9 | `device-proof` | **0** | 0 | round-35 CX=0x00005ff4 BIT(31) clear; round-67 zero gate |
| 10 | `config-on` | **0** | 0 | `CONFIG_XNU_NATIVE_ADRENO` is not set |
| 11 | `macos-frontend` | **0** | 0 | `docs/porting/gpu.md` §1 backend-before-frontend |

Rounds 51/56/57/58/59/61/62/63/64 supply `power-path` and `gpucc-windows`;
round 42 supplies `gmu-cold-boot`; round 31 supplies `hfi-v1-transport`; round
38 supplies `firmware-blobs` (file presence on the device, proven read-only —
not a load). Round 53's kgsl-SMMU work is a **fail-closed READ-safety gate**, not
a stage-1 context, so it is recorded as `modeled = 0` for that prerequisite
rather than being counted as progress.

## Deliverables

| File | Role |
| --- | --- |
| `drivers/NativeAdreno/AdrenoGen1BackendV1.h` | 8 sha256 pins; A630 catalog pins; PAS/ZAP names; SPTPRAC values; resume rates; CP ring offsets; the 4 GMU regions; 5-step resume enum; 12-prerequisite enum; 16 APIs |
| `drivers/NativeAdreno/AdrenoGen1BackendV1.c` | freestanding C11, no libc/MMIO/IOKit; ordered resume plan; region builder that REFUSES the a660/a7xx SZ_8K widening; SPTPRAC poll contract; PAS origin string; the adjudication |
| `drivers/NativeAdreno/tests/adreno-gen1-backend-v1-test.cpp` | 9 test groups, `CHECK()` accumulates failures |
| `scripts/verify-enchilada-gen1-backend.py` | method-2 cross-verifier, **230 checks** (Domain A = 8 pinned sources; Domain B = deliverable) |
| `scripts/build-enchilada-adreno.py` | 23rd archive unit |
| `scripts/check-enchilada-adreno-host.py` | 23rd host suite |

## Two generation traps this module makes explicit (both were latent)

1. **The GMU dummy page stays `SZ_4K`.** `a6xx_gmu.c:1696` sets
   `gmu->dummy.size = SZ_4K`; the widening to `SZ_8K` at `:1704` sits INSIDE the
   `adreno_is_a660_family() || adreno_is_a7xx()` branch at `:1697-1698`. A630
   takes the `:1736` branch, so applying the widened value would be a
   silent cross-generation leak. The module emits `0x1000` and the test asserts
   `bytes != 0x2000`.
2. **There is no icache and no dcache region.** `a6xx_gmu.c:1721-1725` carries a
   porter's NOTE: legacy ("pre-650-family") GPUs should NOT be given
   icache/dcache allocations. A630 allocates debug (`:1741`), log (`:1747`) and
   hfi (`:1752`) — all `SZ_16K` at IOVA 0 — plus the dummy page. Total
   `0xd000`. No region is executable, so nothing here is XN-cleared.

A third, quieter one: `a6xx_has_gbif()` (`a6xx_gpu.h:71-77`) returns **false**
for A630 and true for every other generation, so the A630 backend must skip the
GBIF halt path entirely.

## Three independent verifications (all PASS)

1. **Host suite #23**: `PASS: enchilada A6XX_GEN1 backend admission contract
   (9 test groups, 0 failures)`, and the full runner reports 23 suites:
   `... metal-publish-v1/gen1-backend-v1 host tests (without sanitizers)`.
2. **Cross-verifier (method 2, never compiles the module)**:
   `GEN1_BACKEND_CROSS: PASS (230 checks, 0 failures)` — Domain A re-derives
   PAS 13, the catalog entry, the legacy-branch markers, SPTPRAC values/poll,
   the five resume steps and their two rates, the four GMU regions (and the
   excluded widening), the CP ring word offsets, both GDSC descriptors, and
   **both DTB clock counts parsed out of the node text** (4 GMU / 6 GPU, and
   they must differ); Domain B asserts the deliverable mirrors all of it, keeps
   the adjudication honest, and checks the 23rd-unit wiring.
3. **Strict freestanding compile** `work/tmp/_r69_strict.py`: `clang -std=c11
   -ffreestanding -fno-builtin -O1 -Wall -Wextra -Werror -Wconversion
   -Wsign-conversion -Wshadow -pedantic -target aarch64-apple-darwin` → arm64
   Mach-O object **5168 B**, magic `cf fa ed fe`; native COFF **4782 B**; the
   C++17 host link/run prints the PASS line. `R69_STRICT: PASS`.

Plus the record checker: `enchilada: 15 structured areas checked`.

## Honest boundary

ADJUDICATION ONLY. This module asserts what the pinned sources say and what
this tree currently holds; it does not make anything true, and a green CI build
of it would not change a single row of the table. NOT a GPU driver: no MMIO, no
BO / IOVA / queue / fence / shader cache / MTLDevice / user client, no firmware
load, no PAS authentication, no SMMU programming, no Turnip dylib (xcrun /
codesign / hdiutil need macOS). `CONFIG_XNU_NATIVE_ADRENO` stays `n`. The four
hard blockers of `docs/porting/gpu.md` §1 remain open. The `gpu` area
**STAYS `"host-verified"`**; no device run, no flash, `m39510` NOT exercised.

Chain: round 68 closed at `8dc5c8bcf9e916f8bace970121ea61ffecf55f92`
(CI #234 record).
