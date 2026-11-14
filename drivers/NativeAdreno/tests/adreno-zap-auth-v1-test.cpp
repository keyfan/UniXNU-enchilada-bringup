// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoZapAuthV1 host test: 11 groups, CHECK() accumulates failures.
// Groups 1-7 are the round-73 shape. Groups 8-9 are ROUND 106: the three-stage
// ordered PAS contract and the derived SMC base. Groups 10-11 are ROUND 107:
// the injected SCM transport seam and the fail-closed orchestration over it.
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

// ================= ROUND 107: the injected SCM transport seam =================
//
// A recording + scripted stub. It stands in for the production transport (a thin
// extern "C" wrapper the ArmCpu owner would add -- see
// work/tmp/r107-scm-export-plan.md). The real one is NOT linked here and NOT
// exported yet, which is exactly why the contract layer takes it by injection.
struct StubScm {
    uint32_t calls = 0;
    uint32_t x0[4] = {0, 0, 0, 0};
    uint32_t x1[4] = {0, 0, 0, 0};
    uint32_t x2[4] = {0, 0, 0, 0};
    uint32_t x3[4] = {0, 0, 0, 0};
    uint32_t x4[4] = {0, 0, 0, 0};
    // fail_at == which call index fails; 4 or more means "never fail".
    uint32_t fail_at = 4;
    bool fail_transport = false;   // fail on the transport channel
    uint64_t fail_secure = 0;      // value put in result[0] when the secure world rejects
};

// Round 108: the parameter list IS the register map (P6:166-173):
//   x0=fn id, x1=arginfo, x2=args[0]=peripheral, x3=args[1], x4=args[2].
// The stub records each slot separately so a one-place shift fails a CHECK.
static int stubScm(void *cookie, uint32_t fn, uint32_t arginfo, uint32_t a0,
                   uint32_t a1, uint32_t a2, uint64_t *out)
{
    StubScm *s = static_cast<StubScm *>(cookie);
    if (s == nullptr || out == nullptr) return -1;
    if (s->calls < 4u) {
        s->x0[s->calls] = fn;
        s->x1[s->calls] = arginfo;
        s->x2[s->calls] = a0;
        s->x3[s->calls] = a1;
        s->x4[s->calls] = a2;
    }
    if (s->calls == s->fail_at) {
        ++s->calls;
        if (s->fail_transport) { *out = 0; return -13; }   // -EACCES
        *out = s->fail_secure;                             // transport OK, TZ rejects
        return 0;
    }
    ++s->calls;
    *out = 0;
    return 0;
}

static void testTransportSeam()
{
    // The seam WIRES nothing by itself: no function pointer is a refusal, and
    // "not wired" must be distinguishable from "wired and failing".
    struct adreno_zap_transport t;
    t.cookie = nullptr;
    t.call = nullptr;
    CHECK(adreno_zap_transport_ready(nullptr) == 0);
    CHECK(adreno_zap_transport_ready(&t) == 0);
    t.call = stubScm;
    CHECK(adreno_zap_transport_ready(&t) == 1);

    // THE PRODUCTION STATE TODAY: no export exists, so a NULL transport must
    // fail closed and call NOTHING -- never be read as "0 stages = success".
    struct adreno_zap_pas_region region;
    region.phys = 0x40000000u;
    region.bytes = 0x1000u;
    struct adreno_zap_pas_report rep;
    t.call = nullptr;
    CHECK(adreno_zap_pas_execute(&t, kAdrenoZapSmcBaseDerived, 0x1000u, &region,
                                &rep) == kAdrenoZapExecNoTransport);
    CHECK(rep.stages_run == 0u);
    CHECK(rep.all_succeeded == 0);
    CHECK(adreno_zap_pas_execute(nullptr, kAdrenoZapSmcBaseDerived, 0x1000u,
                                 &region, &rep) == kAdrenoZapExecNoTransport);

    // The three refusal gates must fire BEFORE any call reaches the transport.
    StubScm s;
    t.cookie = &s;
    t.call = stubScm;
    CHECK(adreno_zap_pas_execute(&t, 0xC1000000u, 0x1000u, &region, &rep) ==
          kAdrenoZapExecBadBase);   // owner 1 is NOT ARM_SMCCC_OWNER_SIP
    CHECK(s.calls == 0u);
    CHECK(adreno_zap_pas_execute(&t, kAdrenoZapSmcBaseDerived, 0x1000u, nullptr,
                                 &rep) == kAdrenoZapExecNoRegion);
    CHECK(s.calls == 0u);          // refusing up front means init_image never ran
    CHECK(adreno_zap_pas_execute(&t, kAdrenoZapSmcBaseDerived, 0x1000u, &region,
                                 nullptr) == kAdrenoZapExecNullOut);

    // A missing transport is not a "success with zero stages" either.
    CHECK(adreno_zap_pas_execute(nullptr, kAdrenoZapSmcBaseDerived, 0x1000u,
                                 &region, nullptr) == kAdrenoZapExecNullOut);
}

