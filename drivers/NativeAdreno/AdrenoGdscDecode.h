// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * AdrenoGdscDecode.h - enchilada (SDM845, Adreno A630): derive the GPU GDSC
 *                      status register and its masks from a GDSC descriptor,
 *                      the way Linux v6.12 does, instead of asserting them.
 *
 * STATUS: HOST-VERIFIED DERIVATION ONLY. No register has been read on a device.
 *
 * The gap this closes (round 56)
 * ------------------------------
 * AdrenoDomainState.h carries four constants that a probe depends on:
 *
 *   ADRENO_DOMAIN_CX_STATUS  0x05091540   ADRENO_DOMAIN_CX_ON_BIT  31
 *   ADRENO_DOMAIN_GX_STATUS  0x05091010   ADRENO_DOMAIN_GX_ON_BIT  16
 *                                         ADRENO_DOMAIN_GX_OFF_BIT 15
 *
 * Until round 55 those were ASSERTED in a comment. They are not arbitrary: each
 * one is a FUNCTION of the corresponding GDSC descriptor, via
 *
 *   drivers/clk/qcom/gdsc.c:66-71  (pinned, sha256 e1210770...)
 *       if (sc->flags & POLL_CFG_GDSCR) reg = sc->gdscr + CFG_GDSCR_OFFSET;
 *       else if (sc->gds_hw_ctrl)       reg = sc->gds_hw_ctrl;
 *       else                            reg = sc->gdscr;
 *
 *   drivers/clk/qcom/gdsc.c:77-91
 *       POLL_CFG_GDSCR: ON = val & GDSC_POWER_UP_COMPLETE   (BIT 16)
 *                       OFF = val & GDSC_POWER_DOWN_COMPLETE (BIT 15)
 *       otherwise:      ON = val & PWR_ON_MASK             (BIT 31)
 *                       OFF = NOT(val & PWR_ON_MASK)
 *
 * so this module re-derives the constants from the descriptor fields that
 * drivers/clk/qcom/gpucc-sdm845.c:119-139 (pinned, sha256 80c2fbef...) supplies:
 *
 *   gpu_cx_gdsc { .gdscr = 0x106c, .gds_hw_ctrl = 0x1540, .flags = VOTABLE }
 *     -> no POLL_CFG_GDSCR, gds_hw_ctrl non-zero
 *     -> status = 0x1540 -> 0x05091540, ON mask = BIT(31), no dedicated OFF bit
 *
 *   gpu_gx_gdsc { .gdscr = 0x100c,
 *                 .flags = CLAMP_IO | AON_RESET | POLL_CFG_GDSCR,
 *                 .pd.power_on = gdsc_gx_do_nothing_enable }
 *     -> POLL_CFG_GDSCR
 *     -> status = 0x100c + 0x4 = 0x1010 -> 0x05091010,
 *        ON mask = BIT(16), OFF mask = BIT(15)
 *
 * adreno_gdsc_verify_domain_state_constants() is the mechanical link: it builds
 * both descriptors from the pinned pin-file values, runs the derivation, and
 * reports whether the results equal the AdrenoDomainState.h constants. If
 * somebody edits either side by hand, this returns 0.
 *
 * Why the GX "power on is a no-op" flag matters
 * ---------------------------------------------
 * gdsc.c:596-616 / gdsc.h:89: GX is declared with a dummy enable so that, in the
 * words of the upstream comment, during power up nothing happens from the CPU
 * and the GMU powers GX up normally. The CPU therefore NEVER powers GX on by
 * itself, which is why the
 * device-measured GX status word (rounds 35/49/50/52) sits at a permanent
 * gx_off=1. A GX-OFF reading is the expected steady state, not a fault.
 *
 * Honest limits
 * -------------
 * - Pure derivation from pinned source + board DTB. Nothing here is measured.
 * - No MMIO, no IOKit, no firmware, no kernel caller. gpu area STAYS
 *   host-verified.
 */

