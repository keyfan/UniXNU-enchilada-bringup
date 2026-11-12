// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoSmmuCtxWordV1 host test: 9 groups, CHECK() accumulates failures.
//
// Every expected value below is RECOMPUTED here from the pinned constants rather
// than copied from the module, so a module that changed a constant and its
// expectation together would still fail.
#include <cstdint>
#include <cstdio>
#include "AdrenoSmmuCtxWordV1.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// ---- P1 arm-smmu.h:145-154 SCTLR bits, re-derived from BIT(n) -------------
static uint32_t bit(unsigned n) { return 1u << n; }

static void testIdSize()
{
    /* arm-smmu.c:1727-1744, including the DEFAULT arm: 5 AND everything else
     * collapse to 48. A table-only encoding would invent widths. */
    CHECK(adreno_smmu_ctx_id_size_to_bits(0u) == 32u);
    CHECK(adreno_smmu_ctx_id_size_to_bits(1u) == 36u);
    CHECK(adreno_smmu_ctx_id_size_to_bits(2u) == 40u);
    CHECK(adreno_smmu_ctx_id_size_to_bits(3u) == 42u);
    CHECK(adreno_smmu_ctx_id_size_to_bits(4u) == 44u);
    CHECK(adreno_smmu_ctx_id_size_to_bits(5u) == 48u);
    /* The collapse. */
    CHECK(adreno_smmu_ctx_id_size_to_bits(6u) == 48u);
    CHECK(adreno_smmu_ctx_id_size_to_bits(7u) == 48u);
    CHECK(adreno_smmu_ctx_id_size_to_bits(0xeu) == 48u);
    CHECK(adreno_smmu_ctx_id_size_to_bits(0xfu) == 48u);
    CHECK(adreno_smmu_ctx_id_size_to_bits(0xffffffffu) == 48u);
    CHECK(kAdrenoSmmuIdSizeMax == 0xfu);
}

static void testMair()
{
    /* io-pgtable-arm.c:123-130 the four bytes and indices; :1002-1009 the
     * composition reg = sum(attr[idx] << (idx << 3)). Re-derived here. */
    const uint32_t idx_nc = 0u, idx_cache = 1u, idx_dev = 2u, idx_ocache = 3u;
    const uint32_t a_nc = 0x44u, a_cache = 0xffu, a_dev = 0x04u, a_oc = 0xf4u;
    const uint64_t want = ((uint64_t)a_nc << (idx_nc << 3)) |
                          ((uint64_t)a_cache << (idx_cache << 3)) |
                          ((uint64_t)a_dev << (idx_dev << 3)) |
                          ((uint64_t)a_oc << (idx_ocache << 3));
    CHECK(want == 0x00000000f404ff44ull);
    CHECK(adreno_smmu_ctx_mair() == want);
    /* The constants are the pinned ones. */
    CHECK(kAdrenoSmmuMairAttrNc == a_nc);
    CHECK(kAdrenoSmmuMairAttrWbrwa == a_cache);
    CHECK(kAdrenoSmmuMairAttrDevice == a_dev);
    CHECK(kAdrenoSmmuMairAttrIncOwbrwa == a_oc);
    /* arm-smmu.c:560-561 the split: MAIR0 = low, MAIR1 = >> 32. */
    CHECK(adreno_smmu_ctx_mair0() == (uint32_t)(want & 0xffffffffull));
    CHECK(adreno_smmu_ctx_mair1() == (uint32_t)(want >> 32));
    /* Every attribute is in the low half on this board, so MAIR1 is 0. */
    CHECK(adreno_smmu_ctx_mair1() == 0u);
    CHECK((adreno_smmu_ctx_mair0() & 0xffu) == a_nc);            /* idx 0 -> 7..0  */
    CHECK(((adreno_smmu_ctx_mair0() >> 8) & 0xffu) == a_cache);  /* idx 1 -> 15..8 */
    CHECK(((adreno_smmu_ctx_mair0() >> 16) & 0xffu) == a_dev);   /* idx 2 -> 23..16*/
    CHECK(((adreno_smmu_ctx_mair0() >> 24) & 0xffu) == a_oc);    /* idx 3 -> 31..24*/
    /* The two MAIRs partition the register with no bit in both. */
    CHECK(((uint64_t)adreno_smmu_ctx_mair0() &
           (uint64_t)adreno_smmu_ctx_mair1()) == 0ull);
}

