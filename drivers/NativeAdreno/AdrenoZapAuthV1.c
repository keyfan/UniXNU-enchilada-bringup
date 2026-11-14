// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoZapAuthV1 -- see AdrenoZapAuthV1.h for provenance and the honest
// HOST-VERIFIED boundary. Freestanding C11: no libc, no MMIO, no SCM call.

#include "AdrenoZapAuthV1.h"

int adreno_zap_scm_cmd(uint32_t op, uint32_t *svc, uint32_t *cmd,
                       uint32_t *argc, uint32_t *owner)
{
    if (svc == 0 || cmd == 0 || argc == 0 || owner == 0) {
        return 1;
    }
    /* P3:97 every PAS operation lives in the PIL service. */
    *svc = kAdrenoZapScmSvcPil;
    /* P2:709/:710 every one of these takes exactly the peripheral id. */
    *argc = kAdrenoZapScmArgCount;
    /* P2:711 the owner is SIP, not a hypervisor or a TZ owner. */
    *owner = kAdrenoZapScmOwnerSip;
    switch (op) {
    case 0u:
        *cmd = kAdrenoZapScmPasInitImage;
        return 0;
    case 1u:
        *cmd = kAdrenoZapScmPasMemSetup;
        return 0;
    case 2u:
        *cmd = kAdrenoZapScmPasAuthAndReset;
        return 0;
    case 3u:
        *cmd = kAdrenoZapScmPasShutdown;
        return 0;
    case 4u:
        *cmd = kAdrenoZapScmPasIsSupported;
        return 0;
    default:
        return 1;
    }
}

const char *adreno_zap_scm_op_name(uint32_t op)
{
    switch (op) {
    case 0u:
        return "pas-init-image";
    case 1u:
        return "pas-mem-setup";
    case 2u:
        return "pas-auth-and-reset";
    case 3u:
        return "pas-shutdown";
    case 4u:
        return "pas-is-supported";
    default:
        return "unknown";
    }
}

uint32_t adreno_zap_pas_id(void)
{
    return kAdrenoZapPasId;
}

int adreno_zap_chain(struct adreno_zap_step_state *out, uint32_t capacity,
                     uint32_t *count)
{
    uint32_t n = 0u;

    if (count == 0) {
        return 1;
    }
    *count = 0u;
    if (out == 0 || capacity < kAdrenoZapStepCount) {
        return 1;
    }

    /* Every step below is recorded from the PINNED sources plus the round-71
     * live device-tree read. `present` is 0 with a reason wherever the port has
     * not verified it -- an unverified step is never counted as present. */

    /* S1 CONFIG_ARCH_QCOM (P4:43). XNU is not Linux: no such Kconfig exists, so
     * the symbol can never be "enabled" and the step's real content is the SCM
     * transport itself. Not reachable as-written. */
    out[n].step = kAdrenoZapStepArchQcom;
    out[n].present = 0u;
    out[n].reachable = 0u;
    n += 1u;

    /* S2 the "zap-shader" child node (P4:48). Round 71 walked the live device
     * tree: qcom,kgsl-3d0 has NO zap-shader child. The vendor tree carries
     * qcom,kgsl-hyp (with qcom,pas-id and firmware-name) instead, so the
     * mainline discovery step has nothing to find. */
    out[n].step = kAdrenoZapStepZapShaderNode;
    out[n].present = 0u;
    out[n].reachable = 1u; /* reachable, once a node exists */
    n += 1u;

    /* S3 the memory-region phandle (P4:54). The vendor qcom,kgsl-hyp DOES carry
     * memory-region, so the DATA exists -- but under a different node, which is
     * why S2 must be solved first rather than worked around. */
    out[n].step = kAdrenoZapStepMemoryRegion;
    out[n].present = 0u;
    out[n].reachable = 1u;
    n += 1u;

    /* S4 of_address_to_resource on that region (P4:61). Same: the region is
     * declared, the translation into a phys base has never been done here. */
    out[n].step = kAdrenoZapStepAddressToRes;
    out[n].present = 0u;
    out[n].reachable = 1u;
    n += 1u;

    /* S5 firmware-name (P4:82). THIS ONE IS PRESENT: round 71 read
     * qcom,firmware-name = "a630_zap" from the live device tree. It is the only
     * step of the eight that already exists. */
    out[n].step = kAdrenoZapStepFirmwareName;
    out[n].present = 1u;
    out[n].reachable = 1u;
    n += 1u;

    /* S6 qcom_mdt_get_size (P4:111). The .mdt bytes were located read-only in
     * round 38 and their sizes recorded, but qcom_mdt_get_size is a Linux
     * helper that does not exist in XNU, and no XNU-side MDT parser has been
     * written. */
    out[n].step = kAdrenoZapStepMdtSize;
    out[n].present = 0u;
    out[n].reachable = 1u;
    n += 1u;

    /* S7 qcom_mdt_load into the memremapped region (P4:141/:148). Requires S3,
     * S4 and S6 first; none of those exist. */
    out[n].step = kAdrenoZapStepMdtLoad;
    out[n].present = 0u;
    out[n].reachable = 1u;
    n += 1u;

    /* S8 qcom_scm_pas_auth_and_reset (P4:156). The SHAPE is now encoded (this
     * module), but the SCM transport that would carry it does not exist in XNU
     * and seven of the eight preconditions are absent. */
    out[n].step = kAdrenoZapStepAuthAndReset;
    out[n].present = 0u;
    out[n].reachable = 1u;
    n += 1u;

    *count = n;
    return 0;
}

