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
// PROVENANCE (9 sha256 pins re-checked by scripts/verify-enchilada-cp-ring.py)
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
//  (P8) include/linux/sizes.h  1723 B
//       67fe2ba4a05c73e3ce41b8249a5a63e609639ba9876ea0a2a828fd2755a3efa7
//       :26 SZ_32K 0x00008000 (and :23 SZ_4K, :25 SZ_16K).
//  (P9) drivers/gpu/drm/msm/msm_gpu.h  20026 B
//       6c461e0bd0fd37e10dc5c25990c31e342273e936862826d5ae42cbf707d999e0
//       :311 MSM_GPU_RINGBUFFER_SZ SZ_32K; :312 BLKSIZE 32; :314-316 the
//       MSM_GPU_RB_CNTL_DEFAULT formula over BUFSZ/BLKSZ.
//  (P10) drivers/gpu/drm/msm/adreno/adreno_gpu.h  18946 B
//       7bdabea1ae7310697e0e2a4e0353cff2a7cf0ac2af746e5418e249d4584c86df
//       :637-643 PM4_PARITY (0x9669 LUT); :659-665 OUT_PKT7 (type-7 header);
//       :673-676 get_wptr() (the ring is MSM_GPU_RINGBUFFER_SZ >> 2 dwords).
//  (P11) registers/adreno/adreno_pm4.xml  85907 B
//       ab27047cc9cde4bc5996d6eb528fbb2dbdfdd9dc4515f75bbbada43a23809536
//       :229-234 the CP_TYPE0..7 tags; :246-248 adreno_pm4_type3_packets with
//       CP_ME_INIT = 0x48.
//  (P12) registers/adreno/adreno_common.xml  15485 B
//       c733db8294d0bedc6191044b7a6acfc81495ba1f294c3108746999c596cf5e58
//       :116-123 CP_RB_CNTL's FIELDS: BUFSZ 0..5, BLKSZ 8..13, BUF_SWAP 16..17,
//       POLL_EN 20, NO_UPDATE 27, RPTR_WR_EN 31. This is the file round 71 had
//       not pinned, and why U3/U4 looked unreachable: a6xx.xml:2282 declares
//       CP_RB_CNTL as a BARE reg32 with no fields at all.
//  (P13) registers/gen_header.py  27564 B
//       465b340f07d2fd299c83dc34c21b2b7cd71ef6a027aa9f93814683d3ea946ca6
//       the XML-driven generator that emits a6xx.xml.h; recorded so the
//       accessors' origin is a named build step, not a mystery.
//  (P3) .cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_gpu.h
//       3251 B 8909f9d70b6122899c70ec01fb034a6244f652eef9a0451065d22e86b546033e
//       :59-69 the A6XX_PROTECT_NORDWR / A6XX_PROTECT_RDONLY encoding that the
//       a630_protect table uses (bit 31 = no-read-write, MASK_LEN 18..30,
//       BASE_ADDR 0..17) -- recorded so a caller cannot "improve" the table.
//
// ROUND 71 UPDATE -- THREE OF THE FIVE ARE NOW DEFINED (this corrects round 70)
// ---------------------------------------------------------------------------
// Round 70 recorded five constants as undefined and said the next step was a
// RESEARCH step: pin the headers that might define them. This round did exactly
// that, and the result is a partial defeat of round 70's own claim, recorded
// honestly rather than quietly:
//   P8 include/linux/sizes.h        1723 B 67fe2ba4...a3efa7  :26 SZ_32K = 0x8000
//   P9 drivers/gpu/drm/msm/msm_gpu.h 20026 B 6c461e0b...d999e0
//      :311 #define MSM_GPU_RINGBUFFER_SZ SZ_32K
//      :312 #define MSM_GPU_RINGBUFFER_BLKSIZE 32
//      :314-316 #define MSM_GPU_RB_CNTL_DEFAULT (BUFSZ(ilog2(SZ/8)) | BLKSZ(...))
//   P10 adreno/adreno_gpu.h         18946 B 7bdabea1...84c86df
//      :637-643 PM4_PARITY(); :659-665 OUT_PKT7() (the type-7 header encoding)
//   P11 registers/adreno/adreno_pm4.xml 85907 B ab27047c...23809536
//      :234 CP_TYPE7_PKT = 0x70000000; :248 CP_ME_INIT = 0x48
// So U1 (opcode), U2 (header encoding), U3 (RB_CNTL_DEFAULT is a FORMULA over
// BUFSZ/BLKSZ) and U5 (ring size 0x8000) are DEFINED. What remains genuinely
// open is narrower and now precisely named:
//   U4 AXXX_CP_RB_CNTL_NO_UPDATE -- the OR term at a6xx_gpu.c:1131. It is a
//      BITFIELD accessor from the GENERATED a6xx.xml.h, which is not in the tree
//      (no *.xml.h exists), and CP_RB_CNTL carries NO bitfields in the pinned
//      a6xx.xml (:2282 is a bare reg32). Likewise AXXX_CP_RB_CNTL_BUFSZ/BLKSZ.
//   U3 is therefore downgraded, NOT removed: the formula is known, its two
//      field accessors are not, so the VALUE still cannot be computed.
// The module now emits the REAL type-7 header for CP_ME_INIT (0x70C80008,
// parity(8)=0, parity(0x48)=1) and still reports is_executable()==0, because a
// ring whose RB_CNTL enable word cannot be computed cannot be programmed.
//
// ROUND 70 TEXT (kept for the record, now partly superseded)
// ---------------------------------------------------------
// The round-70 claim was: "the pinned set contains the *calls* OUT_PKT7 /
// OUT_RING and the macros MSM_GPU_RB_CNTL_DEFAULT / AXXX_CP_RB_CNTL_NO_UPDATE,
// but not their definitions: those live in msm_gpu.h / adreno_gpu.h /
// msm_ringbuffer.h, which are NOT pinned." That premise was true and the
// conclusion drawn from it was too strong -- the definitions were reachable and
// had simply not been fetched. The five items round 70 listed were:
//   U1 CP_ME_INIT's numeric opcode      -> NOW 0x48 (P11:248)
//   U2 the type-7 packet header encoding -> NOW P10:659-665
//   U3 MSM_GPU_RB_CNTL_DEFAULT's value   -> formula NOW P9:314-316, fields open
//   U4 AXXX_CP_RB_CNTL_NO_UPDATE's bit   -> STILL open (generated header)
//   U5 the ring size                     -> NOW SZ_32K = 0x8000 (P8/P9:311)
//
// HONEST BOUNDARY
// ---------------
// HOST-VERIFIED, ORDER + PAYLOAD + HEADER + PARTIAL REFUSAL. No BO, no IOVA, no
// mapping, no doorbell write, no fence, no interrupt, no submit. A real packet
// can now be ENCODED on the host; it still cannot be ISSUED, because the ring's
// control word cannot be computed. CONFIG_XNU_NATIVE_ADRENO remains n; the gpu
// area STAYS "host-verified"; no device run, no flash, m39510 NOT exercised. It
// does NOT unblock stage 5.
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

