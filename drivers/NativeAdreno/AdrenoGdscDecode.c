// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * AdrenoGdscDecode.c - pure derivation of the enchilada GPU GDSC status register
 *                      and masks from a GDSC descriptor (mirrors Linux v6.12
 *                      drivers/clk/qcom/gdsc.c:66-91).
 *
 * No MMIO, no IOKit, no libc: this file must remain compilable and testable on
 * any host, and freestanding for the kernel build.
 */

#include "AdrenoGdscDecode.h"

#include "AdrenoDomainState.h"

/* drivers/clk/qcom/gpucc-sdm845.c:119-128 (pinned, sha256 80c2fbef...):
 *
 *   static struct gdsc gpu_cx_gdsc = {
 *       .gdscr = 0x106c,
 *       .gds_hw_ctrl = 0x1540,
 *       .clk_dis_wait_val = 0x8,
 *       .pwrsts = PWRSTS_OFF_ON,
 *       .flags = VOTABLE,
 *   };
 */
#define ADRENO_GDSC_CX_GDSCR        0x106cu
#define ADRENO_GDSC_CX_GDS_HW_CTRL  0x1540u

/* drivers/clk/qcom/gpucc-sdm845.c:130-139 (pinned):
 *
 *   static struct gdsc gpu_gx_gdsc = {
 *       .gdscr = 0x100c,
 *       .clamp_io_ctrl = 0x1508,
 *       .pd = { .name = "gpu_gx_gdsc",
 *               .power_on = gdsc_gx_do_nothing_enable },
 *       .pwrsts = PWRSTS_OFF_ON,
 *       .flags = CLAMP_IO | AON_RESET | POLL_CFG_GDSCR,
 *   };
 */
#define ADRENO_GDSC_GX_GDSCR        0x100cu
#define ADRENO_GDSC_GX_CLAMP_IO     0x1508u

struct adreno_gdsc_descriptor adreno_gdsc_cx_descriptor(void)
{
    struct adreno_gdsc_descriptor desc;

    desc.gdscr = ADRENO_GDSC_CX_GDSCR;
    desc.gds_hw_ctrl = ADRENO_GDSC_CX_GDS_HW_CTRL;
    desc.clamp_io_ctrl = 0u;
    desc.flags = (uint16_t)(ADRENO_GDSC_FLAG_VOTABLE);
    desc.pwrsts = (uint8_t)(ADRENO_GDSC_PWRSTS_OFF_ON);
    desc.power_on_is_noop = 0u;
    /* gpucc-sdm845.c:119-128 sets no .supply and no .cxcs. */
    desc.has_supply = 0u;
    desc.has_cxcs = 0u;
    return desc;
}

struct adreno_gdsc_descriptor adreno_gdsc_gx_descriptor(void)
{
    struct adreno_gdsc_descriptor desc;

    desc.gdscr = ADRENO_GDSC_GX_GDSCR;
    desc.gds_hw_ctrl = 0u;
    desc.clamp_io_ctrl = ADRENO_GDSC_GX_CLAMP_IO;
    desc.flags = (uint16_t)(ADRENO_GDSC_FLAG_CLAMP_IO |
                            ADRENO_GDSC_FLAG_AON_RESET |
                            ADRENO_GDSC_FLAG_POLL_CFG_GDSCR);
    desc.pwrsts = (uint8_t)(ADRENO_GDSC_PWRSTS_OFF_ON);
    /* gdsc.c:604 gdsc_gx_do_nothing_enable(): the CPU never powers GX on. */
    desc.power_on_is_noop = 1u;
    /* gpucc-sdm845.c:130-139 sets no .supply and no .cxcs. */
    desc.has_supply = 0u;
    desc.has_cxcs = 0u;
    return desc;
}

int adreno_gdsc_polls_cfg_gdscr(const struct adreno_gdsc_descriptor *desc)
{
    if (desc == 0) {
        return 0;
    }
    return (desc->flags & ADRENO_GDSC_FLAG_POLL_CFG_GDSCR) ? 1 : 0;
}

