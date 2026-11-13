// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoSmmuAsidV1 -- see AdrenoSmmuAsidV1.h for provenance and the honest
// HOST-VERIFIED boundary. Freestanding C11: no libc, no MMIO.

#include "AdrenoSmmuAsidV1.h"

/* The two absent inputs, in one place. Each anchor says why it is absent TODAY,
 * citing a probe-time read or a measurement -- never "not implemented". */
static uint32_t missing_inputs(void)
{
    uint32_t mask = 0u;
    /* ID1.NUMCB / ID1.NUMS2CB are read at probe (P2:1865-1866) and nobody in this
     * port has read ID1 at all -- the same class of blocker `ias` was before
     * round 101 obtained it. */
    mask |= kAdrenoAsidInputId1;
    /* No caller owns a bank yet: nothing in this port programs a context. */
    mask |= kAdrenoAsidInputCaller;
    return mask;
}

uint32_t adreno_asid_capacity_from_id1(uint32_t id1_raw)
{
    uint32_t count = id1_raw & kAdrenoAsidId1NumcbMask;

    /* 0 banks is a broken read, not an empty SMMU. */
    if (count == 0u) {
        return 0u;
    }
    /* Upstream clamps its own bitmap to ARM_SMMU_MAX_CBS (128) even though the IDR
     * field could encode up to 255; honour the smaller ceiling rather than the
     * wider field, because the allocator being mirrored is the clamped one. */
    if (count > kAdrenoAsidMaxCbs) {
        count = kAdrenoAsidMaxCbs;
    }
    return count;
}

uint32_t adreno_asid_s2cb_count_from_id1(uint32_t id1_raw)
{
    /* P1:62 ARM_SMMU_ID1_NUMS2CB GENMASK(23,16); P2:1865 reads exactly this field.
     * Upper mask bits outside the field are ignored, as FIELD_GET would do. */
    return (id1_raw & kAdrenoAsidId1Nums2cbMask) >> kAdrenoAsidId1Nums2cbShift;
}

int adreno_asid_init_from_id1(struct adreno_asid_bitmap *b, uint32_t id1_raw)
{
    const uint32_t numcb = adreno_asid_capacity_from_id1(id1_raw);
    const uint32_t nums2cb = adreno_asid_s2cb_count_from_id1(id1_raw);

    /* P2:1866 num_context_banks = FIELD_GET(ID1_NUMCB, id);
     * P2:1865 num_s2_context_banks = FIELD_GET(ID1_NUMS2CB, id).
     * init() enforces the P2:1867-1870 sanity check (num_s2 > num_cb is
     * "impossible number of S2 context banks!"), so this is a pure delegation. */
    return adreno_asid_init(b, numcb, nums2cb);
}

int adreno_asid_init(struct adreno_asid_bitmap *b, uint32_t num_context_banks,
                     uint32_t num_s2_context_banks)
{
    uint32_t i;

    if (b == 0) {
        return 1;
    }
    b->word[0] = 0ull;
    b->word[1] = 0ull;
    b->in_use = 0u;
    b->capacity = 0u;
    b->start = 0u;

    /* A stage-1 domain may only use banks at or above the stage-2 reserve
     * (P2:734: start = smmu->num_s2_context_banks). */
    if (num_context_banks == 0u || num_context_banks > kAdrenoAsidMaxCbs) {
        return 1;
    }
    if (num_s2_context_banks >= num_context_banks) {
        /* No stage-1 bank would exist: an inverted or empty range. */
        return 1;
    }
    /* The storage is two 64-bit words = 128 bits, which is exactly the clamp. */
    for (i = 0u; i < kAdrenoAsidBitmapWords; ++i) {
        b->word[i] = 0ull;
    }
    b->capacity = num_context_banks;
    b->start = num_s2_context_banks;
    return 0;
}

int adreno_asid_is_owned(const struct adreno_asid_bitmap *b, uint32_t cbndx)
{
    if (b == 0 || cbndx >= b->capacity) {
        return 0;
    }
    return ((b->word[cbndx >> 6] >> (cbndx & 63u)) & 1ull) != 0ull ? 1 : 0;
}

