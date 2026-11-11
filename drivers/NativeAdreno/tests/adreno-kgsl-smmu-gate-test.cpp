// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for AdrenoKGSLSmmuGate (round 53): the fail-closed decision layer
// that REFUSES a read of the enchilada GPU's dedicated kgsl SMMU while gpu_cx
// is collapsed, and encodes the upstream "clocks + domain BEFORE registers"
// rule. Freestanding: no hardware, no IOKit, no firmware. HOST-VERIFIED only.
//
// Method 1 (this file): compiles the C module and asserts board constants, the
// apps_smmu != kgsl distinction, the GPUCC-exception-does-not-apply fact, the
// five unmasked context SIDs, and EVERY refusal branch of Assess() plus the
// single Permitted path.
// Method 2 is scripts/verify-enchilada-kgsl-smmu-gate.py (source-text cross).
//
// Usage: run by scripts/check-enchilada-adreno-host.py.

#include <cstdio>
#include <cstdint>

extern "C" {
#include "AdrenoKGSLSmmuGate.h"
#include "AdrenoBoardInputs.h"
#include "AdrenoDomainState.h"
}

static unsigned g_checks = 0;
static unsigned g_failures = 0;

static void check(bool ok, const char *label)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        std::printf("  FAIL %s\n", label);
    }
}

/* Round-35/49/50/52 DEVICE-MEASURED words: cx=00005ff4 (bit31 clear),
 * gx=00088400 (bit16 set, bit15 clear). 1912 decoded rows. */
#define CX_DOWN_WORD 0x00005ff4u
#define CX_UP_WORD   0x80005ff4u

static struct AdrenoKGSLSmmuGateInputs base_inputs(void)
{
    struct AdrenoKGSLSmmuGateInputs in = {};
    in.window_base = ADRENO_KGSL_GATE_BASE;
    in.window_size = ADRENO_KGSL_GATE_SIZE;
    in.smmu_phandle = ADRENO_KGSL_GATE_PHANDLE;
    in.cx_status_word = CX_DOWN_WORD;
    in.clocks_enabled = 1u;
    in.domain_powered = 1u;
    return in;
}

static void group_board_constants()
{
    check(ADRENO_KGSL_GATE_BASE == 0x05040000ull, "kgsl SMMU base 0x05040000");
    check(ADRENO_KGSL_GATE_SIZE == 0x00010000ull, "kgsl SMMU size 0x10000");
    check(ADRENO_KGSL_GATE_PHANDLE == 0x1acu, "kgsl SMMU phandle 0x1ac");
    check(ADRENO_KGSL_GATE_IOMMU_CELLS == 1u, "kgsl SMMU 1-cell (unmasked) SIDs");
    check(ADRENO_KGSL_GATE_APPS_BASE == 0x15000000ull, "apps_smmu base 0x15000000");
    check(ADRENO_KGSL_GATE_APPS_PHANDLE == 0x29u, "apps_smmu phandle 0x29");
    check(ADRENO_BOARD_APPS_SMMU_IOMMU_CELLS == 2u, "apps_smmu 2-cell SIDs+mask");
    check(ADRENO_KGSL_GATE_BASE != ADRENO_KGSL_GATE_APPS_BASE, "two DIFFERENT units");
    check(ADRENO_KGSL_GATE_PHANDLE != ADRENO_KGSL_GATE_APPS_PHANDLE, "two DIFFERENT phandles");
    check(AdrenoKGSLSmmuGateRequiredSteps() == 0x3u, "required steps = CLOCKS|DOMAIN (both)");
    check(AdrenoKGSLSmmuGateBoardIsConsistent() == 1, "board plumbing internally consistent");
}

