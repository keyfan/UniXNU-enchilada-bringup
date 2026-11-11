// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoPowerDownV1.c -- see AdrenoPowerDownV1.h for the derivation and the
// two honest findings. Freestanding pure C11: no libc, no MMIO, no ctors.

#include "AdrenoPowerDownV1.h"

#include "AdrenoGmuCallerV1.h"
#include "AdrenoPowerUpGdsc.h"
#include "AdrenoPowerUpV1.h"

#include <stdint.h>
#include <stddef.h>

// Local aliases so the step indices read the same way as in the header.
#define APD_STEP_GX_OFF   ADRENO_POWER_DOWN_V1_STEP_GX_GENPD_OFF
#define APD_STEP_CLKS     ADRENO_POWER_DOWN_V1_STEP_CLOCKS_DISABLE
#define APD_STEP_CX_OFF   ADRENO_POWER_DOWN_V1_STEP_CX_GENPD_OFF

int AdrenoPowerDownV1ShouldRun(const struct AdrenoPowerDownV1Inputs *in)
{
    if (in == (const struct AdrenoPowerDownV1Inputs *)0) {
        return 0;
    }
    return in->gmu_active != 0u ? 1 : 0;
}

int AdrenoPowerDownV1InputsValid(const struct AdrenoPowerDownV1Inputs *in)
{
    if (in == (const struct AdrenoPowerDownV1Inputs *)0) {
        return 0;
    }
    // Booleans must be 0/1, never "truthy"; the clock count must be real.
    if (in->gxpd_present > 1u || in->gmu_active > 1u) {
        return 0;
    }
    if (in->gmu_clock_count == 0u) {
        return 0;
    }
    return 1;
}

AdrenoPowerDownV1Status AdrenoPowerDownV1StepStatus(uint32_t step)
{
    switch (step) {
    case APD_STEP_GX_OFF:
        return kAdrenoPowerDownV1GxFailed;
    case APD_STEP_CLKS:
        return kAdrenoPowerDownV1ClocksFailed;
    case APD_STEP_CX_OFF:
        return kAdrenoPowerDownV1CxFailed;
    default:
        return kAdrenoPowerDownV1Invalid;
    }
}

uint32_t AdrenoPowerDownV1StepGuarded(uint32_t step)
{
    // Only the GX genpd is conditional upstream (IS_ERR_OR_NULL(gmu->gxpd)).
    return step == APD_STEP_GX_OFF ? 1u : 0u;
}

uint32_t AdrenoPowerDownV1PlanSize(const struct AdrenoPowerDownV1Inputs *in)
{
    if (!AdrenoPowerDownV1InputsValid(in)) {
        return 0u;
    }
    return ADRENO_POWER_DOWN_V1_STEP_COUNT;
}

AdrenoPowerDownV1Status AdrenoPowerDownV1Plan(
    const struct AdrenoPowerDownV1Inputs *in,
    AdrenoPowerDownV1Op *ops, uint32_t capacity, uint32_t *count)
{
    uint32_t n = 0u;

    if (count != (uint32_t *)0) {
        *count = 0u;
    }
    if (!AdrenoPowerDownV1InputsValid(in)) {
        return kAdrenoPowerDownV1Invalid;
    }
    if (ops == (AdrenoPowerDownV1Op *)0) {
        return kAdrenoPowerDownV1Invalid;
    }
    if (capacity < ADRENO_POWER_DOWN_V1_STEP_COUNT) {
        return kAdrenoPowerDownV1Invalid;
    }

    // :1209-1210 GX first. "Make sure the GX domain is off before turning off
    // the GMU (CX) domain."
    ops[n].step = APD_STEP_GX_OFF;
    ops[n].guarded = 1u;
    ops[n].value = 0u;
    n++;

    // :1212 clocks BEFORE CX, per the source order.
    ops[n].step = APD_STEP_CLKS;
    ops[n].guarded = 0u;
    ops[n].value = in->gmu_clock_count;
    n++;

    // :1214 CX last (pm_runtime_put_sync(gmu->dev)).
    ops[n].step = APD_STEP_CX_OFF;
    ops[n].guarded = 0u;
    ops[n].value = 0u;
    n++;

    if (count != (uint32_t *)0) {
        *count = n;
    }
    return kAdrenoPowerDownV1Ok;
}

