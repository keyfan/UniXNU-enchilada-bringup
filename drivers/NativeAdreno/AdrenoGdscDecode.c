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

struct adreno_gdsc_descriptor adreno_gdsc_cx_descriptor(void)
{
    struct adreno_gdsc_descriptor desc;

    desc.gdscr = ADRENO_GDSC_CX_GDSCR;
    desc.gds_hw_ctrl = ADRENO_GDSC_CX_GDS_HW_CTRL;
    desc.flags = (uint16_t)(ADRENO_GDSC_FLAG_VOTABLE);
    desc.power_on_is_noop = 0u;
    desc.reserved = 0u;
    return desc;
}

struct adreno_gdsc_descriptor adreno_gdsc_gx_descriptor(void)
{
    struct adreno_gdsc_descriptor desc;

    desc.gdscr = ADRENO_GDSC_GX_GDSCR;
    desc.gds_hw_ctrl = 0u;
    desc.flags = (uint16_t)(ADRENO_GDSC_FLAG_CLAMP_IO |
                            ADRENO_GDSC_FLAG_AON_RESET |
                            ADRENO_GDSC_FLAG_POLL_CFG_GDSCR);
    /* gdsc.c:604 gdsc_gx_do_nothing_enable(): the CPU never powers GX on. */
    desc.power_on_is_noop = 1u;
    desc.reserved = 0u;
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
