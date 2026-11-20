// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuInstallV1 -- resolve CBn_BASE from ID1 and make the nine-word context plan
// absolute, so the writes can actually be issued.
//
// THE GAP THIS CLOSES
// -------------------
// Round 83's context plan says WHAT to write (CBA2R, CBAR, TCR2, TCR, TTBR0, TTBR1,
// MAIR0, MAIR1, SCTLR) and in which ORDER. It deliberately does not say WHERE a context
// bank lives, because that depends on ID1.NUMPAGENDXB and ID1.PAGESIZE -- and ID1 was
// only decoded in round 227. This module supplies the missing base.
//
// UPSTREAM DERIVATION (arm-smmu.c, arm_smmu_device_cfg_probe)
// -----------------------------------------------------------
//   smmu->pgshift = (id & ARM_SMMU_ID1_PAGESIZE) ? 16 : 12;
//   size          = 1 << (FIELD_GET(ARM_SMMU_ID1_NUMPAGENDXB, id) + 1);
//   smmu->numpage = size;
//   #define ARM_SMMU_CB(s, n)  ((s)->numpage + (n))
//
// A bank's byte offset is therefore `(size + n) << pgshift`.
//
// THE MEASURED WORD (103 retained `stage6=id1` rows, all identical)
// ---------------------------------------------------------------
//   ID1 = 0x20008005
//     NUMCB       GENMASK(7,0)   = 5
//     NUMS2CB     GENMASK(23,16) = 0
//     NUMPAGENDXB GENMASK(30,28) = 2     (bit 29 set)
//     PAGESIZE    BIT(31)        = 0
//   -> pgshift 12, numpage 8, CBn_BASE = 0x8000 + n * 0x1000
//
// WHY `gr1` MATTERS HERE
// ----------------------
// A GR1 offset (CBAR, CBA2R) already encodes the bank index in its register number
// upstream, so it is absolute. A bank-relative offset (TCR, TTBR0, SCTLR, ...) is not.
// Adding the base to both would place the GR1 writes inside a context bank; adding it
// to neither would leave the bank-relative writes pointing at CB0. Both are silent
// corruptions, so the two classes are handled separately and asserted in the self-check.
//
// BOUNDARY
// --------
// MODELLED : the base derivation, the per-bank address arithmetic, the GR1/bank split,
//            and refusal of a bank index at or beyond NUMCB.
// NOT done : **no MMIO.** Nothing here writes a register, and nothing asserts that any
//            write has reached the SMMU. A caller issues the writes.

#ifndef ADRENO_SMMU_INSTALL_V1_H
#define ADRENO_SMMU_INSTALL_V1_H

#include <stdint.h>

#include "AdrenoSmmuCtxWordV1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ID1 fields (P1 arm-smmu.h) --------------------------------------- */
#define kAdrenoInstallId1Pagesize            (1u << 31)   /* BIT(31)      */
#define kAdrenoInstallId1NumpagendxbMask     (0x7u << 28) /* GENMASK(30,28) */
#define kAdrenoInstallId1NumpagendxbShift    28u
#define kAdrenoInstallId1NumcbMask           0xffu        /* GENMASK(7,0) */

/* ARM_SMMU_CB_TTBR0 (P1 arm-smmu.h) -- asserted here because the self-check relies on
 * it when it verifies that TTBR0 moved by exactly the base. */
#define kAdrenoInstallCbTtbr0                0x0020u

struct adreno_install_geo {
    uint32_t pgshift;           /* 12 or 16, from ID1.PAGESIZE              */
    uint32_t numpage;           /* 1 << (NUMPAGENDXB + 1), in pages         */
    uint32_t cb_base;           /* numpage << pgshift, the first CB's base  */
    uint32_t num_context_banks; /* ID1.NUMCB: bounds a legitimate cbndx     */
};

/* One resolved register write: an absolute byte offset in the SMMU frame. */
struct adreno_install_write {
    uint64_t offset;    /* absolute, ready to add to the mapped base        */
    uint32_t value;
    uint32_t value_hi;
    uint32_t is_64bit;
    uint32_t gr1;       /* 1 when the offset stayed in the GR1 frame        */
    uint32_t order;
};

/* Derive the geometry from ID1. Returns 0, or 1 on a null out, 2 when NUMCB is zero
 * (a broken read is not an empty SMMU). */
uint32_t adreno_install_cb_geometry(uint32_t id1, struct adreno_install_geo *out);

/* Byte offset of bank `cbndx`. Returns 0, or 1 on null out, 2 when the index is at or
 * beyond NUMCB. */
uint32_t adreno_install_bank_base(const struct adreno_install_geo *geo,
                                  uint32_t cbndx, uint64_t *out);

/* Add the bank base to every bank-relative offset and leave GR1 offsets as they are.
 * Returns 0 and sets *written, or 1 null argument, 2 capacity too small, 3 bad bank. */
uint32_t adreno_install_absolute(const struct adreno_install_geo *geo,
                                 uint32_t cbndx,
                                 const struct adreno_smmu_ctx_write *in,
                                 uint32_t count,
                                 struct adreno_install_write *out,
                                 uint32_t cap, uint32_t *written);

/* Self-check. Returns 0 and writes the check count on success. */
int adreno_install_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_SMMU_INSTALL_V1_H */
