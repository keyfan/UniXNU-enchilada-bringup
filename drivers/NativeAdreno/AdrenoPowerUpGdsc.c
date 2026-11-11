// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoPowerUpGdsc.c -- see AdrenoPowerUpGdsc.h for the contract and the two
// derived findings. Freestanding pure C11: no libc, no MMIO, no constructors.
//
// Everything below is computed from AdrenoGdscDecode (round 57) and
// AdrenoPowerUpV1 (round 51) at call time. There is deliberately NOT ONE
// hard-coded write count, poll offset or guarded bit in this file: if an
// upstream pin changes and the derived plans change, this module changes with
// them, and if it stops agreeing with AdrenoPowerUpV1 the verify function fails.
//
// RETURN CONVENTION: the round-57 planners return 1 on success and 0 on
// failure (AdrenoGdscDecode.h:225-238). The helpers below use 0 = success so
// that they read like C error codes; every call site converts explicitly.

#include "AdrenoPowerUpGdsc.h"

#include "AdrenoDomainState.h"
#include "AdrenoPowerUpV1.h"

#include <stdint.h>
#include <stddef.h>

// Local aliases so the step indices read the same way as in AdrenoPowerUpV1.h.
#define APG_STEP_CX_GENPD_ON   ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON
#define APG_STEP_GX_GENPD_ON   ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON

// The descriptor accessors return by value, so bind a local copy.
// (Taking the address of a temporary is rejected by -Werror; see round 56.)
static int apg_descriptor(uint32_t which, struct adreno_gdsc_descriptor *out)
{
    if (which == 0u) {
        *out = adreno_gdsc_cx_descriptor();
        return 0;
    }
    if (which == 1u) {
        *out = adreno_gdsc_gx_descriptor();
        return 0;
    }
    // Fail closed: an unknown domain index must NOT silently alias GX, or a
    // caller could ask about "domain 7" and be told the GX answer.
    return 1;
}

// Which domain a power-up step drives. Returns 0 for steps that are not GDSC
// operations at all (the core/hub rate steps and the clock enable step).
static int apg_step_domain(uint32_t step, uint32_t *which)
{
    if (step == APG_STEP_CX_GENPD_ON) {
        if (which != (uint32_t *)0) {
            *which = 0u;
        }
        return 1;
    }
    if (step == APG_STEP_GX_GENPD_ON) {
        if (which != (uint32_t *)0) {
            *which = 1u;
        }
        return 1;
    }
    return 0;
}

// Builds the CPU-path ENABLE plan for a domain. Returns 0 on success.
static int apg_enable_plan(uint32_t which, struct adreno_gdsc_plan *plan)
{
    struct adreno_gdsc_descriptor desc;

    if (plan == (struct adreno_gdsc_plan *)0) {
        return 1;
    }
    if (apg_descriptor(which, &desc) != 0) {
        return 1;
    }
    return adreno_gdsc_plan_cpu_enable(&desc, plan) != 0 ? 0 : 1;
}

// Builds the CPU-path DISABLE plan for a domain. Returns 0 on success.
static int apg_disable_plan(uint32_t which, struct adreno_gdsc_plan *plan)
{
    struct adreno_gdsc_descriptor desc;

    if (plan == (struct adreno_gdsc_plan *)0) {
        return 1;
    }
    if (apg_descriptor(which, &desc) != 0) {
        return 1;
    }
    return adreno_gdsc_plan_disable(&desc, plan) != 0 ? 0 : 1;
}

// Finds the first step of a kind. Returns 1 when found.
static int apg_find(const struct adreno_gdsc_plan *plan, uint8_t kind,
                    uint32_t *offset, uint32_t *mask)
{
    int idx = adreno_gdsc_plan_index_of(plan, kind);

    if (idx < 0) {
        return 0;
    }
    if (offset != (uint32_t *)0) {
        *offset = plan->steps[idx].offset;
    }
    if (mask != (uint32_t *)0) {
        *mask = plan->steps[idx].mask;
    }
    return 1;
}

static uint32_t apg_count_polls(const struct adreno_gdsc_plan *plan)
{
    uint32_t i;
    uint32_t n = 0u;

    for (i = 0u; i < plan->count; i++) {
        uint8_t kind = plan->steps[i].kind;

        if (kind == (uint8_t)ADRENO_GDSC_STEP_POLL_ON ||
            kind == (uint8_t)ADRENO_GDSC_STEP_POLL_OFF) {
            n++;
        }
    }
    return n;
}

int AdrenoPowerUpGdscContract(struct adreno_power_up_gdsc_contract *out)
{
    struct adreno_gdsc_plan cxOn;
    struct adreno_gdsc_plan gxOn;
    struct adreno_gdsc_plan cxOff;
    struct adreno_gdsc_plan gxOff;
    uint32_t off = 0u;
    uint32_t mask = 0u;

    if (out == (struct adreno_power_up_gdsc_contract *)0) {
        return 1;
    }
    if (apg_enable_plan(0u, &cxOn) != 0 || apg_enable_plan(1u, &gxOn) != 0 ||
        apg_disable_plan(0u, &cxOff) != 0 || apg_disable_plan(1u, &gxOff) != 0) {
        return 1;
    }

    out->cx_enable_writes = cxOn.register_writes;
    out->gx_enable_writes = gxOn.register_writes;
    out->cx_enable_steps = cxOn.count;
    out->gx_enable_steps = gxOn.count;
    out->cx_disable_writes = cxOff.register_writes;
    out->gx_disable_writes = gxOff.register_writes;
    out->cx_disable_polls = apg_count_polls(&cxOff);
    out->gx_disable_polls = apg_count_polls(&gxOff);
    // A CX enable plan without a POLL_ON would mean the derivation no longer
    // matches gdsc.c:163-177; leave the fields zero and let Verify() report it.
    if (apg_find(&cxOn, (uint8_t)ADRENO_GDSC_STEP_POLL_ON, &off, &mask) != 0) {
        out->cx_poll_offset = off;
        out->cx_poll_mask = mask;
    } else {
        out->cx_poll_offset = 0u;
        out->cx_poll_mask = 0u;
    }
    out->cx_step_guarded = AdrenoPowerUpV1StepGuarded(APG_STEP_CX_GENPD_ON);
    out->gx_step_guarded = AdrenoPowerUpV1StepGuarded(APG_STEP_GX_GENPD_ON);
    return 0;
}

