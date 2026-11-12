// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoGen1BackendV1 host test: 9 groups, CHECK() accumulates failures.
#include <cstdint>
#include <cstdio>
#include "AdrenoGen1BackendV1.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testCatalogPins()
{
    uint32_t chip = 0, alt = 0, revn = 0, gmem = 0;
    CHECK(enchilada_gen1_backend_a630_pins(&chip, &alt, &revn, &gmem) == 0);
    CHECK(chip == 0x06030001u);   /* a6xx_catalog.c:884 */
    CHECK(alt == 0x06030002u);    /* a6xx_catalog.c:885 */
    CHECK(revn == 630u);          /* a6xx_catalog.c:888 */
    CHECK(gmem == 0x00100000u);   /* a6xx_catalog.c:893 SZ_1M */
    CHECK(gmem == kEnchiladaGen1GmemBytes);
    CHECK(kEnchiladaGen1GmuCgcMode == 0x00020202u);       /* :901 */
    CHECK(kEnchiladaGen1PrimFifoThreshold == 0x00180000u); /* :902 */
    CHECK(kEnchiladaGen1HasGmuChipid == 0u);              /* :898-903 absent */
    /* Refusals. */
    CHECK(enchilada_gen1_backend_a630_pins(nullptr, &alt, &revn, &gmem) == 1);
    CHECK(enchilada_gen1_backend_a630_pins(&chip, nullptr, &revn, &gmem) == 1);
    CHECK(enchilada_gen1_backend_a630_pins(&chip, &alt, nullptr, &gmem) == 1);
    CHECK(enchilada_gen1_backend_a630_pins(&chip, &alt, &revn, nullptr) == 1);
}

static void testGbif()
{
    /* a6xx_gpu.c:71-77 a6xx_has_gbif() is false for A630. */
    CHECK(enchilada_gen1_backend_has_gbif() == 0);
    CHECK(enchilada_gen1_backend_gbif_reason() != nullptr);
    CHECK(enchilada_gen1_backend_gbif_reason()[0] != '\0');
}

static void testGmuResumePlan()
{
    struct enchilada_gen1_resume_op ops[kEnchiladaGen1ResumeStepCount];
    uint32_t count = 99u;
    CHECK(enchilada_gen1_backend_gmu_resume(ops, kEnchiladaGen1ResumeStepCount,
                                            &count) == 0);
    CHECK(count == 5u);
    /* a6xx_gmu.c:1033 CX first. */
    CHECK(ops[0].step == kEnchiladaGen1ResumeCxRuntimeGet);
    CHECK(ops[0].takes_value == 0u);
    CHECK(ops[0].guarded == 0u);
    /* a6xx_gmu.c:1040-1041 GX guarded by !IS_ERR_OR_NULL(gmu->gxpd). */
    CHECK(ops[1].step == kEnchiladaGen1ResumeGxRuntimeGet);
    CHECK(ops[1].guarded == 1u);
    /* a6xx_gmu.c:1044/1045-1046 rates. */
    CHECK(ops[2].step == kEnchiladaGen1ResumeCoreRate);
    CHECK(ops[2].takes_value == 1u);
    CHECK(ops[2].value == 200000000u);
    CHECK(ops[3].step == kEnchiladaGen1ResumeHubRate);
    CHECK(ops[3].value == 150000000u);
    /* a6xx_gmu.c:1047 clocks last. */
    CHECK(ops[4].step == kEnchiladaGen1ResumeClocks);
    CHECK(ops[4].takes_value == 0u);
    /* The plan is ordered: step ids ascend. */
    for (uint32_t i = 0; i < count; ++i) {
        CHECK(ops[i].step == i);
    }
    /* Hub rate is generation-dependent, and enchilada is NOT the A740 branch. */
    CHECK(enchilada_gen1_backend_hub_rate_hz(0) == 150000000u);
    CHECK(enchilada_gen1_backend_hub_rate_hz(1) == 200000000u);
    CHECK(enchilada_gen1_backend_hub_rate_hz(0) !=
          enchilada_gen1_backend_hub_rate_hz(1));
    /* Exactly two ops carry a value. */
    uint32_t valued = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (ops[i].takes_value) ++valued;
    }
    CHECK(valued == 2u);
    /* Refusals leave *count at 0. */
    count = 99u;
    CHECK(enchilada_gen1_backend_gmu_resume(ops, 4u, &count) == 1);
    CHECK(count == 0u);
    count = 99u;
    CHECK(enchilada_gen1_backend_gmu_resume(nullptr,
                                            kEnchiladaGen1ResumeStepCount,
                                            &count) == 1);
    CHECK(enchilada_gen1_backend_gmu_resume(ops, 5u, nullptr) == 1);
}

