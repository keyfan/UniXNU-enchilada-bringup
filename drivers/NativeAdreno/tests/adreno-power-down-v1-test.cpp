// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for AdrenoPowerDownV1 -- the teardown half of the A630 GMU power
// sequence derived from a6xx_gmu_stop() (a6xx_gmu.c:1184-1217).
//
// Method 1 of 2 (method 2 is scripts/verify-enchilada-power-down-v1.py, which
// re-derives the SAME facts from the pinned upstream bytes in Python). This
// test asserts the plan shape, the ORDER, the :1189 gate, the guarded GX
// branch, per-step failure statuses, the deliberate ABSENCE of rollback, the
// clearing of *pre, and the composition with round 51 (power-up) and round 58
// (GDSC contract).
//
// Build: compiled as C++17 and linked against AdrenoPowerDownV1.c,
// AdrenoPowerUpV1.c, AdrenoPowerUpGdsc.c, AdrenoGdscDecode.c,
// AdrenoDomainState.c (see scripts/check-enchilada-adreno-host.py).

extern "C" {
#include "AdrenoPowerDownV1.h"
#include "AdrenoPowerUpV1.h"
#include "AdrenoPowerUpGdsc.h"
}

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

unsigned g_checks = 0;
unsigned g_failures = 0;

void check(bool cond, const char *what)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        std::printf("FAIL: %s\n", what);
    }
}

// ---- A recording bus -------------------------------------------------------
//
// LOG_CAP is deliberately larger than any plan so a runaway Execute() cannot
// write out of bounds; overflow is recorded rather than asserted mid-run so a
// failure shows up as a check instead of a crash.
constexpr unsigned LOG_CAP = 8;

struct Rec {
    char kind[8];
    std::uint32_t which;
    std::uint32_t value;
};

struct Sim {
    Rec log[LOG_CAP];
    unsigned n;
    unsigned overflow;
    int fail_at; // 0-based op index to fail on, -1 never
    unsigned op_index;
};

void log_push(Sim *s, const char *kind, std::uint32_t which, std::uint32_t value)
{
    if (s->n >= LOG_CAP) {
        s->overflow++;
        return;
    }
    std::memset(&s->log[s->n], 0, sizeof(s->log[s->n]));
    std::snprintf(s->log[s->n].kind, sizeof(s->log[s->n].kind), "%s", kind);
    s->log[s->n].which = which;
    s->log[s->n].value = value;
    s->n++;
}

bool should_fail(Sim *s)
{
    if (s->fail_at >= 0 && static_cast<unsigned>(s->fail_at) == s->op_index) {
        s->op_index++;
        return true;
    }
    s->op_index++;
    return false;
}

int sim_genpd_off(void *cookie, std::uint32_t which)
{
    Sim *s = static_cast<Sim *>(cookie);

    log_push(s, "off", which, 0);
    return should_fail(s) ? 1 : 0;
}

int sim_clocks_disable(void *cookie, std::uint32_t count)
{
    Sim *s = static_cast<Sim *>(cookie);

    log_push(s, "clk", 0, count);
    return should_fail(s) ? 1 : 0;
}

struct Sim reset_sim()
{
    Sim s;

    std::memset(&s, 0, sizeof(s));
    s.fail_at = -1;
    return s;
}

struct AdrenoPowerUpV1Inputs board_inputs()
{
    struct AdrenoPowerUpV1Inputs in;

    in.gxpd_present = 1u;
    in.gmu_clock_count = 4u;
    in.gpu_clock_count = 6u;
    in.a740_family = 0u;
    in.strict_gx = 0u;
    return in;
}

struct AdrenoPowerDownV1Inputs down_inputs()
{
    struct AdrenoPowerDownV1Inputs in;

    in.gxpd_present = 1u;
    in.gmu_active = 1u;
    in.gmu_clock_count = 4u;
    return in;
}

struct AdrenoGmuCallerV1Pre ready_pre()
{
    struct AdrenoGmuCallerV1Pre pre;

    std::memset(&pre, 0, sizeof(pre));
    pre.cx_genpd_on = 1u;
    pre.gx_genpd_on = 1u;
    pre.clocks_enabled = 1u;
    pre.require_gx = 0u;
    return pre;
}

