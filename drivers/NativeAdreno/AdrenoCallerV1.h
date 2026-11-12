/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Round 64: the ORDERING LAYER between a mapped GPUCC window and the round-62
 * executor / round-46 caller chain.
 *
 * THE DEFECT THIS ROUND EXISTS TO FIX
 * -----------------------------------
 * Round 63's promise was "an IOService that owns the mapping, calls
 * Precheck, then Bind, then Make/Up". Read in the order a driver would
 * actually execute it, that sentence is impossible, and the impossibility is
 * the point:
 *
 *   AdrenoGmuCallerV1Precheck() requires pre->cx_genpd_on != 0,
 *   pre->gx_genpd_on != 0 and pre->clocks_enabled != 0.
 *
 * Those three fields are PRODUCED BY AdrenoPowerUpBackendV1Up(). So the gate
 * cannot run before the power-up: it consumes the power-up's output. Round 62's
 * header says "the gate that must run first is AdrenoGmuCallerV1Precheck, and
 * the whole point of returning an AdrenoGmuCallerV1Pre from Up() is to feed
 * exactly that" -- both halves of that sentence are true, and together they
 * mean Precheck runs LAST, after the MMIO has already happened.
 *
 * That is not a flaw in either module. It does mean the FIRST GPUCC access of
 * any real bring-up is a WRITE: the CX enable plan's collapse clear at 0x106c.
 * Every device-verified result this port has (rounds 35, 49, 52) began with a
 * READ of the CX/GX status words and decided from the answer. The executor
 * begins by writing, and nothing before it observes anything.
 *
 * WHAT THIS MODULE DOES ABOUT IT
 * ------------------------------
 * It does not try to turn Precheck into a pre-gate -- there is no honest way to
 * fake three bits that only the power-up produces, and inventing them would be
 * exactly the "assert what you want, then verify it" move this port has spent
 * 60 rounds avoiding. Instead it inserts the observation that the executor
 * lacks:
 *
 *   OBSERVE  read 0x1540 and 0x1010, decode, record        <- first traffic
 *   POWER    AdrenoPowerUpBackendV1Up()                    <- the writes
 *   OBSERVE  read 0x1540 and 0x1010 again, decode, record
 *   VERDICT  AdrenoGmuCallerV1Precheck(pre, gpucc)
 *
 * Two consequences, both enforced in code and both tested:
 *
 *  1. The first MMIO operation is a READ. If the pre-observation cannot be
 *     read, Up() is never called and there are ZERO writes. A window that is
 *     mapped but dead (wrong base, clock gated, domain truly gone) fails at the
 *     cheap observation instead of at the first store.
 *  2. A successful Up() and a successful Precheck are DIFFERENT results. Up()
 *     succeeding means the five ops executed. Precheck then re-reads the CX
 *     word the executor's own poll accepted and asks whether it says CX is on.
 *     On this board that answer is genuinely unknown -- round 35 measured
 *     cx=00005ff4, BIT(31) clear, and nothing here proves the collapse clear
 *     raises it -- so Ok and PrecheckRefused are separate verdicts and the
 *     report says which one happened.
 *
 * WHAT IT STILL CANNOT DO
 * -----------------------
 * The pre-observation is only as good as the register it reads. It proves the
 * GPUCC window answers; it does not prove the block behind it is powered. And
 * an observed CX-down state does not make the following write illegal --
 * writing gpu_cx_gdsc's own control register is how a collapsed domain is
 * raised, and that register is in always-on GPUCC (round 28/35). So this
 * module OBSERVES and RECORDS; it does not veto on the observation, because a
 * veto there would be a rule with no hardware basis.
 *
 * HONEST BOUNDARY
 * ---------------
 * HOST-VERIFIED. Pure C, freestanding, no mapping, no clock, no regulator, no
 * power-domain service, no IOKit. It performs MMIO only through the caller's
 * three callbacks and it maps nothing. Every byte it can reach is inside the
 * window it was handed, and the window adapter (round 63) bounds that; this
 * module additionally asserts, in Verify and in the test, that the offsets it
 * ever asks for are the five the executor derives. It has never run on a
 * device: the round-22/28 prohibitions are UNCHANGED and are not enforced here.
 */
#ifndef ADRENO_CALLER_V1_H
#define ADRENO_CALLER_V1_H

