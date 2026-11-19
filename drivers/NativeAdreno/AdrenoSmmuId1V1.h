// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuId1V1 -- decode GR0_ID1 into num_context_banks, the last input the
// ASID allocator was missing.
//
// BOUNDARY (read this before using the module)
// -------------------------------------------
// This module DECODES a word. It does not read the SMMU, it does not own a
// context bank, and it does not assert that any bank is usable.
//
//   modelled : the offset (P1:59 0x24), the field (P1:63 GENMASK(7,0)), the
//              zero-refusal and the capacity derivation.
//   NOT known: the value this device actually reports. Nobody has read ID1 on
//              hardware yet. Until a device row carries it, num_context_banks
//              stays an ARGUMENT here, exactly as `ias` was before round 101.
//
// That distinction is the point: round 101 turned `ias` from an argument into a
// measurement, and this module is the decode half of the same move for ID1.

#ifndef ADRENO_SMMU_ID1_V1_H
#define ADRENO_SMMU_ID1_V1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The register and its field (P1:59, P1:63) ------------------------- */
#define kAdrenoSmmuGr0Id1        0x0024u /* P1:59 ARM_SMMU_GR0_ID1  */
#define kAdrenoSmmuId1NumcbMask  0x000000ffu /* P1:63 GENMASK(7, 0) */

struct adreno_smmu_id1 {
    uint32_t raw;                /* the word as read                    */
    uint32_t num_context_banks;  /* P2:1866 FIELD_GET(ID1_NUMCB, raw)   */
    uint32_t plausible;          /* 1 when num_context_banks >= 1       */
    const char *reason;          /* why it was refused, or ""           */
};

/* P2:1866 -- the field itself. Kept separate so a caller that already has a
 * word does not need the struct just to decode it. */
uint32_t adreno_smmu_id1_numcb(uint32_t id1);

/* Decode into `out`. Returns 0 when the word was written (even if it is
 * implausible -- check `plausible`), 1 when `out` is null. A zero NUMCB is
 * refused rather than clamped: P2:1867-1870 treats an impossible count as a
 * fatal condition, and a silent clamp would hide a wrong register. */
uint32_t adreno_smmu_id1_decode(uint32_t id1, struct adreno_smmu_id1 *out);

/* P2:795 cfg->asid = cfg->cbndx, with no stage-2 banks in this port: every
 * bank ID1 reports may be handed out. Derived, never a board constant. */
uint32_t adreno_smmu_id1_asid_capacity(uint32_t num_context_banks);

/* The field's own encoding limit (255), stated so a caller cannot pass a count
 * the register could not have expressed. */
uint32_t adreno_smmu_id1_plausible_max(void);

/* Self-check. Returns 0 and writes the check count on success. */
int adreno_smmu_id1_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_SMMU_ID1_V1_H */
