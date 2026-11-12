/* SPDX-License-Identifier: MIT */
/* Round 63: bounded GPUCC window adapter (see AdrenoGpuccWindowV1.h).
 * Freestanding pure C; no mapping, clock, regulator or power action here. */
#include "AdrenoGpuccWindowV1.h"

/* ---- helpers ------------------------------------------------------------ */

static int agw_aligned(uint32_t value, uint32_t stride)
{
    if (stride == 0u) {
        return 0;
    }
    return (value % stride) == 0u;
}

/* The window really serves min(bytes, max_register + 4). Computed, never
 * stored, so it cannot disagree with the two values it comes from. */
uint32_t AdrenoGpuccWindowV1UsableBytes(const struct AdrenoGpuccWindowV1 *w)
{
    uint64_t limit;

    if (w == (const struct AdrenoGpuccWindowV1 *)0) {
        return 0u;
    }
    /* max_register is the highest ADDRESSABLE byte offset, so the byte count
     * it implies is max_register + one register. Done in 64 bits so a caller
     * that passes 0xffffffff cannot wrap it to 0. */
    limit = (uint64_t)w->max_register + (uint64_t)ADRENO_GPUCC_WINDOW_V1_STRIDE;
    if (limit > (uint64_t)w->bytes) {
        return w->bytes;
    }
    return (uint32_t)limit;
}

int AdrenoGpuccWindowV1Bind(struct AdrenoGpuccWindowV1 *w,
                            volatile uint8_t *base, uint32_t bytes)
{
    if (w == (struct AdrenoGpuccWindowV1 *)0 || base == (volatile uint8_t *)0) {
        return 1;
    }
    if (bytes < ADRENO_GPUCC_WINDOW_V1_STRIDE) {
        return 1;                       /* cannot hold one register        */
    }
    if (!agw_aligned(bytes, ADRENO_GPUCC_WINDOW_V1_STRIDE)) {
        return 1;                       /* a partial register at the end   */
    }
    /* Refuse an unaligned window rather than rounding it: rounding would
     * silently move every register this adapter hands out. */
    if (!agw_aligned((uint32_t)(uintptr_t)base, ADRENO_GPUCC_WINDOW_V1_STRIDE)) {
        return 1;
    }
    if (!agw_aligned(ADRENO_GPUCC_WINDOW_V1_MAX_REGISTER,
                     ADRENO_GPUCC_WINDOW_V1_STRIDE)) {
        return 1;                       /* the derived bound itself is bad  */
    }
    w->base = base;
    w->bytes = bytes;
    w->max_register = ADRENO_GPUCC_WINDOW_V1_MAX_REGISTER;
    w->reads = 0u;
    w->writes = 0u;
    w->refused = 0u;
    /* The delay is a separate decision: Bind() succeeds without one, and
     * Mmio() is what refuses until it is supplied. */
    w->delay_cookie = (void *)0;
    w->udelay = (int (*)(void *, uint32_t))0;
    return 0;
}

int AdrenoGpuccWindowV1SetDelay(struct AdrenoGpuccWindowV1 *w,
                               void *cookie,
                               int (*fn)(void *cookie, uint32_t usec))
{
    if (w == (struct AdrenoGpuccWindowV1 *)0 || fn == 0) {
        return 1;
    }
    w->delay_cookie = cookie;
    w->udelay = fn;
    return 0;
}

int AdrenoGpuccWindowV1Resolve(struct AdrenoGpuccWindowV1 *w, uint32_t byte_offset,
                               volatile uint32_t **out)
{
    uint32_t usable;

    if (w == (struct AdrenoGpuccWindowV1 *)0 || out == (volatile uint32_t **)0) {
        return 1;
    }
    if (w->base == (volatile uint8_t *)0) {
        w->refused++;
        return 1;                       /* unbound window                  */
    }
    /* Bound 1: register granularity. */
    if (!agw_aligned(byte_offset, ADRENO_GPUCC_WINDOW_V1_STRIDE)) {
        w->refused++;
        return 1;
    }
    /* Bound 3: the vendor regmap limit. Checked against usable, which already
     * folds in bound 2, so a window smaller than max_register cannot widen the
     * limit by accident. */
    usable = AdrenoGpuccWindowV1UsableBytes(w);
    if (usable < ADRENO_GPUCC_WINDOW_V1_STRIDE) {
        w->refused++;
        return 1;
    }
    /* Bound 2: the last 4 bytes stay addressable: offset + 4 <= usable. */
    if ((uint64_t)byte_offset + (uint64_t)ADRENO_GPUCC_WINDOW_V1_STRIDE >
        (uint64_t)usable) {
        w->refused++;
        return 1;
    }
    *out = (volatile uint32_t *)(w->base + byte_offset);
    return 0;
}