#ifndef ADRENO_GDSC_DECODE_H
#define ADRENO_GDSC_DECODE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* drivers/clk/qcom/gdsc.h:61-70 flag bits (pinned, sha256 ba487640...). */
#define ADRENO_GDSC_FLAG_VOTABLE            0x0001u  /* BIT(0)  */
#define ADRENO_GDSC_FLAG_CLAMP_IO           0x0002u  /* BIT(1)  */
#define ADRENO_GDSC_FLAG_HW_CTRL            0x0004u  /* BIT(2)  */
#define ADRENO_GDSC_FLAG_SW_RESET           0x0008u  /* BIT(3)  */
#define ADRENO_GDSC_FLAG_AON_RESET          0x0010u  /* BIT(4)  */
#define ADRENO_GDSC_FLAG_POLL_CFG_GDSCR     0x0020u  /* BIT(5)  */
#define ADRENO_GDSC_FLAG_ALWAYS_ON          0x0040u  /* BIT(6)  */
#define ADRENO_GDSC_FLAG_RETAIN_FF_ENABLE   0x0080u  /* BIT(7)  */
#define ADRENO_GDSC_FLAG_NO_RET_PERIPH      0x0100u  /* BIT(8)  */
#define ADRENO_GDSC_FLAG_HW_CTRL_TRIGGER    0x0200u  /* BIT(9)  */

/* drivers/clk/qcom/gdsc.c:20/34 masks and offset (pinned). */
#define ADRENO_GDSC_PWR_ON_BIT              31u      /* PWR_ON_MASK BIT(31) */
#define ADRENO_GDSC_POWER_UP_COMPLETE_BIT   16u      /* CFG_GDSCR BIT(16)   */
#define ADRENO_GDSC_POWER_DOWN_COMPLETE_BIT 15u      /* CFG_GDSCR BIT(15)   */
#define ADRENO_GDSC_CFG_GDSCR_OFFSET        0x4u

/* No dedicated power-down bit exists for the non-POLL_CFG_GDSCR case; "off" is
 * !(val & on_mask) instead, so the off mask is reported as 0. */
#define ADRENO_GDSC_NO_OFF_MASK             0u

/* drivers/clk/qcom/gdsc.h:50-59 power-state bitfields. */
#define ADRENO_GDSC_PWRSTS_OFF     0x01u  /* BIT(0) */
#define ADRENO_GDSC_PWRSTS_RET     0x02u  /* BIT(1) */
#define ADRENO_GDSC_PWRSTS_ON      0x04u  /* BIT(2) */
#define ADRENO_GDSC_PWRSTS_OFF_ON  (ADRENO_GDSC_PWRSTS_OFF | ADRENO_GDSC_PWRSTS_ON)
#define ADRENO_GDSC_PWRSTS_RET_ON  (ADRENO_GDSC_PWRSTS_RET | ADRENO_GDSC_PWRSTS_ON)

/* drivers/clk/qcom/gdsc.c:119-137 SW_COLLAPSE bit written by gdsc_toggle_logic(). */
#define ADRENO_GDSC_SW_COLLAPSE_MASK  0x00000001u  /* BIT(0) */
/* drivers/clk/qcom/gdsc.c:231-250 clamp-IO / AON-reset masks. */
#define ADRENO_GDSC_GMEM_CLAMP_IO_MASK 0x00000001u /* BIT(0) */
#define ADRENO_GDSC_GMEM_RESET_MASK    0x00000010u /* BIT(4) */
/* drivers/clk/qcom/gdsc.c:207-229 retention masks for the (empty) cxc list. */
#define ADRENO_GDSC_RETAIN_MEM_BIT     14u
#define ADRENO_GDSC_RETAIN_PERIPH_BIT  13u
/* drivers/clk/qcom/gdsc.c:49-50 poll / toggle timeouts. */
#define ADRENO_GDSC_STATUS_POLL_TIMEOUT_US 1500u
#define ADRENO_GDSC_TIMEOUT_US             500u

