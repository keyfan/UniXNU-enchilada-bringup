// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for AdrenoClkCtlV1 -- the GPUCC CLOCK-CONTROL half of the A630
// power-up plan, derived from sha256-pinned Linux v6.12 clock sources.
//
// Method 1 of 2 (method 2 is scripts/verify-enchilada-clk-ctl-v1.py, which
// re-derives the SAME facts in Python straight from the pinned bytes plus the
// DTB, and never compiles this C module).
//
// What this test is really defending:
//   * the "hub" step is a provable NO-OP on this board, not a rate;
//   * clk_set_rate() before clk_bulk_prepare_enable() means the resume-time
//     core-rate write is PARKED, so the first GPUCC CFG write happens inside
//     the enable leg -- a backend that "fixes" this by writing CFG during
//     set_rate would be inventing traffic upstream does not make;
//   * only 2 of the 4 GMU clocks are GPUCC registers, so a plan that claims 4
//     is pointed at the wrong node;
//   * the branch halt check is a DISJUNCTION, so collapsing it to one mask
//     would silently accept a stalled clock.
//
// Build: compiled as C++17 and linked against AdrenoClkCtlV1.c
// (see scripts/check-enchilada-adreno-host.py).

extern "C" {
#include "AdrenoClkCtlV1.h"
}

#include <cstdint>
#include <cstdio>

namespace {

unsigned g_checks = 0;
unsigned g_failures = 0;

void check(bool cond, const char *what)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        std::printf("FAIL: %s\n", what);
    }
}

// Count the steps of one kind in a plan.
unsigned count_kind(const struct adreno_clk_ctl_plan &p, unsigned kind)
{
    unsigned n = 0;
    for (unsigned i = 0; i < p.count; i++) {
        if (p.steps[i].kind == kind) {
            n++;
        }
    }
    return n;
}

// Every step that claims to write must carry an in-window offset. PARK_CFG is
// the one kind that legitimately has no address at all.
bool offsets_in_window(const struct adreno_clk_ctl_plan &p)
{
    for (unsigned i = 0; i < p.count; i++) {
        if (p.steps[i].kind == ADRENO_CLK_CTL_STEP_PARK_CFG) {
            if (p.steps[i].writes != 0) {
                return false;
            }
            continue;
        }
        if (p.steps[i].offset + 4u > ADRENO_CLK_CTL_V1_GPUCC_MAX_REG + 4u) {
            return false;
        }
        if (p.steps[i].offset % 4u != 0u) {
            return false;
        }
    }
    return true;
}