/* ---- Ring geometry, DERIVED in round 71 (P8:26, P9:311-312) ------------- */
#define kAdrenoCpRingBufferBytes   0x00008000u /* P9:311 SZ_32K (P8:26) */
#define kAdrenoCpRingBufferDwords  0x00002000u /* 0x8000 / 4, see P10:673-676 */
#define kAdrenoCpRingBlockBytes    32u         /* P9:312 */
/* The two ilog2() field values the RB_CNTL formula feeds (P9:315-316). */
#define kAdrenoCpRingBufszField    12u /* ilog2(0x8000 / 8) = ilog2(4096) */
#define kAdrenoCpRingBlkszField    2u  /* ilog2(32 / 8)     = ilog2(4)    */

/* ---- Type-7 packet header, DERIVED in round 71 (P11:234, P10:637-665) --- */
#define kAdrenoCpRingType7Pkt      0x70000000u /* P11:234 CP_TYPE7_PKT */
#define kAdrenoCpRingMeInitOpcode  0x48u       /* P11:248 CP_ME_INIT */
/* The exact header OUT_PKT7(ring, CP_ME_INIT, 8) emits. Parity of cnt=8 is 0
 * and parity of 0x48 is 1, so the two parity bits land at 0 and 23. */
#define kAdrenoCpRingMeInitHeader  0x70c80008u
/* CP_ME_INIT's opcode exceeds the 7-bit opcode field only if masked wrong; the
 * encoder must apply the & 0x7F that P10:664 applies. */
#define kAdrenoCpRingOpcodeMask    0x7fu
#define kAdrenoCpRingCntMask       0xffffu
#define kAdrenoCpRingParityCntBit  15u /* P10:663 */
#define kAdrenoCpRingParityOpBit   23u /* P10:664 */
#define kAdrenoCpRingParityLut     0x9669u /* P10:639 */

/* ---- CP_RB_CNTL, DERIVED in round 72 (adreno_common.xml:116-123) ------- */
/* The bitfield layout is in adreno_common.xml, NOT a6xx.xml (whose CP_RB_CNTL
   at :2282 is a bare reg32 with no fields -- that is why round 71 could not
   close U3/U4):
        :117 BUFSZ     low 0  high 5
        :118 BLKSZ     low 8  high 13
        :119 BUF_SWAP  low 16 high 17
        :120 POLL_EN   pos 20
        :121 NO_UPDATE pos 27
        :122 RPTR_WR_EN pos 31
   Fed by msm_gpu.h:314-316 with ilog2(0x8000/8)=12 and ilog2(32/8)=2. */
#define kAdrenoCpRingRbCntlBufszLow   0u
#define kAdrenoCpRingRbCntlBufszHigh  5u
#define kAdrenoCpRingRbCntlBlkszLow   8u
#define kAdrenoCpRingRbCntlBlkszHigh  13u
#define kAdrenoCpRingRbCntlNoUpdatePos 27u
/* The two values the A630 init writes (a6xx_gpu.c:1127-1131). */
#define kAdrenoCpRingRbCntlDefault    0x0000020cu
#define kAdrenoCpRingRbCntlNoUpdate   0x0800020cu  /* DEFAULT | BIT(27) */

