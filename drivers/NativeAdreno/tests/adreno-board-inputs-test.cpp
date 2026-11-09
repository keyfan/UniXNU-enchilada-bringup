// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the enchilada A630 GPU BOARD-INPUT study. The constants derive
// from the vendored Mu-Silicium enchilada.dts (sha256-pinned, 4 byte-identical
// copies) and were cross-checked against device-verified C constants in this
// repo + the sha256-pinned Linux v6.12 a6xx_catalog.c (21 checks PASS).
//
// Pure host, no IOKit, no firmware, no MMIO, no kernel linkage. HOST-VERIFIED
// decision record only: a DTB is a firmware description, NOT a device
// measurement. It proves nothing about the physical device.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "AdrenoBoardInputs.h"

static void testDedicatedSmmu()
{
    // The load-bearing Round-21 correction: the GPU does NOT use apps_smmu
    // @0x15000000 (flash #8's survey target). It has a DEDICATED kgsl SMMU.
    assert(ADRENO_BOARD_KGSL_SMMU_BASE == 0x05040000ull);
    assert(ADRENO_BOARD_KGSL_SMMU_SIZE == 0x00010000ull);
    assert(ADRENO_BOARD_KGSL_SMMU_PHANDLE == 0x1acu);
    assert(ADRENO_BOARD_KGSL_SMMU_IOMMU_CELLS == 1u);
    assert(ADRENO_BOARD_APPS_SMMU_BASE == 0x15000000ull);
    assert(ADRENO_BOARD_APPS_SMMU_PHANDLE == 0x29u);
    assert(ADRENO_BOARD_APPS_SMMU_IOMMU_CELLS == 2u);
    assert(ADRENO_BOARD_KGSL_SMMU_BASE != ADRENO_BOARD_APPS_SMMU_BASE);
    assert(ADRENO_BOARD_KGSL_SMMU_PHANDLE != ADRENO_BOARD_APPS_SMMU_PHANDLE);
    printf("  testDedicatedSmmu: kgsl 0x5040000/#cells=1 vs apps 0x15000000/#cells=2\n");
}

static void testPhandleGate()
{
    assert(adreno_board_is_kgsl_smmu(ADRENO_BOARD_KGSL_SMMU_PHANDLE) == 1);
    assert(adreno_board_is_kgsl_smmu(ADRENO_BOARD_APPS_SMMU_PHANDLE) == 0);
    assert(adreno_board_is_kgsl_smmu(0u) == 0);
    assert(adreno_board_is_kgsl_smmu(0xffffu) == 0);
    // #iommu-cells=1 => SID only, NO mask (opposite of apps_smmu's 2 cells).
    assert(adreno_board_sid_is_unmasked() == 1);
    printf("  testPhandleGate: 0x1ac accepted, 0x29 rejected, SID unmasked\n");
}

static void testContextSids()
{
    uint32_t sid = 0xffffffffu;
    assert(adreno_board_context_sid(kAdrenoBoardCtxGfx3dUser, &sid) == 1);
    assert(sid == 0x00u);
    assert(adreno_board_context_sid(kAdrenoBoardCtxGfx3dSecure, &sid) == 1);
    assert(sid == 0x02u);
    assert(adreno_board_context_sid(kAdrenoBoardCtxGfx3dSecureAlt, &sid) == 1);
    assert(sid == 0x01u);
    assert(adreno_board_context_sid(kAdrenoBoardCtxGmuUser, &sid) == 1);
    assert(sid == 0x04u);
    assert(adreno_board_context_sid(kAdrenoBoardCtxGmuKernel, &sid) == 1);
    assert(sid == 0x05u);
    // NULL out pointer is legal; unknown context is rejected.
    assert(adreno_board_context_sid(kAdrenoBoardCtxGmuKernel, nullptr) == 1);
    assert(adreno_board_context_sid(kAdrenoBoardCtxGmuKernel, &sid) == 1);
    assert(adreno_board_context_sid(static_cast<enum adreno_board_context>(99), &sid) == 0);
    printf("  testContextSids: 0/2/1/4/5 (all single-cell, no mask)\n");
}

static void testBoardIdentity()
{
    assert(ADRENO_BOARD_GPU_BASE == 0x05000000ull);
    assert(ADRENO_BOARD_GPU_SIZE == 0x00040000ull);
    assert(ADRENO_BOARD_GMU_BASE == 0x0506a000ull);
    assert(ADRENO_BOARD_GMU_PDC_BASE == 0x0b200000ull);
    assert(ADRENO_BOARD_GPU_IRQ == 300u);
    assert(ADRENO_BOARD_GMU_HFI_IRQ == 304u);
    assert(ADRENO_BOARD_GMU_IRQ == 305u);
    // qcom,chipid=<0x6030001> matches a6xx_catalog.c 0x06030001 BY EQUALITY.
    assert(ADRENO_BOARD_CHIPID == 0x06030001u);
    assert(ADRENO_BOARD_HIGHEST_BANK_BIT == 15u);
    assert(ADRENO_BOARD_UBWC_MODE == 2u);
    assert(ADRENO_BOARD_ZAP_PAS_ID == 13u);
    printf("  testBoardIdentity: chipid 0x06030001, PAS_ID 13, IRQ 300/304/305\n");
}

static void testReadyGate()
{
    // The board description is internally consistent and complete enough to
    // DESIGN a stage-1 SMMU context (still NOT device verification).
    assert(adreno_board_smmu_context_ready() == 1);
    printf("  testReadyGate: board description consistent (design gate, not device)\n");
}

int main()
{
    testDedicatedSmmu();
    testPhandleGate();
    testContextSids();
    testBoardIdentity();
    testReadyGate();
    printf("PASS: enchilada A630 GPU board-input study host tests (5 groups)\n");
    return 0;
}
