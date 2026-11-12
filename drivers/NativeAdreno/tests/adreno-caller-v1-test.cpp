// SPDX-License-Identifier: BSD-2-Clause-Patent
// Round 64: AdrenoCallerV1 -- the ordering layer.
//
// What this suite is actually checking, and what it deliberately cannot check:
//
//   * The ORDER invariant: the first MMIO attempt of a run is a READ, and an
//     unobservable window costs zero writes. This is checked by wiring the
//     caller to a window whose status reads fail.
//   * The SEPARATION invariant: "Up() ran" and "Precheck accepted" are two
//     different verdicts, and the report says which happened. Both directions
//     are exercised.
//   * The traffic invariant: the executor's accesses are counted by this layer,
//     i.e. there is exactly one choke point and the backend is behind it.
//
// It cannot check that clearing the CX collapse bit raises BIT(31) on the real
// part. Nothing on this host can. The CX "sticking" tests below model a silicon
// behaviour (a poll window that disagrees with the settled level) rather than
// asserting it exists -- see the comments at those Sims.
//
// The window is the REAL AdrenoGpuccWindowV1: every access goes through
// AdrenoGpuccWindowV1Resolve() first, exactly like an in-kernel caller would.
// The module under test is the ordering layer, so the fake hardware lives in the
// test and the production window keeps no read-side test hook.

#include <cstdio>
#include <cstring>
#include <cstdint>

extern "C" {
#include "AdrenoCallerV1.h"
#include "AdrenoGpuccWindowV1.h"
#include "AdrenoDomainState.h"
#include "AdrenoPowerUpV1.h"
}

namespace {

constexpr std::uint32_t kGpuccBytes = 0x9000u;

int gFailures = 0;
int gChecks = 0;

void check(bool cond, const char *what)
{
    gChecks += 1;
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        gFailures += 1;
    }
}

void checkEq(std::uint32_t got, std::uint32_t want, const char *what)
{
    gChecks += 1;
    if (got != want) {
        std::printf("  FAIL: %s (got 0x%x, want 0x%x)\n", what, got, want);
        gFailures += 1;
    }
}

// ---------------------------------------------------------------------------
// Fake GPUCC.
//
// Hardware behaviours modelled here, each of which a naive "plain memory" fake
// gets wrong (round 62 paid for this once already):
//   0x1120 BIT(0)  CMD_UPDATE    write-1-to-trigger, SELF-CLEARS
//   0x1120 BIT(31) CMD_ROOT_OFF  read-only STATUS, not a latched value
//   0x1540         CX status     level register the GDSC framework polls
//   0x1010         GX status     level register
//   0x1098/0x109c  branch CBCR   halt poll wants CLK_OFF clear OR FSM ON
// ---------------------------------------------------------------------------
struct FakeHw {
    std::uint32_t reg[kGpuccBytes / 4u];
    std::uint32_t reads;
    std::uint32_t writes;
    std::uint32_t delays;
    std::uint32_t last_write_off;
    std::uint32_t last_write_val;
    std::uint32_t cx_word;        // value 0x1540 settles to
    std::uint32_t gx_word;        // value 0x1010 settles to
    std::uint32_t cx_reads;       // how many times 0x1540 was read
    // -1: never overrides. Otherwise the read of 0x1540 whose 1-based count
    // equals this value sees BIT(31) set, modelling a poll window that accepts
    // while the settled level stays clear.
    int cx_on_at_read;
    int cx_read_fail;             // non-zero: every 0x1540 read fails
    int status_read_fail;         // non-zero: every status read fails
};

FakeHw gHw{};
AdrenoGpuccWindowV1 gWindow{};
bool gWindowBound = false;

void hw_reset()
{
    std::memset(&gHw, 0, sizeof gHw);
    gHw.cx_word = 0x00005ff4u;   // exactly what round 35 measured on the device
    gHw.gx_word = 0x00008000u;   // BIT(15): GX power-down complete
    gHw.cx_on_at_read = -1;
}