// ---- Group 1: the plan shape and its ORDER --------------------------------
void testPlanOrder()
{
    struct AdrenoPowerDownV1Inputs in = down_inputs();
    AdrenoPowerDownV1Op ops[ADRENO_POWER_DOWN_V1_STEP_COUNT];
    std::uint32_t count = 0u;

    check(AdrenoPowerDownV1PlanSize(&in) == ADRENO_POWER_DOWN_V1_STEP_COUNT,
          "plan size is STEP_COUNT for valid inputs");
    check(AdrenoPowerDownV1Plan(&in, ops, ADRENO_POWER_DOWN_V1_STEP_COUNT,
                                &count) == kAdrenoPowerDownV1Ok,
          "plan succeeds");
    check(count == 3u, "plan emits exactly 3 ops");

    // THE round-59 finding, position by position. Upstream a6xx_gmu_stop
    // :1204-1214 puts GX off FIRST, then clocks, then CX last.
    check(ops[0].step == ADRENO_POWER_DOWN_V1_STEP_GX_GENPD_OFF,
          "op 0 is GX off (:1209-1210)");
    check(ops[1].step == ADRENO_POWER_DOWN_V1_STEP_CLOCKS_DISABLE,
          "op 1 is clocks disable (:1212)");
    check(ops[2].step == ADRENO_POWER_DOWN_V1_STEP_CX_GENPD_OFF,
          "op 2 is CX off, LAST (:1214)");

    check(ops[0].guarded == 1u, "GX op is guarded");
    check(ops[1].guarded == 0u && ops[2].guarded == 0u,
          "clock and CX ops are unconditional");
    check(ops[1].value == 4u, "clock op carries gmu_clock_count");
}

// ---- Group 2: teardown is NOT power-up reversed ---------------------------
void testNotTheReverse()
{
    struct AdrenoPowerUpV1Inputs pu = board_inputs();
    AdrenoPowerUpV1Op pops[ADRENO_POWER_UP_V1_STEP_COUNT];
    std::uint32_t pcount = 0u;
    struct AdrenoPowerDownV1Inputs in = down_inputs();
    AdrenoPowerDownV1Op ops[ADRENO_POWER_DOWN_V1_STEP_COUNT];
    std::uint32_t count = 0u;

    check(AdrenoPowerUpV1Plan(&pu, pops, ADRENO_POWER_UP_V1_STEP_COUNT,
                              &pcount) == kAdrenoPowerUpV1Ok,
          "round-51 power-up plan still builds");
    check(pcount == 5u, "power-up plan still has 5 ops");
    check(pops[0].step == ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON,
          "power-up starts with CX (the opposite of teardown)");

    check(AdrenoPowerDownV1Plan(&in, ops, ADRENO_POWER_DOWN_V1_STEP_COUNT,
                                &count) == kAdrenoPowerDownV1Ok,
          "teardown plan builds");

    // A naive reverse of [CX, GX, core, hub, clocks] would begin with clocks.
    check(ops[0].step != ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE,
          "teardown does NOT begin with the clock step (not a reverse)");

    // NOTE (round-59 finding): the two step enums are SEPARATE NAMESPACES that
    // happen to collide numerically -- ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON and
    // ADRENO_POWER_DOWN_V1_STEP_GX_GENPD_OFF are both 0. So a direct
    // `ops[0].step != pops[0].step` comparison is a CATEGORY ERROR (it compares
    // an up-step against a down-step) and must NOT be used to argue ordering.
    // What is actually meaningful is the ORDER WITHIN each namespace, asserted
    // above position by position, plus the sizes being different.
    check(pops[0].step == ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON &&
              pops[1].step == ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON,
          "power-up order is CX then GX (opposite of teardown)");
    check(pops[ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE].step ==
              ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE,
          "power-up puts the clock step LAST; teardown puts it in the middle");
    check(count != pcount, "teardown has a different op count than power-up");
}

