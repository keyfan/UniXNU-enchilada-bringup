// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * adreno-domain-state-test.cpp - host tests for the enchilada GPU domain-state
 *                                probe design. Pure predicates only; nothing
 *                                here touches hardware.
 */

#include <cstdio>
#include <cstdint>

#include "AdrenoDomainState.h"

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

/* 1. The probe window is exactly the GPUCC register file. */
static void testProbeWindowBounds()
{
    check(adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_GPUCC_BASE) == 1,
          "GPUCC base is inside the probe window");
    check(adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_GPUCC_BASE + ADRENO_DOMAIN_GPUCC_SIZE - 1ull) == 1,
          "last GPUCC byte is inside the probe window");
    check(adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_GPUCC_BASE + ADRENO_DOMAIN_GPUCC_SIZE) == 0,
          "first byte past GPUCC is outside");
    check(adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_GPUCC_BASE - 1ull) == 0,
          "byte before GPUCC is outside");
    check(adreno_domain_addr_is_probe_window(0ull) == 0, "address zero is outside");

    /* The three registers a probe actually needs must be inside. */
    check(adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_CX_STATUS) == 1,
          "CX status register is inside GPUCC");
    check(adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_GX_STATUS) == 1,
          "GX status register is inside GPUCC");
    check(adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_CX_GDSCR) == 1,
          "CX control register is inside GPUCC");
}

/* 2. CX status register is NOT the DTB reg value: the correction this design
 *    is built on. If someone "fixes" the header back to 0x509106c, this fails. */
static void testCxStatusRegisterCorrection()
{
    check(ADRENO_DOMAIN_CX_STATUS != ADRENO_DOMAIN_CX_GDSCR,
          "CX status differs from the DTB control register");
    check(ADRENO_DOMAIN_CX_STATUS ==
              ADRENO_DOMAIN_GPUCC_BASE + (uint64_t)ADRENO_DOMAIN_CX_GDS_HW_CTRL,
          "CX status == GPUCC + gds_hw_ctrl 0x1540");
    check(ADRENO_DOMAIN_CX_GDSCR == ADRENO_DOMAIN_GPUCC_BASE + 0x106cull,
          "CX control == GPUCC + 0x106c");
    check(ADRENO_DOMAIN_GX_STATUS == ADRENO_DOMAIN_GPUCC_BASE + 0x100cull + ADRENO_DOMAIN_CFG_GDSCR_OFFSET,
          "GX status == GPUCC + 0x100c + 0x4");
    check(ADRENO_DOMAIN_CFG_GDSCR_OFFSET == 0x4u, "CFG_GDSCR_OFFSET is 0x4");
}

/* 3. CX on-bit is PWR_ON_MASK BIT(31); nothing else flips it. */
static void testCxStatusDecode()
{
    check(adreno_domain_cx_is_on(0x80000000u) == 1, "BIT(31) set -> CX on");
    check(adreno_domain_cx_is_on(0x00000000u) == 0, "all zero -> CX off");
    check(adreno_domain_cx_is_on(0x7fffffffu) == 0, "all but BIT(31) -> CX off");
    check(adreno_domain_cx_is_on(0xffffffffu) == 1, "all ones -> CX on");
    check(adreno_domain_cx_is_on(1u << 30) == 0, "BIT(30) alone is not CX on");
    check(ADRENO_DOMAIN_CX_ON_BIT == 31u, "CX on bit is 31");
}

/* 4. GX decodes up/ready and down/ready on different bits in the CFG_GDSCR. */
static void testGxStatusDecode()
{
    check(adreno_domain_gx_is_on(1u << 16) == 1, "BIT(16) -> GX power-up complete");
    check(adreno_domain_gx_is_on(1u << 15) == 0, "BIT(15) alone is not GX on");
    check(adreno_domain_gx_is_off(1u << 15) == 1, "BIT(15) -> GX power-down complete");
    check(adreno_domain_gx_is_off(1u << 16) == 0, "BIT(16) alone is not GX off");
    check(adreno_domain_gx_is_on(0u) == 0 && adreno_domain_gx_is_off(0u) == 0,
          "zero word reports neither up nor down complete");
    check(ADRENO_DOMAIN_GX_ON_BIT == 16u && ADRENO_DOMAIN_GX_OFF_BIT == 15u,
          "GX on/off bits are 16/15");
}