static void testGmuRegions()
{
    struct enchilada_gen1_region r[kEnchiladaGen1GmuRegionCount];
    uint32_t count = 0;
    CHECK(enchilada_gen1_backend_gmu_regions(r, kEnchiladaGen1GmuRegionCount,
                                             &count) == 0);
    CHECK(count == 4u);
    /* a6xx_gmu.c:1741/:1747/:1752 -- A630 allocates these three at IOVA 0. */
    CHECK(r[0].bytes == 0x4000u);
    CHECK(r[0].iova == 0u);
    CHECK(r[1].bytes == 0x4000u);
    CHECK(r[1].iova == 0u);
    CHECK(r[2].bytes == 0x4000u);
    CHECK(r[2].iova == 0u);
    /* a6xx_gmu.c:1696/:1708-1709 -- dummy stays SZ_4K at 0x60000000. The
     * a660/a7xx SZ_8K widening at :1704 must NOT leak into A630. */
    CHECK(r[3].bytes == 0x1000u);
    CHECK(r[3].iova == 0x60000000u);
    CHECK(r[3].bytes != 0x2000u);
    /* No region is executable: there is no icache for a legacy GMU. */
    for (uint32_t i = 0; i < count; ++i) {
        CHECK(r[i].executable == 0u);
    }
    CHECK(enchilada_gen1_backend_has_icache_region() == 0);
    /* Total is the sum, and 0xd000. */
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        total += r[i].bytes;
    }
    CHECK(total == 0xd000u);
    CHECK(enchilada_gen1_backend_gmu_region_bytes() == total);
    /* Refusals. */
    CHECK(enchilada_gen1_backend_gmu_regions(r, 3u, &count) == 1);
    CHECK(count == 0u);
    CHECK(enchilada_gen1_backend_gmu_regions(nullptr, 4u, &count) == 1);
    CHECK(enchilada_gen1_backend_gmu_regions(r, 4u, nullptr) == 1);
}

static void testSptprac()
{
    uint32_t value = 0, mask = 0, target = 0;
    /* a6xx_gmu.c:373/:375-376 enable. */
    CHECK(enchilada_gen1_backend_sptprac(1u, &value, &mask, &target) == 0);
    CHECK(value == 0x778000u);
    CHECK(mask == 0x38u);
    CHECK(target == 0x28u);
    CHECK(mask == kEnchiladaGen1SptpracPollMask);
    CHECK(target == kEnchiladaGen1SptpracPollTarget);
    /* a6xx_gmu.c:398 disable differs by exactly the low bit. */
    CHECK(enchilada_gen1_backend_sptprac(0u, &value, &mask, &target) == 0);
    CHECK(value == 0x778001u);
    CHECK((value & ~1u) == kEnchiladaGen1SptpracEnable);
    /* Poll bounds a6xx_gmu.c:376. */
    CHECK(kEnchiladaGen1SptpracPollUsecMin == 1u);
    CHECK(kEnchiladaGen1SptpracPollUsecMax == 100u);
    /* Refusals. */
    CHECK(enchilada_gen1_backend_sptprac(1u, nullptr, &mask, &target) == 1);
    CHECK(enchilada_gen1_backend_sptprac(1u, &value, nullptr, &target) == 1);
    CHECK(enchilada_gen1_backend_sptprac(1u, &value, &mask, nullptr) == 1);
}

static void testPasAndZap()
{
    /* a6xx_gpu.c:17 defines GPU_PAS_ID 13; :819 passes it to the ZAP loader.
     * enchilada.dts:23612 carries qcom,pas-id = <0x0d> == 13. */
    CHECK(enchilada_gen1_backend_pas_id() == 13u);
    CHECK(enchilada_gen1_backend_pas_id() == kEnchiladaGen1GPasId);
    CHECK(0x0du == 13u); /* the DTS encoding, made explicit */
    /* The origin string must name the Linux pin and the DTS property, and must
     * record that sheng is NOT the source. */
    const char *origin = enchilada_gen1_backend_pas_origin();
    CHECK(origin != nullptr);
    CHECK(origin[0] != '\0');
    /* Firmware names as the catalog writes them. */
    CHECK(kEnchiladaGen1ZapFirmwareName[0] == 'a');
    CHECK(kEnchiladaGen1ZapFirmwareName[11] == 't');  /* "a630_zap.mdt" */
    CHECK(kEnchiladaGen1ZapFirmwareName[12] == '\0');
    CHECK(kEnchiladaGen1SqeFirmwareName[0] == 'a');
    CHECK(kEnchiladaGen1SqeFirmwareName[10] == 'w');  /* "a630_sqe.fw" */
    CHECK(kEnchiladaGen1SqeFirmwareName[11] == '\0');
    CHECK(kEnchiladaGen1GmuFirmwareName[0] == 'a');
    CHECK(kEnchiladaGen1GmuFirmwareName[11] == 'n');  /* "a630_gmu.bin" */
    CHECK(kEnchiladaGen1GmuFirmwareName[12] == '\0');
}