int adreno_gdsc_power_on_is_noop(const struct adreno_gdsc_descriptor *desc)
{
    if (desc == 0) {
        return 0;
    }
    return desc->power_on_is_noop ? 1 : 0;
}

uint32_t adreno_gdsc_status_offset(const struct adreno_gdsc_descriptor *desc)
{
    if (desc == 0) {
        return 0u;
    }
    /* gdsc.c:66-71, in the same preference order. */
    if ((desc->flags & ADRENO_GDSC_FLAG_POLL_CFG_GDSCR) != 0u) {
        return desc->gdscr + ADRENO_GDSC_CFG_GDSCR_OFFSET;
    }
    if (desc->gds_hw_ctrl != 0u) {
        return desc->gds_hw_ctrl;
    }
    return desc->gdscr;
}

int adreno_gdsc_derive(uint64_t gpucc_base, const struct adreno_gdsc_descriptor *desc,
                       struct adreno_gdsc_status *out)
{
    const uint32_t offset = adreno_gdsc_status_offset(desc);
    const int polls = adreno_gdsc_polls_cfg_gdscr(desc);

    if (desc == 0 || out == 0) {
        return 0;
    }

    out->status_offset = offset;
    out->status_address = gpucc_base + (uint64_t)offset;
    if (polls != 0) {
        /* gdsc.c:77-84 (CFG_GDSCR branch). */
        out->on_mask = 1u << ADRENO_GDSC_POWER_UP_COMPLETE_BIT;
        out->off_mask = 1u << ADRENO_GDSC_POWER_DOWN_COMPLETE_BIT;
    } else {
        /* gdsc.c:86-91 (fallback branch): "off" is the inverse of the on bit. */
        out->on_mask = 1u << ADRENO_GDSC_PWR_ON_BIT;
        out->off_mask = ADRENO_GDSC_NO_OFF_MASK;
    }
    out->polls_cfg_gdscr = (uint8_t)(polls != 0 ? 1u : 0u);
    out->reserved[0] = 0u;
    out->reserved[1] = 0u;
    out->reserved[2] = 0u;
    return 1;
}

int adreno_gdsc_is_on(const struct adreno_gdsc_descriptor *desc, uint32_t status_word)
{
    struct adreno_gdsc_status derived;

    if (adreno_gdsc_derive(0ull, desc, &derived) == 0) {
        return 0;
    }
    return (status_word & derived.on_mask) != 0u ? 1 : 0;
}

int adreno_gdsc_is_off(const struct adreno_gdsc_descriptor *desc, uint32_t status_word)
{
    struct adreno_gdsc_status derived;

    if (adreno_gdsc_derive(0ull, desc, &derived) == 0) {
        return 0;
    }
    if (derived.off_mask != ADRENO_GDSC_NO_OFF_MASK) {
        return (status_word & derived.off_mask) != 0u ? 1 : 0;
    }
    /* gdsc.c:90: OFF is NOT(val & PWR_ON_MASK) when there is no CFG_GDSCR. */
    return (status_word & derived.on_mask) == 0u ? 1 : 0;
}

