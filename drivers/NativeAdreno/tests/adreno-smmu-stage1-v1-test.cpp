// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoSmmuStage1V1 host test: 7 groups, CHECK() accumulates failures.
#include <cstdint>
#include <cstdio>
#include "AdrenoSmmuStage1V1.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testOffsets()
{
    /* arm-smmu.h:145-198, byte offsets inside the context-bank page. */
    CHECK(kAdrenoSmmuCbSctlr == 0x00u);
    CHECK(kAdrenoSmmuCbActlr == 0x04u);
    CHECK(kAdrenoSmmuCbTcr2 == 0x10u);
    CHECK(kAdrenoSmmuCbTtbr0 == 0x20u);
    CHECK(kAdrenoSmmuCbTtbr1 == 0x28u);
    CHECK(kAdrenoSmmuCbTcr == 0x30u);
    CHECK(kAdrenoSmmuCbContextidr == 0x34u);
    CHECK(kAdrenoSmmuCbS1Mair0 == 0x38u);
    CHECK(kAdrenoSmmuCbS1Mair1 == 0x3cu);
    CHECK(kAdrenoSmmuCbFsr == 0x58u);
    /* They are 4-byte spaced where they are adjacent. */
    CHECK(kAdrenoSmmuCbTtbr1 == kAdrenoSmmuCbTtbr0 + 8u);   /* 64-bit pair */
    CHECK(kAdrenoSmmuCbContextidr == kAdrenoSmmuCbTcr + 4u);
    CHECK(kAdrenoSmmuCbS1Mair1 == kAdrenoSmmuCbS1Mair0 + 4u);
    CHECK(kAdrenoSmmuCbFsr > kAdrenoSmmuCbContextidr);
}

static void testSctlr()
{
    /* Bits from arm-smmu.h:146-154. */
    CHECK(kAdrenoSmmuSctlrM == 0x1u);
    CHECK(kAdrenoSmmuSctlrTre == 0x2u);
    CHECK(kAdrenoSmmuSctlrAfe == 0x4u);
    CHECK(kAdrenoSmmuSctlrE == 0x10u);
    CHECK(kAdrenoSmmuSctlrCfre == 0x20u);
    CHECK(kAdrenoSmmuSctlrCfie == 0x40u);
    CHECK(kAdrenoSmmuSctlrCfcfg == 0x80u);
    CHECK(kAdrenoSmmuSctlrHupcf == 0x100u);
    CHECK(kAdrenoSmmuSctlrS1Asidpne == 0x1000u);
    /* arm-smmu.c:642-645: CFIE|CFRE|AFE|TRE|M (+S1_ASIDPNE for stage1). */
    CHECK(adreno_smmu_sctlr_stage1() == 0x00001067u);
    /* The big-endian variant adds EXACTLY bit 4. */
    CHECK(adreno_smmu_sctlr_stage1_be() == 0x00001077u);
    CHECK((adreno_smmu_sctlr_stage1_be() ^ adreno_smmu_sctlr_stage1()) ==
          kAdrenoSmmuSctlrE);
    /* M is the translation-enable bit and it IS set in the default. */
    CHECK(adreno_smmu_sctlr_enabled(adreno_smmu_sctlr_stage1()) == 1);
    CHECK(adreno_smmu_sctlr_enabled(0u) == 0);
    CHECK(adreno_smmu_sctlr_enabled(kAdrenoSmmuSctlrM) == 1);
    /* The little-endian default must NOT set E. */
    CHECK((adreno_smmu_sctlr_stage1() & kAdrenoSmmuSctlrE) == 0u);
    /* And it is not the bare M bit -- the fault-reporting bits are there too. */
    CHECK(adreno_smmu_sctlr_stage1() != kAdrenoSmmuSctlrM);
    CHECK((adreno_smmu_sctlr_stage1() & kAdrenoSmmuSctlrCfie) != 0u);
    CHECK((adreno_smmu_sctlr_stage1() & kAdrenoSmmuSctlrCfcfg) == 0u); /* not set */
}

static void testS2cr()
{
    /* GR0_S2CR(n) = 0xc00 + (n << 2). */
    CHECK(adreno_smmu_s2cr_addr(0u) == 0x0c00u);
    CHECK(adreno_smmu_s2cr_addr(1u) == 0x0c04u);
    CHECK(adreno_smmu_s2cr_addr(2u) == 0x0c08u);
    CHECK(adreno_smmu_s2cr_addr(0x1acu & 0xffu) == 0x0c00u + 0xacu * 4u);
    /* SID is masked: no wild SID escapes the 256-entry range. */
    CHECK(adreno_smmu_s2cr_addr(0x100u) == adreno_smmu_s2cr_addr(0u));
    CHECK(adreno_smmu_s2cr_addr(0x1ffu) == adreno_smmu_s2cr_addr(0xffu));
    /* TYPE at bits 17..16: TRANS 0, BYPASS 1, FAULT 2. */
    CHECK(kAdrenoSmmuS2crTypeTrans == 0);
    CHECK(kAdrenoSmmuS2crTypeBypass == 1);
    CHECK(kAdrenoSmmuS2crTypeFault == 2);
    CHECK(adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeTrans, 0u, 0u, 0u) == 0u);
    CHECK(adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeBypass, 0u, 0u, 0u) ==
          0x00010000u);
    CHECK(adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeFault, 0u, 0u, 0u) ==
          0x00020000u);
    /* CBNDX in the low 8 bits. */
    CHECK(adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeTrans, 0x2au, 0u, 0u) == 0x2au);
    /* PRIVCFG at 25..24, EXIDVALID at bit 10 -- all independent. */
    CHECK(adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeTrans, 0u, 3u, 0u) == 0x03000000u);
    CHECK(adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeTrans, 0u, 0u, 1u) == 0x00000400u);
    CHECK(adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeBypass, 5u, 1u, 1u) ==
          (0x01000000u | 0x00010000u | 0x00000400u | 5u));
    /* Round 53's measured precondition: BYPASS, not TRANS. */
    CHECK(adreno_smmu_s2cr_type_for_state(0) == kAdrenoSmmuS2crTypeBypass);
    CHECK(adreno_smmu_s2cr_type_for_state(1) == kAdrenoSmmuS2crTypeTrans);
}

