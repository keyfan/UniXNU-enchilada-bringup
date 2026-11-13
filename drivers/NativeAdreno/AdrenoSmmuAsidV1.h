// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuAsidV1 -- enchilada stage 5 (GPU) HOST-VERIFIED ASID ALLOCATOR for the
// kgsl SMMU. This is the SECOND of the four absent inputs on the
// `kgsl-smmu-stage1` row; round 101 obtained the first (`ias = 48`).
//
// PROVENANCE (2 sha256 pins, re-checked by scripts/verify-enchilada-smmu-asid.py)
// -----------------------------------------------------------------------------
//  (P1) drivers/iommu/arm/arm-smmu/arm-smmu.h  16630 B
//       ed85998794695d1a824dc1b86f680c8d124546688fd2665e00e3cbcfa9546b53
//       :63  ID1_NUMCB GENMASK(7,0); :120 S2CR_CBNDX GENMASK(7,0);
//       :123 GR1_CBAR(n) = 0x0 + (n << 2); :169 TTBRn_ASID GENMASK_ULL(63,48);
//       :191 CB_CONTEXTIDR 0x34; :253 ARM_SMMU_MAX_CBS 128;
//       :464-475 __arm_smmu_alloc_bitmap() -- the first-fit loop this module
//       mirrors, including its -ENOSPC return and its test_and_set_bit shape.
//  (P2) drivers/iommu/arm/arm-smmu/arm-smmu.c  67693 B
//       d5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a
//       :655-663 arm_smmu_alloc_context_bank() delegates to the bitmap;
//       :734 start = smmu->num_s2_context_banks for a stage-1 domain;
//       :793 cfg->vmid = cfg->cbndx + 1 for stage-2, which is NOT the same
//       allocator; :795 cfg->asid = cfg->cbndx -- the line that makes an "ASID
//       allocator" a CONTEXT BANK allocator; :1865-1866 ID1.NUMS2CB/NUMCB read at
//       probe, 8-bit fields.
//
// WHY AN ALLOCATOR AND WHY IT IS SMALLER THAN IT SOUNDS
// -----------------------------------------------------
// Round 75 recorded "no ASID allocator exists" as blocker I2 and left it at that.
// Reading the pinned driver says something more specific, and the specificity is the
// whole design here:
//
//   arm-smmu.c:795   cfg->asid = cfg->cbndx;
//
// The ASID is NOT drawn from a free-running 8- or 16-bit space. It IS the context
// bank index. So the allocator's real job is the CONTEXT BANK BITMAP, not an ASID
// counter -- and `arm_smmu_alloc_context_bank()` (:655-663) delegates straight to
// `__arm_smmu_alloc_bitmap(smmu->context_map, start, smmu->num_context_banks)`.
//
// `__arm_smmu_alloc_bitmap()` (arm-smmu.h, inline) is a first-fit search:
//
//   do { idx = find_next_zero_bit(map, end, start);
//        if (idx == end) return -ENOSPC; } while (test_and_set_bit(idx, map));
//
// Two consequences that a naive allocator gets wrong, and that this module encodes:
//
//  1. **First-fit, not next-free.** The search RESTARTS at `start` every call, so a
//     bank freed at index 3 is reused by the very next allocation even when a later
//     index is also free. An allocator that hands out indices in a rotating fashion
//     is not upstream-equivalent, and the difference shows up as a different CBAR /
//     TTBRn_ASID value than the driver would have programmed for the same sequence.
//  2. **`start` is not always 0.** For a stage-1 domain upstream passes
//     `start = smmu->num_s2_context_banks` (:734), because the first N banks are
//     reserved for stage-2. A stage-1 allocator that begins at 0 can hand out a bank
//     the hardware reserves.
//
// THE SECOND LIMIT, WHICH IS NOT THE SAME AS THE FIRST
// ----------------------------------------------------
// `num_context_banks` is a RUNTIME value read from ID1.NUMCB (arm-smmu.h:64,
// GENMASK(7,0)) at probe (:1866). It is 8 bits wide, so at most 256 banks, and
// upstream additionally clamps its own bitmap to ARM_SMMU_MAX_CBS = 128. Both facts
// are honoured: the capacity is an INPUT (nobody in this port has read ID1 yet), and
// the hard ceiling is the smaller of the two, enforced rather than assumed.
//
// AND THE ASID FIELD IS WIDER THAN THE BANK COUNT
// -----------------------------------------------
// TTBRn_ASID is GENMASK_ULL(63,48) -- 16 bits (arm-smmu.h:169) -- while a bank index
// is at most 128. Since asid == cbndx, the 16-bit field is never the binding
// constraint on this path; the bank count is. Round 101 measured ID2.VMID16 = 0, so
// the VMID side is 8-bit here, but VMIDs are a stage-2 concern and are deliberately
// NOT allocated by this module.
//
// WHAT THIS MODULE IS NOT
// -----------------------
// It is not a page table, not a context programmer, and it does not touch the SMMU.
// It tracks ownership of context-bank indices in a 128-bit bitmap and computes the
// (cbndx, asid, CONTEXTIDR, TTBRn_ASID) tuple a future writer needs. It hands out
// nothing until the caller supplies the capacity, because the capacity is a register
// read this port has not performed.
//
// HONEST BOUNDARY
// ---------------
// HOST-VERIFIED. Freestanding C11, no libc, no MMIO, no IOKit, no allocation of
// memory (the bitmap is caller-provided storage). `ready()` is derived from a
// missing-inputs mask and is 0 today because ID1.NUMCB has never been read on this
// phone. CONFIG_XNU_NATIVE_ADRENO remains n; the gpu area STAYS "host-verified"; it
// does NOT unblock stage 5.
// ---------------------------------------------------------------------------