static void group_phandle_and_window()
{
    check(AdrenoKGSLSmmuGatePhandleIsKgsl(0x1acu) == 1, "phandle 0x1ac -> kgsl");
    check(AdrenoKGSLSmmuGatePhandleIsKgsl(0x29u) == 0, "phandle 0x29 -> NOT kgsl (apps)");
    check(AdrenoKGSLSmmuGatePhandleIsKgsl(0u) == 0, "phandle 0 -> NOT kgsl");
    check(AdrenoKGSLSmmuGateWindowIsKgsl(0x05040000ull, 0x10000ull) == 1, "exact kgsl window -> ok");
    check(AdrenoKGSLSmmuGateWindowIsKgsl(0x15000000ull, 0x80000ull) == 0, "apps_smmu window -> refuse");
    check(AdrenoKGSLSmmuGateWindowIsKgsl(0x05040000ull, 0x8000ull) == 0, "short window -> refuse");
    check(AdrenoKGSLSmmuGateWindowIsKgsl(0x05040001ull, 0x10000ull) == 0, "misaligned base -> refuse");
}

static void group_gpucc_exception()
{
    /* GPUCC status registers are on always-on rails and readable with CX down;
     * the kgsl SMMU is on gpu_cx (0x1a4) and is NOT. The exception cannot
     * transfer. Round 53's load-bearing distinction. */
    check(adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_GPUCC_BASE) == 1,
          "GPUCC base IS a probe window (always-on rail)");
    check(adreno_domain_addr_is_probe_window(ADRENO_KGSL_GATE_BASE) == 0,
          "kgsl SMMU base is NOT a GPUCC-window address");
    check(adreno_domain_window_is_forbidden(ADRENO_KGSL_GATE_BASE,
                                            ADRENO_KGSL_GATE_SIZE) == 1,
          "kgsl SMMU window IS in the collapsible-domain forbidden list");
    check(AdrenoKGSLSmmuGateGpuccExceptionDoesNotApply() == 1,
          "GPUCC always-on exception does NOT apply to kgsl SMMU");
}

static void group_context_sids()
{
    uint32_t sids[8] = {};
    unsigned i, j;
    uint32_t n = AdrenoKGSLSmmuGateContextSids(sids, 8u);
    check(n == 5u, "five GPU context SIDs enumerated");
    check(sids[0] == 0u && sids[1] == 2u && sids[2] == 1u &&
          sids[3] == 4u && sids[4] == 5u, "SIDs = {gfx3d_user 0, gfx3d_secure 2, alt 1, gmu_user 4, gmu_kernel 5}");
    for (i = 0; i < n; ++i) {
        check(sids[i] <= 5u, "SID fits 0..5 range (1-cell, no mask)");
        for (j = 0; j < i; ++j) {
            check(sids[i] != sids[j], "SIDs distinct");
        }
    }
    check(AdrenoKGSLSmmuGateContextSids(nullptr, 8u) == 0u, "NULL out -> 0 (fail-closed)");
    check(AdrenoKGSLSmmuGateContextSids(sids, 4u) == 0u, "capacity < 5 -> 0 (fail-closed)");
}

static void group_assess_invalid()
{
    struct AdrenoKGSLSmmuGateInputs in;
    check(AdrenoKGSLSmmuGateAssess(nullptr) == kAdrenoKGSLSmmuGateRefusedInvalid,
          "NULL inputs -> RefusedInvalid");
    in = base_inputs(); in.window_size = 0ull;
    check(AdrenoKGSLSmmuGateAssess(&in) == kAdrenoKGSLSmmuGateRefusedInvalid,
          "zero-size window -> RefusedInvalid");
    in = base_inputs(); in.window_base = 0xffffffffffff0000ull; in.window_size = 0x20000ull;
    check(AdrenoKGSLSmmuGateAssess(&in) == kAdrenoKGSLSmmuGateRefusedInvalid,
          "overflowing window -> RefusedInvalid");
}

static void group_assess_wrong_smmu()
{
    struct AdrenoKGSLSmmuGateInputs in = base_inputs();
    in.smmu_phandle = ADRENO_KGSL_GATE_APPS_PHANDLE; /* 0x29 */
    check(AdrenoKGSLSmmuGateAssess(&in) == kAdrenoKGSLSmmuGateRefusedWrongSmmu,
          "caller intends apps_smmu -> RefusedWrongSmmu (GPU does NOT use it)");
}

static void group_assess_wrong_window()
{
    struct AdrenoKGSLSmmuGateInputs in = base_inputs();
    in.window_base = ADRENO_KGSL_GATE_APPS_BASE;
    in.window_size = 0x80000ull;
    check(AdrenoKGSLSmmuGateAssess(&in) == kAdrenoKGSLSmmuGateRefusedWindow,
          "apps_smmu window -> RefusedWindow");
}

