// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * AdrenoDomainState.h - enchilada (SDM845, Adreno A630) GPU power-domain
 *                       state-probe design, derived from board DTB + sha-pinned
 *                       Linux v6.12 clock/gdsc source.
 *
 * STATUS: HOST-VERIFIED DESIGN ONLY. No register has been read on a device.
 * This header answers a narrower question than AdrenoPowerGate.h:
 *
 *   AdrenoPowerGate.h  -> "may we touch the GPU/GMU/SMMU register windows?"
 *                         (answer today: NO, the domain may be collapsed)
 *   AdrenoDomainState.h-> "is there ANY GPU-adjacent register we can read,
 *                          with the domain collapsed, to learn whether it is up?"
 *
 * Why such a register exists
 * --------------------------
 * The mainline GDSC framework polls the GDSC status register *while* a domain
 * transitions out of / into collapse (drivers/clk/qcom/gdsc.c:103-117
 * gdsc_poll_status(), STATUS_POLL_TIMEOUT_US = 1500). A block cannot poll a
 * register that dies with the thing it is switching, so the GDSC status
 * register is by construction readable with its domain collapsed. It lives in
 * the GPUCC register file, which the DTB puts on always-on PMIC rails.
 *
 * Board provenance (oneplus6-ios/notes/mu-silicium/enchilada.dts,
 * sha256 e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275)
 * -------------------------------------------------------------------------
 *   :6223 qcom,gpucc@5090000 {          :6225 reg = <0x5090000 0x9000>;
 *   :6227     vdd_cx-supply = <0x1b>;    <- pm8998_s9_level, node at :10864
 *   :6228     vdd_mx-supply = <0x8d>;    <- pm8998_s6_level, node at :10817
 *   :6235 qcom,gfxcc@5090000 {          :6239     vdd_gfx-supply = <0x1d>;
 *                                        <- pm8005_s1_level, node at :11505
 *   :593  qcom,gdsc@0x509106c           :595 regulator-name = "gpu_cx_gdsc"
 *                                       :596 reg = <0x509106c 0x04>;
 *                                       :597 hw-ctrl-addr = <0x1a>;
 *                                            (VENDOR REGISTER INDEX, not an
 *                                             address - do not read it)
 *   :607  qcom,gdsc@0x509100c           :609 regulator-name = "gpu_gx_gdsc"
 *                                       :610 reg = <0x509100c 0x04>;
 *                                       :611 qcom,poll-cfg-gdscr;
 *
 * CORRECTION this header encodes (round 28)
 * ----------------------------------------
 * The DTB's gpu_cx_gdsc node advertises reg = <0x509106c 0x04>, but that is the
 * CONTROL register. The STATUS register mainline reads for CX is the
 * gds_hw_ctrl register, because gdsc.c:66-71 prefers it:
 *
 *   if (sc->flags & POLL_CFG_GDSCR)  reg = sc->gdscr + CFG_GDSCR_OFFSET; // 0x4
 *   else if (sc->gds_hw_ctrl)        reg = sc->gds_hw_ctrl;
 *   else                             reg = sc->gdscr;
 *
 * and gpucc-sdm845.c:119-128 gives gpu_cx_gdsc { .gdscr = 0x106c,
 * .gds_hw_ctrl = 0x1540, .flags = VOTABLE } while gpu_gx_gdsc
 * (:130-139) gives { .gdscr = 0x100c, .flags = CLAMP_IO | AON_RESET |
 * POLL_CFG_GDSCR }. So:
 *
 *   CX status  = GPUCC + 0x1540 = 0x05091540   test BIT(31)  (PWR_ON_MASK)
 *   GX status  = GPUCC + 0x100c + 0x4 = 0x05091010
 *                                              test BIT(16) up / BIT(15) down
 *   (The gx node's 0x509106c-like "hw-ctrl-addr = <0x1a>" on the cx node is a
 *    vendor register-index table offset, NOT a byte address. Reading 0x1a as
 *    an address would be a bug; this header does not use it.)
 *
 * Honest limits
 * -------------
 * - Pure derivation: DTB + pinned v6.12 source. Nothing here is measured.
 * - GX cannot be powered up by software (power_on = gdsc_gx_do_nothing_enable),
 *   so a GX-OFF reading is the EXPECTED steady state, not evidence of a fault.
 * - Whether XNU early boot leaves the GPUCC PMIC rails voted on is UNKNOWN.
 *   A real probe must be gated behind a boot-arg and a watchdog.
 */

#ifndef ADRENO_DOMAIN_STATE_H
#define ADRENO_DOMAIN_STATE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GPUCC register file window (DTB :6225 reg = <0x5090000 0x9000>). */
#define ADRENO_DOMAIN_GPUCC_BASE            0x05090000ull
#define ADRENO_DOMAIN_GPUCC_SIZE            0x9000ull
#define ADRENO_DOMAIN_GPUCC_MAX_REGISTER    0x8008u
#define ADRENO_DOMAIN_GPUCC_REG_STRIDE      4u

/* Control registers (DTB reg= values; write side, NOT safe to poke). */
#define ADRENO_DOMAIN_CX_GDSCR              0x0509106cull
#define ADRENO_DOMAIN_GX_GDSCR              0x0509100cull

/* Status registers actually polled by mainline gdsc.c. */
#define ADRENO_DOMAIN_CX_STATUS             0x05091540ull
#define ADRENO_DOMAIN_GX_STATUS             0x05091010ull

/* gpucc-sdm845.c:121 .gds_hw_ctrl = 0x1540 (CX status source). */
#define ADRENO_DOMAIN_CX_GDS_HW_CTRL        0x1540u
/* gdsc.c:34 CFG_GDSCR_OFFSET 0x4 (GX uses gdscr + 0x4). */
#define ADRENO_DOMAIN_CFG_GDSCR_OFFSET      0x4u

/* gdsc.c:20 PWR_ON_MASK BIT(31) -> CX on. */
#define ADRENO_DOMAIN_CX_ON_BIT             31u
/* gdsc.c:31 GDSC_POWER_UP_COMPLETE BIT(16) / :32 POWER_DOWN_COMPLETE BIT(15). */
#define ADRENO_DOMAIN_GX_ON_BIT             16u
#define ADRENO_DOMAIN_GX_OFF_BIT            15u

/* Number of GPU windows that must NOT be read before power-up. */
#define ADRENO_DOMAIN_FORBIDDEN_COUNT       6u

/* ---- pure predicates (no MMIO, no platform dependency) ---- */

/* 1 if addr lies inside the GPUCC window, else 0. */
int adreno_domain_addr_is_probe_window(uint64_t addr);

/* 1 if raw CX status word says the CX domain is powered, else 0. */
int adreno_domain_cx_is_on(uint32_t status_word);

/* 1 if raw GX status word reports power-up complete, else 0. */
int adreno_domain_gx_is_on(uint32_t status_word);

/* 1 if raw GX status word reports power-down complete, else 0. */
int adreno_domain_gx_is_off(uint32_t status_word);

/* 1 if [base, base+size) overlaps a forbidden GPU window, else 0. */
int adreno_domain_window_is_forbidden(uint64_t base, uint64_t size);

/* How many forbidden windows the design records. */
uint32_t adreno_domain_forbidden_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_DOMAIN_STATE_H */
