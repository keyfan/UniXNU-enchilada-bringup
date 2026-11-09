// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoPowerGate -- enchilada stage 5 (GPU) HOST-VERIFIED power-domain study.
//
// This is NOT a GPU driver. It is a freestanding, pure-C encoding of the GPU
// power-domain PRECONDITIONS that must hold before ANY GPU/GMU/kgsl-SMMU MMIO
// is safe, plus a read-only gate a future A630 backend must consult. It maps
// nothing, reads/writes no register, loads no firmware, is NOT linked into the
// kernel, and is HOST-VERIFIED only (a DTB + a sha-pinned upstream source are
// descriptions, NOT device measurements). No CI, no flash, no device run.
//
// Why this exists (Round 22, 2026-11-09, load-bearing safety correction)
// ------------------------------------------------------------------------
// The Round-21 record's next_action said "Then read a safe register
// (RBBM_STATUS 0x210 / CP_ALWAYS_ON_COUNTER 0x980) on device". That is UNSAFE
// as literally written. On enchilada the GPU (0x5000000), GMU (0x506a000) and
// the DEDICATED kgsl SMMU (0x5040000) all sit in the COLLAPSIBLE gpu_cx /
// gpu_gx power domain:
//   qcom,kgsl-3d0@5000000  :23666-23668 regulator-names="vddcx\0vdd",
//       vddcx-supply=<0x1a4> (gpu_cx_gdsc), vdd-supply=<0x2a9> (gpu_gx_gdsc);
//       :23656-23657 six clocks (core_clk/rbbmtimer_clk/mem_clk/
//       mem_iface_clk/gmu_clk/l3_vote); :23646 qcom,no-nap.
//   arm,smmu-kgsl@5040000 :13966-13967 vdd-supply=<0x1a4> (SAME gpu_cx
//       regulator), :13969-13970 gcc_gpu_memnoc_gfx_clk.
//   GDSCs: :593-605 qcom,gdsc@0x509106c "gpu_cx_gdsc" phandle 0x1a4;
//       :607-621 qcom,gdsc@0x509100c "gpu_gx_gdsc" phandle 0x2a9.
// A bus read to an unpowered block can hang (no clock / rail collapsed).
//
// CONTRAST -- why flash #8's survey WAS safe: apps-smmu@0x15000000
// (:13975-13996) has `qcom,skip-init`, NO vdd-supply and NO clocks. Firmware
// leaves that SMMU powered/on, so a read-only survey of 0x15000000 was safe.
// That MUST NOT be generalised to 0x5040000 / 0x5000000 / 0x506a000.
//
// Upstream method (sha-pinned Linux v6.12, scripts/data/adreno-a630-inputs.json)
// -----------------------------------------------------------------------------
// A630 is a LEGACY GMU: a6xx_gmu.c:1736-1738 `adreno_is_a630_family(...) {
// /* HFI v1, has sptprac */ gmu->legacy = true; }`. Every GPU register access
// is gated behind the full CX/GX power-up + GMU boot:
//   a6xx_gmu.c:1622,1638  dev_pm_domain_attach_by_name(..., "cx"/"gx")
//   a6xx_gmu.c:1012       a6xx_gmu_resume()
//     :1033               pm_runtime_get_sync(gmu->dev)
//     :1041               pm_runtime_get_sync(gmu->gxpd)
//     :1044-1047          clk_set_rate(core/hub) + clk_bulk_prepare_enable()
//   a6xx_gmu.c:781        a6xx_gmu_fw_start(): a6xx_rpmh_start (:811) +
//                         a6xx_gmu_fw_load (:815)
//   a6xx_gmu.c:875-894    a6xx_gmu_power_config -> a6xx_gmu_start ->
//                         a6xx_gmu_gfx_rail_on (:882, legacy only) ->
//                         a6xx_sptprac_enable (:889, 0x778000 @:373) ->
//                         a6xx_gmu_hfi_start (:894)
//   a6xx_gpu.c:1291,1296  pm_runtime_get_sync(&gpu->pdev->dev) ->
//                         msm_gpu_hw_init() -> a6xx_hw_init (:1204) ->
//                         hw_init (:861) writes first RBBM registers;
//                         RBBM_STATUS reads (:29, :1220) happen WITH power on.
// Therefore the correct next_action is: build the A6XX_GEN1 backend + obtain
// the signed ZAP chain, bring up CX/GX/GMU, THEN (and only then) read a safe
// register. A cold RBBM_STATUS read is a hang risk, not a "safe probe".

#ifndef ADRENO_POWER_GATE_H
#define ADRENO_POWER_GATE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- GDSC / regulator board facts (DTB :593-621) ------------------------ */
#define ADRENO_POWER_GPU_CX_GDSC_ADDR   0x0509106cull  /* "gpu_cx_gdsc" */
#define ADRENO_POWER_GPU_GX_GDSC_ADDR   0x0509100cull  /* "gpu_gx_gdsc" */
#define ADRENO_POWER_GPU_CX_PHANDLE     0x1a4u         /* vddcx-supply */
#define ADRENO_POWER_GPU_GX_PHANDLE     0x2a9u         /* vdd-supply */
#define ADRENO_POWER_KGSL_SMMU_REGULATOR_PHANDLE 0x1a4u /* shares gpu_cx */

