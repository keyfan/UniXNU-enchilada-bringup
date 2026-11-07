// SPDX-License-Identifier: BSD-2-Clause-Patent
// Stage-3 layer 3d (alpha): the OS-facing brightness CONTROL PLANE. Pure C, no IOKit,
// no clock, no register. See MsmDsiControl.h for the two enchilada-specific corrections
// (0..1023 clamp vs sheng's 0..2047, and the transmit-path split) and the L-row layout.
//
// Mirrors sheng package/AppleARMBacklight.cpp:
//   publish(goal)               -> setProperty("IODisplayParameters", {min,max,value})
//   setProperties()             -> clamp, FadePeriod, gRequest, publish(goal)
//   beginFade()/fadeStep()      -> linear from level to goal over the fade period, 16 ms steps
// with the scale/range fixed for the sofef00 DCS panel (bl-max 0x3ff).

#include "MsmDsiControl.h"

/* Freestanding: zero by hand, no memset (the arm64 XNU kernel object has no libc). */
static void zero(struct msm_dsi_control *c) {
    uint8_t *b = (uint8_t *)c;
    for (size_t i = 0; i < sizeof(*c); ++i) b[i] = 0u;
}

uint32_t msm_dsi_control_clamp(int32_t requested) {
    if (requested <= 0) return 0u;
    if ((uint32_t)requested > (uint32_t)kMsmDsiControlMaxLevel)
        return (uint32_t)kMsmDsiControlMaxLevel;
    return (uint32_t)requested;
}

uint32_t msm_dsi_control_fade_ms(uint32_t fade_period_16_16) {
    /* sheng setProperties(): ((uint64_t)fadePeriod->unsigned32BitValue() * 1000) >> 16;
     * cap at 5000 ms. Use 64-bit to avoid the 32-bit overflow of a large FadePeriod. */
    uint64_t ms = ((uint64_t)fade_period_16_16 * 1000u) >> 16;
    if (ms > (uint64_t)kMsmDsiControlMaxFadeMs) ms = (uint64_t)kMsmDsiControlMaxFadeMs;
    return (uint32_t)ms;
}

void msm_dsi_control_init(struct msm_dsi_control *c, uint16_t initial_level) {
    if (!c) return;
    zero(c);
    c->level = msm_dsi_control_clamp((int32_t)initial_level);
    c->from = c->level;
    c->to = c->level;
    c->stage = kMsmDsiControlStageInit;
}

void msm_dsi_control_request(struct msm_dsi_control *c, int32_t requested,
                             uint32_t fade_period_16_16, uint64_t now_ms) {
    if (!c) return;
    const uint32_t goal = msm_dsi_control_clamp(requested);
    c->requests = c->requests + 1u;
    c->from = c->level;
    c->to = goal;
    c->start_ms = now_ms;
    c->period_ms = (uint64_t)msm_dsi_control_fade_ms(fade_period_16_16);
}

int msm_dsi_control_step(struct msm_dsi_control *c, uint64_t now_ms, uint32_t *out_level) {
    if (!c || !out_level) return 0;
    /* Default to the goal so an elapsed >= period (or a zero period) completes the fade. */
    uint32_t next = c->to;
    if (c->period_ms != 0u) {
        const uint64_t elapsed = now_ms >= c->start_ms ? now_ms - c->start_ms : 0u;
        if (elapsed < c->period_ms) {
            /* Linear interpolation. (to-from) fits int64 (range 0..1023); the product
             * with elapsed (< 5000) cannot overflow. */
            const int64_t delta = (int64_t)c->to - (int64_t)c->from;
            const int64_t part = delta * (int64_t)elapsed / (int64_t)c->period_ms;
            next = (uint32_t)((int64_t)c->from + part);
        }
    }
    *out_level = next;
    if (next == c->level) return 0;
    c->level = next;
    c->steps = c->steps + 1u;
    return 1;
}

int msm_dsi_control_fade_done(const struct msm_dsi_control *c) {
    if (!c) return 1;
    return c->level == c->to;
}

void msm_dsi_control_publish(const struct msm_dsi_control *c, volatile int32_t *stats) {
    if (!c || !stats) return;
    /* BootLog.c:749 "L st=%d req=%d lvl=%d a=%d b=%d xerr=%d irq=%x ea=%x eb=%x err=%d".
     * stage 7 = published, req = brightness requests (CoreBrightness acceptance signal),
     * lvl = current level, a = steps, b = goal, xerr = last transmit rc, irq = impersonated,
     * ea = max level published (1023, NOT sheng's 2047), eb = from, err = fade period ms. */
    stats[0] = (int32_t)c->stage;
    stats[1] = (int32_t)c->requests;
    stats[2] = (int32_t)c->level;
    stats[3] = (int32_t)c->steps;
    stats[4] = (int32_t)c->to;
    stats[5] = (int32_t)c->last_rc;
    stats[6] = (int32_t)c->impersonated;
    stats[7] = (int32_t)kMsmDsiControlMaxLevel;
    stats[8] = (int32_t)c->from;
    stats[9] = (int32_t)c->period_ms;
}
