// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoCpRingV1 host test: 11 groups, CHECK() accumulates failures.
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
    /* Round 72 closed the LAST two (U3/U4): NOTHING is open any more, so the
     * ring is programmable AND a packet is encodable. The historical five-bit
     * question is still reported in full. */
    CHECK(adreno_cp_ring_undefined_open() == 0u);
    CHECK(adreno_cp_ring_undefined() == kAdrenoCpRingUndefAll);
    CHECK(adreno_cp_ring_undefined() == 31u);
    CHECK(adreno_cp_ring_can_encode_packet() == 1);
    CHECK(adreno_cp_ring_is_executable() == 1);
    /* "executable" is a statement about COMPUTABILITY, not about hardware. */
    CHECK(adreno_cp_ring_is_executable() ==
          (adreno_cp_ring_undefined_open() == 0u ? 1 : 0));
}


static void testParityAndHeader()
{
    /* adreno_gpu.h:637-643 PM4_PARITY, folded 4 bits at a time through 0x9669. */
    /* 0x9669 bit 0 is SET, so parity(0) is 1 -- a popcount would say 0. This
     * single case is what distinguishes a fold through the LUT from a sum. */
    CHECK(adreno_cp_ring_parity(0u) == 1u);
    CHECK(adreno_cp_ring_parity(8u) == 0u);
    CHECK(adreno_cp_ring_parity(0x48u) == 1u);
    CHECK(adreno_cp_ring_parity(0x7fu) == 0u);
    CHECK(adreno_cp_ring_parity(0xffffffffu) == 1u);
    CHECK(adreno_cp_ring_parity(1u) == 0u);
    CHECK(adreno_cp_ring_parity(3u) == 1u);
    /* adreno_pm4.xml:234 CP_TYPE7_PKT = 0x70000000. */
    CHECK(kAdrenoCpRingType7Pkt == 0x70000000u);
    /* adreno_pm4.xml:248 CP_ME_INIT = 0x48. */
    CHECK(kAdrenoCpRingMeInitOpcode == 0x48u);
    /* OUT_PKT7(ring, CP_ME_INIT, 8) -> 0x70C80008. */
    CHECK(adreno_cp_ring_type7_header(0x48u, 8u) == 0x70c80008u);
    CHECK(kAdrenoCpRingMeInitHeader == 0x70c80008u);
    CHECK((adreno_cp_ring_type7_header(0x48u, 8u) & 0x70000000u) ==
          kAdrenoCpRingType7Pkt);
    CHECK(((adreno_cp_ring_type7_header(0x48u, 8u) >> 16) & 0x7fu) == 0x48u);
    CHECK(((adreno_cp_ring_type7_header(0x48u, 8u) >> 15) & 1u) == 0u);
    CHECK(((adreno_cp_ring_type7_header(0x48u, 8u) >> 23) & 1u) == 1u);
    CHECK((adreno_cp_ring_type7_header(0x48u, 8u) & 0x7fffu) == 8u);
    /* The opcode is masked to 7 bits, not truncated into the parity bit. */
    CHECK(((adreno_cp_ring_type7_header(0xc8u, 1u) >> 16) & 0x7fu) == 0x48u);
    CHECK(adreno_cp_ring_type7_header(0x48u, 8u) !=
          adreno_cp_ring_type7_header(0x48u, 7u));
}

static void testMessage()
{
    uint32_t msg[9];
    uint32_t count = 99;
    CHECK(adreno_cp_ring_me_init_message(msg, 9u, &count) == 0);
    CHECK(count == 9u);
    /* header first, then the eight payload dwords -- that ordering is the point. */
    CHECK(msg[0] == 0x70c80008u);
    CHECK(msg[1] == 0x0000002fu);
    CHECK(msg[2] == 0x00000003u);
    CHECK(msg[3] == 0x20000000u);
    for (uint32_t i = 4; i < 9u; ++i) CHECK(msg[i] == 0u);
    CHECK(adreno_cp_ring_me_init_message_matches(msg, 9u) == 1);
    /* The payload alone is NOT the message. */
    CHECK(adreno_cp_ring_me_init_message_matches(msg, 8u) == 0);
    CHECK(adreno_cp_ring_me_init_message_matches(msg + 1, 8u) == 0);
    /* A header-less vector is refused. */
    uint32_t bad[9];
    for (uint32_t i = 0; i < 9u; ++i) bad[i] = msg[i];
    bad[0] = 0x70000000u;
    CHECK(adreno_cp_ring_me_init_message_matches(bad, 9u) == 0);
    bad[0] = msg[0];
    bad[8] = 1u;
    CHECK(adreno_cp_ring_me_init_message_matches(bad, 9u) == 0);
    /* Refusals leave *count at 0. */
    count = 99;
    CHECK(adreno_cp_ring_me_init_message(msg, 8u, &count) == 1);
    CHECK(count == 0u);
    CHECK(adreno_cp_ring_me_init_message(nullptr, 9u, &count) == 1);
    CHECK(adreno_cp_ring_me_init_message(msg, 9u, nullptr) == 1);
}

