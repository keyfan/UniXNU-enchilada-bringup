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

/* One GDSC descriptor: exactly the fields gdsc_check_status() reads. */
struct adreno_gdsc_descriptor {
    uint32_t gdscr;          /* .gdscr                                   */
    uint32_t gds_hw_ctrl;    /* .gds_hw_ctrl (0 when the driver omits it) */
    uint16_t flags;          /* .flags                                  */
    uint8_t  power_on_is_noop; /* .pd.power_on == gdsc_gx_do_nothing_enable */
    uint8_t  reserved;       /* keep the struct size stable             */
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

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_GDSC_DECODE_H */
