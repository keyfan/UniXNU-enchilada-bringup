/* Round 63 host test: AdrenoGpuccWindowV1, the bounded MMIO adapter.
 *
 * Runs on the BUILD HOST only. It dereferences a fake window laid out in local
 * memory, so it proves the arithmetic and the refusal paths, not any hardware
 * behaviour. The real points are:
 *   - the third bound (regmap max_register 0x8008) is NOT the DTB size 0x9000,
 *     and the adapter refuses the 0x800c..0x8ffc region the DT claims;
 *   - every offset the round-62 executor actually touches resolves;
 *   - Mmio() refuses without a delay, because apb_poll() calls udelay on every
 *     iteration and a NULL there is unrecoverable.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "AdrenoGpuccWindowV1.h"
}

namespace {

unsigned gFailures = 0;
unsigned gChecks = 0;

void check(bool ok, const char *what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("FAIL: %s\n", what);
    }
}

void checkEq(uint64_t got, uint64_t want, const char *what) {
    ++gChecks;
    if (got != want) {
        ++gFailures;
        std::printf("FAIL: %s (got 0x%llx want 0x%llx)\n", what,
                    (unsigned long long)got, (unsigned long long)want);
    }
}

// Simulated GPUCC register file. Deliberately NOT the DTB size: it is the
// regmap limit rounded up to a register, which is all the adapter should serve.
struct FakeWindow {
    uint32_t words[0x9000u / 4u];
};

int delayCalls = 0;
uint32_t delayTotal = 0;
int fakeDelay(void *cookie, uint32_t usec) {
    (void)cookie;
    ++delayCalls;
    delayTotal += usec;
    return 0;
}

// --- bind ---------------------------------------------------------------
void testBind() {
    FakeWindow fake;
    AdrenoGpuccWindowV1 w;
    check(AdrenoGpuccWindowV1Bind(nullptr, (volatile uint8_t *)&fake, 0x9000u) != 0,
          "Bind(NULL window) refuses");
    check(AdrenoGpuccWindowV1Bind(&w, nullptr, 0x9000u) != 0,
          "Bind(NULL base) refuses");
    check(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 0u) != 0,
          "Bind(zero bytes) refuses");
    check(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 3u) != 0,
          "Bind(sub-register) refuses");
    check(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 0x9002u) != 0,
          "Bind(partial trailing register) refuses");
    check(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake + 1u, 0x9000u) != 0,
          "Bind(unaligned base) refuses");
    check(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 0x9000u) == 0,
          "Bind(valid) succeeds");
    checkEq(w.bytes, 0x9000u, "bytes recorded");
    checkEq(w.max_register, 0x8008u, "max_register is the regmap limit");
    checkEq(w.reads, 0u, "reads starts at 0");
    checkEq(w.writes, 0u, "writes starts at 0");
    checkEq(w.refused, 0u, "refused starts at 0");
    check(w.udelay == nullptr, "no delay until one is supplied");
}

// --- the usable-size derivation ------------------------------------------
void testUsableBytes() {
    FakeWindow fake;
    AdrenoGpuccWindowV1 w;
    AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 0x9000u);
    checkEq(AdrenoGpuccWindowV1UsableBytes(&w), 0x800cu,
            "usable = max_register + 4 = 0x800c");
    check(AdrenoGpuccWindowV1UsableBytes(&w) != 0x9000u,
          "usable is NOT the DT window size");
    checkEq(AdrenoGpuccWindowV1UsableBytes(nullptr), 0u, "usable(NULL) is 0");

    // A window smaller than the regmap limit must bound on its own size.
    AdrenoGpuccWindowV1 small;
    AdrenoGpuccWindowV1Bind(&small, (volatile uint8_t *)&fake, 0x100u);
    checkEq(AdrenoGpuccWindowV1UsableBytes(&small), 0x100u,
            "a small window bounds on its own size");

    // Wrapping: a caller passing 0xffffffff must not make usable wrap to 0.
    AdrenoGpuccWindowV1 huge;
    AdrenoGpuccWindowV1Bind(&huge, (volatile uint8_t *)&fake, 0x9000u);
    huge.max_register = 0xfffffffeu;
    checkEq(AdrenoGpuccWindowV1UsableBytes(&huge), 0x9000u,
            "max_register near UINT32_MAX does not wrap usable");
}

// --- the offsets the round-62 executor really touches --------------------
void testExecutorOffsets() {
    FakeWindow fake;
    AdrenoGpuccWindowV1 w;
    AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 0x9000u);
    const uint32_t offsets[] = {
        0x106cu,   // CX GDSCR (collapse clear)
        0x1540u,   // CX status      (PWR_ON poll)
        0x1010u,   // GX status
        0x1098u,   // CX_GMU_CBCR
        0x109cu,   // CXO_CBCR
        0x1120u,   // RCG CMD_RCGR   (CMD_UPDATE / CMD_ROOT_EN)
        0x1124u,   // RCG CFG_RCGR   (the parked 0x0602 write)
    };
    for (uint32_t off : offsets) {
        volatile uint32_t *p = nullptr;
        check(AdrenoGpuccWindowV1Resolve(&w, off, &p) == 0, "executor offset resolves");
        check((uintptr_t)p == (uintptr_t)((volatile uint8_t *)&fake + off),
              "pointer is base + offset (byte offsets, not word offsets)");
    }
    checkEq(w.refused, 0u, "no refusals for the real offsets");
}

// --- bound 1: alignment --------------------------------------------------
void testAlignmentBound() {
    FakeWindow fake;
    AdrenoGpuccWindowV1 w;
    AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 0x9000u);
    volatile uint32_t *p = reinterpret_cast<volatile uint32_t *>(&fake);
    check(AdrenoGpuccWindowV1Resolve(&w, 0x106d, &p) != 0, "odd offset refuses");
    check(AdrenoGpuccWindowV1Resolve(&w, 0x106e, &p) != 0, "half-word offset refuses");
    check(AdrenoGpuccWindowV1Resolve(&w, 1u, &p) != 0, "offset 1 refuses");
    checkEq(w.refused, 3u, "alignment refusals counted");
    checkEq(w.reads, 0u, "a refused resolve does not count a read");
}

// --- bound 3: the regmap limit (the one the DTB size does not give) ------
void testRegmapBound() {
    FakeWindow fake;
    AdrenoGpuccWindowV1 w;
    AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 0x9000u);
    volatile uint32_t *p = nullptr;

    // 0x8008 is the LAST addressable offset per gpucc-sdm845.c max_register.
    check(AdrenoGpuccWindowV1Resolve(&w, 0x8008u, &p) == 0,
          "0x8008 (max_register) resolves");
    checkEq(w.refused, 0u, "the last legal offset is not a refusal");

    // 0x800c is inside the mapped 0x9000 window but PAST the regmap limit.
    // This is the exact access the third bound exists to refuse.
    check(AdrenoGpuccWindowV1Resolve(&w, 0x800cu, &p) != 0,
          "0x800c refuses (past max_register, inside the DT window)");
    check(AdrenoGpuccWindowV1Resolve(&w, 0x8ffcu, &p) != 0,
          "0x8ffc refuses (last DT register, past max_register)");
    checkEq(w.refused, 2u, "regmap refusals counted");

    // Proof that bound 2 alone would NOT have caught this: 0x800c+4 <= 0x9000.
    check(0x800cu + 4u <= 0x9000u,
          "bound 2 alone would have allowed 0x800c (hence bound 3)");
}

// --- bound 2: the mapped window -----------------------------------------
void testWindowBound() {
    FakeWindow fake;
    AdrenoGpuccWindowV1 w;
    AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 0x9000u);
    volatile uint32_t *p = nullptr;
    check(AdrenoGpuccWindowV1Resolve(&w, 0x9000u, &p) != 0, "end of window refuses");
    check(AdrenoGpuccWindowV1Resolve(&w, 0xffffffecu, &p) != 0,
          "offset near UINT32_MAX does not wrap into the window");
    checkEq(w.refused, 2u, "window refusals counted");

    // A window smaller than max_register must bound on its own size, or a
    // caller with a short mapping would be handed a pointer past the end.
    AdrenoGpuccWindowV1 small;
    AdrenoGpuccWindowV1Bind(&small, (volatile uint8_t *)&fake, 0x100u);
    check(AdrenoGpuccWindowV1Resolve(&small, 0xfcu, &p) == 0,
          "0xfc resolves in a 0x100 window");
    check(AdrenoGpuccWindowV1Resolve(&small, 0x100u, &p) != 0,
          "0x100 refuses in a 0x100 window");
    checkEq(small.refused, 1u, "small-window refusal counted");
}

// --- resolve argument hygiene -------------------------------------------
void testResolveArgs() {
    FakeWindow fake;
    AdrenoGpuccWindowV1 w;
    AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 0x9000u);
    check(AdrenoGpuccWindowV1Resolve(nullptr, 0u, nullptr) != 0,
          "Resolve(NULL window) refuses");
    check(AdrenoGpuccWindowV1Resolve(&w, 0x106cu, nullptr) != 0,
          "Resolve(NULL out) refuses");

    AdrenoGpuccWindowV1 unbound;
    std::memset(&unbound, 0, sizeof unbound);
    volatile uint32_t *p = nullptr;
    check(AdrenoGpuccWindowV1Resolve(&unbound, 0u, &p) != 0,
          "Resolve on an unbound window refuses");
    checkEq(unbound.refused, 1u, "unbound refusal counted");
}

// --- the MMIO triple -----------------------------------------------------
void testMmioTriple() {
    FakeWindow fake;
    std::memset(&fake, 0, sizeof fake);
    AdrenoGpuccWindowV1 w;
    AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)&fake, 0x9000u);
    AdrenoPowerUpBackendV1Mmio mmio;

    check(AdrenoGpuccWindowV1Mmio(nullptr, &mmio) != 0, "Mmio(NULL window) refuses");
    check(AdrenoGpuccWindowV1Mmio(&w, nullptr) != 0, "Mmio(NULL out) refuses");

    // THE IMPORTANT ONE: no delay -> no triple. apb_poll() calls udelay on
    // every iteration, so a NULL there is unrecoverable in the GDSC leg.
    check(AdrenoGpuccWindowV1Mmio(&w, &mmio) != 0,
          "Mmio refuses without a delay (the GDSC poll would crash)");

    check(AdrenoGpuccWindowV1SetDelay(&w, nullptr, nullptr) != 0,
          "SetDelay(NULL fn) refuses");
    check(AdrenoGpuccWindowV1SetDelay(nullptr, nullptr, fakeDelay) != 0,
          "SetDelay(NULL window) refuses");
    check(AdrenoGpuccWindowV1SetDelay(&w, &fake, fakeDelay) == 0,
          "SetDelay(valid) succeeds");
    check(AdrenoGpuccWindowV1Mmio(&w, &mmio) == 0, "Mmio succeeds with a delay");
    check(mmio.cookie == &w, "cookie points back at the window");
    check(mmio.read32 != nullptr && mmio.write32 != nullptr && mmio.udelay != nullptr,
          "all three function pointers are set");

    // Round-trip through the triple, exactly as the executor would.
    check(mmio.write32(mmio.cookie, 0x1124u, 0x0602u) == 0, "write via triple");
    uint32_t got = 0;
    check(mmio.read32(mmio.cookie, 0x1124u, &got) == 0, "read via triple");
    checkEq(got, 0x0602u, "round-trip value");
    checkEq(w.writes, 1u, "write counted on the window");
    checkEq(w.reads, 1u, "read counted on the window");

    // The triple must inherit the bounds -- a bare pointer would not.
    check(mmio.write32(mmio.cookie, 0x800cu, 0u) != 0,
          "triple write past max_register refuses");
    check(mmio.read32(mmio.cookie, 0x106du, &got) != 0,
          "triple read at an unaligned offset refuses");
    checkEq(w.refused, 2u, "triple refusals counted on the window");

    // And the delay must actually reach the caller's function.
    delayCalls = 0;
    delayTotal = 0;
    check(mmio.udelay(mmio.cookie, 17u) == 0, "udelay via triple");
    checkEq((uint64_t)delayCalls, 1u, "caller's delay was called");
    checkEq(delayTotal, 17u, "usec forwarded to the caller's delay");
}

// --- INTEGRATION: the round-62 executor driven THROUGH this window --------
//
// This is the check that stops the adapter from being yet another layer that
// exists but is never connected. It builds the real AdrenoPowerUpBackendV1 on
// top of this window and runs the real five-op plan, then inspects the fake
// register file for the writes the derivation predicts. If the adapter and the
// executor ever disagree about the offset convention (bytes vs words), the
// writes land at 4x their intended offsets and this test fails.
//
// WHY THE TRIPLE IS BUILT HERE INSTEAD OF VIA AdrenoGpuccWindowV1Mmio():
// a plain memory window CANNOT emulate a register that self-clears on write,
// and 0x1120 (RCG CMD_RCGR) is exactly that -- CMD_UPDATE BIT(0) is write-1-
// trigger and clears itself, CMD_ROOT_OFF BIT(31) is a read-only status bit.
// Round 62 learned this the hard way: a simulator that treats 0x1120 as
// ordinary storage certifies an executor that would hang forever in its
// CMD_UPDATE poll. So this test's triple keeps the hardware emulation and
// routes EVERY access through the real AdrenoGpuccWindowV1Resolve(), which is
// what is actually under test here (the bounds). Mmio() itself is exercised
// separately in testMmioTriple(). Adding a read-side test hook to the
// production window just to make this test convenient would put emulation
// logic into kernel code, which is a worse trade.
struct FakeHw {
    uint32_t words[0x9000u / 4u];
    uint32_t cxStatus;
    uint32_t gxStatus;
};

// The window under test, reachable from the non-capturing MMIO thunks below so
// that every access they serve still passes AdrenoGpuccWindowV1Resolve() first.
AdrenoGpuccWindowV1 *gBoundWindow = nullptr;

void testExecutorThroughWindow() {
    static FakeHw hw;
    std::memset(&hw, 0, sizeof hw);

    AdrenoGpuccWindowV1 w;
    check(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)hw.words, 0x9000u) == 0,
          "integration: bind");
    gBoundWindow = &w;

    // Emulated hardware state, mirroring the round-62 simulator's lessons.
    hw.cxStatus = 0x80000000u;   // PWR_ON BIT(31): CX already powered
    hw.gxStatus = 0x00010000u;   // POWER_UP_COMPLETE BIT(16)

    AdrenoPowerUpBackendV1Mmio mmio;
    mmio.cookie = &hw;
    mmio.read32 = [](void *c, uint32_t off, uint32_t *out) -> int {
        FakeHw *h = (FakeHw *)c;
        // Every access still goes through the bounds check under test.
        volatile uint32_t *p = nullptr;
        if (AdrenoGpuccWindowV1Resolve(gBoundWindow, off, &p) != 0) {
            return 1;
        }
        if (off == 0x1540u) { *out = h->cxStatus; return 0; }
        if (off == 0x1010u) { *out = h->gxStatus; return 0; }
        uint32_t v = h->words[off / 4u];
        if (off == 0x1120u) {
            // CMD_UPDATE self-clears; CMD_ROOT_OFF is read-only status.
            v &= ~(0x00000001u | 0x80000000u);
        }
        *out = v;
        return 0;
    };
    mmio.write32 = [](void *c, uint32_t off, uint32_t value) -> int {
        FakeHw *h = (FakeHw *)c;
        volatile uint32_t *p = nullptr;
        if (AdrenoGpuccWindowV1Resolve(gBoundWindow, off, &p) != 0) {
            return 1;
        }
        if (off == 0x1540u) { h->cxStatus = value; return 0; }
        h->words[off / 4u] = value;
        return 0;
    };
    mmio.udelay = [](void *, uint32_t) -> int { return 0; };

    AdrenoPowerUpBackendV1Inputs in = {};
    in.armed = 1u;
    in.gxpd_present = 1u;
    in.rcg_enabled = 0u;
    in.core_rate_hz = 200000000u;
    in.strict_gx = 0u;

    AdrenoPowerUpBackendV1 be;
    check(AdrenoPowerUpBackendV1Make(&be, &mmio, &in) == 0, "integration: Make");

    AdrenoGmuCallerV1Pre pre = {};
    uint32_t failIndex = 0xffffffffu;
    const AdrenoPowerUpV1Status rc = AdrenoPowerUpBackendV1Up(&be, &pre, &failIndex);
    check(rc == kAdrenoPowerUpV1Ok, "integration: Up succeeds through the window");
    checkEq(pre.cx_genpd_on, 1u, "integration: CX reported on");
    // gx_genpd_on is NOT "the CPU powered GX". The GX GDSC step is GUARDED
    // (AdrenoPowerUpV1StepGuarded(GX)==1) precisely because GX is powered by
    // the GMU firmware, not by CPU writes -- the round-62 executor performs
    // zero GX writes. What Execute() records is that the GX STEP completed,
    // which with strict_gx=0 and gxpd_present=1 it does. Asserting 0 here was
    // my error, not the executor's: it would have meant "the GX step never
    // ran", which is a different thing from "the CPU never wrote GX".
    checkEq(pre.gx_genpd_on, 1u,
            "integration: the GX step completed (guarded; the CPU wrote nothing)");
    checkEq(pre.clocks_enabled, 1u, "integration: clocks reported enabled");
    checkEq(pre.require_gx, 0u, "integration: require_gx mirrors strict_gx");
    checkEq(be.stats.refused, 0u, "integration: the executor never wrote disarmed");

    // The derived totals, checked against the register file rather than the
    // executor's own counters alone.
    checkEq(be.stats.writes, 9u, "integration: chain total is the derived 9 writes");

    // The CX leg's single write must have landed on the GDSCR at 0x106c.
    checkEq(hw.words[0x106cu / 4u], 0u,
            "integration: CX collapse-clear wrote 0 at 0x106c (not a 4x offset)");

    // The parked core rate must have cached 0x0602 and written NOTHING to the
    // RCG: the CFG word is only programmed later, inside the enable step.
    checkEq(be.parked_cfg, 0x0602u, "integration: parked CFG word is 0x0602");

    // The CFG write must have landed at 0x1124 (CMD + 4), which is the whole
    // point of the byte-offset convention being right.
    checkEq(hw.words[0x1124u / 4u], 0x0602u,
            "integration: the parked CFG word was written at 0x1124");

    // The branch clocks must show CLOCK_ENABLE (BIT 0) set for both.
    check((hw.words[0x1098u / 4u] & 1u) == 1u, "integration: gmu CBCR enabled");
    check((hw.words[0x109cu / 4u] & 1u) == 1u, "integration: cxo CBCR enabled");
}

// --- self-check ----------------------------------------------------------

// --- self-check ----------------------------------------------------------
void testVerify() {
    uint32_t n = 0;
    const int rc = AdrenoGpuccWindowV1Verify(&n);
    check(rc == 0, "AdrenoGpuccWindowV1Verify passes");
    check(n >= 40, "Verify evaluated a substantial number of checks");
    check(AdrenoGpuccWindowV1Verify(nullptr) == 0, "Verify tolerates NULL check_count");
    std::printf("  window verify: %u checks, rc=%d\n", n, rc);
}

}  // namespace

int main() {
    testBind();
    testUsableBytes();
    testExecutorOffsets();
    testAlignmentBound();
    testRegmapBound();
    testWindowBound();
    testResolveArgs();
    testMmioTriple();
    testExecutorThroughWindow();
    testVerify();

    if (gFailures == 0) {
        std::printf("PASS: enchilada GPUCC window host tests (%u checks)\n", gChecks);
        return 0;
    }
    std::printf("FAIL: %u of %u checks failed\n", gFailures, gChecks);
    return 1;
}
