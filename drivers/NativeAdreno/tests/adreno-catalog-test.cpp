// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the stage-5 GPU feasibility study. Pure host, no IOKit, no
// hardware. Verifies the A630/A740 encoding + the honest blocked verdict.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "AdrenoCatalog.h"

static void testA630Lookup()
{
    struct adreno_catalog_entry r;
    assert(adreno_catalog_lookup(ADRENO_CHIP_A630_V1, &r) == 0);
    assert(r.chip_id == 0x06030001u);
    assert(r.generation == kAdrenoGen1_A6xx);
    assert(r.gmu == kAdrenoGmuLegacy);
    assert(r.revn == 630u);
    assert(r.gmem_bytes == 0x00100000u);
    assert(r.address_space_size == 0u);
    assert(r.legacy_gmu == 1u);
    assert(std::strcmp(r.fw_zap, "a630_zap.mbn") == 0);
    assert(std::strcmp(r.fw_gmu, "a630_gmu.bin") == 0);
    assert(std::strcmp(r.fw_sqe, "a630_sqe.fw") == 0);

    struct adreno_catalog_entry v2;
    assert(adreno_catalog_lookup(ADRENO_CHIP_A630_V2, &v2) == 0);
    assert(v2.chip_id == 0x06030002u);
    assert(v2.generation == kAdrenoGen1_A6xx);
    assert(v2.revn == 630u);
    printf("  testA630Lookup: A630 v1/v2 = 6xx GEN1 legacy GMU, 1 MiB GMEM\n");
}

static void testA740Lookup()
{
    struct adreno_catalog_entry r;
    assert(adreno_catalog_lookup(ADRENO_CHIP_A740, &r) == 0);
    assert(r.chip_id == 0x43050a01u);
    assert(r.generation == kAdrenoGen2_A7xx);
    assert(r.gmu == kAdrenoGmuModern);
    assert(r.revn == 740u);
    assert(r.gmem_bytes == 0x00300000u);
    assert(r.address_space_size == 0x0000000400000000ull);
    assert(r.legacy_gmu == 0u);
    assert(std::strcmp(r.fw_gmu, "gmu_gen70200.bin") == 0);
    printf("  testA740Lookup: A740 = 7xx GEN2 modern GMU, 3 MiB GMEM, 16 GiB aspace\n");
}

static void testCrossGenerationGap()
{
    struct adreno_catalog_entry a630, a740;
    assert(adreno_catalog_lookup(ADRENO_CHIP_A630_V1, &a630) == 0);
    assert(adreno_catalog_lookup(ADRENO_CHIP_A740, &a740) == 0);
    assert(a630.generation != a740.generation);
    assert(a630.gmu != a740.gmu);
    assert(a630.gmem_bytes != a740.gmem_bytes);
    assert(std::strcmp(a630.fw_zap, a740.fw_zap) != 0);
    assert(std::strcmp(a630.fw_gmu, a740.fw_gmu) != 0);
    assert(std::strcmp(a630.fw_sqe, a740.fw_sqe) != 0);
    printf("  testCrossGenerationGap: firmware/GMU/GMEM/aspace all differ => no reuse\n");
}

static void testUnknownChipRefusal()
{
    struct adreno_catalog_entry r;
    /* Sentinel proves out is reset on refusal. */
    r.chip_id = 0xdeadbeefu;
    r.generation = kAdrenoGen2_A7xx;
    r.gmu = kAdrenoGmuModern;
    assert(adreno_catalog_lookup(0u, &r) == -1);
    assert(r.chip_id == 0u);
    assert(r.generation == kAdrenoGenerationUnknown);
    assert(r.gmu == kAdrenoGmuUnknown);
    assert(adreno_catalog_lookup(0x12345678u, &r) == -1);
    assert(adreno_catalog_lookup(ADRENO_CHIP_A630_V1, 0) == -1);
    printf("  testUnknownChipRefusal: unknown chip / null out refused, out zeroed\n");
}

static void testFirmwareNames()
{
    assert(std::strcmp(adreno_catalog_firmware_name(0), "a630_zap.mbn") == 0);
    assert(std::strcmp(adreno_catalog_firmware_name(1), "a630_gmu.bin") == 0);
    assert(std::strcmp(adreno_catalog_firmware_name(2), "a630_sqe.fw") == 0);
    assert(adreno_catalog_firmware_name(3) == 0);
    assert(adreno_catalog_firmware_name(99) == 0);
    printf("  testFirmwareNames: 3 A630 blobs, index >= 3 -> NULL\n");
}

