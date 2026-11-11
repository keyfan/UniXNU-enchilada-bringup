// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for AdrenoPowerUpV1: the enchilada A6XX_GEN1 (A630) ordered
// genpd + clock power-up sequence modeled on Linux v6.12 a6xx_gmu_resume().
// Freestanding: a recorder bus logs every callback and can inject a failure
// at any op. No hardware, no IOKit, no firmware. HOST-VERIFIED only.
//
// Method 1 (this file): compiles the C module with the sanitizers and asserts
// the plan, execution order, guarded GX branch, rates, clock-count
// anti-conflation, per-step failure statuses, reverse-order rollback, and the
// composition with round-46's REAL AdrenoGmuCallerV1Precheck.
// Method 2 is scripts/verify-enchilada-power-up-v1.py (source-text cross).
//
// Usage: run by scripts/check-enchilada-adreno-host.py.

#include <cstdio>
#include <cstdint>

extern "C" {
#include "AdrenoPowerUpV1.h"
#include "AdrenoGmuCallerV1.h"
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

/* ---- recorder bus --------------------------------------------------------- */

#define LOG_CAP 32
struct Rec {
    char kind[LOG_CAP];
    uint32_t which;
    uint32_t value;
};

struct Sim {
    Rec log[LOG_CAP];
    unsigned n;
    /* Fail at op index fail_at (0..4); -1 = never. */
    int fail_at;
    /* Make genpd_off return non-zero to test RollbackFailed. */
    int fail_off;
    unsigned op_index;      /* current op being executed (host-side mirror) */
};

static void log_push(Sim *s, const char *kind, uint32_t which, uint32_t value)
{
    if (s->n < LOG_CAP) {
        Rec *r = &s->log[s->n++];
        std::snprintf(r->kind, sizeof(r->kind), "%s", kind);
        r->which = which;
        r->value = value;
    }
}

static int sim_should_fail(Sim *s)
{
    return s->fail_at >= 0 && (unsigned)s->fail_at == s->op_index;
}

/* Evaluate the injected failure against the CURRENT op index, THEN advance.
 * Checking after the increment would never match fail_at (host-test defect
 * caught by review before the first run). */
static int cb_genpd_on(void *cookie, uint32_t which)
{
    Sim *s = (Sim *)cookie;
    int rc = sim_should_fail(s) ? 1 : 0;
    log_push(s, "on", which, 0u);
    s->op_index++;
    return rc;
}

static int cb_genpd_off(void *cookie, uint32_t which)
{
    Sim *s = (Sim *)cookie;
    log_push(s, "off", which, 0u);
    return s->fail_off ? 1 : 0;
}

static int cb_set_rate(void *cookie, uint32_t which, uint32_t hz)
{
    Sim *s = (Sim *)cookie;
    int rc = sim_should_fail(s) ? 1 : 0;
    log_push(s, "rate", which, hz);
    s->op_index++;
    return rc;
}

static int cb_clocks_enable(void *cookie, uint32_t count)
{
    Sim *s = (Sim *)cookie;
    int rc = sim_should_fail(s) ? 1 : 0;
    log_push(s, "clocks", 0u, count);
    s->op_index++;
    return rc;
}

static AdrenoPowerUpV1Bus make_bus(Sim *s)
{
    AdrenoPowerUpV1Bus b;
    b.cookie = s;
    b.genpd_on = cb_genpd_on;
    b.genpd_off = cb_genpd_off;
    b.set_rate = cb_set_rate;
    b.clocks_enable = cb_clocks_enable;
    return b;
}

static Sim make_sim(int fail_at = -1, int fail_off = 0)
{
    Sim s;
    s.n = 0;
    s.fail_at = fail_at;
    s.fail_off = fail_off;
    s.op_index = 0;
    return s;
}

/* NOTE: fail_at counts EXECUTED callbacks, so failure injection is only used
 * on the gxpd_present=1 path (all 5 ops execute). The guarded-skip group uses
 * make_sim() with fail_at=-1, so no index alignment is needed there. */
static AdrenoPowerUpV1Inputs board_inputs(uint32_t gxpd_present)
{
    AdrenoPowerUpV1Inputs in;
    in.gxpd_present = gxpd_present;
    in.gmu_clock_count = AdrenoPowerUpV1BoardGmuClockCount();
    in.gpu_clock_count = AdrenoPowerUpV1BoardGpuClockCount();
    in.a740_family = AdrenoPowerUpV1BoardIsA740Family();
    in.strict_gx = 0u;   /* recommended for enchilada: GX stays off */
    return in;
}

/* Manual kind comparison (keeps the test free of <string> / libc++ deps). */
static int kind_eq(const char *a, const char *b)
{
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}

static bool log_at(const Sim *s, unsigned i, const char *kind,
                   uint32_t which, uint32_t value)
{
    if (i >= s->n) return false;
    const Rec *r = &s->log[i];
    return kind_eq(r->kind, kind) && r->which == which && r->value == value;
}

/* ---- GPUCC synthetic reader for the round-46 Precheck composition --------- */
struct GpuccSim {
    uint32_t cx;
    uint32_t gx;
};

static int gpucc_read(void *cookie, uint32_t byte_offset, uint32_t *out)
{
    GpuccSim *g = (GpuccSim *)cookie;
    if (byte_offset == ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE) {
        *out = g->cx;
        return 0;
    }
    if (byte_offset == ADRENO_GMU_CALLER_V1_GX_STATUS_BYTE) {
        *out = g->gx;
        return 0;
    }
    return 1;
}

/* ---- test groups ---------------------------------------------------------- */

static void group_board_constants()
{
    check(AdrenoPowerUpV1BoardGmuClockCount() == 4u,
          "board GMU clock count = 4 (enchilada.dts:23890)");
    check(AdrenoPowerUpV1BoardGpuClockCount() == 6u,
          "board kgsl-3d0 clock count = 6 (enchilada.dts:23657)");
    check(AdrenoPowerUpV1BoardIsA740Family() == 0u,
          "A630 is NOT A740 family (a6xx_catalog.c:884 vs :1328)");
    check(AdrenoPowerUpV1CoreRateHz() == 200000000u,
          "core rate 200 MHz (a6xx_gmu.c:1044)");
    check(AdrenoPowerUpV1HubRateHz(0u) == 150000000u,
          "A630 hub rate 150 MHz (a6xx_gmu.c:1045-1046 non-A740)");
    check(AdrenoPowerUpV1HubRateHz(1u) == 200000000u,
          "A740 family hub rate 200 MHz");
    check(AdrenoPowerUpV1CountsAreDistinct(4u, 6u) == 1,
          "4 vs 6 clock counts are distinct (anti-conflation)");
    check(AdrenoPowerUpV1CountsAreDistinct(4u, 4u) == 0,
          "equal counts are rejected");
    check(AdrenoPowerUpV1CountsAreDistinct(0u, 6u) == 0,
          "zero GMU count is rejected");
    check(AdrenoPowerUpV1CountsAreDistinct(4u, 0u) == 0,
          "zero GPU count is rejected");
}

static void group_inputs_valid()
{
    AdrenoPowerUpV1Inputs in = board_inputs(1u);
    check(AdrenoPowerUpV1InputsValid(&in) == 1, "board inputs valid");
    check(AdrenoPowerUpV1InputsValid(nullptr) == 0, "NULL inputs invalid");
    in.gxpd_present = 2u;
    check(AdrenoPowerUpV1InputsValid(&in) == 0, "gxpd_present must be boolean");
    in = board_inputs(1u);
    in.a740_family = 7u;
    check(AdrenoPowerUpV1InputsValid(&in) == 0, "a740_family must be boolean");
    in = board_inputs(1u);
    in.strict_gx = 3u;
    check(AdrenoPowerUpV1InputsValid(&in) == 0, "strict_gx must be boolean");
    in = board_inputs(1u);
    in.gmu_clock_count = in.gpu_clock_count;   /* conflate the two DTB nodes */
    check(AdrenoPowerUpV1InputsValid(&in) == 0,
          "conflating GMU(4) and GPU(6) clock counts fails closed");
    in = board_inputs(1u);
    in.gmu_clock_count = 0u;
    check(AdrenoPowerUpV1InputsValid(&in) == 0, "zero GMU count invalid");
}

static void group_plan()
{
    AdrenoPowerUpV1Inputs in = board_inputs(1u);
    AdrenoPowerUpV1Op ops[8];
    uint32_t count = 0u;
    check(AdrenoPowerUpV1PlanSize(&in) == 5u, "plan size = 5");
    check(AdrenoPowerUpV1Plan(&in, ops, 8u, &count) == kAdrenoPowerUpV1Ok,
          "plan builds");
    check(count == 5u, "plan emits 5 ops");
    check(ops[0].step == ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON &&
          ops[0].guarded == 0u, "op0 = CX genpd, unguarded");
    check(ops[1].step == ADRENO_POWER_UP_V1_STEP_GX_GENPD_ON &&
          ops[1].guarded == 1u, "op1 = GX genpd, GUARDED");
    check(ops[2].step == ADRENO_POWER_UP_V1_STEP_SET_CORE_RATE &&
          ops[2].value == 200000000u, "op2 = core 200 MHz");
    check(ops[3].step == ADRENO_POWER_UP_V1_STEP_SET_HUB_RATE &&
          ops[3].value == 150000000u, "op3 = hub 150 MHz (A630)");
    check(ops[4].step == ADRENO_POWER_UP_V1_STEP_CLOCKS_ENABLE &&
          ops[4].value == 4u, "op4 = clk_bulk_prepare_enable(4 GMU clocks)");
    check(AdrenoPowerUpV1StepGuarded(1u) == 1u &&
          AdrenoPowerUpV1StepGuarded(0u) == 0u &&
          AdrenoPowerUpV1StepGuarded(4u) == 0u,
          "guarded predicate only true for the GX step");
    check(AdrenoPowerUpV1StepStatus(0u) == kAdrenoPowerUpV1CxFailed &&
          AdrenoPowerUpV1StepStatus(1u) == kAdrenoPowerUpV1GxFailed &&
          AdrenoPowerUpV1StepStatus(2u) == kAdrenoPowerUpV1CoreRateFailed &&
          AdrenoPowerUpV1StepStatus(3u) == kAdrenoPowerUpV1HubRateFailed &&
          AdrenoPowerUpV1StepStatus(4u) == kAdrenoPowerUpV1ClocksFailed &&
          AdrenoPowerUpV1StepStatus(99u) == kAdrenoPowerUpV1Invalid,
          "step -> failure status table");
    /* A740-family inputs select the 200 MHz hub in the plan. */
    in.a740_family = 1u;
    check(AdrenoPowerUpV1Plan(&in, ops, 8u, &count) == kAdrenoPowerUpV1Ok &&
          ops[3].value == 200000000u,
          "A740-family plan selects 200 MHz hub");
    check(AdrenoPowerUpV1Plan(&in, ops, 4u, &count) == kAdrenoPowerUpV1Invalid,
          "insufficient plan capacity fails closed");
    check(AdrenoPowerUpV1Plan(&in, nullptr, 8u, &count) == kAdrenoPowerUpV1Invalid,
          "NULL ops fails closed");
    AdrenoPowerUpV1Inputs bad = board_inputs(1u);
    bad.gmu_clock_count = bad.gpu_clock_count;
    check(AdrenoPowerUpV1PlanSize(&bad) == 0u,
          "invalid inputs => zero-size plan");
}

static void group_execute_success()
{
    AdrenoPowerUpV1Inputs in = board_inputs(1u);
    AdrenoPowerUpV1Op ops[8];
    uint32_t count = 0u, fail_index = 999u;
    Sim s = make_sim();
    AdrenoPowerUpV1Bus bus = make_bus(&s);
    struct AdrenoGmuCallerV1Pre pre;
    check(AdrenoPowerUpV1Plan(&in, ops, 8u, &count) == kAdrenoPowerUpV1Ok,
          "plan for success run");
    AdrenoPowerUpV1Status st = AdrenoPowerUpV1Execute(
        &in, ops, count, &bus, &pre, &fail_index);
    check(st == kAdrenoPowerUpV1Ok, "execute returns Ok");
    check(fail_index == ADRENO_POWER_UP_V1_STEP_COUNT,
          "success sentinel fail_index = STEP_COUNT");
    check(s.n == 5u, "exactly 5 bus callbacks (no MMIO)");
    check(log_at(&s, 0, "on", 0u, 0u), "cb0: CX genpd_on");
    check(log_at(&s, 1, "on", 1u, 0u), "cb1: GX genpd_on");
    check(log_at(&s, 2, "rate", 0u, 200000000u), "cb2: core 200 MHz");
    check(log_at(&s, 3, "rate", 1u, 150000000u), "cb3: hub 150 MHz");
    check(log_at(&s, 4, "clocks", 0u, 4u), "cb4: enable 4 GMU clocks");
    check(pre.cx_genpd_on == 1u && pre.gx_genpd_on == 1u &&
          pre.clocks_enabled == 1u && pre.require_gx == 0u,
          "pre filled for round-46 caller");
    check(AdrenoPowerUpV1PreIsReady(&pre) == 1,
          "pre satisfies round-46 caller_v1_pre_ok()");
    check(AdrenoPowerUpV1PreIsReady(nullptr) == 0,
          "NULL pre is not ready");
}

static void group_execute_guarded_skip()
{
    /* gxpd_present == 0: the guarded GX step is SKIPPED, not failed. */
    AdrenoPowerUpV1Inputs in = board_inputs(0u);
    AdrenoPowerUpV1Op ops[8];
    uint32_t count = 0u, fail_index = 999u;
    Sim s = make_sim();
    AdrenoPowerUpV1Bus bus = make_bus(&s);
    struct AdrenoGmuCallerV1Pre pre;
    AdrenoPowerUpV1Plan(&in, ops, 8u, &count);
    AdrenoPowerUpV1Status st = AdrenoPowerUpV1Execute(
        &in, ops, count, &bus, &pre, &fail_index);
    check(st == kAdrenoPowerUpV1Ok, "guarded skip still returns Ok");
    check(s.n == 4u, "only 4 callbacks: GX genpd_on skipped");
    check(log_at(&s, 0, "on", 0u, 0u) &&
          log_at(&s, 1, "rate", 0u, 200000000u) &&
          log_at(&s, 2, "rate", 1u, 150000000u) &&
          log_at(&s, 3, "clocks", 0u, 4u),
          "CX -> core -> hub -> clocks (no GX)");
    check(pre.cx_genpd_on == 1u && pre.gx_genpd_on == 0u &&
          pre.clocks_enabled == 1u,
          "pre.gx_genpd_on = 0 (honest: refcount NOT acquired)");
    check(AdrenoPowerUpV1PreIsReady(&pre) == 0,
          "composition limit: round-46 caller refuses (NotReady)");
    check(fail_index == ADRENO_POWER_UP_V1_STEP_COUNT,
          "no failure recorded for a skipped guarded op");
}

static void group_failures()
{
    const AdrenoPowerUpV1Status expect[5] = {
        kAdrenoPowerUpV1CxFailed, kAdrenoPowerUpV1GxFailed,
        kAdrenoPowerUpV1CoreRateFailed, kAdrenoPowerUpV1HubRateFailed,
        kAdrenoPowerUpV1ClocksFailed
    };
    for (uint32_t f = 0u; f < 5u; f++) {
        AdrenoPowerUpV1Inputs in = board_inputs(1u);
        AdrenoPowerUpV1Op ops[8];
        uint32_t count = 0u, fail_index = 999u;
        Sim s = make_sim((int)f);
        AdrenoPowerUpV1Bus bus = make_bus(&s);
        struct AdrenoGmuCallerV1Pre pre;
        AdrenoPowerUpV1Plan(&in, ops, 8u, &count);
        AdrenoPowerUpV1Status st = AdrenoPowerUpV1Execute(
            &in, ops, count, &bus, &pre, &fail_index);
        check(st == expect[f], "per-step failure status");
        check(fail_index == f, "first_fail op index");
        check(pre.cx_genpd_on == 0u && pre.gx_genpd_on == 0u &&
              pre.clocks_enabled == 0u, "pre zeroed after failure");
        /* Rollback: reverse acquisition order, ONLY genpds actually acquired.
         * f=0 CX failed -> none acquired -> no off.
         * f=1 GX failed -> only CX acquired -> one off(0).
         * f>=2 both acquired -> off(1) then off(0), reverse order. */
        if (f == 0u) {
            check(s.n >= 1u && !log_at(&s, s.n - 1u, "off", 0u, 0u),
                  "CX-failed: nothing acquired => no rollback off");
        } else if (f == 1u) {
            check(log_at(&s, s.n - 1u, "off", 0u, 0u) &&
                  !log_at(&s, s.n - 2u, "off", 1u, 0u),
                  "GX-failed: only CX rolled back (GX never acquired)");
        } else {
            check(log_at(&s, s.n - 2u, "off", 1u, 0u) &&
                  log_at(&s, s.n - 1u, "off", 0u, 0u),
                  "rollback GX then CX (reverse order)");
        }
        /* No clocks/rates are undone (upstream :1048-1052 puts genpds only). */
        unsigned undone = 0u;
        for (unsigned i = 0; i < s.n; i++) {
            if (kind_eq(s.log[i].kind, "rate") && s.log[i].which == 2u) undone++;
        }
        check(undone == 0u, "no clock-rate undo callbacks emitted");
    }
    /* genpd_off failure -> RollbackFailed. */
    {
        AdrenoPowerUpV1Inputs in = board_inputs(1u);
        AdrenoPowerUpV1Op ops[8];
        uint32_t count = 0u;
        Sim s = make_sim(4, 1);   /* fail clocks_enable, fail genpd_off */
        AdrenoPowerUpV1Bus bus = make_bus(&s);
        struct AdrenoGmuCallerV1Pre pre;
        AdrenoPowerUpV1Plan(&in, ops, 8u, &count);
        check(AdrenoPowerUpV1Execute(&in, ops, count, &bus, &pre, nullptr)
                  == kAdrenoPowerUpV1RollbackFailed,
              "genpd_off failure -> RollbackFailed");
    }
    /* Invalid execute arguments. */
    {
        AdrenoPowerUpV1Inputs in = board_inputs(1u);
        AdrenoPowerUpV1Op ops[8];
        uint32_t count = 0u;
        Sim s = make_sim();
        AdrenoPowerUpV1Bus bus = make_bus(&s);
        struct AdrenoGmuCallerV1Pre pre;
        AdrenoPowerUpV1Plan(&in, ops, 8u, &count);
        check(AdrenoPowerUpV1Execute(&in, ops, 4u, &bus, &pre, nullptr)
                  == kAdrenoPowerUpV1Invalid, "wrong op count -> Invalid");
        check(AdrenoPowerUpV1Execute(&in, nullptr, count, &bus, &pre, nullptr)
                  == kAdrenoPowerUpV1Invalid, "NULL ops -> Invalid");
        check(AdrenoPowerUpV1Execute(&in, ops, count, nullptr, &pre, nullptr)
                  == kAdrenoPowerUpV1Invalid, "NULL bus -> Invalid");
        check(AdrenoPowerUpV1Execute(&in, ops, count, &bus, nullptr, nullptr)
                  == kAdrenoPowerUpV1Invalid, "NULL pre -> Invalid");
        AdrenoPowerUpV1Inputs bad = in; bad.gmu_clock_count = bad.gpu_clock_count;
        check(AdrenoPowerUpV1Execute(&bad, ops, count, &bus, &pre, nullptr)
                  == kAdrenoPowerUpV1Invalid, "invalid inputs -> Invalid");
    }
}

static void group_composition()
{
    /* Drive the REAL round-46 AdrenoGmuCallerV1Precheck with the produced
     * pre + synthetic GPUCC status words. Proves the two modules compose and
     * that the model does NOT claim a powered GPU on real (cx-DOWN) silicon. */
    AdrenoPowerUpV1Inputs in = board_inputs(1u);
    AdrenoPowerUpV1Op ops[8];
    uint32_t count = 0u;
    Sim s = make_sim();
    AdrenoPowerUpV1Bus bus = make_bus(&s);
    struct AdrenoGmuCallerV1Pre pre;
    AdrenoPowerUpV1Plan(&in, ops, 8u, &count);
    AdrenoPowerUpV1Execute(&in, ops, count, &bus, &pre, nullptr);

    struct AdrenoGmuCallerV1Gpucc gpucc;
    GpuccSim g;
    gpucc.cookie = &g;
    gpucc.read = gpucc_read;

    /* Device-measured round-35 word: cx=0x00005ff4 (CX down), gx=0x00088400
     * (gx_off=1). Even a fully-executed power-up PLAN cannot change silicon
     * that did not actually receive the genpd/clock writes. */
    g.cx = 0x00005ff4u;
    g.gx = 0x00088400u;
    check(AdrenoGmuCallerV1Precheck(&pre, &gpucc) == kAdrenoGmuCallerV1CxOff,
          "device-realistic CX-down word -> caller CxOff (no false GPU claim)");

    /* Hypothetical powered CX + GX-off (expected on enchilada): strict_gx=0
     * accepts, strict_gx=1 refuses. CX on = BIT(31), GX down = BIT(15). */
    g.cx = (1u << 31);
    g.gx = (1u << 15);
    check(AdrenoGmuCallerV1Precheck(&pre, &gpucc) == kAdrenoGmuCallerV1Ok,
          "CX up + GX off + require_gx=0 -> caller Ok (board-realistic)");
    in.strict_gx = 1u;
    AdrenoPowerUpV1Execute(&in, ops, count, &bus, &pre, nullptr);
    check(AdrenoGmuCallerV1Precheck(&pre, &gpucc) == kAdrenoGmuCallerV1GxOff,
          "require_gx=1 refuses GX-off hardware (fail-closed)");

    /* gxpd_present=0 path: caller refuses unconditionally (NotReady). */
    in = board_inputs(0u);
    s = make_sim();
    bus = make_bus(&s);
    AdrenoPowerUpV1Execute(&in, ops, count, &bus, &pre, nullptr);
    check(AdrenoGmuCallerV1Precheck(&pre, &gpucc) == kAdrenoGmuCallerV1NotReady,
          "guarded-skip path -> caller NotReady (composition limit)");
}

int main()
{
    group_board_constants();
    group_inputs_valid();
    group_plan();
    group_execute_success();
    group_execute_guarded_skip();
    group_failures();
    group_composition();
    std::printf("adreno-power-up-v1: %u checks, %u failures\n",
                g_checks, g_failures);
    if (g_failures != 0u) {
        return 1;
    }
    std::printf("PASS: A6XX_GEN1 ordered genpd + clock power-up (7 test groups)\n");
    return 0;
}