// One bus operation. Returns 0 on success.
static int apd_dispatch(const AdrenoPowerDownV1Op *op,
                        const struct AdrenoPowerDownV1Bus *bus)
{
    switch (op->step) {
    case APD_STEP_GX_OFF:
        return bus->genpd_off(bus->cookie, 1u);
    case APD_STEP_CX_OFF:
        return bus->genpd_off(bus->cookie, 0u);
    case APD_STEP_CLKS:
        return bus->clocks_disable(bus->cookie, op->value);
    default:
        return 1; // unreachable for a plan built by Plan()
    }
}

static int apd_bus_ok(const struct AdrenoPowerDownV1Bus *bus)
{
    if (bus == (const struct AdrenoPowerDownV1Bus *)0) {
        return 0;
    }
    // Plain pointer tests: comparing a function pointer against (void *)0
    // would be a constraint violation under -Wpedantic.
    if (!bus->genpd_off) {
        return 0;
    }
    if (!bus->clocks_disable) {
        return 0;
    }
    return 1;
}

AdrenoPowerDownV1Status AdrenoPowerDownV1Execute(
    const struct AdrenoPowerDownV1Inputs *in,
    const AdrenoPowerDownV1Op *ops, uint32_t count,
    const struct AdrenoPowerDownV1Bus *bus,
    struct AdrenoGmuCallerV1Pre *pre, uint32_t *fail_index)
{
    uint32_t i;

    if (fail_index != (uint32_t *)0) {
        *fail_index = ADRENO_POWER_DOWN_V1_STEP_COUNT;
    }
    if (!AdrenoPowerDownV1InputsValid(in) || !apd_bus_ok(bus)) {
        return kAdrenoPowerDownV1Invalid;
    }
    if (ops == (const AdrenoPowerDownV1Op *)0 ||
        count != ADRENO_POWER_DOWN_V1_STEP_COUNT) {
        return kAdrenoPowerDownV1Invalid;
    }

    // Clearing *pre is the whole point on the success path: a torn-down GMU
    // must never satisfy caller_v1_pre_ok(). Do it up front so a mid-sequence
    // failure ALSO leaves the caller refusing, which is the fail-closed side.
    if (pre != (struct AdrenoGmuCallerV1Pre *)0) {
        pre->cx_genpd_on = 0u;
        pre->gx_genpd_on = 0u;
        pre->clocks_enabled = 0u;
        pre->require_gx = 0u;
    }

    // :1189-1190 gate: nothing to do when the GMU was never active.
    if (!AdrenoPowerDownV1ShouldRun(in)) {
        return kAdrenoPowerDownV1Ok;
    }

    for (i = 0u; i < count; i++) {
        const AdrenoPowerDownV1Op *op = &ops[i];

        if (op->step >= ADRENO_POWER_DOWN_V1_STEP_COUNT) {
            return kAdrenoPowerDownV1Invalid;
        }
        // Mirror of the power-up contract: a guarded op is SKIPPED (not a
        // failure) when the domain was never acquired.
        if (op->guarded != 0u && in->gxpd_present == 0u) {
            continue;
        }
        if (apd_dispatch(op, bus) != 0) {
            if (fail_index != (uint32_t *)0) {
                *fail_index = i;
            }
            // No rollback by design (see the header): stopping here leaves the
            // GMU partially torn down and *pre cleared, so the caller refuses.
            return AdrenoPowerDownV1StepStatus(op->step);
        }
    }

    if (fail_index != (uint32_t *)0) {
        *fail_index = ADRENO_POWER_DOWN_V1_STEP_COUNT;
    }
    return kAdrenoPowerDownV1Ok;
}

int AdrenoPowerDownV1CxOffIsUnverified(void)
{
    // Reuses the round-58 predicate, which reads the round-57 disable plan.
    // CX disable has no POLL_OFF (VOTABLE short-cut) -> 1.
    return AdrenoPowerUpGdscDisableIsUnverified(0u);
}

