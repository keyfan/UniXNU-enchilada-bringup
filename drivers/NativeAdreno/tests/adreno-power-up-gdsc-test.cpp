// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for AdrenoPowerUpGdsc: the COMPOSITION contract between the round-57
// derived GDSC step plans and the round-51 ordered power-up plan.
// Freestanding: no hardware, no IOKit, no firmware. HOST-VERIFIED only.
//
// Method 1 (this file) asserts the derived contract values, the guarded-bit
// invariant in both directions, the CX VOTABLE short-cut, the GX fail-safe, and
// that non-GDSC steps never claim a GDSC write.
// Method 2 is scripts/verify-enchilada-power-up-gdsc.py (source-text cross).
//
// Usage: run by scripts/check-enchilada-adreno-host.py.

#include <cstdio>
#include <cstdint>

extern "C" {
#include "AdrenoPowerUpGdsc.h"
#include "AdrenoPowerUpV1.h"
#include "AdrenoGdscDecode.h"
#include "AdrenoDomainState.h"
}

static unsigned g_checks = 0;
static unsigned g_failures = 0;

static void check(bool ok, const char *label)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        std::printf("  FAIL %s\n", label);
    }
}

/* ---- tests --------------------------------------------------------------- */

static void testContractValues()
{
    struct adreno_power_up_gdsc_contract c;

    check(AdrenoPowerUpGdscContract(&c) == 0, "contract builds");

    /* CX must do real work: ONE write (clear collapse), then a status READ.
     * Counting the poll as a write was this test's first catch -- keep the
     * distinction explicit because it is the difference between a write path
     * and a read path for a powered-down-adjacent block. */
    check(c.cx_enable_writes == 1u, "CX enable writes exactly 1 register");
    check(c.cx_enable_steps > c.cx_enable_writes,
          "CX enable plan also has non-writing steps");
    check(c.cx_poll_offset != 0u, "CX enable plan contains a status poll");

    /* GX must do NOTHING: gdsc_gx_do_nothing_enable() + no supply + no cxc. */
    check(c.gx_enable_writes == 0u, "GX enable writes 0 registers");
    check(c.gx_enable_steps == 0u, "GX enable plan is empty (no supply, no cxc)");

    /* The poll is read out of the derived plan, not asserted here. */
    check(c.cx_poll_offset ==
              (uint32_t)(ADRENO_DOMAIN_CX_STATUS - ADRENO_DOMAIN_GPUCC_BASE),
          "CX poll offset is the derived status register");
    check(c.cx_poll_offset == 0x1540u, "CX poll offset is 0x1540");
    check(c.cx_poll_mask == (1u << ADRENO_DOMAIN_CX_ON_BIT),
          "CX poll mask is the derived PWR_ON bit");
    check(c.cx_poll_mask == 0x80000000u, "CX poll mask is BIT(31)");

    /* NULL must fail closed rather than silently succeed. */
    check(AdrenoPowerUpGdscContract(nullptr) != 0, "NULL contract is refused");
}

static void testGuardMatchesWrites()
{
    struct adreno_power_up_gdsc_contract c;

    check(AdrenoPowerUpGdscContract(&c) == 0, "contract builds for guard test");

    /* The load-bearing round-58 invariant, evaluated live. */
    check(c.cx_step_guarded == 0u, "CX step is not guarded");
    check(c.gx_step_guarded == 1u, "GX step is guarded");
    check(AdrenoPowerUpGdscGuardMatchesWrites() == 1,
          "guarded bit agrees with the derived write counts");

    /* And it must agree with what AdrenoPowerUpV1 itself reports. */
    check(AdrenoPowerUpV1StepGuarded(ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON) == 0u,
          "AdrenoPowerUpV1 agrees CX is unguarded");
    check(AdrenoPowerUpV1StepGuarded(ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON) == 1u,
          "AdrenoPowerUpV1 agrees GX is guarded");

    /* The step-writer accessor must route per step, not per module. */
    check(AdrenoPowerUpGdscStepWrites(ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON) == 1u,
          "step 0 writes 1");
    check(AdrenoPowerUpGdscStepWrites(ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON) == 0u,
          "step 1 writes 0");
}

