// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoVaMapV1 -- see AdrenoVaMapV1.h for provenance and the honest
// HOST-VERIFIED boundary. Freestanding C11: no libc, no MMIO.

#include "AdrenoVaMapV1.h"

/* The two blockers, in one place so obtaining one is a single edit. */
static uint32_t missing_blockers(void)
{
    uint32_t mask = 0u;
    /* B1: ID2.UBS is read from the SMMU at probe (P2:1879, :1904-1905). */
    mask |= kAdrenoVaBlockerId2Unread;
    /* B2: reading it needs a powered SMMU; round 35 measured CX DOWN. */
    mask |= kAdrenoVaBlockerNoSmmu;
    /* B3: an address space needs a page-table walker; this port has none. */
    mask |= kAdrenoVaBlockerNoWalker;
    return mask;
}

int adreno_va_ias_is_plausible(uint32_t ias)
{
    /* An ARM SMMU v2 IAS is a whole number of bits the walker can address. The
     * architectural range is 32..48 for the formats this port cares about; below
     * 16 would make the 16 MiB floor meaningless, which is why the clamp exists. */
    return (ias >= 16u && ias <= 48u) ? 1 : 0;
}

uint64_t adreno_va_clamp_start(uint64_t aperture_start)
{
    /* P1:224 start = max_t(u64, SZ_16M, geometry->aperture_start). */
    return (aperture_start > kAdrenoVaFloorBytes) ? aperture_start
                                                  : kAdrenoVaFloorBytes;
}

int adreno_va_geometry(uint32_t ias, uint32_t shape,
                       uint64_t *aperture_start, uint64_t *aperture_end)
{
    if (aperture_start == 0 || aperture_end == 0) {
        return 1;
    }
    if (!adreno_va_ias_is_plausible(ias)) {
        return 1;
    }
    if (shape == kAdrenoVaShapeTtbr0) {
        /* P2:828 aperture_end = (1UL << ias) - 1; start stays 0 here. */
        *aperture_start = 0u;
        *aperture_end = (ias >= 64u) ? ~0ull : ((1ull << ias) - 1ull);
    } else if (shape == kAdrenoVaShapeTtbr1) {
        /* P2:825-826 aperture_start = ~0UL << ias; aperture_end = ~0UL. */
        *aperture_start = (ias >= 64u) ? 0u : (~0ull << ias);
        *aperture_end = ~0ull;
    } else {
        return 1;
    }
    return 0;
}

int adreno_va_aperture(uint32_t ias, uint32_t shape,
                       struct adreno_va_aperture *out)
{
    uint64_t a_start = 0u, a_end = 0u;

    if (out == 0) {
        return 1;
    }
    if (adreno_va_geometry(ias, shape, &a_start, &a_end) != 0) {
        return 1;
    }
    out->ias = ias;
    out->shape = shape;
    out->end = a_end;
    out->start = adreno_va_clamp_start(a_start);
    if (out->end < out->start) {
        return 1; /* empty range: the clamp swallowed the aperture */
    }
    out->size = out->end - out->start + 1u;
    return 0;
}

int adreno_va_gmu_end_is_gpu_end(uint64_t gpu_end)
{
    /* P3:1283 the GMU's space ends at 0x80000000. If a caller's GPU end happens
     * to equal it, that is a coincidence to be flagged, not a fact to rely on. */
    return (gpu_end == kAdrenoVaGmuApertureEnd) ? 1 : 0;
}

uint32_t adreno_va_missing(void)
{
    return missing_blockers();
}

const char *adreno_va_blocker_name(uint32_t bit)
{
    switch (bit) {
    case kAdrenoVaBlockerId2Unread:
        return "id2-ubs-unread";
    case kAdrenoVaBlockerNoSmmu:
        return "no-powered-smmu";
    case kAdrenoVaBlockerNoWalker:
        return "no-page-table-walker";
    default:
        return "unknown";
    }
}

const char *adreno_va_blocker_anchor(uint32_t bit)
{
    switch (bit) {
    case kAdrenoVaBlockerId2Unread:
        return "arm-smmu.c:1904-1905 va_size = id_size_to_bits(FLD_GET(ID2.UBS));"
               " :1879 ID2 is read from the SMMU -- a runtime register, not a"
               " board constant";
    case kAdrenoVaBlockerNoSmmu:
        return "reading ID2 needs the kgsl SMMU powered; round 35 measured"
               " cx=0x00005ff4 with BIT(31) clear (CX DOWN)";
    case kAdrenoVaBlockerNoWalker:
        return "an address space needs a page-table walker; round 75 recorded"
               " that this port allocates and walks no LPAE table";
    default:
        return "unknown";
    }
}

int adreno_va_map_ready(void)
{
    /* Derived, not hard-coded. */
    return adreno_va_missing() == 0u ? 1 : 0;
}