static void testPasExecute()
{
    struct adreno_zap_pas_region region;
    region.phys = 0x40000000u;
    region.bytes = 0x1000u;
    struct adreno_zap_pas_report rep;
    struct adreno_zap_transport t;
    const uint32_t base = kAdrenoZapSmcBaseDerived;
    StubScm s;
    t.cookie = &s;
    t.call = stubScm;

    // ---- ALL THREE SUCCEED ----
    CHECK(adreno_zap_pas_execute(&t, base, 0x1000u, &region, &rep) ==
          kAdrenoZapExecOk);
    CHECK(s.calls == 3u);          // exactly one SMC per stage, no extra probes
    CHECK(rep.stages_run == 3u);
    CHECK(rep.all_succeeded == 1);
    CHECK(rep.first_failure == kAdrenoZapPasStageCount);   // sentinel: none
    CHECK(rep.have_done == 0x7u);

    // ---- PARAMETER ASSEMBLY, pinned against P6:166-173 ----
    // THE LAYOUT WAS WRONG BEFORE ROUND 108. P6:171 puts `desc->arginfo` in
    // x1 and P6:172-173 (SCM_SMC_FIRST_REG_IDX == 2, P6:35) puts `desc->args[0]`
    // in x2. So the peripheral is in **x2**, and x1 is the arginfo. Rounds
    // 106-107 had every slot one early, which would have sent arginfo=13 with a
    // garbage args[0]. Each slot is asserted separately below.
    CHECK(s.x0[0] == 0xC2000201u);            // init_image      (PIL 0x01)
    CHECK(s.x0[1] == 0xC2000202u);            // mem_setup       (PIL 0x02)
    CHECK(s.x0[2] == 0xC2000205u);            // auth_and_reset  (PIL 0x05)
    CHECK(s.x0[0] == (base | 0x0201u));
    CHECK(s.x0[1] == (base | 0x0202u));
    CHECK(s.x0[2] == (base | 0x0205u));
    // x1 is the ARGINFO, per stage, and it must NOT be 13.
    CHECK(s.x1[0] == 0x82u && s.x1[1] == 0x03u && s.x1[2] == 0x01u);
    CHECK(s.x1[0] == kAdrenoZapArgInfoInitImage);
    CHECK(s.x1[1] == kAdrenoZapArgInfoMemSetup);
    CHECK(s.x1[2] == kAdrenoZapArgInfoAuthAndReset);
    CHECK(s.x1[0] != 13u);                    // the pre-108 bug, explicitly
    // x2 is args[0] == the peripheral, on EVERY stage (P2:710 via P6:172).
    CHECK(s.x2[0] == 13u && s.x2[1] == 13u && s.x2[2] == 13u);
    CHECK(s.x2[0] == kAdrenoZapPasId);
    // x3/x4 differ per stage, and that difference IS the contract:
    CHECK(s.x3[0] == 0x1000u && s.x4[0] == 0u);          // init: metadata PHYS in x3
    CHECK(s.x3[1] == 0x40000000u && s.x4[1] == 0x1000u); // mem:  phys in x3, SIZE in x4
    CHECK(s.x3[2] == 0u && s.x4[2] == 0u);               // auth: peripheral only
    // The per-stage report records what was ACTUALLY sent, not what was planned.
    for (uint32_t i = 0; i < 3u; ++i) {
        CHECK(rep.stage[i].stage == i);
        CHECK(rep.stage[i].called == 1);
        CHECK(rep.stage[i].is_success == 1);
        CHECK(rep.stage[i].x0 == s.x0[i]);
    }

    // ---- FAIL CLOSED, one stage at a time ----
    // For each stage: fail it and prove the chain STOPPED there. The assertion
    // that matters is "s.calls == fail_stage + 1" -- no later call happened.
    for (uint32_t fail_stage = 0; fail_stage < 3u; ++fail_stage) {
        // (a) transport-channel failure
        s = StubScm();
        s.fail_at = fail_stage;
        s.fail_transport = true;
        CHECK(adreno_zap_pas_execute(&t, base, 0x1000u, &region, &rep) ==
              kAdrenoZapExecStageFailed);
        CHECK(s.calls == fail_stage + 1u);   // stopped AT the failing stage
        CHECK(rep.first_failure == fail_stage);
        CHECK(rep.stages_run == fail_stage + 1u);
        CHECK(rep.all_succeeded == 0);
        for (uint32_t j = fail_stage + 1u; j < 3u; ++j) {
            CHECK(rep.stage[j].called == 0);   // NOTHING after the failure
        }

        // (b) transport clean, secure world rejects: SAME stop, and the report
        // must say the SECURE channel failed -- the P2:729 trap made concrete.
        s = StubScm();
        s.fail_at = fail_stage;
        s.fail_transport = false;
        s.fail_secure = 0x2Au;
        CHECK(adreno_zap_pas_execute(&t, base, 0x1000u, &region, &rep) ==
              kAdrenoZapExecStageFailed);
        CHECK(s.calls == fail_stage + 1u);
        CHECK(rep.stage[fail_stage].transport_ret == 0);   // transport DID succeed
        CHECK(rep.stage[fail_stage].secure_result == 0x2Au); // TZ rejected
        CHECK(rep.stage[fail_stage].is_success == 0);        // so: FAILURE
        CHECK(rep.all_succeeded == 0);
        CHECK(rep.first_failure == fail_stage);
    }

    // Stage 0 failing means the two later stages were NEVER attempted -- the
    // specific guarantee that a rejected metadata image cannot be followed by a
    // mem_setup or an auth call.
    s = StubScm();
    s.fail_at = 0u;
    s.fail_secure = 1u;
    CHECK(adreno_zap_pas_execute(&t, base, 0x1000u, &region, &rep) ==
          kAdrenoZapExecStageFailed);
    CHECK(s.calls == 1u);
    CHECK(rep.stage[1].called == 0 && rep.stage[2].called == 0);
    CHECK(rep.have_done == 0u);    // not even stage 0 was credited

    // ---- the round-107 self-check ----
    uint32_t checks = 0;
    CHECK(adreno_zap_transport_verify(&checks) == 0);
    CHECK(checks >= 60u);
    CHECK(adreno_zap_transport_verify(nullptr) == 0);

    // ================= ROUND 126: the mem_setup SPAN rule =================
    // THE DEFECT: through round 125 the probe passed the SEGMENT'S FILE LENGTH
    // (1968, kAdrenoZapSegBytes) as mem_setup's args[2]. Upstream (P1:229-233,
    // :252) passes ALIGN(max(p_paddr+p_memsz), 4K) - min(p_paddr) over the VALID
    // phdrs, which for a630_zap.mdt is 0x1000. These assertions pin BOTH numbers
    // against the same function, so a regression to 1968 fails here.
    CHECK(kAdrenoZapSpanPageBytes == 0x1000u);
    // The real blob: only ph[2] is valid (paddr 0x5000, memsz 1968 = 0x7B0).
    // ALIGN(0x5000 + 0x7B0, 0x1000) = ALIGN(0x57B0) = 0x6000; 0x6000-0x5000 = 0x1000.
    CHECK(adreno_zap_pas_mem_setup_span_ok(0x5000u, 0x6000u, 0x1000u) == 1);
    CHECK(adreno_zap_pas_mem_setup_span_ok(0x5000u, 0x6000u, 1968u) == 0);
    // 1968 IS the segment's file length (kAdrenoZapSegBytes on this blob) -- say
    // so once, so the next reader cannot mistake the difference for a typo.
    CHECK(1968u == 0x7B0u);
    // NOT just "!= 1968": the DT region size is not the span either.
    CHECK(adreno_zap_pas_mem_setup_span_ok(0x5000u, 0x6000u, 0x2000u) == 0);
    CHECK(adreno_zap_pas_mem_setup_span_ok(0x5000u, 0x6000u, 0x0FFFu) == 0);
    // Refusals that must not read as a zero-length span (P1:101).
    CHECK(adreno_zap_pas_mem_setup_span_ok(0x5000u, 0x5000u, 0u) == 0);
    CHECK(adreno_zap_pas_mem_setup_span_ok(0x6000u, 0x5000u, 0u) == 0);
    // Unaligned edges are not the P1:232 rule.
    CHECK(adreno_zap_pas_mem_setup_span_ok(0x5000u, 0x6001u, 0x1001u) == 0);
    CHECK(adreno_zap_pas_mem_setup_span_ok(0x5001u, 0x6000u, 0x0FFFu) == 0);
    // A wider legitimate span still passes, so this is a RULE not a constant.
    CHECK(adreno_zap_pas_mem_setup_span_ok(0x5000u, 0x9000u, 0x4000u) == 1);
    CHECK(adreno_zap_pas_mem_setup_span_ok(0x0000u, 0x1000u, 0x1000u) == 1);

    // ============ ROUND 126: the PAS_IS_SUPPORTED probe shape ==============
    // P2:776-796 / P3:102. ONE argument, cmd 0x07, x0 = base | 0x0207.
    struct adreno_zap_pas_call sup;
    CHECK(adreno_zap_pas_is_supported_call(base, &sup) == 0);
    CHECK(sup.cmd == kAdrenoZapScmPasIsSupported);
    CHECK(sup.cmd == 0x07u);
    CHECK(sup.arginfo == kAdrenoZapArgInfoIsSupported);
    CHECK(sup.arginfo == 0x01u);        // P2:782 ARGS(1)
    CHECK(sup.arg_count == 1u);         // derived from arginfo, not written
    CHECK(sup.svc == kAdrenoZapScmSvcPil);
    CHECK(sup.fnid == 0x0207u);
    CHECK(sup.x0 == (base | 0x0207u));
    CHECK(sup.args0 == kAdrenoZapPasId);
    CHECK(sup.owner == kAdrenoZapScmOwnerSip);
    // It is NOT one of the three ordered stages, and cannot be required: a
    // caller must not be able to slot it into adreno_zap_pas_sequence().
    CHECK(sup.stage >= kAdrenoZapPasStageCount);
    CHECK(sup.required == 0);
    CHECK(sup.is_metadata_call == 0);
    CHECK(sup.carries_size_arg == 0);
    // The SAME base gate every other stage uses.
    CHECK(adreno_zap_pas_is_supported_call(0xC1000000u, &sup) == 1);  // owner != SIP
    CHECK(adreno_zap_pas_is_supported_call(base, nullptr) == 1);

    // ---- the verdict is in a1, NOT a0: the whole point of the decoder ----
    int supported = 7;
    CHECK(adreno_zap_pas_supported_verdict(0, 1u, &supported) == 0);
    CHECK(supported == 1);                       // clean transport + a1==1 -> YES
    CHECK(adreno_zap_pas_supported_verdict(0, 0u, &supported) == 0);
    CHECK(supported == 0);                       // clean transport + a1==0 -> NO
    // a0 nonzero: the SMC did not complete. INCONCLUSIVE, never "no".
    CHECK(adreno_zap_pas_supported_verdict(-1, 0u, &supported) == 0);
    CHECK(supported == -1);
    CHECK(adreno_zap_pas_supported_verdict(-4, 1u, &supported) == 0);
    CHECK(supported == -1);                      // even with a1==1: no SMC, no answer
    // A clean transport with an unexpected a1 is NOT "no" either -- folding it
    // into 0 would manufacture a policy rejection out of a decoder gap.
    CHECK(adreno_zap_pas_supported_verdict(0, 0xDEADu, &supported) == 0);
    CHECK(supported == -1);
    CHECK(adreno_zap_pas_supported_verdict(0, 1u, nullptr) == 1);

    // ============ ROUND 129: IS_CALL_AVAILABLE + the v13 fold ==============
    // THE CALL SHAPE. svc is 0x06 (SVC_INFO, P3:109) -- NOT 0x01, which is
    // SVC_BOOT (P3:84). This tree's own ArmCpu.cpp:165 already passes
    // 0xC2000601 for this very query, so a 0x01 here would be a different
    // question entirely.
    CHECK(kAdrenoZapScmSvcInfo == 0x06u);
    CHECK(kAdrenoZapScmInfoIsCallAvail == 0x01u);
    struct adreno_zap_pas_call avail;
    CHECK(adreno_zap_is_call_available_call(base, 0x0207u, &avail) == 0);
    CHECK(avail.svc == 0x06u);
    CHECK(avail.cmd == 0x01u);
    CHECK(avail.fnid == 0x0601u);          // FNID(0x06,0x01), NOT 0x0106
    CHECK(avail.x0 == (base | 0x0601u));
    CHECK(avail.x0 == 0xC2000601u);        // == what ArmCpu.cpp:165 sends
    CHECK(avail.arginfo == 0x01u);         // ARGS(1)
    CHECK(avail.arg_count == 1u);
    CHECK(avail.owner == kAdrenoZapScmOwnerSip);
    // P2:355-356 the OWNER rides in args[0] HERE, unlike the PAS stages where
    // it rides in x0's base. 0x0207 | (2 << 24) == 0x02000207.
    CHECK(avail.args0 == 0x02000207u);
    CHECK(avail.stage >= kAdrenoZapPasStageCount);   // not an ordered stage
    CHECK(avail.required == 0);
    CHECK(avail.is_metadata_call == 0);
    CHECK(avail.carries_size_arg == 0);
    // Refusals.
    CHECK(adreno_zap_is_call_available_call(0xC1000000u, 0x0207u, &avail) == 1);
    CHECK(adreno_zap_is_call_available_call(base, 0x0207u, nullptr) == 1);
    CHECK(adreno_zap_is_call_available_call(base, 0x10207u, &avail) == 1);  // >16 bits

    // ---- the verdict, with the OPPOSITE polarity to PAS_IS_SUPPORTED ----
    // Non-zero a1 means KNOWN. Getting this backwards would invert the whole
    // round's conclusion, so it is asserted from both sides.
    int known = 7;
    CHECK(adreno_zap_is_call_available_verdict(0, 1u, &known) == 0);
    CHECK(known == 1);                       // available
    CHECK(adreno_zap_is_call_available_verdict(0, 0u, &known) == 0);
    CHECK(known == 0);                       // NOT available
    CHECK(adreno_zap_is_call_available_verdict(0, 0xDEADu, &known) == 0);
    CHECK(known == 1);                       // any non-zero is KNOWN, not just 1
    CHECK(adreno_zap_is_call_available_verdict(-1, 1u, &known) == 0);
    CHECK(known == -1);                      // query failed: no answer learned
    CHECK(adreno_zap_is_call_available_verdict(0, 1u, nullptr) == 1);

    // ---- THE FOLD: the round-129 property ----
    // Same 0/0 reply, different meaning depending on whether the command exists.
    CHECK(adreno_zap_pas_supported_verdict_avail(1, 0, 0u, &supported) == 0);
    CHECK(supported == 0);        // command KNOWN + a1==0 -> a REAL "not supported"
    CHECK(adreno_zap_pas_supported_verdict_avail(1, 0, 1u, &supported) == 0);
    CHECK(supported == 1);        // KNOWN + a1==1 -> supported
    // The decisive one: the SAME reply must NOT become a policy answer when the
    // command was never known to exist. Round 128 could not distinguish these.
    CHECK(adreno_zap_pas_supported_verdict_avail(0, 0, 0u, &supported) == 0);
    CHECK(supported == -1);       // UNKNOWN + a1==0 -> INCONCLUSIVE, not 0
    CHECK(adreno_zap_pas_supported_verdict_avail(-1, 0, 0u, &supported) == 0);
    CHECK(supported == -1);       // inconclusive availability -> also -1
    // A positive answer needs no availability support: a monitor that says
    // "supported" has plainly implemented the command.
    CHECK(adreno_zap_pas_supported_verdict_avail(0, 0, 1u, &supported) == 0);
    CHECK(supported == 1);
    // A failed transport stays -1 whatever the availability said.
    CHECK(adreno_zap_pas_supported_verdict_avail(1, -1, 0u, &supported) == 0);
    CHECK(supported == -1);
    CHECK(adreno_zap_pas_supported_verdict_avail(1, 0, 1u, nullptr) == 1);
    // And the two-argument form still behaves exactly as round 126 left it, so
    // the fold is an ADDITION, not a redefinition.
    CHECK(adreno_zap_pas_supported_verdict(0, 0u, &supported) == 0);
    CHECK(supported == 0);

    // THE TRAP ASSERTED DIRECTLY: judging this call on a0 alone reports
    // "unsupported" for every successful probe. a1==1 must never yield 0.
    supported = 0;
    (void)adreno_zap_pas_supported_verdict(0, 1u, &supported);
    CHECK(supported != 0);

    // The round-126 self-check, and that it GREW (the new assertions are real).
    uint32_t pas_checks = 0;
    CHECK(adreno_zap_pas_verify(&pas_checks) == 0);
    CHECK(pas_checks >= 90u);
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
    testTransportSeam();
    testPasExecute();
    if (failures) {
        std::printf("FAIL: enchilada A630 ZAP/PAS auth shape contract "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 ZAP/PAS auth shape contract "
                "(11 test groups, 0 failures)\n");
    return 0;
}
