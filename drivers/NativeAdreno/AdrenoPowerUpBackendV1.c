// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoPowerUpBackendV1.c -- the EXECUTOR for the A6XX_GEN1 power-up chain.
// Freestanding pure C11: no libc, no MMIO of its own, no constructors.
//
// Every register access goes through the caller-supplied
// AdrenoPowerUpBackendV1Mmio triple. Nothing here knows what a window is; the
// caller owns mapping and lifetime. See the header for the honest boundary.
//
// The four AdrenoPowerUpV1Bus callbacks are the whole point of this module:
// before it existed, AdrenoPowerUpV1Bus was a shape any no-op could satisfy.

#include "AdrenoPowerUpBackendV1.h"

#include "AdrenoDomainState.h"
#include "AdrenoPowerUpGdsc.h"

#include <stdint.h>
#include <stddef.h>

/* ---- MMIO wrappers: count, gate, fail closed ------------------------------ */

static int apb_read(struct AdrenoPowerUpBackendV1 *be, uint32_t off, uint32_t *out)
{
    if (be->in.armed != 1u) {
        be->stats.refused++;
        return 1;
    }
    if (be->mmio->read32(be->mmio->cookie, off, out) != 0) {
        return 1;
    }
    be->stats.reads++;
    return 0;
}

static int apb_write(struct AdrenoPowerUpBackendV1 *be, uint32_t off, uint32_t value)
{
    if (be->in.armed != 1u) {
        be->stats.refused++;
        return 1;
    }
    if (be->mmio->write32(be->mmio->cookie, off, value) != 0) {
        return 1;
    }
    be->stats.writes++;
    return 0;
}

static int apb_delay(struct AdrenoPowerUpBackendV1 *be, uint32_t usec)
{
    be->stats.delays++;
    return be->mmio->udelay(be->mmio->cookie, usec);
}

/* read-modify-write against a byte offset, as regmap_update_bits() does:
 * (old & ~mask) | (value & mask). One READ then one WRITE. */
static int apb_update_bits(struct AdrenoPowerUpBackendV1 *be, uint32_t off,
                           uint32_t mask, uint32_t value)
{
    uint32_t old;

    if (apb_read(be, off, &old) != 0) {
        return 1;
    }
    return apb_write(be, off, (old & ~mask) | (value & mask));
}

/* Poll until (word & mask) == 0 or (word & mask) == mask, bounded.
 * gdsc.c and clk-rcg2.c both poll with udelay(1) in a bounded loop; the bound
 * is the same 500/200 the plans carry. */
static int apb_poll(struct AdrenoPowerUpBackendV1 *be, uint32_t off,
                    uint32_t mask, uint32_t want_set, uint32_t budget_us)
{
    uint32_t i;

    for (i = 0u; i <= budget_us; i++) {
        uint32_t v;

        if (apb_read(be, off, &v) != 0) {
            return 1;
        }
        be->stats.polls++;
        if (want_set != 0u) {
            if ((v & mask) == mask) {
                return 0;
            }
        } else if ((v & mask) == 0u) {
            return 0;
        }
        if (i == budget_us) {
            break;
        }
        if (apb_delay(be, 1u) != 0) {
            return 1;
        }
    }
    return 1;   /* timeout */
}

/* clk_branch2_check_halt() when enabling is a DISJUNCTION:
 * (val & CBCR_CLK_OFF) == 0  ||  (val & NOC_FSM_STATUS) == FSM_STATUS_ON.
 * The round-61 module gave it its own step kind; this is the executor side. */
static int apb_poll_branch2(struct AdrenoPowerUpBackendV1 *be, uint32_t off,
                            uint32_t clk_off_mask, uint32_t fsm_on, uint32_t budget_us)
{
    uint32_t i;

    for (i = 0u; i <= budget_us; i++) {
        uint32_t v;

        if (apb_read(be, off, &v) != 0) {
            return 1;
        }
        be->stats.polls++;
        if ((v & clk_off_mask) == 0u || (v & fsm_on) == fsm_on) {
            return 0;
        }
        if (i == budget_us) {
            break;
        }
        if (apb_delay(be, 1u) != 0) {
            return 1;
        }
    }
    return 1;
}

/* ---- the round-57 GDSC plan executor ------------------------------------- */

/* Execute one adreno_gdsc_plan through the backend. Steps are the exact
 * gdsc_enable()/gdsc_disable() list produced by AdrenoGdscDecode; this function
 * adds no step of its own and skips none. */
