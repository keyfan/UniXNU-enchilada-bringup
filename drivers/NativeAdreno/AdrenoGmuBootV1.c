// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoGmuBootV1.c -- enchilada A630 LEGACY GMU cold-boot bring-up sequence.
// Freestanding pure C11: no libc, no MMIO, no kernel linkage. Every op is
// routed through the caller-supplied AdrenoGmuBootV1Bus. See AdrenoGmuBootV1.h
// for the sha256-pinned upstream provenance and honest boundaries.

#include "AdrenoGmuBootV1.h"

#include <stdint.h>
#include <stddef.h>

/* ---- small helpers ------------------------------------------------------- */

static void gmv1_zero_ops(AdrenoGmuBootV1Op *ops, uint32_t count)
{
    uint32_t i;
    for (i = 0u; i < count; i++) {
        ops[i].domain = 0u;
        ops[i].kind = 0u;
        ops[i].offset = 0u;
        ops[i].value = 0u;
        ops[i].mask = 0u;
        ops[i].bulk_words = 0u;
    }
}

static int gmv1_push(AdrenoGmuBootV1Op *ops, uint32_t capacity,
                     uint32_t *count, uint8_t domain, uint8_t kind,
                     uint32_t offset, uint32_t value, uint32_t mask,
                     uint32_t bulk_words)
{
    if (*count >= capacity) {
        return 0;
    }
    ops[*count].domain = domain;
    ops[*count].kind = kind;
    ops[*count].offset = offset;
    ops[*count].value = value;
    ops[*count].mask = mask;
    ops[*count].bulk_words = bulk_words;
    *count = *count + 1u;
    return 1;
}

/* ---- pure predicates ------------------------------------------------------ */

/* a6xx_gmu.c:211-218. */
void AdrenoGmuBootV1Cm3Expect(uint32_t dtcm_snapshot, uint32_t *mask,
                              uint32_t *reset)
{
    if (dtcm_snapshot <= 0x20010004u) {
        *mask = 0xffffffffu;
        *reset = 0xbabefaceu;
    } else {
        *mask = 0x1ffu;
        *reset = 0x100u;
    }
}

/* a6xx_gmu.c:857-859 (A630 has no gmu_chipid). */
uint32_t AdrenoGmuBootV1Chipid(uint32_t chip_id)
{
    uint32_t chipid = chip_id & 0xffff0000u;
    chipid |= (chip_id << 4) & 0xf000u; /* minor */
    chipid |= (chip_id << 8) & 0x0f00u; /* patchid */
    return chipid;
}

/* a6xx_gmu.c:836-839 with A630 upper=0xa lower=0xa0 (BIT(31) | upper<<18 |
 * lower). */
uint32_t AdrenoGmuBootV1FenceValue(void)
{
    return 0x80000000u | (ADRENO_GMU_BOOT_V1_FENCE_UPPER << 18) |
           ADRENO_GMU_BOOT_V1_FENCE_LOWER;
}

/* a6xx_gmu.c:1066-1068 legacy branch. */
uint32_t AdrenoGmuBootV1BootState(uint32_t general7_value)
{
    return (general7_value == 1u) ? ADRENO_GMU_BOOT_V1_WARM
                                  : ADRENO_GMU_BOOT_V1_COLD;
}

/* ---- input validation ----------------------------------------------------- */

static int gmv1_inputs_ok(const AdrenoGmuBootV1Inputs *in)
{
    if (in == (const AdrenoGmuBootV1Inputs *)0) {
        return 0;
    }
    if (in->boot_state != ADRENO_GMU_BOOT_V1_COLD) {
        return 0; /* warm boot intentionally out of scope for the cold plan */
    }
    if (in->gmu_image_words == 0u || in->gmu_image_words > 0x2000u) {
        return 0; /* legacy gate: 0x8000 bytes = 0x2000 words (a6xx_gmu.c:741) */
    }
    if (in->log_pages == 0u) {
        return 0;
    }
    if (in->idle_level > ADRENO_GMU_BOOT_V1_IDLE_IFPC) {
        return 0;
    }
    return 1;
}

