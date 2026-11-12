// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoCpRingV1 -- enchilada stage 5 (GPU) HOST-VERIFIED A630 CP RING
// INITIALISATION and DOORBELL contract (round 70, 2026-11-12).
//
// This is NOT a GPU driver. It is a freestanding, pure-C encoding of the exact
// ordered CP-ring bring-up a future A6XX_GEN1 kernel backend must perform, the
// exact CP_ME_INIT payload, and the two submission semantics that surround it
// (the WPTR doorbell and the shadow-RPTR strategy). It maps nothing, writes no
// register, allocates no ring, and is NOT an IOService.
//
// WHY THIS ROUND (round-69 admission table, prerequisite `cp-ring`)
// ----------------------------------------------------------------
// Round 69's table recorded `cp-ring` as modeled=0 with the reason "only the
// eight word offsets are known". That was true and it was the cheapest gap to
// close: `a6xx_gpu.c` pins the full init ORDER, the full CP_ME_INIT payload and
// both doorbell strategies. This round encodes them and, with equal care,
// records the FIVE constants that are NOT derivable from any pinned source —
// because those five are exactly what stands between a model and an executable
// ring, and hiding them would be the dishonest way to move the table.
//
// modeled goes 5 -> 6. satisfied STAYS 0, and the verdict STAYS BLOCKED: a
// model whose packet header encoding is unknown cannot issue a single packet.
//
// PROVENANCE (3 sha256 pins re-checked by scripts/verify-enchilada-cp-ring.py)
// ---------------------------------------------------------------------------
//  (P1) .cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_gpu.c
//       69087 B c33479377574ed193e82c272b3708e19a2b2153088f246e2639d8a43fef943a2
//       :37-54  a6xx_idle(): adreno_idle(gpu, ring) then spin_until(_a6xx_check_idle);
//               the timeout diagnostic reads RBBM_STATUS, RBBM_INT_0_STATUS,
//               CP_RB_RPTR and CP_RB_WPTR -- i.e. the rptr/wptr pair is the
//               progress evidence the whole submission path rests on.
//       :56-67  update_shadow_rptr(): if (has_whereami && !hw_apriv) emit
//               CP_WHERE_AM_I(2) carrying shadowptr() low/high.
//       :69-90  a6xx_flush(): ring->cur = ring->next; wptr = get_wptr(ring);
//               unlock; mb(); gpu_write(CP_RB_WPTR, wptr). The barrier BEFORE
//               the doorbell is the ordering rule, not a stylistic choice.
//       :612-639 a6xx_cp_init(): OUT_PKT7(ring, CP_ME_INIT, 8) then eight
//               OUT_RING dwords 0x0000002f, 0x00000003 (multiple hardware
//               contexts), 0x20000000 (error detection), 0, 0, 0, 0, 0
//               (header dump off; no workarounds; pad), then a6xx_flush() and
//               a6xx_idle().
//       :641-688 a7xx_cp_init(): CP_THREAD_CONTROL(BIT(27)) first and
//               CP_ME_INIT with count 7 -- the A7xx shape A630 must NOT copy.
//       :1118-1153 the ring setup ORDER: CP_SQE_INSTR_BASE := sqe_iova;
//               CP_RB_BASE := rb[0]->iova; CP_RB_CNTL := RB_CNTL_DEFAULT
//               (hw_apriv) or RB_CNTL_DEFAULT | AXXX_CP_RB_CNTL_NO_UPDATE;
//               CP_RB_RPTR_ADDR := shadowptr() when shadow_bo exists;
//               CP_SQE_CNTL := 1 ("Enable the SQE to start the CP engine");
//               then the a7xx/a6xx cp_init dispatch at :1153.
//  (P2) .cache/adreno-a630-source/drivers/gpu/drm/msm/registers/adreno/a6xx.xml
//       307513 B 7f432c5e189f14ce559e0e7722c077ac778cfb63d64b2043c35d09b379bcd602
//       :2281 CP_RB_BASE 0x0800 (reg64); :2282 CP_RB_CNTL 0x0802;
//       :2283 CP_RB_RPTR_ADDR 0x0804 (reg64); :2284 CP_RB_RPTR 0x0806;
//       :2285 CP_RB_WPTR 0x0807; :2286 CP_SQE_CNTL 0x0808;
//       :2294 CP_SQE_INSTR_BASE 0x0830 (reg64).
//  (P3) .cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_gpu.h
//       3251 B 8909f9d70b6122899c70ec01fb034a6244f652eef9a0451065d22e86b546033e
//       :59-69 the A6XX_PROTECT_NORDWR / A6XX_PROTECT_RDONLY encoding that the
//       a630_protect table uses (bit 31 = no-read-write, MASK_LEN 18..30,
//       BASE_ADDR 0..17) -- recorded so a caller cannot "improve" the table.
//
// THE FIVE CONSTANTS THAT ARE **NOT** DERIVABLE (and are refused, not guessed)
// ---------------------------------------------------------------------------
// The pinned set contains the *calls* OUT_PKT7 / OUT_RING and the macros
// MSM_GPU_RB_CNTL_DEFAULT / AXXX_CP_RB_CNTL_NO_UPDATE, but not their
// definitions: those live in msm_gpu.h / adreno_gpu.h / msm_ringbuffer.h, which
// are NOT pinned. A repo-wide scan of every pinned .c/.h/.xml finds the names
// and never the bodies. Therefore:
//   U1 CP_ME_INIT's numeric opcode      -- the XML register databases have
//                                          ZERO occurrences of CP_ME_INIT.
//   U2 the type-7 packet header encoding -- OUT_PKT7's body is unpinned.
//   U3 MSM_GPU_RB_CNTL_DEFAULT's value   -- name only.
//   U4 AXXX_CP_RB_CNTL_NO_UPDATE's bit   -- name only.
//   U5 the ring size (MSM_GPU_RINGBUFFER_SZ) -- absent from every pinned file.
// A ring that cannot encode a header cannot submit. So this module reports the
// undefined set explicitly rather than presenting the order as executable.
//
// HONEST BOUNDARY
// ---------------
// HOST-VERIFIED, ORDER + PAYLOAD + REFUSAL ONLY. No BO, no IOVA, no mapping, no
// doorbell write, no fence, no interrupt, no submit. CONFIG_XNU_NATIVE_ADRENO
// remains n; the gpu area STAYS "host-verified"; no device run, no flash,
// m39510 NOT exercised. It does NOT unblock stage 5.
// ---------------------------------------------------------------------------

