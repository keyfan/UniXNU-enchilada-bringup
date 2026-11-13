// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoSmmuAsidV1 host test: 8 groups, CHECK() accumulates failures.
//
// Every expected value is re-derived here from the pinned constants rather than
// copied from the module, so a module that changed a constant and its expectation
// together still fails.
#include <cstdint>
#include <cstdio>
#include "AdrenoSmmuAsidV1.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testCeilings()
{
    /* arm-smmu.h: ARM_SMMU_ID1_NUMCB is GENMASK(7,0) -> an 8-bit count, so at most
     * 256; and upstream's own bitmap is clamped to ARM_SMMU_MAX_CBS = 128. */
    CHECK(kAdrenoAsidId1NumcbMask == 0xffu);
    CHECK(kAdrenoAsidId1MaxBanks == 256u);
    CHECK(kAdrenoAsidId1NumcbMask == (kAdrenoAsidId1MaxBanks - 1u));
    CHECK(kAdrenoAsidMaxCbs == 128u);
    /* The clamp is the SMALLER of the two, and that is what binds. */
    CHECK(kAdrenoAsidMaxCbs < kAdrenoAsidId1MaxBanks);
    /* TTBRn_ASID is GENMASK_ULL(63,48): 16 bits. */
    CHECK(kAdrenoAsidFieldBits == 16u);
    CHECK(kAdrenoAsidFieldMask == 0xffffull);
    CHECK((1ull << kAdrenoAsidFieldBits) - 1ull == kAdrenoAsidFieldMask);
    /* The storage is exactly the clamp, so no legal index is unreachable. */
    CHECK(kAdrenoAsidBitmapWords * 64u == kAdrenoAsidMaxCbs);
    /* An 8-bit bank index fits both the S2CR.CBNDX field and the low byte. */
    CHECK(kAdrenoAsidLow8Mask == 0xffu);
}

static void testCapacity()
{
    /* ID1.NUMCB is a raw count, NOT an id_size encoding: there is no
     * size-to-bits conversion here, unlike the ID2 path. */
    CHECK(adreno_asid_capacity_from_id1(0x00000010u) == 16u);
    CHECK(adreno_asid_capacity_from_id1(0x00000001u) == 1u);
    CHECK(adreno_asid_capacity_from_id1(0x00000080u) == 128u);
    /* 255 is CLAMPED to 128, not honoured: the allocator being mirrored is the
     * clamped one. */
    CHECK(adreno_asid_capacity_from_id1(0x000000ffu) == kAdrenoAsidMaxCbs);
    /* Zero banks is a broken read, not an empty SMMU. */
    CHECK(adreno_asid_capacity_from_id1(0u) == 0u);
    CHECK(adreno_asid_capacity_from_id1(0xffffff00u) == 0u);
    /* Only the low 8 bits are the count; the rest of ID1 is other fields. */
    CHECK(adreno_asid_capacity_from_id1(0xffffff10u) == 16u);
    CHECK(adreno_asid_capacity_from_id1(0x0000ff80u) == 128u);
    /* Bit 31 is ID1.PAGESIZE and must not leak into the count. */
    CHECK(adreno_asid_capacity_from_id1(0x80000005u) == 5u);
}

static void testInitRefusals()
{
    struct adreno_asid_bitmap b{};
    CHECK(adreno_asid_init(nullptr, 16u, 0u) == 1);
    CHECK(adreno_asid_init(&b, 0u, 0u) == 1);       /* no banks           */
    CHECK(adreno_asid_init(&b, 129u, 0u) == 1);     /* above the clamp    */
    CHECK(adreno_asid_init(&b, 16u, 16u) == 1);     /* inverted range     */
    CHECK(adreno_asid_init(&b, 8u, 16u) == 1);      /* start beyond end   */
    /* The refusals must leave the struct ZEROED, not half-initialised, or a caller
     * that ignores the return value allocates from garbage. */
    CHECK(b.capacity == 0u);
    CHECK(b.start == 0u);
    CHECK(b.in_use == 0u);
    CHECK(b.word[0] == 0ull && b.word[1] == 0ull);
}

