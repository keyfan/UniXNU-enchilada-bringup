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
- `AdrenoHfiV1.h` / `AdrenoHfiV1.c` — legacy GMU HFI v1 transport (2 queues,
  no 4-dword alignment, no 0xfafafafa padding, v1 startup table).
- `AdrenoFirmwareV1.h` / `AdrenoFirmwareV1.c` — legacy flat ITCM ≤0x8000
  firmware-load contract (no block_header parsing).
- `AdrenoGmuBootV1.h` / `AdrenoGmuBootV1.c` — A630 LEGACY GMU cold-boot
  bring-up SEQUENCE model (round 42): ordered op stream (rpmh_start →
  GENERAL_7=1 → flat ITCM bulk → CM3 config/fence/chipid → power_config →
  gmu_start → gfx_rail_on → [sptprac] → hfi_start) + RSC/PDC sequencer init
  plan, all routed through caller-supplied bus callbacks; every offset
  sha-pinned (a6xx_gmu.xml + 3-tree PDC sources).
- `tests/adreno-gmu-boot-v1-test.cpp` — 7 test groups: pure predicates,
  refusals, exact 33-op SPTP cold stream, sptprac insertion at idle<SPTP,
  RSC/PDC TCS1/TCS3 plan, synthetic bus execution, bus refusal contract.
- `scripts/check-enchilada-adreno-host.py` — C11 + C++17 host runner, mirrors
  `scripts/check-enchilada-dsi-backlight-host.py` (sanitizers when available).
- `scripts/verify-enchilada-gmu-boot-v1.py` — cross verifier: pinned
  a6xx_gmu.c/a6xx_gmu.xml slices + pinned PDC headers vs module defines +
  plan order + no-modern-construct checks (213 checks).
- `AdrenoGmuBusV1.h` / `AdrenoGmuBusV1.c` — A6XX_GEN1 bus adapter (round 44):
  translates the domain/word-offset op model to real MMIO windows
  (word-offset addressing `offset << 2`, RSCC window derived at
  `gmu_mmio + 0x23000`, per-domain byte guards, fail-closed refusals);
  NO mapping/clock/regulator/power action — caller wires windows only.
- `tests/adreno-gmu-bus-v1-test.cpp` — 7 test groups: Make validation,
  resolve bounds, RSCC slice, read/write roundtrip + NULL-PDC refusal,
  bulk refusals, real PlanRpmhInit end-to-end vs simulated windows,
  bounds-violation aborts Execute.
- `scripts/verify-enchilada-gmu-bus-v1.py` — cross verifier: pinned
  a6xx_gmu.c io-contract slices (:1771 RSCC delta, :642-643 setup_pdc,
  :590-605 ucode) + pinned PDC headers vs adapter translation checks
  (40 checks).