/* ---- The constants that are STILL not derivable after round 71 --------- */
/* Only U4 is fully open, plus U3's two field ACCESSORS. The mask keeps five
 * bits so the shape of the original question survives, but three of them are
 * now marked DERIVED and the API states which. */
enum adreno_cp_ring_undefined {
    kAdrenoCpRingUndefMeInitOpcode = 1u,   /* U1 -- DERIVED round 71 (0x48) */
    kAdrenoCpRingUndefPacketHeader = 2u,   /* U2 -- DERIVED round 71 */
    kAdrenoCpRingUndefRbCntlDefault = 4u,  /* U3 -- formula derived, fields open */
    kAdrenoCpRingUndefRbCntlNoUpdate = 8u, /* U4 -- STILL OPEN */
    kAdrenoCpRingUndefRingSize = 16u,      /* U5 -- DERIVED round 71 (0x8000) */
    kAdrenoCpRingUndefAll = 31u,
    /* Round 72 closed U3 and U4 too, so NOTHING is undefined any more. The
     * historical five-bit mask is kept because the question it encoded is the
     * round-70 record; the OPEN set is now empty and that is the honest answer. */
    kAdrenoCpRingUndefOpenNow = 0u
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

/* ---- Round-71 encoders (DERIVED, host-verifiable, never issued) -------- */

/* PM4_PARITY (P10:637-643): the parity bit PM4 puts in a type-7 header. */
uint32_t adreno_cp_ring_parity(uint32_t value);
/* The type-7 header OUT_PKT7 emits for (opcode, count) (P10:659-665). The
 * opcode is masked to 7 bits and the count to 16, exactly as upstream does. */
uint32_t adreno_cp_ring_type7_header(uint32_t opcode, uint32_t count);
/* The FULL 9-dword message: the real header 0x70C80008 followed by the 8 payload
 * dwords. This is the first byte sequence this port can produce that a CP could
 * in principle consume; it still cannot be issued (see is_executable()). */
int adreno_cp_ring_me_init_message(uint32_t *dwords, uint32_t capacity,
                                   uint32_t *count);
/* 1 when the 9-dword vector is exactly that message, 0 otherwise. */
int adreno_cp_ring_me_init_message_matches(const uint32_t *dwords,
                                           uint32_t count);
/* The ring size in bytes (0x8000) and in dwords (0x2000), P8:26 / P9:311. */
uint32_t adreno_cp_ring_buffer_bytes(void);
uint32_t adreno_cp_ring_buffer_dwords(void);
/* The two ilog2() field values the RB_CNTL formula consumes (P9:315-316). The
 * formula itself is known; the AXXX_CP_RB_CNTL_BUFSZ/BLKSZ FIELD ACCESSORS that
 * would turn these into bit positions are not, so this returns the inputs, not
 * the register value. Returns 0, or 1 on any NULL. */
int adreno_cp_ring_rb_cntl_fields(uint32_t *bufsz_field, uint32_t *blksz_field);

/* The bits that are STILL undefined (kAdrenoCpRingUndefOpenNow today), and the
 * full historical five-bit mask. */
uint32_t adreno_cp_ring_undefined_open(void);
uint32_t adreno_cp_ring_undefined(void);
/* Name of one bit, or "unknown". */
const char *adreno_cp_ring_undefined_name(uint32_t bit);
/* Pinned citation for one bit: for a DERIVED bit it states where the value now
 * comes from, for an open bit why it is still open. "unknown" out of range. */
const char *adreno_cp_ring_undefined_anchor(uint32_t bit);
/* 1 when the ring can be PROGRAMMED, i.e. every register word it must write is
 * computable. Since round 72 closed U3/U4 this is 1: the full CP_ME_INIT message
 * AND the CP_RB_CNTL word both exist. It is still NOT a promise that the ring
 * works: nothing here has been written to hardware. */
int adreno_cp_ring_is_executable(void);
/* 1 when a full CP_ME_INIT message can be encoded (since round 71). */
int adreno_cp_ring_can_encode_packet(void);
/* The two CP_RB_CNTL values, DERIVED in round 72. `hw_apriv` selects which of
 * the a6xx_gpu.c:1127-1131 branches applies. Returns 0, or 1 on a NULL. */
int adreno_cp_ring_rb_cntl_value(int hw_apriv, uint32_t *out);
/* Rebuild a CP_RB_CNTL word from (bufsz_field, blksz_field, no_update) using the
 * adreno_common.xml:116-123 layout. Lets a test prove the field placement. */
uint32_t adreno_cp_ring_pack_rb_cntl(uint32_t bufsz_field, uint32_t blksz_field,
                                     uint32_t no_update);
/* Read a field back out of a CP_RB_CNTL word. */
uint32_t adreno_cp_ring_unpack_field(uint32_t word, uint32_t low, uint32_t high);

/* Self-check. Returns 0 when every invariant holds, else 1; *check_count
 * receives the number evaluated. */
int adreno_cp_ring_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_CP_RING_V1_H */
