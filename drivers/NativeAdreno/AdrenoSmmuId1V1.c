// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuId1V1 -- read GR0_ID1 and turn it into the ONE input the ASID
// allocator has always been missing: num_context_banks.
//
// WHY THIS EXISTS
// ---------------
// round 101 measured ID2 on the phone (ias = 48) and that unlocked the stage-1
// context words. The ASID allocator (AdrenoSmmuAsidV1, round 101 too) is
// complete -- adreno_asid_init/alloc/free/is_owned are implemented -- but its
// own `adreno_asid_input_anchor()` says the remaining blocker is:
//
//     "P2:1866 num_context_banks = FIELD_GET(ARM_SMMU_ID1_NUMCB, id) is a
//      PROBE-TIME read of ID1; nobody in this port has read ID1, the same
//      class of blocker ias had before round 101"
//
// So the missing piece is not code, it is a READ. This module supplies the
// decode and the allocation-size derivation, and states plainly what it does
// NOT do: it does not touch the SMMU, does not allocate in a caller's stead,
// and does not claim a context bank is usable merely because it exists.
//
// PROVENANCE (pinned sha256, re-checked by scripts/verify-enchilada-smmu-id1.py)
// -----------------------------------------------------------------------------
//  (P1) drivers/iommu/arm/arm-smmu/arm-smmu.h  16630 B
//       ed85998794695d1a824dc1b86f680c8d124546688fd2665e00e3cbcfa9546b53
//       :59  ARM_SMMU_GR0_ID1 = 0x24
//       :63  ARM_SMMU_ID1_NUMCB = GENMASK(7, 0)
//  (P2) drivers/iommu/arm/arm-smmu/arm-smmu.c  67693 B
//       d5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a
//       :1866 num_context_banks = FIELD_GET(ARM_SMMU_ID1_NUMCB, id)
//       :1867-1870 the "impossible number of S2 context banks!" guard
//
// Freestanding C11: no libc, no MMIO, no globals. The caller does the read.

#include "AdrenoSmmuId1V1.h"

uint32_t adreno_smmu_id1_numcb(uint32_t id1)
{
    /* P1:63 GENMASK(7, 0) -- eight bits, no shift. */
    return id1 & kAdrenoSmmuId1NumcbMask;
}

uint32_t adreno_smmu_id1_decode(uint32_t id1, struct adreno_smmu_id1 *out)
{
    if (out == 0) {
        return 1u;
    }
    out->raw = id1;
    out->num_context_banks = adreno_smmu_id1_numcb(id1);

    /* A device must report at least one bank, or the port cannot allocate an
     * ASID at all. Zero is not a value to work around quietly: it means either
     * the read did not land or the block is not the SMMU. (P2:1866-1870 treats
     * an impossible count the same way -- refuse rather than clamp.)
     *
     * This device reports 5 (id1=0x20008005 in 103 retained windows), so the
     * refusal branch is the fail-closed path, not the expected one. */
    if (out->num_context_banks == 0u) {
        out->plausible = 0u;
        out->reason = "ID1.NUMCB is 0: no context bank reported";
        return 0u;
    }
    out->plausible = 1u;
    out->reason = "";
    return 0u;
}

uint32_t adreno_smmu_id1_asid_capacity(uint32_t num_context_banks)
{
    /* P2:795 cfg->asid = cfg->cbndx, and arm_smmu_alloc_context_bank() starts
     * at num_s2_context_banks. With no stage-2 banks in this port the allocator
     * may hand out every bank ID1 reports, so the capacity is simply that
     * count. Kept as a function, not a constant, so a caller cannot mistake a
     * board constant for a measured value. */
    return num_context_banks;
}

uint32_t adreno_smmu_id1_plausible_max(void)
{
    /* P1:63 is eight bits wide, so the encoding cannot express more than 255.
     * Anything in (0, 256) is representable; the guard in P2:1867-1870 exists
     * because a *derived* count can exceed the bitmap, not because the field
     * can. Stated as the field's own limit. */
    return kAdrenoSmmuId1NumcbMask;
}

int adreno_smmu_id1_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    struct adreno_smmu_id1 d;

    /* ---- the offset and the mask, straight from P1 --------------------- */
    checks += 1u;
    if (kAdrenoSmmuGr0Id1 != 0x24u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoSmmuId1NumcbMask != 0xffu) {
        fails += 1u;
    }

    /* ---- the decode, including both edges ------------------------------ */
    checks += 1u;
    if (adreno_smmu_id1_numcb(0x00000000u) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_id1_numcb(0x000000ffu) != 255u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_id1_numcb(0xffffff00u) != 0u) {
        fails += 1u; /* upper bits are not part of NUMCB */
    }
    /* The value this port must NOT invent: ID2-style upper fields ignored. */
    checks += 1u;
    if (adreno_smmu_id1_numcb(0x00007511u) != 0x11u) {
        fails += 1u;
    }

    /* ---- zero is refused, not clamped ---------------------------------- */
    checks += 1u;
    if (adreno_smmu_id1_decode(0x00000000u, &d) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (d.plausible != 0u || d.num_context_banks != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (d.reason == 0 || d.reason[0] == 0) {
        fails += 1u;
    }

    /* ---- a real value decodes and stays plausible ---------------------- */
    checks += 1u;
    if (adreno_smmu_id1_decode(0x00000030u, &d) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (d.num_context_banks != 48u || d.plausible != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (d.reason != 0 && d.reason[0] != 0) {
        fails += 1u;
    }

    /* ---- the capacity is derived, never a constant --------------------- */
    checks += 1u;
    if (adreno_smmu_id1_asid_capacity(48u) != 48u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_id1_asid_capacity(d.num_context_banks) != d.num_context_banks) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_id1_plausible_max() != 255u) {
        fails += 1u;
    }

    /* ---- a null out-pointer is a refusal, not a crash ------------------ */
    checks += 1u;
    if (adreno_smmu_id1_decode(0x00000030u, 0) == 0u) {
        fails += 1u;
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return (fails == 0u) ? 0 : 1;
}
