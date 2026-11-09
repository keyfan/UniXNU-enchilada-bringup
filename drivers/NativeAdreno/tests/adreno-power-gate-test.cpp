// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// HOST-VERIFIED host tests for the Round-22 GPU power-domain safety gate.
// No kernel, no MMIO, no firmware: this exercises only the pure-C gate logic
// and re-derives the board/upstream constants it encodes.

#include "AdrenoPowerGate.h"

#include <cstdio>
#include <cstring>

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                         #cond);                                               \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static void testColdStateForbidsMmio(void)
{
    /* THE load-bearing assertion: today's record says "read a safe register
     * on device". With no power-up, the gate MUST refuse. */
    struct adreno_power_state cold = adreno_power_current_state();
    CHECK(adreno_power_mmio_permitted(&cold) == 0);
    CHECK(adreno_power_mmio_permitted(NULL) == 0);
    CHECK(cold.cx_genpd_on == 0u);
    CHECK(cold.gx_genpd_on == 0u);
    CHECK(cold.clocks_enabled == 0u);
    CHECK(cold.kgsl_smmu_clock_on == 0u);
    CHECK(cold.gmu_fw_loaded == 0u);
    CHECK(cold.gmu_booted == 0u);
    CHECK(cold.hw_init_done == 0u);
    CHECK(adreno_power_steps_remaining(&cold) == ADRENO_POWER_STEP_COUNT);
    CHECK(adreno_power_steps_remaining(NULL) == ADRENO_POWER_STEP_COUNT);
}

static void testEachPreconditionIsMandatory(void)
{
    struct adreno_power_state full;
    std::memset(&full, 0, sizeof(full));
    full.cx_genpd_on = 1u;
    full.gx_genpd_on = 1u;
    full.clocks_enabled = 1u;
    full.kgsl_smmu_clock_on = 1u;
    full.gmu_fw_loaded = 1u;
    full.gmu_booted = 1u;
    full.hw_init_done = 1u;
    CHECK(adreno_power_mmio_permitted(&full) == 1);
    CHECK(adreno_power_steps_remaining(&full) == 0u);

    struct adreno_power_state s;
    s = full; s.cx_genpd_on = 0u;     CHECK(adreno_power_mmio_permitted(&s) == 0);
    s = full; s.gx_genpd_on = 0u;     CHECK(adreno_power_mmio_permitted(&s) == 0);
    s = full; s.clocks_enabled = 0u;  CHECK(adreno_power_mmio_permitted(&s) == 0);
    s = full; s.kgsl_smmu_clock_on = 0u; CHECK(adreno_power_mmio_permitted(&s) == 0);
    s = full; s.gmu_fw_loaded = 0u;   CHECK(adreno_power_mmio_permitted(&s) == 0);
    s = full; s.gmu_booted = 0u;      CHECK(adreno_power_mmio_permitted(&s) == 0);
    s = full; s.hw_init_done = 0u;    CHECK(adreno_power_mmio_permitted(&s) == 0);
    /* any missing step => all steps still required (board-level honesty) */
    s = full; s.hw_init_done = 0u;    CHECK(adreno_power_steps_remaining(&s) == ADRENO_POWER_STEP_COUNT);
}

static void testBoardPowerDomainFacts(void)
{
    /* The GPU/GMU/kgsl-SMMU windows are in the collapsible gpu_cx/gpu_gx
     * domain; apps_smmu is the always-on skip-init contrast. */
    CHECK(adreno_power_gpu_windows_are_gated() == 1);
    CHECK(adreno_power_apps_smmu_probe_permitted() == 1);
    CHECK(ADRENO_POWER_GPU_CX_PHANDLE == ADRENO_POWER_KGSL_SMMU_REGULATOR_PHANDLE);
    CHECK(ADRENO_POWER_GPU_CX_GDSC_ADDR == 0x0509106cull);
    CHECK(ADRENO_POWER_GPU_GX_GDSC_ADDR == 0x0509100cull);
    CHECK(ADRENO_POWER_GPU_CLOCK_COUNT == 6u);
    CHECK(ADRENO_POWER_KGSL_SMMU_CLOCK_COUNT == 1u);
    CHECK(ADRENO_POWER_MMIO_WINDOW_COUNT == 3u);
}

static void testLegacyGmuFacts(void)
{
    CHECK(adreno_power_is_legacy_gmu() == 1);
    CHECK(ADRENO_POWER_A630_IS_LEGACY_GMU == 1u);
    CHECK(ADRENO_POWER_GMU_HFI_VERSION_MAJOR == 1u);
    CHECK(ADRENO_POWER_SPT_PRAC_POWER_CONTROL_VALUE == 0x00778000u);
}

static void testOrderedStepModel(void)
{
    CHECK(ADRENO_POWER_STEP_COUNT == 9u);
    CHECK((uint32_t)kAdrenoPowerStepCxGenpdOn == 0u);
    CHECK((uint32_t)kAdrenoPowerStepHwInit == (ADRENO_POWER_STEP_COUNT - 1u));
    /* order is strictly increasing */
    CHECK(kAdrenoPowerStepCxGenpdOn < kAdrenoPowerStepGxGenpdOn);
    CHECK(kAdrenoPowerStepGxGenpdOn < kAdrenoPowerStepClocksEnable);
    CHECK(kAdrenoPowerStepClocksEnable < kAdrenoPowerStepRpmhStart);
    CHECK(kAdrenoPowerStepRpmhStart < kAdrenoPowerStepGmuFwLoad);
    CHECK(kAdrenoPowerStepGmuFwLoad < kAdrenoPowerStepGmuStart);
    CHECK(kAdrenoPowerStepGmuStart < kAdrenoPowerStepSptPracEnable);
    CHECK(kAdrenoPowerStepSptPracEnable < kAdrenoPowerStepHfiStart);
    CHECK(kAdrenoPowerStepHfiStart < kAdrenoPowerStepHwInit);
}

int main(void)
{
    testColdStateForbidsMmio();
    testEachPreconditionIsMandatory();
    testBoardPowerDomainFacts();
    testLegacyGmuFacts();
    testOrderedStepModel();
    if (g_failures != 0) {
        std::fprintf(stderr, "FAIL: enchilada GPU power-gate host tests (%d failures)\n", g_failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 GPU power-domain safety gate host tests (5 groups)\n");
    return 0;
}