static void testFirmwareMask()
{
    assert(adreno_catalog_firmware_complete(ADRENO_FW_ALL) == 1);
    assert(adreno_catalog_firmware_complete(0u) == 0);
    assert(adreno_catalog_firmware_complete(ADRENO_FW_ZAP | ADRENO_FW_GMU) == 0);
    assert(adreno_catalog_missing_firmware(0u) == ADRENO_FW_ALL);
    assert(adreno_catalog_missing_firmware(ADRENO_FW_ALL) == 0u);
    assert(adreno_catalog_missing_firmware(ADRENO_FW_ZAP) == (ADRENO_FW_GMU | ADRENO_FW_SQE));
    /* Extra unknown bits are ignored, not misreported as present. */
    assert(adreno_catalog_missing_firmware(ADRENO_FW_ALL | 0x80000000u) == 0u);
    printf("  testFirmwareMask: complete/missing masks correct\n");
}

static void testAssessmentBlocked()
{
    /* enchilada today: no firmware, no matching A630 backend. */
    unsigned v = adreno_catalog_assess(ADRENO_CHIP_A630_V1, 0u, 0);
    assert((v & ADRENO_BLOCK_FIRMWARE) != 0u);
    assert((v & ADRENO_BLOCK_BACKEND) != 0u);
    assert((v & ADRENO_BLOCK_UNKNOWN_CHIP) == 0u);

    /* Even with all firmware present, a GEN1 backend is still required. */
    v = adreno_catalog_assess(ADRENO_CHIP_A630_V1, ADRENO_FW_ALL, 1);
    assert((v & ADRENO_BLOCK_BACKEND) != 0u);
    assert((v & ADRENO_BLOCK_FIRMWARE) == 0u);

    /* Unknown chip is refused before anything else. */
    v = adreno_catalog_assess(0u, ADRENO_FW_ALL, 1);
    assert(v == ADRENO_BLOCK_UNKNOWN_CHIP);

    printf("  testAssessmentBlocked: A630 blocked on firmware + backend (honest)\n");
}

static void testAssessmentA740Reference()
{
    /* A740 with a real matching backend and all firmware is the only feasible
     * case in the catalog -- this is exactly what sheng/SM8550 has. */
    unsigned v = adreno_catalog_assess(ADRENO_CHIP_A740, ADRENO_FW_ALL, 1);
    assert(v == ADRENO_FEASIBLE);
    /* Without its own firmware it is blocked too. */
    v = adreno_catalog_assess(ADRENO_CHIP_A740, 0u, 1);
    assert((v & ADRENO_BLOCK_FIRMWARE) != 0u);
    assert((v & ADRENO_BLOCK_BACKEND) == 0u);
    printf("  testAssessmentA740Reference: feasible only with GEN2 backend + fw\n");
}

static void testChipidMatchesA630()
{
    assert(adreno_catalog_chipid_matches_a630(ADRENO_A630_DTB_CHIPID) == 1);
    assert(adreno_catalog_chipid_matches_a630(ADRENO_CHIP_A630_V2) == 1);
    assert(adreno_catalog_chipid_matches_a630(ADRENO_CHIP_A740) == 0);
    assert(adreno_catalog_chipid_matches_a630(0u) == 0);
    /* kgsl-3d0 window from the vendor DTB. */
    assert(ADRENO_KGSL3D0_BASE == 0x05000000u);
    assert(ADRENO_KGSL3D0_SIZE == 0x00040000u);
    printf("  testChipidMatchesA630: DT chipid check + kgsl-3d0 window constants\n");
}

static void testGenerationNames()
{
    assert(std::strcmp(adreno_catalog_generation_name(kAdrenoGen1_A6xx),
                       "A6XX_GEN1 (A630)") == 0);
    assert(std::strcmp(adreno_catalog_generation_name(kAdrenoGen2_A7xx),
                       "A7XX_GEN2 (A740)") == 0);
    assert(std::strcmp(adreno_catalog_generation_name(kAdrenoGenerationUnknown),
                       "unknown") == 0);
    printf("  testGenerationNames: human-readable generation labels\n");
}

int main()
{
    printf("enchilada stage 5 (GPU) A630 catalog / feasibility study:\n");
    testA630Lookup();
    testA740Lookup();
    testCrossGenerationGap();
    testUnknownChipRefusal();
    testFirmwareNames();
    testFirmwareMask();
    testAssessmentBlocked();
    testAssessmentA740Reference();
    testChipidMatchesA630();
    testGenerationNames();
    printf("PASS: enchilada A630 identification + feasibility study (10 test groups)\n");
    return 0;
}
