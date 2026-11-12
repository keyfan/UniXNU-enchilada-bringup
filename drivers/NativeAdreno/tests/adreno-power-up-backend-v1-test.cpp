// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for AdrenoPowerUpBackendV1 -- the EXECUTOR that finally turns the
// round-57/58/61 derived plans into real MMIO traffic.
//
// Method 1 of 2 (method 2 is scripts/verify-enchilada-power-up-backend-v1.py).
//
// What this test is really defending:
//   * the four AdrenoPowerUpV1Bus callbacks actually WRITE, so a backend can no
//     longer be a silent no-op and still pass;
//   * the write counts equal what rounds 57/58/61 derived, not a guess;
//   * the CX leg writes exactly ONE register and then only polls (round 58);
//   * the GX leg writes NOTHING (gdsc_gx_do_nothing_enable);
//   * the hub leg writes NOTHING (round 61 finding 1);
//   * the core leg is PARKED at resume, so the CFG write happens in the enable
//     leg, and the word written back is 0x0602;
//   * an unarmed backend refuses instead of writing.
//
// Build: compiled as C++17 and linked against AdrenoPowerUpBackendV1.c,
// AdrenoPowerUpV1.c, AdrenoPowerUpGdsc.c, AdrenoGdscDecode.c,
// AdrenoClkCtlV1.c, AdrenoDomainState.c (see check-enchilada-adreno-host.py).

