// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the enchilada stage-3 layer 3d (alpha) OS-facing brightness CONTROL
// PLANE. Pure C over injected time (no IOKit, no clock, no MMIO). Asserts the two
// enchilada-specific corrections vs sheng: the 0..1023 clamp (NOT 2047) and the
// platform-independent fade/fade-period math, plus the L-row publish layout.
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "DsiDcs.h"
#include "MsmDsiControl.h"

static void testScaleMismatch() {
    // The single source of truth is the sofef00 DCS panel's bl-max 0x3ff, NOT sheng's
    // 11-bit KTZ8866 (2047). evidence/applearmbacklight-metaclass.md top open risk.
    assert(kEnchiladaBrightnessMax == 1023);
    assert(kMsmDsiControlMaxLevel == 1023);
    assert(msm_dsi_control_clamp(-5) == 0u);
    assert(msm_dsi_control_clamp(0) == 0u);
    assert(msm_dsi_control_clamp(1) == 1u);
    assert(msm_dsi_control_clamp(512) == 512u);
    assert(msm_dsi_control_clamp(1023) == 1023u);
    // These are exactly the values sheng would have accepted (up to 2047) and MUST clamp.
    assert(msm_dsi_control_clamp(1024) == 1023u);
    assert(msm_dsi_control_clamp(1500) == 1023u);
    assert(msm_dsi_control_clamp(2047) == 1023u);
    assert(msm_dsi_control_clamp(0x7fffffff) == 1023u);
}

static void testFadeMs() {
    // sheng setProperties(): (FadePeriod_16.16 * 1000) >> 16, cap 5000 ms.
    assert(msm_dsi_control_fade_ms(0) == 0u);
    assert(msm_dsi_control_fade_ms(0x00010000u) == 1000u);  // 1.0 s
    assert(msm_dsi_control_fade_ms(0x00008000u) == 500u);   // 0.5 s
    assert(msm_dsi_control_fade_ms(0x00028000u) == 2500u);  // 2.5 s
    assert(msm_dsi_control_fade_ms(0xffffffffu) == 5000u);  // capped, no 32-bit overflow
}

static void testInit() {
    struct msm_dsi_control c;
    msm_dsi_control_init(&c, kMsmDsiControlDefaultLevel);
    assert(c.level == 512u && c.from == 512u && c.to == 512u);
    assert(c.requests == 0u && c.steps == 0u);
    assert(c.stage == kMsmDsiControlStageInit);
    assert(c.impersonated == 0 && c.last_rc == 0);
    assert(msm_dsi_control_fade_done(&c) == 1);
    // init() also clamps a bogus starting level.
    msm_dsi_control_init(&c, 9999u);
    assert(c.level == 1023u);
    msm_dsi_control_init(nullptr, 512u);  // must not crash
}

static void testRequestClamps() {
    struct msm_dsi_control c;
    msm_dsi_control_init(&c, 512u);
    // CoreBrightness sends 2047 (sheng scale) -> clamp to 1023.
    msm_dsi_control_request(&c, 2047, 0, 1000u);
    assert(c.requests == 1u);
    assert(c.to == 1023u);
    assert(c.from == 512u && c.level == 512u);
    assert(c.start_ms == 1000u && c.period_ms == 0u);
    uint32_t level = 0;
    assert(msm_dsi_control_step(&c, 1000u, &level) == 1);
    assert(level == 1023u && c.level == 1023u);
    assert(msm_dsi_control_fade_done(&c) == 1);
    // A negative OSNumber clamps to 0, not to a wrap.
    msm_dsi_control_request(&c, -1, 0, 2000u);
    assert(c.to == 0u);
    assert(msm_dsi_control_step(&c, 2000u, &level) == 1);
    assert(level == 0u);
}

