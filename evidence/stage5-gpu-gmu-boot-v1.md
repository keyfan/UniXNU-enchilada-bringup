# Stage 5 GPU — A630 legacy GMU cold-boot sequence model (AdrenoGmuBootV1) — HOST-VERIFIED (Round 42, 2026-11-10)

## What this round delivered

The A630 legacy GMU cold-boot bring-up sequence is now encoded as a freestanding
pure-C module, the third named code gap in the stage-5 chain to close after
AdrenoHfiV1 (Round 31, legacy HFI v1 transport) and AdrenoFirmwareV1 (Round 33,
flat-ITCM firmware-load contract):

- `drivers/NativeAdreno/AdrenoGmuBootV1.h` — every GMU/RSCC/PDC word offset and
  A630 constant as `#define ADRENO_GMU_BOOT_V1_*`, plus the op/inputs/bus structs
  and the plan/execute API.
- `drivers/NativeAdreno/AdrenoGmuBootV1.c` — the ordered op stream:
  - `AdrenoGmuBootV1PlanColdSize/PlanCold`: 33 ops (SPTP idle level) or 35 ops
    (idle level below SPTP): rpmh_start (4) -> GENERAL_7=1 -> flat ITCM bulk
    (0x8000 cap) -> FW_INIT_RESULT/BOOT_CONFIG/QTBL_ADDR/QTBL_INFO/fence
    (BIT31|0xa<<18|0xa0)/CM3_CFG -> HFI_SFR_ADDR=chipid + CP_MSG -> power_config
    (6) -> gmu_start (4, DTCM-snapshot mask/reset branch) -> gfx_rail_on (6,
    OOB set22/poll30/clear30) -> sptprac (2, only when idle < SPTP) ->
    hfi_start (2).
  - `AdrenoGmuBootV1PlanRpmhInit`: 36 ops — RSC uCode
    {0xa7a506a0,0xa1e6a6e7,0xa2e081e1,0xe9a982e2,0x0020e8a8}, PDC uCode
    {0xfebea1e1,0xa5a4a3a2,0x8382a6e0,0xbce3e284,0x002081fc}, TCS1/TCS3 CMD
    values (A630 pdc_address_offset 0x30080, TCS3 CMD1 DATA 0x3), then
    SEQ_START_ADDR=0 and ENABLE_PDC=0x80000001.
  - `AdrenoGmuBootV1Execute`: walks the ops over caller-supplied bus callbacks
    in four domains (Gmu / Rscc / PdcSeq / Pdc); refusals are fail-closed.
- `drivers/NativeAdreno/tests/adreno-gmu-boot-v1-test.cpp` — 7 groups / 98 checks.
- `scripts/verify-enchilada-gmu-boot-v1.py` — independent cross verifier (213 checks).
- `scripts/data/adreno-a630-pdc-inputs.json` + `scripts/verify-adreno-pdc-offsets.py`
  — the PDC/RSCC offset pins (6 pinned sources, 3 independent kernel trees), with
  two independent verifications (`ENCHILADA_PDC_OFFSETS_VERIFY: PASS (200 checks)`,
  `ENCHILADA_PDC_SECOND_VERIFY: PASS (28 checks)`).

## Sources (sha-pinned, HOST-VERIFIED)

- Linux v6.12 `drivers/gpu/drm/msm/adreno/a6xx_gmu.c` (48981 B,
  sha256 c14fbb253e602bfd2585d757da871dde08ea63b811ff3c6808f657f4522f78d4) —
  exact slices: rpmh_start :480-505, gmu_start :204-240, hfi_start :242-255,
  OOB :265-344, sptprac :365-406, rail_on :409-424, power_config :663-706,
  fw_load :725-779, fw_start :781-902, cold/warm :1060-1095, rpmh_init :530-655.
- Linux v6.12 `drivers/gpu/drm/msm/registers/adreno/a6xx_gmu.xml` (11830 B,
  sha256 462b8960...) — 35 GMU word offsets; RSCC domain at gmu mmio + 0x23000
  (a6xx_gmu.c:1771), RSCC/PDC access dword-addressed (`offset << 2`).
- PDC offsets (ENABLE_PDC 0x1140, SEQ_START_ADDR 0x1148, TCS1 0x1572..0x1577,
  TCS3 0x15d6..0x15db) from the generated `a6xx.xml.h` — pinned from 3
  independent kernel trees (quard_star_tutorial linux-6.1.11
  ce1907c9d6aa215a5806ab894f69d4325ee783a4, projectacrn/acrn-kernel
  72c893382f890a38204f92d88e7bf4cbfffa4cb7, RMerl/asuswrt-merlin.ng linux-4.19
  0283215add51b5e1a07fcf3ca7188bf79272cd0f — the 4.19 tree is pre-rebase and
  presence-checked only). Also re-confirmed against the v6.12 checked-in
  `a6xx.xml:5994-6021` A6XX_PDC domain.

## Verifications (two independent, per the standing constraint)

1. Host runner: `scripts/check-enchilada-adreno-host.py` — all 8 suites PASS,
   including `PASS: enchilada A630 gmu-boot-v1 host tests (98 checks)` and the
   sanitizer pass.
2. Cross verifier: `scripts/verify-enchilada-gmu-boot-v1.py` ->
   `GMU_BOOT_V1_CROSS: PASS (213 checks, 0 failures)` — pinned-source integrity,
   exact upstream slices, 35 XML offsets + 15 PDC defines, 29 module constants +
   68 module offsets == upstream-derived, 16 monotonic order anchors, and no
   modern-only construct (A7xx/A650/block_header/0x80000021/gmu_chipid).
3. Strict compile: `clang -std=c11 -Wall -Wextra -Werror -pedantic
   -Wconversion -Wsign-conversion -Wshadow -O2 -c AdrenoGmuBootV1.c` EXIT=0.

## Honest boundary

This is a HOST-VERIFIED code MODEL: sha-pinned source reading, no device run,
no MMIO, no kernel caller, CONFIG_XNU_NATIVE_ADRENO=n unchanged, gpu area stays
host-verified. The Round 22/28 prohibitions on cold-reading 0x5000000 /
0x506a000 / 0x5040000 before the full CX/GX genpd + 6 clocks + RPMh/PDC +
GMU power-up sequence are unchanged. Remaining stage-5 blockers: TrustZone PAS
authentication (GPU_PAS_ID 13), a kernel caller instantiating the three
modules, the CX/GX genpd + 6 clocks power-up, and a real device kgsl SMMU
stage-1 context.

## Provenance

Module + test + cross verifier pushed as phase-1 commit
`ab4143cd6c60c98e2e03b9ee46fc3cdbf51afb61` (tree
`ad9a5365c79feda7ef1f36d59beffd95cc41ee6f`, parent `67afb565...`
round-38 head). Chain: `9e52009e...` -> `c05082f5...` (round 35 phase 1) ->
`168f086a...` (round 35 phase 2) -> `e5b5b358...` (round 38 phase 1) ->
`67afb565...` (round 38 phase 2) -> `ab4143cd...` (round 42 phase 1).
