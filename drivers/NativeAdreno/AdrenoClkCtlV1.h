/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ADRENO_CLK_CTL_V1_H
#define ADRENO_CLK_CTL_V1_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Round 61: A6XX_GEN1 (enchilada / Adreno A630) GPUCC CLOCK-CONTROL half of
 * the ordered power-up plan.
 *
 * WHY THIS MODULE EXISTS
 * ----------------------
 * Round 51 (AdrenoPowerUpV1) encoded the ORDER of a6xx_gmu_resume()
 * (a6xx_gmu.c:1033-1047) as five ops and routed every side effect through
 * caller-wired AdrenoPowerUpV1Bus callbacks. Round 58 bound the two genpd
 * steps to the derived GDSC register writes (AdrenoPowerUpGdsc). The three
 * CLOCK steps -- SET_CORE_RATE, SET_HUB_RATE, CLOCKS_ENABLE -- stayed opaque:
 * a bus->set_rate()/bus->clocks_enable() callback may do anything and every
 * existing test still passes. This module derives what those callbacks must
 * actually do inside the GPUCC register file, from sha256-pinned Linux v6.12
 * source, so the same "trust -> checkable contract" move is applied to the
 * clock half.
 *
 * FINDING 1 (load-bearing, corrects an implicit assumption of rounds 45-60)
 * ------------------------------------------------------------------------
 * There is NO "hub" clock on SDM845. a6xx_gmu.c:1045-1046 unconditionally does
 *
 *     clk_set_rate(gmu->hub_clk, adreno_is_a740_family(...) ? 200000000
 *                                                          : 150000000);
 *
 * and a6xx_gmu.c:1488-1493 resolves those two handles through
 * msm_clk_bulk_get_clock(), which (msm_io_utils.c:17-32) matches a bulk id
 * equal to "hub" OR "hub_clk". The SDM845 GMU node's clock-names are
 *
 *     "gmu", "cxo", "axi", "memnoc"     (mainline sdm845.dtsi:4941; the
 *                                        vendored vendor DTB says the same
 *                                        4 names at enchilada.dts:23890)
 *
 * so no id matches and the getter returns NULL. clk_set_rate() on NULL is a
 * documented no-op that RETURNS SUCCESS (clk.c clk_set_rate: `if (!clk)
 * return 0;`). Therefore on this board the 150 MHz hub-rate step is a
 * GUARANTEED, SILENT NO-OP.
 *
 * The A740 contrast is not a different rate, it is a DIFFERENT SHAPE: the
 * SM8550 GMU node lists seven clocks whose names include "hub"
 * (sm8550.dtsi gmu@3d6a000: clock-names "ahb","gmu","cxo","axi","memnoc",
 * "hub","demet"), so on A740 the same line actually programs a real RCG.
 * AdrenoPowerUpV1HubRateHz() is still correct as a VALUE (a6xx_gmu.c:1045-1046
 * is exact), but a backend that treats step SET_HUB_RATE as a register write
 * on enchilada would be encoding behaviour this hardware does not have, and a
 * test that expected a write there would be testing an invention.
 *
 * FINDING 2 (the core rate is a PARKED write, not a write)
 * --------------------------------------------------------
 * The "gmu" handle IS present on SDM845 and resolves to gpucc-sdm845.c:88-104
 * gpu_cc_cx_gmu_clk (clk_branch, .enable_reg 0x1098, .enable_mask BIT(0)) whose
 * parent is gpu_cc_gmu_clk_src (clk_rcg2, .cmd_rcgr 0x1120, .mnd_width 0,
 * .hid_width 5, ops clk_rcg2_shared_ops; gpucc-sdm845.c:74-86). So
 * clk_set_rate(core_clk, 200000000) runs clk_rcg2_shared_set_rate():
 *
 *   - qcom_find_freq() picks the FIRST table row with rate <= row.freq, so
 *     200000000 selects F(200000000, P_GPLL0_OUT_MAIN_DIV, 1.5, 0, 0).
 *     The F() macro (clk-rcg.h:10) stores 2*h-1 as pre_div, so pre_div == 2,
 *     and parent_map_0 (gpucc-sdm845.c:53-58) maps P_GPLL0_OUT_MAIN_DIV -> 6.
 *     With mnd_width 0 there are NO M/N/D writes and NO CFG_MODE_DUAL_EDGE:
 *     the whole CFG word is (2 << 0) | (6 << 8) == 0x0602.
 *   - if the RCG is NOT enabled, the shared ops take the `!clk_hw_is_enabled`
 *     branch (clk-rcg2.c:1198-1205) and only cache that word in parked_cfg.
 *     ZERO register writes.
 *   - only when the RCG IS enabled does it run force_enable -> configure ->
 *     update -> clear (4 writes, 2 polls).
 *
 * The resume order is clk_set_rate FIRST (:1044-1046) and
 * clk_bulk_prepare_enable SECOND (:1047), so on a fresh port the set_rate leg
 * is the parked path: the derived plan for the disabled case is one
 * PARK_CFG step with writes == 0. The real CFG write then happens inside
 * CLOCKS_ENABLE, because clk_rcg2_shared_enable() writes parked_cfg back
 * (clk-rcg2.c:1216-1239). That is why the two plans must be derived together.
 *
 * FINDING 3 (which clocks are even in this window)
 * ------------------------------------------------
 * The bulk prepare/enable covers FOUR clocks; only two live in GPUCC
 * (gpu_cc_cx_gmu_clk 0x1098, gpu_cc_cxo_clk 0x109c). The other two --
 * GCC_DDRSS_GPU_AXI_CLK and GCC_GPU_MEMNOC_GFX_CLK -- are GCC registers, are
 * OUTSIDE the GPUCC window, and are deliberately NOT modeled here. Any count
 * this module reports is a GPUCC-subset count, never the bulk count of 4.
 *
 * HONEST BOUNDARY
 * ---------------
 * This is a HOST-VERIFIED DERIVATION from sha256-pinned source plus the board
 * DTB. It performs no MMIO, maps nothing, powers no domain, enables no clock,
 * has no kernel caller, and is NOT a device measurement. Reading a GPUCC
 * register still requires the round-22/28 precondition chain to have run
 * first; nothing here relaxes that. gpu area stays host-verified.
 */

