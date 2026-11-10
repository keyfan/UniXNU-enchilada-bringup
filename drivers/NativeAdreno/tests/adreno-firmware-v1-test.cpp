// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * adreno-firmware-v1-test.cpp - host tests for the enchilada (A630) LEGACY GMU
 *                              firmware-load contract. Pure byte/state
 *                              assertions, no hardware, no firmware, no MMIO.
 * Each test group pins one upstream-derived legacy-vs-modern rule.
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include "AdrenoFirmwareV1.h"

static unsigned g_failures = 0u;
static unsigned g_checks = 0u;

static void check(bool ok, const char *label)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        std::printf("  FAIL %s\n", label);
    }
}

/* A synthetic GMU image buffer; content is irrelevant to the legacy path,
 * since it is copied as ONE flat bulk write with no header parse. */
static uint8_t g_image[ADRENO_FW_V1_GMU_MAX_BYTES];

static void testLegacyGmuIdentity()
{
    check(adreno_fw_v1_is_legacy_gmu() == 1, "A630 is a legacy GMU");
    check(adreno_fw_v1_gmu_max_bytes() == 0x8000u,
          "legacy ITCM region is 0x8000 bytes (a6xx_gmu.c:739)");
    check(ADRENO_FW_V1_ITCM_BASE == 0x0u, "ITCM base 0x00000000");
    check(ADRENO_FW_V1_DTCM_BASE == 0x00040000u, "DTCM base 0x00040000");
    check(ADRENO_FW_V1_ITCM_START_REG == 0x0c00u, "ITCM_START reg 0xc00");
    check(ADRENO_FW_V1_DTCM_START_REG == 0x1c00u, "DTCM_START reg 0x1c00");
    check(adreno_fw_v1_modern_block_header_bytes() == 16u,
          "modern block_header is 16 B (contrast only)");
    std::printf("  testLegacyGmuIdentity: legacy GMU + ITCM/DTCM geometry\n");
}

static void testFlatGmuPlan()
{
    struct adreno_fw_v1_gmu_plan p;
    /* Exactly the observed a630_gmu.bin size = 0x8000 -> SUCCESS. */
    std::memset(g_image, 0xA5, sizeof(g_image));
    check(adreno_fw_v1_plan_gmu_legacy(g_image, 0x8000u, &p) == kAdrenoFwV1Success,
          "0x8000-byte image plans OK");
    check(adreno_fw_v1_plan_is_flat(&p) == 1, "plan is a single flat bulk write");
    check(p.itcm_base == ADRENO_FW_V1_ITCM_BASE, "plan ITCM base");
    check(p.itcm_reg == ADRENO_FW_V1_ITCM_START_REG, "plan ITCM_START reg");
    check(p.bytes == 0x8000u, "plan bytes == image size");
    check(p.bulk_words == 0x8000u / 4u, "plan bulk_words == bytes/4");
    check(p.is_flat == 1u, "is_flat flag set");
    /* A smaller image is still ONE flat write (no padding to 0x8000). */
    struct adreno_fw_v1_gmu_plan small;
    check(adreno_fw_v1_plan_gmu_legacy(g_image, 0x100u, &small) == kAdrenoFwV1Success,
          "0x100-byte image plans OK");
    check(small.bulk_words == 0x40u && small.bytes == 0x100u,
          "small image keeps its exact size, no 0x8000 padding");
    std::printf("  testFlatGmuPlan: one flat ITCM bulk write, exact size, no header parse\n");
}

static void testRefusalsZeroOut()
{
    struct adreno_fw_v1_gmu_plan p;
    std::memset(&p, 0xEE, sizeof(p)); /* sentinel */
    /* > 0x8000 -> TooLarge with *out zeroed. */
    static uint8_t big[ADRENO_FW_V1_GMU_MAX_BYTES + 4u];
    check(adreno_fw_v1_plan_gmu_legacy(big, sizeof(big), &p) == kAdrenoFwV1TooLarge,
          "image > 0x8000 refused (TooLarge)");
    check(p.itcm_reg == 0u && p.bytes == 0u && p.is_flat == 0u,
          "TooLarge zeroes *out");
    /* unaligned (not multiple of 4) -> Unaligned. */
    std::memset(&p, 0xEE, sizeof(p));
    check(adreno_fw_v1_plan_gmu_legacy(g_image, 0x101u, &p) == kAdrenoFwV1Unaligned,
          "unaligned size refused (Unaligned)");
    check(p.is_flat == 0u, "Unaligned zeroes *out");
    /* empty / NULL -> Empty / Invalid. */
    check(adreno_fw_v1_plan_gmu_legacy(g_image, 0u, &p) == kAdrenoFwV1Empty,
          "zero-size image refused (Empty)");
    check(adreno_fw_v1_plan_gmu_legacy(NULL, 0x100u, &p) == kAdrenoFwV1Invalid,
          "NULL image refused (Invalid)");
    check(adreno_fw_v1_plan_gmu_legacy(g_image, 0x100u, NULL) == kAdrenoFwV1Invalid,
          "NULL out refused (Invalid)");
    check(adreno_fw_v1_plan_is_flat(NULL) == 0, "plan_is_flat(NULL) == 0");
    std::printf("  testRefusalsZeroOut: TooLarge/Unaligned/Empty/Invalid all fail closed\n");
}

