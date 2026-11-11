// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoKGSLSmmuGate.h -- ROUND 53 HOST-VERIFIED fail-closed decision layer for
// a proposed READ of the enchilada GPU's DEDICATED kgsl SMMU @ 0x05040000.
//
// WHY THIS MODULE EXISTS
// ----------------------
// The GPU does NOT use apps_smmu @ 0x15000000 (phandle 0x29, 2-cell SIDs);
// all four GPU iommus use arm,smmu-kgsl@5040000 (phandle 0x1ac, 1-cell SIDs).
// Flash #8 (round 20/21) surveyed apps_smmu only. The kgsl SMMU has NEVER been
// read on the device. A naive "just read it" probe is UNSAFE while gpu_cx is
// collapsed: enchilada.dts:13967 gives the kgsl SMMU `vdd-supply = <0x1a4>`,
// and :595/:604 show 0x1a4 IS `gpu_cx_gdsc`. Device logs (rounds 35/49/50/52)
// measured `verdict=cx-DOWN` 1912 times with words cx=00005ff4 gx=00088400.
//
// THE LOAD-BEARING UPSTREAM RULE (Linux v6.12, sha-pinned)
// --------------------------------------------------------
//   arm-smmu.c:2196  err = clk_bulk_prepare_enable(smmu->num_clks, smmu->clks);
//   arm-smmu.c:2200  err = arm_smmu_device_cfg_probe(smmu);
//   arm-smmu.c:2255  arm_smmu_device_reset(smmu);
//   arm-smmu.c:2264  pm_runtime_set_active(dev); pm_runtime_enable(dev);
//   arm-smmu.c:71-77 arm_smmu_rpm_get() -> pm_runtime_resume_and_get(smmu->dev)
//   a6xx_gmu.c:1033 pm_runtime_get_sync(gmu->dev);        /* CX genpd      */
//   a6xx_gmu.c:1041 pm_runtime_get_sync(gmu->gxpd);       /* GX genpd      */
//   a6xx_gmu.c:1047 clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks);
//   a6xx_gmu.c:1277 msm_iommu_new(gmu->dev, 0);           /* AFTER power   */
// Mainline ALWAYS enables the SMMU clocks AND resumes the power domain BEFORE
// it touches any SMMU register. Touching a powered-down SMMU register can
// bus-abort. Therefore no kernel probe may read 0x05040000 until BOTH the
// `gcc_gpu_memnoc_gfx_clk` clock and the `gpu_cx` GDSC are up.
//
// HONEST NUANCE (do not over-claim)
// ---------------------------------
// AdrenoDomainState.h:17-22 notes GPUCC *status* registers ARE readable with
// the domain collapsed because GPUCC sits on always-on PMIC rails, and
// AdrenoDomainState.c:58-77 carves GPUCC out of the forbidden-window list.
// That exception does NOT transfer to the kgsl SMMU: the SMMU is on the
// gpu_cx rail (0x1a4), a DIFFERENT power domain. This module encodes that
// distinction explicitly via AdrenoKGSLSmmuGateGpuccExceptionDoesNotApply().
//
// SCOPE: pure predicates over caller-supplied facts. NO MMIO, NO IOKit, NO
// firmware, NO kernel caller, NO device run, NO flash. The gate merely DECIDES
// whether a future read would be safe; it never performs one. gpu area STAYS
// host-verified. This is a SAFETY STUDY, not a driver and not a measurement.
//
#ifndef ADRENO_KGSL_SMMU_GATE_H
#define ADRENO_KGSL_SMMU_GATE_H

#include <stdint.h>
#include "AdrenoBoardInputs.h"
#include "AdrenoDomainState.h"

/* Board facts re-exported verbatim so a caller cannot silently use the wrong
 * window. All four come from enchilada.dts (sha256 e590ee32...) and are already
 * pinned in AdrenoBoardInputs.h; the equality assertions live in the .c. */
#define ADRENO_KGSL_GATE_BASE       ADRENO_BOARD_KGSL_SMMU_BASE       /* 0x05040000ull */
#define ADRENO_KGSL_GATE_SIZE       ADRENO_BOARD_KGSL_SMMU_SIZE       /* 0x00010000ull */
#define ADRENO_KGSL_GATE_PHANDLE    ADRENO_BOARD_KGSL_SMMU_PHANDLE    /* 0x1acu        */
#define ADRENO_KGSL_GATE_IOMMU_CELLS ADRENO_BOARD_KGSL_SMMU_IOMMU_CELLS /* 1u         */
#define ADRENO_KGSL_GATE_APPS_BASE  ADRENO_BOARD_APPS_SMMU_BASE        /* 0x15000000ull*/
#define ADRENO_KGSL_GATE_APPS_PHANDLE ADRENO_BOARD_APPS_SMMU_PHANDLE   /* 0x29u        */