static void testTcr()
{
    int ok = 0;
    /* arm-smmu.h:393-412 + io-pgtable-arm.c:944-999.
     * Rebuild the TTBR0-geometry word from the individual fields. */
    const uint32_t tg0_4k = 0u;
    const uint32_t sh_is = 3u, sh_os = 2u;
    const uint32_t rgn_wbwa = 1u, rgn_nc = 0u;
    const uint32_t eae = bit(31), epd1 = bit(23), a1 = bit(22);
    const uint32_t epd0 = bit(7);

    auto want_tcr = [&](uint32_t ias, bool coherent, bool ttbr1) {
        uint32_t tg = ttbr1 ? 2u : tg0_4k;              /* TG1_4K == 2 */
        uint32_t sh = coherent ? sh_is : sh_os;
        uint32_t rgn = coherent ? rgn_wbwa : rgn_nc;
        uint32_t t = (tg << 14) | (sh << 12) | (rgn << 10) | (rgn << 8) |
                     ((64u - ias) & 0x3fu);
        if (ttbr1) t = ((t << 16) & ~a1) | epd0;
        else       t = t | epd1;
        return t;
    };

    /* The coherent TTBR0 case, at every legal ias. */
    const uint32_t iases[6] = {32u, 36u, 40u, 42u, 44u, 48u};
    for (uint32_t i = 0; i < 6u; ++i) {
        ok = 0;
        CHECK(adreno_smmu_ctx_tcr(iases[i], 1u, 0u, &ok) ==
              want_tcr(iases[i], true, false));
        CHECK(ok == 1);
        ok = 0;
        CHECK(adreno_smmu_ctx_tcr(iases[i], 0u, 0u, &ok) ==
              want_tcr(iases[i], false, false));
        CHECK(ok == 1);
        ok = 0;
        CHECK(adreno_smmu_ctx_tcr(iases[i], 1u, 1u, &ok) ==
              want_tcr(iases[i], true, true));
        CHECK(ok == 1);
        ok = 0;
        CHECK(adreno_smmu_ctx_tcr(iases[i], 0u, 1u, &ok) ==
              want_tcr(iases[i], false, true));
        CHECK(ok == 1);
    }
    /* The one concrete value this round cites. */
    CHECK(want_tcr(40u, true, false) == 0x00803518u);
    CHECK(adreno_smmu_ctx_tcr(40u, 1u, 0u, nullptr) == 0x00803518u);

    /* T0SZ is 64-ias and is the ONLY field that depends on ias. */
    {
        uint32_t prev = 0;
        for (uint32_t i = 0; i < 6u; ++i) {
            uint32_t t = want_tcr(iases[i], true, false);
            CHECK((t & 0x3fu) == ((64u - iases[i]) & 0x3fu));
            if (i) CHECK((t & ~0x3fu) == (prev & ~0x3fu));
            prev = t;
        }
    }
    /* The EPD pair is EXCLUSIVE: a context that disabled both would translate
     * nothing, which would look like a working bank that faults on every access. */
    for (uint32_t t1 = 0; t1 < 2u; ++t1) {
        uint32_t t = want_tcr(40u, true, t1 != 0);
        if (t1) { CHECK((t & epd0) != 0u); CHECK((t & epd1) == 0u); }
        else    { CHECK((t & epd1) != 0u); CHECK((t & epd0) == 0u); }
    }
    /* The TTBR1 geometry shifts into the high half and clears A1. */
    {
        uint32_t t = want_tcr(40u, true, true);
        CHECK((t & a1) == 0u);
        CHECK(want_tcr(40u, true, true) == 0xb5180080u);
        /* TG1==2 lands in bits 31..30 after the <<16 shift, so bit 31 is set.
         * That bit is ALSO named EAE in the header (P1:172). The collision is a
         * property of the architecture, not of this module: upstream performs the
         * same shift at P1:405-406 and never clears the result. Recorded so a
         * reader does not mistake the set bit for a big-endian context. */
        CHECK((t & 0x80000000u) != 0u);
        CHECK(((t >> 30) & 0x3u) == 2u);   /* TG1 == ARM_LPAE_TCR_TG1_4K */
    }
    /* EAE is never set by the TTBR0 path: that geometry has no field at bit 31. */
    CHECK((adreno_smmu_ctx_tcr(40u, 1u, 0u, &ok) & eae) == 0u);
    /* A1 is cleared by the TTBR1 path and untouched (clear) by the TTBR0 one. */
    CHECK((adreno_smmu_ctx_tcr(40u, 1u, 0u, &ok) & a1) == 0u);

    /* Refusals, each with ok cleared so a caller cannot read a stale success. */
    ok = 99; CHECK(adreno_smmu_ctx_tcr(15u, 1u, 0u, &ok) == 0u && ok == 0);
    ok = 99; CHECK(adreno_smmu_ctx_tcr(0u, 1u, 0u, &ok) == 0u && ok == 0);
    ok = 99; CHECK(adreno_smmu_ctx_tcr(49u, 1u, 0u, &ok) == 0u && ok == 0);
    ok = 99; CHECK(adreno_smmu_ctx_tcr(64u, 1u, 0u, &ok) == 0u && ok == 0);
    ok = 99; CHECK(adreno_smmu_ctx_tcr(40u, 2u, 0u, &ok) == 0u && ok == 0);
    ok = 99; CHECK(adreno_smmu_ctx_tcr(40u, 1u, 2u, &ok) == 0u && ok == 0);
}

