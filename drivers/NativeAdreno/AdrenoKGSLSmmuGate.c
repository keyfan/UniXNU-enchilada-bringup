// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoKGSLSmmuGate.c -- ROUND 53 HOST-VERIFIED fail-closed decision layer.
// Pure C11. No MMIO, no IOKit, no firmware, no kernel caller, no device run.
// See AdrenoKGSLSmmuGate.h for the upstream rule and the honest boundary.
//
#include "AdrenoKGSLSmmuGate.h"

/* Upstream order (sha-pinned Linux v6.12):
 *   arm-smmu.c:2196 clk_bulk_prepare_enable(...)  BEFORE :2200 cfg_probe / :2255 reset
 *   arm-smmu.c:2264 pm_runtime_set_active/enable  (domain resume)
 *   arm-smmu.c:71-77 arm_smmu_rpm_get -> pm_runtime_resume_and_get()
 *   a6xx_gmu.c:1033/:1041 pm_runtime_get_sync(cx/gx), :1047 clk_bulk_prepare_enable
 *   a6xx_gmu.c:1277 msm_iommu_new() AFTER power
 * Both clocks AND the gpu_cx domain must be up before ANY SMMU register read.
 */
uint32_t AdrenoKGSLSmmuGateRequiredSteps(void)
{
    return ADRENO_KGSL_GATE_STEP_ALL;
}

int AdrenoKGSLSmmuGatePhandleIsKgsl(uint32_t phandle)
{
    return adreno_board_is_kgsl_smmu(phandle);
}

int AdrenoKGSLSmmuGateBoardIsConsistent(void)
{
    /* Two independent units, not one block described twice. */
    if (ADRENO_KGSL_GATE_BASE == ADRENO_KGSL_GATE_APPS_BASE) {
        return 0;
    }
    if (ADRENO_KGSL_GATE_PHANDLE == ADRENO_KGSL_GATE_APPS_PHANDLE) {
        return 0;
    }
    /* The GPU phandle must be kgsl, never apps_smmu. */
    if (!AdrenoKGSLSmmuGatePhandleIsKgsl(ADRENO_KGSL_GATE_PHANDLE)) {
        return 0;
    }
    if (AdrenoKGSLSmmuGatePhandleIsKgsl(ADRENO_KGSL_GATE_APPS_PHANDLE)) {
        return 0;
    }
    /* Single-cell (unmasked) SIDs is the round-21 load-bearing correction. */
    if (!adreno_board_sid_is_unmasked()) {
        return 0;
    }
    /* 0x05040000 IS in the collapsible-domain forbidden window list; the
     * GPUCC carve-out must NOT whitelist it (different power rail). */
    if (!adreno_domain_window_is_forbidden(ADRENO_KGSL_GATE_BASE,
                                           ADRENO_KGSL_GATE_SIZE)) {
        return 0;
    }
    return 1;
}

int AdrenoKGSLSmmuGateWindowIsKgsl(uint64_t base, uint64_t size)
{
    return (base == ADRENO_KGSL_GATE_BASE &&
            size == ADRENO_KGSL_GATE_SIZE) ? 1 : 0;
}

int AdrenoKGSLSmmuGateGpuccExceptionDoesNotApply(void)
{
    /* GPUCC status registers are readable with the domain collapsed because
     * GPUCC rides always-on PMIC rails (AdrenoDomainState.c:58-77 carve-out).
     * The kgsl SMMU rides gpu_cx (dtb :13967 vdd-supply=<0x1a4>, :595/:604
     * 0x1a4 == gpu_cx_gdsc). Prove the two windows are distinct, and that the
     * kgsl window is NOT inside GPUCC: the exception cannot transfer. */
    if (!adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_GPUCC_BASE)) {
        return 0; /* GPUCC base would not even be classified as a probe window */
    }
    if (adreno_domain_addr_is_probe_window(ADRENO_KGSL_GATE_BASE)) {
        return 0; /* kgsl SMMU must NOT be a GPUCC-window address */
    }
    if (!adreno_domain_window_is_forbidden(ADRENO_KGSL_GATE_BASE,
                                           ADRENO_KGSL_GATE_SIZE)) {
        return 0; /* if it were whitelisted, the exception WOULD (wrongly) apply */
    }
    return 1;
}