static int apb_run_gdsc_plan(struct AdrenoPowerUpBackendV1 *be,
                             const struct adreno_gdsc_plan *plan)
{
    uint32_t i;

    for (i = 0u; i < plan->count; i++) {
        const struct adreno_gdsc_step *s = &plan->steps[i];

        switch (s->kind) {
        case ADRENO_GDSC_STEP_REGULATOR_ENABLE:
        case ADRENO_GDSC_STEP_REGULATOR_DISABLE:
            /* A regulator vote is NOT an MMIO write and this module owns no
             * regulator handle. gpu_cx_gdsc/gpu_gx_gdsc have no .supply
             * (gpucc-sdm845.c:119-139), so on this board these steps never
             * appear; if one ever did, refusing is the honest response. */
            return 1;

        case ADRENO_GDSC_STEP_UPDATE_COLLAPSE:
        case ADRENO_GDSC_STEP_ASSERT_RESET:
        case ADRENO_GDSC_STEP_DEASSERT_RESET:
        case ADRENO_GDSC_STEP_ASSERT_RESET_AON:
        case ADRENO_GDSC_STEP_DEASSERT_CLAMP_IO:
        case ADRENO_GDSC_STEP_ASSERT_CLAMP_IO:
        case ADRENO_GDSC_STEP_HWCTRL_ON:
        case ADRENO_GDSC_STEP_HWCTRL_OFF:
            if (s->writes != 0u) {
                if (apb_update_bits(be, s->offset, s->mask, s->value) != 0) {
                    return 1;
                }
            }
            break;

        case ADRENO_GDSC_STEP_FORCE_MEM_ON:
        case ADRENO_GDSC_STEP_CLEAR_MEM_ON:
        case ADRENO_GDSC_STEP_RETAIN_FF_ON:
            /* Retention forces write a mask/value the plan did not fill for
             * this descriptor (cxc_count == 0 on both GPUCC domains). */
            return 1;

        case ADRENO_GDSC_STEP_UDELAY:
            if (apb_delay(be, s->delay_us != 0u ? s->delay_us : 1u) != 0) {
                return 1;
            }
            break;

        case ADRENO_GDSC_STEP_POLL_ON:
            /* GDSC_POWER_UP_COMPLETE (BIT 16) or PWR_ON_MASK (BIT 31): the
             * plan carries the mask it derived. */
            if (apb_poll(be, s->offset, s->mask, 1u, 1500u) != 0) {
                return 1;
            }
            break;

        case ADRENO_GDSC_STEP_POLL_OFF:
            /* mask == 0 means the descriptor has no dedicated down bit and the
             * VOTABLE short-cut already skipped polling; the plan emits such a
             * step only for POLL_CFG_GDSCR domains, where off_mask is BIT(15). */
            if (s->mask == 0u) {
                break;
            }
            if (apb_poll(be, s->offset, s->mask, 1u, 1500u) != 0) {
                return 1;
            }
            break;

        case ADRENO_GDSC_STEP_NONE:
        default:
            return 1;   /* an unknown step is a derivation bug: fail closed */
        }
    }
    return 0;
}

/* ---- the round-58 gated genpd legs --------------------------------------- */

/* What a genpd callback may do, derived (round 58): the guarded bit of the
 * power-up step equals "the CPU writes no GDSC register for this domain". */
static uint32_t apb_genpd_may_write(uint32_t which)
{
    return AdrenoPowerUpV1StepGuarded(
               which == 0u ? ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON
                           : ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON) ? 0u : 1u;
}

static int apb_genpd_on(struct AdrenoPowerUpBackendV1 *be, uint32_t which)
{
    struct adreno_gdsc_descriptor desc;
    struct adreno_gdsc_plan plan;

    if (which > 1u) {
        return 1;
    }
    desc = (which == 0u) ? adreno_gdsc_cx_descriptor()
                         : adreno_gdsc_gx_descriptor();

    /* genpd calls sc->pd.power_on: gdsc_enable() for CX, and
     * gdsc_gx_do_nothing_enable() for GX (which writes nothing). */
    if (adreno_gdsc_plan_cpu_enable(&desc, &plan) == 0) {
        return 1;
    }
    if (apb_genpd_may_write(which) == 0u && plan.register_writes != 0u) {
        /* The round-58 invariant is violated: refuse rather than write on a
         * domain the plan says the CPU cannot act on. */
        return 1;
    }
    if (apb_run_gdsc_plan(be, &plan) != 0) {
        return 1;
    }
    if (which == 0u) {
        be->cx_on = 1u;
    } else {
        be->gx_on = 1u;
    }
    return 0;
}