/* The upstream ORDER: clocks + domain must be established before registers.
 * Encoded as a 2-bit mask so a caller cannot accidentally skip a step. */
#define ADRENO_KGSL_GATE_STEP_CLOCKS  0x1u
#define ADRENO_KGSL_GATE_STEP_DOMAIN  0x2u
#define ADRENO_KGSL_GATE_STEP_ALL     (ADRENO_KGSL_GATE_STEP_CLOCKS | ADRENO_KGSL_GATE_STEP_DOMAIN)

/* Fail-closed verdict: the default for ANY doubt is a refusal. */
typedef enum {
    kAdrenoKGSLSmmuGatePermitted       = 0, /* every precondition met        */
    kAdrenoKGSLSmmuGateRefusedInvalid  = 1, /* NULL / zero / overflow window */
    kAdrenoKGSLSmmuGateRefusedWrongSmmu = 2,/* caller targeted apps_smmu     */
    kAdrenoKGSLSmmuGateRefusedWindow   = 3, /* window is not the kgsl SMMU   */
    kAdrenoKGSLSmmuGateRefusedCxDown   = 4, /* device-measured CX collapsed  */
    kAdrenoKGSLSmmuGateRefusedClocksOff = 5,/* clocks not yet enabled (2196)*/
    kAdrenoKGSLSmmuGateRefusedDomainOff = 6 /* gpu_cx not powered (2264)     */
} AdrenoKGSLSmmuGateVerdict;

/* Caller-supplied facts for ONE proposed read. All are descriptions, NOT
 * measured-by-this-module values; the caller must obtain them honestly. */
struct AdrenoKGSLSmmuGateInputs {
    uint64_t window_base;      /* proposed MMIO window base                  */
    uint64_t window_size;      /* proposed MMIO window size                  */
    uint32_t smmu_phandle;     /* which SMMU the caller INTENDS to read      */
    uint32_t cx_status_word;   /* GPUCC CX status (AdrenoDomainState decode) */
    uint32_t clocks_enabled;   /* 0/1: gcc_gpu_memnoc_gfx_clk enabled        */
    uint32_t domain_powered;   /* 0/1: gpu_cx GDSC voted on                  */
};

/* ---- pure board/plumbing predicates (never touch hardware) ---------------- */

/* 1 iff the pinned board constants are internally consistent: the two SMMUs
 * really are different units, the GPU phandle resolves to kgsl (never apps),
 * and the kgsl SMMU declares single-cell (unmasked) SIDs. */
int AdrenoKGSLSmmuGateBoardIsConsistent(void);

/* 1 iff a proposed window is EXACTLY the kgsl SMMU register window. */
int AdrenoKGSLSmmuGateWindowIsKgsl(uint64_t base, uint64_t size);

/* 1 iff phandle is the dedicated kgsl SMMU (0x1ac), NOT apps_smmu (0x29). */
int AdrenoKGSLSmmuGatePhandleIsKgsl(uint32_t phandle);

/* Encodes the upstream rule: returns ADRENO_KGSL_GATE_STEP_ALL, i.e. BOTH
 * clocks and domain are required before any register access. Never 0. */
uint32_t AdrenoKGSLSmmuGateRequiredSteps(void);

/* 1 iff the GPUCC always-on exception does NOT apply to the kgsl SMMU.
 * It does not: the SMMU rides gpu_cx (0x1a4), GPUCC rides always-on rails. */
int AdrenoKGSLSmmuGateGpuccExceptionDoesNotApply(void);

/* Enumerate the GPU context SIDs (unmasked, 1-cell). Returns the count (5)
 * and fills out_sids[0..4] = {0, 2, 1, 4, 5}. Returns 0 on NULL. */
uint32_t AdrenoKGSLSmmuGateContextSids(uint32_t *out_sids, uint32_t capacity);

/* ---- the fail-closed decision --------------------------------------------- */

/* Given caller-supplied facts, decide whether ONE read of the kgsl SMMU window
 * is safe. NULL `in` -> RefusedInvalid. The check ORDER is load-bearing:
 *   wrong SMMU / wrong window / CX down / clocks off / domain off -> refuse;
 *   only if ALL pass -> Permitted. No step is skippable. */
AdrenoKGSLSmmuGateVerdict AdrenoKGSLSmmuGateAssess(const struct AdrenoKGSLSmmuGateInputs *in);

/* Human-readable verdict name (no libc; static strings). */
const char *AdrenoKGSLSmmuGateVerdictName(AdrenoKGSLSmmuGateVerdict v);

/* Convenience: 1 iff Assess() == Permitted. Still pure, still caller-gated. */
int AdrenoKGSLSmmuGateReadPermitted(const struct AdrenoKGSLSmmuGateInputs *in);

#endif /* ADRENO_KGSL_SMMU_GATE_H */
