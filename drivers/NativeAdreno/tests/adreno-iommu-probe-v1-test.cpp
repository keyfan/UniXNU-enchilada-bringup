// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoIommuProbeV1 host test: 9 groups, CHECK() accumulates failures.
//
// Every expected value below is RECOMPUTED here from the pinned constants, and
// where a word is round 83's, this test CALLS round 83 rather than copying it.
// A test that hard-coded 0xb5180080 without asking adreno_smmu_ctx_tcr() for it
// would still pass if round 83 changed; that is the failure mode this avoids.
#include <cstdint>
#include <cstdio>
#include "AdrenoIommuProbeV1.h"
#include "AdrenoSmmuCtxWordV1.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// The exact number adreno_iommu_verify() evaluates. Pinned so a DELETED
// assertion cannot pass silently: a self-check that loses checks still returns
// 0, so the count is the only evidence the checks still exist.
static const uint32_t kExpectedVerifyChecks = 183u;

// ---- Field extraction, re-derived from the GENMASKs rather than the macros --
static uint32_t field(uint32_t raw, uint32_t hi, uint32_t lo)
{
    uint32_t width = hi - lo + 1u;
    uint32_t mask = (width >= 32u) ? 0xffffffffu : ((1u << width) - 1u);
    return (raw >> lo) & mask;
}

static uint32_t bit(unsigned n) { return 1u << n; }

static void testIdSize()
{
    /* arm-smmu.c:1727-1744, INCLUDING the default arm: 5 and everything else
     * collapse to 48. */
    const uint32_t want[6] = {32u, 36u, 40u, 42u, 44u, 48u};
    for (uint32_t i = 0; i < 6u; ++i)
        CHECK(adreno_iommu_id_size_to_bits(i) == want[i]);
    /* The collapse. A table-only encoding would return an invented width here. */
    for (uint32_t s = 6u; s <= 15u; ++s)
        CHECK(adreno_iommu_id_size_to_bits(s) == 48u);
    CHECK(adreno_iommu_id_size_to_bits(0xffffffffu) == 48u);
    /* 5 is BOTH a case label and part of the default; it must still be 48. */
    CHECK(adreno_iommu_id_size_to_bits(5u) == 48u);
    /* No encoding yields anything outside 32..48. */
    for (uint32_t s = 0u; s <= 15u; ++s) {
        uint32_t w = adreno_iommu_id_size_to_bits(s);
        CHECK(w >= 32u && w <= 48u);
    }
    CHECK(kAdrenoIommuIdSizeMax == 0xfu);
}

static void testRegisters()
{
    /* arm-smmu.h:45/:59/:65 the three ID registers in the GR0 frame, and the
     * fixed 4-byte stride between them. */
    CHECK(kAdrenoIommuGr0Id0 == 0x20u);
    CHECK(kAdrenoIommuGr0Id1 == 0x24u);
    CHECK(kAdrenoIommuGr0Id2 == 0x28u);
    CHECK(kAdrenoIommuGr0Id1 - kAdrenoIommuGr0Id0 == 4u);
    CHECK(kAdrenoIommuGr0Id2 - kAdrenoIommuGr0Id1 == 4u);
    /* The board windows. The two SMMUs are different units; conflating them is
     * the error round 53 was written to prevent. */
    CHECK(kAdrenoIommuKgslSmmuBase == 0x05040000ull);
    CHECK(kAdrenoIommuKgslSmmuSize == 0x00010000ull);
    CHECK(kAdrenoIommuAppsSmmuBase == 0x15000000ull);
    CHECK(kAdrenoIommuKgslSmmuBase != kAdrenoIommuAppsSmmuBase);
    /* ID2 sits inside the kgsl window, at base + 0x28. */
    CHECK(kAdrenoIommuKgslSmmuBase + kAdrenoIommuGr0Id2 == 0x05040028ull);
    CHECK(kAdrenoIommuGr0Id2 < kAdrenoIommuKgslSmmuSize);
}

