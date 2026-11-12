/* SPDX-License-Identifier: MIT */
/* Round 63: the BOUNDED MMIO ADAPTER between a mapped GPUCC window and the
 * round-62 AdrenoPowerUpBackendV1 executor.
 *
 * WHY THIS EXISTS
 * ---------------
 * Round 62 gave the power-up plan an executor whose four bus callbacks write
 * registers. But the executor takes a struct AdrenoPowerUpBackendV1Mmio --
 * a cookie and three function pointers -- and NOTHING in this tree produced
 * one. A caller had to hand-roll read32/write32 thunks, which means the
 * arithmetic "byte offset -> volatile uint32_t*" lived in kernel code with no
 * test and no bound. That is precisely the shape of bug that reads a register
 * one past a window, or reads an aligned-looking address that is not aligned.
 *
 * This module is that missing layer, and it is deliberately the smallest thing
 * that can be: it owns no mapping, needs no clock, performs no power action,
 * and never decides whether the domain is up. It translates a byte offset into
 * a bounds-checked pointer inside ONE window, counts what it did, and refuses
 * everything else.
 *
 * THE THREE BOUNDS, AND WHY ALL THREE ARE NEEDED
 * ----------------------------------------------
 *  1. off % 4 == 0. A byte offset that is not register-aligned is a derivation
 *     bug upstream. On this SoC family an unaligned device read is not merely
 *     wrong, it can fault or return silently rotated data.
 *  2. off + 4 <= bytes. The window as mapped. This is the DTB reg size,
 *     0x9000 on enchilada.
 *  3. off <= max_register. THE ONE THAT IS EASY TO FORGET: the DT node claims
 *     0x9000 bytes but gpucc-sdm845.c registers the regmap with
 *     max_register = 0x8008, so the last ~4 KiB of the DTB window is NOT
 *     addressable through the vendor clock driver. A window that trusts the
 *     DT size alone will happily hand out a pointer into a region the hardware
 *     description never promised. Bound 3 closes that gap.
 *
 * Because bound 3 is expressed as a value rather than a literal, the effective
 * usable window is min(bytes, max_register + 4) -- on this board 0x800c bytes,
 * not 0x9000. The round-62 executor's highest touched offset is 0x1124, so
 * every access it makes is comfortably inside; this adapter does not assume
 * that, it checks it.
 *
 * HONEST BOUNDARY
 * ---------------
 * HOST-VERIFIED. This is bounds arithmetic and pointer formation over a window
 * the CALLER mapped; it maps nothing itself and has never run on the device.
 * The round-22/28 prohibition is neither enforced nor weakened here: handing
 * this adapter a pointer into a collapsed gpu_cx still produces exactly the
 * hang it would have produced without the adapter. The gate that must run
 * first is AdrenoGmuCallerV1Precheck, and this module does not stand in for it.
 *
 * The stats are not decoration. `refused` must be zero in any run whose inputs
 * were derived correctly; a nonzero value means an upstream derivation
 * produced an offset this window cannot address, which is a bug to fix, not a
 * condition to tolerate.
 */
#ifndef ADRENO_GPUCC_WINDOW_V1_H
#define ADRENO_GPUCC_WINDOW_V1_H

#include <stdint.h>
#include <stddef.h>

#include "AdrenoPowerUpBackendV1.h"
#include "AdrenoClkCtlV1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The vendor clk driver's own regmap bound (gpucc-sdm845.c: max_register),
 * taken FROM the round-61 derivation rather than retyped, so the two cannot
 * drift apart. NOT the DTB reg size. See bound 3 above. */
#define ADRENO_GPUCC_WINDOW_V1_MAX_REGISTER ADRENO_CLK_CTL_V1_GPUCC_MAX_REG

/* The DTB window size, likewise not retyped. */
#define ADRENO_GPUCC_WINDOW_V1_DTB_BYTES ADRENO_CLK_CTL_V1_GPUCC_SIZE