#ifndef ADRENO_CP_RING_V1_H
#define ADRENO_CP_RING_V1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- CP register word offsets (P2:2281-2286, :2294) -------------------- */
#define kAdrenoCpRingRbBase        0x0800u /* P2:2281 reg64 */
#define kAdrenoCpRingRbCntl        0x0802u /* P2:2282 */
#define kAdrenoCpRingRbRptrAddr    0x0804u /* P2:2283 reg64 */
#define kAdrenoCpRingRbRptr        0x0806u /* P2:2284 */
#define kAdrenoCpRingRbWptr        0x0807u /* P2:2285 */
#define kAdrenoCpRingSqeCntl       0x0808u /* P2:2286 */
#define kAdrenoCpRingSqeInstrBase  0x0830u /* P2:2294 reg64 */

/* ---- CP_ME_INIT payload (P1:616-635) ---------------------------------- */
#define kAdrenoCpRingMeInitDwords  8u          /* P1:616 count */
#define kAdrenoCpRingMeInitWord0   0x0000002fu /* P1:618 */
#define kAdrenoCpRingMeInitWord1   0x00000003u /* P1:621 multiple contexts */
#define kAdrenoCpRingMeInitWord2   0x20000000u /* P1:624 error detection */
/* words 3..7 are 0x00000000 (P1:627,628,631,634,635). */
#define kAdrenoCpRingMeInitZeroFrom 3u
/* The A7xx shape must NOT be used for A630 (P1:650-684). */
#define kAdrenoCpRingA7xxMeInitDwords 7u

/* ---- SQE enable value (P1:1151) --------------------------------------- */
#define kAdrenoCpRingSqeCntlEnable 1u
/* ---- protect encoding (P3:59-69) -------------------------------------- */
#define kAdrenoCpRingProtectNoRdwrBit 31u
#define kAdrenoCpRingProtectLenShift 18u
#define kAdrenoCpRingProtectLenMask 0x3fffu
#define kAdrenoCpRingProtectAddrMask 0x3ffffu

/* ---- The five constants the pinned sources do NOT define -------------- */
enum adreno_cp_ring_undefined {
    kAdrenoCpRingUndefMeInitOpcode = 1u,   /* U1 */
    kAdrenoCpRingUndefPacketHeader = 2u,   /* U2 */
    kAdrenoCpRingUndefRbCntlDefault = 4u,  /* U3 */
    kAdrenoCpRingUndefRbCntlNoUpdate = 8u, /* U4 */
    kAdrenoCpRingUndefRingSize = 16u,      /* U5 */
    kAdrenoCpRingUndefAll = 31u
};

