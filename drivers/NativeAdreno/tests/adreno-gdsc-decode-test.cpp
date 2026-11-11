// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * adreno-gdsc-decode-test.cpp - host tests for the round-56 derivation of the
 * enchilada GPU GDSC status register and masks. Pure arithmetic; nothing here
 * touches hardware.
 *
 * The point of these tests is NOT to re-assert the four AdrenoDomainState.h
 * constants (adreno-domain-state-test.cpp already does that). It is to prove
 * that the constants FOLLOW from the pinned GDSC descriptors via the pinned
 * upstream rule, and to pin the branch behaviour of that rule.
 */

#include <cstdio>
#include <cstdint>

#include "AdrenoGdscDecode.h"
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

/* 1. The descriptors carry exactly what gpucc-sdm845.c:119-139 declares. */
static void testDescriptors()
{
    const adreno_gdsc_descriptor cx = adreno_gdsc_cx_descriptor();
    const adreno_gdsc_descriptor gx = adreno_gdsc_gx_descriptor();

    check(cx.gdscr == 0x106cu, "CX gdscr is 0x106c");
    check(cx.gds_hw_ctrl == 0x1540u, "CX gds_hw_ctrl is 0x1540");
    check(cx.flags == ADRENO_GDSC_FLAG_VOTABLE, "CX flags are VOTABLE only");
    check((cx.flags & ADRENO_GDSC_FLAG_POLL_CFG_GDSCR) == 0u, "CX does NOT set POLL_CFG_GDSCR");
    check(cx.power_on_is_noop == 0u, "CX has a real power-on");

    check(gx.gdscr == 0x100cu, "GX gdscr is 0x100c");
    check(gx.gds_hw_ctrl == 0u, "GX has no gds_hw_ctrl");
    check((gx.flags & ADRENO_GDSC_FLAG_CLAMP_IO) != 0u, "GX sets CLAMP_IO");
    check((gx.flags & ADRENO_GDSC_FLAG_AON_RESET) != 0u, "GX sets AON_RESET");
    check((gx.flags & ADRENO_GDSC_FLAG_POLL_CFG_GDSCR) != 0u, "GX sets POLL_CFG_GDSCR");
    check(gx.power_on_is_noop == 1u, "GX power-on is gdsc_gx_do_nothing_enable");
}

/* 2. gdsc.c:66-71 preference order: POLL_CFG_GDSCR > gds_hw_ctrl > gdscr. */
static void testStatusOffsetSelection()
{
    adreno_gdsc_descriptor d;

    /* All three candidates set: POLL_CFG_GDSCR wins. */
    d.gdscr = 0x2000u;
    d.gds_hw_ctrl = 0x3000u;
    d.flags = ADRENO_GDSC_FLAG_POLL_CFG_GDSCR;
    d.power_on_is_noop = 0u;
    d.reserved = 0u;
    check(adreno_gdsc_status_offset(&d) == 0x2004u, "POLL_CFG_GDSCR selects gdscr + 0x4");

    /* No POLL_CFG_GDSCR, gds_hw_ctrl set: gds_hw_ctrl wins over gdscr. */
    d.flags = 0u;
    check(adreno_gdsc_status_offset(&d) == 0x3000u, "gds_hw_ctrl beats gdscr when not polling");

    /* Neither: plain gdscr. */
    d.gds_hw_ctrl = 0u;
    check(adreno_gdsc_status_offset(&d) == 0x2000u, "gdscr is the last resort");

    check(adreno_gdsc_status_offset(nullptr) == 0u, "NULL descriptor yields offset 0");
    check(adreno_gdsc_polls_cfg_gdscr(nullptr) == 0, "NULL descriptor does not poll");
    check(adreno_gdsc_power_on_is_noop(nullptr) == 0, "NULL descriptor is not a no-op power-on");
}