static void testId2Decode()
{
    /* arm-smmu.h:66-72, rebuilt from the GENMASK bounds. The masks the module
     * declares must be exactly what those bounds produce. */
    CHECK(kAdrenoIommuId2Vmid16 == bit(15));
    CHECK(kAdrenoIommuId2Ptfs64K == bit(14));
    CHECK(kAdrenoIommuId2Ptfs16K == bit(13));
    CHECK(kAdrenoIommuId2Ptfs4K == bit(12));
    CHECK(kAdrenoIommuId2UbsMask == field(0xffffffffu, 11, 8) << 8);
    CHECK(kAdrenoIommuId2OasMask == field(0xffffffffu, 7, 4) << 4);
    CHECK(kAdrenoIommuId2IasMask == 0xfu);
    /* The three fields partition bits 11..0 with no hole and no overlap. */
    CHECK((kAdrenoIommuId2UbsMask | kAdrenoIommuId2OasMask |
           kAdrenoIommuId2IasMask) == 0x00000fffu);
    CHECK((kAdrenoIommuId2UbsMask & kAdrenoIommuId2OasMask) == 0u);
    CHECK((kAdrenoIommuId2OasMask & kAdrenoIommuId2IasMask) == 0u);

    struct adreno_iommu_id2 d{};
    /* A word with a DIFFERENT value in every field, so a swapped decode cannot
     * pass by symmetry. */
    uint32_t raw = (0x9u << 8) | (0x7u << 4) | 0x2u;
    raw |= kAdrenoIommuId2Vmid16 | kAdrenoIommuId2Ptfs4K;
    CHECK(adreno_iommu_id2_decode(raw, &d) == 0);
    CHECK(d.raw == raw);
    CHECK(d.ubs == field(raw, 11, 8));
    CHECK(d.oas == field(raw, 7, 4));
    CHECK(d.ias == field(raw, 3, 0));
    CHECK(d.ubs == 9u && d.oas == 7u && d.ias == 2u);
    CHECK(d.ptfs_4k == 1u && d.ptfs_16k == 0u && d.ptfs_64k == 0u);
    CHECK(d.vmid16 == 1u);
    /* The ENCODINGS and the WIDTHS are different numbers on this word: UBS=9 is
     * not a width, it encodes to 48. */
    CHECK(d.ubs_bits == 48u);
    CHECK(d.ias_bits == 40u);
    CHECK(d.oas_bits == 48u);
    CHECK(d.ubs != d.ubs_bits);

    /* All ones: every flag, every encoding 15, every width 48. */
    CHECK(adreno_iommu_id2_decode(0xffffffffu, &d) == 0);
    CHECK(d.ubs == 15u && d.oas == 15u && d.ias == 15u);
    CHECK(d.ubs_bits == 48u && d.oas_bits == 48u && d.ias_bits == 48u);
    CHECK(d.ptfs_4k == 1u && d.ptfs_16k == 1u && d.ptfs_64k == 1u);
    CHECK(d.vmid16 == 1u);

    /* All zeros: no flag, every encoding 0, every width 32. */
    CHECK(adreno_iommu_id2_decode(0u, &d) == 0);
    CHECK(d.ubs == 0u && d.oas == 0u && d.ias == 0u);
    CHECK(d.ubs_bits == 32u && d.oas_bits == 32u && d.ias_bits == 32u);
    CHECK(d.ptfs_4k == 0u && d.ptfs_16k == 0u && d.ptfs_64k == 0u);
    CHECK(d.vmid16 == 0u);

    /* A NULL out is refused, not written through. */
    CHECK(adreno_iommu_id2_decode(raw, nullptr) == 1);
}

static void testSizes()
{
    /* arm-smmu.c:1880-1885 + :1727-1744. Use a word where IAS and OAS ENCODE to
     * DIFFERENT widths, so a swapped read cannot pass. */
    uint32_t raw = (0x9u << 8) | (0x4u << 4) | 0x2u;   /* UBS=9, OAS=4, IAS=2 */
    CHECK(adreno_iommu_ipa_size(raw) == adreno_iommu_id_size_to_bits(2u));
    CHECK(adreno_iommu_ipa_size(raw) == 40u);
    CHECK(adreno_iommu_pa_size(raw) == adreno_iommu_id_size_to_bits(4u));
    CHECK(adreno_iommu_pa_size(raw) == 44u);
    CHECK(adreno_iommu_ipa_size(raw) != adreno_iommu_pa_size(raw));

    /* THE POINT: a STAGE-1 domain's oas is ipa_size (arm-smmu.c:735-736), NOT
     * pa_size. Deriving it from ID2.OAS would give 44 here and the wrong
     * TCR2.PASIZE nibble. */
    CHECK(adreno_iommu_stage1_oas(raw) == adreno_iommu_ipa_size(raw));
    CHECK(adreno_iommu_stage1_oas(raw) != adreno_iommu_pa_size(raw));
    CHECK(adreno_iommu_stage1_oas(raw) == 40u);

    /* va_size, version-gated (arm-smmu.c:1899-1912). On this word v1 says 40
     * (IPA) and v2 says 48 (UBS) -- the gate is what makes them differ. */
    int ok = 0;
    ok = 0;
    CHECK(adreno_iommu_va_size(raw, kAdrenoIommuVersionV1, &ok) == 40u);
    CHECK(ok == 1);
    ok = 0;
    CHECK(adreno_iommu_va_size(raw, kAdrenoIommuVersionV1_64K, &ok) == 40u);
    CHECK(ok == 1);
    ok = 0;
    CHECK(adreno_iommu_va_size(raw, kAdrenoIommuVersionV2, &ok) == 48u);
    CHECK(ok == 1);
    CHECK(adreno_iommu_va_size(raw, kAdrenoIommuVersionV1, nullptr) !=
          adreno_iommu_va_size(raw, kAdrenoIommuVersionV2, nullptr));

    /* An unknown version is REFUSED with ok cleared, never defaulted to v2. */
    ok = 99;
    CHECK(adreno_iommu_va_size(raw, kAdrenoIommuVersionUnknown, &ok) == 0u);
    CHECK(ok == 0);
    ok = 99;
    CHECK(adreno_iommu_va_size(raw, 7u, &ok) == 0u);
    CHECK(ok == 0);
    /* A NULL ok must not crash. */
    CHECK(adreno_iommu_va_size(raw, kAdrenoIommuVersionV2, nullptr) == 48u);
}