static void testTcr2()
{
    int ok = 0;
    /* arm-smmu.h:414-418: PASIZE | SEP(0x7) | AS for AArch64 (arm-smmu.c:525-527). */
    const uint32_t sep_upstream = 0x7u << 15;
    const uint32_t as_bit = bit(4);
    const uint32_t oass[6] = {32u, 36u, 40u, 42u, 44u, 48u};
    for (uint32_t i = 0; i < 6u; ++i) {
        ok = 0;
        CHECK(adreno_smmu_ctx_tcr2(oass[i], 1u, &ok) ==
              (i | sep_upstream | as_bit));
        CHECK(ok == 1);
        ok = 0;
        CHECK(adreno_smmu_ctx_tcr2(oass[i], 0u, &ok) == (i | sep_upstream));
        CHECK(ok == 1);
        /* AS is the ONLY difference between the two contexts. */
        CHECK((adreno_smmu_ctx_tcr2(oass[i], 1u, &ok) ^
               adreno_smmu_ctx_tcr2(oass[i], 0u, &ok)) == as_bit);
    }
    CHECK(adreno_smmu_ctx_tcr2(36u, 1u, nullptr) == 0x00038011u);
    /* Without AS: SEP(0x7 << 15) | PASIZE(36 -> 1) = 0x00038001. Writing
     * 0x00038010 here would move PASIZE into bit 4, where AS lives. */
    CHECK(adreno_smmu_ctx_tcr2(36u, 0u, nullptr) == 0x00038001u);
    CHECK((adreno_smmu_ctx_tcr2(36u, 0u, &ok) & 0xfu) == 1u);
    /* SEP is upstream's 0x7 in EVERY case -- never 0, never configurable. */
    for (uint32_t i = 0; i < 6u; ++i)
        CHECK((adreno_smmu_ctx_tcr2(oass[i], 1u, &ok) & 0x00038000u) == sep_upstream);
    /* 52 is refused: a 4-bit IDR read cannot produce it (arm-smmu.c:1727-1744). */
    ok = 99; CHECK(adreno_smmu_ctx_tcr2(52u, 1u, &ok) == 0u && ok == 0);
    ok = 99; CHECK(adreno_smmu_ctx_tcr2(33u, 1u, &ok) == 0u && ok == 0);
    ok = 99; CHECK(adreno_smmu_ctx_tcr2(0u, 1u, &ok) == 0u && ok == 0);
    ok = 99; CHECK(adreno_smmu_ctx_tcr2(64u, 1u, &ok) == 0u && ok == 0);
}