/* Register granularity of the block. */
#define ADRENO_GPUCC_WINDOW_V1_STRIDE 4u

/* One mapped GPUCC window plus its traffic counters. The caller owns `base`
 * (it did the mapping) and must keep it valid for as long as the adapter is
 * used; this struct holds no mapping of its own. */
struct AdrenoGpuccWindowV1 {
    volatile uint8_t *base;      /* mapped window, from the caller          */
    uint32_t bytes;              /* bytes actually mapped                   */
    uint32_t max_register;       /* highest addressable byte offset         */
    uint32_t reads;
    uint32_t writes;
    uint32_t refused;            /* must stay 0 in a correct run            */

    /* The GDSC plan genuinely delays between a collapse-clear write and the
     * status poll that waits for it, so a window that cannot delay cannot
     * produce a usable MMIO triple. Freestanding C has no delay primitive of
     * its own and this module will NOT invent one (a spin loop calibrated by
     * guesswork is worse than no loop): the caller supplies it. Until it does,
     * AdrenoGpuccWindowV1Mmio() refuses rather than handing back a triple with
     * a NULL delay that the executor would call anyway. */
    void *delay_cookie;
    int (*udelay)(void *cookie, uint32_t usec);
};

/* Bind a mapped window. Returns 0 on success, non-zero when w or base is NULL,
 * when bytes is smaller than one register, when bytes is not a multiple of the
 * register stride, or when max_register is not stride-aligned. `base` must be
 * at least 4-byte aligned; an unaligned window is refused rather than rounded.
 *
 * This function does NOT validate that the window is safe to touch -- that is
 * the domain-state gate's job, and conflating the two would let a bounds check
 * masquerade as a power check. */
int AdrenoGpuccWindowV1Bind(struct AdrenoGpuccWindowV1 *w,
                            volatile uint8_t *base, uint32_t bytes);

/* Supply the delay the executor's GDSC leg needs. Returns 0 on success,
 * non-zero when w or fn is NULL. Kept separate from Bind() so that a caller
 * which only wants bounds-checked reads does not have to invent a delay. */
int AdrenoGpuccWindowV1SetDelay(struct AdrenoGpuccWindowV1 *w,
                               void *cookie,
                               int (*fn)(void *cookie, uint32_t usec));

/* The number of bytes this window will actually serve, i.e.
 * min(bytes, max_register + 4). Exposed so a caller (and its test) can assert
 * the derived limit rather than re-deriving it in two places. */
uint32_t AdrenoGpuccWindowV1UsableBytes(const struct AdrenoGpuccWindowV1 *w);

/* Resolve a byte offset to a register pointer. Returns 0 and writes *out on
 * success; returns non-zero (and counts a refusal) for an unaligned offset or
 * an offset outside all three bounds. *out is never written on failure. */
int AdrenoGpuccWindowV1Resolve(struct AdrenoGpuccWindowV1 *w, uint32_t byte_offset,
                               volatile uint32_t **out);

/* Produce the MMIO triple the round-62 executor consumes. The returned struct
 * points back at `w`, so `w` must outlive any backend built from it. Returns 0
 * on success, non-zero when w or out is NULL or w is unbound. */
int AdrenoGpuccWindowV1Mmio(struct AdrenoGpuccWindowV1 *w,
                            struct AdrenoPowerUpBackendV1Mmio *out);

/* Self-check. Returns 0 when every invariant holds, else the 1-based index of
 * the first failure. check_count receives the number of checks evaluated.
 *
 * Proves the arithmetic only: that this adapter takes BYTE offsets (as the
 * round-61 clock plans do, 0x1120 meaning GPUCC+0x1120 -- deliberately unlike
 * AdrenoGmuBusV1, whose callers pass WORD offsets), that all three bounds
 * reject, and that the usable size is the derived 0x800c rather than the DTB's
 * 0x9000. It does NOT probe a register. */
int AdrenoGpuccWindowV1Verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_GPUCC_WINDOW_V1_H */