// The window is only used for bounds; the offsets are resolved through it.
int thunk_read(void *cookie, std::uint32_t off, std::uint32_t *out)
{
    (void)cookie;
    volatile std::uint32_t *cell = nullptr;
    if (!gWindowBound) {
        return 1;
    }
    if (AdrenoGpuccWindowV1Resolve(&gWindow, off, &cell) != 0) {
        return 1;
    }
    // Resolve handed back the real window address; the fake hardware answers
    // by offset so the test can model behaviours plain memory cannot.
    if (off == 0x1120u) {
        gHw.reads++;
        *out = gHw.reg[off / 4u] & ~(0x00000001u | 0x80000000u);
        return 0;
    }
    if (off == 0x1540u) {
        gHw.reads++;
        gHw.cx_reads++;
        if (gHw.cx_read_fail != 0) {
            return 1;
        }
        std::uint32_t v = gHw.cx_word;
        if (gHw.cx_on_at_read > 0 &&
            static_cast<int>(gHw.cx_reads) == gHw.cx_on_at_read) {
            v |= 0x80000000u;
        }
        *out = v;
        return 0;
    }
    if (off == 0x1010u) {
        gHw.reads++;
        if (gHw.status_read_fail != 0) {
            return 1;
        }
        *out = gHw.gx_word;
        return 0;
    }
    gHw.reads++;
    if (off == 0x1098u || off == 0x109cu) {
        *out = 0u;   // healthy branch: CLK_OFF clear
        return 0;
    }
    *out = gHw.reg[off / 4u];
    return 0;
}

int thunk_write(void *cookie, std::uint32_t off, std::uint32_t value)
{
    (void)cookie;
    volatile std::uint32_t *cell = nullptr;
    if (!gWindowBound) {
        return 1;
    }
    if (AdrenoGpuccWindowV1Resolve(&gWindow, off, &cell) != 0) {
        return 1;
    }
    gHw.writes++;
    gHw.last_write_off = off;
    gHw.last_write_val = value;
    gHw.reg[off / 4u] = value;
    // A collapse clear on the CX GDSCR does NOT raise the settled status word
    // in this model. That is the honest default: on this board nothing has been
    // observed to move BIT(31), and the tests that need "it does stick" say so
    // explicitly by setting cx_word.
    return 0;
}

int thunk_delay(void *cookie, std::uint32_t usec)
{
    (void)cookie;
    gHw.delays++;
    (void)usec;
    return 0;
}

struct Harness {
    AdrenoCallerV1 caller{};
    AdrenoPowerUpBackendV1Mmio mmio{};
    AdrenoPowerUpBackendV1Inputs in{};
};

// The window needs real storage behind it for Resolve() to hand out addresses,
// even though thunk_read/thunk_write answer by offset.
std::uint32_t gBacking[kGpuccBytes / 4u];