/* 5. Every forbidden GPU window must be rejected, and GPUCC must be accepted. */
static void testForbiddenWindows()
{
    static const uint64_t kForbidden[][2] = {
        { 0x05000000ull, 0x40000ull },
        { 0x05061000ull, 0x800ull   },
        { 0x0509e000ull, 0x1000ull  },
        { 0x0506a000ull, 0x30000ull },
        { 0x0b200000ull, 0x300000ull},
        { 0x05040000ull, 0x10000ull },
    };
    const uint32_t n = sizeof(kForbidden) / sizeof(kForbidden[0]);
    uint32_t i;

    check(adreno_domain_forbidden_count() == ADRENO_DOMAIN_FORBIDDEN_COUNT,
          "forbidden count matches the header");
    check(n == ADRENO_DOMAIN_FORBIDDEN_COUNT,
          "test table covers every forbidden window");

    for (i = 0u; i < n; i++) {
        char label[96];
        std::snprintf(label, sizeof(label), "window %u base is forbidden", i);
        check(adreno_domain_window_is_forbidden(kForbidden[i][0], kForbidden[i][1]) == 1, label);
        std::snprintf(label, sizeof(label), "window %u last byte is forbidden", i);
        check(adreno_domain_window_is_forbidden(kForbidden[i][0] + kForbidden[i][1] - 1ull, 1ull) == 1, label);
    }

    /* Windows that merely ABUT each other must still be individually caught:
     * kgsl-3d0 ends exactly where kgsl-kgsl smmu begins (0x05040000). */
    check(adreno_domain_window_is_forbidden(0x05040000ull, 1ull) == 1,
          "address where kgsl-3d0 ends IS the kgsl smmu base, so still forbidden");

    /* Clear space between windows must NOT be forbidden. kgsl smmu ends at
     * 0x05050000 and the kgsl-3d0 dbgc window starts at 0x05061000. */
    check(adreno_domain_window_is_forbidden(0x05050000ull, 0x11000ull) == 0,
          "gap between kgsl smmu end and kgsl-3d0 dbgc is free");
    check(adreno_domain_window_is_forbidden(0x0b210000ull, 0x1000ull) == 1,
          "inside the GMU vbif window is forbidden");
    check(adreno_domain_window_is_forbidden(0x0b500000ull, 0x1000ull) == 0,
          "past the GMU vbif window (ends 0x0b500000) is free");

    check(adreno_domain_window_is_forbidden(ADRENO_DOMAIN_GPUCC_BASE, ADRENO_DOMAIN_GPUCC_SIZE) == 0,
          "GPUCC window itself is NOT forbidden");
    check(adreno_domain_window_is_forbidden(ADRENO_DOMAIN_CX_STATUS, 4ull) == 0,
          "reading the CX status register is not forbidden");
    check(adreno_domain_window_is_forbidden(ADRENO_DOMAIN_GX_STATUS, 4ull) == 0,
          "reading the GX status register is not forbidden");
    check(adreno_domain_window_is_forbidden(0x05090000ull, 0ull) == 0,
          "zero-length window is never forbidden");
}

/* 6. The GPUCC carve-out: the GMU DT node numerically swallows GPUCC, yet
 *    GPUCC must stay readable. This is the subtle one - if someone simplifies
 *    the overlap test to a plain range check, these fail. */
static void testGpuccCarveOut()
{
    check(adreno_domain_window_is_forbidden(ADRENO_DOMAIN_GPUCC_BASE, ADRENO_DOMAIN_GPUCC_SIZE) == 0,
          "whole GPUCC window is readable despite the GMU node overlapping it");
    check(adreno_domain_window_is_forbidden(ADRENO_DOMAIN_CX_STATUS, 4ull) == 0,
          "CX status read is allowed");
    check(adreno_domain_window_is_forbidden(ADRENO_DOMAIN_GX_STATUS, 4ull) == 0,
          "GX status read is allowed");
    check(adreno_domain_window_is_forbidden(ADRENO_DOMAIN_CX_GDSCR, 4ull) == 0,
          "CX gdscr read is allowed (read-only probe only)");
    /* A window that starts inside GPUCC but runs past it loses the carve-out. */
    check(adreno_domain_window_is_forbidden(ADRENO_DOMAIN_GPUCC_BASE, ADRENO_DOMAIN_GPUCC_SIZE + 4ull) == 1,
          "window straddling GPUCC's upper edge is forbidden");
    /* The GMU window minus GPUCC is still forbidden. */
    check(adreno_domain_window_is_forbidden(0x0506a000ull, 0x26000ull) == 1,
          "GMU window below GPUCC is forbidden");
    check(adreno_domain_window_is_forbidden(0x05099000ull, 0x1000ull) == 1,
          "the sliver of 'gmu' above GPUCC is forbidden");
}

/* 7. A window that spans several forbidden regions is still caught. */
static void testSpanningWindow()
{
    check(adreno_domain_window_is_forbidden(0x04ff0000ull, 0x200000ull) == 1,
          "wide window overlapping kgsl-3d0/kgsl-smmu is forbidden");
    check(adreno_domain_window_is_forbidden(0x0b000000ull, 0x400000ull) == 1,
          "wide window overlapping the GMU vbif is forbidden");
}

int main()
{
    std::printf("enchilada GPU domain-state probe design tests:\n");

    testProbeWindowBounds();
    std::printf("  testProbeWindowBounds: GPUCC window accepted, neighbours rejected\n");
    testCxStatusRegisterCorrection();
    std::printf("  testCxStatusRegisterCorrection: CX status is gds_hw_ctrl 0x1540, not 0x106c\n");
    testCxStatusDecode();
    std::printf("  testCxStatusDecode: PWR_ON_MASK BIT(31)\n");
    testGxStatusDecode();
    std::printf("  testGxStatusDecode: power-up BIT(16) / power-down BIT(15)\n");
    testForbiddenWindows();
    std::printf("  testForbiddenWindows: 6 GPU windows rejected, gaps free\n");
    testGpuccCarveOut();
    std::printf("  testGpuccCarveOut: GPUCC stays readable although the GMU node overlaps it\n");
    testSpanningWindow();
    std::printf("  testSpanningWindow: overlapping ranges caught\n");

    if (g_failures != 0u) {
        std::printf("FAIL: %u of %u checks failed\n", g_failures, g_checks);
        return 1;
    }

    std::printf("PASS: enchilada GPU domain-state probe design host tests (%u checks)\n", g_checks);
    return 0;
}