// ---- Group 3: the :1189 not-active gate ----------------------------------
void testNotActiveGate()
{
    struct AdrenoPowerDownV1Inputs in = down_inputs();
    AdrenoPowerDownV1Op ops[ADRENO_POWER_DOWN_V1_STEP_COUNT];
    struct AdrenoGmuCallerV1Pre pre = ready_pre();
    Sim sim = reset_sim();
    struct AdrenoPowerDownV1Bus bus;
    std::uint32_t count = 0u;
    std::uint32_t fail_index = 99u;

    in.gmu_active = 0u;
    bus.cookie = &sim;
    bus.genpd_off = sim_genpd_off;
    bus.clocks_disable = sim_clocks_disable;

    check(AdrenoPowerDownV1ShouldRun(&in) == 0,
          "ShouldRun is 0 when pm_runtime_active() is false");
    check(AdrenoPowerDownV1Plan(&in, ops, ADRENO_POWER_DOWN_V1_STEP_COUNT,
                                &count) == kAdrenoPowerDownV1Ok,
          "plan still builds when inactive (shape is input-independent)");
    check(AdrenoPowerDownV1Execute(&in, ops, count, &bus, &pre,
                                   &fail_index) == kAdrenoPowerDownV1Ok,
          "Execute returns Ok for an inactive GMU");
    check(sim.n == 0u, "no bus callback ran (:1189 early return)");
    check(fail_index == ADRENO_POWER_DOWN_V1_STEP_COUNT,
          "fail_index stays at the success sentinel");
    check(pre.cx_genpd_on == 0u && pre.gx_genpd_on == 0u &&
              pre.clocks_enabled == 0u,
          "*pre was cleared even on the no-op path");
}

// ---- Group 4: execution ORDER against a recording bus --------------------
void testExecuteOrder()
{
    struct AdrenoPowerDownV1Inputs in = down_inputs();
    AdrenoPowerDownV1Op ops[ADRENO_POWER_DOWN_V1_STEP_COUNT];
    struct AdrenoGmuCallerV1Pre pre = ready_pre();
    Sim sim = reset_sim();
    struct AdrenoPowerDownV1Bus bus;
    std::uint32_t count = 0u;
    std::uint32_t fail_index = 99u;

    bus.cookie = &sim;
    bus.genpd_off = sim_genpd_off;
    bus.clocks_disable = sim_clocks_disable;
    check(AdrenoPowerDownV1Plan(&in, ops, ADRENO_POWER_DOWN_V1_STEP_COUNT,
                                &count) == kAdrenoPowerDownV1Ok,
          "plan builds");

    check(AdrenoPowerDownV1Execute(&in, ops, count, &bus, &pre,
                                   &fail_index) == kAdrenoPowerDownV1Ok,
          "Execute succeeds");
    check(sim.overflow == 0u, "no callback overflow");
    check(sim.n == 3u, "exactly three callbacks ran");

    // GX (which=1) off, then clocks(4), then CX (which=0) off. Note the CX
    // put is LAST -- that is the hardware constraint.
    check(std::strcmp(sim.log[0].kind, "off") == 0 && sim.log[0].which == 1u,
          "first callback is genpd_off(GX=1)");
    check(std::strcmp(sim.log[1].kind, "clk") == 0 && sim.log[1].value == 4u,
          "second callback disables the 4 GMU clocks");
    check(std::strcmp(sim.log[2].kind, "off") == 0 && sim.log[2].which == 0u,
          "third and last callback is genpd_off(CX=0)");

    check(pre.cx_genpd_on == 0u, "pre.cx_genpd_on cleared");
    check(pre.gx_genpd_on == 0u, "pre.gx_genpd_on cleared");
    check(pre.clocks_enabled == 0u, "pre.clocks_enabled cleared");
    check(fail_index == ADRENO_POWER_DOWN_V1_STEP_COUNT,
          "fail_index is the success sentinel");
}

// ---- Group 5: guarded GX skip when gxpd is absent ------------------------
void testGuardedSkip()
{
    struct AdrenoPowerDownV1Inputs in = down_inputs();
    AdrenoPowerDownV1Op ops[ADRENO_POWER_DOWN_V1_STEP_COUNT];
    struct AdrenoGmuCallerV1Pre pre = ready_pre();
    Sim sim = reset_sim();
    struct AdrenoPowerDownV1Bus bus;
    std::uint32_t count = 0u;
    std::uint32_t fail_index = 99u;

    in.gxpd_present = 0u;
    bus.cookie = &sim;
    bus.genpd_off = sim_genpd_off;
    bus.clocks_disable = sim_clocks_disable;

    check(AdrenoPowerDownV1Plan(&in, ops, ADRENO_POWER_DOWN_V1_STEP_COUNT,
                                &count) == kAdrenoPowerDownV1Ok,
          "plan still emits all three ops without gxpd");
    check(AdrenoPowerDownV1Execute(&in, ops, count, &bus, &pre,
                                   &fail_index) == kAdrenoPowerDownV1Ok,
          "Execute succeeds without gxpd");
    check(sim.n == 2u, "the guarded GX op was skipped, not executed");
    check(std::strcmp(sim.log[0].kind, "clk") == 0,
          "clocks run first when GX is skipped");
    check(std::strcmp(sim.log[1].kind, "off") == 0 && sim.log[1].which == 0u,
          "CX is still turned off last");
}