int adreno_asid_alloc(struct adreno_asid_bitmap *b, uint32_t *cbndx)
{
    uint32_t i;

    if (b == 0 || cbndx == 0) {
        return 1;
    }
    *cbndx = 0xffffffffu;
    /* FIRST-FIT from `start`, exactly as __arm_smmu_alloc_bitmap() does: the search
     * restarts at `start` on every call, so a lower bank freed by any means is
     * reused before any higher one. A rotating allocator would hand out a different
     * bank -- and therefore a different CBAR/TTBRn_ASID -- than the driver would. */
    for (i = b->start; i < b->capacity; ++i) {
        if (adreno_asid_is_owned(b, i) == 0) {
            b->word[i >> 6] |= (1ull << (i & 63u));
            b->in_use += 1u;
            *cbndx = i;
            return 0;
        }
    }
    return 1; /* the -ENOSPC case */
}

int adreno_asid_free(struct adreno_asid_bitmap *b, uint32_t cbndx)
{
    if (b == 0 || cbndx >= b->capacity) {
        return 1;
    }
    /* Refuse a double free rather than underflowing the count. Banks below `start`
     * are not ours to free either. */
    if (cbndx < b->start) {
        return 1;
    }
    if (adreno_asid_is_owned(b, cbndx) == 0) {
        return 1;
    }
    b->word[cbndx >> 6] &= ~(1ull << (cbndx & 63u));
    b->in_use -= 1u;
    return 0;
}

uint32_t adreno_asid_count(const struct adreno_asid_bitmap *b)
{
    uint32_t i;
    uint32_t n = 0u;

    if (b == 0) {
        return 0u;
    }
    for (i = 0u; i < b->capacity; ++i) {
        if (adreno_asid_is_owned(b, i) != 0) {
            n += 1u;
        }
    }
    return n;
}

uint64_t adreno_asid_ttbr_asid_field(uint32_t cbndx)
{
    /* P2:795 asid = cbndx, placed at TTBRn bits 63..48 (P1:169). Since a bank index
     * is at most 127, masking to the 16-bit field never truncates. */
    return ((uint64_t)cbndx & kAdrenoAsidFieldMask);
}

uint32_t adreno_asid_contextidr(uint32_t cbndx)
{
    /* P1:191 is the register; P2:625 writes it with cfg->asid, but ONLY in the
     * AArch32 short-descriptor branch. For the AArch64 context this port uses, the
     * ASID reaches the bank through TTBRn_ASID and CONTEXTIDR is not written at all.
     * The value is still returned so a caller that does write it writes the right
     * one. */
    return cbndx & kAdrenoAsidLow8Mask;
}

uint32_t adreno_asid_s2cr_cbndx(uint32_t cbndx)
{
    /* P1:120 ARM_SMMU_S2CR_CBNDX is GENMASK(7,0). */
    if (cbndx > kAdrenoAsidLow8Mask) {
        return 0xffffffffu;
    }
    return cbndx;
}

uint32_t adreno_asid_cbar_offset(uint32_t cbndx)
{
    /* P1:123 ARM_SMMU_GR1_CBAR(n) = 0x0 + (n << 2). */
    return cbndx << 2;
}

uint32_t adreno_asid_missing(void)
{
    return missing_inputs();
}

const char *adreno_asid_input_name(uint32_t bit)
{
    switch (bit) {
    case kAdrenoAsidInputId1:
        return "id1-numcb";
    case kAdrenoAsidInputCaller:
        return "bank-owner";
    default:
        return "unknown";
    }
}

const char *adreno_asid_input_anchor(uint32_t bit)
{
    switch (bit) {
    case kAdrenoAsidInputId1:
        return "P2:1866 num_context_banks = FIELD_GET(ARM_SMMU_ID1_NUMCB, id) is a "
               "PROBE-TIME read of ID1; nobody in this port has read ID1, the same "
               "class of blocker ias had before round 101";
    case kAdrenoAsidInputCaller:
        return "P2:795 cfg->asid = cfg->cbndx; allocating a bank is only meaningful "
               "when a caller will program it, and no context programmer exists";
    default:
        return "unknown";
    }
}

int adreno_asid_ready(void)
{
    /* Derived, not hard-coded. */
    return adreno_asid_missing() == 0u ? 1 : 0;
}

