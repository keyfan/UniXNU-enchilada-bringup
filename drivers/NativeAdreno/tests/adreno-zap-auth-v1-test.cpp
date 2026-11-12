// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoZapAuthV1 host test: 7 groups, CHECK() accumulates failures.
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

int main()
{
    testPasId();
    testScmShape();
    testChain();
    testStepNames();
    testResultSemantics();
    testUnsupportedFallback();
    testSelfCheck();
    if (failures) {
        std::printf("FAIL: enchilada A630 ZAP/PAS auth shape contract "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 ZAP/PAS auth shape contract "
                "(7 test groups, 0 failures)\n");
    return 0;
}