/* ---- The ordered ring bring-up (P1:1118-1153) ------------------------- */
enum adreno_cp_ring_step {
    kAdrenoCpRingStepSqeInstrBase = 0u, /* P1:1118 */
    kAdrenoCpRingStepRbBase = 1u,       /* P1:1121 */
    kAdrenoCpRingStepRbCntl = 2u,       /* P1:1127-1131 */
    kAdrenoCpRingStepRptrAddr = 3u,     /* P1:1134-1136, only with a shadow BO */
    kAdrenoCpRingStepSqeEnable = 4u,    /* P1:1151 */
    kAdrenoCpRingStepMeInit = 5u,       /* P1:1153 -> :612-639 */
    kAdrenoCpRingStepCount = 6u
};

/* Which register a step touches, and whether the value is a 64-bit IOVA or a
 * constant. `conditional` marks the shadow-RPTR step: it is skipped when no
 * shadow BO exists (P1:1134). */
struct adreno_cp_ring_step_desc {
    uint32_t step;         /* enum adreno_cp_ring_step                       */
    uint32_t reg_offset;   /* P2 word offset; 0 for the packet-only ME_INIT  */
    uint32_t is_iova;      /* 1 when the value is a 64-bit address           */
    uint32_t conditional;  /* 1 when the step may be skipped                */
};

/* ---- Shadow-RPTR strategy (P1:61-66, :1123-1131) ---------------------- */
typedef enum {
    kAdrenoCpRingShadowHwApriv = 0, /* RB_CNTL keeps the hw RPTR shadow     */
    kAdrenoCpRingShadowWhereAmI = 1 /* NO_UPDATE + CP_WHERE_AM_I opcode     */
} AdrenoCpRingShadowMode;

/* ---- API (freestanding; no libc, no MMIO, no IOKit) ------------------- */

/* The seven CP word offsets. Returns 0, or 1 on any NULL. */
int adreno_cp_ring_offsets(uint32_t *rb_base, uint32_t *rb_cntl,
                           uint32_t *rptr_addr, uint32_t *rptr,
                           uint32_t *wptr, uint32_t *sqe_cntl,
                           uint32_t *sqe_instr_base);

/* The ordered bring-up plan. `has_shadow_bo` decides whether step 3 is emitted.
 * Returns 0, or 1 on NULL/short capacity; *count is 6 (with shadow) or 5
 * (without) on success and 0 otherwise. */
int adreno_cp_ring_plan(uint32_t has_shadow_bo,
                        struct adreno_cp_ring_step_desc *ops, uint32_t capacity,
                        uint32_t *count);

/* The exact CP_ME_INIT payload (P1:616-635). Returns 0, or 1 on NULL/short
 * capacity; *count is 8. A630 uses the 8-dword form, never the A7xx 7-dword
 * form. */
int adreno_cp_ring_me_init(uint32_t *dwords, uint32_t capacity, uint32_t *count);

/* 1 when the dword vector is exactly the A630 CP_ME_INIT payload, 0 otherwise
 * (including every A7xx-shaped vector). */
int adreno_cp_ring_me_init_matches(const uint32_t *dwords, uint32_t count);

/* P1:1131: on this board (no extended APRIV) RB_CNTL carries NO_UPDATE and the
 * RPTR shadow is maintained by the CP_WHERE_AM_I opcode instead. */
AdrenoCpRingShadowMode adreno_cp_ring_shadow_mode(int hw_apriv);
/* P1:82-89: the doorbell is a single WPTR store that MUST follow a full barrier.
 * Returns 0 always; the parameters exist so a caller cannot omit them. */
int adreno_cp_ring_doorbell_order(uint32_t *barrier_before_write,
                                  uint32_t *write_wptr_after_barrier);

/* The five undefined constants (bitmask of enum adreno_cp_ring_undefined). */
uint32_t adreno_cp_ring_undefined(void);
/* Name of one undefined constant bit, or "unknown". */
const char *adreno_cp_ring_undefined_name(uint32_t bit);
/* Pinned citation for one undefined constant bit, or "unknown". */
const char *adreno_cp_ring_undefined_anchor(uint32_t bit);
/* 1 when the undefined set prevents a packet from being encoded at all. Always
 * 1 today: U1 and U2 alone are enough. */
int adreno_cp_ring_is_executable(void);

/* Self-check. Returns 0 when every invariant holds, else 1; *check_count
 * receives the number evaluated. */
int adreno_cp_ring_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_CP_RING_V1_H */