static void testPlanOrder()
{
    struct adreno_smmu_cb_write plan[8];
    uint32_t count = 0;
    CHECK(adreno_smmu_stage1_plan(0u, 7u, 0xdeadbeefcafe0000ull, 0x1234u,
                                  0xffu, 0x00ff00ffu, plan, 8u, &count) == 0);
    CHECK(count == 6u);
    /* The upstream order, and the ONE ordering rule that matters: SCTLR last. */
    CHECK(plan[0].offset == kAdrenoSmmuCbS1Mair0);
    CHECK(plan[1].offset == kAdrenoSmmuCbS1Mair1);
    CHECK(plan[2].offset == kAdrenoSmmuCbTtbr0);
    CHECK(plan[3].offset == kAdrenoSmmuCbTcr);
    CHECK(plan[4].offset == kAdrenoSmmuCbContextidr);
    CHECK(plan[5].offset == kAdrenoSmmuCbSctlr);
    /* Translation is enabled only after the table and format are in place. */
    CHECK(plan[count - 1u].offset == kAdrenoSmmuCbSctlr);
    CHECK(plan[count - 1u].value == 0x00001067u);
    /* TTBR0 is the only 64-bit write. */
    CHECK(plan[2].is_64bit == 1u);
    uint32_t wide = 0;
    for (uint32_t i = 0; i < count; ++i) if (plan[i].is_64bit) ++wide;
    CHECK(wide == 1u);
    /* The 64-bit value is split: the low half lands in .value. */
    CHECK(plan[2].value == 0xcafe0000u);
    /* CONTEXTIDR carries the low 8 bits of the ASID. */
    CHECK(plan[4].value == 7u);
    /* order is 0-based and ascending. */
    for (uint32_t i = 0; i < count; ++i) CHECK(plan[i].order == i);
    /* Refusals. */
    struct adreno_smmu_cb_write tiny[5];
    count = 99;
    CHECK(adreno_smmu_stage1_plan(0u, 1u, 0u, 0u, 0u, 0u, tiny, 5u, &count) == 1);
    CHECK(count == 0u);
    CHECK(adreno_smmu_stage1_plan(0u, 1u, 0u, 0u, 0u, 0u, nullptr, 8u, &count) == 1);
    CHECK(adreno_smmu_stage1_plan(0u, 1u, 0u, 0u, 0u, 0u, plan, 8u, nullptr) == 1);
}

static void testMissingInputs()
{
    /* ALL FOUR are missing, and that is the honest state. */
    CHECK(adreno_smmu_stage1_missing() == kAdrenoSmmuInputAll);
    CHECK(adreno_smmu_stage1_missing() == 15u);
    CHECK(kAdrenoSmmuInputPageTable == 1u);
    CHECK(kAdrenoSmmuInputAsid == 2u);
    CHECK(kAdrenoSmmuInputStreamBinding == 4u);
    CHECK(kAdrenoSmmuInputCxPowered == 8u);
    /* readiness is DERIVED and is 0. */
    CHECK(adreno_smmu_stage1_ready() == 0);
    CHECK(adreno_smmu_stage1_ready() !=
          (adreno_smmu_stage1_missing() == 0u ? 0 : 1));
    /* Names and anchors are total and distinct. */
    const uint32_t bits[4] = {kAdrenoSmmuInputPageTable, kAdrenoSmmuInputAsid,
                              kAdrenoSmmuInputStreamBinding,
                              kAdrenoSmmuInputCxPowered};
    for (uint32_t i = 0; i < 4u; ++i) {
        const char *n = adreno_smmu_stage1_input_name(bits[i]);
        const char *a = adreno_smmu_stage1_input_anchor(bits[i]);
        CHECK(n != nullptr && n[0] != '\0');
        CHECK(a != nullptr && a[0] != '\0');
        CHECK(!(n[0] == 'u' && n[1] == 'n'));
        for (uint32_t j = 0; j < i; ++j) {
            const char *m = adreno_smmu_stage1_input_name(bits[j]);
            bool same = true;
            uint32_t k = 0;
            while (n[k] || m[k]) { if (n[k] != m[k]) { same = false; break; } ++k; }
            CHECK(!same);
        }
    }
    CHECK(adreno_smmu_stage1_input_name(0u)[0] == 'u');
    CHECK(adreno_smmu_stage1_input_anchor(0u)[0] == 'u');
    /* The 8-bit ASID ceiling is why I2 cannot be faked with a counter. */
    CHECK((kAdrenoSmmuS2crCbndxMask & 0xffu) == 0xffu);
}

static void testSelfCheck()
{
    uint32_t count = 0;
    CHECK(adreno_smmu_stage1_verify(&count) == 0);
    CHECK(count >= 55u);
    CHECK(adreno_smmu_stage1_verify(nullptr) == 0);
}

int main()
{
    testOffsets();
    testSctlr();
    testS2cr();
    testPlanOrder();
    testMissingInputs();
    testSelfCheck();
    if (failures) {
        std::printf("FAIL: enchilada A630 SMMU stage-1 context word model "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 SMMU stage-1 context word model "
                "(6 test groups, 0 failures)\n");
    return 0;
}
