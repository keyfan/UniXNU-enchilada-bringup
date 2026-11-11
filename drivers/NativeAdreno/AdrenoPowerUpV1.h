/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ADRENO_POWER_UP_V1_H
#define ADRENO_POWER_UP_V1_H

#include <stdint.h>
#include <stddef.h>
#include "AdrenoGmuCallerV1.h"   /* struct AdrenoGmuCallerV1Pre */

#ifdef __cplusplus
extern "C" {
#endif

/* Round 51: A6XX_GEN1 (enchilada / Adreno A630) ORDERED genpd + clock
 * POWER-UP sequence, faithfully modeled on Linux v6.12 a6xx_gmu_resume()
 * (:1012-1060). This closes the gap left by round 46's AdrenoGmuCallerV1:
 * that module only ASSERTS the preconditions in AdrenoGmuCallerV1Pre ("the
 * caller ASSERTS the genpd/clock state"); nothing in this tree encoded HOW to
 * reach that state, or the rollback when a step fails. This module produces
 * that Pre struct from an executed plan, so the two modules compose.
 *
 * STATUS: HOST-VERIFIED freestanding pure-C model. No libc, no MMIO, no
 * constructors, no kernel linkage. Every side effect goes through
 * caller-wired AdrenoPowerUpV1Bus callbacks; the host test wires a recorder.
 * A sha-pinned upstream source is a SPECIFICATION, not a device measurement.
 * The gpu area stays host-verified until a device run.
 *
 * Upstream order encoded (pinned a6xx_gmu.c, sha256 c14fbb25...):
 *   :1033  pm_runtime_get_sync(gmu->dev)                 -> CX genpd
 *   :1040  if (!IS_ERR_OR_NULL(gmu->gxpd))
 *   :1041      pm_runtime_get_sync(gmu->gxpd)            -> GX genpd (guarded;
 *          upstream comment: "won't actually do anything but it will make
 *          sure that the refcounting is correct")
 *   :1044  clk_set_rate(gmu->core_clk, 200000000)
 *   :1045  clk_set_rate(gmu->hub_clk, adreno_is_a740_family(...) ?
 *   :1046              200000000 : 150000000)           -> A630 = 150000000
 *   :1047  ret = clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks)
 *   :1048  if (ret) { pm_runtime_put(gmu->gxpd);
 *   :1050            pm_runtime_put(gmu->dev); return ret; }   -> ROLLBACK
 *
 * Deliberate OUT-OF-SCOPE boundary: :1055 a6xx_gmu_set_initial_bw,
 * :1058-1059 GMU_AO_HOST_INTERRUPT_CLR/MASK and :1060 enable_irq are
 * GMU-window MMIO / IRQ wiring, already modeled by the boot/caller chain
 * (AdrenoGmuBootV1 / AdrenoGmuCallerV1). This module stops at :1047/:1052.
 *
 * TWO CLOCK COUNTS, NEVER CONFLATED (load-bearing, enchilada.dts):
 *   qcom,gmu  :23889-23890  clocks / clock-names = 4
 *              "gmu_clk\0cxo_clk\0axi_clk\0memnoc_clk"
 *              -> gmu->nr_clocks from devm_clk_bulk_get_all(gmu->dev)
 *                 (a6xx_gmu.c:1481-1486); THIS is clk_bulk_prepare_enable's
 *                 count.
 *   qcom,kgsl-3d0@5000000 :23656-23657 = 6
 *              "core_clk\0rbbmtimer_clk\0mem_clk\0mem_iface_clk\0gmu_clk\0l3_vote"
 *              -> ADRENO_POWER_GPU_CLOCK_COUNT 6u in AdrenoPowerGate.h.
 *   Upstream clk_set_rate(..., "gmu"/"hub") names are MAINLINE sdm845.dtsi
 *   names, NOT the vendor DTB names; do not translate them into the vendor
 *   list. AdrenoPowerUpV1Inputs requires the two counts to be DISTINCT so a
 *   backend that used the wrong DTB node fails closed instead of silently
 *   enabling the wrong number of clocks.
 *
 * GX policy (honest): AdrenoDomainState.h documents that GX cannot be
 * powered up by software (power_on = gdsc_gx_do_nothing_enable), so GX-OFF is
 * the EXPECTED steady state on this board (device-verified round 35 GPUCC
 * probe: gx_off=1, gx_on=0, verdict=cx-DOWN). Therefore:
 *   - The vendored DTB DOES carry gpu_gx_gdsc (enchilada.dts:607-621,
 *     phandle 0x2a9, vdd-supply of kgsl-3d0 at :23668) and mainline attaches
 *     it (a6xx_gmu.c:1638 dev_pm_domain_attach_by_name(gmu->dev, "gx")), so
 *     gxpd_present = 1 is the BOARD-REALISTIC value. The guarded branch
 *     `if (!IS_ERR_OR_NULL(gmu->gxpd))` is upstream's defensive check; BOTH
 *     branches are modeled and tested.
 *   - pre->gx_genpd_on means "the GX genpd refcount was acquired", NOT
 *     "the GX hardware domain is powered" (upstream's own comment at
 *     :1035-1039: enabling it "won't actually do anything").
 *   - strict_gx (-> pre->require_gx) is recommended 0 for enchilada: with
 *     require_gx=1 the round-46 caller's Precheck rejects every run on this
 *     hardware (GX BIT(16) stays 0). The module does not force either value.
 *   - COMPOSITION LIMIT (explicit, not hidden): round-46's
 *     caller_v1_pre_ok() requires gx_genpd_on != 0 UNCONDITIONALLY. When
 *     gxpd_present == 0 this module honestly produces gx_genpd_on = 0 and
 *     the round-46 caller returns NotReady. That is a CONSUMER-SIDE
 *     fail-closed gate, not a power-up failure; AdrenoPowerUpV1PreIsReady()
 *     exposes exactly that check so the host test can assert it.
 *
 * Rollback hardening (documented deviation): upstream ONLY rolls back on the
 * clk_bulk_prepare_enable failure (:1048-1052), by putting gxpd then dev.
 * This model rolls back ANY already-acquired genpd on ANY failed step, in
 * reverse acquisition order. That is a fail-closed SUPERSET (never weaker
 * than upstream); no clocks/rates are undone, matching upstream.
 */

/* ---- Board constants with provenance ------------------------------------- */

/* a6xx_gmu.c:1044 clk_set_rate(gmu->core_clk, 200000000). Same on all gens. */
#define ADRENO_POWER_UP_V1_CORE_RATE_HZ   200000000u
/* a6xx_gmu.c:1045-1046: A740 family 200 MHz, otherwise (A630) 150 MHz. */
#define ADRENO_POWER_UP_V1_HUB_RATE_A740  200000000u
#define ADRENO_POWER_UP_V1_HUB_RATE_A630  150000000u
/* enchilada.dts:23889-23890 qcom,gmu clock-names (4 entries). */
#define ADRENO_POWER_UP_V1_GMU_CLOCKS     4u
/* enchilada.dts:23656-23657 qcom,kgsl-3d0 clock-names (6 entries). */
#define ADRENO_POWER_UP_V1_GPU_CLOCKS     6u
/* A630 is NOT an A740-family part (a6xx_catalog.c:884-885 chipids
 * 0x06030001/2 vs :1328 A740 0x43050a01). */
#define ADRENO_POWER_UP_V1_IS_A740_FAMILY 0u

/* ---- Ordered plan steps (a6xx_gmu_resume :1033-1047) --------------------- */
#define ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON    0u
#define ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON    1u
#define ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE  2u
#define ADRENO_POWER_UP_V1_STEP_SET_HUB_RATE   3u
#define ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE  4u
#define ADRENO_POWER_UP_V1_STEP_COUNT          5u

typedef enum {
    kAdrenoPowerUpV1Ok = 0,
    kAdrenoPowerUpV1Invalid = 1,          /* NULL / bad inputs / bad plan     */
    kAdrenoPowerUpV1CxFailed = 2,         /* pm_runtime_get_sync(gmu->dev)   */
    kAdrenoPowerUpV1GxFailed = 3,         /* pm_runtime_get_sync(gmu->gxpd)  */
    kAdrenoPowerUpV1CoreRateFailed = 4,   /* clk_set_rate(core)              */
    kAdrenoPowerUpV1HubRateFailed = 5,    /* clk_set_rate(hub)               */
    kAdrenoPowerUpV1ClocksFailed = 6,     /* clk_bulk_prepare_enable         */
    kAdrenoPowerUpV1RollbackFailed = 7    /* a genpd_off callback failed     */
} AdrenoPowerUpV1Status;

/* Caller-owned board + policy facts. All *_flag fields are 0/1 booleans. */
struct AdrenoPowerUpV1Inputs {
    uint32_t gxpd_present;       /* !IS_ERR_OR_NULL(gmu->gxpd); caller-owned */
    uint32_t gmu_clock_count;    /* gmu->nr_clocks (vendor DTB = 4)          */
    uint32_t gpu_clock_count;    /* kgsl-3d0 clocks (vendor DTB = 6)         */
    uint32_t a740_family;        /* 1 -> hub 200 MHz; 0 -> A630 hub 150 MHz  */
    uint32_t strict_gx;          /* -> pre->require_gx (recommended 0)      */
};

/* One op of the ordered plan. The plan is DETERMINISTIC for given inputs:
 * the guarded GX step is always emitted (with guarded=1) and Execute skips
 * it when gxpd_present==0, so the plan itself never depends on runtime state. */
typedef struct {
    uint32_t step;               /* ADRENO_POWER_UP_V1_STEP_*                */
    uint32_t guarded;            /* 1 = skip when inputs.gxpd_present == 0   */
    uint32_t value;              /* rate Hz (Set*Rate) or clock count        */
} AdrenoPowerUpV1Op;

/* Caller-wired bus. All callbacks return 0 on success, non-zero on failure.
 * which: 0 = CX genpd / core clk, 1 = GX genpd / hub clk. No callback owns
 * MMIO; a real XNU backend wires pm_runtime / clk ops here. */
struct AdrenoPowerUpV1Bus {
    void *cookie;
    int (*genpd_on)(void *cookie, uint32_t which);
    int (*genpd_off)(void *cookie, uint32_t which);   /* rollback only */
    int (*set_rate)(void *cookie, uint32_t which, uint32_t hz);
    int (*clocks_enable)(void *cookie, uint32_t count);
};

/* ---- Pure helpers (no bus, no side effects) ------------------------------ */

/* Board defaults that ARE backed by the vendored DTB / pinned sources.
 * gxpd_present and strict_gx are intentionally NOT returned here: they are
 * backend/policy decisions, not board constants. */
uint32_t AdrenoPowerUpV1BoardGmuClockCount(void);   /* 4 */
uint32_t AdrenoPowerUpV1BoardGpuClockCount(void);   /* 6 */
uint32_t AdrenoPowerUpV1BoardIsA740Family(void);    /* 0 */

/* a6xx_gmu.c:1044 / :1045-1046 rate selection, pure. */
uint32_t AdrenoPowerUpV1CoreRateHz(void);
uint32_t AdrenoPowerUpV1HubRateHz(uint32_t a740_family);

/* 1 iff the two clock counts are distinct (the anti-conflation invariant). */
int AdrenoPowerUpV1CountsAreDistinct(uint32_t gmu_count, uint32_t gpu_count);

/* 1 iff all inputs are structurally valid (booleans 0/1, nonzero distinct
 * counts). Used by Plan/Execute and independently by the host test. */
int AdrenoPowerUpV1InputsValid(const struct AdrenoPowerUpV1Inputs *in);

/* 1 iff *pre satisfies round-46 caller_v1_pre_ok() (cx && gx &&
 * clocks_enabled). With gxpd_present == 0 this is 0 BY DESIGN: the guarded
 * GX step was skipped, so the round-46 caller correctly refuses. Exposed so
 * the composition limit is testable rather than implicit. */
int AdrenoPowerUpV1PreIsReady(const struct AdrenoGmuCallerV1Pre *pre);

/* Step -> failure status mapping, and guarded-step predicate. Pure tables so
 * the host test can assert the contract without injecting failures. */
AdrenoPowerUpV1Status AdrenoPowerUpV1StepStatus(uint32_t step);
uint32_t AdrenoPowerUpV1StepGuarded(uint32_t step);

/* ---- Plan / Execute ------------------------------------------------------ */

/* Number of ops Plan emits for valid inputs; 0 for invalid inputs. */
uint32_t AdrenoPowerUpV1PlanSize(const struct AdrenoPowerUpV1Inputs *in);

/* Build the EXACT 5-op ordered sequence. ops capacity must be >= PlanSize.
 * Never emits hardware traffic. Returns Invalid on bad inputs/capacity. */
AdrenoPowerUpV1Status AdrenoPowerUpV1Plan(const struct AdrenoPowerUpV1Inputs *in,
    AdrenoPowerUpV1Op *ops, uint32_t capacity, uint32_t *count);

/* Execute the plan in order against the bus. On success fills *pre with
 * {cx_genpd_on, gx_genpd_on, clocks_enabled, require_gx} so it can be passed
 * straight to AdrenoGmuCallerV1Precheck/ColdBoot. On ANY failure: stops
 * immediately, rolls back acquired genpds in reverse order, zeroes the three
 * state fields of *pre, and returns the step-specific status (or
 * RollbackFailed if a genpd_off callback failed).
 *
 * fail_index (optional) receives the op index of the first failure, or
 * ADRENO_POWER_UP_V1_STEP_COUNT when no op failed (success sentinel). */
AdrenoPowerUpV1Status AdrenoPowerUpV1Execute(
    const struct AdrenoPowerUpV1Inputs *in,
    const AdrenoPowerUpV1Op *ops, uint32_t count,
    const struct AdrenoPowerUpV1Bus *bus,
    struct AdrenoGmuCallerV1Pre *pre, uint32_t *fail_index);

#ifdef __cplusplus
}
#endif
#endif /* ADRENO_POWER_UP_V1_H */