static void testCbarCba2r()
{
    /* arm-smmu.c:594-612 CBAR for a stage-1 domain (P2:733 TYPE=1):
     * S1_MEMATTR=WB(0xf) at 15..12, S1_BPSHCFG=NSH(3) at 9..8, TYPE at 17..16,
     * and an 8-bit VMID at 7..0 ONLY when 16-bit VMIDs are absent. */
    const uint32_t want_base = (1u << 16) | (0xfu << 12) | (3u << 8);
    CHECK(want_base == 0x0001f300u);
    CHECK(adreno_smmu_ctx_cbar(0u, 0u) == want_base);
    CHECK(adreno_smmu_ctx_cbar(0u, 0x3fu) == (want_base | 0x3fu));
    /* With 16-bit VMIDs the low byte stays EMPTY (P2:608-611). */
    CHECK(adreno_smmu_ctx_cbar(1u, 0x3fu) == want_base);
    CHECK(adreno_smmu_ctx_cbar(1u, 0xffffu) == want_base);
    /* TYPE is exactly 1 and nothing else lands in that field. */
    CHECK((adreno_smmu_ctx_cbar(0u, 0xffu) & 0x00030000u) == 0x00010000u);
    CHECK(kAdrenoSmmuCbarTypeS1TransS2Bypass == 1u);
    CHECK(kAdrenoSmmuCbarTypeS2Trans == 0u);
    CHECK(kAdrenoSmmuCbarTypeS1TransS2Fault == 2u);
    CHECK(kAdrenoSmmuCbarTypeS1TransS2Trans == 3u);

    /* arm-smmu.c:581-592 CBA2R: VA64 for AArch64, VMID16 at 31..16. */
    CHECK(adreno_smmu_ctx_cba2r(1u, 0u, 0u) == 0x1u);
    CHECK(adreno_smmu_ctx_cba2r(0u, 0u, 0u) == 0u);
    CHECK(adreno_smmu_ctx_cba2r(1u, 1u, 0x1234u) == (0x1u | 0x12340000u));
    CHECK(adreno_smmu_ctx_cba2r(0u, 1u, 0x1234u) == 0x12340000u);
    /* An 8-bit VMID must not leak here, and a 16-bit one must not appear twice. */
    CHECK((adreno_smmu_ctx_cba2r(1u, 0u, 0xffffu) & 0xffff0000u) == 0u);
    CHECK(adreno_smmu_ctx_cba2r(1u, 1u, 0x1u) == (0x1u | 0x10000u));
}

static void testTtbr()
{
    int ok = 0;
    /* arm-smmu.h:169 TTBRn_ASID = GENMASK_ULL(63,48); arm-smmu.c:540-548 puts
     * the ASID there for BOTH TTBRs. */
    const uint32_t asid_shift = 48u;
    const uint64_t table = 0x877fe000ull;
    const uint32_t asid = 0x12u;
    const uint64_t want = ((uint64_t)asid << asid_shift) | table;
    CHECK(want == 0x00120000877fe000ull);
    CHECK(adreno_smmu_ctx_ttbrn_checked(asid, table, &ok) == want);
    CHECK(ok == 1);
    CHECK(adreno_smmu_ctx_ttbrn(asid, table) == want);
    /* The ASID occupies exactly bits 63..48. */
    {
        uint64_t v = adreno_smmu_ctx_ttbrn(0xffffu, 0u);
        CHECK(v == 0xffff000000000000ull);
        CHECK((uint32_t)((v >> asid_shift) & 0xffffu) == 0xffffu);
        /* The pointer occupies exactly the low 48 bits (it is 4 KiB aligned). */
        uint64_t p = adreno_smmu_ctx_ttbrn(0u, 0xfffffffff000ull);
        CHECK(p == 0xfffffffff000ull);
    }
    /* Round-trip: the pointer survives composition unchanged below bit 48. */
    CHECK((adreno_smmu_ctx_ttbrn(asid, table) & 0x0000ffffffffffffull) == table);
    /* An unaligned root pointer is REFUSED, not rounded down. */
    ok = 99; CHECK(adreno_smmu_ctx_ttbrn_checked(asid, 0x877fe800ull, &ok) == 0ull);
    CHECK(ok == 0);
    ok = 99; CHECK(adreno_smmu_ctx_ttbrn_checked(asid, 0x877fe001ull, &ok) == 0ull);
    CHECK(ok == 0);
    /* An ASID wider than 16 bits is refused, so the shift cannot drop bits. */
    ok = 99; CHECK(adreno_smmu_ctx_ttbrn_checked(0x10000u, table, &ok) == 0ull);
    CHECK(ok == 0);
    /* Both boundaries are accepted. */
    ok = 99; CHECK(adreno_smmu_ctx_ttbrn_checked(0xffffu, 0ull, &ok) ==
                   0xffff000000000000ull && ok == 1);
    ok = 99; CHECK(adreno_smmu_ctx_ttbrn_checked(0u, 0ull, &ok) == 0ull && ok == 1);
}

