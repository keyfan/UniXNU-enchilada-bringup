// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoPowerUpV1.c -- enchilada A6XX_GEN1 (A630) ordered genpd + clock
// power-up sequence, modeled on Linux v6.12 a6xx_gmu_resume() :1012-1060.
// Freestanding pure C11: no libc, no MMIO, no kernel linkage, no constructors.
// Every side effect is routed through the caller-wired AdrenoPowerUpV1Bus.
// See AdrenoPowerUpV1.h for the sha256-pinned provenance and honest limits.

#include "AdrenoPowerUpV1.h"

#include <stdint.h>
#include <stddef.h>

/* ---- pure board constants ------------------------------------------------- */

uint32_t AdrenoPowerUpV1BoardGmuClockCount(void)
{
    return ADRENO_POWER_UP_V1_GMU_CLOCKS;
}

uint32_t AdrenoPowerUpV1BoardGpuClockCount(void)
{
    return ADRENO_POWER_UP_V1_GPU_CLOCKS;
}

uint32_t AdrenoPowerUpV1BoardIsA740Family(void)
{
    return ADRENO_POWER_UP_V1_IS_A740_FAMILY;
}

uint32_t AdrenoPowerUpV1CoreRateHz(void)
{
    return ADRENO_POWER_UP_V1_CORE_RATE_HZ;
}

uint32_t AdrenoPowerUpV1HubRateHz(uint32_t a740_family)
{
    /* a6xx_gmu.c:1045-1046 -- exact ternary, no approximation. */
    return a740_family ? ADRENO_POWER_UP_V1_HUB_RATE_A740
                       : ADRENO_POWER_UP_V1_HUB_RATE_A630;
}

int AdrenoPowerUpV1CountsAreDistinct(uint32_t gmu_count, uint32_t gpu_count)
{
    return gmu_count != 0u && gpu_count != 0u && gmu_count != gpu_count;
}

static int apuv1_bool(uint32_t v)
{
    return v == 0u || v == 1u;
}

int AdrenoPowerUpV1InputsValid(const struct AdrenoPowerUpV1Inputs *in)
{
    if (in == (const struct AdrenoPowerUpV1Inputs *)0) {
        return 0;
    }
    if (!apuv1_bool(in->gxpd_present) || !apuv1_bool(in->a740_family) ||
        !apuv1_bool(in->strict_gx)) {
        return 0;
    }
    if (!AdrenoPowerUpV1CountsAreDistinct(in->gmu_clock_count,
                                          in->gpu_clock_count)) {
        return 0;
    }
    return 1;
}

int AdrenoPowerUpV1PreIsReady(const struct AdrenoGmuCallerV1Pre *pre)
{
    /* Mirrors round-46 caller_v1_pre_ok() (AdrenoGmuCallerV1.c:12-17). */
    return pre != (const struct AdrenoGmuCallerV1Pre *)0 &&
           pre->cx_genpd_on != 0u && pre->gx_genpd_on != 0u &&
           pre->clocks_enabled != 0u;
}

AdrenoPowerUpV1Status AdrenoPowerUpV1StepStatus(uint32_t step)
{
    switch (step) {
    case ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON:
        return kAdrenoPowerUpV1CxFailed;
    case ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON:
        return kAdrenoPowerUpV1GxFailed;
    case ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE:
        return kAdrenoPowerUpV1CoreRateFailed;
    case ADRENO_POWER_UP_V1_STEP_SET_HUB_RATE:
        return kAdrenoPowerUpV1HubRateFailed;
    case ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE:
        return kAdrenoPowerUpV1ClocksFailed;
    default:
        return kAdrenoPowerUpV1Invalid;
    }
}

uint32_t AdrenoPowerUpV1StepGuarded(uint32_t step)
{
    /* Only a6xx_gmu.c:1040-1041 is guarded by !IS_ERR_OR_NULL(gmu->gxpd). */
    return step == ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON ? 1u : 0u;
}

/* ---- plan ----------------------------------------------------------------- */

uint32_t AdrenoPowerUpV1PlanSize(const struct AdrenoPowerUpV1Inputs *in)
{
    if (!AdrenoPowerUpV1InputsValid(in)) {
        return 0u;
    }
    return ADRENO_POWER_UP_V1_STEP_COUNT;
}

AdrenoPowerUpV1Status AdrenoPowerUpV1Plan(const struct AdrenoPowerUpV1Inputs *in,
    AdrenoPowerUpV1Op *ops, uint32_t capacity, uint32_t *count)
{
    uint32_t n = 0u;

    if (!AdrenoPowerUpV1InputsValid(in) ||
        ops == (AdrenoPowerUpV1Op *)0 || count == (uint32_t *)0 ||
        capacity < ADRENO_POWER_UP_V1_STEP_COUNT) {
        return kAdrenoPowerUpV1Invalid;
    }

    /* :1033 CX genpd (always first). */
    ops[n].step = ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON;
    ops[n].guarded = AdrenoPowerUpV1StepGuarded(ops[n].step);
    ops[n].value = 0u;
    n++;

    /* :1040-1041 GX genpd (guarded). Emitted even when absent so the plan is
     * deterministic; Execute skips it when gxpd_present == 0. */
    ops[n].step = ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON;
    ops[n].guarded = AdrenoPowerUpV1StepGuarded(ops[n].step);
    ops[n].value = 0u;
    n++;

    /* :1044 core rate. */
    ops[n].step = ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE;
    ops[n].guarded = AdrenoPowerUpV1StepGuarded(ops[n].step);
    ops[n].value = AdrenoPowerUpV1CoreRateHz();
    n++;

    /* :1045-1046 hub rate, generation-selected. */
    ops[n].step = ADRENO_POWER_UP_V1_STEP_SET_HUB_RATE;
    ops[n].guarded = AdrenoPowerUpV1StepGuarded(ops[n].step);
    ops[n].value = AdrenoPowerUpV1HubRateHz(in->a740_family);
    n++;

    /* :1047 clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks). The count
     * is the GMU device's 4 clocks, NOT the kgsl-3d0 node's 6. */
    ops[n].step = ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE;
    ops[n].guarded = AdrenoPowerUpV1StepGuarded(ops[n].step);
    ops[n].value = in->gmu_clock_count;
    n++;

    *count = n;
    return kAdrenoPowerUpV1Ok;
}