extern "C" {
#include "AdrenoPowerUpBackendV1.h"
#include "AdrenoPowerUpV1.h"
#include "AdrenoPowerUpGdsc.h"
#include "AdrenoClkCtlV1.h"
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

// ---- a simulated GPUCC register file -------------------------------------
//
// Only the offsets this chain touches are honoured; the rest of the window
// reads as 0. `writes` records every store so the test can count them by offset.
constexpr unsigned GPUCC_BYTES = 0x9000;

struct Sim {
    std::uint32_t reg[GPUCC_BYTES / 4];
    std::uint32_t writes;
    std::uint32_t reads;
    std::uint32_t delays;
    std::uint32_t last_write_off;
    std::uint32_t last_write_val;
    // Set a register to a fixed value on read, to model a poll that succeeds.
    std::uint32_t cx_status;   // 0x1540
    std::uint32_t gx_status;   // 0x1010
    std::uint32_t cbcr_clk_off; // 0x1098 / 0x109c: BIT(31) set => stalled
};

int sim_read(void *cookie, std::uint32_t off, std::uint32_t *out)
{
    Sim *s = static_cast<Sim *>(cookie);
    if (off % 4u != 0u || off + 4u > GPUCC_BYTES) {
        return 1;
    }
    s->reads++;
    std::uint32_t v = 0;
    if (off == 0x1120u) {
        // The RCG CMD register is NOT plain storage, and modelling it as such
        // is what made the first run of this test fail:
        //   BIT(0) CMD_UPDATE     is write-1-to-trigger and SELF-CLEARS; the
        //                         poll after it would never end against a
        //                         register that remembered the write.
        //   BIT(31) CMD_ROOT_OFF  is a read-only STATUS bit that reads 0 while
        //                         the root is running, which force-enable just
        //                         made it. It is not a latched value either.
        v = s->reg[off / 4u] & ~(0x00000001u | 0x80000000u);
    } else if (off == 0x1540u) {
        v = s->cx_status;
    } else if (off == 0x1010u) {
        v = s->gx_status;
    } else if (off == 0x1098u || off == 0x109cu) {
        // The halt poll wants CLK_OFF clear OR FSM ON. Model a healthy clock.
        v = s->cbcr_clk_off ? 0x80000000u : 0x00000000u;
    } else {
        v = s->reg[off / 4u];
    }
    *out = v;
    return 0;
}

int sim_write(void *cookie, std::uint32_t off, std::uint32_t value)
{
    Sim *s = static_cast<Sim *>(cookie);
    if (off % 4u != 0u || off + 4u > GPUCC_BYTES) {
        return 1;
    }
    s->writes++;
    s->last_write_off = off;
    s->last_write_val = value;
    s->reg[off / 4u] = value;
    return 0;
}

int sim_delay(void *cookie, std::uint32_t usec)
{
    Sim *s = static_cast<Sim *>(cookie);
    s->delays++;
    (void)usec;
    return 0;
}

void sim_reset(Sim &s)
{
    std::memset(&s, 0, sizeof s);
    // A deasserted CX: BIT(31) clear would mean "not powered", so to make the
    // CX enable poll succeed the status must have BIT(31) SET.
    s.cx_status = 0x80000000u;
    s.gx_status = 0x00008000u;   // BIT(15) down-complete
    s.cbcr_clk_off = 0;
}

// Build a backend around a simulator.
struct Harness {
    Sim sim{};
    AdrenoPowerUpBackendV1Mmio mmio{};
    AdrenoPowerUpBackendV1Inputs in{};
    AdrenoPowerUpBackendV1 be{};

    void setup(std::uint32_t armed = 1u, std::uint32_t rcg_enabled = 0u)
    {
        sim_reset(sim);
        mmio.cookie = &sim;
        mmio.read32 = sim_read;
        mmio.write32 = sim_write;
        mmio.udelay = sim_delay;
        in.armed = armed;
        in.gxpd_present = 1u;
        in.rcg_enabled = rcg_enabled;
        in.core_rate_hz = 200000000u;
        in.strict_gx = 0u;
        std::memset(&be, 0, sizeof be);
    }
};

// ---- group 1: Make is fail-closed ---------------------------------------
void testMake()
{
    Harness h;
    h.setup();

    check(AdrenoPowerUpBackendV1Make(&h.be, &h.mmio, &h.in) == 0,
          "Make accepts a fully wired, armed backend");

    // Disarmed: the backend is not built at all.
    Harness d;
    d.setup(0u);
    check(AdrenoPowerUpBackendV1Make(&d.be, &d.mmio, &d.in) != 0,
          "an unarmed backend is REFUSED, not built inert");

    // An incomplete MMIO triple is refused.
    Harness m;
    m.setup();
    m.mmio.udelay = nullptr;
    check(AdrenoPowerUpBackendV1Make(&m.be, &m.mmio, &m.in) != 0,
          "a missing udelay callback is refused");

    Harness n;
    n.setup();
    check(AdrenoPowerUpBackendV1Make(nullptr, &n.mmio, &n.in) != 0,
          "a NULL backend is refused");
    check(AdrenoPowerUpBackendV1Make(&n.be, nullptr, &n.in) != 0,
          "a NULL mmio is refused");
    check(AdrenoPowerUpBackendV1Make(&n.be, &n.mmio, nullptr) != 0,
          "a NULL inputs is refused");

    // Out-of-range boolean inputs are refused.
    Harness b;
    b.setup();
    b.in.gxpd_present = 7u;
    check(AdrenoPowerUpBackendV1Make(&b.be, &b.mmio, &b.in) != 0,
          "a non-boolean gxpd_present is refused");

    std::printf("power-up-backend make: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 2: the CX leg writes exactly one register -------------------
void testCxLeg()
{
    Harness h;
    h.setup();
    check(AdrenoPowerUpBackendV1Make(&h.be, &h.mmio, &h.in) == 0, "backend built");

    const std::uint32_t before = h.sim.writes;
    const int rc = h.be.bus.genpd_on(&h.be, 0u);
    const std::uint32_t delta = h.sim.writes - before;

    check(rc == 0, "CX genpd_on succeeds");
    // Round 58: the CX CPU enable path is ONE register write (collapse clear at
    // 0x106c) plus a status READ. Reading is not writing.
    check(delta == 1u, "CX genpd_on performs exactly ONE register write");
    check(h.sim.last_write_off == 0x106cu,
          "and that write lands on the CX GDSCR 0x106c");
    check(h.sim.last_write_val == 0u,
          "clearing the collapse bit (value 0)");
    check(h.sim.reads > 0u, "and the status register is READ, not written");
    check(h.be.cx_on == 1u, "the backend records CX as up");

    std::printf("power-up-backend cx leg: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 3: the GX leg writes NOTHING --------------------------------
void testGxLeg()
{
    Harness h;
    h.setup();
    check(AdrenoPowerUpBackendV1Make(&h.be, &h.mmio, &h.in) == 0, "backend built");

    const std::uint32_t before = h.sim.writes;
    const int rc = h.be.bus.genpd_on(&h.be, 1u);
    const std::uint32_t delta = h.sim.writes - before;

    check(rc == 0, "GX genpd_on succeeds");
    // gdsc_gx_do_nothing_enable: the CPU never powers GX. This is a PROVABLE
    // zero, and it is why the device word sits at permanent down-complete.
    check(delta == 0u, "GX genpd_on performs ZERO register writes");
    check(h.be.gx_on == 1u, "but the refcount is recorded as acquired");

    // The derived invariant this depends on still holds.
    check(AdrenoPowerUpGdscGuardMatchesWrites() == 1,
          "round-58 guard/write invariant still holds");

    std::printf("power-up-backend gx leg: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 4: the hub leg writes NOTHING -------------------------------
void testHubLeg()
{
    Harness h;
    h.setup();
    check(AdrenoPowerUpBackendV1Make(&h.be, &h.mmio, &h.in) == 0, "backend built");

    const std::uint32_t before = h.sim.writes;
    const int rc = h.be.bus.set_rate(&h.be, 1u, 150000000u);
    const std::uint32_t delta = h.sim.writes - before;

    check(rc == 0, "set_rate(hub) SUCCEEDS, exactly as clk_set_rate(NULL) does");
    check(delta == 0u, "and performs ZERO register writes (round 61 finding 1)");
    check(AdrenoClkCtlV1HubStepWritesNothing() == 1, "the derived no-op still holds");

    std::printf("power-up-backend hub leg: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 5: the core leg is PARKED at resume ------------------------
void testCoreLegParked()
{
    Harness h;
    h.setup(1u, /*rcg_enabled=*/0u);
    check(AdrenoPowerUpBackendV1Make(&h.be, &h.mmio, &h.in) == 0, "backend built");

    const std::uint32_t before = h.sim.writes;
    const int rc = h.be.bus.set_rate(&h.be, 0u, 200000000u);
    const std::uint32_t delta = h.sim.writes - before;

    check(rc == 0, "set_rate(core) succeeds");
    // Round 61 finding 2: when the RCG is disabled the shared ops cache the CFG
    // word and write NOTHING. A backend that wrote CFG here would be inventing
    // traffic upstream does not make.
    check(delta == 0u, "with the RCG disabled the core leg writes ZERO registers");
    check(h.be.parked_cfg == 0x0602u, "and parks the derived CFG word 0x0602");

    std::printf("power-up-backend core parked: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 6: clocks_enable does the real work ------------------------
void testClocksEnable()
{
    Harness h;
    h.setup(1u, 0u);
    check(AdrenoPowerUpBackendV1Make(&h.be, &h.mmio, &h.in) == 0, "backend built");

    // Without a parked word the RCG would be configured with a word we never
    // derived: the executor must refuse.
    check(h.be.bus.clocks_enable(&h.be, 4u) != 0,
          "clocks_enable REFUSES before set_rate parked a word");

    // Park it, then enable.
    check(h.be.bus.set_rate(&h.be, 0u, 200000000u) == 0, "core rate parked");

    const std::uint32_t before = h.sim.writes;
    const int rc = h.be.bus.clocks_enable(&h.be, 4u);
    const std::uint32_t delta = h.sim.writes - before;

    check(rc == 0, "clocks_enable succeeds");
    // 4 writes for the RCG leg (force_enable, CFG, CMD_UPDATE, clear) plus
    // 2 writes per branch clock (wake/sleep, CLOCK_ENABLE) x 2 = 4. Total 8.
    check(delta == 8u, "clocks_enable performs exactly 8 GPUCC writes");
    check(h.be.clocks_on == 1u, "and records the clocks as on");

    // A bulk count we do not model (4 is right, 6 is the kgsl-3d0 node) must be
    // refused: claiming to enable 6 would claim the two GCC clocks.
    Harness h2;
    h2.setup(1u, 0u);
    check(AdrenoPowerUpBackendV1Make(&h2.be, &h2.mmio, &h2.in) == 0, "second backend built");
    check(h2.be.bus.clocks_enable(&h2.be, 6u) != 0,
          "clocks_enable REFUSES the kgsl-3d0 count of 6");

    std::printf("power-up-backend clocks: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 7: the full Up() chain and the Pre it produces ------------
void testFullUp()
{
    Harness h;
    h.setup(1u, 0u);
    check(AdrenoPowerUpBackendV1Make(&h.be, &h.mmio, &h.in) == 0, "backend built");

    AdrenoGmuCallerV1Pre pre{};
    std::uint32_t fail = 0;
    const AdrenoPowerUpV1Status rc = AdrenoPowerUpBackendV1Up(&h.be, &pre, &fail);

    check(rc == kAdrenoPowerUpV1Ok, "Up() completes the whole five-op chain");
    check(fail == ADRENO_POWER_UP_V1_STEP_COUNT, "no step failed");

    // The five ops are exactly: CX, GX, core, hub, clocks.
    check(h.be.cx_on == 1u, "CX acquired");
    check(h.be.gx_on == 1u, "GX refcount acquired");
    check(h.be.clocks_on == 1u, "clocks enabled");
    check(h.be.parked_cfg == 0x0602u, "the parked word survived to the enable leg");

    // Total writes across the whole chain: CX 1 + GX 0 + core 0 + hub 0 +
    // clocks 8 = 9.
    check(h.sim.writes == 9u, "the whole chain performs exactly 9 GPUCC writes");

    // And the Pre it hands to the round-46 caller is ready.
    check(AdrenoPowerUpV1PreIsReady(&pre) == 1,
          "the produced Pre satisfies the round-46 caller gate");
    check(pre.cx_genpd_on == 1u && pre.clocks_enabled == 1u,
          "with cx and clocks recorded");

    std::printf("power-up-backend full up: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 8: a stalled clock fails the branch poll -------------------
void testStalledClock()
{
    Harness h;
    h.setup(1u, 0u);
    check(AdrenoPowerUpBackendV1Make(&h.be, &h.mmio, &h.in) == 0, "backend built");
    check(h.be.bus.set_rate(&h.be, 0u, 200000000u) == 0, "core rate parked");

    // Make the branch clock read as stalled (CLK_OFF set, FSM not ON).
    h.sim.cbcr_clk_off = 1u;

    const int rc = h.be.bus.clocks_enable(&h.be, 4u);
    check(rc != 0, "a clock whose halt bit never clears FAILS the enable");
    check(h.be.clocks_on == 0u, "and the backend does not record it as on");

    std::printf("power-up-backend stalled clock: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 9: the module's own self-check ----------------------------
void testVerify()
{
    std::uint32_t checks = 0;
    check(AdrenoPowerUpBackendV1Verify(&checks) == 0, "AdrenoPowerUpBackendV1Verify passes");
    check(checks >= 8u, "it ran its full check list");

    std::printf("power-up-backend verify: %u checks, %u failures\n", g_checks, g_failures);
}

} // namespace

int main()
{
    testMake();
    testCxLeg();
    testGxLeg();
    testHubLeg();
    testCoreLegParked();
    testClocksEnable();
    testFullUp();
    testStalledClock();
    testVerify();

    if (g_failures != 0u) {
        std::printf("FAIL: enchilada power-up backend host tests (%u checks, %u failures)\n",
                    g_checks, g_failures);
        return 1;
    }
    std::printf("PASS: enchilada power-up backend host tests (%u checks)\n", g_checks);
    return 0;
}