int adreno_gdsc_verify_domain_state_constants(void)
{
    const struct adreno_gdsc_descriptor cx = adreno_gdsc_cx_descriptor();
    const struct adreno_gdsc_descriptor gx = adreno_gdsc_gx_descriptor();
    struct adreno_gdsc_status sx;
    struct adreno_gdsc_status sgx;

    if (adreno_gdsc_derive(ADRENO_DOMAIN_GPUCC_BASE, &cx, &sx) == 0) {
        return 0;
    }
    if (adreno_gdsc_derive(ADRENO_DOMAIN_GPUCC_BASE, &gx, &sgx) == 0) {
        return 0;
    }

    /* CX: status 0x05091540 from .gds_hw_ctrl, ON bit 31, no dedicated OFF bit. */
    if (sx.status_address != ADRENO_DOMAIN_CX_STATUS) {
        return 0;
    }
    if (sx.status_offset != ADRENO_DOMAIN_CX_GDS_HW_CTRL) {
        return 0;
    }
    if (sx.on_mask != (1u << ADRENO_DOMAIN_CX_ON_BIT)) {
        return 0;
    }
    if (sx.off_mask != ADRENO_GDSC_NO_OFF_MASK) {
        return 0;
    }
    if (sx.polls_cfg_gdscr != 0u) {
        return 0;
    }

    /* GX: status 0x05091010 = gdscr + CFG_GDSCR_OFFSET, ON 16 / OFF 15. */
    if (sgx.status_address != ADRENO_DOMAIN_GX_STATUS) {
        return 0;
    }
    if (sgx.status_offset != (ADRENO_DOMAIN_GX_GDSCR - ADRENO_DOMAIN_GPUCC_BASE +
                              (uint64_t)ADRENO_DOMAIN_CFG_GDSCR_OFFSET)) {
        return 0;
    }
    if (sgx.on_mask != (1u << ADRENO_DOMAIN_GX_ON_BIT)) {
        return 0;
    }
    if (sgx.off_mask != (1u << ADRENO_DOMAIN_GX_OFF_BIT)) {
        return 0;
    }
    if (sgx.polls_cfg_gdscr != 1u) {
        return 0;
    }
    if (adreno_gdsc_power_on_is_noop(&gx) != 1) {
        return 0;
    }

    return 1;
}

/* ------------------------------------------------------------------------- *
 * Round 57: encode gdsc_enable() (gdsc.c:259-315) and gdsc_disable()
 * (gdsc.c:317-364) as ordered plans.
 *
 * Every step below is a direct transcription of one statement in those two
 * functions; the conditions are the descriptor fields. Nothing is measured and
 * nothing is executed here -- this is the plan the kernel WOULD run.
 * ------------------------------------------------------------------------- */

static void plan_reset(struct adreno_gdsc_plan *plan, uint32_t gdscr)
{
    plan->gdscr = gdscr;
    plan->count = 0u;
    plan->register_writes = 0u;
}

static int plan_push(struct adreno_gdsc_plan *plan, uint8_t kind, uint8_t writes,
                     uint16_t delay_us, uint32_t offset, uint32_t mask, uint32_t value)
{
    struct adreno_gdsc_step step;

    if (plan->count >= ADRENO_GDSC_MAX_STEPS) {
        return 0;
    }
    step.kind = kind;
    step.writes = writes;
    step.delay_us = delay_us;
    step.offset = offset;
    step.mask = mask;
    step.value = value;
    plan->steps[plan->count] = step;
    plan->count++;
    if (writes != 0u) {
        plan->register_writes++;
    }
    return 1;
}

/* gdsc.enable(): the CX descriptor takes every branch except SW_RESET, HW_CTRL
 * and RETAIN_FF_ENABLE, and it is not PWRSTS_ON. The GX descriptor differs in
 * two ways: CLAMP_IO + AON_RESET are set, and its power_on is the no-op, so the
 * whole enable is skipped by the caller (gdsc.c:604-616). */
int adreno_gdsc_plan_enable(const struct adreno_gdsc_descriptor *desc,
                            struct adreno_gdsc_plan *out)
{
    uint32_t status_offset;
    uint32_t on_mask;

    if (desc == 0 || out == 0) {
        return 0;
    }
    plan_reset(out, desc->gdscr);

    /* gdsc.c:264: if (sc->pwrsts == PWRSTS_ON) return gdsc_deassert_reset(sc); */
    if (desc->pwrsts == ADRENO_GDSC_PWRSTS_ON) {
        return plan_push(out, ADRENO_GDSC_STEP_DEASSERT_RESET, 0u, 0u, 0u, 0u, 0u);
    }