static void testVersion()
{
    /* arm-smmu.c:1965-1975 arm_smmu_of_match[]. arm,mmu-401 is the ONLY entry
     * that is not plain v1 or v2 -- it is ARM_SMMU_V1_64K. */
    CHECK(adreno_iommu_version_of_compatible("arm,smmu-v1") ==
          kAdrenoIommuVersionV1);
    CHECK(adreno_iommu_version_of_compatible("arm,smmu-v2") ==
          kAdrenoIommuVersionV2);
    CHECK(adreno_iommu_version_of_compatible("arm,mmu-400") ==
          kAdrenoIommuVersionV1);
    CHECK(adreno_iommu_version_of_compatible("arm,mmu-401") ==
          kAdrenoIommuVersionV1_64K);
    CHECK(adreno_iommu_version_of_compatible("arm,mmu-500") ==
          kAdrenoIommuVersionV2);
    CHECK(adreno_iommu_version_of_compatible("cavium,smmu-v2") ==
          kAdrenoIommuVersionV2);
    CHECK(adreno_iommu_version_of_compatible("nvidia,smmu-500") ==
          kAdrenoIommuVersionV2);
    CHECK(adreno_iommu_version_of_compatible("qcom,smmu-v2") ==
          kAdrenoIommuVersionV2);
    /* The version enum values themselves (arm-smmu.h:259-263). */
    CHECK(kAdrenoIommuVersionV1 == 0u);
    CHECK(kAdrenoIommuVersionV1_64K == 1u);
    CHECK(kAdrenoIommuVersionV2 == 2u);

    /* sdm845.dtsi:4904 carries THREE compatibles; only the third is in the
     * pinned table. A first-entry lookup would fail and a guess would be v2 by
     * luck rather than by rule. */
    static const char *const node[] = {
        "qcom,sdm845-smmu-v2", "qcom,adreno-smmu", "qcom,smmu-v2"
    };
    CHECK(adreno_iommu_version_select(node, 3u) == kAdrenoIommuVersionV2);
    CHECK(adreno_iommu_version_select(node, 1u) == kAdrenoIommuVersionUnknown);
    CHECK(adreno_iommu_version_select(node, 2u) == kAdrenoIommuVersionUnknown);
    /* apps_smmu's compatibles, at sdm845.dtsi:5093. The first is qcom's own and
     * is not in the arm-smmu.c table; the second IS (arm-smmu.c:1970). */
    static const char *const apps[] = { "qcom,sdm845-smmu-500", "arm,mmu-500" };
    CHECK(adreno_iommu_version_select(apps, 2u) == kAdrenoIommuVersionV2);

    CHECK(adreno_iommu_version_select(nullptr, 3u) ==
          kAdrenoIommuVersionUnknown);
    CHECK(adreno_iommu_version_select(node, 0u) == kAdrenoIommuVersionUnknown);
    CHECK(adreno_iommu_version_of_compatible(nullptr) ==
          kAdrenoIommuVersionUnknown);
    CHECK(adreno_iommu_version_of_compatible("") ==
          kAdrenoIommuVersionUnknown);
}