#include <stdint.h>
#include <stddef.h>

#include "AdrenoGmuCallerV1.h"
#include "AdrenoPowerUpBackendV1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes. Deliberately NOT a boolean and deliberately not the round-46
 * enum: "the plan executed" and "the chain is ready to boot" are different
 * facts and collapsing them would lose the round's only interesting outcome. */
typedef enum {
    kAdrenoCallerV1Ok = 0,             /* Up() ran AND Precheck accepted      */
    kAdrenoCallerV1Invalid = 1,        /* a NULL argument                     */
    kAdrenoCallerV1Disarmed = 2,       /* inputs.armed != 1: nothing touched  */
    kAdrenoCallerV1ObserveFailed = 3,  /* pre-observation read failed, no UP  */
    kAdrenoCallerV1UpFailed = 4,       /* Up() failed; see fail_index         */
    kAdrenoCallerV1PostObserveFailed = 5,
    kAdrenoCallerV1PrecheckRefused = 6 /* Up() succeeded; Precheck did not    */
} AdrenoCallerV1Status;

/* One observation of the two status registers. */
struct AdrenoCallerV1Observation {
    uint32_t cx;             /* 0x1540 word                                  */
    uint32_t gx;             /* 0x1010 word                                  */
    uint32_t cx_on;          /* adreno_domain_cx_is_on(cx)                   */
    uint32_t gx_on;          /* adreno_domain_gx_is_on(gx)                   */
    uint32_t gx_off;         /* adreno_domain_gx_is_off(gx)                  */
    uint32_t valid;          /* 1 when both reads returned                   */
};

/* Everything one call produced, for the one log line the driver publishes. */
struct AdrenoCallerV1Report {
    struct AdrenoCallerV1Observation before;
    struct AdrenoCallerV1Observation after;
    struct AdrenoGmuCallerV1Pre pre;
    AdrenoPowerUpV1Status up_status;
    uint32_t up_fail_index;
    AdrenoGmuCallerV1Status precheck_status;
    struct AdrenoPowerUpBackendV1Stats stats;
    uint32_t ops;            /* MMIO operations the caller attempted        */
};

struct AdrenoCallerV1 {
    struct AdrenoPowerUpBackendV1 backend;
    struct AdrenoGmuCallerV1Gpucc gpucc;
    struct AdrenoPowerUpBackendV1Mmio mmio;    /* the caller's triple, copied */
    struct AdrenoPowerUpBackendV1Mmio wrapped; /* the choke point the backend
                                                * is actually wired to, so every
                                                * executor access is counted by
                                                * this layer                       */
    uint32_t armed;
    uint32_t ops;
    uint32_t saw_op;              /* 1 once any MMIO callback has returned   */
    uint32_t first_op_was_read;   /* the invariant of this round             */
    uint32_t ready;               /* Make() succeeded                        */
};

/* Wire the caller. Refuses (non-zero) when any argument is NULL, when the MMIO
 * triple is incomplete, or when in->armed != 1. `mmio` is copied, so the caller
 * may keep a stack temporary; the WINDOW it points into must outlive this
 * struct, because read32/write32 are called through it later.
 *
 * in->armed is checked here AND in AdrenoPowerUpBackendV1Make(): two
 * independent refusals rather than one shared flag, so a caller that
 * constructs a backend for inspection cannot arm this layer by accident. */
int AdrenoCallerV1Make(struct AdrenoCallerV1 *c,
                       const struct AdrenoPowerUpBackendV1Mmio *mmio,
                       const struct AdrenoPowerUpBackendV1Inputs *in);

/* OBSERVE -> POWER -> OBSERVE -> VERDICT. Fills *out on every path that is not
 * a NULL argument, including the failure paths, so the driver can log what it
 * saw rather than only what went wrong. Never leaves *out partially written
 * without marking stats/ops. */
AdrenoCallerV1Status AdrenoCallerV1Run(struct AdrenoCallerV1 *c,
                                       struct AdrenoCallerV1Report *out);

/* Self-check. Returns 0 when every invariant holds, else the 1-based index of
 * the first failure. Proves the ORDER (observe before power), the separation of
 * the two verdicts, and that a failed pre-observation produces zero writes. It
 * does NOT probe a register. */
int AdrenoCallerV1Verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_CALLER_V1_H */
