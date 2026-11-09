# Stage 5 GPU — A630 board inputs (dedicated kgsl SMMU), HOST-VERIFIED

**Status: HOST-VERIFIED only.** A device tree is a *firmware description*, not a
device measurement. This record is derived from a locally SHA-pinned vendored
DTB, cross-checked against device-verified C constants already in this repo and
against SHA-pinned upstream Linux sources. **No CI compile, no flash, no device
register read, no firmware load, no GMU/HFI start, no command submission, no
rendered image.** It does not make stage 5 `device-verified`.

Round 21 / 2026-11-09. Goal: continue the strict unixnu 8-stage driver order on
OnePlus 6 (enchilada, SDM845), GPU mainline. This round closes a *wrong* claim
made in Round 20, following the hard constraint “先核验再上机 / 每做一步之前进行
两次验证”: verify first, only then touch hardware.

---

## What this corrects (load-bearing)

Round 20 (`evidence/stage5-gpu-a630-registers.md`, honest correction #7) and the
`porting.toml` gpu area recorded:

> “GPU SMMU SID/mask is UNVERIFIED … the board DTS could NOT be fetched
> (Mu-Silicium/Resources 404); do not assert a stream id.”

That was **wrong**: the 404 was a *fetch failure by one research subagent*, not
an absent file. `Mu-Silicium/Resources/DTBs/enchilada.dts` exists locally in
**4 byte-identical copies** (see Provenance). It is the **same DTB** behind the
already **device-verified** stage-3 sofef00 `bl_ctrl_dcs` backlight node (`:2661`),
the stage-4 Synaptics touch facts and the stage-7 bq27441 gauge facts, and the
same DTB already cited by `drivers/NativeAdreno/AdrenoCatalog.h:25-28`. The GPU
board inputs were therefore already available and trustworthy at board level;
the “UNVERIFIED / do not assert a stream id” wording must be retracted.

**Correction:** the GPU does **NOT** use the `apps_smmu @ 0x15000000` that
flash #8 surveyed. It has a **dedicated kgsl SMMU**:

| | dedicated kgsl SMMU (GPU) | apps_smmu (flash #8 survey) |
|---|---|---|
| node | `arm,smmu-kgsl@5040000` | `apps-smmu@0x15000000` |
| `compatible` | `qcom,smmu-v2` | `qcom,qsmmu-v500` |
| `reg` | `<0x5040000 0x10000>` | `<0x15000000 0x80000 0x150c2000 0x20>` |
| `#iommu-cells` | **`<0x01>` — SID only, NO mask** | `<0x02>` — SID + mask |
| `phandle` | **`0x1ac`** | `0x29` |

Because `#iommu-cells=1`, every GPU `iommus` tuple is `(phandle, SID)` — there
is **no mask cell**. Context-bank SIDs extracted from `qcom,kgsl-iommu` and
`qcom,gmu` children:

| context bank | `iommus` | SID(s) |
|---|---|---|
| `gfx3d_user` | `<0x1ac 0x00>` (+ `qcom,gpu-offset=<0x48000>`) | `0x00` |
| `gfx3d_secure` | `<0x1ac 0x02 0x1ac 0x01>` | `0x02`, `0x01` |
| `gmu_user` | `<0x1ac 0x04>` | `0x04` |
| `gmu_kernel` | `<0x1ac 0x05>` | `0x05` |

**ALL** GPU `iommus` tuples use phandle `0x1ac`; **NONE** use `0x29`. (Contrast
`ssusb@a600000` at `:23931`: `iommus = <0x29 0x740 0x00>` + `qcom,smmu-s1-bypass`
— USB genuinely uses apps_smmu with 2 cells.)

**Consequence:** “a stream id / mask must be sourced before any GPU MMIO” is now
**resolved at the board-description level**. A stage-1 SMMU context can be
designed against the *dedicated* kgsl SMMU `0x5040000` (not apps_smmu), with
single-cell SIDs `0/2,1/4/5`. It is **still not device-verified**: no register
was read on the device, and a real SMMU context has not been programmed (flash
#8's `pre=10`/`ArmSmmu4KBusy` gate applies to apps_smmu streams, not this unit).

---

## Provenance (independently pinned)

- File: `Mu-Silicium/Resources/DTBs/enchilada.dts`, decompiled `dtc` text,
  **784857 bytes**, full SHA-256
  `e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275`.
- 4 byte-identical local copies (`oneplus6-ios/` relative):
  `notes/mu-silicium/enchilada.dts`,
  `work/Mu-Silicium-full/Resources/DTBs/enchilada.dts`,
  `work/Mu-Silicium/Resources/DTBs/enchilada.dts`,
  `work/mock-mu-silicium/Resources/DTBs/enchilada.dts`.
- Node line numbers (1-based, identical across all 4 copies):
  `arm,smmu-kgsl@5040000` `:13957`, `apps-smmu@0x15000000` `:13975`,
  `qcom,kgsl-hyp` `:23610`, `qcom,kgsl-3d0@5000000` `:23632`,
  `qcom,kgsl-iommu` `:23848`, `gfx3d_user` `:23860`, `gfx3d_secure` `:23868`,
  `qcom,gmu` `:23875`, `gmu_user` `:23914`, `gmu_kernel` `:23920`.
  Key lines: `qcom,chipid=<0x6030001>` `:23641`, `qcom,pas-id=<0x0d>` `:23612`,
  `qcom,firmware-name="a630_zap"` `:23613`, `phandle=<0x1ac>` `:13972`,
  `#iommu-cells=<0x01>` `:13961`, `bl_ctrl_dcs` `:2661`.
- Provenance anchor: the **same** DTB carries the device-verified stage-3
  `qcom,mdss-dsi-bl-pmic-control-type = "bl_ctrl_dcs"` at `:2661`.

SHA pin + all node slices + extracted facts live in
`scripts/data/enchilada-gpu-board.json` (schema_version 1).

---

## Board facts extracted (verbatim)

- `qcom,kgsl-3d0@5000000` (`:23632`):
  `reg = <0x5000000 0x40000 0x5061000 0x800 0x509e000 0x1000>`,
  `interrupts = <0x00 0x12c 0x00>` (GIC SPI **300**),
  `qcom,chipid = <0x6030001>`, `qcom,highest-bank-bit = <0x0f>` (15),
  `qcom,ubwc-mode = <0x02>`, 6 clocks
  (`core_clk/rbbmtimer_clk/mem_clk/mem_iface_clk/gmu_clk/l3_vote`).
- `qcom,gmu` (`:23875`): `reg = <0x506a000 0x30000 0xb200000 0x300000>`,
  `interrupts = <0x00 0x130 0x00 0x00 0x131 0x00>` (HFI **304**, GMU **305**).
- `qcom,kgsl-hyp` (`:23610`): `qcom,pas-id = <0x0d>` (**GPU_PAS_ID 13**),
  `qcom,firmware-name = "a630_zap"`.
- `qcom,kgsl-iommu` (`:23848`): `reg = <0x5040000 0x10000>`,
  `qcom,protect = <0x40000 0xc000>`, `qcom,micro-mmu-control = <0x6000>`,
  `qcom,secure_align_mask = <0xfff>`.

---

## Vendor-vs-mainline distinction (broad lookup, honest)

The DTB is a **downstream KGSL/vendor** board description. Mainline Linux v6.12
does **not** name these kgsl context banks: `a6xx_gmu.c:1277` uses
`mmu = msm_iommu_new(gmu->dev, 0);` and `:1283
gmu->aspace = msm_gem_address_space_create(mmu, "gmu", 0x0, 0x80000000);` — a
generic IOMMU handle. Broad lookups this round:
- `https://cdn.jsdelivr.net/gh/torvalds/linux@v6.12/arch/arm64/boot/dts/qcom/sdm845.dtsi`
  → `Error: unsupported content type "application/octet-stream"` (also with a
  trailing `?`); no local `sdm845.dtsi` exists in the workspace.
- `Documentation/devicetree/bindings/display/msm/qcom,adreno.yaml` / `.txt`
  → HTTP 404 `Couldn't find the requested file`.

Therefore the vendor DTB SIDs are the authoritative **BOARD** facts for this
port (they describe *this* firmware's SMMU wiring), not mainline driver
constants. Do not conflate the two.

---

## The deliverable (all pure host, NOT linked into the kernel)

- `drivers/NativeAdreno/AdrenoBoardInputs.h` + `.c` — freestanding pure-C host
  module: 26 `#define ADRENO_BOARD_*` (dedicated kgsl SMMU base/size/phandle/
  cells, apps_smmu, GPU/GMU register windows, IRQs, chipid, HBB/UBWC, ZAP
  PAS_ID/fw, protect/micro-mmu/secure-align), `enum adreno_board_context`, and
  4 read-only helpers (`adreno_board_is_kgsl_smmu`, `adreno_board_sid_is_unmasked`,
  `adreno_board_context_sid`, `adreno_board_smmu_context_ready`). It maps nothing,
  writes no register, loads no firmware.
- `drivers/NativeAdreno/tests/adreno-board-inputs-test.cpp` — 5 C++17 host test
  groups (dedicated SMMU, phandle gate, context SIDs, board identity, ready gate).
- `scripts/check-enchilada-adreno-host.py` — extended with the 3rd test + source.
- `scripts/data/enchilada-gpu-board.json` — SHA-pinned DTB + node slices + facts.
- `scripts/extract-enchilada-gpu-board.py` — **Verification method 1** (extract).
- `scripts/verify-enchilada-gpu-board.py` — **Verification method 2** (cross-domain).

---

## Two independent verifications (both PASS)

**Verification 1 — extraction from the pinned DTB**
(`python -X utf8 scripts/extract-enchilada-gpu-board.py`): 4 copies, 1 distinct
full SHA-256 and 1 distinct GPU-slice hash; full SHA matches the manifest pin;
regex parse of phandle / `#iommu-cells` / context-bank SIDs / reg / chipid /
pas-id / firmware; every GPU iommus uses `0x1ac`, none `0x29`; provenance
`bl_ctrl_dcs` at `:2661`. → `ENCHILADA_GPU_BOARD_EXTRACT: PASS (18 checks)`.

**Verification 2 — cross-domain (independent domains, independent parser)**
(`python -X utf8 scripts/verify-enchilada-gpu-board.py`): re-pins the full DTB
SHA, then cross-checks the extracted constants against
(a) **device-verified** C constants in this repo — `AdrenoCatalog.h:62`
`ADRENO_KGSL3D0_BASE 0x05000000u`, `:65 ADRENO_A630_DTB_CHIPID 0x06030001u`,
`SmmuSurvey.h:40 SMMU_SURVEY_SDM845_BASE 0x15000000ull`,
`Dwc3Registers.h:35 kAppsSmmuBase = 0x15000000ull`;
(b) the **SHA-pinned Linux v6.12** `a6xx_catalog.c` (chipids `0x06030001/2`,
`a630_gmu.bin`, `a630_sqe.fw`, `ADRENO_6XX_GEN1`, sha
`6d9061ee…c24c`, fetched by the upstream pin-before-derive recipe);
(c) the **new** `AdrenoBoardInputs.h` — all 26 `#define`s asserted equal to the
DTB-derived values (guards against transcription drift).
→ `ENCHILADA_GPU_BOARD_CROSS: PASS (21 checks, three independent domains agree)`.

**Verification 1b — extended host runner**
(`python -X utf8 scripts/check-enchilada-adreno-host.py`): the original 10
catalog groups + 3 A630 hwinit groups + 5 new board-input groups all PASS
(`PASS: enchilada A630 catalog/hwinit/board-input host tests`).

**Verification 2b — strict compile of a real consumer** (round-20 lesson: a
header with `static const` tables alone triggers `-Wunused-const-variable`; a
consumer that calls every function and references every macro is required):
`clang -std=c11` and `clang++ -std=c++17`, each with `-Wall -Wextra -Werror
-Wconversion -Wsign-conversion -Wshadow -pedantic -O2`, against
`AdrenoBoardInputs.c` + `board_strict_probe.c` → exit 0, zero warnings bytes.

Both verifiers are independent of each other (method 1 re-parses the DTB with
regex; method 2 slices by pinned line numbers and never re-parses the same way),
and both agree with the device-verified C constants and the pinned upstream
source. Per the hard constraint “每做一步之前进行两次验证”, no CI/flash was
attempted on the strength of a single check.

---

## Honest corrections to the Round-20 record

1. **The 404 was a fetch failure, not an absent DTB.** The vendored DTB is
   local in 4 byte-identical copies and is already the provenance of 3
   device-verified stages. “GPU SMMU SID/mask UNVERIFIED / do not assert a
   stream id” is **retracted**.
2. **The GPU uses a dedicated kgsl SMMU, not apps_smmu.** `0x5040000`,
   `#iommu-cells=1`, phandle `0x1ac`; flash #8 surveyed `0x15000000`
   (`#iommu-cells=2`, phandle `0x29`) — the wrong unit for a GPU context.
3. **There is no mask cell.** Single-cell SIDs are `0/2,1/4/5`; the Round-20
   “SID/mask” phrasing itself was imprecise for this unit.
4. **Vendor ≠ mainline.** Mainline `a6xx_gmu.c` uses a generic
   `msm_iommu_new(..., 0)` handle; the named context banks are a downstream
   KGSL/vendor DTB construct. The board facts are authoritative *for this
   firmware*, not for upstream Linux.
5. **Still HOST-VERIFIED, not device-verified.** This closes a *documentation*
   gap, not a hardware gap: no register was read, no stage-1 SMMU context was
   programmed, no GPU firmware was loaded, no command was submitted. The
   remaining stage-5 blockers (signed ZAP `.mdt/.b00/.b01` MDT chain + TrustZone
   PAS auth `GPU_PAS_ID 13`; a NEW A6XX_GEN1 kernel backend with legacy GMU HFI
   v1 / SPTPRAC / RPMh-PDC cold start; A7XX-only CP/IRQ/ring layers) are
   **unchanged**.

---

## Does NOT establish

- Any device measurement, register read, MMIO write, SMMU context programming,
  firmware load, GMU/HFI start, command submission or rendered image.
- That `0x5040000` is physically accessible / unclaimed by firmware on this
  boot (a DTB is a description; needs a device read to confirm).
- That programming SIDs `0/2,1/4/5` is sufficient or safe (no device run).
- A solution to the signed ZAP MDT chain or the A6XX_GEN1 backend/compiler gap.
- Reuse of sheng's 6 A740/GEN2 GPU archives.

---

## Reproduce

```
python -X utf8 scripts/extract-enchilada-gpu-board.py
python -X utf8 scripts/verify-enchilada-gpu-board.py
python -X utf8 scripts/check-enchilada-adreno-host.py \
    --cc oneplus6-ios/tools/llvm-mingw/bin/clang.exe \
    --cxx oneplus6-ios/tools/llvm-mingw/bin/clang++.exe \
    --out work/tmp/adreno-host-out
# (upstream pin-before-derive cache, if not yet present)
python -X utf8 scripts/fetch-adreno-a630-sources.py
```

**Strict 8-stage order then continues** with an actual A6XX_GEN1 backend and the
remaining areas; each new hardware claim still requires CI compile + two
independent real-device verifications.
