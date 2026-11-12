// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoClkCtlV1.c -- enchilada A6XX_GEN1 (A630) GPUCC clock-control
// derivation. Freestanding pure C11: no libc, no MMIO, no kernel linkage,
// no constructors. Nothing here touches hardware.
//
// See AdrenoClkCtlV1.h for the three findings this module encodes and for the
// sha256-pinned provenance. Short version:
//   * there is no "hub" clock on SDM845, so a6xx_gmu.c:1045-1046 is a silent
//     no-op that returns success (clk.c NULL guard);
//   * the core-rate set is the PARKED path on a fresh boot, because resume
//     calls clk_set_rate BEFORE clk_bulk_prepare_enable and the shared RCG
//     caches instead of writing when the RCG is disabled;
//   * only 2 of the 4 bulk clocks are GPUCC registers; the other 2 are GCC.

#include "AdrenoClkCtlV1.h"

#include <stdint.h>
#include <stddef.h>

/* ---- the frequency table, gpucc-sdm845.c:67-72 --------------------------- */

/* F(19200000, P_BI_TCXO,           1,   0, 0) -> pre_div 2*1-1 = 1, cfg 0
 * F(200000000, P_GPLL0_OUT_MAIN_DIV,1.5, 0, 0) -> pre_div 2*1.5-1 = 2, cfg 6
 * F(500000000, P_GPU_CC_PLL1_OUT_MAIN,1, 0, 0) -> pre_div 2*1-1 = 1, cfg 3
 * The parent_map_0 .cfg values are gpucc-sdm845.c:53-58: BI_TCXO 0,
 * GPU_CC_PLL1 3, GPLL0_MAIN 5, GPLL0_MAIN_DIV 6. */
static const struct adreno_clk_ctl_freq kFreqTable[3] = {
    { 19200000u,  0u, 1u },
    { 200000000u, 6u, 2u },
    { 500000000u, 3u, 1u }
};

const struct adreno_clk_ctl_freq *AdrenoClkCtlV1FreqTable(uint32_t *count)
{
    if (count != (uint32_t *)0) {
        *count = (uint32_t)(sizeof kFreqTable / sizeof kFreqTable[0]);
    }
    return kFreqTable;
}

int AdrenoClkCtlV1SelectFreq(uint32_t rate_hz, struct adreno_clk_ctl_freq *out)
{
    uint32_t i;
    uint32_t n = (uint32_t)(sizeof kFreqTable / sizeof kFreqTable[0]);

    if (out == (struct adreno_clk_ctl_freq *)0) {
        return 0;
    }
    /* common.c:28-42 qcom_find_freq(): first row with rate <= row.freq. */
    for (i = 0u; i < n; i++) {
        if (rate_hz <= kFreqTable[i].freq_hz) {
            *out = kFreqTable[i];
            return 1;
        }
    }
    /* Above every row: upstream returns f - 1, i.e. the LAST row. */
    *out = kFreqTable[n - 1u];
    return 1;
}

uint32_t AdrenoClkCtlV1CfgWord(const struct adreno_clk_ctl_freq *f)
{
    uint32_t cfg;

    if (f == (const struct adreno_clk_ctl_freq *)0) {
        return 0u;
    }
    /* clk-rcg2.c:432-441 __clk_rcg2_configure(), restricted to what this RCG
     * can express: mnd_width is 0 so there is no DUAL_EDGE mode bit, and
     * hw_clk_ctrl is unset for gpu_cc_gmu_clk_src. The generic path also
     * PRESERVES the bits outside mask (read-modify-write); landing exactly on
     * this word requires the pre-existing HW_CLK_CTRL bit to be clear, which
     * is why the caller's CFG is written whole rather than OR-ed. */
    cfg = (f->pre_div << ADRENO_CLK_CTL_V1_CFG_SRC_DIV_SHIFT) &
          ((1u << ADRENO_CLK_CTL_V1_RCG_HID_WIDTH) - 1u);
    cfg |= (f->src_cfg << ADRENO_CLK_CTL_V1_CFG_SRC_SEL_SHIFT) &
           ADRENO_CLK_CTL_V1_CFG_SRC_SEL_MASK;
    return cfg;
}

