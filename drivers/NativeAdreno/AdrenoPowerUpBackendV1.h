/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ADRENO_POWER_UP_BACKEND_V1_H
#define ADRENO_POWER_UP_BACKEND_V1_H

#include <stdint.h>
#include <stddef.h>

#include "AdrenoGdscDecode.h"
#include "AdrenoPowerUpV1.h"
#include "AdrenoClkCtlV1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Round 62: the EXECUTOR for the A6XX_GEN1 power-up chain.
 *
 * THE GAP THIS CLOSES
 * -------------------
 * Rounds 51, 57, 58, 59 and 61 all end in the same place: a PLAN. AdrenoPowerUpV1
 * emits five ordered ops, AdrenoGdscDecode emits register-step lists for each
 * domain, AdrenoClkCtlV1 emits the RCG and branch plans. Not one of them writes
 * a register, and until now nothing in this tree did either. AdrenoPowerUpV1Bus
 * was a shape with no implementation: a backend could wire four callbacks that
 * do nothing at all and every existing test would still pass.
 *
 * This module is that implementation. It supplies a REAL AdrenoPowerUpV1Bus
 * whose four callbacks turn the derived plans into actual MMIO traffic, so
 * AdrenoPowerUpV1Execute() and the round-46 AdrenoGmuCallerV1 chain finally
 * have a bus that writes registers instead of a stub that returns 0.
 *
 * WHAT EACH CALLBACK DOES (all of it derived, none of it invented)
 * ---------------------------------------------------------------
 *   genpd_on(0)   CX: execute the round-57 CX enable plan
 *                     = 1 write (collapse clear at 0x106c), udelay, 1 poll of
 *                       0x1540 BIT(31), udelay.
 *   genpd_on(1)   GX: execute the round-57 GX CPU-enable plan
 *                     = ZERO register writes (gpu_gx_gdsc.pd.power_on is
 *                       gdsc_gx_do_nothing_enable; the GMU powers GX up).
 *   genpd_off(0)  CX: collapse + udelay, and NO poll (VOTABLE short-cut).
 *   genpd_off(1)  GX: clamp/reset + collapse + poll BIT(15).
 *   set_rate(0,h) core: plan from AdrenoClkCtlV1PlanSetCoreRate(h, rcg_enabled).
 *                     When the RCG is disabled this is ONE PARK_CFG step and
 *                     performs ZERO MMIO -- the round-61 finding, executed.
 *   set_rate(1,h) hub:  ZERO MMIO, unconditionally. There is no "hub" clock on
 *                     SDM845, so clk_set_rate() never reaches a register
 *                     (round-61 finding 1). The callback still returns success,
 *                     exactly as the kernel's NULL guard does.
 *   clocks_enable RCG enable leg (4 writes + 2 polls), then the two GPUCC
 *                 branch clocks (2 writes + 1 branch2 halt poll each).
 *
 * THE ARM GATE
 * ------------
 * Nothing here touches MMIO unless the caller explicitly arms it. `armed` must
 * be 1, and arming is a SEPARATE decision from constructing the backend, so a
 * code path that builds a backend for inspection cannot accidentally write.
 * This mirrors the boot-arg gating every device-verified stage in this port has
 * used, and it is what makes AdrenoPowerUpBackendV1Make() safe to call from a
 * probe that has not yet passed its preconditions.
 *
 * HONEST BOUNDARY
 * ---------------
 * HOST-VERIFIED EXECUTOR. It performs MMIO *through the caller's callbacks* and
 * does nothing else: it maps nothing, owns no window, enables no regulator,
 * runs no TrustZone PAS, and has no IOKit caller of its own. The round-22/28
 * prohibition on touching 0x5000000 / 0x506a000 / 0x5040000 while the domain is
 * collapsed is UNCHANGED and is NOT enforced here -- a caller that arms this
 * module with a collapsed domain will still hang the bus. The gate that must
 * run first is AdrenoGmuCallerV1Precheck, and the whole point of returning an
 * AdrenoGmuCallerV1Pre from Up() is to feed exactly that.
 */

/* The four callbacks are wired onto this struct, which is handed straight to
 * AdrenoPowerUpV1Execute(). AdrenoPowerUpV1Bus.cookie points back at the
 * backend, so a caller must keep the backend alive for the duration. */
struct AdrenoPowerUpBackendV1Mmio {
    void *cookie;
    int (*read32)(void *cookie, uint32_t byte_offset, uint32_t *out);
    int (*write32)(void *cookie, uint32_t byte_offset, uint32_t value);
    int (*udelay)(void *cookie, uint32_t usec);
};

struct AdrenoPowerUpBackendV1Inputs {
    uint32_t armed;           /* MUST be 1 or Make() refuses                     */
    uint32_t gxpd_present;    /* enchilada: 1 (the DTB carries gpu_gx_gdsc)      */
    uint32_t rcg_enabled;     /* 1 if gmu_clk_src is already running             */
    uint32_t core_rate_hz;    /* a6xx_gmu.c:1044 = 200000000                     */
    uint32_t strict_gx;       /* -> pre->require_gx; 0 recommended on this board */
};

/* Observable traffic counters. `refused` counts MMIO attempts made while the
 * backend was disarmed -- it must stay 0 in any real run. */
struct AdrenoPowerUpBackendV1Stats {
    uint32_t reads;
    uint32_t writes;
    uint32_t delays;
    uint32_t polls;
    uint32_t refused;
};

struct AdrenoPowerUpBackendV1 {
    struct AdrenoPowerUpV1Bus bus;        /* what AdrenoPowerUpV1Execute eats */
    struct AdrenoPowerUpBackendV1Inputs in;
    struct AdrenoPowerUpBackendV1Stats stats;
    const struct AdrenoPowerUpBackendV1Mmio *mmio;
    uint32_t parked_cfg;                  /* survives set_rate -> clocks_enable */
    uint32_t cx_on;                       /* observed/asserted, for Pre          */
    uint32_t gx_on;
    uint32_t clocks_on;
};

/* Wire the backend. Returns 0 on success, non-zero when any argument is NULL,
 * when the MMIO triple is incomplete, or when in->armed != 1 (fail-closed: an
 * unarmed backend is not constructed at all, rather than constructed inert). */
int AdrenoPowerUpBackendV1Make(struct AdrenoPowerUpBackendV1 *be,
                               const struct AdrenoPowerUpBackendV1Mmio *mmio,
                               const struct AdrenoPowerUpBackendV1Inputs *in);

/* Build the five-op plan for these inputs and execute it through the backend,
 * producing the Pre the round-46 AdrenoGmuCallerV1 chain consumes.
 *
 * This is the entry point a kernel backend calls once its windows are mapped
 * and its preconditions hold. On success *pre is ready for
 * AdrenoGmuCallerV1Precheck(); on any failure *pre is cleared and the status
 * says which step failed. */
AdrenoPowerUpV1Status AdrenoPowerUpBackendV1Up(
    struct AdrenoPowerUpBackendV1 *be,
    struct AdrenoGmuCallerV1Pre *pre,
    uint32_t *fail_index);

/* Self-check. Returns 0 when every invariant holds, else the 1-based index of
 * the first failure. check_count receives the number of checks evaluated. */
int AdrenoPowerUpBackendV1Verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif
#endif /* ADRENO_POWER_UP_BACKEND_V1_H */
