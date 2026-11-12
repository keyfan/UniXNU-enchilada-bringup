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