int adreno_asid_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    uint32_t i = 0u;
    uint32_t cbndx = 0xffffffffu;
    struct adreno_asid_bitmap b;

    /* ---- the two ceilings -------------------------------------------- */
    checks += 1u;
    if (kAdrenoAsidId1NumcbMask != 0xffu || kAdrenoAsidId1MaxBanks != 256u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoAsidMaxCbs != 128u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoAsidFieldMask != 0xffffull || kAdrenoAsidFieldBits != 16u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoAsidBitmapWords * 64u != kAdrenoAsidMaxCbs) {
        fails += 1u; /* the storage is exactly the clamp, so no index is unreachable */
    }

    /* ---- capacity derivation, including the CLAMP and the zero refusal -- */
    checks += 1u;
    if (adreno_asid_capacity_from_id1(0x00000010u) != 16u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_capacity_from_id1(0x00000080u) != 128u) {
        fails += 1u; /* exactly at the clamp */
    }
    checks += 1u;
    if (adreno_asid_capacity_from_id1(0x000000ffu) != kAdrenoAsidMaxCbs) {
        fails += 1u; /* 255 is clamped to 128, not honoured */
    }
    checks += 1u;
    if (adreno_asid_capacity_from_id1(0x00000000u) != 0u) {
        fails += 1u; /* zero banks is a broken read */
    }
    /* The ID1 field is 8 bits: the upper bits of the word are NOT part of the count. */
    checks += 1u;
    if (adreno_asid_capacity_from_id1(0xffffff10u) != 16u) {
        fails += 1u;
    }

    /* ---- init refusals ------------------------------------------------- */
    checks += 1u;
    if (adreno_asid_init(0, 16u, 0u) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_init(&b, 0u, 0u) != 1) {
        fails += 1u; /* no banks */
    }
    checks += 1u;
    if (adreno_asid_init(&b, 129u, 0u) != 1) {
        fails += 1u; /* above the clamp */
    }
    checks += 1u;
    if (adreno_asid_init(&b, 16u, 16u) != 1) {
        fails += 1u; /* inverted range: no stage-1 bank would exist */
    }
    checks += 1u;
    if (adreno_asid_init(&b, 8u, 16u) != 1) {
        fails += 1u;
    }

    /* ---- first-fit, and the `start` reserve ---------------------------- */
    checks += 1u;
    if (adreno_asid_init(&b, 8u, 2u) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (b.capacity != 8u || b.start != 2u || b.in_use != 0u) {
        fails += 1u;
    }
    /* The first allocation must be `start`, not 0: banks below it are the stage-2
     * reserve (P2:734). */
    checks += 1u;
    if (adreno_asid_alloc(&b, &cbndx) != 0 || cbndx != 2u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_alloc(&b, &cbndx) != 0 || cbndx != 3u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_is_owned(&b, 0u) != 0 || adreno_asid_is_owned(&b, 1u) != 0) {
        fails += 1u; /* the reserve stays unowned */
    }
    /* FIRST-FIT: free the LOWER index then allocate again. Upstream restarts the
     * scan at `start`, so 2 must come back even though 4 is also free. A rotating
     * allocator would return 4 and silently diverge from the driver. */
    checks += 1u;
    if (adreno_asid_free(&b, 2u) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_alloc(&b, &cbndx) != 0 || cbndx != 2u) {
        fails += 1u; /* the first-fit property, or the allocator is not upstream */
    }

    /* ---- exhaustion and the -ENOSPC shape ------------------------------ */
    checks += 1u;
    if (adreno_asid_init(&b, 3u, 0u) != 0) {
        fails += 1u;
    }
    for (i = 0u; i < 3u; ++i) {
        checks += 1u;
        if (adreno_asid_alloc(&b, &cbndx) != 0 || cbndx != i) {
            fails += 1u;
        }
    }
    checks += 1u;
    cbndx = 0x12345678u;
    if (adreno_asid_alloc(&b, &cbndx) != 1 || cbndx != 0xffffffffu) {
        fails += 1u; /* refusal must CLEAR the out-parameter */
    }
    checks += 1u;
    if (b.in_use != 3u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_count(&b) != 3u) {
        fails += 1u; /* the running count agrees with the bits */
    }

    /* ---- free refusals: double free, out of range, below start --------- */
    checks += 1u;
    if (adreno_asid_free(&b, 0u) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_free(&b, 0u) != 1) {
        fails += 1u; /* double free is refused */
    }
    checks += 1u;
    if (adreno_asid_free(&b, 3u) != 1) {
        fails += 1u; /* out of range */
    }
    checks += 1u;
    if (adreno_asid_init(&b, 8u, 4u) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_free(&b, 2u) != 1) {
        fails += 1u; /* below start is not ours to free */
    }
    checks += 1u;
    if (b.in_use != 0u) {
        fails += 1u;
    }

    /* ---- the derived tuple -------------------------------------------- */
    checks += 1u;
    if (adreno_asid_ttbr_asid_field(0u) != 0ull) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_ttbr_asid_field(0x7fu) != 0x7full) {
        fails += 1u;
    }
    /* asid == cbndx: the field is the index, with no offset and no transform. */
    checks += 1u;
    if (adreno_asid_ttbr_asid_field(5u) != 5ull) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_contextidr(0x2au) != 0x2au) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_s2cr_cbndx(0x2au) != 0x2au) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_s2cr_cbndx(0x100u) != 0xffffffffu) {
        fails += 1u; /* CBNDX is 8 bits; a wider index is refused, not truncated */
    }
    checks += 1u;
    if (adreno_asid_cbar_offset(0u) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_cbar_offset(1u) != 4u) {
        fails += 1u; /* GR1_CBAR(n) = n << 2 */
    }
    checks += 1u;
    if (adreno_asid_cbar_offset(0x1f4u) != 0x1f4u * 4u) {
        fails += 1u; /* the pinned SMMU phandle value maps to a real offset */
    }

    /* ---- one ID1 word decodes BOTH fields (stage-6 device read) -------- */
    /* 0x00020008u encodes NUMS2CB=2, NUMCB=8 -- a plausible probe word. */
    {
        const uint32_t id1 = 0x00020008u;
        checks += 1u;
        if (adreno_asid_s2cb_count_from_id1(id1) != 2u) {
            fails += 1u;
        }
        checks += 1u;
        if (adreno_asid_capacity_from_id1(id1) != 8u) {
            fails += 1u;
        }
        checks += 1u;
        if (adreno_asid_init_from_id1(&b, id1) != 0) {
            fails += 1u;
        }
        checks += 1u;
        if (b.capacity != 8u || b.start != 2u) {
            fails += 1u; /* start == num_s2_context_banks (P2:734) */
        }
        cbndx = 0xffffffffu;
        checks += 1u;
        if (adreno_asid_alloc(&b, &cbndx) != 0 || cbndx != 2u) {
            fails += 1u; /* first-fit lands on the first non-reserved bank */
        }
        /* 0x00090008u encodes NUMS2CB=9 > NUMCB=8: P2:1867-1870 "impossible
         * number of S2 context banks!" -- init_from_id1 must refuse. */
        checks += 1u;
        if (adreno_asid_init_from_id1(&b, 0x00090008u) == 0) {
            fails += 1u;
        }
    }

    /* ---- the absent inputs, derived ----------------------------------- */
    checks += 1u;
    if (adreno_asid_missing() != kAdrenoAsidInputAll) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoAsidInputAll != 3u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_asid_ready() != 0) {
        fails += 1u;
    }
    /* Names and anchors are total, non-empty and distinct. */
    {
        const uint32_t bits[2] = { kAdrenoAsidInputId1, kAdrenoAsidInputCaller };
        for (i = 0u; i < 2u; ++i) {
            const char *n = adreno_asid_input_name(bits[i]);
            const char *a = adreno_asid_input_anchor(bits[i]);
            uint32_t j;
            uint32_t k;
            uint32_t same;
            checks += 1u;
            if (n == 0 || n[0] == '\0' || a == 0 || a[0] == '\0') {
                fails += 1u;
                continue;
            }
            checks += 1u;
            if (n[0] == 'u' && n[1] == 'n') {
                fails += 1u;
                continue;
            }
            for (j = 0u; j < i; ++j) {
                const char *m = adreno_asid_input_name(bits[j]);
                k = 0u;
                same = 1u;
                while (n[k] != '\0' || m[k] != '\0') {
                    if (n[k] != m[k]) {
                        same = 0u;
                        break;
                    }
                    k += 1u;
                }
                checks += 1u;
                if (same != 0u) {
                    fails += 1u;
                }
            }
        }
        checks += 1u;
        if (adreno_asid_input_name(0u)[0] != 'u' ||
            adreno_asid_input_anchor(0u)[0] != 'u') {
            fails += 1u;
        }
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return (fails != 0u) ? 1 : 0;
}