int AdrenoPowerDownV1Verify(uint32_t *check_count)
{
    struct AdrenoPowerDownV1Inputs in;
    AdrenoPowerDownV1Op ops[ADRENO_POWER_DOWN_V1_STEP_COUNT];
    struct AdrenoPowerUpV1Inputs pu;
    AdrenoPowerUpV1Op pops[ADRENO_POWER_UP_V1_STEP_COUNT];
    uint32_t pcount = 0u;
    uint32_t count = 0u;
    uint32_t n = 0u;
    int rc = 0;

#define APD_REQUIRE(cond)                                                     \
    do {                                                                      \
        n++;                                                                  \
        if (!(cond)) {                                                        \
            if (rc == 0) {                                                    \
                rc = (int)n;                                                  \
            }                                                                 \
        }                                                                     \
    } while (0)

    in.gxpd_present = 1u;
    in.gmu_active = 1u;
    in.gmu_clock_count = 4u; // vendor DTB gmu clock count

    APD_REQUIRE(ADRENO_POWER_DOWN_V1_STEP_COUNT == 3u);
    APD_REQUIRE(AdrenoPowerDownV1Plan(&in, ops, 3u, &count) ==
                kAdrenoPowerDownV1Ok);
    APD_REQUIRE(count == 3u);

    // 4-6: the derived ORDER. GX before clocks before CX -- this is the whole
    // round-59 finding, so it is asserted position by position.
    APD_REQUIRE(ops[0].step == APD_STEP_GX_OFF);
    APD_REQUIRE(ops[1].step == APD_STEP_CLKS);
    APD_REQUIRE(ops[2].step == APD_STEP_CX_OFF);

    // 7-8: only GX is conditional; the clock op carries the real clock count.
    APD_REQUIRE(ops[0].guarded == 1u && ops[1].guarded == 0u &&
                ops[2].guarded == 0u);
    APD_REQUIRE(ops[1].value == 4u);

    // 9-11: teardown is NOT "power-up backwards". Build the round-51 plan and
    // compare SHAPES. NOTE the two step enums are SEPARATE NAMESPACES whose
    // numbering collides (up CX_GENPD_ON == 0 == down GX_GENPD_OFF), so a
    // cross-namespace `!=` comparison would be a category error; the meaningful
    // assertions are each plan's own order and their differing sizes.
    pu.gxpd_present = 1u;
    pu.gmu_clock_count = 4u;
    pu.gpu_clock_count = 6u;
    pu.a740_family = 0u;
    pu.strict_gx = 0u;
    APD_REQUIRE(AdrenoPowerUpV1Plan(&pu, pops, ADRENO_POWER_UP_V1_STEP_COUNT,
                                    &pcount) == kAdrenoPowerUpV1Ok);
    APD_REQUIRE(pcount == 5u);
    APD_REQUIRE(pops[0].step == ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON &&
                pops[1].step == ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON &&
                pops[ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE].step ==
                    ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE);
    // The clock veto sits BETWEEN GX-off and CX-off; power-up puts clocks last.
    APD_REQUIRE(ops[0].step != ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE);

    // 12-13: guarded bits must agree with reality in BOTH directions.
    APD_REQUIRE(AdrenoPowerDownV1StepGuarded(APD_STEP_GX_OFF) == 1u);
    APD_REQUIRE(AdrenoPowerDownV1StepGuarded(APD_STEP_CLKS) == 0u &&
                AdrenoPowerDownV1StepGuarded(APD_STEP_CX_OFF) == 0u);

    // 14-15: the :1189 gate and input validation are real predicates.
    {
        struct AdrenoPowerDownV1Inputs idle = in;

        idle.gmu_active = 0u;
        APD_REQUIRE(AdrenoPowerDownV1ShouldRun(&in) == 1);
        APD_REQUIRE(AdrenoPowerDownV1ShouldRun(&idle) == 0);
    }

    // 16-17: the derived CX-off-unverified finding still holds, and GX-off is
    // the verifiable one (the two domains are genuinely asymmetric).
    APD_REQUIRE(AdrenoPowerDownV1CxOffIsUnverified() == 1);
    APD_REQUIRE(AdrenoPowerUpGdscDisableIsUnverified(1u) == 0);

    // 18: the underlying GDSC contract this module composes with is intact.
    APD_REQUIRE(AdrenoPowerUpGdscGuardMatchesWrites() == 1);

#undef APD_REQUIRE

    if (check_count != (uint32_t *)0) {
        *check_count = n;
    }
    return rc;
}