uint32_t AdrenoGmuBootV1PlanColdSize(const AdrenoGmuBootV1Inputs *in)
{
    /* Exact stream emitted by PlanCold for valid A630 inputs:
     * rpmh_start 4 (REQ, poll ACK, poll RSCC busy, REQ=0)
     * + GENERAL_7 1 + ITCM bulk 1
     * + FW_INIT_RESULT/BOOT_CONFIG/QTBL_ADDR/QTBL_INFO 4
     * + fence 1 + CM3_CFG 1
     * + HFI_SFR_ADDR/CP_MSG 2
     * + power_config 6 (SYS_BUS/ICACHE/DCACHE/IFPC_CTRL/SPTPRAC_HYST/rmw x2)
     * + gmu_start 4 (SYSRESET=1, CP_RESP=0, SYSRESET=0, poll; the DTCM
     *   snapshot read is an INPUT, not an op)
     * + gfx_rail_on 6 (option, GX vote, MX vote, OOB set/poll/clear)
     * + sptprac 2 (idle < SPTP only)
     * + hfi_start 2
     * = 33 ops for SPTP idle, 35 for idle < SPTP. */
    if (!gmv1_inputs_ok(in)) {
        return 0u;
    }
    return 33u + ((in->idle_level < ADRENO_GMU_BOOT_V1_IDLE_SPTP) ? 2u : 0u);
}

/* ---- cold plan ------------------------------------------------------------ */

