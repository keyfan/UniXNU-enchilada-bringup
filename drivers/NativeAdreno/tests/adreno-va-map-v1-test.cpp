// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoVaMapV1 host test: 7 groups, CHECK() accumulates failures.
#include <cstdint>
#include <cstdio>
#include "AdrenoVaMapV1.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testClamp()
{
    /* adreno_gpu.c:224 start = max_t(u64, SZ_16M, geometry->aperture_start). */
    CHECK(kAdrenoVaFloorBytes == 0x01000000ull);
    CHECK(adreno_va_clamp_start(0u) == 0x01000000ull);
    CHECK(adreno_va_clamp_start(0x1000u) == 0x01000000ull);
    CHECK(adreno_va_clamp_start(0x00ffffffull) == 0x01000000ull);  /* just under */
    CHECK(adreno_va_clamp_start(0x01000000ull) == 0x01000000ull);  /* exactly */
    CHECK(adreno_va_clamp_start(0x01000001ull) == 0x01000001ull);  /* just over */
    CHECK(adreno_va_clamp_start(0x40000000ull) == 0x40000000ull);
    CHECK(adreno_va_clamp_start(~0ull) == ~0ull);
}

static void testIas()
{
    /* The RANGE is architectural; the VALUE is read from ID2.UBS. */
    CHECK(adreno_va_ias_is_plausible(0u) == 0);
    CHECK(adreno_va_ias_is_plausible(15u) == 0);
    CHECK(adreno_va_ias_is_plausible(16u) == 1);
    CHECK(adreno_va_ias_is_plausible(32u) == 1);
    CHECK(adreno_va_ias_is_plausible(40u) == 1);
    CHECK(adreno_va_ias_is_plausible(48u) == 1);
    CHECK(adreno_va_ias_is_plausible(49u) == 0);
    CHECK(adreno_va_ias_is_plausible(64u) == 0);
}

static void testGeometry()
{
    uint64_t s = 1, e = 1;
    /* arm-smmu.c:828 aperture_end = (1UL << ias) - 1. */
    CHECK(adreno_va_geometry(32u, kAdrenoVaShapeTtbr0, &s, &e) == 0);
    CHECK(s == 0u);
    CHECK(e == 0xffffffffull);
    CHECK(adreno_va_geometry(40u, kAdrenoVaShapeTtbr0, &s, &e) == 0);
    CHECK(e == 0xffffffffffull);
    CHECK(adreno_va_geometry(48u, kAdrenoVaShapeTtbr0, &s, &e) == 0);
    CHECK(e == 0xffffffffffffull);
    /* arm-smmu.c:825-826 the TTBR1 shape. */
    CHECK(adreno_va_geometry(40u, kAdrenoVaShapeTtbr1, &s, &e) == 0);
    CHECK(s == (~0ull << 40));
    CHECK(e == ~0ull);
    /* The two shapes are genuinely different. */
    uint64_t s0, e0, s1, e1;
    CHECK(adreno_va_geometry(40u, kAdrenoVaShapeTtbr0, &s0, &e0) == 0);
    CHECK(adreno_va_geometry(40u, kAdrenoVaShapeTtbr1, &s1, &e1) == 0);
    CHECK(s0 != s1);
    CHECK(e0 != e1);
    /* Refusals. */
    CHECK(adreno_va_geometry(15u, kAdrenoVaShapeTtbr0, &s, &e) == 1);
    CHECK(adreno_va_geometry(32u, 2u, &s, &e) == 1);
    CHECK(adreno_va_geometry(32u, kAdrenoVaShapeTtbr0, nullptr, &e) == 1);
    CHECK(adreno_va_geometry(32u, kAdrenoVaShapeTtbr0, &s, nullptr) == 1);
}