void harness_init(Harness &h, std::uint32_t strict_gx)
{
    hw_reset();
    std::memset(gBacking, 0, sizeof gBacking);
    gWindow = AdrenoGpuccWindowV1{};
    check(AdrenoGpuccWindowV1Bind(&gWindow,
                                  reinterpret_cast<volatile std::uint8_t *>(gBacking),
                                  kGpuccBytes) == 0,
          "window bind");
    AdrenoGpuccWindowV1SetDelay(&gWindow, nullptr, nullptr);
    gWindowBound = true;

    h.mmio.cookie = nullptr;
    h.mmio.read32 = thunk_read;
    h.mmio.write32 = thunk_write;
    h.mmio.udelay = thunk_delay;

    h.in.armed = 1u;
    h.in.gxpd_present = 1u;
    h.in.rcg_enabled = 0u;
    h.in.core_rate_hz = AdrenoPowerUpV1CoreRateHz();
    h.in.strict_gx = strict_gx;
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------
void testMake()
{
    std::printf("[ make ]\n");
    Harness h;
    harness_init(h, 0u);

    check(AdrenoCallerV1Make(nullptr, &h.mmio, &h.in) != 0, "NULL caller");
    check(AdrenoCallerV1Make(&h.caller, nullptr, &h.in) != 0, "NULL mmio");
    check(AdrenoCallerV1Make(&h.caller, &h.mmio, nullptr) != 0, "NULL inputs");

    AdrenoPowerUpBackendV1Mmio hole = h.mmio;
    hole.read32 = nullptr;
    check(AdrenoCallerV1Make(&h.caller, &hole, &h.in) != 0, "no read32");
    hole = h.mmio;
    hole.write32 = nullptr;
    check(AdrenoCallerV1Make(&h.caller, &hole, &h.in) != 0, "no write32");
    hole = h.mmio;
    hole.udelay = nullptr;
    check(AdrenoCallerV1Make(&h.caller, &hole, &h.in) != 0,
          "no udelay (the polls need it)");

    AdrenoPowerUpBackendV1Inputs off = h.in;
    off.armed = 0u;
    check(AdrenoCallerV1Make(&h.caller, &h.mmio, &off) != 0, "disarmed");
    off.armed = 2u;
    check(AdrenoCallerV1Make(&h.caller, &h.mmio, &off) != 0, "armed=2");

    check(AdrenoCallerV1Make(&h.caller, &h.mmio, &h.in) == 0, "make ok");
    checkEq(h.caller.ready, 1u, "ready");
    checkEq(h.caller.armed, 1u, "armed");
    checkEq(h.caller.ops, 0u, "constructing performed no MMIO");
    checkEq(h.caller.saw_op, 0u, "no access seen yet");
    checkEq(gHw.writes, 0u, "no writes during construction");
    checkEq(gHw.reads, 0u, "no reads during construction");
}

void testObserveFailCostsNothing()
{
    std::printf("[ observe-fail ]\n");
    Harness h;
    harness_init(h, 0u);
    gHw.cx_read_fail = 1;

    check(AdrenoCallerV1Make(&h.caller, &h.mmio, &h.in) == 0, "make ok");

    AdrenoCallerV1Report rep;
    const AdrenoCallerV1Status st = AdrenoCallerV1Run(&h.caller, &rep);

    check(st == kAdrenoCallerV1ObserveFailed, "observe failed, not up");
    checkEq(h.caller.first_op_was_read, 1u, "FIRST access was a READ");
    checkEq(h.caller.saw_op, 1u, "one access seen");
    checkEq(h.caller.ops, 1u, "stopped at the first read");
    checkEq(gHw.writes, 0u, "ZERO writes when the window is unreadable");
    checkEq(h.caller.backend.stats.writes, 0u, "backend writes 0");
    checkEq(rep.before.valid, 0u, "pre observation invalid");
    checkEq(rep.after.valid, 0u, "post observation not attempted");
    checkEq(rep.up_status, static_cast<std::uint32_t>(kAdrenoPowerUpV1Invalid),
            "up never ran");
    checkEq(rep.precheck_status,
            static_cast<std::uint32_t>(kAdrenoGmuCallerV1Invalid),
            "precheck never ran");
    checkEq(rep.pre.cx_genpd_on, 0u, "pre cleared");
}

void testObserveFailOnGx()
{
    std::printf("[ observe-fail-gx ]\n");
    Harness h;
    harness_init(h, 0u);
    gHw.status_read_fail = 1;

    check(AdrenoCallerV1Make(&h.caller, &h.mmio, &h.in) == 0, "make ok");
    AdrenoCallerV1Report rep;
    check(AdrenoCallerV1Run(&h.caller, &rep) == kAdrenoCallerV1ObserveFailed,
          "second status read failing is also a pre-observation failure");
    checkEq(gHw.writes, 0u, "still zero writes");
    checkEq(h.caller.ops, 2u, "two reads attempted, then stop");
}

void testFullRun()
{
    std::printf("[ full run ]\n");
    Harness h;
    harness_init(h, 0u);
    // The CX poll must accept, so the status word has to show BIT(31) while the
    // plan runs. Round 35 measured BIT(31) CLEAR on the device; this Sim sets
    // it so the success path can be exercised at all.
    gHw.cx_word = 0x80000000u;

    check(AdrenoCallerV1Make(&h.caller, &h.mmio, &h.in) == 0, "make ok");

    AdrenoCallerV1Report rep;
    const AdrenoCallerV1Status st = AdrenoCallerV1Run(&h.caller, &rep);

    checkEq(h.caller.first_op_was_read, 1u, "first access was a READ");
    check(st == kAdrenoCallerV1Ok, "run ok");
    checkEq(rep.before.valid, 1u, "pre observation valid");
    checkEq(rep.before.cx_on, 1u, "pre: CX reports on (as modelled)");
    checkEq(rep.before.gx_off, 1u, "pre: GX reports power-down complete");
    checkEq(rep.after.valid, 1u, "post observation valid");
    checkEq(rep.up_status, static_cast<std::uint32_t>(kAdrenoPowerUpV1Ok),
            "up ok");
    checkEq(rep.up_fail_index, ADRENO_POWER_UP_V1_STEP_COUNT, "no step failed");
    checkEq(rep.pre.cx_genpd_on, 1u, "pre.cx_genpd_on folded from the backend");
    checkEq(rep.pre.clocks_enabled, 1u, "pre.clocks_enabled folded");
    checkEq(rep.precheck_status, static_cast<std::uint32_t>(kAdrenoGmuCallerV1Ok),
            "precheck accepted");
    checkEq(rep.ops, h.caller.ops, "report ops matches the counter");
    check(h.caller.ops > 8u, "a full run is more than the 9 executor writes");
    checkEq(h.caller.backend.stats.refused, 0u, "nothing refused");

    // The order, concretely: the two observations bracket the writes.
    checkEq(gHw.cx_reads, 4u,
            "0x1540 read by: pre-observe, in-plan poll, post-observe, precheck");
    // This layer writes nothing itself -- it only observes, and the executor
    // behind it writes. So every write on the window came from the plan, and
    // the plan's own counter must account for all of them exactly.
    checkEq(h.caller.backend.stats.writes, gHw.writes,
            "the caller added no writes of its own");
    checkEq(gHw.writes, 9u, "the nine executor writes of round 62");
}

void testPrecheckRefusedByGx()
{
    std::printf("[ precheck refused by gx ]\n");
    Harness h;
    // strict_gx = 1 is the honest knob: GX is powered by the GMU firmware on
    // this board, so a CPU-side run can succeed while GX does not report up.
    harness_init(h, 1u);
    gHw.cx_word = 0x80000000u;
    gHw.gx_word = 0x00008000u;   // BIT(15): still reports power-down complete

    check(AdrenoCallerV1Make(&h.caller, &h.mmio, &h.in) == 0, "make ok");

    AdrenoCallerV1Report rep;
    const AdrenoCallerV1Status st = AdrenoCallerV1Run(&h.caller, &rep);

    check(st == kAdrenoCallerV1PrecheckRefused,
          "Up ran but Precheck refused: the two verdicts are distinct");
    checkEq(rep.up_status, static_cast<std::uint32_t>(kAdrenoPowerUpV1Ok),
            "up genuinely succeeded");
    checkEq(rep.precheck_status,
            static_cast<std::uint32_t>(kAdrenoGmuCallerV1GxOff),
            "the refusal is GxOff, not a write failure");
    checkEq(rep.pre.require_gx, 1u, "require_gx was forwarded");
    check(gHw.writes > 0u, "the writes really happened");
}

void testCxLevelDoesNotStick()
{
    std::printf("[ cx level does not stick ]\n");
    Harness h;
    harness_init(h, 0u);
    // Model: the in-plan CX poll sees BIT(31) accepted (read #2 of 0x1540 --
    // read #1 is the pre-observation), but the settled level stays clear
    // (0x00005ff4, the round-35 device value). This is the scenario the report
    // separation exists for: Up() succeeds, and the silicon still says CX is
    // off. It is a MODELLED silicon behaviour, not a claim about the part.
    gHw.cx_word = 0x00005ff4u;
    gHw.cx_on_at_read = 2;

    check(AdrenoCallerV1Make(&h.caller, &h.mmio, &h.in) == 0, "make ok");

    AdrenoCallerV1Report rep;
    const AdrenoCallerV1Status st = AdrenoCallerV1Run(&h.caller, &rep);

    checkEq(rep.up_status, static_cast<std::uint32_t>(kAdrenoPowerUpV1Ok),
            "up accepted the single on-reading");
    check(st == kAdrenoCallerV1PrecheckRefused,
          "precheck refused on the settled level");
    checkEq(rep.precheck_status,
            static_cast<std::uint32_t>(kAdrenoGmuCallerV1CxOff),
            "refusal is CxOff");
    checkEq(rep.before.cx_on, 0u, "pre-observation: CX off, recorded");
    checkEq(rep.after.cx_on, 0u, "post-observation: CX still off, recorded");
    checkEq(rep.before.cx, 0x00005ff4u, "raw hex preserved for the log");
    // Crucially the run was NOT vetoed on the pre-observation.
    check(gHw.writes > 0u, "CX-down before powering did not prevent the writes");
}

void testCxDownBeforeIsNotVetoed()
{
    std::printf("[ cx down is recorded, not vetoed ]\n");
    Harness h;
    harness_init(h, 0u);
    gHw.cx_word = 0x00005ff4u;   // BIT(31) clear
    gHw.cx_on_at_read = -1;

    check(AdrenoCallerV1Make(&h.caller, &h.mmio, &h.in) == 0, "make ok");
    AdrenoCallerV1Report rep;
    const AdrenoCallerV1Status st = AdrenoCallerV1Run(&h.caller, &rep);

    checkEq(rep.before.cx_on, 0u, "pre-observation recorded CX off");
    // The GDSC CX poll requires BIT(31), so with a settled clear level the plan
    // itself stops -- the point here is WHERE it stopped, not that it failed.
    check(st == kAdrenoCallerV1UpFailed, "the plan is what stops, not the gate");
    check(rep.up_fail_index < ADRENO_POWER_UP_V1_STEP_COUNT, "a step failed");
    checkEq(rep.precheck_status,
            static_cast<std::uint32_t>(kAdrenoGmuCallerV1Invalid),
            "precheck is not consulted after a failed power-up");
}

void testVerify()
{
    std::printf("[ verify ]\n");
    std::uint32_t n = 0u;
    const int rc = AdrenoCallerV1Verify(&n);
    checkEq(static_cast<std::uint32_t>(rc), 0u, "AdrenoCallerV1Verify rc");
    check(n >= 30u, "verify evaluated a meaningful number of checks");
    std::printf("  caller verify: %u checks, rc=%d\n", n, rc);
}

} // namespace

int main()
{
    testMake();
    testObserveFailCostsNothing();
    testObserveFailOnGx();
    testFullRun();
    testPrecheckRefusedByGx();
    testCxLevelDoesNotStick();
    testCxDownBeforeIsNotVetoed();
    testVerify();

    if (gFailures != 0) {
        std::printf("FAIL: enchilada caller host tests (%d checks, %d failures)\n",
                    gChecks, gFailures);
        return 1;
    }
    std::printf("PASS: enchilada caller host tests (%d checks)\n", gChecks);
    return 0;
}