/* ---- clock topology facts ------------------------------------------------ */

uint32_t AdrenoClkCtlV1GmuClockCount(void)
{
    /* sdm845.dtsi:4941 + enchilada.dts:23890: gmu, cxo, axi, memnoc. */
    return 4u;
}

uint32_t AdrenoClkCtlV1GpuccClockCount(void)
{
    /* Only gmu (GPU_CC_CX_GMU_CLK) and cxo (GPU_CC_CXO_CLK) are GPUCC regs;
     * axi is GCC_DDRSS_GPU_AXI_CLK and memnoc is GCC_GPU_MEMNOC_GFX_CLK. */
    return 2u;
}

int AdrenoClkCtlV1HubClockPresent(void)
{
    /* The SDM845 GMU node lists no "hub"/"hub_clk" id. The A740 SM8550 node
     * does ("hub", sm8550.dtsi gmu@3d6a000), which is exactly the contrast
     * that makes this a generation fact rather than a mistake. */
    return 0;
}

int AdrenoClkCtlV1HubSetRateIsNoop(void)
{
    /* clk.c clk_set_rate(): `if (!clk) return 0;` -- a NULL handle is a
     * SUCCESSFUL no-op. a6xx_gmu.c:1488-1493 gets NULL when no bulk id
     * matches "hub"/"hub_clk" (msm_io_utils.c:17-32). */
    return AdrenoClkCtlV1HubClockPresent() == 0 ? 1 : 0;
}

int AdrenoClkCtlV1HubStepWritesNothing(void)
{
    /* a6xx_gmu.c:1045-1046 is one clk_set_rate call. With a NULL handle it
     * cannot reach clk_core_set_rate_nolock(), so no RCG register is written. */
    return AdrenoClkCtlV1HubSetRateIsNoop();
}

/* ---- plan construction helpers ------------------------------------------- */

static void accv1_reset(struct adreno_clk_ctl_plan *plan)
{
    uint32_t i;

    plan->count = 0u;
    plan->register_writes = 0u;
    for (i = 0u; i < ADRENO_CLK_CTL_V1_MAX_STEPS; i++) {
        plan->steps[i].kind = (uint8_t)ADRENO_CLK_CTL_STEP_NONE;
        plan->steps[i].writes = 0u;
        plan->steps[i].timeout_us = 0u;
        plan->steps[i].offset = 0u;
        plan->steps[i].mask = 0u;
        plan->steps[i].value = 0u;
    }
}

/* Returns 1 when the step was appended, 0 when the plan is full. */
static int accv1_push(struct adreno_clk_ctl_plan *plan, uint8_t kind,
                      uint8_t writes, uint16_t timeout_us,
                      uint32_t offset, uint32_t mask, uint32_t value)
{
    struct adreno_clk_ctl_step *s;

    if (plan->count >= ADRENO_CLK_CTL_V1_MAX_STEPS) {
        return 0;
    }
    s = &plan->steps[plan->count];
    s->kind = kind;
    s->writes = writes;
    s->timeout_us = timeout_us;
    s->offset = offset;
    s->mask = mask;
    s->value = value;
    if (writes != 0u) {
        plan->register_writes++;
    }
    plan->count++;
    return 1;
}

/* ---- the RCG configure legs ---------------------------------------------- */

/* clk-rcg2.c:1139-1166 clk_rcg2_set_force_enable(): set CMD_ROOT_EN at
 * cmd_rcgr + CMD_REG, then poll CMD_ROOT_OFF clear (clk_rcg2_is_enabled),
 * budget 500 iterations of udelay(1). */
