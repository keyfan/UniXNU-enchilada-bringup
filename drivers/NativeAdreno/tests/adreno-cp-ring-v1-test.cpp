// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoCpRingV1 host test: 7 groups, CHECK() accumulates failures.
#include <cstdint>
#include <cstdio>
#include "AdrenoCpRingV1.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testOffsets()
{
    uint32_t rb_base = 0, rb_cntl = 0, rptr_addr = 0, rptr = 0, wptr = 0;
    uint32_t sqe_cntl = 0, sqe_instr = 0;
    CHECK(adreno_cp_ring_offsets(&rb_base, &rb_cntl, &rptr_addr, &rptr, &wptr,
                                 &sqe_cntl, &sqe_instr) == 0);
    /* a6xx.xml:2281-2286, :2294 -- WORD offsets. */
    CHECK(rb_base == 0x0800u);
    CHECK(rb_cntl == 0x0802u);
    CHECK(rptr_addr == 0x0804u);
    CHECK(rptr == 0x0806u);
    CHECK(wptr == 0x0807u);
    CHECK(sqe_cntl == 0x0808u);
    CHECK(sqe_instr == 0x0830u);
    /* A byte-offset misreading would space these by 4. */
    CHECK(rptr_addr == rb_base + 4u);
    CHECK(wptr == rptr + 1u);
    CHECK(sqe_instr != rb_base * 4u);
    /* Refusals. */
    CHECK(adreno_cp_ring_offsets(nullptr, &rb_cntl, &rptr_addr, &rptr, &wptr,
                                 &sqe_cntl, &sqe_instr) == 1);
    CHECK(adreno_cp_ring_offsets(&rb_base, &rb_cntl, &rptr_addr, &rptr, &wptr,
                                 &sqe_cntl, nullptr) == 1);
}

static void testPlan()
{
    struct adreno_cp_ring_step_desc ops[kAdrenoCpRingStepCount];
    uint32_t count = 0;
    CHECK(adreno_cp_ring_plan(1u, ops, kAdrenoCpRingStepCount, &count) == 0);
    CHECK(count == 6u);
    CHECK(kAdrenoCpRingStepCount == 6u);
    /* Order is the pinned order: SQE instr base, RB base, RB_CNTL, RPTR_ADDR,
     * SQE enable, ME_INIT. */
    CHECK(ops[0].step == kAdrenoCpRingStepSqeInstrBase);
    CHECK(ops[0].reg_offset == 0x0830u);
    CHECK(ops[0].is_iova == 1u);
    CHECK(ops[1].step == kAdrenoCpRingStepRbBase);
    CHECK(ops[1].reg_offset == 0x0800u);
    CHECK(ops[1].is_iova == 1u);
    CHECK(ops[2].step == kAdrenoCpRingStepRbCntl);
    CHECK(ops[2].is_iova == 0u);
    CHECK(ops[3].step == kAdrenoCpRingStepRptrAddr);
    CHECK(ops[3].conditional == 1u);
    CHECK(ops[4].step == kAdrenoCpRingStepSqeEnable);
    CHECK(ops[4].reg_offset == 0x0808u);
    CHECK(ops[5].step == kAdrenoCpRingStepMeInit);
    CHECK(ops[5].reg_offset == 0u);  /* packet-only: no register write */
    /* Exactly three steps carry a 64-bit IOVA and exactly one is conditional. */
    uint32_t iovas = 0, conds = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (ops[i].is_iova) ++iovas;
        if (ops[i].conditional) ++conds;
    }
    CHECK(iovas == 3u);
    CHECK(conds == 1u);
}

static void testPlanWithoutShadow()
{
    struct adreno_cp_ring_step_desc ops[kAdrenoCpRingStepCount];
    uint32_t count = 99;
    CHECK(adreno_cp_ring_plan(0u, ops, kAdrenoCpRingStepCount, &count) == 0);
    CHECK(count == 5u);
    /* No conditional step survives. */
    for (uint32_t i = 0; i < count; ++i) {
        CHECK(ops[i].conditional == 0u);
        CHECK(ops[i].step != kAdrenoCpRingStepRptrAddr);
    }
    /* The last two are still SQE enable then ME_INIT. */
    CHECK(ops[count - 2u].step == kAdrenoCpRingStepSqeEnable);
    CHECK(ops[count - 1u].step == kAdrenoCpRingStepMeInit);
    /* Refusals leave *count at 0. */
    count = 99;
    CHECK(adreno_cp_ring_plan(0u, ops, 4u, &count) == 1);
    CHECK(count == 0u);
    CHECK(adreno_cp_ring_plan(1u, nullptr, 6u, &count) == 1);
}