uint32_t adreno_zap_present_count(void)
{
    struct adreno_zap_step_state st[kAdrenoZapStepCount];
    uint32_t count = 0u;
    uint32_t i = 0u;
    uint32_t present = 0u;

    if (adreno_zap_chain(st, kAdrenoZapStepCount, &count) != 0) {
        return 0u;
    }
    for (i = 0u; i < count; ++i) {
        if (st[i].present != 0u) {
            present += 1u;
        }
    }
    return present;
}

const char *adreno_zap_step_name(uint32_t step)
{
    switch (step) {
    case kAdrenoZapStepArchQcom:
        return "config-arch-qcom";
    case kAdrenoZapStepZapShaderNode:
        return "zap-shader-node";
    case kAdrenoZapStepMemoryRegion:
        return "memory-region";
    case kAdrenoZapStepAddressToRes:
        return "address-to-resource";
    case kAdrenoZapStepFirmwareName:
        return "firmware-name";
    case kAdrenoZapStepMdtSize:
        return "mdt-size";
    case kAdrenoZapStepMdtLoad:
        return "mdt-load";
    case kAdrenoZapStepAuthAndReset:
        return "auth-and-reset";
    default:
        return "unknown";
    }
}

const char *adreno_zap_step_anchor(uint32_t step)
{
    switch (step) {
    case kAdrenoZapStepArchQcom:
        return "adreno_gpu.c:43 gates on CONFIG_ARCH_QCOM; XNU has no such "
               "Kconfig, so this step IS the SCM transport";
    case kAdrenoZapStepZapShaderNode:
        return "adreno_gpu.c:48 of_get_child_by_name(dev, \"zap-shader\"); the "
               "live DT read in round 71 found NO such child under qcom,kgsl-3d0";
    case kAdrenoZapStepMemoryRegion:
        return "adreno_gpu.c:54 of_parse_phandle(np, \"memory-region\", 0); the "
               "vendor qcom,kgsl-hyp carries memory-region, a different node";
    case kAdrenoZapStepAddressToRes:
        return "adreno_gpu.c:61 of_address_to_resource -> r.start is the phys base";
    case kAdrenoZapStepFirmwareName:
        return "adreno_gpu.c:82 of_property_read_string_index(np, "
               "\"firmware-name\") -- PRESENT: round 71 read a630_zap live";
    case kAdrenoZapStepMdtSize:
        return "adreno_gpu.c:111 qcom_mdt_get_size; the round-38 read-only "
               "inventory has the sizes, the parser does not";
    case kAdrenoZapStepMdtLoad:
        return "adreno_gpu.c:141/:148 qcom_mdt_load into a MEMREMAP_WC region";
    case kAdrenoZapStepAuthAndReset:
        return "adreno_gpu.c:156 qcom_scm_pas_auth_and_reset(13); shape here, "
               "transport absent";
    default:
        return "unknown";
    }
}

int adreno_zap_auth_ready(void)
{
    /* Derived, not hard-coded: the auth call may be attempted only when every
     * one of the eight preconditions exists. */
    return adreno_zap_present_count() == kAdrenoZapStepCount ? 1 : 0;
}

int adreno_zap_result_semantics(int transport_ret, uint32_t pctx_result,
                                int *is_success)
{
    if (is_success == 0) {
        return 1;
    }
    /* P2:729 `return ret ?: res.result[0];` -- a zero transport return is NOT
     * enough. A non-zero PCTX result is a failure even when the SMC call
     * itself completed. Getting this backwards would report a rejected image
     * as authorised. */
    *is_success = (transport_ret == 0 && pctx_result == 0u) ? 1 : 0;
    return 0;
}

int adreno_zap_unsupported_is_quiet(int scm_error)
{
    /* P4:158-163: -EOPNOTSUPP means "this target needs no ZAP shader", so the
     * driver disables zap and CARRIES ON. Linux encodes it as -95. */
    return scm_error == -95 ? 1 : 0;
}