static void testContextAndGranules()
{
    /* arm-smmu.c:720-724: an AArch64 context needs at least one AArch64 granule
     * feature, which comes from the three PTFS bits (:1906-1911). */
    CHECK(adreno_iommu_aarch64_context(kAdrenoIommuId2Ptfs4K) == 1u);
    CHECK(adreno_iommu_aarch64_context(kAdrenoIommuId2Ptfs16K) == 1u);
    CHECK(adreno_iommu_aarch64_context(kAdrenoIommuId2Ptfs64K) == 1u);
    CHECK(adreno_iommu_aarch64_context(0x00000fffu) == 0u);
    CHECK(adreno_iommu_aarch64_context(0u) == 0u);

    /* The effective granule mask: 4K=1, 16K=2, 64K=4. */
    CHECK(adreno_iommu_ptfs_effective(kAdrenoIommuId2Ptfs4K, 0u) == 1u);
    CHECK(adreno_iommu_ptfs_effective(kAdrenoIommuId2Ptfs16K, 0u) == 2u);
    CHECK(adreno_iommu_ptfs_effective(kAdrenoIommuId2Ptfs64K, 0u) == 4u);
    CHECK(adreno_iommu_ptfs_effective(
              kAdrenoIommuId2Ptfs4K | kAdrenoIommuId2Ptfs64K, 0u) == 5u);
    /* arm-smmu-qcom.c:358 CLEARS 16K on the Adreno impl, so an advertised 16K
     * bit must not be reported as usable there. */
    CHECK(adreno_iommu_ptfs_effective(kAdrenoIommuId2Ptfs16K, 1u) == 0u);
    CHECK(adreno_iommu_ptfs_effective(
              kAdrenoIommuId2Ptfs4K | kAdrenoIommuId2Ptfs16K |
              kAdrenoIommuId2Ptfs64K, 1u) == 5u);
    CHECK(adreno_iommu_ptfs_effective(0u, 0u) == 0u);
    CHECK(adreno_iommu_ptfs_effective(0u, 1u) == 0u);

    /* ID0.CTTW (arm-smmu.h:54) OR the firmware (arm-smmu.c:1799-1805). */
    CHECK(kAdrenoIommuId0Cttw == bit(14));
    CHECK(adreno_iommu_coherent_walk(kAdrenoIommuId0Cttw, 0u) == 1u);
    CHECK(adreno_iommu_coherent_walk(0u, 1u) == 1u);
    CHECK(adreno_iommu_coherent_walk(0u, 0u) == 0u);
    CHECK(adreno_iommu_coherent_walk(kAdrenoIommuId0Cttw, 1u) == 1u);

    /* arm-smmu-qcom.c:200-208/:226-229: the GPU's SID 0 selects TTBR1 on this
     * SoC; only msm8996 is excluded, and it is not this board. */
    int ok = 0;
    ok = 0;
    CHECK(adreno_iommu_ttbr1_for_gpu(&ok) == 1u);
    CHECK(ok == 1);
    CHECK(adreno_iommu_ttbr1_for_gpu(nullptr) == 1u);

    /* arm-smmu.c:545-548: upstream puts the table in ONE TTBR and leaves the
     * other carrying only the ASID. Round 83 writes asid|table to both, which
     * is consistent because its TCR disables the other bank (EPD). */
    const uint32_t asid = 0x12u;
    const uint64_t table = 0x877fe000ull;
    CHECK(adreno_iommu_ttbr_idle_word(asid) == 0x0012000000000000ull);
    CHECK((adreno_iommu_ttbr_idle_word(asid) & 0x0000ffffffffffffull) == 0ull);
    /* The ASID occupies TTBR bits 63..48, so in the HIGH 32-bit word it sits at
     * bits 31..16 -- i.e. value_hi == asid << 16, not asid. */
    CHECK(static_cast<uint32_t>(adreno_iommu_ttbr_idle_word(asid) >> 32) ==
          (asid << 16));
    CHECK(static_cast<uint32_t>(adreno_iommu_ttbr_idle_word(asid) >> 32) != asid);
    /* It differs from round 83's word ONLY in the table pointer. */
    uint64_t r83 = adreno_smmu_ctx_ttbrn(asid, table);
    CHECK(adreno_iommu_ttbr_idle_word(asid) != r83);
    CHECK((adreno_iommu_ttbr_idle_word(asid) ^ r83) == table);
}

