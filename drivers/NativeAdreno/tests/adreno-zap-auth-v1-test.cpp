// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoZapAuthV1 host test: 9 groups, CHECK() accumulates failures.
// Groups 1-7 are the round-73 shape. Groups 8-9 are ROUND 106: the three-stage
// ordered PAS contract and the derived SMC base.
#include <cstdint>
#include <cstdio>
#include "AdrenoZapAuthV1.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testPasId()
{
    /* GPU_PAS_ID 13, derived in round 69 and DEVICE-confirmed in round 71. */
    CHECK(adreno_zap_pas_id() == 13u);
    CHECK(adreno_zap_pas_id() == kAdrenoZapPasId);
    CHECK(0x0du == 13u);   /* the DTS encoding */
}

static void testScmShape()
{
    uint32_t svc = 0, cmd = 0, argc = 0, owner = 0;
    /* qcom_scm.c:703-731: SVC_PIL, PAS_AUTH_AND_RESET, ARGS(1), SIP owner. */
    CHECK(adreno_zap_scm_cmd(2u, &svc, &cmd, &argc, &owner) == 0);
    CHECK(svc == 0x02u);
    CHECK(svc == kAdrenoZapScmSvcPil);
    CHECK(cmd == 0x05u);
    CHECK(cmd == kAdrenoZapScmPasAuthAndReset);
    CHECK(argc == 1u);
    CHECK(owner == kAdrenoZapScmOwnerSip);
    /* The other four operations share the service and the arity. */
    const uint32_t want[5] = {0x01u, 0x02u, 0x05u, 0x06u, 0x07u};
    for (uint32_t i = 0; i < 5u; ++i) {
        CHECK(adreno_zap_scm_cmd(i, &svc, &cmd, &argc, &owner) == 0);
        CHECK(cmd == want[i]);
        CHECK(svc == 0x02u);
        CHECK(argc == 1u);
        CHECK(owner == kAdrenoZapScmOwnerSip);
    }
    CHECK(kAdrenoZapScmPasInitImage == 0x01u);
    CHECK(kAdrenoZapScmPasMemSetup == 0x02u);
    CHECK(kAdrenoZapScmPasShutdown == 0x06u);
    CHECK(kAdrenoZapScmPasIsSupported == 0x07u);
    /* Refusals. */
    CHECK(adreno_zap_scm_cmd(5u, &svc, &cmd, &argc, &owner) == 1);
    CHECK(adreno_zap_scm_cmd(0u, nullptr, &cmd, &argc, &owner) == 1);
    CHECK(adreno_zap_scm_cmd(0u, &svc, &cmd, &argc, nullptr) == 1);
    /* Names. */
    CHECK(adreno_zap_scm_op_name(2)[0] == 'p');
    CHECK(adreno_zap_scm_op_name(9)[0] == 'u');
}

static void testChain()
{
    struct adreno_zap_step_state st[kAdrenoZapStepCount];
    uint32_t count = 0;
    CHECK(adreno_zap_chain(st, kAdrenoZapStepCount, &count) == 0);
    CHECK(count == 8u);
    CHECK(kAdrenoZapStepCount == 8u);
    /* The order is the source order at adreno_gpu.c:43-156. */
    CHECK(st[0].step == kAdrenoZapStepArchQcom);
    CHECK(st[1].step == kAdrenoZapStepZapShaderNode);
    CHECK(st[2].step == kAdrenoZapStepMemoryRegion);
    CHECK(st[3].step == kAdrenoZapStepAddressToRes);
    CHECK(st[4].step == kAdrenoZapStepFirmwareName);
    CHECK(st[5].step == kAdrenoZapStepMdtSize);
    CHECK(st[6].step == kAdrenoZapStepMdtLoad);
    CHECK(st[7].step == kAdrenoZapStepAuthAndReset);
    for (uint32_t i = 0; i < count; ++i) CHECK(st[i].step == i);
    /* EXACTLY ONE is present, and it is firmware-name (the live DT read). */
    uint32_t present = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (st[i].present) { ++present; CHECK(st[i].step == kAdrenoZapStepFirmwareName); }
    }
    CHECK(present == 1u);
    CHECK(adreno_zap_present_count() == 1u);
    /* Seven of eight are reachable; CONFIG_ARCH_QCOM is not, because XNU is not
     * Linux and there is no such Kconfig to enable. */
    CHECK(st[kAdrenoZapStepArchQcom].reachable == 0u);
    uint32_t reach = 0;
    for (uint32_t i = 0; i < count; ++i) if (st[i].reachable) ++reach;
    CHECK(reach == 7u);
    /* Readiness is derived and must be 0 today. */
    CHECK(adreno_zap_auth_ready() == 0);
    CHECK(adreno_zap_auth_ready() ==
          (adreno_zap_present_count() == kAdrenoZapStepCount ? 1 : 0));
    /* Refusals. */
    struct adreno_zap_step_state tiny[kAdrenoZapStepCount - 1u];
    count = 99;
    CHECK(adreno_zap_chain(tiny, kAdrenoZapStepCount - 1u, &count) == 1);
    CHECK(count == 0u);
    CHECK(adreno_zap_chain(nullptr, kAdrenoZapStepCount, &count) == 1);
}

