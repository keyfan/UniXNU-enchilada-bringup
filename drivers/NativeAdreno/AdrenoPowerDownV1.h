// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoPowerDownV1.h -- the TEARDOWN half of the A630 GMU power sequence.
//
// WHY THIS MODULE EXISTS
// ----------------------
// Round 51 (AdrenoPowerUpV1) modeled ONLY the 5-step power-UP sequence from
// a6xx_gmu_resume() (a6xx_gmu.c:1033-1047). The DOWN direction was never
// modeled at all: AdrenoPowerUpV1Bus.genpd_off() existed solely so that
// Execute() could roll back on a failure, and apuv1_rollback() reverses
// ACQUISITION order (GX then CX).
//
// Upstream's real teardown is NOT simply "run the power-up plan backwards".
// a6xx_gmu_stop() (a6xx_gmu.c:1184-1217) emits:
//
//     :1189-1190  if (!pm_runtime_active(gmu->dev)) return 0;      // gate
//     :1196-1199  hang ? a6xx_gmu_force_off() : a6xx_gmu_shutdown() // GMU halt
//     :1202       dev_pm_opp_set_opp(&gpu->pdev->dev, NULL);        // bus vote
//     :1204-1210  /* Make sure the GX domain is off before turning off the
//                     GMU (CX) domain. Usually the GMU does this but only if
//                     the shutdown sequence was successful */
//                 if (!IS_ERR_OR_NULL(gmu->gxpd))
//                     pm_runtime_put_sync(gmu->gxpd);
//     :1212       clk_bulk_disable_unprepare(gmu->nr_clocks, gmu->clocks);
//     :1214       pm_runtime_put_sync(gmu->dev);                    // CX last
//
// The ORDERING CONSTRAINT is stated twice upstream:
//   gdsc.c:590      "Hardware constraints dictate that the GX be powered down
//                    before the CX"
//   a6xx_gmu.c:1205 "Make sure the GX domain is off before turning off the GMU
//                    (CX) domain"
//
// So the hardware rule is CX-LAST, and the clock veto sits BETWEEN GX-off and
// CX-off. Teardown is therefore a constrained ordering, not a mirror image:
//
//   power-up : CX -> GX -> core rate -> hub rate -> clocks
//   power-down: GX -> clocks -> CX          (rates are not undone)
//
// Note what is deliberately NOT reversed: the clock rates. Power-up sets core
// 200 MHz and hub 150 MHz (A630); teardown disables the clocks but never
// restores a previous rate, matching upstream (no clk_set_rate in
// a6xx_gmu_stop). Modeling a "restore old rate" step would invent behavior
// upstream does not have.
//
// THE HAZARD THIS EXPOSES (honest, not hidden)
// --------------------------------------------
// The CX domain's disable plan performs NO status poll (the VOTABLE short-cut,
// gdsc.c:153-161, peer of the board DTB's qcom,no-status-check-on-disable).
// So after `pm_runtime_put_sync(gmu->dev)` the CPU has NO evidence that CX
// actually reached the off-complete state -- it only knows the request was
// issued. AdrenoPowerDownV1CxOffIsUnverified() reports that as a first-class,
// testable property rather than a footnote. GX, by contrast, DOES poll its
// derived down-complete bit, so GX-off is verifiable by the CPU.

#ifndef ADRENO_POWER_DOWN_V1_H
#define ADRENO_POWER_DOWN_V1_H

#include <stdint.h>

#include "AdrenoGmuCallerV1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Ordered teardown steps (a6xx_gmu_stop :1204-1214) ------------------- */
#define ADRENO_POWER_DOWN_V1_STEP_GX_GENPD_OFF   0u
#define ADRENO_POWER_DOWN_V1_STEP_CLOCKS_DISABLE 1u
#define ADRENO_POWER_DOWN_V1_STEP_CX_GENPD_OFF   2u
#define ADRENO_POWER_DOWN_V1_STEP_COUNT          3u

typedef enum {
    kAdrenoPowerDownV1Ok = 0,
    kAdrenoPowerDownV1Invalid = 1,       /* NULL / bad inputs / bad plan      */
    kAdrenoPowerDownV1GxFailed = 2,      /* pm_runtime_put_sync(gmu->gxpd)    */
    kAdrenoPowerDownV1ClocksFailed = 3,  /* clk_bulk_disable_unprepare        */
    kAdrenoPowerDownV1CxFailed = 4       /* pm_runtime_put_sync(gmu->dev)     */
} AdrenoPowerDownV1Status;

