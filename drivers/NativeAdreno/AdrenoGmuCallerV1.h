/* SPDX-License-Identifier: MIT */
#ifndef ADRENO_GMU_CALLER_V1_H
#define ADRENO_GMU_CALLER_V1_H

#include <stdint.h>
#include "AdrenoGmuBootV1.h"
#include "AdrenoGmuBusV1.h"
#include "AdrenoDomainState.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Round 46: A6XX_GEN1 powered-GMU backend CALLER. This module ties the
 * round-42 boot-sequence plan (AdrenoGmuBootV1), the round-44 bus adapter
 * (AdrenoGmuBusV1) and the round-22/28 power gates (AdrenoPowerGate /
 * AdrenoDomainState) into ONE fail-closed entry point a future kernel
 * backend would call after mapping its windows.
 *
 * It is still a freestanding pure-C model: every hardware access goes
 * through the caller-wired AdrenoGmuBootV1Bus (GMU/RSCC/PDC windows) and a
 * GPUCC status-word read callback. It maps nothing, enables no clock and
 * no regulator by itself; the caller ASSERTS the genpd/clock state in
 * AdrenoGmuCallerV1Pre and the module CROSS-CHECKS it against the GPUCC
 * status registers the mainline GDSC framework polls (gdsc.c:66-71):
 *
 *   CX status = GPUCC + 0x1540 (gds_hw_ctrl), BIT(31) PWR_ON_MASK
 *   GX status = GPUCC + 0x1010 (gdscr + CFG_GDSCR_OFFSET), BIT(16) up
 *
 * Upstream order encoded (Linux v6.12 a6xx_gmu.c):
 *   probe time : a6xx_gmu_rpmh_init()      (:1745, once -- optional here)
 *   resume     : CX genpd -> GX genpd -> clocks (:1033-1047, caller-owned)
 *                read GENERAL_7 -> WARM or COLD (:1066-1068, legacy branch)
 *   cold path  : a6xx_gmu_fw_start -> rpmh_start / fw_load / CM3 / fence /
 *                chipid / power_config / gmu_start / gfx_rail_on / sptprac /
 *                hfi_start (the PlanCold op stream)
 *
 * Fail-closed rules:
 *   - pre.clocks_enabled == 0        -> kAdrenoGmuCallerV1NotReady
 *   - pre.cx_genpd_on == 0           -> kAdrenoGmuCallerV1NotReady
 *   - GPUCC CX status BIT(31) == 0   -> kAdrenoGmuCallerV1CxOff
 *   - require_gx && GX BIT(16) == 0  -> kAdrenoGmuCallerV1GxOff
 *   - GENERAL_7 says WARM            -> kAdrenoGmuCallerV1WarmUnsupported
 *                                      (warm path intentionally out of
 *                                      scope, upstream :1049-1068)
 *   - bus op failure                 -> kAdrenoGmuCallerV1BusError with the
 *                                      flattened op index
 *   - poll retries exhausted         -> kAdrenoGmuCallerV1PollTimeout
 *
 * HOST-VERIFIED: sha-pinned source text is a specification, not a device
 * measurement. gpu area stays host-verified until a device run.
 */

/* GPUCC status byte offsets inside the GPUCC window (base 0x05090000). */
#define ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE 0x1540u /* gpucc-sdm845.c:121  */
#define ADRENO_GMU_CALLER_V1_GX_STATUS_BYTE 0x1010u /* gdscr 0x100c + 0x4  */

typedef enum {
    kAdrenoGmuCallerV1Ok = 0,
    kAdrenoGmuCallerV1Invalid = 1,       /* NULL argument                    */
    kAdrenoGmuCallerV1NotReady = 2,      /* caller-asserted pre missing      */
    kAdrenoGmuCallerV1CxOff = 3,         /* GPUCC CX status says collapsed   */
    kAdrenoGmuCallerV1GxOff = 4,         /* require_gx and GX not up         */
    kAdrenoGmuCallerV1WarmUnsupported = 5, /* GENERAL_7 == 1 (out of scope)  */
    kAdrenoGmuCallerV1BusError = 6,      /* forwarded from Execute           */
    kAdrenoGmuCallerV1PollTimeout = 7    /* retries exhausted on a Poll op   */
} AdrenoGmuCallerV1Status;

/* GPUCC status-word reader. byte_offset is a BYTE offset inside the GPUCC
 * register file (0x1540 CX / 0x1010 GX). Returns 0 on success. */
struct AdrenoGmuCallerV1Gpucc {
    void *cookie;
    int (*read)(void *cookie, uint32_t byte_offset, uint32_t *out);
};

/* Caller-asserted power preconditions (upstream :1033-1047) plus the GX
 * policy. GX cannot be powered by software on some generations (DomainState
 * note); require_gx lets a backend choose fail-closed vs record-only. */
struct AdrenoGmuCallerV1Pre {
    uint32_t cx_genpd_on;      /* pm_runtime_get_sync(gmu->dev) done     */
    uint32_t gx_genpd_on;      /* pm_runtime_get_sync(gmu->gxpd) done    */
    uint32_t clocks_enabled;   /* clk_bulk_prepare_enable(6 clocks) done */
    uint32_t require_gx;       /* 1: refuse when GX status is not up     */
};

/* Precheck only (no GMU traffic). Consumes the caller-asserted state and
 * the GPUCC status words; refuses before ANY GMU-window access. */
AdrenoGmuCallerV1Status AdrenoGmuCallerV1Precheck(
    const struct AdrenoGmuCallerV1Pre *pre,
    const struct AdrenoGmuCallerV1Gpucc *gpucc);

/* Read GENERAL_7 through the wired bus and decode the legacy boot state
 * (AdrenoGmuBootV1BootState). Pure read; no precheck inside. */
AdrenoGmuCallerV1Status AdrenoGmuCallerV1BootState(
    const AdrenoGmuBootV1Bus *bus, uint32_t *boot_state);

/* Full cold boot: Precheck -> optional PlanRpmhInit (rsc/pdc ucode, probe
 * time, upstream runs it ONCE at probe) -> boot-state decision -> PlanCold
 * -> Execute with per-Poll retries. poll_retries = extra attempts per Poll
 * op (0 -> single attempt, matching AdrenoGmuBootV1Execute semantics).
 * first_fail_op (optional) receives the flattened op index (rpmh ops first,
 * cold ops after) of the first failing op. */
AdrenoGmuCallerV1Status AdrenoGmuCallerV1ColdBoot(
    const struct AdrenoGmuCallerV1Pre *pre,
    const struct AdrenoGmuCallerV1Gpucc *gpucc,
    const AdrenoGmuBootV1Inputs *in,
    const uint32_t *rsc_ucode, const uint32_t *pdc_ucode,
    const AdrenoGmuBootV1Bus *bus,
    uint32_t poll_retries,
    uint32_t *first_fail_op);

#ifdef __cplusplus
}
#endif
#endif /* ADRENO_GMU_CALLER_V1_H */