static int accv1_push_force_enable(struct adreno_clk_ctl_plan *plan)
{
    if (!accv1_push(plan, (uint8_t)ADRENO_CLK_CTL_STEP_REG_UPDATE_BITS, 1u, 0u,
                    ADRENO_CLK_CTL_V1_RCG_CMD_RCGR + ADRENO_CLK_CTL_V1_CMD_REG,
                    ADRENO_CLK_CTL_V1_CMD_ROOT_EN,
                    ADRENO_CLK_CTL_V1_CMD_ROOT_EN)) {
        return 0;
    }
    return accv1_push(plan, (uint8_t)ADRENO_CLK_CTL_STEP_POLL_CLEAR, 0u,
                      (uint16_t)ADRENO_CLK_CTL_V1_RCG_POLL_US,
                      ADRENO_CLK_CTL_V1_RCG_CMD_RCGR + ADRENO_CLK_CTL_V1_CMD_REG,
                      ADRENO_CLK_CTL_V1_CMD_ROOT_OFF, 0u);
}

/* clk-rcg2.c:1149-1153 clk_rcg2_clear_force_enable(): clear CMD_ROOT_EN. */
static int accv1_push_clear_force_enable(struct adreno_clk_ctl_plan *plan)
{
    return accv1_push(plan, (uint8_t)ADRENO_CLK_CTL_STEP_REG_UPDATE_BITS, 1u, 0u,
                      ADRENO_CLK_CTL_V1_RCG_CMD_RCGR + ADRENO_CLK_CTL_V1_CMD_REG,
                      ADRENO_CLK_CTL_V1_CMD_ROOT_EN, 0u);
}

/* clk-rcg2.c:111-137 update_config(): set CMD_UPDATE, then poll it clear,
 * budget 500 iterations of udelay(1). */
static int accv1_push_update(struct adreno_clk_ctl_plan *plan)
{
    if (!accv1_push(plan, (uint8_t)ADRENO_CLK_CTL_STEP_REG_UPDATE_BITS, 1u, 0u,
                    ADRENO_CLK_CTL_V1_RCG_CMD_RCGR + ADRENO_CLK_CTL_V1_CMD_REG,
                    ADRENO_CLK_CTL_V1_CMD_UPDATE,
                    ADRENO_CLK_CTL_V1_CMD_UPDATE)) {
        return 0;
    }
    return accv1_push(plan, (uint8_t)ADRENO_CLK_CTL_STEP_POLL_CLEAR, 0u,
                      (uint16_t)ADRENO_CLK_CTL_V1_RCG_POLL_US,
                      ADRENO_CLK_CTL_V1_RCG_CMD_RCGR + ADRENO_CLK_CTL_V1_CMD_REG,
                      ADRENO_CLK_CTL_V1_CMD_UPDATE, 0u);
}

/* The CFG write itself (clk-rcg2.c:1260-1264 shared path / :1203 parked). */
static int accv1_push_cfg_write(struct adreno_clk_ctl_plan *plan, uint32_t cfg)
{
    return accv1_push(plan, (uint8_t)ADRENO_CLK_CTL_STEP_REG_WRITE, 1u, 0u,
                      ADRENO_CLK_CTL_V1_RCG_CMD_RCGR + ADRENO_CLK_CTL_V1_CFG_REG,
                      0xffffffffu, cfg);
}

/* ---- SET_CORE_RATE ------------------------------------------------------- */

int AdrenoClkCtlV1PlanSetCoreRate(uint32_t rate_hz, int rcg_enabled,
                                  struct adreno_clk_ctl_plan *out)
{
    struct adreno_clk_ctl_freq f;
    uint32_t cfg;

    if (out == (struct adreno_clk_ctl_plan *)0) {
        return 1;
    }
    accv1_reset(out);
    if (!AdrenoClkCtlV1SelectFreq(rate_hz, &f)) {
        return 2;
    }
    cfg = AdrenoClkCtlV1CfgWord(&f);

    if (!rcg_enabled) {
        /* clk-rcg2.c:1196-1205: qcom_find_freq succeeded, so an unknown rate
         * already failed above; the disabled RCG only caches. */
        if (!accv1_push(out, (uint8_t)ADRENO_CLK_CTL_STEP_PARK_CFG, 0u, 0u,
                        0u, 0u, cfg)) {
            return 3;
        }
        return 0;
    }