static void testPlanCompleteness()
{
    /* THE POINT OF THIS ROUND: round 75 emits 6 writes and MISSES 4 registers.
     * Pin both numbers so the gap is measurable, not folklore. */
    CHECK(adreno_smmu_ctx_round75_writes() == 6u);
    CHECK(adreno_smmu_ctx_plan_writes() == 9u);
    CHECK(adreno_smmu_ctx_plan_writes() == adreno_smmu_ctx_round75_writes() + 3u);

    struct adreno_smmu_ctx_params p{};
    p.cbndx = 2u; p.asid = 0x12u; p.ttbr = 0x877fe000ull;
    p.ias = 40u; p.oas = 36u; p.coherent_walk = 1u; p.ttbr1 = 0u;
    p.aarch64 = 1u; p.vmid16 = 0u; p.vmid = 0u;
    struct adreno_smmu_ctx_write plan[12];
    uint32_t count = 0;
    CHECK(adreno_smmu_ctx_plan(&p, plan, 12u, &count) == 0);
    CHECK(count == 9u);

    /* The exact upstream order, arm-smmu.c:581-652, as (gr1, offset) pairs. The
     * GR1 offsets carry the context-bank index: base + (cbndx << 2). */
    const uint32_t gr1_idx = p.cbndx << 2;
    struct { uint32_t gr1; uint32_t off; } const want[9] = {
        {1u, kAdrenoSmmuGr1Cba2r + gr1_idx}, /* 1 CBA2R  P2:581-592 */
        {1u, kAdrenoSmmuGr1Cbar + gr1_idx},  /* 2 CBAR   P2:594-612 */
        {0u, 0x10u},                         /* 3 TCR2   P2:619-620 */
        {0u, 0x30u},                         /* 4 TCR    P2:621     */
        {0u, 0x20u},                         /* 5 TTBR0  P2:629     */
        {0u, 0x28u},                         /* 6 TTBR1  P2:630-632 */
        {0u, 0x38u},                         /* 7 MAIR0  P2:637     */
        {0u, 0x3c},                          /* 8 MAIR1  P2:638     */
        {0u, 0x00u},                         /* 9 SCTLR  P2:641-652 */
    };
    for (uint32_t i = 0; i < 9u; ++i) {
        CHECK(plan[i].order == i);
        CHECK(plan[i].gr1 == want[i].gr1);
        CHECK(plan[i].offset == want[i].off);
    }
    CHECK(plan[0].offset == 0x808u);   /* CBA2R(cbndx=2) */
    CHECK(plan[1].offset == 0x008u);   /* CBAR(cbndx=2)  */
    CHECK(kAdrenoSmmuGr1Cbar == 0x0000u);
    CHECK(kAdrenoSmmuGr1Cba2r == 0x0800u);

    /* Two GR1 writes and seven context-bank writes. Round 75 had ZERO GR1
     * writes because it could not express the distinction at all. */
    uint32_t gr1 = 0, cb = 0;
    for (uint32_t i = 0; i < count; ++i) (plan[i].gr1 ? gr1 : cb)++;
    CHECK(gr1 == 2u);
    CHECK(cb == 7u);

    /* Exactly TWO 64-bit writes: TTBR0 and TTBR1, carrying the SAME ASID. */
    uint32_t wide = 0;
    for (uint32_t i = 0; i < count; ++i) if (plan[i].is_64bit) ++wide;
    CHECK(wide == 2u);
    CHECK(plan[4].is_64bit == 1u && plan[5].is_64bit == 1u);
    CHECK(plan[4].value_hi == plan[5].value_hi);
    CHECK(plan[4].value == plan[5].value);
    /* The ASID sits in TTBR bits 63..48. In a 64-bit store split into two 32-bit
     * halves that field occupies value_hi bits 15..0, i.e. value_hi = asid << 16.
     * For asid 0x12 that is 0x00120000, NOT 0x0012 -- writing the latter would
     * place the ASID four bits too low and is exactly the class of error a
     * recomputed expectation catches. */
    CHECK(plan[4].value_hi == 0x00120000u);
    CHECK(plan[4].value_hi == (p.asid << 16));
    CHECK(((uint32_t)plan[4].value_hi >> 16) == p.asid);
    CHECK(plan[4].value == 0x877fe000u);
    /* And the 64-bit recomposition matches the checked composer exactly. */
    CHECK((((uint64_t)plan[4].value_hi << 32) | plan[4].value) ==
          adreno_smmu_ctx_ttbrn_checked(p.asid, p.ttbr, nullptr));
    /* The recomposition is what the XNU caller would hand to a 64-bit store. */
    CHECK(adreno_smmu_ctx_ttbrn(p.asid, p.ttbr) ==
          ((((uint64_t)plan[4].value_hi) << 32) | (uint64_t)plan[4].value));

    /* SCTLR is LAST and carries the stage-1 default. Its M bit is the ONE bit
     * that turns translation on, so nothing may follow it. */
    CHECK(plan[8].offset == 0x00u && plan[8].gr1 == 0u);
    CHECK(plan[8].value == 0x00001067u);
    CHECK((plan[8].value & 0x1u) != 0u);
    CHECK((plan[8].value & 0x10u) == 0u);      /* E: little-endian host */
    CHECK((plan[8].value & 0x1000u) != 0u);    /* S1_ASIDPNE: stage-1 bank */

    /* The computed values landed in their registers. */
    CHECK(plan[2].value == adreno_smmu_ctx_tcr2(36u, 1u, nullptr));
    CHECK(plan[3].value == adreno_smmu_ctx_tcr(40u, 1u, 0u, nullptr));
    CHECK(plan[6].value == adreno_smmu_ctx_mair0());
    CHECK(plan[7].value == adreno_smmu_ctx_mair1());
    CHECK(plan[0].value == adreno_smmu_ctx_cba2r(1u, 0u, 0u));
    CHECK(plan[1].value == adreno_smmu_ctx_cbar(0u, 0u));

    /* NO CONTEXTIDR write: arm-smmu.c:625 writes it ONLY in the AARCH32_S
     * branch, and this plan is the AArch64 one. Round 75 wrote it unconditionally. */
    uint32_t ctxidr = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!plan[i].gr1 && plan[i].offset == 0x34u) ++ctxidr;
    CHECK(ctxidr == 0u);

    /* The four registers round 75 omitted are ALL present now. */
    CHECK(plan[0].gr1 == 1u);                  /* CBA2R */
    CHECK(plan[1].gr1 == 1u);                  /* CBAR  */
    CHECK(plan[2].offset == 0x10u);            /* TCR2  */
    CHECK(plan[5].offset == 0x28u);            /* TTBR1 */
}