/* GPU clock-name count (DTB :23657) and kgsl-SMMU clock (DTB :13969-13970). */
#define ADRENO_POWER_GPU_CLOCK_COUNT     6u
#define ADRENO_POWER_KGSL_SMMU_CLOCK_COUNT 1u
#define ADRENO_POWER_MMIO_WINDOW_COUNT   3u  /* GPU + GMU + kgsl SMMU */

/* apps_smmu is the ALWAYS-ON contrast (qcom,skip-init, no supply/clock). */
#define ADRENO_POWER_APPS_SMMU_SKIP_INIT 1u
#define ADRENO_POWER_APPS_SMMU_SUPPLY_COUNT 0u
#define ADRENO_POWER_APPS_SMMU_CLOCK_COUNT  0u

/* Legacy GMU identification (upstream a6xx_gmu.c:1736-1738). */
#define ADRENO_POWER_A630_IS_LEGACY_GMU  1u
#define ADRENO_POWER_SPT_PRAC_POWER_CONTROL_VALUE 0x00778000u /* a6xx_gmu.c:373 */
#define ADRENO_POWER_GMU_HFI_VERSION_MAJOR 1u                  /* HFI v1 */

/* Ordered power-up steps (upstream a6xx_gmu_resume / a6xx_gmu_fw_start). */
enum adreno_power_step {
    kAdrenoPowerStepCxGenpdOn = 0,   /* pm_runtime_get_sync(gmu->dev), cxpd */
    kAdrenoPowerStepGxGenpdOn,       /* pm_runtime_get_sync(gmu->gxpd) */
    kAdrenoPowerStepClocksEnable,    /* clk_bulk_prepare_enable(6 clocks) */
    kAdrenoPowerStepRpmhStart,       /* a6xx_rpmh_start */
    kAdrenoPowerStepGmuFwLoad,       /* a6xx_gmu_fw_load (signed ZAP chain) */
    kAdrenoPowerStepGmuStart,        /* a6xx_gmu_start + gfx_rail_on */
    kAdrenoPowerStepSptPracEnable,   /* a6xx_sptprac_enable (0x778000) */
    kAdrenoPowerStepHfiStart,        /* a6xx_gmu_hfi_start (HFI v1) */
    kAdrenoPowerStepHwInit,          /* msm_gpu_hw_init -> a6xx_hw_init */
    kAdrenoPowerStepCount
};

#define ADRENO_POWER_STEP_COUNT ((uint32_t)kAdrenoPowerStepCount)

/* A read-only snapshot of the preconditions that must ALL be true before any
 * GPU/GMU/kgsl-SMMU register access. Zeroed = today's honest state. */
struct adreno_power_state {
    uint32_t cx_genpd_on;        /* gpu_cx_gdsc enabled */
    uint32_t gx_genpd_on;        /* gpu_gx_gdsc enabled */
    uint32_t clocks_enabled;     /* 6 GPU clocks + gcc_gpu_memnoc_gfx_clk */
    uint32_t kgsl_smmu_clock_on; /* gcc_gpu_memnoc_gfx_clk specifically */
    uint32_t gmu_fw_loaded;      /* signed ZAP + GMU microcode loaded */
    uint32_t gmu_booted;         /* GMU running (legacy HFI v1 / SPTPRAC) */
    uint32_t hw_init_done;       /* msm_gpu_hw_init / a6xx_hw_init completed */
};

/* Returns 1 iff EVERY precondition is set => GPU/GMU/kgsl-SMMU MMIO is
 * permitted. Returns 0 otherwise (including a NULL state). A cold probe with
 * an all-zero state MUST return 0: this is the load-bearing gate that forbids
 * the Round-21 "read RBBM_STATUS 0x210 on device" step as literally written. */
int adreno_power_mmio_permitted(const struct adreno_power_state *state);

/* Today's honest device state: CONFIG_XNU_NATIVE_ADRENO=n, no signed ZAP,
 * no A6XX_GEN1 backend => nothing powered. Always returns an all-zero struct. */
struct adreno_power_state adreno_power_current_state(void);

/* 1 iff the GPU MMIO windows (GPU/GMU/kgsl SMMU) are in the collapsible
 * gpu_cx/gpu_gx domain (board fact from the DTB). Contrast-only helper. */
int adreno_power_gpu_windows_are_gated(void);

/* 1 iff the apps_smmu @0x15000000 is an always-on, skip-init domain (the
 * reason flash #8's read-only survey of 0x15000000 was safe). */
int adreno_power_apps_smmu_probe_permitted(void);

/* 1 iff this GPU is a legacy GMU (A630 / A6XX_GEN1, HFI v1 + SPTPRAC). */
int adreno_power_is_legacy_gmu(void);

/* Returns the number of ordered power-up steps still required before a safe
 * GPU register read, given the state (kAdrenoPowerStepCount minus steps that
 * are already satisfied). A fully-powered state returns 0. */
uint32_t adreno_power_steps_remaining(const struct adreno_power_state *state);

#ifdef __cplusplus
}
#endif
#endif
