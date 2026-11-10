// SPDX-License-Identifier: MIT
/* adreno-gmu-caller-v1-ci-check.cpp - CI object-consumer check for the
 * round-46 powered-GMU caller. The CI archive build compiles
 * AdrenoGmuCallerV1.c as the 10th object; this TU references every public
 * entry point so a broken/missing object fails the link instead of
 * silently shrinking the archive. Mirrors adreno-gmu-bus-v1-ci-check.cpp. */
#include "AdrenoGmuCallerV1.h"

int caller_v1_ci_probe(void);

int caller_v1_ci_probe(void)
{
    struct AdrenoGmuCallerV1Pre pre = {1u, 1u, 1u, 0u};
    struct AdrenoGmuCallerV1Gpucc gp;
    gp.cookie = (void *)0;
    gp.read = (int (*)(void *, uint32_t, uint32_t *))0;
    AdrenoGmuBootV1Bus bus;
    bus.cookie = (void *)0;
    bus.write = (int (*)(void *, uint32_t, uint32_t, uint32_t))0;
    bus.write_bulk = (int (*)(void *, uint32_t, uint32_t, const uint32_t *, uint32_t))0;
    bus.read = (int (*)(void *, uint32_t, uint32_t, uint32_t *))0;
    uint32_t bs = 0u;
    uint32_t fail_op = 0u;

    /* every call must fail closed on the all-NULL wiring: compile-only probe */
    int acc = 0;
    acc += (int)AdrenoGmuCallerV1Precheck(&pre, &gp);
    acc += (int)AdrenoGmuCallerV1BootState(&bus, &bs);
    acc += (int)AdrenoGmuCallerV1ColdBoot(&pre, &gp, (const AdrenoGmuBootV1Inputs *)0,
                                          (const uint32_t *)0, (const uint32_t *)0,
                                          &bus, 0u, &fail_op);
    return acc;
}
