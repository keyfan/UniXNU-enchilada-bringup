// SPDX-License-Identifier: BSD-2-Clause-Patent
// Stage-3 layer 3d (alpha): the OS-facing brightness CONTROL PLANE for the enchilada
// AppleARMBacklight-facing IOService, extracted into pure C so it can be host-verified
// before the IOKit C++ ABI is compiled by CI (no macOS/Xcode locally).
//
// This is the logic of sheng package/AppleARMBacklight.cpp's publish()/setProperties()/
// beginFade()/fadeStep(), with TWO enchilada-specific corrections:
//   (1) SCALE MISMATCH (the top open risk from evidence/applearmbacklight-metaclass.md):
//       sheng's KTZ8866 I2C backlight is 11-bit and publishes IODisplayParameters
//       brightness.max = kMaxLevel = 2047. enchilada's sofef00 panel is DCS
//       bl_ctrl_dcs with bl-max = 0x3ff = 1023. Publishing max=2047, or feeding 2047
//       straight to dsi_dcs_set_brightness_large(), would silently clamp or send a
//       wrong level. This core clamps EVERY request to 0..1023 (kEnchiladaBrightnessMax
//       is the single source of truth, pulled in from DsiDcs.h) and the service must
//       publish max=1023.
//   (2) The transmit path is not sheng's GENI-I2C/KTZ8866 but the DEVICE-VERIFIED
//       msm_dsi_brightness_send() (evidence/backlight210mode3.md, CI #210 st=5). This
//       core only produces the level to send; the adapter calls the transmit.
//
// No IOKit, no clock, no register: time is injected as an absolute millisecond value
// through msm_dsi_control_step(), so the whole fade state machine is deterministic on
// the host. FadePeriod is the 16.16 fixed-point value CoreBrightness/backboardd sends
// through setProperties (sheng setProperties(): (fadePeriod->unsigned32BitValue()*1000)>>16,
// cap 5000 ms; a fade is stepped every 16 ms).
//
// Published into the existing BootLog.c L row
//   "L st=%d req=%d lvl=%d a=%d b=%d xerr=%d irq=%x ea=%x eb=%x err=%d"
// via native_backlight_stats[10] (stats[1] = brightness requests = the CoreBrightness
// acceptance signal we must measure on the real device in layer 3d-beta).

#ifndef ENCHILADA_MSM_DSI_CONTROL_H
#define ENCHILADA_MSM_DSI_CONTROL_H

#include <stddef.h>
#include <stdint.h>
#include "DsiDcs.h"   /* kEnchiladaBrightnessMax = 1023, the one and only brightness range */

#ifdef __cplusplus
extern "C" {
#endif

/* Stage codes published into native_backlight_stats[0]. Distinct from the read-only
 * probe stages (3 = DSI session, 4 = SMMU survey, 5 = first-write transmit). */
enum {
    kMsmDsiControlStageOff = 0,
    kMsmDsiControlStageInit = 6,        /* control core initialized, service not registered */
    kMsmDsiControlStagePublished = 7,   /* registerService done (impersonated or not) */
    kMsmDsiControlStageNoMeta = -1,     /* getMetaClassWithName("AppleARMBacklight") failed */
};

enum {
    /* Publish IODisplayParameters.brightness.max = THIS, never sheng's 2047. */
    kMsmDsiControlMaxLevel = kEnchiladaBrightnessMax,  /* 1023 */
    kMsmDsiControlStepMs = 16,        /* fade step, as sheng kStepMs */
    kMsmDsiControlMaxFadeMs = 5000,  /* sheng setProperties cap */
    kMsmDsiControlDefaultLevel = 512,/* mid-range starting level */
};

/* All state is plain data. start_ms/period_ms are milliseconds in the caller's clock;
 * the core never reads a clock itself. */
struct msm_dsi_control {
    uint32_t level;        /* the level currently shown on the panel */
    uint32_t from, to;     /* fade endpoints */
    uint64_t start_ms;     /* absolute ms when the fade began */
    uint64_t period_ms;    /* fade duration; 0 = instant */
    uint32_t requests;     /* setProperties() calls (= CoreBrightness brightness sets) */
    uint32_t steps;        /* fade steps that produced a new level */
    int32_t last_rc;       /* last msm_dsi_brightness_send() return, recorded by the adapter */
    int stage;             /* kMsmDsiControlStage* */
    int impersonated;      /* 1 when the service answers as AppleARMBacklight */
};

/* Clamp a signed brightness request to 0..1023. OSNumber can carry a negative value;
 * sheng clamps to 0..2047, enchilada MUST clamp to 0..1023 (the scale-mismatch fix). */
uint32_t msm_dsi_control_clamp(int32_t requested);

/* FadePeriod (16.16 seconds) -> milliseconds, capped at 5000. */
uint32_t msm_dsi_control_fade_ms(uint32_t fade_period_16_16);

/* Initialize at a starting level. Freestanding: no libc memset. */
void msm_dsi_control_init(struct msm_dsi_control *c, uint16_t initial_level);

/* One setProperties(): count the request, clamp the goal, begin a linear fade at now_ms. */
void msm_dsi_control_request(struct msm_dsi_control *c, int32_t requested,
                             uint32_t fade_period_16_16, uint64_t now_ms);

/* Advance the fade at now_ms (linear interpolation, sheng fadeStep()). Returns 1 when a
 * NEW level must be transmitted (*out_level differs from the previous level), 0 when the
 * fade is finished or the level is unchanged. A clock that moved backwards is treated as
 * elapsed=0 (level stays at `from`), never as "done". */
int msm_dsi_control_step(struct msm_dsi_control *c, uint64_t now_ms, uint32_t *out_level);

/* Nonzero when the fade has reached its goal (the adapter stops re-arming its timer). */
int msm_dsi_control_fade_done(const struct msm_dsi_control *c);

/* Publish into the L row. Pointer-to-volatile is C++20-safe (CI #207 -Wdeprecated-volatile
 * lesson: a volatile-qualified array parameter is deprecated, a pointer-to-volatile is not).
 * Null pointers are refused without touching memory. */
void msm_dsi_control_publish(const struct msm_dsi_control *c, volatile int32_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* ENCHILADA_MSM_DSI_CONTROL_H */