static void testGate()
{
    /* A fully-satisfied input set. The CX word is round 35's ACTUAL reading
     * with BIT(31) forced SET -- i.e. the word the phone would show if it were
     * powered, not a hypothetical. */
    struct adreno_iommu_gate_inputs g{};
    g.window_base = kAdrenoIommuKgslSmmuBase;
    g.window_size = kAdrenoIommuKgslSmmuSize;
    g.cx_status_word = 0x80005ff4u;
    g.clocks_enabled = 1u;
    g.kmem_mapped = 1u;
    g.cb_already_owned = 0u;
    g.stream_faults = 1u;
    CHECK(adreno_iommu_assess(&g) == kAdrenoIommuProbePermitted);
    CHECK(adreno_iommu_read_permitted(&g) == 1);

    /* Fail-closed: no facts at all. */
    CHECK(adreno_iommu_assess(nullptr) == kAdrenoIommuProbeRefusedInvalid);
    CHECK(adreno_iommu_read_permitted(nullptr) == 0);

    /* THE MEASURED SITUATION: round 35 recorded cx=0x00005ff4, BIT(31) clear.
     * The gate must refuse exactly that. */
    struct adreno_iommu_gate_inputs cx = g;
    cx.cx_status_word = 0x00005ff4u;
    CHECK(adreno_iommu_assess(&cx) == kAdrenoIommuProbeRefusedCxDown);
    CHECK(adreno_iommu_read_permitted(&cx) == 0);

    /* Each precondition, alone, produces its OWN verdict -- so an operator can
     * tell which one to fix. A gate that collapsed them could not. */
    struct adreno_iommu_gate_inputs c = g;
    c.window_base = kAdrenoIommuAppsSmmuBase;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedWrongWindow);
    c = g; c.window_size = 0x80000ull;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedWrongWindow);
    c = g; c.window_size = 0x100000ull; c.window_base = 0x05000000ull;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedWrongWindow);
    c = g; c.clocks_enabled = 0u;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedClocksOff);
    c = g; c.kmem_mapped = 0u;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedUnmapped);
    c = g; c.cb_already_owned = 1u;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedCbOwned);
    c = g; c.stream_faults = 0u;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedNoStream);

    /* Zero and non-power-of-two windows are INVALID before they are windows.
     * NOTE 0x8000 IS a power of two, so it is a valid SIZE and reaches the
     * window check instead -- a test that assumed "wrong size" would be wrong. */
    c = g; c.window_size = 0ull;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedInvalid);
    c = g; c.window_size = 0x10000ull * 3ull;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedInvalid);
    c = g; c.window_size = 0x10001ull;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedInvalid);
    c = g; c.window_size = 0x8000ull;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedWrongWindow);

    /* ORDER is load-bearing: a wrong window beats a down CX (the window check
     * is pure arithmetic and must not be preempted), and a down CX beats the
     * merely-incorrect checks behind it. */
    c = g; c.window_base = kAdrenoIommuAppsSmmuBase; c.cx_status_word = 0x00005ff4u;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedWrongWindow);
    c = g; c.cx_status_word = 0x00005ff4u; c.clocks_enabled = 0u;
    c.kmem_mapped = 0u; c.cb_already_owned = 1u; c.stream_faults = 0u;
    CHECK(adreno_iommu_assess(&c) == kAdrenoIommuProbeRefusedCxDown);

    /* Every verdict has a distinct, non-"unknown" name. */
    const uint32_t verdicts[8] = {
        kAdrenoIommuProbePermitted, kAdrenoIommuProbeRefusedInvalid,
        kAdrenoIommuProbeRefusedWrongWindow, kAdrenoIommuProbeRefusedCxDown,
        kAdrenoIommuProbeRefusedClocksOff, kAdrenoIommuProbeRefusedUnmapped,
        kAdrenoIommuProbeRefusedCbOwned, kAdrenoIommuProbeRefusedNoStream
    };
    for (uint32_t i = 0; i < 8u; ++i) {
        const char *n = adreno_iommu_verdict_name(
            static_cast<enum adreno_iommu_probe_verdict>(verdicts[i]));
        CHECK(n != nullptr && n[0] != '\0');
        CHECK(!(n[0] == 'u' && n[1] == 'n'));
        for (uint32_t j = 0; j < i; ++j) {
            const char *m = adreno_iommu_verdict_name(
                static_cast<enum adreno_iommu_probe_verdict>(verdicts[j]));
            bool same = true;
            uint32_t k = 0;
            while (n[k] || m[k]) { if (n[k] != m[k]) { same = false; break; } ++k; }
            CHECK(!same);
        }
    }
    CHECK(adreno_iommu_verdict_name(
              static_cast<enum adreno_iommu_probe_verdict>(99))[0] == 'u');

    /* Precondition names and anchors: total, distinct, non-empty. */
    const uint32_t pre[6] = {
        kAdrenoIommuPreCxPowered, kAdrenoIommuPreClocksOn,
        kAdrenoIommuPreKmemMapped, kAdrenoIommuPreWindowIsKgsl,
        kAdrenoIommuPreCbFree, kAdrenoIommuPreStreamFaults
    };
    for (uint32_t i = 0; i < 6u; ++i) {
        const char *n = adreno_iommu_precondition_name(pre[i]);
        const char *a = adreno_iommu_precondition_anchor(pre[i]);
        CHECK(n != nullptr && n[0] != '\0');
        CHECK(a != nullptr && a[0] != '\0');
        CHECK(!(n[0] == 'u' && n[1] == 'n'));
        for (uint32_t j = 0; j < i; ++j) {
            const char *m = adreno_iommu_precondition_name(pre[j]);
            bool same = true;
            uint32_t k = 0;
            while (n[k] || m[k]) { if (n[k] != m[k]) { same = false; break; } ++k; }
            CHECK(!same);
        }
    }
    CHECK(adreno_iommu_precondition_name(0u)[0] == 'u');
    CHECK(adreno_iommu_precondition_anchor(0u)[0] == 'u');
    /* Derived from the enum, never typed as 0x3f. */
    CHECK(adreno_iommu_required_preconditions() == kAdrenoIommuPreAll);
    CHECK(adreno_iommu_required_preconditions() == 0x3fu);
}