static void testMeInit()
{
    uint32_t d[8];
    uint32_t count = 0;
    CHECK(adreno_cp_ring_me_init(d, kAdrenoCpRingMeInitDwords, &count) == 0);
    CHECK(count == 8u);
    CHECK(kAdrenoCpRingMeInitDwords == 8u);
    /* a6xx_gpu.c:618/:621/:624 then five zeros. */
    CHECK(d[0] == 0x0000002fu);
    CHECK(d[1] == 0x00000003u);   /* multiple hardware contexts */
    CHECK(d[2] == 0x20000000u);   /* error detection */
    CHECK(d[3] == 0u);            /* header dump off */
    CHECK(d[4] == 0u);
    CHECK(d[5] == 0u);            /* no workarounds */
    CHECK(d[6] == 0u);
    CHECK(d[7] == 0u);            /* pad */
    CHECK(adreno_cp_ring_me_init_matches(d, 8u) == 1);
    /* The A7xx shape is refused: wrong length... */
    CHECK(adreno_cp_ring_me_init_matches(d, 7u) == 0);
    CHECK(kAdrenoCpRingA7xxMeInitDwords == 7u);
    /* ...and wrong mask word (A7xx writes a computed mask at index 1). */
    uint32_t wrong[8];
    for (uint32_t i = 0; i < 8u; ++i) wrong[i] = d[i];
    wrong[1] = 0x00000007u;
    CHECK(adreno_cp_ring_me_init_matches(wrong, 8u) == 0);
    wrong[1] = d[1];
    wrong[2] = 0x00000000u;  /* drop error detection */
    CHECK(adreno_cp_ring_me_init_matches(wrong, 8u) == 0);
    /* Any non-zero pad dword is refused. */
    wrong[2] = d[2];
    wrong[7] = 1u;
    CHECK(adreno_cp_ring_me_init_matches(wrong, 8u) == 0);
    CHECK(adreno_cp_ring_me_init_matches(nullptr, 8u) == 0);
    /* Short capacity refuses and leaves *count at 0. */
    uint32_t c = 99;
    CHECK(adreno_cp_ring_me_init(d, 7u, &c) == 1);
    CHECK(c == 0u);
}

static void testShadowAndDoorbell()
{
    /* a6xx_gpu.c:1127-1131: no extended APRIV -> NO_UPDATE + WHERE_AM_I. */
    CHECK(adreno_cp_ring_shadow_mode(0) == kAdrenoCpRingShadowWhereAmI);
    CHECK(adreno_cp_ring_shadow_mode(1) == kAdrenoCpRingShadowHwApriv);
    CHECK(adreno_cp_ring_shadow_mode(0) != adreno_cp_ring_shadow_mode(1));
    /* a6xx_gpu.c:87/89: barrier strictly before the single WPTR store. */
    uint32_t barrier = 0, store = 0;
    CHECK(adreno_cp_ring_doorbell_order(&barrier, &store) == 0);
    CHECK(barrier == 1u);
    CHECK(store == 1u);
    CHECK(adreno_cp_ring_doorbell_order(nullptr, &store) == 1);
    CHECK(adreno_cp_ring_doorbell_order(&barrier, nullptr) == 1);
    /* Protect encoding (a6xx_gpu.h:59-69). */
    CHECK(kAdrenoCpRingProtectNoRdwrBit == 31u);
    CHECK(kAdrenoCpRingProtectLenShift == 18u);
    CHECK(kAdrenoCpRingProtectLenMask == 0x3fffu);
    CHECK(kAdrenoCpRingProtectAddrMask == 0x3ffffu);
}

static void testUndefinedSet()
{
    /* The five constants the pinned sources never define. */
    CHECK(adreno_cp_ring_undefined() == kAdrenoCpRingUndefAll);
    CHECK(kAdrenoCpRingUndefAll == 31u);
    const uint32_t bits[5] = {kAdrenoCpRingUndefMeInitOpcode,
                              kAdrenoCpRingUndefPacketHeader,
                              kAdrenoCpRingUndefRbCntlDefault,
                              kAdrenoCpRingUndefRbCntlNoUpdate,
                              kAdrenoCpRingUndefRingSize};
    uint32_t seen = 0;
    for (uint32_t i = 0; i < 5u; ++i) {
        CHECK((seen & bits[i]) == 0u);      /* distinct bit */
        seen |= bits[i];
        CHECK((adreno_cp_ring_undefined() & bits[i]) != 0u);
        const char *n = adreno_cp_ring_undefined_name(bits[i]);
        const char *a = adreno_cp_ring_undefined_anchor(bits[i]);
        CHECK(n != nullptr && n[0] != '\0');
        CHECK(a != nullptr && a[0] != '\0');
        CHECK(!(n[0] == 'u' && n[1] == 'n'));  /* not the sentinel */
        /* No two bits may share a name. */
        for (uint32_t j = 0; j < i; ++j) {
            const char *m = adreno_cp_ring_undefined_name(bits[j]);
            bool same = true;
            uint32_t k = 0;
            while (n[k] || m[k]) {
                if (n[k] != m[k]) { same = false; break; }
                ++k;
            }
            CHECK(!same);
        }
    }
    CHECK(seen == kAdrenoCpRingUndefAll);
    CHECK(adreno_cp_ring_undefined_name(0u)[0] == 'u');
    CHECK(adreno_cp_ring_undefined_anchor(0u)[0] == 'u');
    CHECK(adreno_cp_ring_undefined_name(kAdrenoCpRingUndefAll)[0] == 'u');
    /* The decisive one: an unencodable packet means NOT executable. */
    CHECK(adreno_cp_ring_is_executable() == 0);
}

static void testSelfCheck()
{
    uint32_t count = 0;
    CHECK(adreno_cp_ring_verify(&count) == 0);
    CHECK(count >= 40u);
    CHECK(adreno_cp_ring_verify(nullptr) == 0);
}

int main()
{
    testOffsets();
    testPlan();
    testPlanWithoutShadow();
    testMeInit();
    testShadowAndDoorbell();
    testUndefinedSet();
    testSelfCheck();
    if (failures) {
        std::printf("FAIL: enchilada A630 CP ring init/doorbell contract "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 CP ring init/doorbell contract "
                "(7 test groups, 0 failures)\n");
    return 0;
}