#ifndef ADRENO_SMMU_ASID_V1_H
#define ADRENO_SMMU_ASID_V1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The two ceilings, from two different places --------------------- */
/* P1:64 GENMASK(7,0): ID1.NUMCB is 8 bits wide, so <= 256 banks. */
#define kAdrenoAsidId1NumcbMask      0x000000ffu
#define kAdrenoAsidId1MaxBanks       256u
/* P1:62 GENMASK(23,16): ID1.NUMS2CB is the stage-2 reserve, read at probe
 * (P2:1865 num_s2_context_banks = FIELD_GET(ID1_NUMS2CB, id)). It becomes the
 * first-fit start for a stage-1 domain (P2:734). */
#define kAdrenoAsidId1Nums2cbMask    0x00ff0000u
#define kAdrenoAsidId1Nums2cbShift   16u
/* P1:366 of arm-smmu.h: upstream ALSO clamps its own bitmap to 128. */
#define kAdrenoAsidMaxCbs            128u
/* TTBRn_ASID is GENMASK_ULL(63,48): a 16-bit field (P1:169). */
#define kAdrenoAsidFieldBits         16u
#define kAdrenoAsidFieldMask         0xffffull
/* The stage-1 CONTEXTIDR write carries the ASID (P2:625 for the AArch32 branch,
 * P1:191 for the offset); the low 8 bits are what this board can express because
 * round 101 measured ID2.VMID16 = 0 -- noted, though CBNDX is the tighter bound. */
#define kAdrenoAsidLow8Mask          0xffu

/* ---- The bitmap: 128 bits in two words, caller-owned storage -------- */
#define kAdrenoAsidBitmapWords       2u

struct adreno_asid_bitmap {
    uint64_t word[kAdrenoAsidBitmapWords]; /* bit i set => bank i is OWNED     */
    uint32_t capacity;   /* banks usable for stage-1 = num_context_banks  */
    uint32_t start;      /* P2:734 start = num_s2_context_banks           */
    uint32_t in_use;     /* count of owned banks, kept consistent          */
};

/* ---- What is still missing ------------------------------------------- */
enum adreno_asid_input {
    /* ID1.NUMCB and ID1.NUMS2CB are PROBE-TIME register reads (:1866, :1865), and
     * nobody in this port has read ID1 at all -- the same class of blocker as `ias`
     * was before round 101. */
    kAdrenoAsidInputId1 = 1u,
    /* A caller that will OWN the bank. An allocator with no writer is a library
     * exercise; recording it keeps `ready()` honest. */
    kAdrenoAsidInputCaller = 2u,
    kAdrenoAsidInputAll = 3u
};

/* ---- API (freestanding; no libc, no MMIO, no IOKit) ----------------- */

