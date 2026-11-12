/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Round 64: AdrenoCallerV1 -- OBSERVE, POWER, OBSERVE, VERDICT.
 *
 * See AdrenoCallerV1.h for why the round-63 plan ("Precheck, then Up") cannot
 * be executed in that order, and what this module does instead of faking it.
 *
 * Style follows AdrenoPowerUpGdsc.c / AdrenoPowerUpBackendV1.c: C11
 * freestanding, no libc, no MMIO of its own, no constructor. Every offset and
 * every count is computed at call time from the round-46 / round-62 / round-63
 * definitions -- nothing here hardcodes a register byte, a write count or a
 * status bit. Return convention is inverted relative to the round-57 planner:
 * 0 means success everywhere in this file, and each call site converts
 * explicitly.
 */
#include "AdrenoCallerV1.h"

#include "AdrenoDomainState.h"
#include "AdrenoGpuccWindowV1.h"

/* ------------------------------------------------------------------------- */
/* The choke point                                                            */
/* ------------------------------------------------------------------------- */
/* Every MMIO access this module causes -- its own two observations AND every
 * access the round-62 executor makes -- passes through these three functions.
 * That is the whole reason the backend is wired to `wrapped` instead of
 * directly to the caller's triple: without a single choke point, "the first
 * access was a read" would be a claim about one code path out of two.
 *
 * The order flag is set on INVOCATION, not on return: what this round asserts
 * is the order of the ATTEMPTS, so a first read that then fails still counts as
 * a read-first sequence. */

static void cli_note_op(struct AdrenoCallerV1 *c, uint32_t is_read)
{
    if (c->saw_op == 0u) {
        c->saw_op = 1u;
        c->first_op_was_read = is_read;
    }
    c->ops += 1u;
}

static int cli_read_counted(struct AdrenoCallerV1 *c, uint32_t byte_offset,
                            uint32_t *out)
{
    cli_note_op(c, 1u);
    return c->mmio.read32(c->mmio.cookie, byte_offset, out);
}

static int cli_write_counted(struct AdrenoCallerV1 *c, uint32_t byte_offset,
                             uint32_t value)
{
    cli_note_op(c, 0u);
    return c->mmio.write32(c->mmio.cookie, byte_offset, value);
}

/* Backend thunks: the executor reaches the window only through these. */
static int cli_thunk_read32(void *cookie, uint32_t byte_offset, uint32_t *out)
{
    return cli_read_counted((struct AdrenoCallerV1 *)cookie, byte_offset, out);
}

static int cli_thunk_write32(void *cookie, uint32_t byte_offset, uint32_t value)
{
    return cli_write_counted((struct AdrenoCallerV1 *)cookie, byte_offset, value);
}

static int cli_thunk_udelay(void *cookie, uint32_t usec)
{
    struct AdrenoCallerV1 *c = (struct AdrenoCallerV1 *)cookie;
    /* A delay is neither a read nor a write; it must not claim the first-op
     * slot. It is still counted as traffic, because a poll loop that spins
     * without ever reaching the window is a distinct failure mode. */
    c->ops += 1u;
    return c->mmio.udelay(c->mmio.cookie, usec);
}

/* Gpucc reader thunk for AdrenoGmuCallerV1Precheck(). Same choke point, so the
 * observations take part in the ordering claim rather than bypassing it. */
static int cli_thunk_gpucc_read(void *cookie, uint32_t byte_offset,
                                uint32_t *out)
{
    return cli_read_counted((struct AdrenoCallerV1 *)cookie, byte_offset, out);
}

/* ------------------------------------------------------------------------- */
/* Observation                                                                */
/* ------------------------------------------------------------------------- */
/* One read of each status word, decoded through the round-33 predicates. The
 * raw words are kept in the report: on a board where the expected bit has never
 * been observed to move, the hex is worth more than the boolean.
 *
 * Returns 0 when both reads returned, 1 otherwise. All fields are written
 * before returning either way. */
static int cli_observe(struct AdrenoCallerV1 *c,
                       struct AdrenoCallerV1Observation *obs)
{
    uint32_t cx = 0u;
    uint32_t gx = 0u;