static void testPlanRefusals()
{
    struct adreno_smmu_ctx_params p{};
    p.cbndx = 0u; p.asid = 1u; p.ttbr = 0x1000ull;
    p.ias = 40u; p.oas = 36u; p.coherent_walk = 1u; p.aarch64 = 1u;
    struct adreno_smmu_ctx_write plan[12];
    struct adreno_smmu_ctx_write tiny[8];
    uint32_t count = 99;

    /* A buffer one short of the full sequence is REFUSED, not truncated. This is
     * the whole reason plan_writes() is exposed: 8 < 9 must not silently yield 8. */
    CHECK(adreno_smmu_ctx_plan(&p, tiny, 8u, &count) == 1);
    CHECK(count == 0u);
    CHECK(adreno_smmu_ctx_plan(&p, plan, 8u, &count) == 1);
    /* NULL arguments. */
    CHECK(adreno_smmu_ctx_plan(nullptr, plan, 12u, &count) == 1);
    CHECK(adreno_smmu_ctx_plan(&p, nullptr, 12u, &count) == 1);
    CHECK(adreno_smmu_ctx_plan(&p, plan, 12u, nullptr) == 1);

    /* The AArch32 short-descriptor format is REFUSED, not approximated: it takes
     * its words from arm_v7s_cfg (P2:520-521), a different pinned-unpinned unit. */
    struct adreno_smmu_ctx_params q = p;
    q.aarch64 = 0u;
    CHECK(adreno_smmu_ctx_plan(&q, plan, 12u, &count) == 1);
    /* A bad ias / oas / ttbr propagates as a refusal. */
    q = p; q.ias = 49u;      CHECK(adreno_smmu_ctx_plan(&q, plan, 12u, &count) == 1);
    q = p; q.ias = 15u;      CHECK(adreno_smmu_ctx_plan(&q, plan, 12u, &count) == 1);
    q = p; q.oas = 52u;      CHECK(adreno_smmu_ctx_plan(&q, plan, 12u, &count) == 1);
    q = p; q.oas = 33u;      CHECK(adreno_smmu_ctx_plan(&q, plan, 12u, &count) == 1);
    q = p; q.ttbr = 0x1008ull; CHECK(adreno_smmu_ctx_plan(&q, plan, 12u, &count) == 1);
    q = p; q.coherent_walk = 2u;
    CHECK(adreno_smmu_ctx_plan(&q, plan, 12u, &count) == 1);
    /* A caller who fixes the parameter gets a plan -- the refusal was specific. */
    q = p; q.oas = 32u; q.ias = 48u; q.ttbr1 = 1u;
    CHECK(adreno_smmu_ctx_plan(&q, plan, 12u, &count) == 0);
    CHECK(count == 9u);
}