static void testFirstFit()
{
    struct adreno_asid_bitmap b{};
    uint32_t cb = 0xffffffffu;

    /* start = 2 models P2:734: the first num_s2_context_banks are stage-2 only. */
    CHECK(adreno_asid_init(&b, 8u, 2u) == 0);
    CHECK(b.capacity == 8u);
    CHECK(b.start == 2u);
    CHECK(b.in_use == 0u);

    /* The first allocation is `start`, NOT 0. */
    CHECK(adreno_asid_alloc(&b, &cb) == 0 && cb == 2u);
    CHECK(adreno_asid_alloc(&b, &cb) == 0 && cb == 3u);
    CHECK(adreno_asid_alloc(&b, &cb) == 0 && cb == 4u);
    CHECK(b.in_use == 3u);

    /* The reserve is never owned. */
    CHECK(adreno_asid_is_owned(&b, 0u) == 0);
    CHECK(adreno_asid_is_owned(&b, 1u) == 0);
    CHECK(adreno_asid_is_owned(&b, 2u) == 1);

    /* FREE THE LOWER ONE AND ALLOCATE AGAIN: first-fit must return 2 even though 5
     * is also free. A rotating allocator returns 5 and silently diverges from
     * __arm_smmu_alloc_bitmap(). */
    CHECK(adreno_asid_free(&b, 2u) == 0);
    CHECK(adreno_asid_alloc(&b, &cb) == 0 && cb == 2u);
    /* And after freeing 3 it comes back before 5 too, proving the scan restarts at
     * `start` every call rather than continuing from a cursor. */
    CHECK(adreno_asid_free(&b, 3u) == 0);
    CHECK(adreno_asid_alloc(&b, &cb) == 0 && cb == 3u);
    CHECK(b.in_use == 3u);
}

static void testExhaustion()
{
    struct adreno_asid_bitmap b{};
    uint32_t cb = 0xffffffffu;
    uint32_t i;

    CHECK(adreno_asid_init(&b, 3u, 0u) == 0);
    for (i = 0u; i < 3u; ++i) {
        CHECK(adreno_asid_alloc(&b, &cb) == 0);
        CHECK(cb == i);
    }
    /* The -ENOSPC shape: refuse AND clear the out-parameter, so a caller cannot read
     * a stale index as an allocation. */
    cb = 0x12345678u;
    CHECK(adreno_asid_alloc(&b, &cb) == 1);
    CHECK(cb == 0xffffffffu);
    CHECK(b.in_use == 3u);
    CHECK(adreno_asid_count(&b) == 3u);

    /* Free one: the next allocation succeeds and is that one. */
    CHECK(adreno_asid_free(&b, 1u) == 0);
    CHECK(adreno_asid_alloc(&b, &cb) == 0 && cb == 1u);
    CHECK(adreno_asid_count(&b) == 3u);

    /* A NULL out-parameter is refused, not dereferenced. */
    CHECK(adreno_asid_alloc(&b, nullptr) == 1);
    CHECK(adreno_asid_alloc(nullptr, &cb) == 1);
}

static void testFreeRefusals()
{
    struct adreno_asid_bitmap b{};
    uint32_t cb = 0xffffffffu;

    CHECK(adreno_asid_init(&b, 3u, 0u) == 0);
    CHECK(adreno_asid_alloc(&b, &cb) == 0 && cb == 0u);
    CHECK(adreno_asid_free(&b, 0u) == 0);
    /* A double free is refused rather than underflowing the count. */
    CHECK(adreno_asid_free(&b, 0u) == 1);
    CHECK(b.in_use == 0u);
    /* Out of range is refused. */
    CHECK(adreno_asid_free(&b, 3u) == 1);
    CHECK(adreno_asid_free(&b, 0xffffffffu) == 1);
    CHECK(adreno_asid_free(nullptr, 0u) == 1);

    /* A bank below `start` is not ours to free even if it were somehow set. */
    CHECK(adreno_asid_init(&b, 8u, 4u) == 0);
    CHECK(adreno_asid_free(&b, 2u) == 1);
    CHECK(adreno_asid_free(&b, 4u) == 1);   /* clear, so refused */
    CHECK(adreno_asid_alloc(&b, &cb) == 0 && cb == 4u);
    CHECK(adreno_asid_free(&b, 4u) == 0);
}