int adreno_zap_auth_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    uint32_t svc = 0u, cmd = 0u, argc = 0u, owner = 0u;
    uint32_t i = 0u;
    uint32_t count = 0u;
    uint32_t present = 0u;
    uint32_t reachable = 0u;
    struct adreno_zap_step_state st[kAdrenoZapStepCount];
    struct adreno_zap_step_state tiny[kAdrenoZapStepCount - 1u];
    int ok = 0;

    /* PAS id. */
    checks += 1u;
    if (adreno_zap_pas_id() != 13u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_pas_id() != kAdrenoZapPasId) {
        fails += 1u;
    }

    /* SCM shape: every op is in SVC_PIL with one arg owned by SIP. */
    checks += 1u;
    if (adreno_zap_scm_cmd(2u, &svc, &cmd, &argc, &owner) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (svc != 0x02u) {
        fails += 1u;
    }
    checks += 1u;
    if (cmd != 0x05u) {
        fails += 1u; /* PAS_AUTH_AND_RESET */
    }
    checks += 1u;
    if (argc != 1u) {
        fails += 1u; /* QCOM_SCM_ARGS(1) */
    }
    checks += 1u;
    if (owner != kAdrenoZapScmOwnerSip) {
        fails += 1u;
    }
    {
        const uint32_t want[5] = {0x01u, 0x02u, 0x05u, 0x06u, 0x07u};
        for (i = 0u; i < 5u; ++i) {
            checks += 1u;
            if (adreno_zap_scm_cmd(i, &svc, &cmd, &argc, &owner) != 0) {
                fails += 1u;
            } else if (cmd != want[i] || svc != 0x02u) {
                fails += 1u;
            }
        }
    }
    checks += 1u;
    if (adreno_zap_scm_cmd(5u, &svc, &cmd, &argc, &owner) == 0) {
        fails += 1u; /* out of range refuses */
    }
    checks += 1u;
    if (adreno_zap_scm_cmd(0u, 0, &cmd, &argc, &owner) == 0) {
        fails += 1u;
    }
    for (i = 0u; i < 5u; ++i) {
        checks += 1u;
        if (adreno_zap_scm_op_name(i)[0] == '\0') {
            fails += 1u;
        }
        checks += 1u;
        if (adreno_zap_scm_op_name(i)[0] == 'u' &&
            adreno_zap_scm_op_name(i)[1] == 'n') {
            fails += 1u;
        }
    }
    checks += 1u;
    if (adreno_zap_scm_op_name(9u)[0] != 'u') {
        fails += 1u;
    }

    /* The eight-step chain. */
    checks += 1u;
    if (adreno_zap_chain(st, kAdrenoZapStepCount, &count) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != kAdrenoZapStepCount) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoZapStepCount != 8u) {
        fails += 1u;
    }
    checks += 1u;
    for (i = 0u; i < count; ++i) {
        if (st[i].step != i) {
            fails += 1u;
            break;
        }
    }
    /* EXACTLY ONE step is present today, and it is firmware-name. */
    for (i = 0u; i < count; ++i) {
        if (st[i].present != 0u) {
            present += 1u;
            checks += 1u;
            if (st[i].step != kAdrenoZapStepFirmwareName) {
                fails += 1u;
            }
        }
        if (st[i].reachable != 0u) {
            reachable += 1u;
        }
        checks += 1u;
        if (adreno_zap_step_name(st[i].step)[0] == '\0' ||
            adreno_zap_step_anchor(st[i].step)[0] == '\0') {
            fails += 1u;
        }
    }
    checks += 1u;
    if (present != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_present_count() != present) {
        fails += 1u;
    }
    checks += 1u;
    if (present == kAdrenoZapStepCount) {
        fails += 1u; /* it must NOT be complete today */
    }
    /* The CONFIG_ARCH_QCOM step is the ONE that is not even reachable on a
     * non-Linux kernel; every other step is merely not yet built. */
    checks += 1u;
    if (st[kAdrenoZapStepArchQcom].reachable != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (reachable != kAdrenoZapStepCount - 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_chain(tiny, kAdrenoZapStepCount - 1u, &count) == 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_chain(0, kAdrenoZapStepCount, &count) == 0) {
        fails += 1u;
    }

    /* Names are total and distinct. */
    for (i = 0u; i < kAdrenoZapStepCount; ++i) {
        uint32_t j = 0u;
        checks += 1u;
        if (adreno_zap_step_name(i)[0] == 'u' &&
            adreno_zap_step_name(i)[1] == 'n') {
            fails += 1u;
        }
        for (j = 0u; j < i; ++j) {
            const char *a = adreno_zap_step_name(i);
            const char *b = adreno_zap_step_name(j);
            uint32_t k = 0u;
            int same = 1;
            while (a[k] != '\0' || b[k] != '\0') {
                if (a[k] != b[k]) {
                    same = 0;
                    break;
                }
                k += 1u;
            }
            checks += 1u;
            if (same != 0) {
                fails += 1u;
            }
        }
    }
    checks += 1u;
    if (adreno_zap_step_name(kAdrenoZapStepCount)[0] != 'u') {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_step_anchor(kAdrenoZapStepCount)[0] != 'u') {
        fails += 1u;
    }

    /* Readiness is derived and is 0 today. */
    checks += 1u;
    if (adreno_zap_present_count() != kAdrenoZapStepCount) {
        checks += 1u;
        if (adreno_zap_auth_ready() != 0) {
            fails += 1u;
        }
    } else {
        checks += 1u;
        if (adreno_zap_auth_ready() != 1) {
            fails += 1u;
        }
    }
    checks += 1u;
    if (adreno_zap_auth_ready() ==
        (adreno_zap_present_count() == kAdrenoZapStepCount ? 1 : 0)) {
        /* always true by construction; kept so the derivation is exercised */
    } else {
        fails += 1u;
    }

    /* Result semantics: the trap this round exists to prevent. */
    checks += 1u;
    if (adreno_zap_result_semantics(0, 0u, &ok) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (ok != 1) {
        fails += 1u; /* transport 0 + PCTX 0 is the only success */
    }
    checks += 1u;
    if (adreno_zap_result_semantics(0, 1u, &ok) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (ok != 0) {
        fails += 1u; /* SCM completed but the secure world REJECTED: failure */
    }
    checks += 1u;
    if (adreno_zap_result_semantics(-1, 0u, &ok) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (ok != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_result_semantics(0, 0u, 0) == 0) {
        fails += 1u;
    }

    /* -EOPNOTSUPP is a quiet fallback, not an error. */
    checks += 1u;
    if (adreno_zap_unsupported_is_quiet(-95) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_unsupported_is_quiet(0) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_unsupported_is_quiet(-13) != 0) {
        fails += 1u; /* -EACCES is a real failure, not a fallback */
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return fails == 0u ? 0 : 1;
}

/* ==========================================================================
 * ROUND 106: the three-stage ordered PAS contract.
 * Pure arithmetic and table lookups. No SCM call, no MMIO, no allocation.
 * ========================================================================== */

uint32_t adreno_zap_scm_fnid(uint32_t svc, uint32_t cmd)
{
    /* P3:69 #define SCM_SMC_FNID(s, c) ((((s) & 0xFF) << 8) | ((c) & 0xFF)) */
    return ((svc & kAdrenoZapFnidSvcMask) << kAdrenoZapFnidSvcShift) |
           (cmd & kAdrenoZapFnidCmdMask);
}

uint32_t adreno_zap_smc_base_derived(void)
{
    /* P2:355-356 is the ONLY pinned site that shows how a function id is
     * combined with the owner, and it does so as an OR of the owner shifted
     * into place. The top bits (FAST_CALL, SMC_64) come from arm-smccc.h,
     * which is NOT pinned, so the composed value is DERIVED. */
    return kAdrenoZapSmcBaseDerived;
}

uint32_t adreno_zap_smc_base_owner(uint32_t base)
{
    /* Recover the owner field FROM the base. This is the derivation that makes
     * kAdrenoZapScmOwnerSip == 2 checkable instead of asserted: the base this
     * port is given is 0xC2000000 and (0xC2000000 >> 24) & 0x3F == 2. */
    return (base >> kAdrenoZapSmcOwnerShift) & kAdrenoZapSmcOwnerMask;
}

int adreno_zap_smc_base_gate(uint32_t base)
{
    /* Two independent requirements, both derived from the base itself:
     *  1. the OWNER field must be ARM_SMCCC_OWNER_SIP (P2:711), and
     *  2. the SMC_64 bit must be set -- these PAS calls are the SMC64
     *     convention (P2:352-357 lists ARM_64 among the conventions that use
     *     SCM_SMC_FNID with the owner OR'd in), not the legacy 32-bit one.
     * A base of 0xC1000000 fails (1); a base of 0x02000000 fails (2); a base of
     * 0xC3000000 passes both but carries a different owner, so it fails (1). */
    if (adreno_zap_smc_base_owner(base) != kAdrenoZapScmOwnerSip) {
        return 0;
    }
    if ((base & kAdrenoZapSmc64Bit) == 0u) {
        return 0;
    }
    return 1;
}

const char *adreno_zap_smc_base_provenance(void)
{
    /* Deliberately one fixed string: a report that prints an x0 MUST be able to
     * say where the base came from, and the answer is always the same. If this
     * ever becomes a pinned fact, this function and the header note both have
     * to change together -- which is the point of keeping it in one place. */
    return "derived-unpinned";
}

int adreno_zap_pas_stage_required(uint32_t stage, int blob_relocatable)
{
    switch (stage) {
    case kAdrenoZapPasStageInitImage:
        /* P5:236-249: read_metadata then pas_init_image, unconditionally. */
        return 1;
    case kAdrenoZapPasStageMemSetup:
        /* P5:251-258: `if (relocate)` -- required exactly when the blob carries
         * QCOM_MDT_RELOCATABLE on a valid phdr. On the real a630_zap.mdt that
         * is true (ph[2] BIT(27)), so this returns 1 for this board -- but the
         * answer comes from the ARGUMENT, never from a constant. */
        return blob_relocatable != 0 ? 1 : 0;
    case kAdrenoZapPasStageAuthAndReset:
        /* P4:156: the auth call is the last step of the chain, always made. */
        return 1;
    default:
        return 0;
    }
}

uint32_t adreno_zap_pas_required_count(int blob_relocatable)
{
    uint32_t n = 0u;
    uint32_t i = 0u;

    for (i = 0u; i < kAdrenoZapPasStageCount; ++i) {
        if (adreno_zap_pas_stage_required(i, blob_relocatable) != 0) {
            n += 1u;
        }
    }
    return n;
}

const char *adreno_zap_pas_stage_name(uint32_t stage)
{
    switch (stage) {
    case kAdrenoZapPasStageInitImage:
        return "pas-init-image";
    case kAdrenoZapPasStageMemSetup:
        return "pas-mem-setup";
    case kAdrenoZapPasStageAuthAndReset:
        return "pas-auth-and-reset";
    default:
        return "unknown";
    }
}

const char *adreno_zap_pas_stage_anchor(uint32_t stage)
{
    switch (stage) {
    case kAdrenoZapPasStageInitImage:
        return "qcom_scm.h:75 qcom_scm_pas_init_image; mdt_loader.c:243 calls it "
               "after read_metadata and aborts on failure";
    case kAdrenoZapPasStageMemSetup:
        return "mdt_loader.c:251-252 `if (relocate)` -> qcom_scm_pas_mem_setup("
               "pas_id, mem_phys, max_addr - min_addr); :226-227 sets relocate "
               "from QCOM_MDT_RELOCATABLE";
    case kAdrenoZapPasStageAuthAndReset:
        return "qcom_scm.h:79 qcom_scm_pas_auth_and_reset; adreno_gpu.c:156 is "
               "the LAST step of the chain, after the load";
    default:
        return "unknown";
    }
}

int adreno_zap_pas_call(uint32_t stage, uint32_t base, int blob_relocatable,
                        const struct adreno_zap_pas_region *region,
                        struct adreno_zap_pas_call *out)
{
    if (out == 0) {
        return 1;
    }
    if (stage >= kAdrenoZapPasStageCount) {
        return 1;
    }
    /* The base is a GATE INPUT, not a fact: refuse one whose owner field is not
     * SIP or that is not SMC64, so a caller cannot produce a wrong x0 here. */
    if (adreno_zap_smc_base_gate(base) == 0) {
        return 1;
    }
    /* Stage 1 without a region cannot be expressed: its args are the region's
     * address and size. Refusing is the only honest answer. */
    if (stage == kAdrenoZapPasStageMemSetup && region == 0) {
        return 1;
    }

    out->stage = stage;
    out->svc = kAdrenoZapScmSvcPil; /* P3:97 every PAS op is in PIL */
    out->owner = kAdrenoZapScmOwnerSip; /* P2:711 */
    out->args0 = kAdrenoZapPasId;       /* P2:710 args[0] is the peripheral */
    out->required = adreno_zap_pas_stage_required(stage, blob_relocatable);

    switch (stage) {
    case kAdrenoZapPasStageInitImage:
        out->cmd = kAdrenoZapScmPasInitImage;         /* P3:98  -> 0x01 */
        out->arginfo = kAdrenoZapArgInfoInitImage;    /* P2:584 ARGS(2,VAL,RW) */
        out->is_metadata_call = 1;
        out->carries_size_arg = 0;
        /* args[1] is the METADATA PHYS ADDRESS (P2:617). Its size is implied by
         * the blob's own metadata length, not passed as args[2] -- QCOM_SCM_RW
         * tells the secure world to translate and measure it. */
        out->args1_kind = 2u;
        break;
    case kAdrenoZapPasStageMemSetup:
        out->cmd = kAdrenoZapScmPasMemSetup;          /* P3:99  -> 0x02 */
        out->arginfo = kAdrenoZapArgInfoMemSetup;     /* P2:670 ARGS(3) */
        out->is_metadata_call = 0;
        out->carries_size_arg = 1;
        out->args1_kind = 1u;                          /* P2:672 addr */
        break;
    default:
        out->cmd = kAdrenoZapScmPasAuthAndReset;      /* P3:100 -> 0x05 */
        out->arginfo = kAdrenoZapArgInfoAuthAndReset; /* P2:709 ARGS(1) */
        out->is_metadata_call = 0;
        out->carries_size_arg = 0;
        out->args1_kind = 0u;                         /* one argument only */
        break;
    }

    /* P3:41 the count lives in the low nibble of arginfo. Deriving it instead of
     * writing 1/2/3 beside each arginfo means the two can never disagree. */
    out->arg_count = out->arginfo & 0x0Fu;

    out->fnid = adreno_zap_scm_fnid(out->svc, out->cmd); /* P3:69 */
    /* P2:355-356 the x0 really passed is base | fnid, with the base DERIVED. */
    out->x0 = base | out->fnid;

    return 0;
}

int adreno_zap_pas_sequence(uint32_t base, int blob_relocatable,
                            const struct adreno_zap_pas_region *region,
                            struct adreno_zap_pas_call *out, uint32_t capacity,
                            uint32_t *count)
{
    uint32_t i = 0u;

    if (count == 0) {
        return 1;
    }
    *count = 0u;
    if (out == 0 || capacity < kAdrenoZapPasStageCount) {
        return 1;
    }
    /* Ascending stage order by construction. A caller that wanted a different
     * order would have to stop using this function, which is deliberate. */
    for (i = 0u; i < kAdrenoZapPasStageCount; ++i) {
        if (adreno_zap_pas_call(i, base, blob_relocatable, region, &out[i]) != 0) {
            return 1;
        }
    }
    *count = kAdrenoZapPasStageCount;
    return 0;
}

int adreno_zap_pas_order_allows(uint32_t stage, uint32_t have_done)
{
    uint32_t i = 0u;

    if (stage >= kAdrenoZapPasStageCount) {
        return 0;
    }
    /* Every PRECEDING stage (present or not) must already be marked done.
     * Stages that are not required are expected to be pre-marked by the caller,
     * so this stays a pure prefix test and never inspects `required` itself. */
    for (i = 0u; i < stage; ++i) {
        if ((have_done & (1u << i)) == 0u) {
            return 0;
        }
    }
    return 1;
}

uint32_t adreno_zap_pas_done_bit(uint32_t stage)
{
    if (stage >= kAdrenoZapPasStageCount) {
        return 0u;
    }
    return 1u << stage;
}

int adreno_zap_pas_judge(int transport_ret, uint32_t secure_result,
                         struct adreno_zap_pas_result *out)
{
    if (out == 0) {
        return 1;
    }
    out->transport_ret = transport_ret;
    out->secure_result = secure_result;
    out->transport_ok = transport_ret == 0 ? 1 : 0;
    out->secure_ok = secure_result == 0u ? 1 : 0;
    /* P2:729 `return ret ?: res.result[0];` -- BOTH channels must be zero. A
     * loader that logs only `transport_ret` reports a secure-world rejection as
     * a success, which is the exact failure this record exists to prevent. */
    out->is_success = (out->transport_ok != 0 && out->secure_ok != 0) ? 1 : 0;
    return 0;
}

int adreno_zap_pas_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    uint32_t i = 0u;
    uint32_t count = 0u;
    uint32_t base = 0u;
    struct adreno_zap_pas_call seq[kAdrenoZapPasStageCount];
    struct adreno_zap_pas_region region;
    struct adreno_zap_pas_result res;

    region.phys = 0x1000u;
    region.bytes = 0x1000u;

    /* ---- SCM_SMC_FNID arithmetic vs the pinned macro (P3:69) ---------- */
    base = adreno_zap_smc_base_derived();
    checks += 1u;
    if (base != kAdrenoZapSmcBaseDerived) {
        fails += 1u;
    }
    checks += 1u;
    if (base != 0xC2000000u) {
        fails += 1u; /* the DERIVED base this round was given */
    }
    /* The three function ids. 0x0201, 0x0202, 0x0205 -- the ((s&0xFF)<<8)|(c&0xFF)
     * form, NOT the legacy ((s)<<10)|(c&0x3ff) form at P3:76. */
    checks += 1u;
    if (adreno_zap_scm_fnid(0x02u, 0x01u) != 0x0201u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_scm_fnid(0x02u, 0x02u) != 0x0202u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_scm_fnid(0x02u, 0x05u) != 0x0205u) {
        fails += 1u;
    }
    /* The two encodings must differ, or the shift distinction was lost. */
    checks += 1u;
    if (adreno_zap_scm_fnid(0x04u, 0x00u) == ((0x04u << 10) | 0x00u)) {
        fails += 1u;
    }
    /* Masking really applies to BOTH fields. */
    checks += 1u;
    if (adreno_zap_scm_fnid(0x102u, 0x205u) != 0x0205u) {
        fails += 1u;
    }
    /* The x0 really is base | fnid, not base + fnid. */
    checks += 1u;
    if ((base | 0x0205u) != 0xC2000205u) {
        fails += 1u;
    }

    /* ---- The owner is DERIVED from the base, and it is 2 ------------- */
    checks += 1u;
    if (adreno_zap_smc_base_owner(base) != kAdrenoZapScmOwnerSip) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_smc_base_owner(base) != 2u) {
        fails += 1u; /* ARM_SMCCC_OWNER_SIP is 2, NOT 1 (round-106 correction) */
    }
    checks += 1u;
    if (adreno_zap_smc_base_owner(0xC1000000u) == kAdrenoZapScmOwnerSip) {
        fails += 1u; /* owner 1 must NOT read as SIP */
    }

    /* ---- The base gate refuses wrong owners and non-SMC64 bases ------- */
    checks += 1u;
    if (adreno_zap_smc_base_gate(base) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_smc_base_gate(0xC1000000u) != 0) {
        fails += 1u; /* owner 1 */
    }
    checks += 1u;
    if (adreno_zap_smc_base_gate(0x02000000u) != 0) {
        fails += 1u; /* no SMCCC top bits at all */
    }
    checks += 1u;
    if (adreno_zap_smc_base_gate(0xC3000000u) != 0) {
        fails += 1u; /* owner 3 -- SMC64 but not SIP */
    }
    /* The provenance string is NOT a pin, and says so. */
    checks += 1u;
    if (adreno_zap_smc_base_provenance()[0] != 'd') {
        fails += 1u; /* "derived-unpinned" */
    }
    checks += 1u;
    if (adreno_zap_smc_base_provenance()[8] != 'u') {
        fails += 1u; /* the "unpinned" half must be present too */
    }
    /* ---- The three stages, their ids and their ARGINFO -------------- */
    checks += 1u;
    if (kAdrenoZapPasStageCount != 3u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoZapPasStageInitImage != 0u ||
        kAdrenoZapPasStageMemSetup != 1u ||
        kAdrenoZapPasStageAuthAndReset != 2u) {
        fails += 1u; /* the ORDER is the enum itself */
    }
    /* arginfo values recomputed from P3:29-42 by hand, not copied. */
    checks += 1u;
    if ((((0u & 0x3u) << 4) | ((2u & 0x3u) << 6) | 2u) != kAdrenoZapArgInfoInitImage) {
        fails += 1u; /* ARGS(2, VAL, RW) == 0x82 */
    }
    checks += 1u;
    if (kAdrenoZapArgInfoInitImage != 0x82u) {
        fails += 1u;
    }
    checks += 1u;
    if (((3u & 0xFu)) != kAdrenoZapArgInfoMemSetup) {
        fails += 1u; /* ARGS(3) == 0x03 -- the COUNT is 3, not 1 */
    }
    checks += 1u;
    if (((1u & 0xFu)) != kAdrenoZapArgInfoAuthAndReset) {
        fails += 1u; /* ARGS(1) == 0x01 */
    }
    /* The round-73 shape's ARGS(1) is retained and still 1. */
    checks += 1u;
    if (kAdrenoZapScmArgCount != 1u) {
        fails += 1u;
    }

    /* ---- Per-stage call resolution ---------------------------------- */
    for (i = 0u; i < kAdrenoZapPasStageCount; ++i) {
        checks += 1u;
        if (adreno_zap_pas_call(i, base, 1, &region, &seq[i]) != 0) {
            fails += 1u;
        }
    }
    checks += 1u;
    if (seq[0].cmd != 0x01u || seq[1].cmd != 0x02u || seq[2].cmd != 0x05u) {
        fails += 1u; /* P3:98/99/100 */
    }
    checks += 1u;
    if (seq[0].svc != 0x02u || seq[1].svc != 0x02u || seq[2].svc != 0x02u) {
        fails += 1u; /* P3:97 */
    }
    checks += 1u;
    if (seq[0].owner != 2u || seq[1].owner != 2u || seq[2].owner != 2u) {
        fails += 1u;
    }
    checks += 1u;
    if (seq[0].args0 != 13u || seq[1].args0 != 13u || seq[2].args0 != 13u) {
        fails += 1u; /* P2:710 args[0] is the peripheral */
    }
    /* arginfo -> arg_count is DERIVED, so these must track it. */
    checks += 1u;
    if (seq[0].arg_count != 2u) {
        fails += 1u; /* init_image takes metadata ptr + is RW */
    }
    checks += 1u;
    if (seq[1].arg_count != 3u) {
        fails += 1u; /* mem_setup takes peripheral + addr + size */
    }
    checks += 1u;
    if (seq[2].arg_count != 1u) {
        fails += 1u; /* auth_and_reset takes only the peripheral */
    }
    /* x0 per stage, with the derived base, and the three must be distinct. */
    checks += 1u;
    if (seq[0].x0 != 0xC2000201u) {
        fails += 1u;
    }
    checks += 1u;
    if (seq[1].x0 != 0xC2000202u) {
        fails += 1u;
    }
    checks += 1u;
    if (seq[2].x0 != 0xC2000205u) {
        fails += 1u;
    }
    checks += 1u;
    if (seq[0].x0 == seq[1].x0 || seq[1].x0 == seq[2].x0 ||
        seq[0].x0 == seq[2].x0) {
        fails += 1u;
    }
    /* Exactly one stage carries metadata, exactly one carries a size. */
    checks += 1u;
    if (seq[0].is_metadata_call != 1 || seq[1].is_metadata_call != 0 ||
        seq[2].is_metadata_call != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (seq[0].carries_size_arg != 0 || seq[1].carries_size_arg != 1 ||
        seq[2].carries_size_arg != 0) {
        fails += 1u;
    }

    /* ---- stage 1 is REQUIRED because the real blob is relocatable ------ */
    checks += 1u;
    if (adreno_zap_pas_stage_required(kAdrenoZapPasStageInitImage, 0) != 1) {
        fails += 1u; /* unconditional */
    }
    checks += 1u;
    if (adreno_zap_pas_stage_required(kAdrenoZapPasStageAuthAndReset, 0) != 1) {
        fails += 1u; /* unconditional */
    }
    checks += 1u;
    if (adreno_zap_pas_stage_required(kAdrenoZapPasStageMemSetup, 1) != 1) {
        fails += 1u; /* P5:251-258 relocate=1 -> REQUIRED, this board */
    }
    checks += 1u;
    if (adreno_zap_pas_stage_required(kAdrenoZapPasStageMemSetup, 0) != 0) {
        fails += 1u; /* relocate=0 -> the conditional is not taken */
    }
    checks += 1u;
    if (adreno_zap_pas_stage_required(kAdrenoZapPasStageCount, 1) != 0) {
        fails += 1u; /* out of range is never "required" */
    }
    checks += 1u;
    if (adreno_zap_pas_required_count(1) != 3u) {
        fails += 1u; /* the real a630_zap.mdt: all three */
    }
    checks += 1u;
    if (adreno_zap_pas_required_count(0) != 2u) {
        fails += 1u; /* a non-relocatable blob skips mem_setup */
    }
    /* The required flag is COMPUTED into the record, not copied from a table. */
    checks += 1u;
    if (seq[1].required != 1) {
        fails += 1u;
    }
    checks += 1u;
    {
        struct adreno_zap_pas_call nonrel;
        if (adreno_zap_pas_call(kAdrenoZapPasStageMemSetup, base, 0, &region,
                                &nonrel) != 0) {
            fails += 1u;
        } else if (nonrel.required != 0) {
            fails += 1u;
        }
    }

    /* ---- Sequence enumeration is ordered and complete ---------------- */
    checks += 1u;
    if (adreno_zap_pas_sequence(base, 1, &region, seq, kAdrenoZapPasStageCount,
                                &count) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != kAdrenoZapPasStageCount) {
        fails += 1u;
    }
    checks += 1u;
    for (i = 0u; i < count; ++i) {
        if (seq[i].stage != i) {
            fails += 1u;
            break;
        }
    }
    /* Refusals: short capacity, NULL, and a bad base. */
    checks += 1u;
    if (adreno_zap_pas_sequence(base, 1, &region, seq,
                                kAdrenoZapPasStageCount - 1u, &count) == 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_pas_sequence(base, 1, &region, 0, kAdrenoZapPasStageCount,
                                &count) == 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_pas_sequence(0xC1000000u, 1, &region, seq,
                                kAdrenoZapPasStageCount, &count) == 0) {
        fails += 1u; /* the base gate is enforced by the sequence too */
    }
    checks += 1u;
    if (adreno_zap_pas_call(1u, base, 1, 0, seq) == 0) {
        fails += 1u; /* mem_setup without a region cannot be expressed */
    }
    checks += 1u;
    if (adreno_zap_pas_call(kAdrenoZapPasStageCount, base, 1, &region, seq) == 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_pas_call(0u, base, 1, &region, 0) == 0) {
        fails += 1u;
    }

    /* ---- ORDER enforcement: a prefix, not a set --------------------- */
    checks += 1u;
    if (adreno_zap_pas_order_allows(0u, 0x0u) != 1) {
        fails += 1u; /* init_image may always go first */
    }
    checks += 1u;
    if (adreno_zap_pas_order_allows(2u, 0x0u) != 0) {
        fails += 1u; /* auth_and_reset with NOTHING done must be refused */
    }
    checks += 1u;
    if (adreno_zap_pas_order_allows(1u, 0x1u) != 1) {
        fails += 1u; /* init done -> mem_setup allowed */
    }
    checks += 1u;
    if (adreno_zap_pas_order_allows(2u, 0x1u) != 0) {
        fails += 1u; /* mem_setup still missing */
    }
    checks += 1u;
    if (adreno_zap_pas_order_allows(2u, 0x3u) != 1) {
        fails += 1u; /* both prior stages done -> auth allowed */
    }
    checks += 1u;
    if (adreno_zap_pas_order_allows(0u, 0x7u) != 1) {
        fails += 1u; /* re-running stage 0 is not a false refusal */
    }
    checks += 1u;
    if (adreno_zap_pas_order_allows(kAdrenoZapPasStageCount, 0x7u) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_pas_done_bit(0u) != 1u ||
        adreno_zap_pas_done_bit(1u) != 2u ||
        adreno_zap_pas_done_bit(2u) != 4u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_pas_done_bit(kAdrenoZapPasStageCount) != 0u) {
        fails += 1u;
    }

    /* ---- The two-channel judgement --------------------------------- */
    checks += 1u;
    if (adreno_zap_pas_judge(0, 0u, &res) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (res.is_success != 1 || res.transport_ok != 1 || res.secure_ok != 1) {
        fails += 1u; /* the ONLY success */
    }
    checks += 1u;
    if (adreno_zap_pas_judge(0, 1u, &res) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (res.is_success != 0) {
        fails += 1u; /* secure-world rejection despite a clean transport */
    }
    /* ...and the record still says WHICH channel was clean. */
    checks += 1u;
    if (res.transport_ok != 1 || res.secure_ok != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_pas_judge(-13, 0u, &res) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (res.is_success != 0 || res.transport_ok != 0 || res.secure_ok != 1) {
        fails += 1u; /* transport failure, secure channel untouched */
    }
    /* It must agree with the round-73 collapsed predicate on every case. */
    for (i = 0u; i < 4u; ++i) {
        static const int tr[4] = {0, 0, -1, -13};
        static const uint32_t sr[4] = {0u, 1u, 0u, 0xdeadbeefu};
        int collapsed = -1;
        checks += 1u;
        if (adreno_zap_result_semantics(tr[i], sr[i], &collapsed) != 0) {
            fails += 1u;
        } else if (adreno_zap_pas_judge(tr[i], sr[i], &res) != 0) {
            fails += 1u;
        } else if (collapsed != res.is_success) {
            fails += 1u; /* the two APIs must NEVER disagree */
        }
    }
    checks += 1u;
    if (adreno_zap_pas_judge(0, 0u, 0) == 0) {
        fails += 1u;
    }

    /* ---- Names are total and distinct ------------------------------ */
    for (i = 0u; i < kAdrenoZapPasStageCount; ++i) {
        uint32_t j = 0u;
        const char *a = adreno_zap_pas_stage_name(i);
        checks += 1u;
        if (a[0] == '\0' || (a[0] == 'u' && a[1] == 'n')) {
            fails += 1u;
        }
        checks += 1u;
        if (adreno_zap_pas_stage_anchor(i)[0] == '\0') {
            fails += 1u;
        }
        for (j = 0u; j < i; ++j) {
            const char *b = adreno_zap_pas_stage_name(j);
            uint32_t k = 0u;
            int same = 1;
            while (a[k] != '\0' || b[k] != '\0') {
                if (a[k] != b[k]) {
                    same = 0;
                    break;
                }
                k += 1u;
            }
            checks += 1u;
            if (same != 0) {
                fails += 1u;
            }
        }
    }
    checks += 1u;
    if (adreno_zap_pas_stage_name(kAdrenoZapPasStageCount)[0] != 'u') {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_zap_pas_stage_anchor(kAdrenoZapPasStageCount)[0] != 'u') {
        fails += 1u;
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return fails == 0u ? 0 : 1;
}
