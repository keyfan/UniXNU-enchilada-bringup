# ROUND 48 · Date: 2026-11-11 — Conditional GMU boot-state probe (stage 2), HOST-VERIFIED

## What was built

Round 48 extends the device-verified GPUCC probe `package/EnchiladaAdrenoProbe.cpp`
with a stage-2 observation behind the existing default-OFF boot-arg
(`enchilada-adreno=2`):

1. The stage-1 GPUCC observation runs FIRST and is unchanged (its verdict line,
   the round-35 device-verified behavior, is preserved byte-for-byte in spirit:
   same two status reads, same decode, same replay mechanism).
2. The CX status word just read is the ONLY permission source for stage 2.
   `CX status BIT(31)` up (mainline gdsc.c:66-71 field, host-tested in
   AdrenoDomainState) permits mapping the GMU window; CX down records
   `stage2=cx-down-gmu-not-mapped` and never maps — the round-35 measured
   steady state makes this the EXPECTED outcome on a port that has not powered
   the GPU.
3. With CX up, the GMU window (0x506a000 / 0x30000, `AdrenoBoardInputs.h`, the
   pinned DTB `qcom,gmu` reg at :23875) is mapped once via the device-proven
   `armcpu_map_physical` and `GENERAL_7` (word 0x51cc, `a6xx_gmu.xml
   GMU_GENERAL_7`; byte 0x14730) is read ONCE, decoded WARM/COLD with the
   round-42 host-tested `AdrenoGmuBootV1BootState()` (upstream a6xx_gmu.c
   :1066-1068 legacy branch).

## Two-domain hardware contract (round-48 pre-flight, both PASS)

- `work/tmp/_r48_preflight_final.py` → `R48_PREFLIGHT_FINAL: PASS (0 failures)`:
  Domain 1 = pinned `a6xx_gmu.c` exact-line contracts (GENERAL_7 warm/cold
  discriminator :1067, fw_start order, BOOT_CONFIG 0x02, CM3_CFG 0x4052, legacy
  gfx_rail_on/sptprac/hfi_start order, rpmh REQ/ACK handshake).
  Domain 2 = register XML offsets read independently from
  `drivers/gpu/drm/msm/registers/adreno/a6xx_gmu.xml`: RSCC_CONTROL_REQ 0x9307,
  RSCC_CONTROL_ACK 0x9308, CM3_FW_INIT_RESULT 0x501c, CM3_BOOT_CONFIG 0x5001,
  CM3_CFG 0x502d, GENERAL_7 0x51cc, CM3_ITCM_START 0xc00 — all matching the
  round-46 caller-test constants.
- `work/tmp/_r48_dtb_gmu_contract.py` → `R48_DTB_GMU_CONTRACT: PASS`: the
  downstream vendor DTB has ONE 3 MiB PDC window `reg=<0x506a000 0x30000
  0xb200000 0x300000>`; the two upstream 64 KiB windows lie inside it:
  pdc = base + 0x90000 (0xb290000), pdc_seq = base + 0x290000 (0xb490000).
  Legacy RSCC = gmu_mmio + 0x23000 = 0x508d000 (a6xx_gmu.c:1771 else-branch).
- Device observation (READ-ONLY adb, TWRP): `/dev/kgsl-3d0` EXISTS (char
  233:0) on the 4.9 downstream kernel; no debugfs clk, no genpd exposed.
  Boundary: TWRP observes, it cannot execute the XNU-side caller chain.

## Safety contract of stage 2 (encoded in code, not comments)

- Default OFF: absent boot-arg or any other value = no-op (unchanged stage-1
  behavior).
- The GMU window gate `adreno_board_window_is_gmu()` refuses any base/size
  that is not exactly the pinned DTB window and refuses when GENERAL_7's byte
  offset (0x14730) would fall outside the window.
- Zero writes: the only dereference is a volatile uint32_t load (the shim
  checker proves "no store through a pointer" mechanically).
- Single observation + 1 Hz text replay (no MMIO in replay), same mechanism
  the device-verified stage-1 probe uses.
- Honest residual risk, logged in the header comment: CX up does NOT prove
  gmu_clk is on (upstream enables clocks between genpd and the GENERAL_7
  read; a probe cannot write clock gates). A powered-but-unclocked read may
  return data of undefined currency; the raw word, the decode and the caveat
  are all logged. Arming the stage also requires an explicit flash decision
  (m12247: verify before flashing; flash itself needs separate authorization).

## Verifications (m03546 two-verification discipline)

1. `scripts/check-enchilada-adreno-probe.py` (host shim, strict C++17/C11
   arm64 Mach-O, store-free proof): **18/18 checks PASS** — run twice
   (r48c/r48d, independent object dirs).
2. `scripts/check-enchilada-adreno-host.py` full suite: **PASS, 10 suites**
   (catalog/hwinit/board-input/power-gate/domain-state/hfi-v1/firmware-v1/
   gmu-boot-v1/gmu-bus-v1/gmu-caller-v1; caller 50 checks, 0 failures) — the
   stage-2 edits did not disturb any host-tested module.
3. Round-48 two-domain pre-flight (above) — PASS.

## Honest boundary

HOST-VERIFIED ONLY. No flash, no device boot, no register read through OUR
kernel. The gpu area stays host-verified. Stage 2 is a READY, fail-closed,
device-proven-pattern probe awaiting an authorized flash decision.

## Chain

Previous: `8194bdd0` (round 47, CI #222 10 objects). This round: no push yet
at evidence-creation time; record citation added in the ROUND 48 porting.toml
entry.
