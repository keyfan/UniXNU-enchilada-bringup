// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoVaMapV1 -- enchilada stage 5 (GPU) HOST-VERIFIED GPU VIRTUAL ADDRESS
// SPACE DERIVATION (round 77, 2026-11-12).
//
// This is NOT a board VA map and NOT a driver. It encodes the exact CHAIN that
// produces the GPU VA aperture in mainline, and then proves -- rather than
// asserts -- that the chain terminates in a RUNTIME REGISTER READ on this
// hardware. That is the load-bearing finding: the GPU VA range is not a board
// constant that a document could record, so the round-67 "window-zero gate" is
// waiting for something no static source can supply.
//
// WHY THIS ROUND (admission table, prerequisite `board-va-map`)
// -----------------------------------------------------------
// That row has read `modeled = 0` since round 69 with the reason "the map itself
// is the missing input". Repeated rounds have said variants of "a board VA map is
// absent". None of them said WHY it cannot simply be derived the way the catalog,
// clock and register facts were. This round answers that, and the answer changes
// what the remaining work is: deriving it needs a powered SMMU and a page-table
// walker, not another document.
//
// PROVENANCE (3 sha256 pins re-checked by scripts/verify-enchilada-va-map.py)
// -------------------------------------------------------------------------
//  (P1) drivers/gpu/drm/msm/adreno/adreno_gpu.c  29592 B
//       e6b5203fb56358d22542197bd251c1513dc2b2f1ae8aaf3f9d9fca129f6f00b7
//       :201-233 adreno_iommu_create_address_space(): asks the SMMU for its
//       geometry, then start = max(SZ_16M, geometry->aperture_start) and
//       size = aperture_end - start + 1. :195-198 adreno_create_address_space()
//       delegates with quirks 0.
//  (P2) drivers/iommu/arm/arm-smmu/arm-smmu.c  67693 B
//       d5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a
//       :820-831 the domain geometry is derived from the PAGE TABLE FORMAT and
//       `ias`: aperture_end = (1 << ias) - 1, or the TTBR1 variant
//       aperture_start = ~0 << ias. :735 ias = smmu->va_size.
//       :1904-1905 for an SMMU v2, va_size = arm_smmu_id_size_to_bits(
//       FIELD_GET(ARM_SMMU_ID2_UBS, ID2)) -- i.e. read from ID2 at probe time.
//       :1879-1885 ID2 is read with arm_smmu_gr0_read(); ipa/pa sizes likewise.
//  (P3) drivers/gpu/drm/msm/adreno/a6xx_gmu.c  48981 B
//       c14fbb253e602bfd2585d757da871dde08ea63b811ff3c6808f657f4522f78d4
//       :1283 the GMU's OWN address space is created independently with
//       msm_gem_address_space_create(mmu, "gmu", 0x0, 0x80000000), and :1245
//       range_end = 0x80000000 for the un-IOVA'd allocations. That 2 GiB is the
//       GMU's space, NOT the GPU's -- conflating the two is the trap.
//
// THE CHAIN, AND WHERE IT STOPS
// -----------------------------
//   GPU aperture
//     <- max(16 MiB, aperture_start) .. aperture_end        (P1:224-225)
//     <- (1 << ias) - 1                                     (P2:828)
//     <- ias = smmu->va_size                                (P2:735)
//     <- arm_smmu_id_size_to_bits(ID2.UBS)                  (P2:1904-1905)
//     <- READ OF THE SMMU ID2 REGISTER AT PROBE              (P2:1879)
// The last line is the end of what any pinned source can tell you: the value
// depends on the silicon's ID2.UBS field. A document cannot record it, a DTS
// cannot declare it, and this port cannot read it without a powered SMMU and a
// mapping -- which is the same blocker the SMMU row already records.
//
// HONEST BOUNDARY
// ---------------
// HOST-VERIFIED, DERIVATION MODEL ONLY. No MMIO, no page table, no VA
// allocation, no mapping. `adreno_va_map_ready()` is derived from the two things
// it needs and is 0. CONFIG_XNU_NATIVE_ADRENO remains n; the gpu area STAYS
// "host-verified"; no device run, no flash, m39510 NOT exercised. It does NOT
// unblock stage 5. ---------------------------------------------------------------------------