static int apb_genpd_off(struct AdrenoPowerUpBackendV1 *be, uint32_t which)
{
    struct adreno_gdsc_descriptor desc;
    struct adreno_gdsc_plan plan;

    if (which > 1u) {
        return 1;
    }
    desc = (which == 0u) ? adreno_gdsc_cx_descriptor()
                         : adreno_gdsc_gx_descriptor();
    if (adreno_gdsc_plan_disable(&desc, &plan) == 0) {
        return 1;
    }
    if (apb_run_gdsc_plan(be, &plan) != 0) {
        return 1;
    }
    /* CX-off is NOT verifiable by the CPU (round 59: VOTABLE polls nothing), so
     * the flag records "the put was issued", never "the domain is quiet". */
    if (which == 0u) {
        be->cx_on = 0u;
    } else {
        be->gx_on = 0u;
    }
    return 0;
}

/* ---- the round-61 clock legs -------------------------------------------- */

/* Execute one AdrenoClkCtlV1 plan. PARK_CFG is the only kind with no MMIO and
 * it is handled by the caller, which is where the parked word is stored. */
static int apb_run_clk_plan(struct AdrenoPowerUpBackendV1 *be,
                           const struct adreno_clk_ctl_plan *plan)
{
    uint32_t i;

    for (i = 0u; i < plan->count; i++) {
        const struct adreno_clk_ctl_step *s = &plan->steps[i];

        switch (s->kind) {
        case ADRENO_CLK_CTL_STEP_REG_WRITE:
            if (s->mask == 0xffffffffu) {
                if (apb_write(be, s->offset, s->value) != 0) {
                    return 1;
                }
            } else if (apb_update_bits(be, s->offset, s->mask, s->value) != 0) {
                return 1;
            }
            break;

        case ADRENO_CLK_CTL_STEP_REG_UPDATE_BITS:
            if (apb_update_bits(be, s->offset, s->mask, s->value) != 0) {
                return 1;
            }
            break;

        case ADRENO_CLK_CTL_STEP_POLL_CLEAR:
            if (apb_poll(be, s->offset, s->mask, 0u,
                         s->timeout_us != 0u ? s->timeout_us : 500u) != 0) {
                return 1;
            }
            break;

        case ADRENO_CLK_CTL_STEP_POLL_SET:
            if (apb_poll(be, s->offset, s->mask, 1u,
                         s->timeout_us != 0u ? s->timeout_us : 500u) != 0) {
                return 1;
            }
            break;

        case ADRENO_CLK_CTL_STEP_POLL_BRANCH2_ON:
            if (apb_poll_branch2(be, s->offset, s->mask, s->value,
                                 s->timeout_us != 0u ? s->timeout_us : 200u) != 0) {
                return 1;
            }
            break;

        case ADRENO_CLK_CTL_STEP_PARK_CFG:
            /* No MMIO by construction; the value is the parked word the caller
             * already captured. Reaching here from a plan other than
             * PlanSetCoreRate would be a derivation bug. */
            return 1;

        case ADRENO_CLK_CTL_STEP_NONE:
        default:
            return 1;
        }
    }
    return 0;
}

static int apb_set_rate(struct AdrenoPowerUpBackendV1 *be, uint32_t which, uint32_t hz)
{
    struct adreno_clk_ctl_plan plan;

    if (which == 0u) {
        /* The core clock. Round 61: while the RCG is disabled this is the
         * PARKED path and performs ZERO MMIO -- we record the word and do not
         * synthesise traffic upstream would not make. */
        if (AdrenoClkCtlV1PlanSetCoreRate(hz, (int)be->in.rcg_enabled, &plan) != 0) {
            return 1;
        }
        if (plan.count == 1u &&
            plan.steps[0].kind == (uint8_t)ADRENO_CLK_CTL_STEP_PARK_CFG) {
            be->parked_cfg = plan.steps[0].value;
            return 0;
        }
        /* RCG already running: the plan is real register work, and the word it
         * writes is the same word ClocksEnable writes back, so recover it from
         * the plan rather than recomputing it a second way. */
        if (apb_run_clk_plan(be, &plan) != 0) {
            return 1;
        }
        if (plan.count > 2u &&
            plan.steps[2].kind == (uint8_t)ADRENO_CLK_CTL_STEP_REG_WRITE) {
            be->parked_cfg = plan.steps[2].value;
        } else {
            return 1;
        }
        return 0;
    }

    /* The hub clock. Round 61 finding 1: there is no "hub"/"hub_clk" id on
     * SDM845, so upstream's clk_set_rate() gets NULL and returns success
     * without touching a register. We do exactly that, and say so. */
    if (AdrenoClkCtlV1HubStepWritesNothing() == 1) {
        return 0;
    }
    return 1;
}

