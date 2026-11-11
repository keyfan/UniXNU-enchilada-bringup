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

static void fill(adreno_gdsc_descriptor &d, uint32_t gdscr, uint32_t hw, uint16_t flags)
{
    d.gdscr = gdscr;
    d.gds_hw_ctrl = hw;
    d.clamp_io_ctrl = 0u;
    d.flags = flags;
    d.pwrsts = (uint8_t)(ADRENO_GDSC_PWRSTS_OFF_ON);
    d.power_on_is_noop = 0u;
    d.has_supply = 0u;
    d.has_cxcs = 0u;
}

/* 2. gdsc.c:66-71 preference order: POLL_CFG_GDSCR > gds_hw_ctrl > gdscr. */
static void testStatusOffsetSelection()
{
    adreno_gdsc_descriptor d;

    /* All three candidates set: POLL_CFG_GDSCR wins. */
    fill(d, 0x2000u, 0x3000u, ADRENO_GDSC_FLAG_POLL_CFG_GDSCR);
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

    fill(d, 0x100cu, 0u, ADRENO_GDSC_FLAG_POLL_CFG_GDSCR);
    check(adreno_gdsc_derive(0x05090000ull, &d, &s) == 1, "derive succeeds for a polling descriptor");
    check(s.on_mask == (1u << 16), "polling: on mask is BIT(16) GDSC_POWER_UP_COMPLETE");
    check(s.off_mask == (1u << 15), "polling: off mask is BIT(15) GDSC_POWER_DOWN_COMPLETE");
    check(s.polls_cfg_gdscr == 1u, "polling flag recorded");

    fill(d, 0x100cu, 0x1540u, ADRENO_GDSC_FLAG_VOTABLE);
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

/* 7. Round 57: the CX enable plan is gdsc_enable()'s statement order. */
static void testCxEnablePlan()
{
    const adreno_gdsc_descriptor cx = adreno_gdsc_cx_descriptor();
    adreno_gdsc_plan plan;

    check(adreno_gdsc_plan_cpu_enable(&cx, &plan) == 1, "CX enable plan builds");
    check(plan.register_writes > 0u, "CX enable plan does write registers");
    check(plan.count <= ADRENO_GDSC_MAX_STEPS, "CX enable plan fits the fixed array");

    /* CX has no CLAMP_IO, no SW_RESET, no HW_CTRL, no RETAIN_FF_ENABLE. */
    check(adreno_gdsc_plan_has(&plan, ADRENO_GDSC_STEP_ASSERT_CLAMP_IO) == 0,
          "CX never asserts clamp IO on the way up");
    check(adreno_gdsc_plan_has(&plan, ADRENO_GDSC_STEP_DEASSERT_CLAMP_IO) == 0,
          "CX has no clamp IO to deassert");
    check(adreno_gdsc_plan_has(&plan, ADRENO_GDSC_STEP_ASSERT_RESET_AON) == 0,
          "CX has no AON reset");
    check(adreno_gdsc_plan_has(&plan, ADRENO_GDSC_STEP_HWCTRL_ON) == 0,
          "CX does not enter hardware trigger mode");

    /* The steps CX DOES take, in gdsc.c:150/163-175/177/293 order. */
    const int collapse = adreno_gdsc_plan_index_of(&plan, ADRENO_GDSC_STEP_UPDATE_COLLAPSE);
    const int hwDelay = adreno_gdsc_plan_index_of(&plan, ADRENO_GDSC_STEP_UDELAY);
    const int poll = adreno_gdsc_plan_index_of(&plan, ADRENO_GDSC_STEP_POLL_ON);
    check(collapse >= 0, "CX clears the collapse bit");
    check(poll >= 0, "CX polls for the ON state");
    check(hwDelay >= 0, "CX waits before polling (gds_hw_ctrl is set)");
    check(collapse < poll, "collapse write happens BEFORE the poll");
    check(plan.steps[collapse].value == 0u, "the enable path CLEARS SW_COLLAPSE (value 0)");
    check(plan.steps[collapse].mask == ADRENO_GDSC_SW_COLLAPSE_MASK, "collapse mask is BIT(0)");
    check(plan.steps[collapse].offset == 0x106cu, "the collapse write targets gdscr 0x106c");

    /* The poll must use the DERIVED status register and ON mask, not literals. */
    check(plan.steps[poll].offset == 0x1540u, "CX polls the derived gds_hw_ctrl 0x1540");
    check(plan.steps[poll].mask == (1u << 31), "CX polls BIT(31) PWR_ON_MASK");

    /* gdsc.c:283: CX cannot be PWRSTS_RET, and it has no cxc list, so retention
     * writes are absent -- an honest "not modelled" rather than a fake 0. */
    check(adreno_gdsc_plan_has(&plan, ADRENO_GDSC_STEP_FORCE_MEM_ON) == 0,
          "CX has no cxc list, so no retention write is planned");
}

/* 8. Round 57: the GX enable path performs ZERO GDSC register writes. */
static void testGxEnableIsNoop()
{
    const adreno_gdsc_descriptor gx = adreno_gdsc_gx_descriptor();
    adreno_gdsc_plan cpuPlan;
    adreno_gdsc_plan genericPlan;
    adreno_gdsc_plan disablePlan;

    check(adreno_gdsc_plan_cpu_enable(&gx, &cpuPlan) == 1, "GX CPU enable plan builds");
    check(cpuPlan.count == 0u, "GX CPU enable plan has NO steps at all");
    check(cpuPlan.register_writes == 0u, "GX CPU enable plan performs ZERO register writes");

    /* The generic gdsc_enable() would write registers: that contrast is the
     * whole reason gdsc_gx_do_nothing_enable() exists. */
    check(adreno_gdsc_plan_enable(&gx, &genericPlan) == 1, "generic GX enable plan builds");
    check(genericPlan.register_writes > 0u,
          "the generic path WOULD write GX registers, hence the no-op");

    /* AON reset and clamp IO come BEFORE the collapse write on that path. */
    const int aon = adreno_gdsc_plan_index_of(&genericPlan, ADRENO_GDSC_STEP_ASSERT_RESET_AON);
    const int clamp = adreno_gdsc_plan_index_of(&genericPlan, ADRENO_GDSC_STEP_DEASSERT_CLAMP_IO);
    const int collapse = adreno_gdsc_plan_index_of(&genericPlan, ADRENO_GDSC_STEP_UPDATE_COLLAPSE);
    check(aon >= 0, "GX asserts AON reset");
    check(clamp >= 0, "GX deasserts clamp IO");
    check(aon >= 0 && clamp >= 0 && collapse >= 0 && aon < clamp && clamp < collapse,
          "AON reset then clamp-IO release then collapse, in that order");
    check(genericPlan.steps[clamp].offset == 0x1508u, "clamp IO targets clamp_io_ctrl 0x1508");

    /* Disable is real for GX: it polls the down-complete bit. */
    check(adreno_gdsc_plan_disable(&gx, &disablePlan) == 1, "GX disable plan builds");
    check(disablePlan.register_writes > 0u, "GX disable DOES write registers");
    const int pollOff = adreno_gdsc_plan_index_of(&disablePlan, ADRENO_GDSC_STEP_POLL_OFF);
    check(pollOff >= 0, "GX disable polls for the OFF state");
    check(disablePlan.steps[pollOff].mask == (1u << 15), "GX disable polls BIT(15)");
    const int clampOn = adreno_gdsc_plan_index_of(&disablePlan, ADRENO_GDSC_STEP_ASSERT_CLAMP_IO);
    check(clampOn > pollOff, "clamp IO is asserted AFTER the collapse finished");

    check(adreno_gdsc_verify_gx_enable_writes_nothing() == 1,
          "the mechanical safety property holds for the pinned descriptors");
}

/* 9. Round 57: CX disable takes the VOTABLE short-cut -- no polling at all. */
static void testCxDisableDoesNotPoll()
{
    const adreno_gdsc_descriptor cx = adreno_gdsc_cx_descriptor();
    adreno_gdsc_plan plan;

    check(adreno_gdsc_plan_disable(&cx, &plan) == 1, "CX disable plan builds");
    check(adreno_gdsc_plan_has(&plan, ADRENO_GDSC_STEP_UPDATE_COLLAPSE) == 1,
          "CX sets the collapse bit on the way down");
    check(adreno_gdsc_plan_has(&plan, ADRENO_GDSC_STEP_POLL_OFF) == 0,
          "VOTABLE CX does NOT poll on disable (gdsc.c:153-161)");

    const int collapse = adreno_gdsc_plan_index_of(&plan, ADRENO_GDSC_STEP_UPDATE_COLLAPSE);
    check(collapse >= 0 && plan.steps[collapse].value == ADRENO_GDSC_SW_COLLAPSE_MASK,
          "the disable path SETS SW_COLLAPSE (value == mask)");
    check(collapse >= 0 && (collapse + 1) < (int)plan.count, "the disable plan ends with a delay");
    check(collapse >= 0 && plan.steps[collapse + 1].kind == ADRENO_GDSC_STEP_UDELAY,
          "the VOTABLE short-cut waits, then returns");
    check(collapse >= 0 && plan.steps[collapse + 1].delay_us == 500u,
          "the short-cut wait is TIMEOUT_US 500");
}

/* 10. Round 57: the plans stay comparable across descriptors and reject NULLs. */
static void testPlanHelpers()
{
    const adreno_gdsc_descriptor cx = adreno_gdsc_cx_descriptor();
    adreno_gdsc_plan plan;

    check(adreno_gdsc_plan_enable(nullptr, &plan) == 0, "NULL descriptor cannot plan an enable");
    check(adreno_gdsc_plan_enable(&cx, nullptr) == 0, "NULL output cannot receive a plan");
    check(adreno_gdsc_plan_disable(nullptr, &plan) == 0, "NULL descriptor cannot plan a disable");
    check(adreno_gdsc_plan_cpu_enable(nullptr, &plan) == 0, "NULL descriptor cannot plan a CPU enable");

    check(adreno_gdsc_plan_has(nullptr, ADRENO_GDSC_STEP_POLL_ON) == 0, "NULL plan has no steps");
    check(adreno_gdsc_plan_index_of(nullptr, ADRENO_GDSC_STEP_POLL_ON) == -1,
          "NULL plan index is -1");

    /* A real plan that simply does not contain a POLL_OFF step. */
    check(adreno_gdsc_plan_disable(&cx, &plan) == 1, "CX disable plan builds for the absent-step check");
    check(adreno_gdsc_plan_index_of(&plan, ADRENO_GDSC_STEP_POLL_OFF) == -1,
          "an absent step reports -1, not 0");
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
    testCxEnablePlan();
    std::printf("  testCxEnablePlan: CX enable follows gdsc_enable()'s statement order\n");
    testGxEnableIsNoop();
    std::printf("  testGxEnableIsNoop: the CPU's GX enable writes NOTHING\n");
    testCxDisableDoesNotPoll();
    std::printf("  testCxDisableDoesNotPoll: VOTABLE CX skips the disable poll\n");
    testPlanHelpers();
    std::printf("  testPlanHelpers: NULL handling and index sentinels\n");

    if (g_failures != 0u) {
        std::printf("FAIL: %u of %u checks failed\n", g_failures, g_checks);
        return 1;
    }

    std::printf("PASS: enchilada GPU GDSC derivation host tests (%u checks)\n", g_checks);
    return 0;
}