/* ---- Board / window facts ------------------------------------------------ */

/* DTB qcom,gpucc@5090000 reg = <0x5090000 0x9000>; gpucc-sdm845.c:157
 * .max_register = 0x8008. Both derived, not re-asserted. */
#define ADRENO_CLK_CTL_V1_GPUCC_BASE      0x05090000u
#define ADRENO_CLK_CTL_V1_GPUCC_SIZE      0x9000u
#define ADRENO_CLK_CTL_V1_GPUCC_MAX_REG   0x8008u

/* gpucc-sdm845.c:88-104 gpu_cc_cx_gmu_clk: .halt_reg .enable_reg 0x1098,
 * .enable_mask BIT(0), .halt_check BRANCH_HALT. */
#define ADRENO_CLK_CTL_V1_CX_GMU_CBCR     0x1098u
/* gpucc-sdm845.c:106-117 gpu_cc_cxo_clk: .halt_reg .enable_reg 0x109c. */
#define ADRENO_CLK_CTL_V1_CXO_CBCR        0x109cu

/* gpucc-sdm845.c:74-86 gpu_cc_gmu_clk_src RCG. */
#define ADRENO_CLK_CTL_V1_RCG_CMD_RCGR    0x1120u
#define ADRENO_CLK_CTL_V1_RCG_HID_WIDTH   5u
#define ADRENO_CLK_CTL_V1_RCG_MND_WIDTH   0u

/* clk-rcg2.c:23-46 register offsets and bit fields, verbatim. */
#define ADRENO_CLK_CTL_V1_CMD_REG         0x0u
#define ADRENO_CLK_CTL_V1_CFG_REG         0x4u
#define ADRENO_CLK_CTL_V1_CMD_UPDATE      0x00000001u  /* BIT(0)  */
#define ADRENO_CLK_CTL_V1_CMD_ROOT_EN     0x00000002u  /* BIT(1)  */
#define ADRENO_CLK_CTL_V1_CMD_ROOT_OFF    0x80000000u  /* BIT(31) */
#define ADRENO_CLK_CTL_V1_CFG_SRC_DIV_SHIFT 0u
#define ADRENO_CLK_CTL_V1_CFG_SRC_SEL_SHIFT 8u
#define ADRENO_CLK_CTL_V1_CFG_SRC_SEL_MASK  0x00000700u
#define ADRENO_CLK_CTL_V1_CFG_MODE_MASK     0x00003000u
#define ADRENO_CLK_CTL_V1_CFG_HW_CLK_CTRL   0x00100000u

/* clk-branch.h:59-67 CBCR bits and the halt poll the branch2 ops use. */
#define ADRENO_CLK_CTL_V1_CBCR_CLK_OFF        0x80000000u  /* BIT(31) */
#define ADRENO_CLK_CTL_V1_CBCR_NOC_FSM_STATUS 0x70000000u  /* GENMASK(30,28) */
#define ADRENO_CLK_CTL_V1_FSM_STATUS_ON       0x20000000u  /* BIT(1) of field */
#define ADRENO_CLK_CTL_V1_CBCR_CLOCK_ENABLE   0x00000001u  /* BIT(0)  */