static void testGeometry()
{
    /* sizes.h:26 SZ_32K = 0x8000; msm_gpu.h:311 the ring is exactly that. */
    CHECK(adreno_cp_ring_buffer_bytes() == 0x8000u);
    CHECK(adreno_cp_ring_buffer_dwords() == 0x2000u);
    CHECK(adreno_cp_ring_buffer_bytes() == adreno_cp_ring_buffer_dwords() * 4u);
    CHECK(kAdrenoCpRingBufferBytes == 0x8000u);
    CHECK(kAdrenoCpRingBlockBytes == 32u);
    CHECK(adreno_cp_ring_buffer_bytes() > kAdrenoCpRingBlockBytes);
    /* msm_gpu.h:315-316: the two ilog2() inputs to the RB_CNTL formula. */
    uint32_t bufsz = 0, blksz = 0;
    CHECK(adreno_cp_ring_rb_cntl_fields(&bufsz, &blksz) == 0);
    CHECK(bufsz == 12u);
    CHECK(blksz == 2u);
    CHECK((1u << bufsz) == adreno_cp_ring_buffer_bytes() / 8u);
    CHECK((1u << blksz) == kAdrenoCpRingBlockBytes / 8u);
    /* The two fields are distinct, so conflating them is detectable. */
    CHECK(bufsz != blksz);
    CHECK(adreno_cp_ring_rb_cntl_fields(nullptr, &blksz) == 1);
    CHECK(adreno_cp_ring_rb_cntl_fields(&bufsz, nullptr) == 1);
}


static void testRbCntl()
{
    /* adreno_common.xml:116-123 field layout, fed by msm_gpu.h:314-316. */
    CHECK(kAdrenoCpRingRbCntlBufszLow == 0u);
    CHECK(kAdrenoCpRingRbCntlBufszHigh == 5u);
    CHECK(kAdrenoCpRingRbCntlBlkszLow == 8u);
    CHECK(kAdrenoCpRingRbCntlBlkszHigh == 13u);
    CHECK(kAdrenoCpRingRbCntlNoUpdatePos == 27u);
    /* BUFSZ=12 -> 0xC ; BLKSZ=2 -> 0x200 ; together 0x20C. */
    CHECK(adreno_cp_ring_pack_rb_cntl(12u, 2u, 0u) == 0x0000020cu);
    CHECK(adreno_cp_ring_pack_rb_cntl(12u, 2u, 1u) == 0x0800020cu);
    CHECK(kAdrenoCpRingRbCntlDefault == 0x0000020cu);
    CHECK(kAdrenoCpRingRbCntlNoUpdate == 0x0800020cu);
    /* The A630 branch (no extended APRIV) is the NO_UPDATE one. */
    uint32_t a630 = 0, apriv = 0;
    CHECK(adreno_cp_ring_rb_cntl_value(0, &a630) == 0);
    CHECK(adreno_cp_ring_rb_cntl_value(1, &apriv) == 0);
    CHECK(a630 == 0x0800020cu);
    CHECK(apriv == 0x0000020cu);
    CHECK((a630 ^ apriv) == (1u << 27));   /* differ EXACTLY in NO_UPDATE */
    /* Field round-trip proves the placement rather than restating it. */
    CHECK(adreno_cp_ring_unpack_field(a630, 0u, 5u) == 12u);
    CHECK(adreno_cp_ring_unpack_field(a630, 8u, 13u) == 2u);
    CHECK(adreno_cp_ring_unpack_field(a630, 27u, 27u) == 1u);
    CHECK(adreno_cp_ring_unpack_field(a630, 16u, 17u) == 0u);  /* BUF_SWAP */
    CHECK(adreno_cp_ring_unpack_field(a630, 20u, 20u) == 0u);  /* POLL_EN */
    CHECK(adreno_cp_ring_unpack_field(a630, 31u, 31u) == 0u);  /* RPTR_WR_EN */
    /* Packing then unpacking is the identity on the fields we set. */
    uint32_t rt = adreno_cp_ring_pack_rb_cntl(
        adreno_cp_ring_unpack_field(a630, 0u, 5u),
        adreno_cp_ring_unpack_field(a630, 8u, 13u), 1u);
    CHECK(rt == a630);
    /* Refusals. */
    CHECK(adreno_cp_ring_rb_cntl_value(0, nullptr) == 1);
    CHECK(adreno_cp_ring_unpack_field(a630, 5u, 1u) == 0u);
    CHECK(adreno_cp_ring_unpack_field(a630, 0u, 32u) == 0u);
    /* A wrong BUFSZ is detectable, i.e. the field is really 6 bits wide. */
    CHECK(adreno_cp_ring_pack_rb_cntl(64u, 2u, 0u) ==
          adreno_cp_ring_pack_rb_cntl(0u, 2u, 0u));
}

static void testSelfCheck()
{
    uint32_t count = 0;
    CHECK(adreno_cp_ring_verify(&count) == 0);
    CHECK(count >= 75u);
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
    testParityAndHeader();
    testMessage();
    testGeometry();
    testRbCntl();
    testSelfCheck();
    if (failures) {
        std::printf("FAIL: enchilada A630 CP ring init/doorbell contract "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 CP ring init/doorbell contract "
                "(11 test groups, 0 failures)\n");
    return 0;
}