static void testSteps()
{
    struct adreno_iommu_step steps[20]{};
    uint32_t count = 0;
    CHECK(adreno_iommu_steps(steps, 20u, &count) == 0);
    CHECK(count == adreno_iommu_step_count());
    CHECK(count == 14u);
    CHECK(count <= 20u);

    uint32_t gate = 0, read = 0, derive = 0, handoff = 0;
    uint32_t cx_index = 0xffffffffu, first_read = 0xffffffffu;
    uint32_t last_gate = 0u;
    for (uint32_t i = 0; i < count; ++i) {
        CHECK(steps[i].order == i);
        CHECK(steps[i].name != nullptr && steps[i].name[0] != '\0');
        CHECK(steps[i].anchor != nullptr && steps[i].anchor[0] != '\0');
        if (steps[i].kind == kAdrenoIommuStepGate) { ++gate; last_gate = i; }
        else if (steps[i].kind == kAdrenoIommuStepRead) {
            ++read;
            if (first_read == 0xffffffffu) first_read = i;
        }
        else if (steps[i].kind == kAdrenoIommuStepDerive) ++derive;
        else if (steps[i].kind == kAdrenoIommuStepHandoff) { ++handoff; }
        else CHECK(false);
        /* Identify the CX gate BY NAME, so the safety rule below cannot be
         * satisfied by an accidental ordering. */
        const char *want = "confirm-cx-powered";
        const char *have = steps[i].name;
        uint32_t k = 0; bool same = true;
        while (want[k] || have[k]) {
            if (want[k] != have[k]) { same = false; break; }
            ++k;
        }
        if (same) cx_index = i;
    }
    CHECK(gate == 6u);
    CHECK(read == 2u);
    CHECK(derive == 5u);
    CHECK(handoff == 1u);
    CHECK(gate + read + derive + handoff == count);

    /* THE SAFETY RULE from rounds 22/28, as a property of the DATA: the first
     * register READ comes strictly AFTER the CX-powered gate. */
    CHECK(cx_index != 0xffffffffu);
    CHECK(first_read != 0xffffffffu);
    CHECK(first_read > cx_index);
    /* And no read precedes ANY gate at all. */
    for (uint32_t i = 0; i < count; ++i)
        if (steps[i].kind == kAdrenoIommuStepRead) CHECK(i >= last_gate);

    /* The round-83 handoff is LAST: computing words is the end, not the start. */
    CHECK(steps[count - 1].kind == kAdrenoIommuStepHandoff);

    /* A short buffer is REFUSED with count 0, never truncated. */
    struct adreno_iommu_step tiny[4]{};
    uint32_t n = 99;
    CHECK(adreno_iommu_steps(tiny, 4u, &n) == 1);
    CHECK(n == 0u);
    CHECK(adreno_iommu_steps(tiny, 13u, &n) == 1);   /* one short */
    CHECK(n == 0u);
    CHECK(adreno_iommu_steps(nullptr, 20u, &n) == 1);
    CHECK(adreno_iommu_steps(steps, 20u, nullptr) == 1);
    CHECK(adreno_iommu_steps(steps, 14u, &n) == 0);  /* exact fit is accepted */
    CHECK(n == 14u);
}