static void testDisableIsUnverified()
{
    struct adreno_power_up_gdsc_contract c;

    check(AdrenoPowerUpGdscContract(&c) == 0, "contract builds for disable test");

    /* CX is VOTABLE: gdsc.c:153-161 waits 500 us and polls nothing. */
    check(c.cx_disable_polls == 0u, "CX disable polls nothing");
    check(c.cx_disable_writes == 1u, "CX disable writes exactly the collapse bit");
    check(AdrenoPowerUpGdscDisableIsUnverified(0u) == 1,
          "CX power-off is UNVERIFIED by construction");

    /* GX is not VOTABLE: it polls the derived down-complete bit. */
    check(c.gx_disable_polls >= 1u, "GX disable does poll");
    check(AdrenoPowerUpGdscDisableIsUnverified(1u) == 0,
          "GX power-off IS verified by polling");
    check(c.gx_disable_writes >= 1u, "GX disable writes registers");

    /* An out-of-range domain must not read as verified. */
    check(AdrenoPowerUpGdscDisableIsUnverified(7u) == 0,
          "unknown domain reports not-unverified rather than claiming safety");
}

static void testNonGdscStepsWriteNothing()
{
    check(AdrenoPowerUpGdscStepWrites(ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE) == 0u,
          "core-rate step performs no GDSC write");
    check(AdrenoPowerUpGdscStepWrites(ADRENO_POWER_UP_V1_STEP_SET_HUB_RATE) == 0u,
          "hub-rate step performs no GDSC write");
    check(AdrenoPowerUpGdscStepWrites(ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE) == 0u,
          "clock-enable step performs no GDSC write");
    check(AdrenoPowerUpGdscStepWrites(99u) == 0u,
          "an out-of-range step index writes nothing");
}

static void testVerifyAggregate()
{
    uint32_t n = 0u;
    int rc = AdrenoPowerUpGdscVerify(&n);

    check(rc == 0, "aggregate verify passes");
    check(n >= 16u, "aggregate verify evaluated at least 16 checks");
    std::printf("  [aggregate verify: %d failures, %u checks]\n",
                rc, static_cast<unsigned>(n));

    /* NULL out-param must not crash. (When the contract is intact this also
     * returns success; a non-zero return here would mean a real contract
     * failure, which the earlier check already reports.) */
    check(AdrenoPowerUpGdscVerify(nullptr) == 0, "aggregate verify tolerates NULL");

    /* The composition must not have broken the round-57 property. */
    check(adreno_gdsc_verify_gx_enable_writes_nothing() == 1,
          "round-57 GX zero-write property still holds");
    check(adreno_gdsc_verify_domain_state_constants() == 1,
          "round-56 domain-state derivation still holds");
}

static void testPowerUpPlanStillIntact()
{
    struct AdrenoPowerUpV1Inputs in;
    AdrenoPowerUpV1Op ops[ADRENO_POWER_UP_V1_STEP_COUNT];
    uint32_t count = 0u;

    in.gxpd_present = 0u;
    in.gmu_clock_count = AdrenoPowerUpV1BoardGmuClockCount();
    in.gpu_clock_count = AdrenoPowerUpV1BoardGpuClockCount();
    in.a740_family = AdrenoPowerUpV1BoardIsA740Family();
    in.strict_gx = 0u;

    check(AdrenoPowerUpV1Plan(&in, ops, ADRENO_POWER_UP_V1_STEP_COUNT,
                              &count) == kAdrenoPowerUpV1Ok,
          "round-51 plan still builds");
    check(count == ADRENO_POWER_UP_V1_STEP_COUNT, "round-51 plan is 5 steps");
    check(ops[0].step == ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON,
          "step 0 is still CX genpd");
    check(ops[1].step == ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON,
          "step 1 is still GX genpd");
    /* The guarded bits in the PLAN must equal the ones the contract derives. */
    check(ops[0].guarded == 0u && ops[1].guarded == 1u,
          "plan guarded bits match the contract");
}

int main()
{
    std::printf("enchilada Adreno power-up/GDSC composition host tests\n");

    testContractValues();
    std::printf("  contract values: %u checks\n", g_checks);
    testGuardMatchesWrites();
    std::printf("  guard/writes invariant: %u checks\n", g_checks);
    testDisableIsUnverified();
    std::printf("  disable verification: %u checks\n", g_checks);
    testNonGdscStepsWriteNothing();
    std::printf("  non-GDSC steps: %u checks\n", g_checks);
    testVerifyAggregate();
    std::printf("  aggregate: %u checks\n", g_checks);
    testPowerUpPlanStillIntact();
    std::printf("  power-up plan intact: %u checks\n", g_checks);

    if (g_failures != 0) {
        std::printf("FAIL: %u of %u checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS: enchilada Adreno power-up/GDSC composition host tests "
                "(%u checks)\n", g_checks);
    return 0;
}