    /* clk-rcg2.c:1171-1186 clk_rcg2_shared_force_enable_clear(). */
    if (!accv1_push_force_enable(out)) {
        return 3;
    }
    if (!accv1_push_cfg_write(out, cfg)) {
        return 3;
    }
    if (!accv1_push_update(out)) {
        return 3;
    }
    if (!accv1_push_clear_force_enable(out)) {
        return 3;
    }
    return 0;
}

/* ---- RCG enable leg ------------------------------------------------------ */

/* 1 when the CFG word could have come from kFreqTable, 0 otherwise. */
static int accv1_cfg_is_derived(uint32_t cfg)
{
    uint32_t i;
    uint32_t n = (uint32_t)(sizeof kFreqTable / sizeof kFreqTable[0]);

    /* Fail-closed: nothing outside the exact derived word is accepted. This
     * rejects a stray HW_CLK_CTRL bit (which the generic configure path would
     * preserve) and any SRC_SEL/SRC_DIV combination the table cannot produce. */
    for (i = 0u; i < n; i++) {
        if (AdrenoClkCtlV1CfgWord(&kFreqTable[i]) == cfg) {
            return 1;
        }
    }
    return 0;
}

int AdrenoClkCtlV1PlanRcgEnable(uint32_t parked_cfg,
                                struct adreno_clk_ctl_plan *out)
{
    if (out == (struct adreno_clk_ctl_plan *)0) {
        return 1;
    }
    accv1_reset(out);
    if (!accv1_cfg_is_derived(parked_cfg)) {
        return 2;
    }
    /* clk-rcg2.c:1216-1239 clk_rcg2_shared_enable(). */
    if (!accv1_push_force_enable(out)) {
        return 3;
    }
    if (!accv1_push_cfg_write(out, parked_cfg)) {
        return 3;
    }
    if (!accv1_push_update(out)) {
        return 3;
    }
    if (!accv1_push_clear_force_enable(out)) {
        return 3;
    }
    return 0;
}

/* ---- branch clock enable ------------------------------------------------- */

static int accv1_is_gpucc_branch(uint32_t cbcr)
{
    return cbcr == ADRENO_CLK_CTL_V1_CX_GMU_CBCR ||
           cbcr == ADRENO_CLK_CTL_V1_CXO_CBCR;
}

int AdrenoClkCtlV1PlanBranchEnable(uint32_t cbcr,
                                   struct adreno_clk_ctl_plan *out)
{
    if (out == (struct adreno_clk_ctl_plan *)0) {
        return 1;
    }
    accv1_reset(out);
    if (!accv1_is_gpucc_branch(cbcr)) {
        return 2;
    }
    /* gpucc-sdm845.c:190-193 (probe-time wake/sleep recommendation). */
    if (!accv1_push(out, (uint8_t)ADRENO_CLK_CTL_STEP_REG_UPDATE_BITS, 1u, 0u,
                    cbcr, ADRENO_CLK_CTL_V1_CBCR_WS_MASK,
                    ADRENO_CLK_CTL_V1_CBCR_WS_VALUE)) {
        return 3;
    }
    /* clk-branch.c:100-106 clk_enable_regmap(). */
    if (!accv1_push(out, (uint8_t)ADRENO_CLK_CTL_STEP_REG_UPDATE_BITS, 1u, 0u,
                    cbcr, ADRENO_CLK_CTL_V1_CBCR_CLOCK_ENABLE,
                    ADRENO_CLK_CTL_V1_CBCR_CLOCK_ENABLE)) {
        return 3;
    }
    /* clk-branch.c:43-60 + :80-89: branch2 halt poll, 200 iterations x 1 us,
     * accepted when CBCR_CLK_OFF is clear OR the NOC FSM reports ON. */
    if (!accv1_push(out, (uint8_t)ADRENO_CLK_CTL_STEP_POLL_BRANCH2_ON, 0u, 200u,
                    cbcr, ADRENO_CLK_CTL_V1_CBCR_CLK_OFF,
                    ADRENO_CLK_CTL_V1_FSM_STATUS_ON)) {
        return 3;
    }
    return 0;
}

/* ---- self-check ---------------------------------------------------------- */

