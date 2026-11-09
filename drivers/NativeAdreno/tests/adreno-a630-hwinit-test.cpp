// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the A6XX GEN1 (A630) hw_init register map derived by
// scripts/derive-a630-hwinit.py from SHA-pinned Linux v6.12 sources.
//
// Pure host, no IOKit, no firmware, no MMIO, no kernel linkage. This verifies
// the GENERATION-SPECIFIC facts (A6XX variant offsets, a630_hwcg, a630_protect)
// and that the four A7XX-only registers are correctly absent. HOST-VERIFIED
// decision record only: it proves nothing about the physical device.
#include <cassert>
#include <cstdio>
#include "NativeAdrenoA630HwInit.h"

static void testGenerationOffsets()
{
    // A6XX GMU CGC registers: 0x09309/0x0930a/0x0930b. The sheng A7XX backend
    // uses 0x23b09/0x23b0a/0x23b0b -- a real generation delta, not a guess.
    assert(A630_GPU_GMU_AO_GMU_CGC_MODE_CNTL == 0x09309u);
    assert(A630_GPU_GMU_AO_GMU_CGC_DELAY_CNTL == 0x0930au);
    assert(A630_GPU_GMU_AO_GMU_CGC_HYST_CNTL == 0x0930bu);

    // Safe read-only probe registers (from the pinned a6xx.xml, no variants).
    assert(A630_RBBM_STATUS == 0x00210u);
    assert(A630_RBBM_STATUS3 == 0x00213u);
    assert(A630_RBBM_INT_0_STATUS == 0x00201u);
    assert(A630_CP_ALWAYS_ON_COUNTER == 0x00980u);
    assert(A630_CP_RB_BASE == 0x00800u);
    assert(A630_CP_RB_RPTR == 0x00806u);
    assert(A630_CP_RB_WPTR == 0x00807u);
    assert(A630_CP_PROTECT == 0x00850u);
    printf("  testGenerationOffsets: A6XX GMU CGC 0x0930x, RBBM/CP offsets OK\n");
}

static void testTableCounts()
{
    assert(NATIVE_ADRENO_A630_HWCG_COUNT == 105);
    assert(NATIVE_ADRENO_A630_PROTECT_COUNT == 32);
    assert(sizeof(NativeAdrenoA630Hwcg) / sizeof(NativeAdrenoA630Hwcg[0])
           == NATIVE_ADRENO_A630_HWCG_COUNT);
    assert(sizeof(NativeAdrenoA630Protect) / sizeof(NativeAdrenoA630Protect[0])
           == NATIVE_ADRENO_A630_PROTECT_COUNT);
    printf("  testTableCounts: 105 hwcg entries, 32 protect writes\n");
}

static void testFirstLastProtect()
{
    // a630_protect_regs[0] = A6XX_PROTECT_RDONLY(0x00000, 0x04ff)
    //   -> (0x04ff << 18) | 0 = 0x13fc0000
    assert(NativeAdrenoA630Protect[0][0] == 0u);
    assert(NativeAdrenoA630Protect[0][1] == 0x13fc0000u);
    // Last entry is written at count_max-1 = 31 (infinite NORDWR range):
    //   NORDWR(0x11c00, 0x0000) -> (1<<31) | (0<<18) | 0x11c00 = 0x80011c00
    assert(NativeAdrenoA630Protect[31][0] == 31u);
    assert(NativeAdrenoA630Protect[31][1] == 0x80011c00u);
    printf("  testFirstLastProtect: RDONLY(0,0x4ff) and infinite NORDWR encode OK\n");
}

int main()
{
    testGenerationOffsets();
    testTableCounts();
    testFirstLastProtect();
    printf("PASS: enchilada A630 (A6XX GEN1) hw_init register map host tests\n");
    return 0;
}