static int apb_clocks_enable(struct AdrenoPowerUpBackendV1 *be, uint32_t count)
{
    struct adreno_clk_ctl_plan plan;
    uint32_t i;

    /* The bulk count is 4 on this board but only 2 of them are GPUCC registers
     * (round 61 finding 3). Refuse a count that does not match what we model:
     * silently enabling "4" would claim to own the two GCC clocks. */
    if (count != AdrenoClkCtlV1GmuClockCount()) {
        return 1;
    }
    if (be->parked_cfg == 0u) {
        /* Nothing was parked, which means set_rate never ran. The RCG would be
         * configured with a word we never derived: fail closed. */
        return 1;
    }

    /* RCG enable leg: force_enable, write parked CFG, CMD_UPDATE, clear. */
    if (AdrenoClkCtlV1PlanRcgEnable(be->parked_cfg, &plan) != 0) {
        return 1;
    }
    if (apb_run_clk_plan(be, &plan) != 0) {
        return 1;
    }

    /* The GPUCC-subset branch clocks, in the bulk order: gmu then cxo. */
    for (i = 0u; i < AdrenoClkCtlV1GpuccClockCount(); i++) {
        const uint32_t cbcr = (i == 0u) ? ADRENO_CLK_CTL_V1_CX_GMU_CBCR
                                        : ADRENO_CLK_CTL_V1_CXO_CBCR;
        if (AdrenoClkCtlV1PlanBranchEnable(cbcr, &plan) != 0) {
            return 1;
        }
        if (apb_run_clk_plan(be, &plan) != 0) {
            return 1;
        }
    }
    be->clocks_on = 1u;
    return 0;
}

/* ---- bus thunks (C linkage, cookie -> backend) --------------------------- */

static int apb_thunk_genpd_on(void *cookie, uint32_t which)
{
    return apb_genpd_on((struct AdrenoPowerUpBackendV1 *)cookie, which);
}

static int apb_thunk_genpd_off(void *cookie, uint32_t which)
{
    return apb_genpd_off((struct AdrenoPowerUpBackendV1 *)cookie, which);
}

static int apb_thunk_set_rate(void *cookie, uint32_t which, uint32_t hz)
{
    return apb_set_rate((struct AdrenoPowerUpBackendV1 *)cookie, which, hz);
}

static int apb_thunk_clocks_enable(void *cookie, uint32_t count)
{
    return apb_clocks_enable((struct AdrenoPowerUpBackendV1 *)cookie, count);
}

/* ---- public API --------------------------------------------------------- */

int AdrenoPowerUpBackendV1Make(struct AdrenoPowerUpBackendV1 *be,
                               const struct AdrenoPowerUpBackendV1Mmio *mmio,
                               const struct AdrenoPowerUpBackendV1Inputs *in)
{
    if (be == (struct AdrenoPowerUpBackendV1 *)0 ||
        mmio == (const struct AdrenoPowerUpBackendV1Mmio *)0 ||
        in == (const struct AdrenoPowerUpBackendV1Inputs *)0) {
        return 1;
    }
    if (mmio->read32 == 0 || mmio->write32 == 0 || mmio->udelay == 0) {
        return 1;
    }
    /* Fail-closed: an unarmed backend is not built at all. A constructed
     * backend can therefore be assumed to be one the caller meant to run. */
    if (in->armed != 1u) {
        return 1;
    }
    if (in->gxpd_present > 1u || in->rcg_enabled > 1u || in->strict_gx > 1u) {
        return 1;
    }

    be->in = *in;
    be->mmio = mmio;
    be->bus.cookie = be;
    be->bus.genpd_on = apb_thunk_genpd_on;
    be->bus.genpd_off = apb_thunk_genpd_off;
    be->bus.set_rate = apb_thunk_set_rate;
    be->bus.clocks_enable = apb_thunk_clocks_enable;
    be->parked_cfg = 0u;
    be->cx_on = 0u;
    be->gx_on = 0u;
    be->clocks_on = 0u;
    be->stats.reads = 0u;
    be->stats.writes = 0u;
    be->stats.delays = 0u;
    be->stats.polls = 0u;
    be->stats.refused = 0u;
    return 0;
}