    /* gdsc.c:267-271: SW_RESET. */
    if ((desc->flags & ADRENO_GDSC_FLAG_SW_RESET) != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_ASSERT_RESET, 0u, 0u, 0u, 0u, 0u) == 0) {
            return 0;
        }
        if (plan_push(out, ADRENO_GDSC_STEP_UDELAY, 0u, 1u, 0u, 0u, 0u) == 0) {
            return 0;
        }
        if (plan_push(out, ADRENO_GDSC_STEP_DEASSERT_RESET, 0u, 0u, 0u, 0u, 0u) == 0) {
            return 0;
        }
    }

    /* gdsc.c:273-277: CLAMP_IO (and AON_RESET inside it). */
    if ((desc->flags & ADRENO_GDSC_FLAG_CLAMP_IO) != 0u) {
        if ((desc->flags & ADRENO_GDSC_FLAG_AON_RESET) != 0u) {
            if (plan_push(out, ADRENO_GDSC_STEP_ASSERT_RESET_AON, 1u, 1u,
                          desc->clamp_io_ctrl, ADRENO_GDSC_GMEM_RESET_MASK,
                          ADRENO_GDSC_GMEM_RESET_MASK) == 0) {
                return 0;
            }
        }
        if (plan_push(out, ADRENO_GDSC_STEP_DEASSERT_CLAMP_IO, 1u, 0u,
                      desc->clamp_io_ctrl, ADRENO_GDSC_GMEM_CLAMP_IO_MASK, 0u) == 0) {
            return 0;
        }
    }

    /* gdsc.c:144-147 (inside gdsc_toggle_logic): regulator first, when any. */
    if (desc->has_supply != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_REGULATOR_ENABLE, 0u, 0u, 0u, 0u, 0u) == 0) {
            return 0;
        }
    }

    /* gdsc.c:150: clear the collapse bit, because this is an ENABLE. */
    if (plan_push(out, ADRENO_GDSC_STEP_UPDATE_COLLAPSE, 1u, 0u, desc->gdscr,
                  ADRENO_GDSC_SW_COLLAPSE_MASK, 0u) == 0) {
        return 0;
    }

    /* gdsc.c:163-175: 1 us before polling when a hardware controller is used. */
    if (desc->gds_hw_ctrl != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_UDELAY, 0u, 1u, 0u, 0u, 0u) == 0) {
            return 0;
        }
    }

    /* gdsc.c:177: poll for the ON state (the VOTABLE short-cut applies only to
     * DISABLE, so CX does poll its gds_hw_ctrl status bit here). */
    status_offset = adreno_gdsc_status_offset(desc);
    on_mask = 1u << (adreno_gdsc_polls_cfg_gdscr(desc) != 0
                     ? ADRENO_GDSC_POWER_UP_COMPLETE_BIT : ADRENO_GDSC_PWR_ON_BIT);
    if (plan_push(out, ADRENO_GDSC_STEP_POLL_ON, 0u, 0u, status_offset, on_mask, on_mask) == 0) {
        return 0;
    }

    /* gdsc.c:283-284: retention, only when the domain can be OFF at all. */
    if ((desc->pwrsts & ADRENO_GDSC_PWRSTS_OFF) != 0u && desc->has_cxcs != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_FORCE_MEM_ON, 1u, 0u, 0u, 0u, 0u) == 0) {
            return 0;
        }
    }

    /* gdsc.c:293: the unconditional post-enable settle delay. */
    if (plan_push(out, ADRENO_GDSC_STEP_UDELAY, 0u, 1u, 0u, 0u, 0u) == 0) {
        return 0;
    }

    /* gdsc.c:296-309: hardware trigger mode. */
    if ((desc->flags & ADRENO_GDSC_FLAG_HW_CTRL) != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_HWCTRL_ON, 1u, 1u, desc->gdscr,
                      0x00000002u, 0x00000002u) == 0) {
            return 0;
        }
    }

    /* gdsc.c:311-312: retain the flip-flops. */
    if ((desc->flags & ADRENO_GDSC_FLAG_RETAIN_FF_ENABLE) != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_RETAIN_FF_ON, 1u, 0u, desc->gdscr,
                      0x00000800u, 0x00000800u) == 0) {
            return 0;
        }
    }

    return 1;
}