/* ---- execute -------------------------------------------------------------- */

static void apuv1_zero_pre(struct AdrenoGmuCallerV1Pre *pre)
{
    pre->cx_genpd_on = 0u;
    pre->gx_genpd_on = 0u;
    pre->clocks_enabled = 0u;
    /* require_gx is POLICY, not execution state: keep the caller's choice. */
}

static int apuv1_bus_ok(const struct AdrenoPowerUpV1Bus *bus)
{
    return bus != (const struct AdrenoPowerUpV1Bus *)0 &&
           bus->genpd_on != 0 && bus->genpd_off != 0 &&
           bus->set_rate != 0 && bus->clocks_enable != 0;
}

/* Roll back acquired genpds in REVERSE acquisition order (GX then CX),
 * matching upstream :1049-1050 but generalized to ANY failed step.
 * Returns 0 on success, non-zero if a genpd_off callback failed. */
static int apuv1_rollback(const struct AdrenoPowerUpV1Bus *bus,
                          uint32_t gx_acquired, uint32_t cx_acquired)
{
    int rc = 0;
    if (gx_acquired) {
        if (bus->genpd_off(bus->cookie, 1u) != 0) {
            rc = 1;
        }
    }
    if (cx_acquired) {
        if (bus->genpd_off(bus->cookie, 0u) != 0) {
            rc = 1;
        }
    }
    return rc;
}

AdrenoPowerUpV1Status AdrenoPowerUpV1Execute(
    const struct AdrenoPowerUpV1Inputs *in,
    const AdrenoPowerUpV1Op *ops, uint32_t count,
    const struct AdrenoPowerUpV1Bus *bus,
    struct AdrenoGmuCallerV1Pre *pre, uint32_t *fail_index)
{
    uint32_t i;
    uint32_t cx_acquired = 0u;
    uint32_t gx_acquired = 0u;
    int failed = 0;
    uint32_t first_fail = ADRENO_POWER_UP_V1_STEP_COUNT;
    AdrenoPowerUpV1Status status = kAdrenoPowerUpV1Ok;

    if (!AdrenoPowerUpV1InputsValid(in) ||
        ops == (const AdrenoPowerUpV1Op *)0 ||
        count != ADRENO_POWER_UP_V1_STEP_COUNT ||
        !apuv1_bus_ok(bus) || pre == (struct AdrenoGmuCallerV1Pre *)0) {
        return kAdrenoPowerUpV1Invalid;
    }

    pre->require_gx = in->strict_gx;
    apuv1_zero_pre(pre);

    for (i = 0u; i < count; i++) {
        const AdrenoPowerUpV1Op *op = &ops[i];
        int rc;

        /* The guarded GX step is SKIPPED when gxpd_present == 0. It is not a
         * failure; pre->gx_genpd_on stays 0 and the round-46 caller refuses. */
        if (op->guarded && !in->gxpd_present) {
            continue;
        }

        switch (op->step) {
        case ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON:
            rc = bus->genpd_on(bus->cookie, 0u);
            if (rc == 0) {
                cx_acquired = 1u;
            }
            break;
        case ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON:
            rc = bus->genpd_on(bus->cookie, 1u);
            if (rc == 0) {
                gx_acquired = 1u;
            }
            break;
        case ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE:
        case ADRENO_POWER_UP_V1_STEP_SET_HUB_RATE:
            rc = bus->set_rate(bus->cookie,
                               op->step == ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE
                                   ? 0u : 1u,
                               op->value);
            break;
        case ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE:
            rc = bus->clocks_enable(bus->cookie, op->value);
            break;
        default:
            rc = 1;
            break;
        }

        if (rc != 0) {
            failed = 1;
            first_fail = i;
            status = AdrenoPowerUpV1StepStatus(op->step);
            break;
        }
    }

    if (failed) {
        int rollback_rc = apuv1_rollback(bus, gx_acquired, cx_acquired);
        apuv1_zero_pre(pre);
        if (rollback_rc != 0) {
            status = kAdrenoPowerUpV1RollbackFailed;
        }
    } else {
        pre->cx_genpd_on = cx_acquired;
        pre->gx_genpd_on = gx_acquired;
        pre->clocks_enabled = 1u;
    }

    if (fail_index != (uint32_t *)0) {
        *fail_index = first_fail;
    }
    return status;
}