static void testBulkWordsNoHeaderParse()
{
    /* The modern 16-byte block_header (addr,size,type,value) would be parsed
     * by sheng's AdrenoFirmwarePlanGmu; the legacy path must IGNORE it. Prove
     * that a header-shaped prefix does NOT change the flat plan: the whole
     * image is still ONE bulk write of bytes/4 words. */
    uint32_t *w = reinterpret_cast<uint32_t *>(g_image);
    w[0] = 0x00000000u; /* addr */
    w[1] = 0x00000100u; /* size */
    w[2] = 0x00000001u; /* type */
    w[3] = 0x00000000u; /* value */
    struct adreno_fw_v1_gmu_plan p;
    check(adreno_fw_v1_plan_gmu_legacy(g_image, 0x100u, &p) == kAdrenoFwV1Success,
          "header-shaped prefix still plans OK");
    check(p.bulk_words == 0x40u,
          "legacy treats all 0x100 B as bulk words, no 16-B header skip");
    check(p.bytes == 0x100u, "bulk bytes == full image (header not stripped)");
    std::printf("  testBulkWordsNoHeaderParse: legacy NEVER parses block_header\n");
}

static void testSqeWhereamiPredicate()
{
    /* Exact a6xx_gpu.c:731-736 predicate on MAPPED BO dwords. */
    uint32_t b[3] = {0u, 0u, 0u};
    check(adreno_fw_v1_sqe_whereami(b) == 0, "zero dwords -> no whereami");
    b[0] = 0x190u; /* version >= 0x190 */
    check(adreno_fw_v1_sqe_whereami(b) == 1, "version 0x190 -> whereami");
    b[0] = 0x18fu;
    check(adreno_fw_v1_sqe_whereami(b) == 0, "version 0x18f -> no whereami");
    b[0] = 0xau;   /* patch marker 0xa */
    b[2] = 0x1u;   /* buf[2]&0xf >= 1 */
    check(adreno_fw_v1_sqe_whereami(b) == 1, "patched 0xa + buf[2]>=1 -> whereami");
    b[2] = 0x0u;
    check(adreno_fw_v1_sqe_whereami(b) == 0,
          "patched 0xa but buf[2]&0xf==0 -> no whereami");
    b[0] = 0xau; b[2] = 0x10u; /* low nibble 0 */
    check(adreno_fw_v1_sqe_whereami(b) == 0, "buf[2] low nibble 0 -> no whereami");
    check(adreno_fw_v1_sqe_whereami(NULL) == 0, "NULL buf -> 0");
    std::printf("  testSqeWhereamiPredicate: exact upstream version/patch predicate\n");
}

static void testBlobTable()
{
    for (unsigned i = 0; i < ADRENO_FW_V1_ROLE_COUNT; ++i) {
        const struct adreno_fw_v1_blob *b = adreno_fw_v1_blob((enum adreno_fw_v1_role)i);
        check(b != NULL, "blob(i) non-NULL");
        check(b->bytes > 0u, "blob(i) has a size");
        check(std::strlen(b->sha256) == 64u, "blob(i) sha256 is 64 hex chars");
        check(adreno_fw_v1_blob_matches((enum adreno_fw_v1_role)i, b->bytes, b->sha256) == 1,
              "blob_matches(i) true for recorded observation");
        check(adreno_fw_v1_blob_matches((enum adreno_fw_v1_role)i, b->bytes + 1u, b->sha256) == 0,
              "blob_matches(i) false for wrong size");
        check(adreno_fw_v1_blob_matches((enum adreno_fw_v1_role)i, b->bytes, "deadbeef") == 0,
              "blob_matches(i) false for wrong hash");
    }
    check(adreno_fw_v1_blob((enum adreno_fw_v1_role)3) == NULL, "role 3 -> NULL");
    check(adreno_fw_v1_blob_matches((enum adreno_fw_v1_role)99, 1u, "x") == 0,
          "invalid role mismatch -> 0");
    check(adreno_fw_v1_blob(kAdrenoFwV1RoleGmu)->bytes == ADRENO_FW_V1_GMU_BYTES,
          "GMU blob == 0x8000 observation");
    check(std::strcmp(adreno_fw_v1_blob(kAdrenoFwV1RoleZap)->public_name,
                      "qcom/sdm845/a630_zap.mbn") == 0, "ZAP public name");
    std::printf("  testBlobTable: 3 observed public blobs, exact pins\n");
}