/* ---- the MMIO triple the round-62 executor consumes ---------------------- */

static int agw_read32(void *cookie, uint32_t byte_offset, uint32_t *out)
{
    struct AdrenoGpuccWindowV1 *w = (struct AdrenoGpuccWindowV1 *)cookie;
    volatile uint32_t *p;

    if (w == (struct AdrenoGpuccWindowV1 *)0 || out == (uint32_t *)0) {
        return 1;
    }
    if (AdrenoGpuccWindowV1Resolve(w, byte_offset, &p) != 0) {
        return 1;
    }
    *out = *p;
    w->reads++;
    return 0;
}

static int agw_write32(void *cookie, uint32_t byte_offset, uint32_t value)
{
    struct AdrenoGpuccWindowV1 *w = (struct AdrenoGpuccWindowV1 *)cookie;
    volatile uint32_t *p;

    if (w == (struct AdrenoGpuccWindowV1 *)0) {
        return 1;
    }
    if (AdrenoGpuccWindowV1Resolve(w, byte_offset, &p) != 0) {
        return 1;
    }
    *p = value;
    w->writes++;
    return 0;
}

static int agw_udelay(void *cookie, uint32_t usec)
{
    /* The executor hands this thunk the SAME cookie it hands read/write (the
     * window), but the delay itself belongs to the caller, so it is forwarded
     * to the caller's own delay_cookie -- the window does not own a delay. */
    struct AdrenoGpuccWindowV1 *w = (struct AdrenoGpuccWindowV1 *)cookie;

    if (w == (struct AdrenoGpuccWindowV1 *)0 || w->udelay == 0) {
        return 1;
    }
    return w->udelay(w->delay_cookie, usec);
}

int AdrenoGpuccWindowV1Mmio(struct AdrenoGpuccWindowV1 *w,
                            struct AdrenoPowerUpBackendV1Mmio *out)
{
    if (w == (struct AdrenoGpuccWindowV1 *)0 || out == (struct AdrenoPowerUpBackendV1Mmio *)0) {
        return 1;
    }
    if (w->base == (volatile uint8_t *)0) {
        return 1;                       /* unbound window                  */
    }
    if (w->udelay == 0) {
        /* Fail-closed rather than handing the executor a NULL delay: the GDSC
         * leg calls udelay between its collapse-clearing write and its status
         * poll, and a NULL there is a crash in the one place the code cannot
         * recover from. */
        return 1;
    }
    out->cookie = (void *)w;
    out->read32 = agw_read32;
    out->write32 = agw_write32;
    out->udelay = agw_udelay;
    return 0;
}

/* ---- self-check --------------------------------------------------------- */

#define AGW_REQUIRE(cond)                                                      \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            if (check_count != (uint32_t *)0) {                                \
                *check_count = checks;                                         \
            }                                                                  \
            return (int)checks;                                                \
        }                                                                      \
    } while (0)