static void testComposition()
{
    /* THE COMPOSITION: decode a word, derive ias/oas, and hand them to round
     * 83's REAL tcr/tcr2. Nothing below re-implements those builders. */
    uint32_t raw = (0x2u << 8) | (0x1u << 4) | 0x2u;  /* UBS=2, OAS=1, IAS=2 */
    raw |= kAdrenoIommuId2Ptfs4K;

    struct adreno_iommu_stage1_words w{};
    CHECK(adreno_iommu_stage1_words(raw, kAdrenoIommuVersionV2, 1u, &w) == 0);
    CHECK(w.ok == 1u);
    /* va_size = UBS = 40; stage-1 oas = ipa = 40. */
    CHECK(w.ias == adreno_iommu_va_size(raw, kAdrenoIommuVersionV2, nullptr));
    CHECK(w.oas == adreno_iommu_stage1_oas(raw));
    CHECK(w.ias == 40u && w.oas == 40u);
    CHECK(w.coherent_walk == 1u);
    CHECK(w.ttbr1 == 1u);

    /* The words ARE round 83's -- CALLED here, not copied. */
    CHECK(w.tcr == adreno_smmu_ctx_tcr(w.ias, w.coherent_walk, w.ttbr1, nullptr));
    CHECK(w.tcr2 == adreno_smmu_ctx_tcr2(w.oas, 1u, nullptr));
    /* And the concrete numbers, so a change in EITHER module is visible. The
     * TCR word is 0xb5180080 rather than round 83's own 0x00803518 example
     * because the GPU selects the TTBR1 geometry, which shifts it. */
    CHECK(w.tcr == 0xb5180080u);
    CHECK(w.tcr2 == 0x00038012u);      /* SEP(0x7) | PASIZE=2 | AS */
    CHECK((w.tcr2 & kAdrenoSmmuTcr2PasizeMask) == 2u);
    CHECK((w.tcr2 & kAdrenoSmmuTcr2SepMask) == kAdrenoSmmuTcr2SepUpstream);
    CHECK((w.tcr2 & kAdrenoSmmuTcr2As) != 0u);
    /* T0SZ = 64 - ias, and exactly one EPD bit is set. NOTE the geometry: with
     * TTBR1 selected, arm-smmu.h:405-409 SHIFTS every field by 16, so T0SZ
     * (GENMASK(5,0)) lands at bits 21..16 and SH0 (13..12) at 29..28. Reading
     * them at their TTBR0 positions would find zero -- which is itself proof the
     * shift happened, so both facts are asserted. */
    CHECK((w.tcr & kAdrenoSmmuTcrT0szMask) == 0u);
    CHECK(((w.tcr >> 16) & kAdrenoSmmuTcrT0szMask) == (64u - w.ias));
    CHECK(((w.tcr & kAdrenoSmmuTcrEpd0) != 0u) !=
          ((w.tcr & kAdrenoSmmuTcrEpd1) != 0u));
    /* The TTBR1 geometry is the one selected, so EPD0 is the set bit. */
    CHECK((w.tcr & kAdrenoSmmuTcrEpd0) != 0u);

    /* ias actually DRIVES TCR: a different UBS must move the T1SZ field. */
    {
        uint32_t raw2 = (0x4u << 8) | (0x1u << 4) | 0x2u;  /* UBS=4 -> 44 */
        raw2 |= kAdrenoIommuId2Ptfs4K;
        struct adreno_iommu_stage1_words w2{};
        CHECK(adreno_iommu_stage1_words(raw2, kAdrenoIommuVersionV2, 1u, &w2) == 0);
        CHECK(w2.ias == 44u);
        CHECK(w2.tcr != w.tcr);
        CHECK(((w2.tcr >> 16) & kAdrenoSmmuTcrT0szMask) == (64u - 44u));
        /* Only that field moves: the shifted geometry is unchanged. */
        const uint32_t tsz_hi = kAdrenoSmmuTcrT0szMask << 16;
        CHECK((w2.tcr & ~tsz_hi) == (w.tcr & ~tsz_hi));
        /* And a v1 SMMU on the SAME word takes IPA (40), ignoring UBS. */
        struct adreno_iommu_stage1_words w3{};
        CHECK(adreno_iommu_stage1_words(raw2, kAdrenoIommuVersionV1, 1u, &w3) == 0);
        CHECK(w3.ias == 40u);
        CHECK(w3.tcr == w.tcr);
    }

    /* oas drives TCR2.PASIZE from the IPA, NOT from ID2.OAS: with IAS=2 (40)
     * and OAS=6 (48), a module reading OAS would emit PASIZE 5. */
    {
        uint32_t raw3 = (0x2u << 8) | (0x6u << 4) | 0x2u;
        raw3 |= kAdrenoIommuId2Ptfs4K;
        struct adreno_iommu_stage1_words w4{};
        CHECK(adreno_iommu_stage1_words(raw3, kAdrenoIommuVersionV2, 1u, &w4) == 0);
        CHECK(w4.oas == 40u);
        CHECK((w4.tcr2 & kAdrenoSmmuTcr2PasizeMask) == 2u);
        CHECK((w4.tcr2 & kAdrenoSmmuTcr2PasizeMask) != 5u);
        CHECK(w4.tcr2 == adreno_smmu_ctx_tcr2(40u, 1u, nullptr));
    }

    /* coherent_walk reaches the TCR: the non-coherent branch changes SH0, which
     * under the SHIFTED TTBR1 geometry sits at bits 29..28, not 13..12. */
    {
        struct adreno_iommu_stage1_words w5{};
        CHECK(adreno_iommu_stage1_words(raw, kAdrenoIommuVersionV2, 0u, &w5) == 0);
        CHECK(((w5.tcr >> 28) & 0x3u) ==
              static_cast<uint32_t>(kAdrenoSmmuTcrShOs));
        CHECK(((w.tcr >> 28) & 0x3u) ==
              static_cast<uint32_t>(kAdrenoSmmuTcrShIs));
        /* SH_IS != SH_OS, so the two branches really differ. */
        CHECK(((w5.tcr >> 28) & 0x3u) != ((w.tcr >> 28) & 0x3u));
        CHECK((w5.tcr & 0x0f00f000u) == 0u);  /* nothing at the TTBR0 slots */
        CHECK(w5.tcr != w.tcr);
        /* A non-boolean is NORMALISED so the two modules cannot disagree. */
        struct adreno_iommu_stage1_words w6{};
        CHECK(adreno_iommu_stage1_words(raw, kAdrenoIommuVersionV2, 7u, &w6) == 0);
        CHECK(w6.coherent_walk == 1u);
        CHECK(w6.tcr == w.tcr);
    }

    /* Refusals. */
    CHECK(adreno_iommu_stage1_words(raw, kAdrenoIommuVersionV2, 1u, nullptr) == 1);
    CHECK(adreno_iommu_stage1_words(raw, kAdrenoIommuVersionUnknown, 1u, &w) == 1);
    /* No granule bit at all -> no context this port can model. */
    CHECK(adreno_iommu_stage1_words(0x00000212u, kAdrenoIommuVersionV2, 1u,
                                    &w) == 1);
    /* 16K advertised but the Adreno impl CLEARS it -> no 4 KiB -> refused. */
    CHECK(adreno_iommu_stage1_words(
              (0x2u << 8) | (0x1u << 4) | 0x2u | kAdrenoIommuId2Ptfs16K,
              kAdrenoIommuVersionV2, 1u, &w) == 1);
    /* A refusal leaves no half-written word: everything zero. */
    CHECK(w.ias == 0u && w.oas == 0u && w.tcr == 0u && w.tcr2 == 0u && w.ok == 0u);
}