- `AdrenoGen1BackendV1.h` / `AdrenoGen1BackendV1.c` — A6XX_GEN1 BACKEND
  ADMISSION contract (round 69): adjudicates the TWELVE prerequisites an A630
  kernel backend needs, with two independent columns (`modeled` in this tree vs
  `satisfied` by a device-capable backend) and a tri-state verdict. Since round
  74: modeled=7, satisfied=0, verdict BLOCKED (cp-ring and zap-auth modelled; MDT
  parser added under zap-auth's S6/S7). Also carries the A630 catalog pins,
  the four GMU memory regions (and REFUSES the a660/a7xx SZ_8K dummy widening),
  the five-step GMU resume plan with both generation-dependent rates, the
  SPTPRAC value/poll contract, the CP ring word offsets, and `GPU_PAS_ID 13`
  with its two pins — recording that the value is NOT inherited from the
  A740/sheng tree, which has no PAS concept in code.
- `tests/adreno-gen1-backend-v1-test.cpp` — 9 test groups: catalog pins, GBIF
  absence, resume plan order/rates, GMU regions, SPTPRAC, PAS/ZAP names, CP
  ring offsets, the 12-way adjudication, self-check.
- `scripts/verify-enchilada-gen1-backend.py` — cross verifier: 8 sha256-pinned
  sources re-derive the PAS id, catalog entry, legacy markers, SPTPRAC, resume
  order, GMU regions, CP ring, both GDSCs and BOTH DTB clock counts; Domain B
  asserts the deliverable mirrors them and that all FIVE local copies of
  `GPU_PAS_ID` still agree (230 checks).
- `AdrenoCpRingV1.h` / `AdrenoCpRingV1.c` — A630 CP RING INITIALISATION and
  DOORBELL contract (round 70): the six-step ring bring-up order with its one
  CONDITIONAL step (the shadow-RPTR write happens only when a shadow BO exists),
  the exact eight-dword `CP_ME_INIT` payload, the barrier-before-`CP_RB_WPTR`
  doorbell rule, the WHERE_AM_I vs hardware shadow strategy, and the protect
  encoding. With equal care it records the FIVE constants no pinned source
  defines (`CP_ME_INIT`'s opcode, the type-7 header encoding,
  `MSM_GPU_RB_CNTL_DEFAULT`, `AXXX_CP_RB_CNTL_NO_UPDATE`, the ring size), so
  `adreno_cp_ring_is_executable()` is **0**: the order is known, a packet cannot
  be encoded. This is why the admission table moved one row (modeled 5 -> 6) and
  not the verdict.
- `tests/adreno-cp-ring-v1-test.cpp` — 7 test groups: word offsets, plan with and
  without a shadow BO, the payload builder + strict matcher (A7xx refused),
  shadow mode / doorbell order, the five undefined constants, self-check.
- `AdrenoMdtV1.h` / `AdrenoMdtV1.c` — XNU-side MDT/ELF32 PARSER (round 74): the four
  Qualcomm rules the ZAP handshake needs — `mdt_phdr_valid` (PT_LOAD + not HASH + non-zero
  memsz), `qcom_mdt_get_size` (min..ALIGN(max) over VALID phdrs only), the split `bNN`
  naming (last three chars replaced), and the metadata/split test
  (`ehdr_size + hash_size == file size`). Validated against the REAL 6684-byte
  `a630_zap.mdt` from this phone: only ONE of three segments is loadable, so `get_size`
  answers 4096 from a 6684-byte container while the `p_filesz` sum is 8652 — and the three
  `p_filesz` values (148/6536/1968) are exactly the device `.b00/.b01/.b02` sizes. PARSER
  ONLY: `adreno_mdt_loader_available()` is 0 and `ready()` is derived from it, so zap-auth's
  S6/S7 stay present=0 — their rules are modelled, their execution is not.
- `tests/adreno-mdt-v1-test.cpp` — 6 test groups; group 6 parses the real blob and SKIPs
  loudly if it is absent rather than passing silently.
- `scripts/verify-enchilada-mdt.py` — cross verifier: 2 sha256-pinned sources plus an
  INDEPENDENT Python reparse of the real blob confronted with the round-38 device
  inventory (113 checks).
- `AdrenoZapAuthV1.h` / `AdrenoZapAuthV1.c` — Qualcomm PAS / ZAP auth SHAPE contract
  (round 73): the exact SCM descriptor the handshake uses (`SVC_PIL 0x02`,
  `PAS_AUTH_AND_RESET 0x05`, one SIP-owned arg — and the finding that those numeric
  ids are NOT in the public `qcom_scm.h`, only in the internal one), the EIGHT-step
  prerequisite chain from `adreno_gpu.c:43-156` with a per-step `present`/`reachable`
  flag, and the result-semantics trap that makes a rejected image look like success
  (`return ret ?: res.result[0]` — transport 0 alone is NOT enough). Exactly ONE of
  the eight preconditions exists on enchilada today (`firmware-name`); `CONFIG_ARCH_QCOM`
  is not even reachable because XNU has no such Kconfig, so that step's real content
  is the SCM transport itself. `adreno_zap_auth_ready()` is derived from the count and
  is 0.
- `tests/adreno-zap-auth-v1-test.cpp` — 7 test groups: pasid, SCM shape, chain
  presence/reachability, step names, result semantics, the -EOPNOTSUPP quiet fallback,
  self-check.
- `scripts/verify-enchilada-zap-auth.py` — cross verifier: 4 sha256-pinned sources
  re-derive the call shape, the public-header absence and the eight anchors in strict
  source order (114 checks).
- `scripts/verify-enchilada-cp-ring.py` — cross verifier: 3 sha256-pinned sources
  re-derive the offsets, order, payload, barrier rule and protect encoding, and
  prove the five undefined macros by exhaustive scan (139 checks).

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
Round-38 correction to the honest boundary: the three vendor-signed A630
blobs ARE present on the device (`/vendor/firmware/`, read-only extraction,
`evidence/stage5-gpu-device-firmware.md`); the remaining blockers are the
TrustZone PAS auth, the kernel-side caller, and the kgsl SMMU context.

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