static void testTuple()
{
    /* P2:795 asid = cbndx. No offset, no transform, no rotation. */
    CHECK(adreno_asid_ttbr_asid_field(0u) == 0ull);
    CHECK(adreno_asid_ttbr_asid_field(5u) == 5ull);
    CHECK(adreno_asid_ttbr_asid_field(0x7fu) == 0x7full);
    /* The value lands in TTBRn bits 63..48, so a writer shifts by 48; the field
     * itself is the low 16 bits. */
    CHECK((adreno_asid_ttbr_asid_field(5u) << 48) == 0x0005000000000000ull);

    /* CONTEXTIDR carries the ASID low byte; on the AArch64 path upstream does not
     * write this register at all, but the value must still be right. */
    CHECK(adreno_asid_contextidr(0u) == 0u);
    CHECK(adreno_asid_contextidr(0x2au) == 0x2au);
    CHECK(adreno_asid_contextidr(0x1ffu) == 0xffu);  /* masked to the byte */

    /* S2CR.CBNDX is GENMASK(7,0): a wider index is REFUSED, not truncated. */
    CHECK(adreno_asid_s2cr_cbndx(0u) == 0u);
    CHECK(adreno_asid_s2cr_cbndx(0x7fu) == 0x7fu);
    CHECK(adreno_asid_s2cr_cbndx(0x100u) == 0xffffffffu);
    CHECK(adreno_asid_s2cr_cbndx(0xffffffffu) == 0xffffffffu);

    /* GR1_CBAR(n) = 0x0 + (n << 2). */
    CHECK(adreno_asid_cbar_offset(0u) == 0u);
    CHECK(adreno_asid_cbar_offset(1u) == 4u);
    CHECK(adreno_asid_cbar_offset(2u) == 8u);
    CHECK(adreno_asid_cbar_offset(0x1f4u) == 0x1f4u * 4u);
    CHECK(adreno_asid_cbar_offset(1u) - adreno_asid_cbar_offset(0u) == 4u);
}

static void testMissingAndVerify()
{
    /* Both inputs are absent and that is the honest state. */
    CHECK(adreno_asid_missing() == kAdrenoAsidInputAll);
    CHECK(kAdrenoAsidInputAll == 3u);
    CHECK(kAdrenoAsidInputId1 == 1u);
    CHECK(kAdrenoAsidInputCaller == 2u);
    /* Readiness is DERIVED and is 0. */
    CHECK(adreno_asid_ready() == 0);
    CHECK(adreno_asid_ready() != (adreno_asid_missing() == 0u ? 0 : 1));

    /* Names and anchors are total, non-empty and distinct. */
    const uint32_t bits[2] = {kAdrenoAsidInputId1, kAdrenoAsidInputCaller};
    for (uint32_t i = 0; i < 2u; ++i) {
        const char *n = adreno_asid_input_name(bits[i]);
        const char *a = adreno_asid_input_anchor(bits[i]);
        CHECK(n != nullptr && n[0] != '\0');
        CHECK(a != nullptr && a[0] != '\0');
        CHECK(!(n[0] == 'u' && n[1] == 'n'));
        for (uint32_t j = 0; j < i; ++j) {
            const char *m = adreno_asid_input_name(bits[j]);
            bool same = true;
            uint32_t k = 0;
            while (n[k] || m[k]) { if (n[k] != m[k]) { same = false; break; } ++k; }
            CHECK(!same);
        }
    }
    CHECK(adreno_asid_input_name(0u)[0] == 'u');
    CHECK(adreno_asid_input_anchor(0u)[0] == 'u');

    uint32_t count = 0;
    CHECK(adreno_asid_verify(&count) == 0);
    /* Pin the EXACT count so a DELETED assertion cannot pass silently: the second
     * CHECK then fails if someone removes a check from the self-verifier. */
    CHECK(count == 52u);
    CHECK(count >= 52u);
    CHECK(adreno_asid_verify(nullptr) == 0);
}

int main()
{
    testCeilings();
    testCapacity();
    testInitRefusals();
    testFirstFit();
    testExhaustion();
    testFreeRefusals();
    testTuple();
    testMissingAndVerify();
    if (failures) {
        std::printf("FAIL: enchilada A630 SMMU ASID allocator contract "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 SMMU ASID allocator contract "
                "(8 test groups, 0 failures)\n");
    return 0;
}