/* Caller-owned board + runtime facts. All *_flag fields are 0/1 booleans. */
struct AdrenoPowerDownV1Inputs {
    uint32_t gxpd_present;    /* !IS_ERR_OR_NULL(gmu->gxpd); caller-owned      */
    uint32_t gmu_active;      /* pm_runtime_active(gmu->dev); the :1189 gate   */
    uint32_t gmu_clock_count; /* gmu->nr_clocks (vendor DTB = 4)               */
};

/* One op of the ordered teardown plan. Deterministic for given inputs: the
 * guarded GX step is always EMITTED with guarded=1, and Execute() skips it
 * when gxpd_present == 0 -- exactly mirroring the power-up plan contract, so
 * the plan shape never depends on runtime state. */
typedef struct {
    uint32_t step;    /* ADRENO_POWER_DOWN_V1_STEP_*                         */
    uint32_t guarded; /* 1 = skip when inputs.gxpd_present == 0              */
    uint32_t value;   /* clock count for the disable step, else 0            */
} AdrenoPowerDownV1Op;

/* Caller-wired bus. All callbacks return 0 on success, non-zero on failure.
 * which: 0 = CX genpd, 1 = GX genpd. No callback owns MMIO; a real XNU
 * backend wires pm_runtime / clk ops here. This struct is deliberately
 * SEPARATE from AdrenoPowerUpV1Bus: a teardown-only consumer must not be
 * forced to supply genpd_on/set_rate callbacks it will never use. */
struct AdrenoPowerDownV1Bus {
    void *cookie;
    int (*genpd_off)(void *cookie, uint32_t which);
    int (*clocks_disable)(void *cookie, uint32_t count);
};

/* ---- Pure helpers (no bus, no side effects) ------------------------------ */

/* a6xx_gmu_stop:1189-1190 -- teardown is a no-op when the GMU is not active.
 * Exposed as a predicate so the host test asserts the gate without executing. */
int AdrenoPowerDownV1ShouldRun(const struct AdrenoPowerDownV1Inputs *in);

/* 1 iff all inputs are structurally valid (booleans 0/1, nonzero clock count).
 * Used by Plan/Execute and independently by the host test. */
int AdrenoPowerDownV1InputsValid(const struct AdrenoPowerDownV1Inputs *in);

/* Step -> failure status mapping, and guarded-step predicate. Pure tables. */
AdrenoPowerDownV1Status AdrenoPowerDownV1StepStatus(uint32_t step);
uint32_t AdrenoPowerDownV1StepGuarded(uint32_t step);

/* Number of ops Plan emits for valid inputs; 0 for invalid inputs. */
uint32_t AdrenoPowerDownV1PlanSize(const struct AdrenoPowerDownV1Inputs *in);

/* Build the EXACT 3-op ordered sequence GX -> clocks -> CX. ops capacity must
 * be >= PlanSize. Never emits hardware traffic. Returns Invalid on bad
 * inputs/capacity. */
AdrenoPowerDownV1Status AdrenoPowerDownV1Plan(
    const struct AdrenoPowerDownV1Inputs *in,
    AdrenoPowerDownV1Op *ops, uint32_t capacity, uint32_t *count);

/* Execute the plan in order against the bus. On success clears *pre (a torn
 * down GMU must not look ready to the round-46 caller) and returns Ok.
 *
 * ON FAILURE THIS MODULE DOES NOT "ROLL BACK": re-acquiring CX to redo a
 * partial teardown would be inventing behavior, and upstream does not do it
 * either -- a6xx_gmu_stop() ignores every return value and proceeds. Instead
 * Execute() stops at the failing step, leaves *pre cleared (still not ready),
 * and reports which step failed so the caller can escalate. That asymmetry
 * with Execute()'s rollback is intentional and is asserted by the host test.
 *
 * fail_index (optional) receives the op index of the first failure, or
 * ADRENO_POWER_DOWN_V1_STEP_COUNT when no op failed (success sentinel). */
AdrenoPowerDownV1Status AdrenoPowerDownV1Execute(
    const struct AdrenoPowerDownV1Inputs *in,
    const AdrenoPowerDownV1Op *ops, uint32_t count,
    const struct AdrenoPowerDownV1Bus *bus,
    struct AdrenoGmuCallerV1Pre *pre, uint32_t *fail_index);

/* 1 iff the CX leg of the teardown is NOT verifiable by the CPU (no status
 * poll). This is the round-59 finding expressed as a predicate; it reads the
 * round-57 disable plan, so it flips automatically if the pin changes. */
int AdrenoPowerDownV1CxOffIsUnverified(void);

/* Self-check of this module's contract against the derived GDSC plans.
 * Returns 0 when every check passes, else the 1-based index of the first
 * failing check (the same convention as AdrenoPowerUpGdscVerify). */
int AdrenoPowerDownV1Verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif
#endif /* ADRENO_POWER_DOWN_V1_H */