    obs->cx = 0u;
    obs->gx = 0u;
    obs->cx_on = 0u;
    obs->gx_on = 0u;
    obs->gx_off = 0u;
    obs->valid = 0u;

    if (cli_read_counted(c, ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE, &cx) != 0) {
        return 1;
    }
    if (cli_read_counted(c, ADRENO_GMU_CALLER_V1_GX_STATUS_BYTE, &gx) != 0) {
        return 1;
    }

    obs->cx = cx;
    obs->gx = gx;
    obs->cx_on = (uint32_t)adreno_domain_cx_is_on(cx);
    obs->gx_on = (uint32_t)adreno_domain_gx_is_on(gx);
    obs->gx_off = (uint32_t)adreno_domain_gx_is_off(gx);
    obs->valid = 1u;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Make                                                                       */
/* ------------------------------------------------------------------------- */
int AdrenoCallerV1Make(struct AdrenoCallerV1 *c,
                       const struct AdrenoPowerUpBackendV1Mmio *mmio,
                       const struct AdrenoPowerUpBackendV1Inputs *in)
{
    if (c == NULL || mmio == NULL || in == NULL) {
        return 1;
    }
    if (mmio->read32 == NULL || mmio->write32 == NULL || mmio->udelay == NULL) {
        return 1;
    }
    if (in->armed != 1u) {
        return 1;
    }

    c->mmio = *mmio;
    c->armed = 0u;
    c->ops = 0u;
    c->saw_op = 0u;
    c->first_op_was_read = 0u;
    c->ready = 0u;

    c->wrapped.cookie = (void *)c;
    c->wrapped.read32 = cli_thunk_read32;
    c->wrapped.write32 = cli_thunk_write32;
    c->wrapped.udelay = cli_thunk_udelay;

    c->gpucc.cookie = (void *)c;
    c->gpucc.read = cli_thunk_gpucc_read;

    /* Constructing the backend performs no MMIO, so this cannot move the
     * first-op flag; Verify asserts exactly that. */
    if (AdrenoPowerUpBackendV1Make(&c->backend, &c->wrapped, in) != 0) {
        return 1;
    }

    c->armed = in->armed;
    c->ready = 1u;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Run                                                                        */
/* ------------------------------------------------------------------------- */
AdrenoCallerV1Status AdrenoCallerV1Run(struct AdrenoCallerV1 *c,
                                       struct AdrenoCallerV1Report *out)
{
    uint32_t fail_index = 0u;
    AdrenoPowerUpV1Status up_status;
    AdrenoGmuCallerV1Status verdict;

    if (out == NULL) {
        return kAdrenoCallerV1Invalid;
    }

    /* Clear the report first: every path below may return early, and a caller
     * that logs the report must never read a stale field. */
    out->before.cx = 0u;
    out->before.gx = 0u;
    out->before.cx_on = 0u;
    out->before.gx_on = 0u;
    out->before.gx_off = 0u;
    out->before.valid = 0u;
    out->after = out->before;
    out->pre.cx_genpd_on = 0u;
    out->pre.gx_genpd_on = 0u;
    out->pre.clocks_enabled = 0u;
    out->pre.require_gx = 0u;
    out->up_status = kAdrenoPowerUpV1Invalid;
    out->up_fail_index = 0u;
    out->precheck_status = kAdrenoGmuCallerV1Invalid;
    out->stats.reads = 0u;
    out->stats.writes = 0u;
    out->stats.delays = 0u;
    out->stats.polls = 0u;
    out->stats.refused = 0u;
    out->ops = 0u;

    if (c == NULL || c->ready == 0u) {
        return kAdrenoCallerV1Invalid;
    }
    if (c->armed != 1u) {
        return kAdrenoCallerV1Disarmed;
    }

    /* ---- 1. OBSERVE (this is the first GPUCC access of the bring-up) ---- */
    if (cli_observe(c, &out->before) != 0) {
        out->stats = c->backend.stats;
        out->ops = c->ops;
        return kAdrenoCallerV1ObserveFailed;
    }

    /* CX-down here is recorded, NOT vetoed. Writing gpu_cx_gdsc's own control
     * register is how a collapsed domain is raised, and that register lives in
     * always-on GPUCC (rounds 28/35). A veto on this observation would be a
     * rule with no hardware behind it. */

    out->pre.require_gx = c->backend.in.strict_gx;

    /* ---- 2. POWER (the only writes in this module's control) ---- */
    up_status = AdrenoPowerUpBackendV1Up(&c->backend, &out->pre, &fail_index);
    out->up_status = up_status;
    out->up_fail_index = fail_index;

    if (up_status != kAdrenoPowerUpV1Ok) {
        out->stats = c->backend.stats;
        out->ops = c->ops;
        return kAdrenoCallerV1UpFailed;
    }

    /* ---- 3. OBSERVE again ---- */
    if (cli_observe(c, &out->after) != 0) {
        out->stats = c->backend.stats;
        out->ops = c->ops;
        return kAdrenoCallerV1PostObserveFailed;
    }

    /* ---- 4. VERDICT -------------------------------------------------------
     * The honest part: Precheck re-reads the STATUS WORDS, but it decides from
     * `pre`, and `pre` came from the executor. So a refusal here does NOT mean
     * the writes failed -- it means the assertion the executor produced is not
     * confirmed by the silicon yet. Both outcomes are reported separately
     * because "the plan ran" and "the chain may boot" are different facts. */
    verdict = AdrenoGmuCallerV1Precheck(&out->pre, &c->gpucc);
    out->precheck_status = verdict;

    out->stats = c->backend.stats;
    out->ops = c->ops;

    if (verdict != kAdrenoGmuCallerV1Ok) {
        return kAdrenoCallerV1PrecheckRefused;
    }
    return kAdrenoCallerV1Ok;
}

/* ------------------------------------------------------------------------- */
/* Verify                                                                     */
/* ------------------------------------------------------------------------- */
#define ACC_REQUIRE(cond)                                                      \
    do {                                                                       \
        count += 1u;                                                           \
        if (!(cond)) {                                                         \
            if (check_count != NULL) {                                         \
                *check_count = count;                                          \
            }                                                                  \
            return (int)count;                                                 \
        }                                                                      \
    } while (0)

/* A read that always fails, and a write that would be a bug if reached. No
 * windows, no registers: this only proves what the module does before it has
 * touched anything. */
static int acc_verify_fail_read(void *cookie, uint32_t byte_offset,
                                uint32_t *out)
{
    (void)cookie;
    (void)byte_offset;
    (void)out;
    return 1;
}

static int acc_verify_trap_write(void *cookie, uint32_t byte_offset,
                                 uint32_t value)
{
    (void)cookie;
    (void)byte_offset;
    (void)value;
    return 1;
}

static int acc_verify_noop_delay(void *cookie, uint32_t usec)
{
    (void)cookie;
    (void)usec;
    return 0;
}

int AdrenoCallerV1Verify(uint32_t *check_count)
{
    uint32_t count = 0u;
    struct AdrenoCallerV1 c;
    struct AdrenoCallerV1Report rep;
    struct AdrenoPowerUpBackendV1Inputs in;
    struct AdrenoPowerUpBackendV1Mmio mmio;
    AdrenoPowerUpV1Status up;
    uint32_t fail_index = 0u;

    in.armed = 1u;
    in.gxpd_present = 1u;
    in.rcg_enabled = 0u;
    in.core_rate_hz = AdrenoPowerUpV1CoreRateHz();
    in.strict_gx = 0u;

    mmio.cookie = NULL;
    mmio.read32 = acc_verify_fail_read;
    mmio.write32 = acc_verify_trap_write;
    mmio.udelay = acc_verify_noop_delay;

    /* -- argument refusal ------------------------------------------------ */
    ACC_REQUIRE(AdrenoCallerV1Make(NULL, &mmio, &in) != 0);
    ACC_REQUIRE(AdrenoCallerV1Make(&c, NULL, &in) != 0);
    ACC_REQUIRE(AdrenoCallerV1Make(&c, &mmio, NULL) != 0);
    ACC_REQUIRE(AdrenoCallerV1Run(NULL, &rep) == kAdrenoCallerV1Invalid);

    /* -- incomplete triple ------------------------------------------------ */
    {
        struct AdrenoPowerUpBackendV1Mmio hole = mmio;
        hole.udelay = NULL;
        ACC_REQUIRE(AdrenoCallerV1Make(&c, &hole, &in) != 0);
        hole = mmio;
        hole.read32 = NULL;
        ACC_REQUIRE(AdrenoCallerV1Make(&c, &hole, &in) != 0);
        hole = mmio;
        hole.write32 = NULL;
        ACC_REQUIRE(AdrenoCallerV1Make(&c, &hole, &in) != 0);
    }

    /* -- disarmed is a second, independent refusal ------------------------ */
    {
        struct AdrenoPowerUpBackendV1Inputs off_in = in;
        off_in.armed = 0u;
        ACC_REQUIRE(AdrenoCallerV1Make(&c, &mmio, &off_in) != 0);
        off_in.armed = 2u;
        ACC_REQUIRE(AdrenoCallerV1Make(&c, &mmio, &off_in) != 0);
    }

    /* -- building performs no MMIO ---------------------------------------- */
    ACC_REQUIRE(AdrenoCallerV1Make(&c, &mmio, &in) == 0);
    ACC_REQUIRE(c.ready == 1u);
    ACC_REQUIRE(c.armed == 1u);
    ACC_REQUIRE(c.ops == 0u);          /* constructing touched nothing */
    ACC_REQUIRE(c.saw_op == 0u);
    ACC_REQUIRE(c.first_op_was_read == 0u); /* not yet meaningful */

    /* -- an unobservable window costs ZERO writes -------------------------
     * This is the round's load-bearing claim, and it is checked against a
     * caller wired to a read that always fails. If the order were ever
     * reversed -- power first, observe after -- this would attempt a write. */
    ACC_REQUIRE(AdrenoCallerV1Run(&c, &rep) == kAdrenoCallerV1ObserveFailed);
    ACC_REQUIRE(c.saw_op == 1u);
    ACC_REQUIRE(c.first_op_was_read == 1u);  /* the invariant of this round */
    ACC_REQUIRE(c.ops == 1u);              /* stopped at the first read */
    ACC_REQUIRE(c.backend.stats.writes == 0u);
    ACC_REQUIRE(c.backend.stats.reads == 0u); /* the read failed before it
                                               * reached the executor's gate */
    ACC_REQUIRE(rep.before.valid == 0u);
    ACC_REQUIRE(rep.after.valid == 0u);
    ACC_REQUIRE(rep.up_status == kAdrenoPowerUpV1Invalid);
    ACC_REQUIRE(rep.precheck_status == kAdrenoGmuCallerV1Invalid);
    ACC_REQUIRE(rep.pre.cx_genpd_on == 0u);

    /* -- a run on an unbuilt caller is refused, not attempted ------------- */
    {
        struct AdrenoCallerV1 unused;
        unused.ready = 0u;
        unused.armed = 1u;
        ACC_REQUIRE(AdrenoCallerV1Run(&unused, &rep) ==
                    kAdrenoCallerV1Invalid);
    }

    /* -- the verdict enum keeps the two outcomes apart -------------------- */
    ACC_REQUIRE(kAdrenoCallerV1Ok != kAdrenoCallerV1UpFailed);
    ACC_REQUIRE(kAdrenoCallerV1Ok != kAdrenoCallerV1PrecheckRefused);
    ACC_REQUIRE(kAdrenoCallerV1UpFailed != kAdrenoCallerV1PrecheckRefused);
    ACC_REQUIRE(kAdrenoCallerV1Disarmed != kAdrenoCallerV1Invalid);

    /* -- the offsets observed are the two the round-46 gate reads --------- */
    ACC_REQUIRE(ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE ==
                ADRENO_DOMAIN_CX_GDS_HW_CTRL);
    ACC_REQUIRE(ADRENO_GMU_CALLER_V1_GX_STATUS_BYTE < 0x9000u);
    ACC_REQUIRE(ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE <
                ADRENO_GPUCC_WINDOW_V1_DTB_BYTES);

    /* -- the executor still refuses an unarmed backend -------------------- */
    up = AdrenoPowerUpBackendV1Up(&c.backend, &rep.pre, &fail_index);
    ACC_REQUIRE(up != kAdrenoPowerUpV1Ok);

    if (check_count != NULL) {
        *check_count = count;
    }
    return 0;
}