AdrenoGmuBootV1Status AdrenoGmuBootV1PlanCold(const AdrenoGmuBootV1Inputs *in,
    AdrenoGmuBootV1Op *ops, uint32_t capacity, uint32_t *count)
{
    uint32_t n = 0u;
    uint32_t mask = 0u;
    uint32_t reset = 0u;
    const uint8_t G = (uint8_t)kAdrenoGmuBootV1DomainGmu;
    const uint8_t R = (uint8_t)kAdrenoGmuBootV1DomainRscc;

    if (!gmv1_inputs_ok(in) || ops == (AdrenoGmuBootV1Op *)0 ||
        count == (uint32_t *)0) {
        return kAdrenoGmuBootV1Invalid;
    }
    gmv1_zero_ops(ops, capacity);
    *count = 0u;
    if (capacity < AdrenoGmuBootV1PlanColdSize(in)) {
        return kAdrenoGmuBootV1NoSpace;
    }

    AdrenoGmuBootV1Cm3Expect(in->dtcm_snapshot, &mask, &reset);

    /* ---- a6xx_rpmh_start() :480-505 ---- */
    /* REQ=BIT(1); poll ACK bit1; poll RSCC SEQ_BUSY == 0; REQ=0. */
    if (!gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ, 0x2u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpPoll,
            ADRENO_GMU_BOOT_V1_RSCC_CONTROL_ACK, 0x2u, 0x2u, 0u) ||
        !gmv1_push(ops, capacity, &n, R, kAdrenoGmuBootV1OpPoll,
            ADRENO_GMU_BOOT_V1_RSCC_SEQ_BUSY_DRV0, 0u, 0xffffffffu, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ, 0u, 0u, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }

    /* ---- fw_start cold prologue :796-818 ---- */
    /* TCM retention (non-A7xx) :800; then flat ITCM firmware load :747-748. */
    if (!gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_GENERAL_7, 1u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWriteBulk,
            ADRENO_GMU_BOOT_V1_CM3_ITCM_START, in->gmu_image_words, 0u,
            in->gmu_image_words)) {
        return kAdrenoGmuBootV1NoSpace;
    }

    /* ---- fw_start CM3 config :820-839 ---- */
    if (!gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT, 0u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG,
            ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_HFI_QTBL_ADDR, in->hfi_iova, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO, 1u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_AHB_FENCE_RANGE_0,
            AdrenoGmuBootV1FenceValue(), 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_CM3_CFG,
            ADRENO_GMU_BOOT_V1_CM3_CFG_VALUE, 0u, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }

    /* ---- chipid + log :867-871 (non-A7xx branch) ---- */
    if (!gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_HFI_SFR_ADDR,
            AdrenoGmuBootV1Chipid(in->chip_id), 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PWR_COL_CP_MSG,
            in->log_iova | (in->log_pages - 1u), 0u, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }

    /* ---- a6xx_gmu_power_config() :666-706 (non-A7xx, SPTP idle) ----
     * SYS_BUS/ICACHE/DCACHE; IFPC_CTRL base; SPTP: SPTPRAC hyst + IFPC
     * enable rmw; RPMH_CTRL vote-enable rmw. IFPC idle additionally writes
     * INTER_FRAME_HYST + HM_POWER_COLLAPSE (caller must use the SPTP plan or
     * extend; the A630 bring-up fixes idle_level=SPTP upstream via probe). */
    if (!gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_SYS_BUS_CONFIG, 0x1u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_ICACHE_CONFIG, 0x1u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_DCACHE_CONFIG, 0x1u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_CTRL,
            ADRENO_GMU_BOOT_V1_PWR_COL_IFPC_CTRL, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PWR_COL_SPTPRAC_HYST,
            ADRENO_GMU_BOOT_V1_PWR_COL_HYST, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpRmw,
            ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_CTRL, 0u,
            0x3u /* IFPC_ENABLE|SPTPRAC_POWER_CONTROL_ENABLE (pos0|pos2) */,
            0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpRmw,
            ADRENO_GMU_BOOT_V1_RPMH_CTRL, 0u,
            ADRENO_GMU_BOOT_V1_RPMH_VOTE_BITS, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }

    /* ---- a6xx_gmu_start() :204-240 ----
     * DTCM snapshot read (input, not op) already consumed; SYSRESET=1,
     * CP_RESP=0 (non-A7xx :228), SYSRESET=0, poll FW_INIT_RESULT. */
    if (!gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_CM3_SYSRESET, 1u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PWR_COL_CP_RESP, 0u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_CM3_SYSRESET, 0u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpPoll,
            ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT, reset, mask, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }

    /* ---- a6xx_gmu_gfx_rail_on() :409-424 (legacy) ---- */
    if (!gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_BOOT_SLUMBER_OPTION, 0u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_GX_VOTE_IDX, in->gx_arc_vote & 0xffu, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_MX_VOTE_IDX,
            (in->gx_arc_vote >> 8) & 0xffu, 0u, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }
    /* OOB BOOT_SLUMBER: HOST2GMU set 1<<22, poll GMU2HOST bit30, clr bit30. */
    if (!gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_HOST2GMU_INTR_SET,
            1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_SET, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpPoll,
            ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_INFO,
            1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_ACK,
            1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_ACK, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_CLR,
            1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_CLEAR, 0u, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }

    /* ---- a6xx_sptprac_enable() :365-384 (idle < SPTP only) ---- */
    if (in->idle_level < ADRENO_GMU_BOOT_V1_IDLE_SPTP) {
        if (!gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
                ADRENO_GMU_BOOT_V1_SPTPRAC_POWER_CONTROL,
                ADRENO_GMU_BOOT_V1_SPTPRAC_ON_VALUE, 0u, 0u) ||
            !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpPoll,
                ADRENO_GMU_BOOT_V1_SPTPRAC_PWR_CLK_STATUS,
                ADRENO_GMU_BOOT_V1_SPTPRAC_CLK_PATTERN,
                ADRENO_GMU_BOOT_V1_SPTPRAC_CLK_MASK, 0u)) {
            return kAdrenoGmuBootV1NoSpace;
        }
    }

    /* ---- a6xx_gmu_hfi_start() :242-255 ---- */
    if (!gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT, 1u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, G, kAdrenoGmuBootV1OpPoll,
            ADRENO_GMU_BOOT_V1_HFI_CTRL_STATUS, 0x1u, 0x1u, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }

    *count = n;
    return kAdrenoGmuBootV1Success;
}