static void group_assess_cx_down()
{
    /* THE DEVICE-MEASURED STATE. A powered-down kgsl SMMU read may bus-abort;
     * refuse even though clocks/domain flags are hypothetically set. */
    struct AdrenoKGSLSmmuGateInputs in = base_inputs();
    in.cx_status_word = CX_DOWN_WORD;
    check(!adreno_domain_cx_is_on(CX_DOWN_WORD), "device word cx=00005ff4 decodes CX-DOWN");
    check(AdrenoKGSLSmmuGateAssess(&in) == kAdrenoKGSLSmmuGateRefusedCxDown,
          "CX-DOWN -> RefusedCxDown (the round-53 safety conclusion)");
}

static void group_assess_power_order()
{
    struct AdrenoKGSLSmmuGateInputs in;
    /* CX up, but clocks not yet enabled -> arm-smmu.c:2196 order violated. */
    in = base_inputs();
    in.cx_status_word = CX_UP_WORD;
    in.clocks_enabled = 0u;
    in.domain_powered = 1u;
    check(adreno_domain_cx_is_on(CX_UP_WORD), "hypothetical CX-UP word decodes on");
    check(AdrenoKGSLSmmuGateAssess(&in) == kAdrenoKGSLSmmuGateRefusedClocksOff,
          "clocks off -> RefusedClocksOff (upstream step 1)");
    /* Clocks on, but gpu_cx domain not resumed -> arm-smmu.c:2264 violated. */
    in.clocks_enabled = 1u;
    in.domain_powered = 0u;
    check(AdrenoKGSLSmmuGateAssess(&in) == kAdrenoKGSLSmmuGateRefusedDomainOff,
          "domain off -> RefusedDomainOff (upstream step 2)");
    /* BOTH off -> clocks refusal wins (check order is load-bearing). */
    in.clocks_enabled = 0u;
    in.domain_powered = 0u;
    check(AdrenoKGSLSmmuGateAssess(&in) == kAdrenoKGSLSmmuGateRefusedClocksOff,
          "both off -> RefusedClocksOff (clocks checked first)");
}

static void group_assess_permitted()
{
    struct AdrenoKGSLSmmuGateInputs in = base_inputs();
    in.cx_status_word = CX_UP_WORD;
    in.clocks_enabled = 1u;
    in.domain_powered = 1u;
    check(AdrenoKGSLSmmuGateAssess(&in) == kAdrenoKGSLSmmuGatePermitted,
          "ALL preconditions met (hypothetical powered GMU) -> Permitted");
    check(AdrenoKGSLSmmuGateReadPermitted(&in) == 1, "ReadPermitted wrapper agrees");
    in.domain_powered = 0u;
    check(AdrenoKGSLSmmuGateReadPermitted(&in) == 0, "ReadPermitted refuses when domain off");
}

static void group_verdict_names()
{
    const char *p = AdrenoKGSLSmmuGateVerdictName(kAdrenoKGSLSmmuGatePermitted);
    check(p != nullptr && p[0] == 'P', "Permitted name present");
    check(AdrenoKGSLSmmuGateVerdictName(kAdrenoKGSLSmmuGateRefusedCxDown)[0] == 'R',
          "RefusedCxDown name present");
    check(AdrenoKGSLSmmuGateVerdictName((AdrenoKGSLSmmuGateVerdict)99)[0] == 'R',
          "unknown verdict -> RefusedInvalid (fail-closed name)");
}

int main()
{
    group_board_constants();
    group_phandle_and_window();
    group_gpucc_exception();
    group_context_sids();
    group_assess_invalid();
    group_assess_wrong_smmu();
    group_assess_wrong_window();
    group_assess_cx_down();
    group_assess_power_order();
    group_assess_permitted();
    group_verdict_names();
    std::printf("adreno-kgsl-smmu-gate: %u checks, %u failures\n",
                g_checks, g_failures);
    if (g_failures != 0u) {
        return 1;
    }
    std::printf("PASS: enchilada kgsl SMMU read SAFETY gate (11 test groups, fail-closed)\n");
    return 0;
}