int AdrenoGpuccWindowV1Verify(uint32_t *check_count)
{
    uint32_t checks = 0u;
    /* A stand-in window: aligned, big enough, never dereferenced by Verify. */
    static uint32_t fake[ADRENO_GPUCC_WINDOW_V1_DTB_BYTES / 4u];
    struct AdrenoGpuccWindowV1 w;
    struct AdrenoPowerUpBackendV1Mmio mmio;
    volatile uint32_t *p;

    /* --- the derived constants must not have drifted -------------------- */
    AGW_REQUIRE(ADRENO_GPUCC_WINDOW_V1_STRIDE == 4u);
    AGW_REQUIRE(ADRENO_GPUCC_WINDOW_V1_MAX_REGISTER == 0x8008u);
    AGW_REQUIRE(ADRENO_GPUCC_WINDOW_V1_DTB_BYTES == 0x9000u);
    /* The whole reason this module exists: the DTB window and the regmap limit
     * are DIFFERENT numbers. If they ever became equal the third bound would
     * silently stop doing anything. */
    AGW_REQUIRE(ADRENO_GPUCC_WINDOW_V1_MAX_REGISTER + ADRENO_GPUCC_WINDOW_V1_STRIDE
                < ADRENO_GPUCC_WINDOW_V1_DTB_BYTES);

    /* --- bind rejects malformed windows --------------------------------- */
    AGW_REQUIRE(AdrenoGpuccWindowV1Bind((struct AdrenoGpuccWindowV1 *)0,
                                        (volatile uint8_t *)fake, 0x9000u) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)0, 0x9000u) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)fake, 0u) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)fake, 2u) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)fake, 0x9002u) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Bind(&w,
                                        (volatile uint8_t *)fake + 2u, 0x9000u) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Bind(&w, (volatile uint8_t *)fake, 0x9000u) == 0);

    /* --- usable size is the DERIVED limit, not the DTB size -------------- */
    AGW_REQUIRE(AdrenoGpuccWindowV1UsableBytes(&w) == 0x800cu);
    AGW_REQUIRE(AdrenoGpuccWindowV1UsableBytes(&w) != 0x9000u);
    AGW_REQUIRE(AdrenoGpuccWindowV1UsableBytes((const struct AdrenoGpuccWindowV1 *)0) == 0u);

    /* --- resolve accepts the offsets the round-62 executor really uses --- */
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x106cu, &p) == 0);   /* CX GDSCR   */
    if (p != 0) {
        AGW_REQUIRE((uintptr_t)p == (uintptr_t)fake + 0x106cu);
    } else {
        AGW_REQUIRE(0);
    }
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x1540u, &p) == 0);   /* CX status  */
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x1010u, &p) == 0);   /* GX status  */
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x1098u, &p) == 0);   /* gmu CBCR   */
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x109cu, &p) == 0);   /* cxo CBCR   */
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x1120u, &p) == 0);   /* RCG CMD    */
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x1124u, &p) == 0);   /* RCG CFG    */
    /* Highest offset the round-62 executor touches is 0x1124; the window must
     * reach at least that far or the executor cannot have run at all. */
    AGW_REQUIRE(0x1124u + ADRENO_GPUCC_WINDOW_V1_STRIDE
                <= AdrenoGpuccWindowV1UsableBytes(&w));

    /* --- bound 1: alignment --------------------------------------------- */
    w.refused = 0u;
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x106du, &p) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x106eu, &p) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 1u, &p) != 0);
    AGW_REQUIRE(w.refused == 3u);

    /* --- bound 3: the regmap limit, which the DTB size does NOT imply ---- */
    w.refused = 0u;
    /* 0x8008 is the last addressable offset: it MUST succeed. */
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x8008u, &p) == 0);
    /* 0x800c is inside the mapped 0x9000 window but past the regmap limit:
     * this is exactly the access the third bound exists to refuse. */
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x800cu, &p) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x8ffcu, &p) != 0);
    AGW_REQUIRE(w.refused == 2u);

    /* --- bound 2: the mapped window -------------------------------------- */
    w.refused = 0u;
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0x9000u, &p) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&w, 0xffffffecu, &p) != 0);
    AGW_REQUIRE(w.refused == 2u);

    /* A window SMALLER than max_register must bound on its own size. */
    {
        struct AdrenoGpuccWindowV1 small;
        AGW_REQUIRE(AdrenoGpuccWindowV1Bind(&small,
                                            (volatile uint8_t *)fake, 0x100u) == 0);
        AGW_REQUIRE(AdrenoGpuccWindowV1UsableBytes(&small) == 0x100u);
        AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&small, 0xfcu, &p) == 0);
        small.refused = 0u;
        AGW_REQUIRE(AdrenoGpuccWindowV1Resolve(&small, 0x100u, &p) != 0);
        AGW_REQUIRE(small.refused == 1u);
    }

    /* --- the MMIO triple: refuses without a delay, works with one ------- */
    AGW_REQUIRE(AdrenoGpuccWindowV1Mmio((struct AdrenoGpuccWindowV1 *)0, &mmio) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Mmio(&w, (struct AdrenoPowerUpBackendV1Mmio *)0) != 0);
    AGW_REQUIRE(AdrenoGpuccWindowV1Mmio(&w, &mmio) != 0);   /* no delay yet    */
    AGW_REQUIRE(AdrenoGpuccWindowV1SetDelay(&w, (void *)0,
                                            (int (*)(void *, uint32_t))0) != 0);

    /* --- a window that was never bound cannot produce a triple ---------- */
    {
        struct AdrenoGpuccWindowV1 unbound;
        unbound.base = (volatile uint8_t *)0;
        unbound.bytes = 0u;
        unbound.max_register = 0u;
        unbound.reads = 0u;
        unbound.writes = 0u;
        unbound.refused = 0u;
        unbound.delay_cookie = (void *)0;
        unbound.udelay = 0;
        AGW_REQUIRE(AdrenoGpuccWindowV1Mmio(&unbound, &mmio) != 0);
    }

    if (check_count != (uint32_t *)0) {
        *check_count = checks;
    }
    return 0;
}
