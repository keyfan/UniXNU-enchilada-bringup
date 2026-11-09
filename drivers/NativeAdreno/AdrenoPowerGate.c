// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoPowerGate.c -- see AdrenoPowerGate.h for provenance. Pure-C, freestanding,
// HOST-VERIFIED, no MMIO, no firmware, NOT linked into the kernel.

#include "AdrenoPowerGate.h"

int adreno_power_mmio_permitted(const struct adreno_power_state *state)
{
    if (state == NULL)
        return 0;
    if (!state->cx_genpd_on)
        return 0;
    if (!state->gx_genpd_on)
        return 0;
    if (!state->clocks_enabled)
        return 0;
    if (!state->kgsl_smmu_clock_on)
        return 0;
    if (!state->gmu_fw_loaded)
        return 0;
    if (!state->gmu_booted)
        return 0;
    if (!state->hw_init_done)
        return 0;
    return 1;
}

struct adreno_power_state adreno_power_current_state(void)
{
    struct adreno_power_state s;
    s.cx_genpd_on = 0u;
    s.gx_genpd_on = 0u;
    s.clocks_enabled = 0u;
    s.kgsl_smmu_clock_on = 0u;
    s.gmu_fw_loaded = 0u;
    s.gmu_booted = 0u;
    s.hw_init_done = 0u;
    return s;
}

int adreno_power_gpu_windows_are_gated(void)
{
    /* DTB: GPU/GMU/kgsl SMMU all carry vddcx/vdd supplies + clocks, while
     * apps_smmu has qcom,skip-init and no supply/clock. */
    if (ADRENO_POWER_GPU_CX_PHANDLE != ADRENO_POWER_KGSL_SMMU_REGULATOR_PHANDLE)
        return 0;  /* the kgsl SMMU must share the gpu_cx regulator */
    if (ADRENO_POWER_GPU_CLOCK_COUNT == 0u)
        return 0;
    if (ADRENO_POWER_KGSL_SMMU_CLOCK_COUNT == 0u)
        return 0;
    if (ADRENO_POWER_MMIO_WINDOW_COUNT != 3u)
        return 0;
    return 1;
}

int adreno_power_apps_smmu_probe_permitted(void)
{
    /* flash #8 read 0x15000000 safely because firmware leaves it on. */
    if (!ADRENO_POWER_APPS_SMMU_SKIP_INIT)
        return 0;
    if (ADRENO_POWER_APPS_SMMU_SUPPLY_COUNT != 0u)
        return 0;
    if (ADRENO_POWER_APPS_SMMU_CLOCK_COUNT != 0u)
        return 0;
    return 1;
}

int adreno_power_is_legacy_gmu(void)
{
    /* a6xx_gmu.c:1736-1738 -- A630 is legacy (HFI v1, SPTPRAC). */
    if (!ADRENO_POWER_A630_IS_LEGACY_GMU)
        return 0;
    if (ADRENO_POWER_GMU_HFI_VERSION_MAJOR != 1u)
        return 0;
    if (ADRENO_POWER_SPT_PRAC_POWER_CONTROL_VALUE != 0x00778000u)
        return 0;
    return 1;
}

uint32_t adreno_power_steps_remaining(const struct adreno_power_state *state)
{
    if (state == NULL)
        return ADRENO_POWER_STEP_COUNT;
    if (adreno_power_mmio_permitted(state))
        return 0u;
    /* BOARD-level gate only: without a powered, booted GMU no step can be
     * considered satisfied, so ALL ordered steps remain. A more granular
     * counter would require device measurements we do not have. */
    return ADRENO_POWER_STEP_COUNT;
}