AdrenoPowerUpV1Status AdrenoPowerUpBackendV1Up(
    struct AdrenoPowerUpBackendV1 *be,
    struct AdrenoGmuCallerV1Pre *pre,
    uint32_t *fail_index)
{
    struct AdrenoPowerUpV1Inputs in;
    AdrenoPowerUpV1Op ops[ADRENO_POWER_UP_V1_STEP_COUNT];
    uint32_t n = 0u;
    AdrenoPowerUpV1Status rc;

    if (be == (struct AdrenoPowerUpBackendV1 *)0 ||
        pre == (struct AdrenoGmuCallerV1Pre *)0) {
        return kAdrenoPowerUpV1Invalid;
    }

    /* The plan inputs come from the backend's own board facts, except the two
     * counts which round 61 derives. */
    in.gxpd_present = be->in.gxpd_present;
    in.gmu_clock_count = AdrenoClkCtlV1GmuClockCount();
    in.gpu_clock_count = AdrenoPowerUpV1BoardGpuClockCount();
    in.a740_family = AdrenoPowerUpV1BoardIsA740Family();
    in.strict_gx = be->in.strict_gx;

    if (AdrenoPowerUpV1Plan(&in, ops, ADRENO_POWER_UP_V1_STEP_COUNT, &n) !=
        kAdrenoPowerUpV1Ok) {
        return kAdrenoPowerUpV1Invalid;
    }
    /* Force the derived core rate into the plan the model emitted, so the two
     * cannot disagree about what this backend programs. */
    ops[ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE].value = be->in.core_rate_hz;

    rc = AdrenoPowerUpV1Execute(&in, ops, n, &be->bus, pre, fail_index);
    if (rc != kAdrenoPowerUpV1Ok) {
        return rc;
    }
    /* Execute() clears *pre on failure; on success it set the acquired flags.
     * Fold in what the backend itself observed. */
    pre->cx_genpd_on = be->cx_on;
    pre->clocks_enabled = be->clocks_on;
    return rc;
}

/* ---- self-check --------------------------------------------------------- */

int AdrenoPowerUpBackendV1Verify(uint32_t *check_count)
{
    uint32_t checks = 0u;

#define APB_REQUIRE(cond)                                                   \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            if (check_count != (uint32_t *)0) { *check_count = checks; }    \
            return (int)checks;                                             \
        }                                                                   \
    } while (0)

    /* 1-2: the two derived board facts this module delegates to. */
    APB_REQUIRE(AdrenoClkCtlV1GmuClockCount() == 4u);
    APB_REQUIRE(AdrenoClkCtlV1GpuccClockCount() == 2u);

    /* 3: the hub leg is the round-61 no-op, so set_rate(1,...) cannot fail. */
    APB_REQUIRE(AdrenoClkCtlV1HubStepWritesNothing() == 1);

    /* 4-5: rounds 58 and 61 still agree about which domains the CPU writes. */
    APB_REQUIRE(AdrenoPowerUpGdscGuardMatchesWrites() == 1);
    APB_REQUIRE(AdrenoClkCtlV1Verify((uint32_t *)0) == 0);

    /* 6-7: the parked core word the executor will write back is the derived one. */
    {
        struct adreno_clk_ctl_plan plan;
        if (AdrenoClkCtlV1PlanSetCoreRate(200000000u, 0, &plan) == 0 &&
            plan.count == 1u &&
            plan.steps[0].kind == (uint8_t)ADRENO_CLK_CTL_STEP_PARK_CFG) {
            APB_REQUIRE(plan.steps[0].value == 0x0602u);
            APB_REQUIRE(plan.register_writes == 0u);
        } else {
            APB_REQUIRE(0);
        }
    }

    /* 8: the RCG enable leg the executor runs still validates that word. */
    APB_REQUIRE(AdrenoClkCtlV1PlanRcgEnable(0x0602u, (struct adreno_clk_ctl_plan *)0) != 0);

    if (check_count != (uint32_t *)0) {
        *check_count = checks;
    }
    return 0;
#undef APB_REQUIRE
}