uint32_t AdrenoKGSLSmmuGateContextSids(uint32_t *out_sids, uint32_t capacity)
{
    static const enum adreno_board_context kCtx[5] = {
        kAdrenoBoardCtxGfx3dUser,
        kAdrenoBoardCtxGfx3dSecure,
        kAdrenoBoardCtxGfx3dSecureAlt,
        kAdrenoBoardCtxGmuUser,
        kAdrenoBoardCtxGmuKernel,
    };
    uint32_t i;

    if (!out_sids || capacity < 5u) {
        return 0u;
    }
    for (i = 0u; i < 5u; ++i) {
        uint32_t sid = 0u;
        if (!adreno_board_context_sid(kCtx[i], &sid) || sid > 5u) {
            return 0u;
        }
        out_sids[i] = sid;
    }
    return 5u;
}

AdrenoKGSLSmmuGateVerdict AdrenoKGSLSmmuGateAssess(const struct AdrenoKGSLSmmuGateInputs *in)
{
    if (!in) {
        return kAdrenoKGSLSmmuGateRefusedInvalid;
    }
    if (in->window_size == 0ull || (in->window_base + in->window_size) < in->window_base) {
        return kAdrenoKGSLSmmuGateRefusedInvalid;
    }
    /* Board plumbing must be sane BEFORE any device-state question. */
    if (!AdrenoKGSLSmmuGateBoardIsConsistent()) {
        return kAdrenoKGSLSmmuGateRefusedInvalid;
    }
    /* The caller must INTEND the dedicated kgsl SMMU, never apps_smmu. */
    if (!AdrenoKGSLSmmuGatePhandleIsKgsl(in->smmu_phandle)) {
        return kAdrenoKGSLSmmuGateRefusedWrongSmmu;
    }
    /* The window must be EXACTLY the kgsl SMMU register file. */
    if (!AdrenoKGSLSmmuGateWindowIsKgsl(in->window_base, in->window_size)) {
        return kAdrenoKGSLSmmuGateRefusedWindow;
    }
    /* Device-measured state: CX collapsed -> powered-down SMMU -> refuse.
     * Round 35/49/50/52 measured cx=00005ff4 (bit31 clear) 1912 times. */
    if (!adreno_domain_cx_is_on(in->cx_status_word)) {
        return kAdrenoKGSLSmmuGateRefusedCxDown;
    }
    /* Upstream rule step 1: clocks BEFORE registers (arm-smmu.c:2196). */
    if (!in->clocks_enabled) {
        return kAdrenoKGSLSmmuGateRefusedClocksOff;
    }
    /* Upstream rule step 2: power domain BEFORE registers (arm-smmu.c:2264). */
    if (!in->domain_powered) {
        return kAdrenoKGSLSmmuGateRefusedDomainOff;
    }
    return kAdrenoKGSLSmmuGatePermitted;
}

int AdrenoKGSLSmmuGateReadPermitted(const struct AdrenoKGSLSmmuGateInputs *in)
{
    return (AdrenoKGSLSmmuGateAssess(in) == kAdrenoKGSLSmmuGatePermitted) ? 1 : 0;
}

const char *AdrenoKGSLSmmuGateVerdictName(AdrenoKGSLSmmuGateVerdict v)
{
    switch (v) {
    case kAdrenoKGSLSmmuGatePermitted:         return "Permitted";
    case kAdrenoKGSLSmmuGateRefusedInvalid:    return "RefusedInvalid";
    case kAdrenoKGSLSmmuGateRefusedWrongSmmu:  return "RefusedWrongSmmu";
    case kAdrenoKGSLSmmuGateRefusedWindow:     return "RefusedWindow";
    case kAdrenoKGSLSmmuGateRefusedCxDown:     return "RefusedCxDown";
    case kAdrenoKGSLSmmuGateRefusedClocksOff:  return "RefusedClocksOff";
    case kAdrenoKGSLSmmuGateRefusedDomainOff:  return "RefusedDomainOff";
    default:                                   return "RefusedInvalid";
    }
}