int adreno_va_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    uint32_t i = 0u;
    uint32_t missing = 0u;
    uint64_t s = 0u, e = 0u;
    struct adreno_va_aperture ap;

    /* ---- the clamp (P1:224) ------------------------------------------- */
    checks += 1u;
    if (adreno_va_clamp_start(0u) != kAdrenoVaFloorBytes) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_clamp_start(0x1000u) != kAdrenoVaFloorBytes) {
        fails += 1u; /* smaller than the floor -> clamped up */
    }
    checks += 1u;
    if (adreno_va_clamp_start(0x40000000ull) != 0x40000000ull) {
        fails += 1u; /* larger -> passes through */
    }
    checks += 1u;
    if (adreno_va_clamp_start(kAdrenoVaFloorBytes) != kAdrenoVaFloorBytes) {
        fails += 1u;
    }

    /* ---- ias plausibility --------------------------------------------- */
    checks += 1u;
    if (adreno_va_ias_is_plausible(15u) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_ias_is_plausible(16u) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_ias_is_plausible(48u) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_ias_is_plausible(49u) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_ias_is_plausible(32u) != 1) {
        fails += 1u;
    }

    /* ---- TTBR0 geometry (P2:828) -------------------------------------- */
    checks += 1u;
    if (adreno_va_geometry(32u, kAdrenoVaShapeTtbr0, &s, &e) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (s != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (e != 0xffffffffull) {
        fails += 1u; /* (1 << 32) - 1 */
    }
    checks += 1u;
    if (adreno_va_geometry(40u, kAdrenoVaShapeTtbr0, &s, &e) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (e != 0xffffffffffull) {
        fails += 1u; /* (1 << 40) - 1 */
    }

    /* ---- TTBR1 geometry (P2:825-826) ---------------------------------- */
    checks += 1u;
    if (adreno_va_geometry(40u, kAdrenoVaShapeTtbr1, &s, &e) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (s != (~0ull << 40)) {
        fails += 1u;
    }
    checks += 1u;
    if (e != ~0ull) {
        fails += 1u;
    }

    /* ---- the composed aperture (P1:224-225) --------------------------- */
    checks += 1u;
    if (adreno_va_aperture(32u, kAdrenoVaShapeTtbr0, &ap) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (ap.start != kAdrenoVaFloorBytes) {
        fails += 1u; /* the 16 MiB floor applies to a 0 start */
    }
    checks += 1u;
    if (ap.end != 0xffffffffull) {
        fails += 1u;
    }
    checks += 1u;
    if (ap.size != 0xffffffffull - kAdrenoVaFloorBytes + 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (ap.ias != 32u || ap.shape != kAdrenoVaShapeTtbr0) {
        fails += 1u;
    }
    /* A larger ias gives a strictly larger space -- monotonic, no surprises. */
    {
        struct adreno_va_aperture big;
        checks += 1u;
        if (adreno_va_aperture(40u, kAdrenoVaShapeTtbr0, &big) != 0) {
            fails += 1u;
        }
        checks += 1u;
        if (big.size <= ap.size) {
            fails += 1u;
        }
        checks += 1u;
        if (big.end <= ap.end) {
            fails += 1u;
        }
    }
    /* Refusals. */
    checks += 1u;
    if (adreno_va_aperture(15u, kAdrenoVaShapeTtbr0, &ap) == 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_aperture(32u, 2u, &ap) == 0) {
        fails += 1u; /* unknown shape */
    }
    checks += 1u;
    if (adreno_va_aperture(32u, kAdrenoVaShapeTtbr0, 0) == 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_geometry(32u, kAdrenoVaShapeTtbr0, 0, &e) == 0) {
        fails += 1u;
    }
    /* TTBR1 with the clamp: start is huge so it passes through unchanged. */
    checks += 1u;
    if (adreno_va_aperture(40u, kAdrenoVaShapeTtbr1, &ap) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (ap.start != (~0ull << 40) || ap.end != ~0ull) {
        fails += 1u;
    }

    /* ---- the GMU/GPU conflation trap (P3:1283) ------------------------ */
    checks += 1u;
    if (adreno_va_gmu_end_is_gpu_end(kAdrenoVaGmuApertureEnd) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_gmu_end_is_gpu_end(0x100000000ull) != 0) {
        fails += 1u;
    }
    /* A 32-bit ias happens to produce end == 0xffffffff, NOT 0x80000000, so the
     * two are genuinely different numbers on this board. */
    checks += 1u;
    if (adreno_va_gmu_end_is_gpu_end(0xffffffffull) != 0) {
        fails += 1u;
    }

    /* ---- the three blockers -------------------------------------------- */
    missing = adreno_va_missing();
    checks += 1u;
    if (missing != kAdrenoVaBlockerAll) {
        fails += 1u;
    }
    checks += 1u;
    if (missing != 7u) {
        fails += 1u;
    }
    {
        const uint32_t bits[3] = {kAdrenoVaBlockerId2Unread,
                                  kAdrenoVaBlockerNoSmmu,
                                  kAdrenoVaBlockerNoWalker};
        uint32_t seen = 0u;
        for (i = 0u; i < 3u; ++i) {
            checks += 1u;
            if ((seen & bits[i]) != 0u) {
                fails += 1u;
            }
            seen |= bits[i];
            checks += 1u;
            if (adreno_va_blocker_name(bits[i])[0] == '\0' ||
                adreno_va_blocker_anchor(bits[i])[0] == '\0') {
                fails += 1u;
            }
            checks += 1u;
            if (adreno_va_blocker_name(bits[i])[0] == 'u' &&
                adreno_va_blocker_name(bits[i])[1] == 'n') {
                fails += 1u;
            }
        }
        checks += 1u;
        if (seen != kAdrenoVaBlockerAll) {
            fails += 1u;
        }
    }
    checks += 1u;
    if (adreno_va_blocker_name(0u)[0] != 'u') {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_blocker_anchor(0u)[0] != 'u') {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_map_ready() != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_va_map_ready() != (adreno_va_missing() == 0u ? 1 : 0)) {
        fails += 1u;
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return fails == 0u ? 0 : 1;
}