uint32_t AdrenoPowerUpGdscStepWrites(uint32_t step)
{
    struct adreno_gdsc_plan plan;
    uint32_t which = 0u;

    if (!apg_step_domain(step, &which)) {
        return 0u;
    }
    if (apg_enable_plan(which, &plan) != 0) {
        return 0u;
    }
    return plan.register_writes;
}

int AdrenoPowerUpGdscGuardMatchesWrites(void)
{
    uint32_t cx_writes = AdrenoPowerUpGdscStepWrites(APG_STEP_CX_GENPD_ON);
    uint32_t gx_writes = AdrenoPowerUpGdscStepWrites(APG_STEP_GX_GENPD_ON);
    uint32_t cx_guarded = AdrenoPowerUpV1StepGuarded(APG_STEP_CX_GENPD_ON);
    uint32_t gx_guarded = AdrenoPowerUpV1StepGuarded(APG_STEP_GX_GENPD_ON);

    // The invariant: guarded <=> the CPU writes nothing for that domain.
    if ((cx_guarded != 0u) != (cx_writes == 0u)) {
        return 0;
    }
    if ((gx_guarded != 0u) != (gx_writes == 0u)) {
        return 0;
    }
    return 1;
}

int AdrenoPowerUpGdscDisableIsUnverified(uint32_t which)
{
    struct adreno_gdsc_plan plan;

    if (apg_disable_plan(which, &plan) != 0) {
        return 0;
    }
    return apg_count_polls(&plan) == 0u ? 1 : 0;
}

int AdrenoPowerUpGdscVerify(uint32_t *check_count)
{
    struct adreno_power_up_gdsc_contract c;
    uint32_t n = 0u;
    int rc = 0;

#define APG_REQUIRE(cond)                                                     \
    do {                                                                      \
        n++;                                                                  \
        if (!(cond)) {                                                        \
            if (rc == 0) {                                                    \
                rc = (int)n;                                                  \
            }                                                                 \
        }                                                                     \
    } while (0)

    if (AdrenoPowerUpGdscContract(&c) != 0) {
        if (check_count != (uint32_t *)0) {
            *check_count = 1u;
        }
        return 1;
    }

    // 1-3: the derived contract agrees with the pinned descriptors.
    // CX performs exactly ONE register WRITE (clearing the collapse bit); the
    // status read that follows is a POLL, not a write.
    APG_REQUIRE(c.cx_enable_writes == 1u);
    APG_REQUIRE(c.gx_enable_writes == 0u);
    APG_REQUIRE(c.cx_poll_offset ==
                (uint32_t)(ADRENO_DOMAIN_CX_STATUS - ADRENO_DOMAIN_GPUCC_BASE));

    // 4-5: the poll mask is the derived PWR_ON bit, not a literal here.
    APG_REQUIRE(c.cx_poll_mask == (1u << ADRENO_DOMAIN_CX_ON_BIT));
    APG_REQUIRE(c.cx_enable_steps > c.cx_enable_writes);

    // 6-8: guarded bits must agree with the write counts (the round-58
    // invariant), in BOTH directions.
    APG_REQUIRE(c.cx_step_guarded == 0u);
    APG_REQUIRE(c.gx_step_guarded == 1u);
    APG_REQUIRE(AdrenoPowerUpGdscGuardMatchesWrites() == 1);

    // 9-11: the VOTABLE short-cut is visible, and only on the CX side.
    APG_REQUIRE(c.cx_disable_polls == 0u);
    APG_REQUIRE(AdrenoPowerUpGdscDisableIsUnverified(0u) == 1);
    APG_REQUIRE(AdrenoPowerUpGdscDisableIsUnverified(1u) == 0);

    // 12: GX disable DOES write (the fail-safe the GMU relies on) and DOES
    // poll its derived down-complete bit -- the opposite of CX on both counts.
    APG_REQUIRE(c.gx_disable_writes >= 1u);
    APG_REQUIRE(c.gx_disable_polls >= 1u);

    // 13-14: non-GDSC steps never claim to write a GDSC register.
    APG_REQUIRE(AdrenoPowerUpGdscStepWrites(
                    ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE) == 0u);
    APG_REQUIRE(AdrenoPowerUpGdscStepWrites(
                    ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE) == 0u);

    // 15: the domain-state constants this contract leans on are still derived.
    APG_REQUIRE(adreno_gdsc_verify_domain_state_constants() == 1);

    // 16: the round-57 zero-write property still holds.
    APG_REQUIRE(adreno_gdsc_verify_gx_enable_writes_nothing() == 1);

#undef APG_REQUIRE

    if (check_count != (uint32_t *)0) {
        *check_count = n;
    }
    return rc;
}