/* ---- RSC/PDC sequencer init plan (a6xx_gmu_rpmh_init :530-655) ------------ */

AdrenoGmuBootV1Status AdrenoGmuBootV1PlanRpmhInit(
    const uint32_t *rsc_ucode, const uint32_t *pdc_ucode,
    AdrenoGmuBootV1Op *ops, uint32_t capacity, uint32_t *count)
{
    uint32_t n = 0u;
    uint32_t i;
    const uint8_t R = (uint8_t)kAdrenoGmuBootV1DomainRscc;
    const uint8_t S = (uint8_t)kAdrenoGmuBootV1DomainPdcSeq;
    const uint8_t P = (uint8_t)kAdrenoGmuBootV1DomainPdc;

    if (ops == (AdrenoGmuBootV1Op *)0 || count == (uint32_t *)0 ||
        rsc_ucode == (const uint32_t *)0 || pdc_ucode == (const uint32_t *)0) {
        return kAdrenoGmuBootV1Invalid;
    }
    gmv1_zero_ops(ops, capacity);
    *count = 0u;

    /* RSC sequencer uCode :590-594 (A6xx non-650/7xx). */
    for (i = 0u; i < ADRENO_GMU_BOOT_V1_RSC_UCODE_WORDS; i++) {
        if (!gmv1_push(ops, capacity, &n, R, kAdrenoGmuBootV1OpWrite,
                ADRENO_GMU_BOOT_V1_RSCC_SEQ_MEM_0_DRV0 + i, rsc_ucode[i],
                0u, 0u)) {
            return kAdrenoGmuBootV1NoSpace;
        }
    }
    /* PDC sequencer uCode :601-605. */
    for (i = 0u; i < ADRENO_GMU_BOOT_V1_PDC_UCODE_WORDS; i++) {
        if (!gmv1_push(ops, capacity, &n, S, kAdrenoGmuBootV1OpWrite,
                ADRENO_GMU_BOOT_V1_PDC_SEQ_MEM_0 + i, pdc_ucode[i],
                0u, 0u)) {
            return kAdrenoGmuBootV1NoSpace;
        }
    }
    /* TCS1 :608-620 (ENABLE_BANK=7, WAIT=0, CONTROL=0, MSGID/ADDR/DATA x3). */
    if (!gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD_ENABLE_BANK, 7u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD_WAIT_BANK, 0u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CONTROL, 0u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_MSGID,
            ADRENO_GMU_BOOT_V1_TCS_MSGID, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_ADDR,
            ADRENO_GMU_BOOT_V1_TCS_CMD0_ADDR, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_DATA, 1u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_MSGID + 4u,
            ADRENO_GMU_BOOT_V1_TCS_MSGID, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_ADDR + 4u,
            ADRENO_GMU_BOOT_V1_TCS_CMD1_ADDR, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_DATA + 4u, 0u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_MSGID + 8u,
            ADRENO_GMU_BOOT_V1_TCS_MSGID, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_ADDR + 8u,
            ADRENO_GMU_BOOT_V1_PDC_ADDRESS_OFFSET, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_DATA + 8u, 0u, 0u, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }
    /* TCS3 :622-638 (A630 else-branch: CMD1 DATA=0x3, CMD2 DATA=0x3). */
    if (!gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD_ENABLE_BANK, 7u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD_WAIT_BANK, 0u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CONTROL, 0u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_MSGID,
            ADRENO_GMU_BOOT_V1_TCS_MSGID, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_ADDR,
            ADRENO_GMU_BOOT_V1_TCS_CMD0_ADDR, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_DATA, 2u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_MSGID + 4u,
            ADRENO_GMU_BOOT_V1_TCS_MSGID, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_ADDR + 4u,
            ADRENO_GMU_BOOT_V1_TCS_CMD1_ADDR, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_DATA + 4u, 0x3u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_MSGID + 8u,
            ADRENO_GMU_BOOT_V1_TCS_MSGID, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_ADDR + 8u,
            ADRENO_GMU_BOOT_V1_PDC_ADDRESS_OFFSET, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_DATA + 8u, 0x3u, 0u, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }
    /* setup_pdc :641-643. */
    if (!gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_SEQ_START_ADDR, 0u, 0u, 0u) ||
        !gmv1_push(ops, capacity, &n, P, kAdrenoGmuBootV1OpWrite,
            ADRENO_GMU_BOOT_V1_PDC_ENABLE_PDC,
            ADRENO_GMU_BOOT_V1_ENABLE_PDC_VALUE, 0u, 0u)) {
        return kAdrenoGmuBootV1NoSpace;
    }

    *count = n;
    return kAdrenoGmuBootV1Success;
}