/* One GDSC descriptor: exactly the fields gdsc_check_status() reads. */
struct adreno_gdsc_descriptor {
    uint32_t gdscr;          /* .gdscr                                   */
    uint32_t gds_hw_ctrl;    /* .gds_hw_ctrl (0 when the driver omits it) */
    uint32_t clamp_io_ctrl;  /* .clamp_io_ctrl (0 when omitted)          */
    uint16_t flags;          /* .flags                                  */
    uint8_t  pwrsts;         /* .pwrsts                                 */
    uint8_t  power_on_is_noop; /* .pd.power_on == gdsc_gx_do_nothing_enable */
    uint8_t  has_supply;     /* .supply != NULL -> regulator steps exist  */
    uint8_t  has_cxcs;       /* cxc_count != 0 -> mem-retention writes    */
};

/* ------------------------------------------------------------------------- *
 * Round 57: the enable/disable SEQUENCE, not just the status register.
 *
 * gdsc_enable()  (gdsc.c:259-315) and gdsc_disable() (gdsc.c:317-364) are
 * straight-line lists of conditional steps whose presence follows from the
 * SAME descriptor fields used above. Encoding them as a plan makes the CX
 * power-up path that AdrenoPowerUpV1 step 0 calls derived rather than asserted,
 * and it makes one safety property checkable arithmetic:
 *
 *   the CPU's GX enable path performs ZERO GDSC register writes, because
 *   gpu_gx_gdsc.pd.power_on == gdsc_gx_do_nothing_enable (gdsc.c:604-616).
 *
 * That is why the device-measured GX word (rounds 35/49/50/52) sits at a
 * permanent down-complete: the CPU never powers GX up; the GMU does.
 * ------------------------------------------------------------------------- */

#define ADRENO_GDSC_MAX_STEPS 8u

enum adreno_gdsc_step_kind {
    ADRENO_GDSC_STEP_NONE = 0,
    ADRENO_GDSC_STEP_REGULATOR_ENABLE = 1,
    ADRENO_GDSC_STEP_REGULATOR_DISABLE = 2,
    ADRENO_GDSC_STEP_ASSERT_RESET = 3,
    ADRENO_GDSC_STEP_DEASSERT_RESET = 4,
    ADRENO_GDSC_STEP_ASSERT_RESET_AON = 5,
    ADRENO_GDSC_STEP_DEASSERT_CLAMP_IO = 6,
    ADRENO_GDSC_STEP_ASSERT_CLAMP_IO = 7,
    ADRENO_GDSC_STEP_UPDATE_COLLAPSE = 8,
    ADRENO_GDSC_STEP_UDELAY = 9,
    ADRENO_GDSC_STEP_POLL_ON = 10,
    ADRENO_GDSC_STEP_POLL_OFF = 11,
    ADRENO_GDSC_STEP_FORCE_MEM_ON = 12,
    ADRENO_GDSC_STEP_CLEAR_MEM_ON = 13,
    ADRENO_GDSC_STEP_HWCTRL_ON = 14,
    ADRENO_GDSC_STEP_HWCTRL_OFF = 15,
    ADRENO_GDSC_STEP_RETAIN_FF_ON = 16
};

/* One step. `writes` is 1 only when the step touches a GDSC register; a step
 * that only waits, or only votes on a regulator, leaves the registers alone. */
struct adreno_gdsc_step {
    uint8_t  kind;      /* enum adreno_gdsc_step_kind                     */
    uint8_t  writes;    /* 1 when `offset`/`mask`/`value` are meaningful  */
    uint16_t delay_us;  /* wait performed by this step, when any          */
    uint32_t offset;    /* register offset inside the controller file     */
    uint32_t mask;      /* mask for register steps                        */
    uint32_t value;     /* value written for register steps               */
};

struct adreno_gdsc_plan {
    uint32_t gdscr;
    uint32_t count;           /* steps used                                 */
    uint32_t register_writes; /* number of steps that actually write        */
    struct adreno_gdsc_step steps[ADRENO_GDSC_MAX_STEPS];
};