/* P2:1727-1744 is NOT used here. ID1.NUMCB is a raw count, not an id_size encoding,
 * so no size-to-bits conversion applies -- stated explicitly because the two have
 * been confused elsewhere in this tree. Returns the usable capacity, or 0 when the
 * encoded count is 0 or exceeds the ARM_SMMU_MAX_CBS ceiling. */
uint32_t adreno_asid_capacity_from_id1(uint32_t id1_raw);

/* P1:62 / P2:1865: decode the stage-2 reserve field of the SAME ID1 word. This is
 * the second device-sourced register field the stage-6 probe supplies. */
uint32_t adreno_asid_s2cb_count_from_id1(uint32_t id1_raw);

/* Decode BOTH fields of one ID1 probe word and initialise the bitmap through
 * adreno_asid_init(). Delegating (rather than duplicating) keeps the upstream
 * P2:1867-1870 "impossible number of S2 context banks!" refusal in one place:
 * init() already rejects num_s2 >= num_cb. Returns 0, or 1 on refusal. */
int adreno_asid_init_from_id1(struct adreno_asid_bitmap *b, uint32_t id1_raw);

/* Initialise the bitmap from the two probe values. `start` is the first bank the
 * caller may use (P2:734: num_s2_context_banks for a stage-1 domain). Refuses when
 * storage is NULL or the range is empty/inverted. Returns 0, or 1 on refusal. */
int adreno_asid_init(struct adreno_asid_bitmap *b, uint32_t num_context_banks,
                     uint32_t num_s2_context_banks);

/* The first-fit allocation __arm_smmu_alloc_bitmap() performs (arm-smmu.h inline):
 * scan from `start` for the first CLEAR bit below capacity, set it, return it.
 * Returns 0 and writes *cbndx on success; returns 1 and leaves *cbndx at
 * 0xffffffffu when the range is full (the -ENOSPC case). */
int adreno_asid_alloc(struct adreno_asid_bitmap *b, uint32_t *cbndx);

/* Free a bank index. Refuses (returns 1) when out of range or already clear, so a
 * double free cannot silently corrupt the count. */
int adreno_asid_free(struct adreno_asid_bitmap *b, uint32_t cbndx);

/* 1 when the bank index is currently owned, else 0 (also 0 when out of range). */
int adreno_asid_is_owned(const struct adreno_asid_bitmap *b, uint32_t cbndx);

/* Recompute the owned count by walking the bitmap. Used by the self-check to prove
 * the running `in_use` field cannot drift from the bits. */
uint32_t adreno_asid_count(const struct adreno_asid_bitmap *b);

/* ---- The tuple a writer needs, derived from the bank index ---------- */
/* P2:795: asid = cbndx. Returns the value for TTBRn bits 63..48 (masked to the
 * 16-bit field). */
uint64_t adreno_asid_ttbr_asid_field(uint32_t cbndx);
/* The CONTEXTIDR value for this bank. On this port that register is written only in
 * the AArch32 branch upstream (P2:625); for the AArch64 context the ASID reaches the
 * bank through TTBRn_ASID. Exposed so a caller cannot confuse the two. */
uint32_t adreno_asid_contextidr(uint32_t cbndx);
/* P1:120 S2CR.CBNDX is GENMASK(7,0): the bank index as it appears in S2CR. Refuses
 * (returns 0xffffffffu) for an index that does not fit. */
uint32_t adreno_asid_s2cr_cbndx(uint32_t cbndx);
/* P1:123 GR1_CBAR(n) = 0x0 + (n << 2): the CBAR byte offset for this bank. */
uint32_t adreno_asid_cbar_offset(uint32_t cbndx);

/* The inputs still missing. Mask of enum adreno_asid_input. */
uint32_t adreno_asid_missing(void);
/* Name of one input bit, or "unknown". */
const char *adreno_asid_input_name(uint32_t bit);
/* Citation for one input bit, or "unknown". */
const char *adreno_asid_input_anchor(uint32_t bit);
/* 1 only when NOTHING is missing. Derived, and 0 today. */
int adreno_asid_ready(void);

/* Self-check. Returns 0 when every invariant holds, else 1; *check_count receives
 * the number evaluated. */
int adreno_asid_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_SMMU_ASID_V1_H */