// ---- Group 6: per-step failure statuses, and NO rollback -----------------
void testFailureStopsWithoutRollback()
{
    // Each failing op index must map to its documented status and stop the
    // sequence immediately. Crucially, nothing is re-powered: the module has
    // no genpd_on callback at all, so a "rollback" is structurally impossible.
    struct {
        int fail_at;
        AdrenoPowerDownV1Status want;
        unsigned want_ran;
        const char *what;
    } cases[] = {
        {0, kAdrenoPowerDownV1GxFailed, 1u, "GX failure -> GxFailed"},
        {1, kAdrenoPowerDownV1ClocksFailed, 2u, "clock failure -> ClocksFailed"},
        {2, kAdrenoPowerDownV1CxFailed, 3u, "CX failure -> CxFailed"},
    };

    for (unsigned i = 0u; i < 3u; i++) {
        struct AdrenoPowerDownV1Inputs in = down_inputs();
        AdrenoPowerDownV1Op ops[ADRENO_POWER_DOWN_V1_STEP_COUNT];
        struct AdrenoGmuCallerV1Pre pre = ready_pre();
        Sim sim = reset_sim();
        struct AdrenoPowerDownV1Bus bus;
        std::uint32_t count = 0u;
        std::uint32_t fail_index = 99u;
        AdrenoPowerDownV1Status st;

        sim.fail_at = cases[i].fail_at;
        bus.cookie = &sim;
        bus.genpd_off = sim_genpd_off;
        bus.clocks_disable = sim_clocks_disable;
        check(AdrenoPowerDownV1Plan(&in, ops, ADRENO_POWER_DOWN_V1_STEP_COUNT,
                                    &count) == kAdrenoPowerDownV1Ok,
              "plan builds in failure case");

        st = AdrenoPowerDownV1Execute(&in, ops, count, &bus, &pre,
                                      &fail_index);
        check(st == cases[i].want, cases[i].what);
        check(sim.n == cases[i].want_ran,
              "execution stopped at the failing op");
        check(fail_index == static_cast<std::uint32_t>(cases[i].fail_at),
              "fail_index names the failing op");
        // No rollback: the only callbacks that exist are off/disable, and *pre
        // is left cleared so the round-46 caller refuses.
        check(pre.cx_genpd_on == 0u && pre.gx_genpd_on == 0u &&
                  pre.clocks_enabled == 0u,
              "no rollback: *pre stays cleared after a failure");
    }
}