// ---- group 1: the frequency table and CFG encoding ------------------------
void testFreqTable()
{
    std::uint32_t n = 0;
    const struct adreno_clk_ctl_freq *t = AdrenoClkCtlV1FreqTable(&n);
    check(t != nullptr, "the frequency table has an address");
    check(n == 3u, "gpucc-sdm845.c lists exactly three rows");

    // The F() macro stores 2*h-1, so pre_div is NOT the raw h from the source.
    // Row 1 uses h = 1   -> pre_div 1.
    // Row 2 uses h = 1.5 -> pre_div 2.  <-- the one that is easy to get wrong.
    // Row 3 uses h = 1   -> pre_div 1.
    check(t[0].pre_div == 1u, "19.2 MHz row has pre_div 2*1-1 = 1");
    check(t[1].pre_div == 2u, "200 MHz row has pre_div 2*1.5-1 = 2, not 1.5");
    check(t[2].pre_div == 1u, "500 MHz row has pre_div 2*1-1 = 1");

    // parent_map_0's .cfg values, which are NOT the enum indices.
    check(t[0].src_cfg == 0u, "bi_tcxo maps to cfg 0");
    check(t[1].src_cfg == 6u, "gcc_gpu_gpll0_div_clk_src maps to cfg 6");
    check(t[2].src_cfg == 3u, "gpu_cc_pll1 maps to cfg 3 (enum index 2)");

    struct adreno_clk_ctl_freq f{};
    check(AdrenoClkCtlV1SelectFreq(200000000u, &f) == 1 && f.freq_hz == 200000000u,
          "an exact rate selects its own row");
    check(AdrenoClkCtlV1SelectFreq(1u, &f) == 1 && f.freq_hz == 19200000u,
          "a rate below the table selects the first row");
    check(AdrenoClkCtlV1SelectFreq(999999999u, &f) == 1 && f.freq_hz == 500000000u,
          "a rate above the table selects the LAST row (qcom_find_freq f-1)");
    check(AdrenoClkCtlV1SelectFreq(300000000u, &f) == 1 && f.freq_hz == 500000000u,
          "a rate between rows selects the next HIGHER row, not the lower one");

    // The one CFG word the resume path produces, computed by hand from the
    // field layout: (pre_div << 0) | (cfg << 8) = 2 | 0x600 = 0x0602.
    struct adreno_clk_ctl_freq r{};
    (void)AdrenoClkCtlV1SelectFreq(200000000u, &r);
    check(AdrenoClkCtlV1CfgWord(&r) == 0x0602u, "resume CFG word is exactly 0x0602");
    check((AdrenoClkCtlV1CfgWord(&r) & ADRENO_CLK_CTL_V1_CFG_MODE_MASK) == 0u,
          "no DUAL_EDGE mode bit (mnd_width is 0)");
    check((AdrenoClkCtlV1CfgWord(&r) & ADRENO_CLK_CTL_V1_CFG_HW_CLK_CTRL) == 0u,
          "no HW_CLK_CTRL bit for this RCG");
    check(AdrenoClkCtlV1CfgWord(nullptr) == 0u, "a NULL row yields no CFG word");

    std::printf("clk-ctl-v1 frequency table: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 2: SET_CORE_RATE is the PARKED path at resume time ------------
void testSetCoreRateParked()
{
    struct adreno_clk_ctl_plan p{};

    // This is the load-bearing finding. a6xx_gmu_resume() calls
    // clk_set_rate(:1044) BEFORE clk_bulk_prepare_enable(:1047), so the shared
    // RCG is still disabled and clk_rcg2_shared_set_rate() takes the
    // !clk_hw_is_enabled() branch: cache, write nothing.
    check(AdrenoClkCtlV1PlanSetCoreRate(200000000u, 0, &p) == 0,
          "the disabled-RCG plan builds");
    check(p.count == 1u, "the disabled-RCG plan is ONE step");
    check(p.register_writes == 0u, "and that step writes ZERO registers");
    check(p.steps[0].kind == ADRENO_CLK_CTL_STEP_PARK_CFG,
          "the single step is PARK_CFG");
    check(p.steps[0].value == 0x0602u, "the parked word is the derived CFG word");
    check(count_kind(p, ADRENO_CLK_CTL_STEP_REG_WRITE) == 0u,
          "no CFG register write happens on this path");

    // The other branch: an ENABLED RCG does real work.
    check(AdrenoClkCtlV1PlanSetCoreRate(200000000u, 1, &p) == 0,
          "the enabled-RCG plan builds");
    check(p.count == 6u, "the enabled-RCG plan is six steps");
    check(p.register_writes == 4u, "four of them write a register");
    check(count_kind(p, ADRENO_CLK_CTL_STEP_POLL_CLEAR) == 2u,
          "two of them poll (force-enable and CMD_UPDATE)");
    check(p.steps[0].offset == ADRENO_CLK_CTL_V1_RCG_CMD_RCGR,
          "the first write lands on the CMD register");
    check(p.steps[0].value == ADRENO_CLK_CTL_V1_CMD_ROOT_EN,
          "and sets CMD_ROOT_EN");
    check(p.steps[1].kind == ADRENO_CLK_CTL_STEP_POLL_CLEAR &&
          p.steps[1].mask == ADRENO_CLK_CTL_V1_CMD_ROOT_OFF,
          "force-enable polls CMD_ROOT_OFF clear");
    check(p.steps[2].kind == ADRENO_CLK_CTL_STEP_REG_WRITE &&
          p.steps[2].offset == ADRENO_CLK_CTL_V1_RCG_CMD_RCGR + ADRENO_CLK_CTL_V1_CFG_REG,
          "then CFG is written");
    check(p.steps[3].kind == ADRENO_CLK_CTL_STEP_REG_UPDATE_BITS &&
          p.steps[3].mask == ADRENO_CLK_CTL_V1_CMD_UPDATE,
          "then CMD_UPDATE is raised");
    check(p.steps[4].kind == ADRENO_CLK_CTL_STEP_POLL_CLEAR &&
          p.steps[4].mask == ADRENO_CLK_CTL_V1_CMD_UPDATE,
          "and CMD_UPDATE is polled clear");
    check(p.steps[5].kind == ADRENO_CLK_CTL_STEP_REG_UPDATE_BITS &&
          p.steps[5].value == 0u, "finally force-enable is cleared");
    check(offsets_in_window(p), "every step of the enabled plan is in the GPUCC window");

    // The PARKED plan and the ENABLED plan are genuinely different code paths,
    // which is the whole reason this module exists.
    struct adreno_clk_ctl_plan parked{};
    (void)AdrenoClkCtlV1PlanSetCoreRate(200000000u, 0, &parked);
    check(parked.count != p.count, "parked and enabled plans differ in LENGTH");
    check(parked.register_writes != p.register_writes,
          "parked and enabled plans differ in WRITES");

    std::printf("clk-ctl-v1 core-rate parked/enabled: %u checks, %u failures\n",
                g_checks, g_failures);
}

// ---- group 3: the hub no-op ------------------------------------------------
void testHubIsNoop()
{
    // There is no "hub" clock on SDM845. The A740 SM8550 GMU node DOES list
    // one, so this is a generation fact rather than an omission.
    check(AdrenoClkCtlV1HubClockPresent() == 0, "no hub clock exists on this board");
    check(AdrenoClkCtlV1HubSetRateIsNoop() == 1,
          "clk_set_rate(NULL hub) is a SUCCESSFUL no-op (clk.c NULL guard)");
    check(AdrenoClkCtlV1HubStepWritesNothing() == 1,
          "therefore the SET_HUB_RATE step writes no register");

    // And it stays consistent: if a hub clock ever appeared, the two
    // predicates above would have to flip together.
    check(AdrenoClkCtlV1HubStepWritesNothing() ==
              (AdrenoClkCtlV1HubClockPresent() == 0 ? 1 : 0),
          "hub step writes nothing iff the hub clock is absent");

    // The rate VALUE the round-51 model reports is still the upstream ternary;
    // it is simply never programmed here. 150 MHz for A630.
    check(ADRENO_CLK_CTL_V1_GPUCC_BASE == 0x05090000u, "GPUCC base is the DTB address");
    check(ADRENO_CLK_CTL_V1_GPUCC_SIZE == 0x9000u, "GPUCC size is the DTB size");

    std::printf("clk-ctl-v1 hub no-op: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 4: only 2 of the 4 GMU clocks are GPUCC ------------------------
void testClockCounts()
{
    check(AdrenoClkCtlV1GmuClockCount() == 4u, "the GMU node names four clocks");
    check(AdrenoClkCtlV1GpuccClockCount() == 2u,
          "only two of them are GPUCC registers (gmu, cxo)");
    check(AdrenoClkCtlV1GpuccClockCount() < AdrenoClkCtlV1GmuClockCount(),
          "the two counts must stay DISTINCT so they cannot be conflated");
    // axi = GCC_DDRSS_GPU_AXI_CLK and memnoc = GCC_GPU_MEMNOC_GFX_CLK are GCC
    // registers, outside the window this module bounds itself to.
    check(AdrenoClkCtlV1GpuccClockCount() * 2u == AdrenoClkCtlV1GmuClockCount(),
          "exactly half of the bulk set is in GPUCC");

    std::printf("clk-ctl-v1 clock counts: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 5: RCG enable leg and fail-closed CFG acceptance --------------
void testRcgEnable()
{
    struct adreno_clk_ctl_plan p{};

    check(AdrenoClkCtlV1PlanRcgEnable(0x0602u, &p) == 0, "a derived CFG word is accepted");
    check(p.count == 6u && p.register_writes == 4u,
          "the RCG enable leg is 4 writes over 6 steps");
    check(p.steps[2].value == 0x0602u, "it writes back the parked word");

    // Every word the table can produce must be accepted...
    for (std::uint32_t i = 0; i < 3u; i++) {
        std::uint32_t n = 0;
        const struct adreno_clk_ctl_freq *t = AdrenoClkCtlV1FreqTable(&n);
        const std::uint32_t w = AdrenoClkCtlV1CfgWord(&t[i]);
        check(AdrenoClkCtlV1PlanRcgEnable(w, &p) == 0, "each table word is accepted");
    }

    // ...and a word it cannot produce is REFUSED. 0x0612 is 0x0602 with a
    // stray HW_CLK_CTRL bit, which the generic configure path would preserve
    // but which this RCG's table never sets.
    check(AdrenoClkCtlV1PlanRcgEnable(0x0612u, &p) != 0,
          "a CFG word with a stray HW_CLK_CTRL bit is refused");
    check(AdrenoClkCtlV1PlanRcgEnable(0x0u, &p) != 0,
          "a CFG word the table cannot produce is refused");
    check(AdrenoClkCtlV1PlanRcgEnable(0xffffffffu, &p) != 0,
          "an all-ones CFG word is refused");
    check(AdrenoClkCtlV1PlanRcgEnable(0x0602u, nullptr) != 0, "a NULL out is refused");

    std::printf("clk-ctl-v1 rcg enable: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 6: branch enable + the DISJUNCTION halt poll ------------------
void testBranchEnable()
{
    struct adreno_clk_ctl_plan p{};

    check(AdrenoClkCtlV1PlanBranchEnable(ADRENO_CLK_CTL_V1_CX_GMU_CBCR, &p) == 0,
          "the CX_GMU branch plan builds");
    check(p.count == 3u && p.register_writes == 2u,
          "it is two writes plus one poll");
    check(p.steps[0].offset == ADRENO_CLK_CTL_V1_CX_GMU_CBCR,
          "step 0 is on the CX_GMU CBCR");
    check(p.steps[0].mask == ADRENO_CLK_CTL_V1_CBCR_WS_MASK &&
          p.steps[0].value == ADRENO_CLK_CTL_V1_CBCR_WS_VALUE,
          "step 0 applies the probe-time wake/sleep recommendation");
    check(ADRENO_CLK_CTL_V1_CBCR_WS_MASK == 0x0ff0u,
          "wake 0xf<<8 | sleep 0xf<<4 is 0x0ff0, not 0x0f00");
    check(p.steps[1].mask == ADRENO_CLK_CTL_V1_CBCR_CLOCK_ENABLE &&
          p.steps[1].value == ADRENO_CLK_CTL_V1_CBCR_CLOCK_ENABLE,
          "step 1 sets CBCR_CLOCK_ENABLE");

    // The halt check is `(val & CLK_OFF) == 0 || FSM == ON`. Modelling it as a
    // single POLL_CLEAR would drop the FSM acceptance path and could report a
    // healthy clock as stalled.
    check(p.steps[2].kind == ADRENO_CLK_CTL_STEP_POLL_BRANCH2_ON,
          "the halt check is its own DISJUNCTION kind");
    check(p.steps[2].kind != ADRENO_CLK_CTL_STEP_POLL_CLEAR,
          "it is not a plain mask poll");
    check(p.steps[2].mask == ADRENO_CLK_CTL_V1_CBCR_CLK_OFF,
          "the first acceptance path tests CBCR_CLK_OFF");
    check(p.steps[2].value == ADRENO_CLK_CTL_V1_FSM_STATUS_ON,
          "the second acceptance path tests the NOC FSM");
    check(p.steps[2].timeout_us == 200u, "the halt budget is 200 x 1 us");

    // The CXO branch is also modelled; a GCC register is not.
    check(AdrenoClkCtlV1PlanBranchEnable(ADRENO_CLK_CTL_V1_CXO_CBCR, &p) == 0,
          "the CXO branch plan builds");
    check(AdrenoClkCtlV1PlanBranchEnable(0x1440u, &p) != 0,
          "a GCC CBCR is refused, so the module cannot wander outside GPUCC");
    check(AdrenoClkCtlV1PlanBranchEnable(ADRENO_CLK_CTL_V1_CX_GMU_CBCR, nullptr) != 0,
          "a NULL out is refused");

    std::printf("clk-ctl-v1 branch enable: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 7: the whole plan composes with round 51's step list ----------
void testComposition()
{
    // Round 51 emits five ops in the order CX -> GX -> core rate -> hub rate
    // -> clocks. This module supplies the CONTENT of the three clock ops:
    //   SET_CORE_RATE  (the parked plan, 0 writes at resume)
    //   SET_HUB_RATE   (provable no-op)
    //   CLOCKS_ENABLE  (RCG enable leg + branch enables)
    // Summing the writes a backend must actually perform is therefore:
    struct adreno_clk_ctl_plan core{};
    struct adreno_clk_ctl_plan rcg{};
    struct adreno_clk_ctl_plan br{};
    (void)AdrenoClkCtlV1PlanSetCoreRate(200000000u, 0, &core);
    (void)AdrenoClkCtlV1PlanRcgEnable(0x0602u, &rcg);
    (void)AdrenoClkCtlV1PlanBranchEnable(ADRENO_CLK_CTL_V1_CX_GMU_CBCR, &br);

    const unsigned total = core.register_writes + rcg.register_writes + br.register_writes;
    check(core.register_writes == 0u, "the core-rate leg adds no writes at resume");
    check(total == 6u, "the clock legs perform exactly six GPUCC writes in total");
    check(AdrenoClkCtlV1HubStepWritesNothing() == 1,
          "the hub leg contributes nothing, or the total would be seven");

    // The parked word the set_rate leg caches is the SAME word the enable leg
    // writes back. If these ever diverged the RCG would come up at the wrong
    // frequency, silently.
    check(core.steps[0].value == rcg.steps[2].value,
          "parked_cfg flows unchanged from set_rate into the CFG write");

    std::printf("clk-ctl-v1 composition: %u checks, %u failures\n", g_checks, g_failures);
}

// ---- group 8: the module's own self-check --------------------------------
void testVerify()
{
    std::uint32_t checks = 0;
    check(AdrenoClkCtlV1Verify(&checks) == 0, "AdrenoClkCtlV1Verify passes");
    check(checks >= 20u, "AdrenoClkCtlV1Verify ran its full check list");

    std::printf("clk-ctl-v1 verify: %u checks, %u failures\n", g_checks, g_failures);
}

} // namespace

int main()
{
    testFreqTable();
    testSetCoreRateParked();
    testHubIsNoop();
    testClockCounts();
    testRcgEnable();
    testBranchEnable();
    testComposition();
    testVerify();

    if (g_failures != 0u) {
        std::printf("FAIL: enchilada Adreno clock-control host tests (%u checks, %u failures)\n",
                    g_checks, g_failures);
        return 1;
    }
    std::printf("PASS: enchilada Adreno clock-control host tests (%u checks)\n", g_checks);
    return 0;
}