static void testStepNames()
{
    for (uint32_t i = 0; i < kAdrenoZapStepCount; ++i) {
        const char *n = adreno_zap_step_name(i);
        const char *a = adreno_zap_step_anchor(i);
        CHECK(n != nullptr && n[0] != '\0');
        CHECK(a != nullptr && a[0] != '\0');
        CHECK(!(n[0] == 'u' && n[1] == 'n'));
        for (uint32_t j = 0; j < i; ++j) {
            const char *m = adreno_zap_step_name(j);
            bool same = true;
            uint32_t k = 0;
            while (n[k] || m[k]) { if (n[k] != m[k]) { same = false; break; } ++k; }
            CHECK(!same);
        }
    }
    CHECK(adreno_zap_step_name(kAdrenoZapStepCount)[0] == 'u');
    CHECK(adreno_zap_step_anchor(kAdrenoZapStepCount)[0] == 'u');
}

static void testResultSemantics()
{
    /* qcom_scm.c:729 `return ret ?: res.result[0];` -- the trap. */
    int ok = -1;
    CHECK(adreno_zap_result_semantics(0, 0u, &ok) == 0);
    CHECK(ok == 1);   /* transport 0 AND PCTX 0 */
    CHECK(adreno_zap_result_semantics(0, 1u, &ok) == 0);
    CHECK(ok == 0);   /* SCM completed, secure world REJECTED -> failure */
    CHECK(adreno_zap_result_semantics(0, 0xdeadbeefu, &ok) == 0);
    CHECK(ok == 0);
    CHECK(adreno_zap_result_semantics(-1, 0u, &ok) == 0);
    CHECK(ok == 0);   /* transport failed */
    CHECK(adreno_zap_result_semantics(0, 0u, nullptr) == 1);
}

static void testUnsupportedFallback()
{
    /* adreno_gpu.c:158-163: -EOPNOTSUPP is a QUIET fallback, not an error. */
    CHECK(adreno_zap_unsupported_is_quiet(-95) == 1);
    CHECK(adreno_zap_unsupported_is_quiet(0) == 0);
    CHECK(adreno_zap_unsupported_is_quiet(-13) == 0);  /* -EACCES is real */
    CHECK(adreno_zap_unsupported_is_quiet(-1) == 0);
}

static void testSelfCheck()
{
    uint32_t count = 0;
    CHECK(adreno_zap_auth_verify(&count) == 0);
    CHECK(count >= 55u);
    CHECK(adreno_zap_auth_verify(nullptr) == 0);
}