/* 3. The masks follow the same branch, per gdsc.c:77-91. */
static void testMaskSelection()
{
    adreno_gdsc_descriptor d;
    adreno_gdsc_status s;

    d.gdscr = 0x100cu;
    d.gds_hw_ctrl = 0u;
    d.flags = ADRENO_GDSC_FLAG_POLL_CFG_GDSCR;
    d.power_on_is_noop = 0u;
    d.reserved = 0u;
    check(adreno_gdsc_derive(0x05090000ull, &d, &s) == 1, "derive succeeds for a polling descriptor");
    check(s.on_mask == (1u << 16), "polling: on mask is BIT(16) GDSC_POWER_UP_COMPLETE");
    check(s.off_mask == (1u << 15), "polling: off mask is BIT(15) GDSC_POWER_DOWN_COMPLETE");
    check(s.polls_cfg_gdscr == 1u, "polling flag recorded");

    d.flags = ADRENO_GDSC_FLAG_VOTABLE;
    d.gds_hw_ctrl = 0x1540u;
    check(adreno_gdsc_derive(0x05090000ull, &d, &s) == 1, "derive succeeds for a non-polling descriptor");
    check(s.on_mask == (1u << 31), "non-polling: on mask is BIT(31) PWR_ON_MASK");
    check(s.off_mask == ADRENO_GDSC_NO_OFF_MASK, "non-polling: no dedicated off mask");
    check(s.polls_cfg_gdscr == 0u, "non-polling flag recorded");

    check(adreno_gdsc_derive(0x05090000ull, nullptr, &s) == 0, "NULL descriptor cannot be derived");
    check(adreno_gdsc_derive(0x05090000ull, &d, nullptr) == 0, "NULL output cannot be derived");
}

/* 4. THE CORE RESULT: both SDM845 descriptors re-derive the AdrenoDomainState.h
 *    constants. If either side is edited by hand, this fails. */
static void testConstantsAreDerived()
{
    const adreno_gdsc_descriptor cxDesc = adreno_gdsc_cx_descriptor();
    const adreno_gdsc_descriptor gxDesc = adreno_gdsc_gx_descriptor();
    adreno_gdsc_status cx;
    adreno_gdsc_status gx;

    check(adreno_gdsc_derive(ADRENO_DOMAIN_GPUCC_BASE, &cxDesc, &cx) == 1,
          "CX derivation succeeds");
    check(adreno_gdsc_derive(ADRENO_DOMAIN_GPUCC_BASE, &gxDesc, &gx) == 1,
          "GX derivation succeeds");

    check(cx.status_address == ADRENO_DOMAIN_CX_STATUS, "CX status address 0x05091540 is DERIVED");
    check(cx.status_offset == ADRENO_DOMAIN_CX_GDS_HW_CTRL, "CX status offset is gds_hw_ctrl 0x1540");
    check(cx.on_mask == (1u << ADRENO_DOMAIN_CX_ON_BIT), "CX on mask is ADRENO_DOMAIN_CX_ON_BIT");
    check(cx.status_address == 0x05091540ull, "CX status literal is 0x05091540");

    check(gx.status_address == ADRENO_DOMAIN_GX_STATUS, "GX status address 0x05091010 is DERIVED");
    check(gx.status_offset == 0x1010u, "GX status offset is gdscr 0x100c + 0x4");
    check(gx.on_mask == (1u << ADRENO_DOMAIN_GX_ON_BIT), "GX on mask is ADRENO_DOMAIN_GX_ON_BIT");
    check(gx.off_mask == (1u << ADRENO_DOMAIN_GX_OFF_BIT), "GX off mask is ADRENO_DOMAIN_GX_OFF_BIT");
    check(gx.status_address == 0x05091010ull, "GX status literal is 0x05091010");

    check(adreno_gdsc_verify_domain_state_constants() == 1,
          "all five AdrenoDomainState.h constants re-derive from the pinned descriptors");
}

/* 5. Decoding is consistent with the existing predicates and with the device
 *    words the earlier rounds measured (reused, not re-measured). */