// ---- Group 7: invalid inputs / bad bus / bad capacity --------------------
void testInvalid()
{
    struct AdrenoPowerDownV1Inputs in = down_inputs();
    AdrenoPowerDownV1Op ops[ADRENO_POWER_DOWN_V1_STEP_COUNT];
    struct AdrenoGmuCallerV1Pre pre = ready_pre();
    Sim sim = reset_sim();
    struct AdrenoPowerDownV1Bus bus;
    std::uint32_t count = 0u;

    bus.cookie = &sim;
    bus.genpd_off = sim_genpd_off;
    bus.clocks_disable = sim_clocks_disable;

    check(AdrenoPowerDownV1Plan(&in, ops, count, &count) ==
              kAdrenoPowerDownV1Invalid,
          "capacity 0 is rejected");
    check(AdrenoPowerDownV1Plan(&in, nullptr, 3u, &count) ==
              kAdrenoPowerDownV1Invalid,
          "NULL ops is rejected");
    check(AdrenoPowerDownV1Plan(nullptr, ops, 3u, &count) ==
              kAdrenoPowerDownV1Invalid,
          "NULL inputs is rejected");

    in.gxpd_present = 2u;
    check(AdrenoPowerDownV1InputsValid(&in) == 0,
          "a non-boolean gxpd_present is rejected");
    in.gxpd_present = 1u;
    in.gmu_clock_count = 0u;
    check(AdrenoPowerDownV1InputsValid(&in) == 0,
          "a zero clock count is rejected");
    check(AdrenoPowerDownV1PlanSize(&in) == 0u,
          "plan size is 0 for invalid inputs");
    in.gmu_clock_count = 4u;

    check(AdrenoPowerDownV1Plan(&in, ops, ADRENO_POWER_DOWN_V1_STEP_COUNT,
                                &count) == kAdrenoPowerDownV1Ok,
          "plan builds again after fixing inputs");
    check(AdrenoPowerDownV1Execute(&in, ops, count, nullptr, &pre, nullptr) ==
              kAdrenoPowerDownV1Invalid,
          "NULL bus is rejected");
    check(AdrenoPowerDownV1Execute(&in, ops, count - 1u, &bus, &pre, nullptr) ==
              kAdrenoPowerDownV1Invalid,
          "a short plan count is rejected");

    {
        struct AdrenoPowerDownV1Bus partial;

        partial.cookie = &sim;
        partial.genpd_off = sim_genpd_off;
        partial.clocks_disable = nullptr;
        check(AdrenoPowerDownV1Execute(&in, ops, count, &partial, &pre,
                                       nullptr) == kAdrenoPowerDownV1Invalid,
              "a bus without clocks_disable is rejected");
    }
    check(sim.n == 0u, "no callback ran for any rejected call");
}

// ---- Group 8: composition with the round-58 GDSC contract ---------------
void testComposesWithGdscContract()
{
    struct adreno_power_up_gdsc_contract c;
    std::uint32_t checks = 0u;

    check(AdrenoPowerUpGdscContract(&c) == 0,
          "round-58 contract still builds");
    check(AdrenoPowerUpGdscGuardMatchesWrites() == 1,
          "round-58 guard/write invariant still holds");
    check(c.cx_enable_writes == 1u,
          "CX enable is still exactly 1 register write");

    // The asymmetry this module's header documents: GX-off is verifiable by
    // the CPU (it polls its down-complete bit), CX-off is not (VOTABLE).
    check(AdrenoPowerDownV1CxOffIsUnverified() == 1,
          "CX off is NOT verifiable by the CPU");
    check(AdrenoPowerUpGdscDisableIsUnverified(1u) == 0,
          "GX off IS verifiable by the CPU");

    check(AdrenoPowerDownV1Verify(&checks) == 0,
          "AdrenoPowerDownV1Verify passes");
    check(checks >= 16u, "AdrenoPowerDownV1Verify ran its full check list");
}

} // namespace

int main()
{
    testPlanOrder();
    std::printf("power-down-v1 plan order: %u checks, %u failures\n", g_checks,
                g_failures);

    testNotTheReverse();
    std::printf("power-down-v1 not-the-reverse: %u checks, %u failures\n",
                g_checks, g_failures);

    testNotActiveGate();
    std::printf("power-down-v1 not-active gate: %u checks, %u failures\n",
                g_checks, g_failures);

    testExecuteOrder();
    std::printf("power-down-v1 execute order: %u checks, %u failures\n",
                g_checks, g_failures);

    testGuardedSkip();
    std::printf("power-down-v1 guarded skip: %u checks, %u failures\n",
                g_checks, g_failures);

    testFailureStopsWithoutRollback();
    std::printf("power-down-v1 failure/no-rollback: %u checks, %u failures\n",
                g_checks, g_failures);

    testInvalid();
    std::printf("power-down-v1 invalid inputs: %u checks, %u failures\n",
                g_checks, g_failures);

    testComposesWithGdscContract();
    std::printf("power-down-v1 gdsc composition: %u checks, %u failures\n",
                g_checks, g_failures);

    if (g_failures != 0u) {
        std::printf("FAIL: enchilada Adreno power-down host tests "
                    "(%u checks, %u failures)\n", g_checks, g_failures);
        return 1;
    }
    std::printf("PASS: enchilada Adreno power-down host tests (%u checks)\n",
                g_checks);
    return 0;
}