static void testCpRingOffsets()
{
    uint32_t base = 0, cntl = 0, rptr_addr = 0, rptr = 0, wptr = 0;
    /* a6xx.xml:2281-2285, WORD offsets. */
    CHECK(enchilada_gen1_backend_cp_ring_offsets(&base, &cntl, &rptr_addr, &rptr,
                                                 &wptr) == 0);
    CHECK(base == 0x0800u);
    CHECK(cntl == 0x0802u);
    CHECK(rptr_addr == 0x0804u);
    CHECK(rptr == 0x0806u);
    CHECK(wptr == 0x0807u);
    /* The pair the doorbell loop touches differs by one word. */
    CHECK(wptr == rptr + 1u);
    /* RPTR_ADDR is a reg64 whose low word sits at 0x0804. */
    CHECK(rptr_addr == base + 4u);
    CHECK(enchilada_gen1_backend_cp_ring_offsets(nullptr, &cntl, &rptr_addr,
                                                 &rptr, &wptr) == 1);
    CHECK(enchilada_gen1_backend_cp_ring_offsets(&base, &cntl, &rptr_addr, &rptr,
                                                 nullptr) == 1);
}

static void testAdmission()
{
    struct enchilada_gen1_admission adm;
    CHECK(enchilada_gen1_backend_admit(&adm) == 0);
    /* The whole point of the module: six modelled, zero satisfied. Round 70
     * moved CP_RING from 0 to 1 by modelling the bring-up ORDER; the verdict
     * did not move, because nothing is satisfied. */
    CHECK(adm.modeled_count == 6u);
    CHECK(adm.satisfied_count == 0u);
    CHECK(adm.verdict == kEnchiladaGen1AdmitBlocked);
    CHECK(adm.verdict != kEnchiladaGen1AdmitReady);
    /* modelled + not-modelled == the whole set. */
    CHECK(adm.modeled_count + 6u == kEnchiladaGen1PrereqCount);
    CHECK(kEnchiladaGen1PrereqCount == 12u);
    /* Exactly these six are modelled. */
    CHECK(adm.items[kEnchiladaGen1PrereqPowerPath].modeled == 1u);
    CHECK(adm.items[kEnchiladaGen1PrereqWindows].modeled == 1u);
    CHECK(adm.items[kEnchiladaGen1PrereqGmuBoot].modeled == 1u);
    CHECK(adm.items[kEnchiladaGen1PrereqHfiV1].modeled == 1u);
    CHECK(adm.items[kEnchiladaGen1PrereqFirmware].modeled == 1u);
    CHECK(adm.items[kEnchiladaGen1PrereqCpRing].modeled == 1u);
    /* And these six are NOT -- ZAP_AUTH above all, because the sheng tree
     * contains no PAS code that could have been carried over. */
    CHECK(adm.items[kEnchiladaGen1PrereqZapAuth].modeled == 0u);
    CHECK(adm.items[kEnchiladaGen1PrereqSmmuStage1].modeled == 0u);
    CHECK(adm.items[kEnchiladaGen1PrereqBoardVaMap].modeled == 0u);
    CHECK(adm.items[kEnchiladaGen1PrereqDeviceProof].modeled == 0u);
    CHECK(adm.items[kEnchiladaGen1PrereqConfigOn].modeled == 0u);
    CHECK(adm.items[kEnchiladaGen1PrereqMacosFrontend].modeled == 0u);
    /* modelled is NOT satisfied, for every single item. */
    for (uint32_t i = 0; i < kEnchiladaGen1PrereqCount; ++i) {
        CHECK(adm.items[i].satisfied == 0u);
        CHECK(adm.items[i].id == i);
    }
    CHECK(enchilada_gen1_backend_admit(nullptr) == 1);
    /* Names and anchors are total over 0..11 and sentinel beyond 11. */
    for (uint32_t i = 0; i < kEnchiladaGen1PrereqCount; ++i) {
        const char *n = enchilada_gen1_backend_prereq_name(i);
        const char *a = enchilada_gen1_backend_prereq_anchor(i);
        CHECK(n != nullptr && n[0] != '\0');
        CHECK(a != nullptr && a[0] != '\0');
        /* No two prerequisites may share a name. */
        for (uint32_t j = 0; j < i; ++j) {
            const char *m = enchilada_gen1_backend_prereq_name(j);
            bool same = true;
            uint32_t k = 0;
            while (n[k] || m[k]) {
                if (n[k] != m[k]) { same = false; break; }
                ++k;
            }
            CHECK(!same);
        }
    }
    CHECK(enchilada_gen1_backend_prereq_name(kEnchiladaGen1PrereqCount)[0] == 'u');
    CHECK(enchilada_gen1_backend_prereq_anchor(kEnchiladaGen1PrereqCount)[0] == 'u');
}

static void testSelfCheck()
{
    uint32_t count = 0;
    CHECK(enchilada_gen1_backend_verify(&count) == 0);
    CHECK(count >= 60u);
    /* A NULL out-parameter must not crash and must still report. */
    CHECK(enchilada_gen1_backend_verify(nullptr) == 0);
}

int main()
{
    testCatalogPins();
    testGbif();
    testGmuResumePlan();
    testGmuRegions();
    testSptprac();
    testPasAndZap();
    testCpRingOffsets();
    testAdmission();
    testSelfCheck();
    if (failures) {
        std::printf("FAIL: enchilada A6XX_GEN1 backend admission contract "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A6XX_GEN1 backend admission contract "
                "(9 test groups, 0 failures)\n");
    return 0;
}