static void testDecodeAgreesWithDomainState()
{
    const adreno_gdsc_descriptor cx = adreno_gdsc_cx_descriptor();
    const adreno_gdsc_descriptor gx = adreno_gdsc_gx_descriptor();
    const uint32_t cxDown = 0x00005ff4u; /* rounds 35/49/50/52 */
    const uint32_t gxDown = 0x00088400u; /* rounds 35/49/50/52 */

    check(adreno_gdsc_is_on(&cx, 0x80000000u) == 1, "CX BIT(31) -> on");
    check(adreno_gdsc_is_on(&cx, 0x00000000u) == 0, "CX zero -> not on");
    check(adreno_gdsc_is_off(&cx, 0x00000000u) == 1, "CX zero -> off (inverse of PWR_ON_MASK)");
    check(adreno_gdsc_is_off(&cx, 0x80000000u) == 0, "CX BIT(31) -> not off");
    check(adreno_gdsc_is_on(&cx, cxDown) == 0, "measured CX word 0x00005ff4 reads as ON=0");
    check(adreno_gdsc_is_off(&cx, cxDown) == 1, "measured CX word 0x00005ff4 reads as OFF=1");

    check(adreno_gdsc_is_on(&gx, 1u << 16) == 1, "GX BIT(16) -> on");
    check(adreno_gdsc_is_off(&gx, 1u << 15) == 1, "GX BIT(15) -> off");
    check(adreno_gdsc_is_on(&gx, 0u) == 0 && adreno_gdsc_is_off(&gx, 0u) == 0,
          "GX zero reports neither complete");

    /* The measured GX word has BIT(15) set and BIT(16) clear: down-complete, and
     * that is EXPECTED because the CPU never powers GX on. */
    check((gxDown & (1u << 16)) == 0u, "measured GX word has power-up bit clear");
    check((gxDown & (1u << 15)) != 0u, "measured GX word has power-down bit set");
    check(adreno_gdsc_is_off(&gx, gxDown) == 1, "measured GX word 0x00088400 reads as OFF=1");
    check(adreno_gdsc_power_on_is_noop(&gx) == 1,
          "and that off state is EXPECTED: GX power-on is a no-op for the CPU");

    /* Cross-check against the pre-existing predicates so the two modules can
     * never drift apart silently. */
    check(adreno_gdsc_is_on(&cx, cxDown) == adreno_domain_cx_is_on(cxDown),
          "CX decode agrees with adreno_domain_cx_is_on");
    check(adreno_gdsc_is_on(&gx, gxDown) == adreno_domain_gx_is_on(gxDown),
          "GX decode agrees with adreno_domain_gx_is_on");
    check(adreno_gdsc_is_off(&gx, gxDown) == adreno_domain_gx_is_off(gxDown),
          "GX decode agrees with adreno_domain_gx_is_off");
}

/* 6. Both status registers must fall inside the readable GPUCC window: the whole
 *    derivation is useless if the address is outside the probe window. */
static void testDerivedAddressesAreReadable()
{
    const adreno_gdsc_descriptor cxDesc = adreno_gdsc_cx_descriptor();
    const adreno_gdsc_descriptor gxDesc = adreno_gdsc_gx_descriptor();
    adreno_gdsc_status cx;
    adreno_gdsc_status gx;

    check(adreno_gdsc_derive(ADRENO_DOMAIN_GPUCC_BASE, &cxDesc, &cx) == 1, "CX derive");
    check(adreno_gdsc_derive(ADRENO_DOMAIN_GPUCC_BASE, &gxDesc, &gx) == 1, "GX derive");

    check(adreno_domain_addr_is_probe_window(cx.status_address) == 1, "CX status is inside GPUCC");
    check(adreno_domain_addr_is_probe_window(gx.status_address) == 1, "GX status is inside GPUCC");
    check(adreno_domain_window_is_forbidden(cx.status_address, 4ull) == 0, "CX status read is not forbidden");
    check(adreno_domain_window_is_forbidden(gx.status_address, 4ull) == 0, "GX status read is not forbidden");
}

int main()
{
    std::printf("enchilada GPU GDSC derivation tests:\n");

    testDescriptors();
    std::printf("  testDescriptors: gpucc-sdm845.c descriptors carried exactly\n");
    testStatusOffsetSelection();
    std::printf("  testStatusOffsetSelection: gdsc.c:66-71 preference order\n");
    testMaskSelection();
    std::printf("  testMaskSelection: gdsc.c:77-91 mask branches\n");
    testConstantsAreDerived();
    std::printf("  testConstantsAreDerived: AdrenoDomainState.h constants are DERIVED, not asserted\n");
    testDecodeAgreesWithDomainState();
    std::printf("  testDecodeAgreesWithDomainState: decode agrees with the existing predicates\n");
    testDerivedAddressesAreReadable();
    std::printf("  testDerivedAddressesAreReadable: both derived registers stay inside GPUCC\n");

    if (g_failures != 0u) {
        std::printf("FAIL: %u of %u checks failed\n", g_failures, g_checks);
        return 1;
    }

    std::printf("PASS: enchilada GPU GDSC derivation host tests (%u checks)\n", g_checks);
    return 0;
}