static void testSignedZapBlocker()
{
    /* a6xx_catalog.c:897 .zapfw="a630_zap.mdt" + a6xx_gpu.c:17 PAS 13. */
    check(ADRENO_FW_V1_GPU_PAS_ID == 13u, "GPU_PAS_ID == 13");
    check(adreno_fw_v1_zap_requires_signed_chain() == 1,
          "ZAP requires vendor-signed .mdt/.b00/.b01 chain");
    check(adreno_fw_v1_blob(kAdrenoFwV1RoleZap)->needs_signed_chain == 1u,
          "ZAP blob flagged needs_signed_chain");
    check(adreno_fw_v1_blob(kAdrenoFwV1RoleGmu)->needs_signed_chain == 0u,
          "GMU blob does NOT need signed chain");
    check(adreno_fw_v1_blob(kAdrenoFwV1RoleSqe)->needs_signed_chain == 0u,
          "SQE blob does NOT need signed chain");
    std::printf("  testSignedZapBlocker: signed ZAP chain (PAS 13) is an explicit blocker\n");
}

static void testReadyFailClosed()
{
    struct adreno_fw_v1_state s = adreno_fw_v1_default_state();
    check(adreno_fw_v1_ready(&s) == 0, "default state is NOT ready");
    check(adreno_fw_v1_ready(NULL) == 0, "NULL state is NOT ready");
    /* Set every field except one, each time -> still NOT ready. */
    s.power_ready = 1u; s.gmu_plan_valid = 1u;
    s.blobs_present = ADRENO_FW_V1_PRESENT_ALL;
    s.zap_signed_chain = 1u; s.sqe_whereami = 1u; s.kernel_caller = 0u;
    check(adreno_fw_v1_ready(&s) == 0, "missing kernel caller -> NOT ready");
    s.kernel_caller = 1u; s.sqe_whereami = 0u;
    check(adreno_fw_v1_ready(&s) == 0, "missing SQE whereami -> NOT ready");
    s.sqe_whereami = 1u; s.zap_signed_chain = 0u;
    check(adreno_fw_v1_ready(&s) == 0, "missing signed ZAP -> NOT ready");
    s.zap_signed_chain = 1u; s.blobs_present = 0x3u;
    check(adreno_fw_v1_ready(&s) == 0, "incomplete blobs -> NOT ready");
    s.blobs_present = ADRENO_FW_V1_PRESENT_ALL; s.power_ready = 0u;
    check(adreno_fw_v1_ready(&s) == 0, "power not ready -> NOT ready");
    s.power_ready = 1u; s.gmu_plan_valid = 0u;
    check(adreno_fw_v1_ready(&s) == 0, "invalid GMU plan -> NOT ready");
    /* Only ALL six precondition bits -> ready. */
    s.gmu_plan_valid = 1u;
    check(adreno_fw_v1_ready(&s) == 1, "all preconditions met -> ready");
    std::printf("  testReadyFailClosed: 6-way AND gate, 0 today (no device/caller/ZAP)\n");
}

int main()
{
    std::setvbuf(stdout, NULL, _IONBF, 0);
    std::printf("enchilada stage 5 (GPU) A630 LEGACY GMU firmware-load contract:\n");
    testLegacyGmuIdentity();
    testFlatGmuPlan();
    testRefusalsZeroOut();
    testBulkWordsNoHeaderParse();
    testSqeWhereamiPredicate();
    testBlobTable();
    testSignedZapBlocker();
    testReadyFailClosed();
    std::printf("PASS: enchilada A630 legacy GMU firmware-load contract host tests (%u checks)\n",
                g_checks);
    return g_failures ? 1 : 0;
}
