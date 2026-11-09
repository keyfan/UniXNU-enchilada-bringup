// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoBoardInputs -- host-verified stage-5 GPU board-input study. See header.
#include "AdrenoBoardInputs.h"

/* A GPU MMIO probe must be gated on the DEDICATED kgsl SMMU. Programming a
 * stage-1 context on apps_smmu (phandle 0x29) for the GPU is the WRONG SMMU
 * (flash #8 surveyed apps_smmu @0x15000000). */
int adreno_board_is_kgsl_smmu(uint32_t phandle)
{
    return (phandle == ADRENO_BOARD_KGSL_SMMU_PHANDLE) ? 1 : 0;
}

/* The vendor DTB declares #iommu-cells=<0x01> on arm,smmu-kgsl@5040000, so
 * each GPU iommus tuple is (phandle, SID): there is NO mask cell. This is the
 * direct opposite of apps_smmu (#iommu-cells=<0x02>, SID + mask). */
int adreno_board_sid_is_unmasked(void)
{
    return (ADRENO_BOARD_KGSL_SMMU_IOMMU_CELLS == 1u &&
            ADRENO_BOARD_APPS_SMMU_IOMMU_CELLS == 2u) ? 1 : 0;
}

int adreno_board_context_sid(enum adreno_board_context ctx, uint32_t *out_sid)
{
    uint32_t sid;

    switch (ctx) {
    case kAdrenoBoardCtxGfx3dUser:     sid = 0x00u; break;
    case kAdrenoBoardCtxGfx3dSecure:   sid = 0x02u; break;
    case kAdrenoBoardCtxGfx3dSecureAlt: sid = 0x01u; break;
    case kAdrenoBoardCtxGmuUser:       sid = 0x04u; break;
    case kAdrenoBoardCtxGmuKernel:     sid = 0x05u; break;
    default: return 0;
    }
    if (out_sid) {
        *out_sid = sid;
    }
    return 1;
}

/* Read-only BOARD-DESCRIPTION gate (NOT device verification). Returns 1 only
 * when the extracted facts are internally consistent and complete enough to
 * design a stage-1 SMMU context. Every condition can fail if the constants
 * above were transcribed wrongly. */
int adreno_board_smmu_context_ready(void)
{
    uint32_t sids[5];
    unsigned i, j;
    static const enum adreno_board_context kCtx[5] = {
        kAdrenoBoardCtxGfx3dUser,
        kAdrenoBoardCtxGfx3dSecure,
        kAdrenoBoardCtxGfx3dSecureAlt,
        kAdrenoBoardCtxGmuUser,
        kAdrenoBoardCtxGmuKernel,
    };

    /* the two SMMUs really are different units */
    if (ADRENO_BOARD_KGSL_SMMU_BASE == ADRENO_BOARD_APPS_SMMU_BASE) {
        return 0;
    }
    if (ADRENO_BOARD_KGSL_SMMU_PHANDLE == ADRENO_BOARD_APPS_SMMU_PHANDLE) {
        return 0;
    }
    /* the GPU phandle must resolve to the dedicated SMMU, never apps_smmu */
    if (!adreno_board_is_kgsl_smmu(ADRENO_BOARD_KGSL_SMMU_PHANDLE) ||
        adreno_board_is_kgsl_smmu(ADRENO_BOARD_APPS_SMMU_PHANDLE)) {
        return 0;
    }
    /* single-cell SID (no mask) is the load-bearing correction */
    if (!adreno_board_sid_is_unmasked()) {
        return 0;
    }
    /* board identity must agree with the sha-pinned A630 catalog chip id */
    if (ADRENO_BOARD_CHIPID != 0x06030001u) {
        return 0;
    }
    if (!adreno_board_is_kgsl_smmu(ADRENO_BOARD_KGSL_SMMU_PHANDLE)) {
        return 0;
    }
    /* all five context SIDs must be present, distinct, and fit the 0..5 range */
    for (i = 0; i < 5u; ++i) {
        if (!adreno_board_context_sid(kCtx[i], &sids[i]) || sids[i] > 5u) {
            return 0;
        }
        for (j = 0; j < i; ++j) {
            if (sids[i] == sids[j]) {
                return 0;
            }
        }
    }
    /* an unknown context must be rejected */
    if (adreno_board_context_sid((enum adreno_board_context)99, 0)) {
        return 0;
    }
    return 1;
}