// ---- ROUND 106 group 8: the three-stage ordered PAS contract ----
static void testPasStages()
{
    const uint32_t base = adreno_zap_smc_base_derived();
    struct adreno_zap_pas_region region;
    region.phys = 0x1000;
    region.bytes = 0x1000;

    // The order IS the enum: init_image, mem_setup, auth_and_reset.
    CHECK(kAdrenoZapPasStageCount == 3u);
    CHECK(kAdrenoZapPasStageInitImage == 0u);
    CHECK(kAdrenoZapPasStageMemSetup == 1u);
    CHECK(kAdrenoZapPasStageAuthAndReset == 2u);

    // P3:98/99/100 the three command ids, resolved through the loader API.
    struct adreno_zap_pas_call seq[kAdrenoZapPasStageCount];
    uint32_t n = 0;
    CHECK(adreno_zap_pas_sequence(base, 1, &region, seq, kAdrenoZapPasStageCount, &n) == 0);
    CHECK(n == 3u);
    CHECK(seq[0].cmd == 0x01u && seq[1].cmd == 0x02u && seq[2].cmd == 0x05u);
    CHECK(seq[0].cmd == kAdrenoZapScmPasInitImage);
    CHECK(seq[1].cmd == kAdrenoZapScmPasMemSetup);
    CHECK(seq[2].cmd == kAdrenoZapScmPasAuthAndReset);
    // ...all in SVC_PIL (P3:97) with args[0] == the peripheral (P2:710).
    for (uint32_t i = 0; i < n; ++i) {
        CHECK(seq[i].stage == i);          // ordered by construction
        CHECK(seq[i].svc == 0x02u);
        CHECK(seq[i].args0 == 13u);
        CHECK(seq[i].owner == 2u);         // ARM_SMCCC_OWNER_SIP, NOT 1
    }

    // The arginfo is PINNED per call (P2:584 / :670 / :709), not shared, and
    // the argument count is DERIVED from its low nibble -- so the two can never
    // disagree.
    CHECK(seq[0].arginfo == 0x82u);        // ARGS(2, VAL, RW)
    CHECK(seq[1].arginfo == 0x03u);        // ARGS(3)
    CHECK(seq[2].arginfo == 0x01u);        // ARGS(1)
    CHECK(seq[0].arg_count == 2u);
    CHECK(seq[1].arg_count == 3u);
    CHECK(seq[2].arg_count == 1u);
    CHECK((seq[0].arginfo & 0xFu) == seq[0].arg_count);
    CHECK((seq[1].arginfo & 0xFu) == seq[1].arg_count);
    CHECK((seq[2].arginfo & 0xFu) == seq[2].arg_count);
    // The round-73 ARGS(1) shape survives as its own constant.
    CHECK(kAdrenoZapScmArgCount == 1u);
    CHECK(seq[2].arginfo == kAdrenoZapScmArgCount);

    // Exactly ONE stage carries the metadata blob and exactly ONE carries a
    // region size -- the asymmetry that makes a shared shape wrong.
    CHECK(seq[0].is_metadata_call == 1 && seq[1].is_metadata_call == 0 &&
          seq[2].is_metadata_call == 0);
    CHECK(seq[0].carries_size_arg == 0 && seq[1].carries_size_arg == 1 &&
          seq[2].carries_size_arg == 0);

    // STAGE 1 IS MANDATORY HERE because the real blob is relocatable. The
    // requirement is COMPUTED from the flag, and both branches are exercised.
    CHECK(adreno_zap_pas_stage_required(kAdrenoZapPasStageInitImage, 0) == 1);
    CHECK(adreno_zap_pas_stage_required(kAdrenoZapPasStageAuthAndReset, 0) == 1);
    CHECK(adreno_zap_pas_stage_required(kAdrenoZapPasStageMemSetup, 1) == 1);
    CHECK(adreno_zap_pas_stage_required(kAdrenoZapPasStageMemSetup, 0) == 0);
    CHECK(adreno_zap_pas_stage_required(kAdrenoZapPasStageCount, 1) == 0);
    CHECK(adreno_zap_pas_required_count(1) == 3u);   // a630_zap.mdt: all three
    CHECK(adreno_zap_pas_required_count(0) == 2u);   // non-relocatable skips it
    CHECK(seq[1].required == 1);

    // Refusals: no region for the call that needs one, bad stage, NULL out,
    // short capacity, and a base that fails the gate.
    CHECK(adreno_zap_pas_call(kAdrenoZapPasStageMemSetup, base, 1, nullptr, seq) == 1);
    CHECK(adreno_zap_pas_call(kAdrenoZapPasStageCount, base, 1, &region, seq) == 1);
    CHECK(adreno_zap_pas_call(0u, base, 1, &region, nullptr) == 1);
    CHECK(adreno_zap_pas_call(0u, 0xC1000000u, 1, &region, seq) == 1);
    CHECK(adreno_zap_pas_sequence(base, 1, &region, seq, kAdrenoZapPasStageCount - 1u, &n) == 1);
    CHECK(n == 0u);
    CHECK(adreno_zap_pas_sequence(base, 1, &region, nullptr, kAdrenoZapPasStageCount, &n) == 1);
    CHECK(adreno_zap_pas_sequence(0xC1000000u, 1, &region, seq, kAdrenoZapPasStageCount, &n) == 1);

    // ORDER enforcement is a PREFIX test, not a set-membership test: the auth
    // call with nothing done must be refused HERE, not by the secure world.
    CHECK(adreno_zap_pas_order_allows(0u, 0x0u) == 1);
    CHECK(adreno_zap_pas_order_allows(1u, 0x0u) == 0);
    CHECK(adreno_zap_pas_order_allows(2u, 0x0u) == 0);
    CHECK(adreno_zap_pas_order_allows(1u, 0x1u) == 1);
    CHECK(adreno_zap_pas_order_allows(2u, 0x1u) == 0);   // mem_setup still due
    CHECK(adreno_zap_pas_order_allows(2u, 0x3u) == 1);
    CHECK(adreno_zap_pas_order_allows(kAdrenoZapPasStageCount, 0x7u) == 0);
    CHECK(adreno_zap_pas_done_bit(0u) == 1u);
    CHECK(adreno_zap_pas_done_bit(1u) == 2u);
    CHECK(adreno_zap_pas_done_bit(2u) == 4u);
    CHECK(adreno_zap_pas_done_bit(kAdrenoZapPasStageCount) == 0u);

    // Names and anchors are total and distinct.
    for (uint32_t i = 0; i < kAdrenoZapPasStageCount; ++i) {
        const char *nm = adreno_zap_pas_stage_name(i);
        const char *an = adreno_zap_pas_stage_anchor(i);
        CHECK(nm != nullptr && nm[0] != '\0');
        CHECK(an != nullptr && an[0] != '\0');
        CHECK(!(nm[0] == 'u' && nm[1] == 'n'));
        for (uint32_t j = 0; j < i; ++j) {
            const char *om = adreno_zap_pas_stage_name(j);
            bool same = true;
            uint32_t k = 0;
            while (nm[k] || om[k]) { if (nm[k] != om[k]) { same = false; break; } ++k; }
            CHECK(!same);
        }
    }
    CHECK(adreno_zap_pas_stage_name(kAdrenoZapPasStageCount)[0] == 'u');
    CHECK(adreno_zap_pas_stage_anchor(kAdrenoZapPasStageCount)[0] == 'u');
}