static void testAperture()
{
    struct adreno_va_aperture ap;
    /* The composed GPU space for a 32-bit ias: 16 MiB .. 0xffffffff. */
    CHECK(adreno_va_aperture(32u, kAdrenoVaShapeTtbr0, &ap) == 0);
    CHECK(ap.start == 0x01000000ull);
    CHECK(ap.end == 0xffffffffull);
    CHECK(ap.size == ap.end - ap.start + 1u);
    CHECK(ap.ias == 32u);
    CHECK(ap.shape == kAdrenoVaShapeTtbr0);
    /* Bigger ias => strictly bigger space and end. */
    struct adreno_va_aperture big;
    CHECK(adreno_va_aperture(40u, kAdrenoVaShapeTtbr0, &big) == 0);
    CHECK(big.size > ap.size);
    CHECK(big.end > ap.end);
    CHECK(big.start == ap.start);   /* the floor is the same either way */
    /* TTBR1 with a huge start passes through the clamp unchanged. */
    CHECK(adreno_va_aperture(40u, kAdrenoVaShapeTtbr1, &ap) == 0);
    CHECK(ap.start == (~0ull << 40));
    CHECK(ap.end == ~0ull);
    CHECK(ap.size == (~0ull - (~0ull << 40) + 1u));
    /* Refusals. */
    CHECK(adreno_va_aperture(15u, kAdrenoVaShapeTtbr0, &ap) == 1);
    CHECK(adreno_va_aperture(32u, 9u, &ap) == 1);
    CHECK(adreno_va_aperture(32u, kAdrenoVaShapeTtbr0, nullptr) == 1);
}

static void testGmuTrap()
{
    /* a6xx_gmu.c:1283 -- the GMU's OWN space ends at 0x80000000; the GPU's does
     * not. This API exists so a caller cannot substitute one for the other. */
    CHECK(kAdrenoVaGmuApertureEnd == 0x80000000ull);
    CHECK(adreno_va_gmu_end_is_gpu_end(0x80000000ull) == 1);
    CHECK(adreno_va_gmu_end_is_gpu_end(0xffffffffull) == 0);
    CHECK(adreno_va_gmu_end_is_gpu_end(0x100000000ull) == 0);
    /* A 32-bit GPU aperture really does end elsewhere. */
    struct adreno_va_aperture ap;
    CHECK(adreno_va_aperture(32u, kAdrenoVaShapeTtbr0, &ap) == 0);
    CHECK(ap.end != kAdrenoVaGmuApertureEnd);
}

static void testBlockers()
{
    /* All THREE blockers are present, and the derivation therefore cannot end. */
    CHECK(adreno_va_missing() == kAdrenoVaBlockerAll);
    CHECK(adreno_va_missing() == 7u);
    CHECK(kAdrenoVaBlockerId2Unread == 1u);
    CHECK(kAdrenoVaBlockerNoSmmu == 2u);
    CHECK(kAdrenoVaBlockerNoWalker == 4u);
    CHECK(adreno_va_map_ready() == 0);
    CHECK(adreno_va_map_ready() !=
          (adreno_va_missing() == 0u ? 0 : 1));
    const uint32_t bits[3] = {kAdrenoVaBlockerId2Unread, kAdrenoVaBlockerNoSmmu,
                              kAdrenoVaBlockerNoWalker};
    for (uint32_t i = 0; i < 3u; ++i) {
        const char *n = adreno_va_blocker_name(bits[i]);
        const char *a = adreno_va_blocker_anchor(bits[i]);
        CHECK(n != nullptr && n[0] != '\0');
        CHECK(a != nullptr && a[0] != '\0');
        CHECK(!(n[0] == 'u' && n[1] == 'n'));
        for (uint32_t j = 0; j < i; ++j) {
            const char *m = adreno_va_blocker_name(bits[j]);
            bool same = true;
            uint32_t k = 0;
            while (n[k] || m[k]) { if (n[k] != m[k]) { same = false; break; } ++k; }
            CHECK(!same);
        }
    }
    CHECK(adreno_va_blocker_name(0u)[0] == 'u');
    CHECK(adreno_va_blocker_anchor(0u)[0] == 'u');
}

static void testSelfCheck()
{
    uint32_t count = 0;
    CHECK(adreno_va_verify(&count) == 0);
    CHECK(count >= 45u);
    CHECK(adreno_va_verify(nullptr) == 0);
}

int main()
{
    testClamp();
    testIas();
    testGeometry();
    testAperture();
    testGmuTrap();
    testBlockers();
    testSelfCheck();
    if (failures) {
        std::printf("FAIL: enchilada A630 GPU VA aperture derivation model "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 GPU VA aperture derivation model "
                "(7 test groups, 0 failures)\n");
    return 0;
}