/* gpucc-sdm845.c:21-24 + :190-193: probe-time wake/sleep recommended values,
 * 0xf in BOTH the wake field (11:8) and the sleep field (7:4) of 0x1098. */
#define ADRENO_CLK_CTL_V1_CBCR_WAKE_SHIFT  8u
#define ADRENO_CLK_CTL_V1_CBCR_SLEEP_SHIFT 4u
/* mask = (WAKE_MASK << WAKE_SHIFT) | (SLEEP_MASK << SLEEP_SHIFT)
 *      = (0xf << 8) | (0xf << 4) = 0x0ff0; the value written is the SAME
 *      word, because upstream writes 0xf into both fields. */
#define ADRENO_CLK_CTL_V1_CBCR_WS_MASK     0x00000ff0u
#define ADRENO_CLK_CTL_V1_CBCR_WS_VALUE    0x00000ff0u

/* clk-rcg2.c:129-137 CMD_UPDATE poll bound is 500; clk-rcg2.c:1152-1166
 * force-enable poll bound is 500 as well (both udelay(1)). */
#define ADRENO_CLK_CTL_V1_RCG_POLL_US      500u

/* ---- Frequency table (gpucc-sdm845.c:67-72, F() applied) ---------------- */

/* clk-rcg.h:10  #define F(f, s, h, m, n) { (f), (s), (2 * (h) - 1), (m), (n) }
 * The stored pre_div is therefore already 2*h-1, and with mnd_width == 0 the
 * m/n columns are unused. */
struct adreno_clk_ctl_freq {
    uint32_t freq_hz;
    uint32_t src_cfg;   /* parent_map_0 .cfg value, NOT the enum index      */
    uint32_t pre_div;   /* stored 2*h-1, i.e. the CFG SRC_DIV field value   */
};

/* ---- Derived plans ------------------------------------------------------ */

#define ADRENO_CLK_CTL_V1_MAX_STEPS 10u

enum adreno_clk_ctl_step_kind {
    ADRENO_CLK_CTL_STEP_NONE = 0,
    ADRENO_CLK_CTL_STEP_REG_WRITE = 1,       /* write(offset, value)        */
    ADRENO_CLK_CTL_STEP_REG_UPDATE_BITS = 2, /* update_bits(offset,mask,val)*/
    ADRENO_CLK_CTL_STEP_POLL_CLEAR = 3,      /* poll while (val & mask)!=0  */
    ADRENO_CLK_CTL_STEP_POLL_SET = 4,        /* poll until (val & mask)!=0  */
    ADRENO_CLK_CTL_STEP_PARK_CFG = 5,        /* cache only: NO register I/O */
    /* clk_branch2_check_halt() when enabling is a DISJUNCTION, not a mask:
     *   (val & CBCR_CLK_OFF) == 0  ||  FIELD_GET(NOC_FSM_STATUS) == ON
     * so it gets its own kind. `mask` holds CBCR_CLK_OFF, `value` holds the
     * raw FSM_STATUS_ON bit (already shifted into place). Modelling this as a
     * single-mask POLL would silently drop one of the two acceptance paths. */
    ADRENO_CLK_CTL_STEP_POLL_BRANCH2_ON = 6
};

struct adreno_clk_ctl_step {
    uint8_t  kind;       /* enum adreno_clk_ctl_step_kind                  */
    uint8_t  writes;     /* 1 only when a register is actually written     */
    uint16_t timeout_us; /* poll budget for the POLL_* kinds, else 0       */
    uint32_t offset;     /* byte offset inside GPUCC                       */
    uint32_t mask;       /* mask for UPDATE_BITS / POLL_*                  */
    uint32_t value;      /* written value, or the parked CFG word          */
};

struct adreno_clk_ctl_plan {
    uint32_t count;
    uint32_t register_writes;  /* number of steps with writes == 1 */
    struct adreno_clk_ctl_step steps[ADRENO_CLK_CTL_V1_MAX_STEPS];
};

/* ---- Pure board predicates ---------------------------------------------- */

/* 1 when a clock whose bulk id is "hub"/"hub_clk" exists on this board.
 * Enchilada: 0 -- the SDM845 GMU node names only gmu/cxo/axi/memnoc. */
int AdrenoClkCtlV1HubClockPresent(void);

/* 1 when clk_set_rate(gmu->hub_clk, ...) is a guaranteed silent no-op that
 * still returns success (clk.c clk_set_rate NULL guard). Enchilada: 1. */
int AdrenoClkCtlV1HubSetRateIsNoop(void);

/* The four GMU bulk clocks the prepare/enable covers (sdm845.dtsi:4941). */
uint32_t AdrenoClkCtlV1GmuClockCount(void);