int AdrenoClkCtlV1Verify(uint32_t *check_count)
{
    uint32_t checks = 0u;
    struct adreno_clk_ctl_freq f;
    struct adreno_clk_ctl_plan plan;
    uint32_t count = 0u;
    uint32_t i;

#define ACCV1_REQUIRE(cond)                 \
    do {                                    \
        checks++;                           \
        if (!(cond)) {                      \
            if (check_count != (uint32_t *)0) { *check_count = checks; } \
            return (int)checks;             \
        }                                   \
    } while (0)

    /* 1-3: the table is the pinned three rows, and the F() macro arithmetic
     * (pre_div == 2*h - 1) is applied rather than the raw h. */
    (void)AdrenoClkCtlV1FreqTable(&count);
    ACCV1_REQUIRE(count == 3u);
    ACCV1_REQUIRE(kFreqTable[0].freq_hz == 19200000u &&
                  kFreqTable[0].src_cfg == 0u && kFreqTable[0].pre_div == 1u);
    ACCV1_REQUIRE(kFreqTable[1].freq_hz == 200000000u &&
                  kFreqTable[1].src_cfg == 6u && kFreqTable[1].pre_div == 2u);

    /* 4: the third row, and the parent-map cfg values are the DTB/table ones,
     * not the enum indices (P_GPU_CC_PLL1_OUT_MAIN is index 2 but cfg 3). */
    ACCV1_REQUIRE(kFreqTable[2].freq_hz == 500000000u &&
                  kFreqTable[2].src_cfg == 3u && kFreqTable[2].pre_div == 1u);

    /* 5-7: qcom_find_freq() semantics at, below and above the table. */
    ACCV1_REQUIRE(AdrenoClkCtlV1SelectFreq(200000000u, &f) == 1 &&
                  f.freq_hz == 200000000u);
    ACCV1_REQUIRE(AdrenoClkCtlV1SelectFreq(1u, &f) == 1 &&
                  f.freq_hz == 19200000u);
    ACCV1_REQUIRE(AdrenoClkCtlV1SelectFreq(1000000000u, &f) == 1 &&
                  f.freq_hz == 500000000u);

    /* 8: the derived CFG word for the resume rate is exactly 0x0602. */
    ACCV1_REQUIRE(AdrenoClkCtlV1SelectFreq(200000000u, &f) == 1 &&
                  AdrenoClkCtlV1CfgWord(&f) == 0x0602u);

    /* 9: and it contains no DUAL_EDGE mode bit and no HW_CLK_CTRL bit. */
    ACCV1_REQUIRE((AdrenoClkCtlV1CfgWord(&f) & ADRENO_CLK_CTL_V1_CFG_MODE_MASK) == 0u &&
                  (AdrenoClkCtlV1CfgWord(&f) & ADRENO_CLK_CTL_V1_CFG_HW_CLK_CTRL) == 0u);

    /* 10-11: the parked plan writes NOTHING, which is the finding. */
    ACCV1_REQUIRE(AdrenoClkCtlV1PlanSetCoreRate(200000000u, 0, &plan) == 0 &&
                  plan.count == 1u && plan.register_writes == 0u);
    ACCV1_REQUIRE(plan.steps[0].kind == (uint8_t)ADRENO_CLK_CTL_STEP_PARK_CFG &&
                  plan.steps[0].value == 0x0602u);

    /* 12-13: the enabled plan does 4 writes and 2 polls. */
    ACCV1_REQUIRE(AdrenoClkCtlV1PlanSetCoreRate(200000000u, 1, &plan) == 0 &&
                  plan.count == 6u && plan.register_writes == 4u);
    {
        uint32_t polls = 0u;
        for (i = 0u; i < plan.count; i++) {
            if (plan.steps[i].kind == (uint8_t)ADRENO_CLK_CTL_STEP_POLL_CLEAR) {
                polls++;
            }
        }
        ACCV1_REQUIRE(polls == 2u);
    }

    /* 14: every step of the enabled plan stays inside the GPUCC window. */
    {
        int inside = 1;
        for (i = 0u; i < plan.count; i++) {
            if (plan.steps[i].kind == (uint8_t)ADRENO_CLK_CTL_STEP_PARK_CFG) {
                continue;
            }
            if (plan.steps[i].offset > ADRENO_CLK_CTL_V1_GPUCC_MAX_REG) {
                inside = 0;
            }
        }
        ACCV1_REQUIRE(inside == 1);
    }

    /* 15-16: the hub finding, both directions. */
    ACCV1_REQUIRE(AdrenoClkCtlV1HubClockPresent() == 0);
    ACCV1_REQUIRE(AdrenoClkCtlV1HubSetRateIsNoop() == 1 &&
                  AdrenoClkCtlV1HubStepWritesNothing() == 1);

    /* 17-18: the 4-vs-2 clock-count distinction is real, not cosmetic. */
    ACCV1_REQUIRE(AdrenoClkCtlV1GmuClockCount() == 4u);
    ACCV1_REQUIRE(AdrenoClkCtlV1GpuccClockCount() == 2u);
    ACCV1_REQUIRE(AdrenoClkCtlV1GpuccClockCount() < AdrenoClkCtlV1GmuClockCount());

    /* 19: the RCG enable leg is 4 writes and 2 polls. */
    ACCV1_REQUIRE(AdrenoClkCtlV1PlanRcgEnable(0x0602u, &plan) == 0 &&
                  plan.count == 6u && plan.register_writes == 4u);

    /* 20: a CFG word the table cannot produce is REFUSED (fail-closed). */
    ACCV1_REQUIRE(AdrenoClkCtlV1PlanRcgEnable(0x0612u, &plan) != 0);

    /* 21: the branch enable is 2 writes + 1 halt poll, in that order. */
    ACCV1_REQUIRE(AdrenoClkCtlV1PlanBranchEnable(ADRENO_CLK_CTL_V1_CX_GMU_CBCR,
                                                 &plan) == 0 &&
                  plan.count == 3u && plan.register_writes == 2u);
    ACCV1_REQUIRE(plan.steps[0].mask == ADRENO_CLK_CTL_V1_CBCR_WS_MASK &&
                  plan.steps[0].value == ADRENO_CLK_CTL_V1_CBCR_WS_VALUE);
    ACCV1_REQUIRE(plan.steps[1].mask == ADRENO_CLK_CTL_V1_CBCR_CLOCK_ENABLE &&
                  plan.steps[1].value == ADRENO_CLK_CTL_V1_CBCR_CLOCK_ENABLE);

    /* 22: the halt poll is the DISJUNCTION kind, not a single mask. */
    ACCV1_REQUIRE(plan.steps[2].kind ==
                  (uint8_t)ADRENO_CLK_CTL_STEP_POLL_BRANCH2_ON);
    ACCV1_REQUIRE(plan.steps[2].mask == ADRENO_CLK_CTL_V1_CBCR_CLK_OFF);
    ACCV1_REQUIRE(plan.steps[2].value == ADRENO_CLK_CTL_V1_FSM_STATUS_ON);

    /* 23: a CBCR outside GPUCC is refused, so the module cannot be pointed at
     * the two GCC clocks it deliberately does not model. */
    ACCV1_REQUIRE(AdrenoClkCtlV1PlanBranchEnable(0x1440u, &plan) != 0);

    /* 24: NULL refusal on every plan builder. */
    ACCV1_REQUIRE(AdrenoClkCtlV1PlanSetCoreRate(200000000u, 0, (struct adreno_clk_ctl_plan *)0) != 0 &&
                  AdrenoClkCtlV1PlanRcgEnable(0x0602u, (struct adreno_clk_ctl_plan *)0) != 0 &&
                  AdrenoClkCtlV1PlanBranchEnable(ADRENO_CLK_CTL_V1_CX_GMU_CBCR,
                                                 (struct adreno_clk_ctl_plan *)0) != 0);

    /* 25: zero-width constants really are zero-width for this RCG. */
    ACCV1_REQUIRE(ADRENO_CLK_CTL_V1_RCG_MND_WIDTH == 0u);

    if (check_count != (uint32_t *)0) {
        *check_count = checks;
    }
    return 0;
#undef ACCV1_REQUIRE
}