/* gdsc.disable(): note the VOTABLE short-cut at gdsc.c:153-161. CX is VOTABLE
 * and gdsc_disable() passes domain->synced_poweroff, which is false in the
 * normal case, so CX does NOT poll on the way down -- it just waits 500 us.
 * GX is not VOTABLE and does poll for the down-complete bit. */
int adreno_gdsc_plan_disable(const struct adreno_gdsc_descriptor *desc,
                             struct adreno_gdsc_plan *out)
{
    uint32_t status_offset;
    uint32_t off_mask;

    if (desc == 0 || out == 0) {
        return 0;
    }
    plan_reset(out, desc->gdscr);

    /* gdsc.c:322-323: PWRSTS_ON never collapses. */
    if (desc->pwrsts == ADRENO_GDSC_PWRSTS_ON) {
        return plan_push(out, ADRENO_GDSC_STEP_ASSERT_RESET, 0u, 0u, 0u, 0u, 0u);
    }

    /* gdsc.c:326-341: turning the hardware trigger off, then poll for ON. */
    if ((desc->flags & ADRENO_GDSC_FLAG_HW_CTRL) != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_HWCTRL_OFF, 1u, 1u, desc->gdscr,
                      0x00000002u, 0u) == 0) {
            return 0;
        }
        if (plan_push(out, ADRENO_GDSC_STEP_POLL_ON, 0u, 0u,
                      adreno_gdsc_status_offset(desc),
                      1u << (adreno_gdsc_polls_cfg_gdscr(desc) != 0
                             ? ADRENO_GDSC_POWER_UP_COMPLETE_BIT : ADRENO_GDSC_PWR_ON_BIT),
                      0u) == 0) {
            return 0;
        }
    }

    /* gdsc.c:343-344: drop retention before collapsing. */
    if ((desc->pwrsts & ADRENO_GDSC_PWRSTS_OFF) != 0u && desc->has_cxcs != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_CLEAR_MEM_ON, 1u, 0u, 0u, 0u, 0u) == 0) {
            return 0;
        }
    }

    /* gdsc.c:353-354: PWRSTS_RET_ON stays on from the CPU's point of view. */
    if (desc->pwrsts == ADRENO_GDSC_PWRSTS_RET_ON) {
        return 1;
    }

    /* gdsc.c:144-147: regulator_enable() only happens on the ON path, so the
     * matching disable comes after a successful collapse (gdsc.c:180-184). */
    /* gdsc.c:150: set the collapse bit. */
    if (plan_push(out, ADRENO_GDSC_STEP_UPDATE_COLLAPSE, 1u, 0u, desc->gdscr,
                  ADRENO_GDSC_SW_COLLAPSE_MASK, ADRENO_GDSC_SW_COLLAPSE_MASK) == 0) {
        return 0;
    }

    /* gdsc.c:153-161: VOTABLE, not synced, so wait 500 us and DO NOT poll. */
    if ((desc->flags & ADRENO_GDSC_FLAG_VOTABLE) != 0u) {
        return plan_push(out, ADRENO_GDSC_STEP_UDELAY, 0u,
                         (uint16_t)ADRENO_GDSC_TIMEOUT_US, 0u, 0u, 0u);
    }

    /* gdsc.c:163-175 + :177: gds_hw_ctrl delay, then poll for OFF. */
    if (desc->gds_hw_ctrl != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_UDELAY, 0u, 1u, 0u, 0u, 0u) == 0) {
            return 0;
        }
    }
    status_offset = adreno_gdsc_status_offset(desc);
    off_mask = adreno_gdsc_polls_cfg_gdscr(desc) != 0
               ? (1u << ADRENO_GDSC_POWER_DOWN_COMPLETE_BIT) : 0u;
    if (plan_push(out, ADRENO_GDSC_STEP_POLL_OFF, 0u, 0u, status_offset, off_mask, off_mask) == 0) {
        return 0;
    }

    /* gdsc.c:180-184: drop the regulator vote after the collapse succeeded. */
    if (desc->has_supply != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_REGULATOR_DISABLE, 0u, 0u, 0u, 0u, 0u) == 0) {
            return 0;
        }
    }

    /* gdsc.c:360-361: clamp IO last. */
    if ((desc->flags & ADRENO_GDSC_FLAG_CLAMP_IO) != 0u) {
        if (plan_push(out, ADRENO_GDSC_STEP_ASSERT_CLAMP_IO, 1u, 0u,
                      desc->clamp_io_ctrl, ADRENO_GDSC_GMEM_CLAMP_IO_MASK,
                      ADRENO_GDSC_GMEM_CLAMP_IO_MASK) == 0) {
            return 0;
        }
    }

    return 1;
}