static void testWindowsAndMissing()
{
    /* The window predicates. */
    CHECK(adreno_iommu_window_is_kgsl(kAdrenoIommuKgslSmmuBase,
                                      kAdrenoIommuKgslSmmuSize) == 1);
    CHECK(adreno_iommu_window_is_kgsl(kAdrenoIommuAppsSmmuBase,
                                      kAdrenoIommuKgslSmmuSize) == 0);
    /* Containment is NOT identity: a bigger window could reach other registers,
     * and a shifted base would reach a different unit entirely. */
    CHECK(adreno_iommu_window_is_kgsl(kAdrenoIommuKgslSmmuBase, 0x100000ull) == 0);
    CHECK(adreno_iommu_window_is_kgsl(0x05000000ull, 0x100000ull) == 0);
    CHECK(adreno_iommu_window_is_kgsl(0x05040000ull, 0x10000ull) == 1);
    CHECK(adreno_iommu_window_is_kgsl(0x05040004ull, 0x10000ull) == 0);

    /* Rounds 22/28: every one of the six collapsible-rail windows is forbidden,
     * and anything outside them is not. */
    CHECK(adreno_iommu_forbidden_window_count() == 6u);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x05000000ull) == 1);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x05030000ull) == 1);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x05040000ull) == 1);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x05040028ull) == 1);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x05061000ull) == 1);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x0506a000ull) == 1);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x05070000ull) == 1);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x0509e000ull) == 1);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x0b200000ull) == 1);
    /* Outside every window. 0x05050000 is the gap between the GPU block and the
     * SMMU; 0x15000000 is apps_smmu, a different unit on a different rail. */
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x05050000ull) == 0);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x0509a000ull) == 0);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x0b500000ull) == 0);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x15000000ull) == 0);
    /* THE GPUCC CARVE-OUT (round 28) is a HOLE in the GMU window, not a
     * deletion of it. GPUCC is readable while the domain is collapsed; the byte
     * just past it, still inside the GMU's numeric reach, is forbidden again.
     * A model that simply dropped the GMU window would get 0x05099000 wrong. */
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(kAdrenoIommuGpuccBase) == 0);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x05098000ull) == 0);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(0x05099000ull) == 1);
    /* Each window's LAST byte is forbidden and the next one is not. */
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(kAdrenoIommuKgslSmmuBase +
                                                kAdrenoIommuKgslSmmuSize - 1ull) == 1);
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(kAdrenoIommuKgslSmmuBase +
                                                kAdrenoIommuKgslSmmuSize) == 0);
    /* ID2 -- the register this whole module is about -- is INSIDE the forbidden
     * window. That is precisely why a gate has to exist instead of a probe. */
    CHECK(adreno_iommu_addr_is_pre_cx_forbidden(kAdrenoIommuKgslSmmuBase +
                                                kAdrenoIommuGr0Id2) == 1);

    /* The seven absent inputs. */
    CHECK(adreno_iommu_missing() == kAdrenoIommuMissingAll);
    CHECK(kAdrenoIommuMissingAll == 127u);
    CHECK(kAdrenoIommuMissingId2Word == 1u);
    CHECK(kAdrenoIommuMissingId0Word == 2u);
    CHECK(kAdrenoIommuMissingCxUp == 4u);
    CHECK(kAdrenoIommuMissingAsid == 8u);
    CHECK(kAdrenoIommuMissingPageTable == 16u);
    CHECK(kAdrenoIommuMissingStream == 32u);
    CHECK(kAdrenoIommuMissingKernelCaller == 64u);
    /* Readiness is DERIVED and is 0. */
    CHECK(adreno_iommu_ready() == 0);
    CHECK((adreno_iommu_missing() == 0u) == (adreno_iommu_ready() != 0));

    const uint32_t bits[7] = {
        kAdrenoIommuMissingId2Word, kAdrenoIommuMissingId0Word,
        kAdrenoIommuMissingCxUp, kAdrenoIommuMissingAsid,
        kAdrenoIommuMissingPageTable, kAdrenoIommuMissingStream,
        kAdrenoIommuMissingKernelCaller
    };
    for (uint32_t i = 0; i < 7u; ++i) {
        const char *n = adreno_iommu_missing_name(bits[i]);
        const char *a = adreno_iommu_missing_anchor(bits[i]);
        CHECK(n != nullptr && n[0] != '\0');
        CHECK(a != nullptr && a[0] != '\0');
        CHECK(!(n[0] == 'u' && n[1] == 'n'));
        for (uint32_t j = 0; j < i; ++j) {
            const char *m = adreno_iommu_missing_name(bits[j]);
            bool same = true;
            uint32_t k = 0;
            while (n[k] || m[k]) { if (n[k] != m[k]) { same = false; break; } ++k; }
            CHECK(!same);
        }
    }
    CHECK(adreno_iommu_missing_name(0u)[0] == 'u');
    CHECK(adreno_iommu_missing_anchor(0u)[0] == 'u');
}

static void testVerify()
{
    uint32_t count = 0;
    CHECK(adreno_iommu_verify(&count) == 0);
    /* Pin the count so a DELETED check cannot pass silently: a verifier that
     * loses assertions still returns 0. Both exact and a floor. */
    CHECK(count == kExpectedVerifyChecks);
    CHECK(count >= kExpectedVerifyChecks);
    CHECK(adreno_iommu_verify(nullptr) == 0);
}

int main()
{
    testIdSize();
    testRegisters();
    testId2Decode();
    testSizes();
    testVersion();
    testContextAndGranules();
    testGate();
    testSteps();
    testComposition();
    testWindowsAndMissing();
    testVerify();
    if (failures) {
        std::printf("FAIL: enchilada A630 SMMU ID2 probe contract "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 SMMU ID2 probe contract "
                "(11 test groups, 0 failures)\n");
    return 0;
}
