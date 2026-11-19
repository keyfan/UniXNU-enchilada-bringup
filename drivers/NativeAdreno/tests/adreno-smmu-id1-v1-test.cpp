// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for AdrenoSmmuId1V1 -- the ID1 decode that supplies num_context_banks.
//
// What this test can and cannot prove is the whole point. It CAN prove that the
// decode matches the pinned field definition and that a zero count is refused
// rather than clamped. It CANNOT prove what the phone reports, because nothing has
// read GR0_ID1 on hardware yet. Keeping those apart is why the module's header
// carries a modelled / NOT-known split.

#include <cstdio>
#include <cstring>

#include "../AdrenoSmmuId1V1.h"

static unsigned g_checks = 0;
static unsigned g_fails = 0;

static void check(bool cond, const char *label)
{
    ++g_checks;
    if (!cond) {
        ++g_fails;
        std::printf("  FAIL %s\n", label);
    }
}

int main(void)
{
    // ---- the pinned constants --------------------------------------------
    check(kAdrenoSmmuGr0Id1 == 0x24u, "GR0_ID1 is 0x24 (P1:59)");
    check(kAdrenoSmmuId1NumcbMask == 0xffu, "NUMCB is GENMASK(7,0) (P1:63)");

    // ---- the field decode, both edges and the ignored upper bits ---------
    check(adreno_smmu_id1_numcb(0x00000000u) == 0u, "0 decodes to 0");
    check(adreno_smmu_id1_numcb(0x00000001u) == 1u, "1 decodes to 1");
    check(adreno_smmu_id1_numcb(0x000000ffu) == 255u, "0xff decodes to 255");
    check(adreno_smmu_id1_numcb(0xffffff00u) == 0u, "upper bits are not NUMCB");
    check(adreno_smmu_id1_numcb(0x00007511u) == 0x11u,
          "an ID2-shaped word yields only its low byte");

    // ---- zero is refused, and the refusal is named -----------------------
    struct adreno_smmu_id1 d;
    std::memset(&d, 0, sizeof d);
    check(adreno_smmu_id1_decode(0x00000000u, &d) == 0u, "zero decodes (returns 0)");
    check(d.num_context_banks == 0u, "zero gives no banks");
    check(d.plausible == 0u, "zero is implausible");
    check(d.reason != nullptr && d.reason[0] != 0, "zero names a reason");
    check(std::strstr(d.reason, "no context bank") != nullptr,
          "the reason says why, not just that");

    // ---- a real word is plausible ---------------------------------------
    std::memset(&d, 0, sizeof d);
    check(adreno_smmu_id1_decode(0x00000030u, &d) == 0u, "0x30 decodes");
    check(d.num_context_banks == 48u, "0x30 is 48 banks");
    check(d.plausible == 1u, "48 banks is plausible");
    check(d.reason != nullptr && d.reason[0] == 0, "a good value carries no reason");
    check(d.raw == 0x00000030u, "raw is preserved");

    // ---- capacity is derived from the count, never a constant -----------
    check(adreno_smmu_id1_asid_capacity(48u) == 48u, "capacity follows the count");
    check(adreno_smmu_id1_asid_capacity(1u) == 1u, "capacity of 1 is 1");
    check(adreno_smmu_id1_asid_capacity(d.num_context_banks) == d.num_context_banks,
          "capacity of a decoded value equals that value");

    // ---- the field's own limit ------------------------------------------
    check(adreno_smmu_id1_plausible_max() == 255u, "eight bits express at most 255");

    // ---- a null out-pointer is refused, not dereferenced ----------------
    check(adreno_smmu_id1_decode(0x00000030u, nullptr) == 1u, "null out is refused");

    // ---- the module's own self-check ------------------------------------
    unsigned int checks = 0;
    check(adreno_smmu_id1_verify(&checks) == 0, "module self-check passes");
    check(checks >= 16u, "self-check covers the constants and both edges");
    check(adreno_smmu_id1_verify(nullptr) == 0, "self-check tolerates a null counter");

    std::printf("%u/%u checks passed\n", g_checks - g_fails, g_checks);
    if (g_fails != 0) {
        std::printf("FAIL: AdrenoSmmuId1V1 (%u failures)\n", g_fails);
        return 1;
    }
    std::printf("PASS: AdrenoSmmuId1V1 ID1 decode (the value itself is still unread)\n");
    return 0;
}