static void testLinearFade() {
    struct msm_dsi_control c;
    msm_dsi_control_init(&c, 0u);
    msm_dsi_control_request(&c, 1000, 0x00010000u, 0u);  // 0 -> 1000 over 1000 ms
    assert(c.period_ms == 1000u);
    uint32_t level = 9999u;
    // Elapsed 0: stays at from, no new level.
    assert(msm_dsi_control_step(&c, 0u, &level) == 0);
    assert(level == 0u);
    assert(msm_dsi_control_step(&c, 250u, &level) == 1);
    assert(level == 250u);
    assert(msm_dsi_control_step(&c, 500u, &level) == 1);
    assert(level == 500u);
    assert(msm_dsi_control_step(&c, 750u, &level) == 1);
    assert(level == 750u);
    // Clock before start is treated as elapsed=0, never as "done".
    msm_dsi_control_request(&c, 800, 0x00010000u, 5000u);
    assert(msm_dsi_control_step(&c, 4999u, &level) == 0);  // level still 750
    assert(level == 750u);
    // At/after the deadline the goal is reached.
    assert(msm_dsi_control_step(&c, 6000u, &level) == 1);
    assert(level == 800u);
    assert(msm_dsi_control_fade_done(&c) == 1);
    assert(c.steps == 4u);  // 250,500,750,800
}

static void testZeroFadePeriodIsInstant() {
    struct msm_dsi_control c;
    msm_dsi_control_init(&c, 300u);
    msm_dsi_control_request(&c, 900, 0, 10u);
    uint32_t level = 0;
    assert(msm_dsi_control_step(&c, 10u, &level) == 1);
    assert(level == 900u);
    assert(msm_dsi_control_step(&c, 10u, &level) == 0);  // no repeated transmit
}

static void testDownFadeAndRefusals() {
    struct msm_dsi_control c;
    msm_dsi_control_init(&c, 1023u);
    msm_dsi_control_request(&c, 23, 0x00008000u, 0u);  // 1023 -> 23 over 500 ms
    uint32_t level = 0;
    assert(msm_dsi_control_step(&c, 250u, &level) == 1);
    // from=1023, to=23 -> delta=-1000, halfway = 1023-500 = 523.
    assert(level == 523u);
    // Null arguments are refused without writing anything.
    assert(msm_dsi_control_step(nullptr, 1u, &level) == 0);
    assert(msm_dsi_control_step(&c, 1u, nullptr) == 0);
    msm_dsi_control_request(nullptr, 1, 0, 0u);
    msm_dsi_control_publish(nullptr, nullptr);
    assert(msm_dsi_control_fade_done(nullptr) == 1);
}

static void testPublishLayout() {
    struct msm_dsi_control c;
    msm_dsi_control_init(&c, 100u);
    c.stage = kMsmDsiControlStagePublished;
    c.requests = 7u;
    c.steps = 3u;
    c.to = 900u;
    c.from = 100u;
    c.period_ms = 1250u;
    c.last_rc = 0;
    c.impersonated = 1;
    volatile int32_t stats[10] = {0,0,0,0,0,0,0,0,0,0};
    msm_dsi_control_publish(&c, stats);
    assert(stats[0] == kMsmDsiControlStagePublished);  // st=7
    assert(stats[1] == 7);                              // req = brightness requests
    assert(stats[2] == 100);                            // lvl
    assert(stats[3] == 3);                              // a = steps
    assert(stats[4] == 900);                            // b = goal
    assert(stats[5] == 0);                              // xerr = last rc
    assert(stats[6] == 1);                              // irq = impersonated
    assert(stats[7] == 1023);                           // ea = PUBLISHED MAX (not 2047)
    assert(stats[8] == 100);                            // eb = from
    assert(stats[9] == 1250);                           // err = fade period ms
    // Null pointers leave the row untouched.
    volatile int32_t guard[10] = {9,9,9,9,9,9,9,9,9,9};
    msm_dsi_control_publish(nullptr, guard);
    msm_dsi_control_publish(&c, nullptr);
    for (int i = 0; i < 10; ++i) assert(guard[i] == 9);
}

int main() {
    testScaleMismatch();
    testFadeMs();
    testInit();
    testRequestClamps();
    testLinearFade();
    testZeroFadePeriodIsInstant();
    testDownFadeAndRefusals();
    testPublishLayout();
    std::printf("PASS: enchilada OS-facing brightness control (1023 clamp, 16.16 fade, L row) (8 test groups)\n");
    return 0;
}