/* ---- execution ------------------------------------------------------------ */

AdrenoGmuBootV1Status AdrenoGmuBootV1Execute(const AdrenoGmuBootV1Op *ops,
    uint32_t count, const AdrenoGmuBootV1Bus *bus)
{
    uint32_t i;
    uint32_t val = 0u;

    if (ops == (const AdrenoGmuBootV1Op *)0 ||
        bus == (const AdrenoGmuBootV1Bus *)0) {
        return kAdrenoGmuBootV1Invalid;
    }
    if (bus->write == 0 || bus->read == 0) {
        return kAdrenoGmuBootV1Invalid;
    }

    for (i = 0u; i < count; i++) {
        const uint8_t d = ops[i].domain;
        switch (ops[i].kind) {
        case kAdrenoGmuBootV1OpWrite:
            if (bus->write(bus->cookie, d, ops[i].offset, ops[i].value) != 0) {
                return kAdrenoGmuBootV1BusError;
            }
            break;
        case kAdrenoGmuBootV1OpWriteBulk:
            /* The op carries only the offset and word count; the bulk DATA
             * lives in caller-owned memory and is resolved by the callback
             * through its own cookie (one bulk op per plan: the ITCM image). */
            if (bus->write_bulk == 0) {
                return kAdrenoGmuBootV1Invalid;
            }
            if (bus->write_bulk(bus->cookie, d, ops[i].offset,
                    (const uint32_t *)0, ops[i].bulk_words) != 0) {
                return kAdrenoGmuBootV1BusError;
            }
            break;
        case kAdrenoGmuBootV1OpRead:
            if (bus->read(bus->cookie, d, ops[i].offset, &val) != 0) {
                return kAdrenoGmuBootV1BusError;
            }
            break;
        case kAdrenoGmuBootV1OpPoll:
            /* Single probe: caller retries until Success or timeout. */
            if (bus->read(bus->cookie, d, ops[i].offset, &val) != 0) {
                return kAdrenoGmuBootV1BusError;
            }
            if ((val & ops[i].mask) != ops[i].value) {
                return kAdrenoGmuBootV1PollTimeout;
            }
            break;
        case kAdrenoGmuBootV1OpRmw:
            if (bus->read(bus->cookie, d, ops[i].offset, &val) != 0) {
                return kAdrenoGmuBootV1BusError;
            }
            if (bus->write(bus->cookie, d, ops[i].offset,
                    val | ops[i].mask) != 0) {
                return kAdrenoGmuBootV1BusError;
            }
            break;
        default:
            return kAdrenoGmuBootV1Invalid;
        }
    }
    return kAdrenoGmuBootV1Success;
}