/* How many of those live in the GPUCC window. Deliberately 2, NOT 4: the axi
 * and memnoc clocks are GCC registers. */
uint32_t AdrenoClkCtlV1GpuccClockCount(void);

/* ---- Frequency selection ------------------------------------------------ */

/* The three-row table, and its length. Never NULL for a non-NULL count. */
const struct adreno_clk_ctl_freq *AdrenoClkCtlV1FreqTable(uint32_t *count);

/* qcom_find_freq() semantics (common.c:28-42): the FIRST row with
 * rate <= row.freq_hz; when the request exceeds every row, the LAST row.
 * Returns 1 on success and 0 for a NULL out or an empty table. */
int AdrenoClkCtlV1SelectFreq(uint32_t rate_hz, struct adreno_clk_ctl_freq *out);

/* The full CFG word clk_rcg2_configure() would write for `f`: the SRC_DIV
 * field, the SRC_SEL field, and NOTHING else (mnd_width 0 means no DUAL_EDGE
 * mode bit; hw_clk_ctrl is unset for this RCG). */
uint32_t AdrenoClkCtlV1CfgWord(const struct adreno_clk_ctl_freq *f);

/* ---- Plans -------------------------------------------------------------- */

/* The derived SET_CORE_RATE plan.
 *  rcg_enabled == 0  -> 1 step, PARK_CFG, register_writes == 0.
 *  rcg_enabled == 1  -> force_enable(1 write + ROOT_EN poll) -> CFG write ->
 *                        CMD_UPDATE (1 write + poll) -> clear (1 write):
 *                        count 6, register_writes 4, 2 polls.
 * Returns 0 on success, non-zero on a NULL argument, an unknown rate, or a
 * plan that would not fit. */
int AdrenoClkCtlV1PlanSetCoreRate(uint32_t rate_hz, int rcg_enabled,
                                  struct adreno_clk_ctl_plan *out);

/* The derived CLOCKS_ENABLE plan for one GPUCC branch clock, in the order
 * clk_bulk_prepare_enable() produces (prepare-all, then enable-all):
 *   1. UPDATE_BITS wake/sleep recommended value on `cbcr` (gpucc-sdm845.c:190-193,
 *      mask 0x0ff0 value 0x0ff0) -- upstream does this ONCE at probe, so a
 *      backend that really wants to mirror upstream performs it once outside
 *      this per-enable plan; it is kept here because it must be true of the
 *      register before the branch is enabled and it is harmless to repeat.
 *   2. enable: UPDATE_BITS(cbcr, CBCR_CLOCK_ENABLE, CBCR_CLOCK_ENABLE)
 *   3. halt:   POLL_BRANCH2_ON(cbcr)  (clk-branch.c:43-60)
 * `cbcr` must be one of the two GPUCC branch registers this module models.
 * Returns 0 on success, non-zero on a NULL out or an unknown cbcr. */
int AdrenoClkCtlV1PlanBranchEnable(uint32_t cbcr,
                                   struct adreno_clk_ctl_plan *out);

/* The derived plan for the RCG enable leg alone, i.e. steps 2 of
 * clk_rcg2_shared_enable() (clk-rcg2.c:1216-1239) WITHOUT the branch clocks:
 *   force_enable(UPDATE_BITS CMD_ROOT_EN) -> POLL_SET(CMD_ROOT_OFF clear)
 *   -> CFG <- parked word -> UPDATE(CMD_UPDATE) -> POLL_CLEAR(CMD_UPDATE)
 *   -> clear(UPDATE_BITS CMD_ROOT_EN, 0)
 * `parked_cfg` is the word AdrenoClkCtlV1CfgWord() produced.
 * Returns 0 on success, non-zero on a NULL out or a CFG word that could not
 * have come from this table (fail-closed: the SRC_SEL must be one of
 * parent_map_0's three cfg values and the SRC_DIV must fit hid_width 5). */
int AdrenoClkCtlV1PlanRcgEnable(uint32_t parked_cfg,
                                struct adreno_clk_ctl_plan *out);

/* The derived hub-rate step as DATA: 1 when the step performs no register
 * write on this board (the enchilada case), 0 when it would. */
int AdrenoClkCtlV1HubStepWritesNothing(void);

/* ---- Self-check --------------------------------------------------------- */

/* Cross-check every derived invariant in this module. Returns 0 when all
 * hold, else the 1-based index of the first failure (same convention as
 * AdrenoPowerUpGdscVerify / AdrenoPowerDownV1Verify). check_count receives
 * the number of checks evaluated. */
int AdrenoClkCtlV1Verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif
#endif /* ADRENO_CLK_CTL_V1_H */