// ---- ROUND 106 group 9: the DERIVED SMC base and the two channels ----
static void testSmcBaseAndChannels()
{
    const uint32_t base = adreno_zap_smc_base_derived();

    // P3:69 SCM_SMC_FNID(s, c) = ((s & 0xFF) << 8) | (c & 0xFF). Re-derive the
    // three ids by hand rather than trusting the table.
    CHECK(adreno_zap_scm_fnid(0x02u, 0x01u) == 0x0201u);
    CHECK(adreno_zap_scm_fnid(0x02u, 0x02u) == 0x0202u);
    CHECK(adreno_zap_scm_fnid(0x02u, 0x05u) == 0x0205u);
    CHECK(adreno_zap_scm_fnid(0x02u, 0x01u) == ((0x02u & 0xFFu) << 8 | (0x01u & 0xFFu)));
    // Both fields are masked: out-of-range bits do not leak through.
    CHECK(adreno_zap_scm_fnid(0x102u, 0x205u) == 0x0205u);
    // The SMC form is NOT the legacy form (P3:76 ((s)<<10)|((c)&0x3ff)).
    CHECK(adreno_zap_scm_fnid(0x04u, 0x00u) != ((0x04u << 10) | 0x00u));

    // THE BASE IS DERIVED, NOT PINNED, and says so.
    CHECK(base == 0xC2000000u);
    CHECK(adreno_zap_smc_base_provenance()[0] == 'd');   // "derived-unpinned"
    CHECK(adreno_zap_smc_base_provenance()[8] == 'u');   // ...and "unpinned"

    // The owner is RECOVERED from the base, which is how 2 is proven, not
    // asserted: 0xC2000000 >> 24 & 0x3F == 2.
    CHECK(adreno_zap_smc_base_owner(base) == 2u);
    CHECK(adreno_zap_smc_base_owner(base) == kAdrenoZapScmOwnerSip);
    CHECK(((base >> 24) & 0x3Fu) == 2u);
    CHECK(adreno_zap_smc_base_owner(0xC1000000u) == 1u);   // the OLD wrong value

    // The gate refuses a wrong owner and a base with no SMCCC top bits.
    CHECK(adreno_zap_smc_base_gate(base) == 1);
    CHECK(adreno_zap_smc_base_gate(0xC1000000u) == 0);     // owner 1
    CHECK(adreno_zap_smc_base_gate(0xC3000000u) == 0);     // owner 3
    CHECK(adreno_zap_smc_base_gate(0x02000000u) == 0);     // no SMC64/FASTCALL
    CHECK(adreno_zap_smc_base_gate(0x82000000u) == 0);     // FASTCALL, no SMC64
    // The gate reads OWNER and SMC64, nothing else: clearing FAST_CALL while
    // keeping SMC64 and owner 2 still passes, which proves the check is not
    // "== the derived constant" in disguise.
    CHECK(adreno_zap_smc_base_gate(0x42000000u) == 1);
    CHECK(adreno_zap_smc_base_gate(0xC2000000u) == 1);

    // x0 is base | fnid, NOT base + fnid, and the three are distinct.
    struct adreno_zap_pas_region region;
    region.phys = 0x1000;
    region.bytes = 0x1000;
    struct adreno_zap_pas_call seq[kAdrenoZapPasStageCount];
    uint32_t n = 0;
    CHECK(adreno_zap_pas_sequence(base, 1, &region, seq, kAdrenoZapPasStageCount, &n) == 0);
    CHECK(seq[0].x0 == 0xC2000201u);
    CHECK(seq[1].x0 == 0xC2000202u);
    CHECK(seq[2].x0 == 0xC2000205u);
    CHECK(seq[0].fnid == 0x0201u);
    CHECK(seq[2].x0 == (base | 0x0205u));

    // THE TWO CHANNELS: transport and secure world, kept separate, and the
    // conjunction is the only success (P2:729 `return ret ?: res.result[0]`).
    struct adreno_zap_pas_result r;
    CHECK(adreno_zap_pas_judge(0, 0u, &r) == 0);
    CHECK(r.transport_ok == 1 && r.secure_ok == 1 && r.is_success == 1);
    CHECK(r.transport_ret == 0 && r.secure_result == 0u);
    // A clean transport with a rejected image is a FAILURE, and the record still
    // says WHICH channel was clean -- that is the whole point of the struct.
    CHECK(adreno_zap_pas_judge(0, 1u, &r) == 0);
    CHECK(r.is_success == 0);
    CHECK(r.transport_ok == 1 && r.secure_ok == 0);
    CHECK(r.secure_result == 1u);
    // A transport failure leaves the secure channel untouched.
    CHECK(adreno_zap_pas_judge(-13, 0u, &r) == 0);
    CHECK(r.is_success == 0 && r.transport_ok == 0 && r.secure_ok == 1);
    // The new API must NEVER disagree with the round-73 collapsed predicate.
    const int tr[4] = {0, 0, -1, -13};
    const uint32_t sr[4] = {0u, 1u, 0u, 0xdeadbeefu};
    for (uint32_t i = 0; i < 4u; ++i) {
        int collapsed = -1;
        CHECK(adreno_zap_result_semantics(tr[i], sr[i], &collapsed) == 0);
        CHECK(adreno_zap_pas_judge(tr[i], sr[i], &r) == 0);
        CHECK(collapsed == r.is_success);
    }
    CHECK(adreno_zap_pas_judge(0, 0u, nullptr) == 1);

    // The round-106 self-check runs and is not trivially short.
    uint32_t checks = 0;
    CHECK(adreno_zap_pas_verify(&checks) == 0);
    CHECK(checks >= 80u);
    CHECK(adreno_zap_pas_verify(nullptr) == 0);
}

int main()
{
    testPasId();
    testScmShape();
    testChain();
    testStepNames();
    testResultSemantics();
    testUnsupportedFallback();
    testSelfCheck();
    testPasStages();
    testSmcBaseAndChannels();
    if (failures) {
        std::printf("FAIL: enchilada A630 ZAP/PAS auth shape contract "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 ZAP/PAS auth shape contract "
                "(9 test groups, 0 failures)\n");
    return 0;
}
