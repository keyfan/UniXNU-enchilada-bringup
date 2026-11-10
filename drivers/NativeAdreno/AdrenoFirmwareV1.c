// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoFirmwareV1.c -- enchilada A630 LEGACY GMU firmware-load contract.
// Freestanding pure C11: no libc, no MMIO, no firmware, no kernel linkage.
// See AdrenoFirmwareV1.h for the sha256-pinned upstream provenance.

#include "AdrenoFirmwareV1.h"

#include <stdint.h>
#include <stddef.h>

static const struct adreno_fw_v1_blob gBlobs[ADRENO_FW_V1_ROLE_COUNT] = {
    { "qcom/sdm845/a630_zap.mbn", ADRENO_FW_V1_ZAP_MBN_SHA256,
      ADRENO_FW_V1_ZAP_MBN_BYTES, 1u },
    { "qcom/a630_gmu.bin", ADRENO_FW_V1_GMU_SHA256,
      ADRENO_FW_V1_GMU_BYTES, 0u },
    { "qcom/a630_sqe.fw", ADRENO_FW_V1_SQE_SHA256,
      ADRENO_FW_V1_SQE_BYTES, 0u },
};

static void zero_plan(struct adreno_fw_v1_gmu_plan *out)
{
    out->itcm_base = 0u;
    out->itcm_reg = 0u;
    out->bytes = 0u;
    out->bulk_words = 0u;
    out->is_flat = 0u;
}

int adreno_fw_v1_is_legacy_gmu(void)
{
    return 1; /* a6xx_gmu.c:1736-1738: A630 => gmu->legacy = true */
}

uint32_t adreno_fw_v1_gmu_max_bytes(void)
{
    return ADRENO_FW_V1_GMU_MAX_BYTES;
}

uint32_t adreno_fw_v1_modern_block_header_bytes(void)
{
    return ADRENO_FW_V1_MODERN_BLOCK_HEADER_BYTES;
}

enum adreno_fw_v1_status adreno_fw_v1_plan_gmu_legacy(const uint8_t *image,
        size_t bytes, struct adreno_fw_v1_gmu_plan *out)
{
    if (image == (const uint8_t *)0 || out == (struct adreno_fw_v1_gmu_plan *)0) {
        return kAdrenoFwV1Invalid;
    }
    zero_plan(out);
    if (bytes == 0u) {
        return kAdrenoFwV1Empty;
    }
    if (bytes > (size_t)ADRENO_FW_V1_GMU_MAX_BYTES) {
        return kAdrenoFwV1TooLarge;
    }
    if ((bytes & 3u) != 0u) {
        return kAdrenoFwV1Unaligned;
    }
    /* a6xx_gmu.c:739-750: ONE gmu_write_bulk() to ITCM_START, NO header parse. */
    out->itcm_base = ADRENO_FW_V1_ITCM_BASE;
    out->itcm_reg = ADRENO_FW_V1_ITCM_START_REG;
    out->bytes = (uint32_t)bytes;
    out->bulk_words = (uint32_t)(bytes >> 2);
    out->is_flat = 1u;
    return kAdrenoFwV1Success;
}

int adreno_fw_v1_plan_is_flat(const struct adreno_fw_v1_gmu_plan *plan)
{
    if (plan == (const struct adreno_fw_v1_gmu_plan *)0) {
        return 0;
    }
    return (plan->is_flat == 1u && plan->bulk_words > 0u &&
            plan->itcm_reg == ADRENO_FW_V1_ITCM_START_REG) ? 1 : 0;
}

int adreno_fw_v1_sqe_whereami(const uint32_t *buf)
{
    if (buf == (const uint32_t *)0) {
        return 0;
    }
    /* a6xx_gpu.c:731-736, exact predicate on the MAPPED BO dwords. */
    if (((buf[0] & 0xfu) == ADRENO_FW_V1_SQE_PATCH_MARKER &&
         (buf[2] & 0xfu) >= 1u) ||
        (buf[0] & 0xfffu) >= ADRENO_FW_V1_SQE_WHEREAMI_MIN_VERSION) {
        return 1;
    }
    return 0;
}

const struct adreno_fw_v1_blob *adreno_fw_v1_blob(enum adreno_fw_v1_role role)
{
    unsigned i = (unsigned)role;
    if (i >= ADRENO_FW_V1_ROLE_COUNT) {
        return (const struct adreno_fw_v1_blob *)0;
    }
    return &gBlobs[i];
}

static int streq(const char *a, const char *b)
{
    if (a == (const char *)0 || b == (const char *)0) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

int adreno_fw_v1_blob_matches(enum adreno_fw_v1_role role, uint32_t bytes,
        const char *sha256)
{
    const struct adreno_fw_v1_blob *b = adreno_fw_v1_blob(role);
    if (b == (const struct adreno_fw_v1_blob *)0) {
        return 0;
    }
    if (bytes != b->bytes) {
        return 0;
    }
    return streq(b->sha256, sha256);
}

int adreno_fw_v1_zap_requires_signed_chain(void)
{
    /* a6xx_catalog.c:897 .zapfw="a630_zap.mdt" + a6xx_gpu.c:17 PAS 13.
     * The public a630_zap.mbn alone is NOT the driver's split signed image. */
    return adreno_fw_v1_blob(kAdrenoFwV1RoleZap)->needs_signed_chain == 1u ? 1 : 0;
}

struct adreno_fw_v1_state adreno_fw_v1_default_state(void)
{
    struct adreno_fw_v1_state s;
    s.power_ready = 0u;
    s.gmu_plan_valid = 0u;
    s.blobs_present = 0u;
    s.zap_signed_chain = 0u;
    s.sqe_whereami = 0u;
    s.kernel_caller = 0u;
    return s;
}

int adreno_fw_v1_ready(const struct adreno_fw_v1_state *state)
{
    if (state == (const struct adreno_fw_v1_state *)0) {
        return 0;
    }
    if (state->power_ready != 1u ||
        state->gmu_plan_valid != 1u ||
        state->blobs_present != ADRENO_FW_V1_PRESENT_ALL ||
        state->zap_signed_chain != 1u ||
        state->sqe_whereami != 1u ||
        state->kernel_caller != 1u) {
        return 0;
    }
    return 1;
}