/* Derived values for one descriptor. */
struct adreno_gdsc_status {
    uint32_t status_offset;  /* register offset inside the controller file */
    uint64_t status_address; /* gpucc_base + status_offset                 */
    uint32_t on_mask;        /* bit that means "powered"                   */
    uint32_t off_mask;       /* bit that means "down", or NO_OFF_MASK      */
    uint8_t  polls_cfg_gdscr;/* whether POLL_CFG_GDSCR selected gdscr + 4  */
    uint8_t  reserved[3];
};

/* ---- the two SDM845 GPU descriptors, from the pin file ---- */

struct adreno_gdsc_descriptor adreno_gdsc_cx_descriptor(void);
struct adreno_gdsc_descriptor adreno_gdsc_gx_descriptor(void);

/* ---- the derivation itself (mirrors gdsc.c:66-91) ---- */

/* gdscr + 4 when POLL_CFG_GDSCR, else gds_hw_ctrl when set, else gdscr. */
uint32_t adreno_gdsc_status_offset(const struct adreno_gdsc_descriptor *desc);

/* Full derivation: offset, address, on/off masks. Returns 0 for a NULL desc. */
int adreno_gdsc_derive(uint64_t gpucc_base, const struct adreno_gdsc_descriptor *desc,
                       struct adreno_gdsc_status *out);

/* 1 when the descriptor polls gdscr + CFG_GDSCR_OFFSET. */
int adreno_gdsc_polls_cfg_gdscr(const struct adreno_gdsc_descriptor *desc);

/* 1 when the CPU cannot power this domain on (GX: the GMU does it). */
int adreno_gdsc_power_on_is_noop(const struct adreno_gdsc_descriptor *desc);

/* "powered?" / "down?" for a raw status word, mirroring gdsc_check_status(). */
int adreno_gdsc_is_on(const struct adreno_gdsc_descriptor *desc, uint32_t status_word);
int adreno_gdsc_is_off(const struct adreno_gdsc_descriptor *desc, uint32_t status_word);

/* Mechanical link to AdrenoDomainState.h: 1 when both descriptors re-derive the
 * five constants in that header exactly, 0 otherwise. */
int adreno_gdsc_verify_domain_state_constants(void);

/* ---- round 57: the enable/disable step sequence (gdsc.c:259-364) ---- */

/* Build the plan gdsc_enable() would execute for this descriptor.
 * Returns 1 on success, 0 for a NULL argument or if the plan would not fit. */
int adreno_gdsc_plan_enable(const struct adreno_gdsc_descriptor *desc,
                            struct adreno_gdsc_plan *out);

/* Build the plan gdsc_disable() would execute for this descriptor. */
int adreno_gdsc_plan_disable(const struct adreno_gdsc_descriptor *desc,
                             struct adreno_gdsc_plan *out);

/* Build the plan the CPU actually executes on power-up: genpd calls
 * sc->pd.power_on, which is gdsc_gx_do_nothing_enable() for GX (zero register
 * writes) and gdsc_enable() for CX. */
int adreno_gdsc_plan_cpu_enable(const struct adreno_gdsc_descriptor *desc,
                                struct adreno_gdsc_plan *out);

/* 1 when the plan contains a step of `kind`, 0 otherwise. */
int adreno_gdsc_plan_has(const struct adreno_gdsc_plan *plan, uint8_t kind);

/* Index of the first step of `kind`, or -1 when absent. */
int adreno_gdsc_plan_index_of(const struct adreno_gdsc_plan *plan, uint8_t kind);

/* Mechanical safety property: the CPU's GX enable path must perform ZERO GDSC
 * register writes (gdsc.c:604-616 is a no-op apart from an optional regulator).
 * Returns 1 when that holds for the pinned descriptors, 0 otherwise. */
int adreno_gdsc_verify_gx_enable_writes_nothing(void);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_GDSC_DECODE_H */