int adreno_gdsc_plan_has(const struct adreno_gdsc_plan *plan, uint8_t kind)
{
    return adreno_gdsc_plan_index_of(plan, kind) >= 0 ? 1 : 0;
}

int adreno_gdsc_plan_index_of(const struct adreno_gdsc_plan *plan, uint8_t kind)
{
    uint32_t i;

    if (plan == 0) {
        return -1;
    }
    for (i = 0u; i < plan->count; i++) {
        if (plan->steps[i].kind == kind) {
            return (int)i;
        }
    }
    return -1;
}

int adreno_gdsc_plan_cpu_enable(const struct adreno_gdsc_descriptor *desc,
                                struct adreno_gdsc_plan *out)
{
    if (desc == 0 || out == 0) {
        return 0;
    }

    /* genpd calls sc->pd.power_on. For GX that is
     * gdsc_gx_do_nothing_enable() (gdsc.c:604-616): enable the parent supply
     * when one exists, then do NOTHING to the GDSC itself. */
    if (desc->power_on_is_noop != 0u) {
        plan_reset(out, desc->gdscr);
        if (desc->has_supply != 0u) {
            return plan_push(out, ADRENO_GDSC_STEP_REGULATOR_ENABLE, 0u, 0u, 0u, 0u, 0u);
        }
        return 1;
    }

    return adreno_gdsc_plan_enable(desc, out);
}

int adreno_gdsc_verify_gx_enable_writes_nothing(void)
{
    const struct adreno_gdsc_descriptor gx = adreno_gdsc_gx_descriptor();
    struct adreno_gdsc_plan cpu_plan;
    struct adreno_gdsc_plan gdsc_plan;

    /* 1. The no-op power_on replaces the entire enable path (gdsc.c:604-616),
     *    and gpucc-sdm845.c gives GX no .supply, so nothing at all is left. */
    if (adreno_gdsc_power_on_is_noop(&gx) != 1) {
        return 0;
    }
    if (adreno_gdsc_plan_cpu_enable(&gx, &cpu_plan) == 0) {
        return 0;
    }
    if (cpu_plan.count != 0u || cpu_plan.register_writes != 0u) {
        return 0;
    }

    /* 2. The generic path, were it ever used for GX, WOULD write registers --
     *    which is exactly why the no-op exists and why this is a real check. */
    if (adreno_gdsc_plan_enable(&gx, &gdsc_plan) == 0) {
        return 0;
    }
    if (gdsc_plan.register_writes == 0u) {
        return 0;
    }

    /* 3. CX, by contrast, must do real work on the way up. */
    {
        const struct adreno_gdsc_descriptor cx = adreno_gdsc_cx_descriptor();
        struct adreno_gdsc_plan cx_plan;

        if (adreno_gdsc_plan_cpu_enable(&cx, &cx_plan) == 0) {
            return 0;
        }
        if (cx_plan.register_writes == 0u) {
            return 0;
        }
        if (adreno_gdsc_plan_has(&cx_plan, ADRENO_GDSC_STEP_POLL_ON) != 1) {
            return 0;
        }
    }

    return 1;
}