#ifndef ADRENO_VA_MAP_V1_H
#define ADRENO_VA_MAP_V1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- P1:224 the 16 MiB floor, and P3:1283 the GMU's 2 GiB (a DIFFERENT space) */
#define kAdrenoVaFloorBytes      0x01000000ull /* P1:224 SZ_16M */
#define kAdrenoVaGmuApertureEnd  0x80000000ull /* P3:1283 GMU only, NOT the GPU */

/* ---- P2:825-828 the two geometry shapes ------------------------------- */
#define kAdrenoVaShapeTtbr0      0u /* aperture_end = (1 << ias) - 1, start 0 */
#define kAdrenoVaShapeTtbr1      1u /* aperture_start = ~0 << ias              */

/* ---- Why the derivation cannot finish (P2:1879, :1904-1905) ----------- */
enum adreno_va_blocker {
    kAdrenoVaBlockerId2Unread = 1u,  /* ID2.UBS is a RUNTIME register read   */
    kAdrenoVaBlockerNoSmmu = 2u,     /* no powered SMMU to read it through   */
    kAdrenoVaBlockerNoWalker = 4u,   /* no page-table walker to back an aspace */
    kAdrenoVaBlockerAll = 7u
};

/* The derived aperture, once `ias` is known. */
struct adreno_va_aperture {
    uint64_t start;   /* after the max(SZ_16M, ...) clamp                    */
    uint64_t end;     /* aperture_end from the geometry shape               */
    uint64_t size;    /* end - start + 1                                    */
    uint32_t ias;     /* the input dimension, in bits                       */
    uint32_t shape;   /* kAdrenoVaShapeTtbr0 / _Ttbr1                        */
};

/* ---- API (freestanding; no libc, no MMIO, no IOKit) ------------------ */

/* P2:825-828: geometry from (ias, shape). Refuses ias outside 16..48, or when
 * the computed range would not fit 64 bits. Returns 0, or 1 on refusal. */
int adreno_va_geometry(uint32_t ias, uint32_t shape,
                       uint64_t *aperture_start, uint64_t *aperture_end);
/* P1:224-225: the GPU space = max(16 MiB, aperture_start) .. aperture_end. */
int adreno_va_aperture(uint32_t ias, uint32_t shape,
                       struct adreno_va_aperture *out);
/* P1:224 the clamp, in isolation: max(SZ_16M, start). */
uint64_t adreno_va_clamp_start(uint64_t aperture_start);

/* The GMU's own space (P3:1283). Exposed separately so a caller cannot mistake
 * its 0x80000000 for the GPU aperture. 1 when the two are the same value -- the
 * conflation this API exists to prevent. */
int adreno_va_gmu_end_is_gpu_end(uint64_t gpu_end);

/* The two things still missing. Mask of enum adreno_va_blocker. */
uint32_t adreno_va_missing(void);
/* Name of a blocker bit, or "unknown". */
const char *adreno_va_blocker_name(uint32_t bit);
/* Citation for a blocker bit, or "unknown". */
const char *adreno_va_blocker_anchor(uint32_t bit);

/* 1 only when the derivation could complete on this hardware. Derived from
 * adreno_va_missing(); 0 today. */
int adreno_va_map_ready(void);
/* 1 when `ias` is a plausible SMMU input-address size, else 0. The RANGE is
 * derivable from the architecture; the VALUE is not. */
int adreno_va_ias_is_plausible(uint32_t ias);

/* Self-check. Returns 0 when every invariant holds, else 1; *check_count
 * receives the number evaluated. */
int adreno_va_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_VA_MAP_V1_H */