static void testMissingInputs()
{
    /* All THREE are missing: ias (ID2.UBS), asid (no allocator), page table.
     * Round 75's I3 (stream binding) and I4 (CX powered) are NOT repeated here:
     * they gate whether a context may be PROGRAMMED, not which word to write. */
    CHECK(adreno_smmu_ctx_missing() == kAdrenoSmmuCtxInputAll);
    CHECK(kAdrenoSmmuCtxInputAll == 7u);
    CHECK(kAdrenoSmmuCtxInputIas == 1u);
    CHECK(kAdrenoSmmuCtxInputAsid == 2u);
    CHECK(kAdrenoSmmuCtxInputPageTable == 4u);
    /* Readiness is DERIVED and is 0. */
    CHECK(adreno_smmu_ctx_ready() == 0);
    /* Names and anchors are total, distinct and non-empty. */
    const uint32_t bits[3] = {kAdrenoSmmuCtxInputIas, kAdrenoSmmuCtxInputAsid,
                              kAdrenoSmmuCtxInputPageTable};
    for (uint32_t i = 0; i < 3u; ++i) {
        const char *n = adreno_smmu_ctx_input_name(bits[i]);
        const char *a = adreno_smmu_ctx_input_anchor(bits[i]);
        CHECK(n != nullptr && n[0] != '\0');
        CHECK(a != nullptr && a[0] != '\0');
        CHECK(!(n[0] == 'u' && n[1] == 'n'));
        for (uint32_t j = 0; j < i; ++j) {
            const char *m = adreno_smmu_ctx_input_name(bits[j]);
            bool same = true;
            uint32_t k = 0;
            while (n[k] || m[k]) { if (n[k] != m[k]) { same = false; break; } ++k; }
            CHECK(!same);
        }
    }
    CHECK(adreno_smmu_ctx_input_name(0u)[0] == 'u');
    CHECK(adreno_smmu_ctx_input_anchor(0u)[0] == 'u');
}

static void testVerify()
{
    uint32_t count = 0;
    CHECK(adreno_smmu_ctx_verify(&count) == 0);
    /* Pin the count so a DELETED check cannot pass silently: a verifier that
     * loses assertions still returns 0. Both the exact value and a floor. */
    CHECK(count == 82u);
    CHECK(count >= 82u);
    CHECK(adreno_smmu_ctx_verify(nullptr) == 0);
}

int main()
{
    testIdSize();
    testMair();
    testTcr();
    testTcr2();
    testCbarCba2r();
    testTtbr();
    testPlanCompleteness();
    testPlanRefusals();
    testMissingInputs();
    testVerify();
    if (failures) {
        std::printf("FAIL: enchilada A630 SMMU stage-1 context WORD derivation "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 SMMU stage-1 context WORD derivation "
                "(10 test groups, 0 failures)\n");
    return 0;
}
