/* SPDX-License-Identifier: MIT */
/* Round 46: A6XX_GEN1 powered-GMU backend caller (see AdrenoGmuCallerV1.h).
 * Freestanding pure C11; no libc, no MMIO of its own, no kernel linkage.
 * Every hardware access is forwarded to the caller-wired bus / gpucc reader. */
#include "AdrenoGmuCallerV1.h"

#include <stdint.h>
#include <stddef.h>

/* ---- precheck -------------------------------------------------------------- */

static int caller_v1_pre_ok(const struct AdrenoGmuCallerV1Pre *pre)
{
    return pre != (const struct AdrenoGmuCallerV1Pre *)0 &&
           pre->cx_genpd_on != 0u && pre->gx_genpd_on != 0u &&
           pre->clocks_enabled != 0u;
}

AdrenoGmuCallerV1Status AdrenoGmuCallerV1Precheck(
    const struct AdrenoGmuCallerV1Pre *pre,
    const struct AdrenoGmuCallerV1Gpucc *gpucc)
{
    uint32_t cx = 0u;
    uint32_t gx = 0u;

    if (pre == (const struct AdrenoGmuCallerV1Pre *)0 ||
        gpucc == (const struct AdrenoGmuCallerV1Gpucc *)0 ||
        gpucc->read == 0) {
        return kAdrenoGmuCallerV1Invalid;
    }
    if (!caller_v1_pre_ok(pre)) {
        return kAdrenoGmuCallerV1NotReady;
    }
    /* Cross-check the caller-asserted genpd state against the GPUCC status
     * registers mainline gdsc.c polls (gdsc.c:66-71, gpucc-sdm845.c:119-139). */
    if (gpucc->read(gpucc->cookie, ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE,
                    &cx) != 0) {
        return kAdrenoGmuCallerV1BusError;
    }
    if (!adreno_domain_cx_is_on(cx)) {
        return kAdrenoGmuCallerV1CxOff;
    }
    if (gpucc->read(gpucc->cookie, ADRENO_GMU_CALLER_V1_GX_STATUS_BYTE,
                    &gx) != 0) {
        return kAdrenoGmuCallerV1BusError;
    }
    if (pre->require_gx != 0u && !adreno_domain_gx_is_on(gx)) {
        return kAdrenoGmuCallerV1GxOff;
    }
    return kAdrenoGmuCallerV1Ok;
}

/* ---- boot state ------------------------------------------------------------ */

AdrenoGmuCallerV1Status AdrenoGmuCallerV1BootState(
    const AdrenoGmuBootV1Bus *bus, uint32_t *boot_state)
{
    uint32_t general7 = 0u;

    if (bus == (const AdrenoGmuBootV1Bus *)0 || boot_state == (uint32_t *)0 ||
        bus->read == 0) {
        return kAdrenoGmuCallerV1Invalid;
    }
    if (bus->read(bus->cookie, (uint32_t)kAdrenoGmuBootV1DomainGmu,
                  ADRENO_GMU_BOOT_V1_GENERAL_7, &general7) != 0) {
        return kAdrenoGmuCallerV1BusError;
    }
    *boot_state = AdrenoGmuBootV1BootState(general7);
    return kAdrenoGmuCallerV1Ok;
}

/* ---- execute-with-retries -------------------------------------------------- */

/* One Execute pass with retry on Poll ops only. Returns the caller status,
 * writes the flattened op index of the first failing op through fail_idx. */
static AdrenoGmuCallerV1Status caller_v1_execute_retry(
    const AdrenoGmuBootV1Op *ops, uint32_t count,
    const AdrenoGmuBootV1Bus *bus, uint32_t poll_retries,
    uint32_t *fail_idx)
{
    AdrenoGmuBootV1Status st = kAdrenoGmuBootV1Success;
    uint32_t attempt = 0u;

    if (ops == (const AdrenoGmuBootV1Op *)0 || bus == (const AdrenoGmuBootV1Bus *)0 ||
        fail_idx == (uint32_t *)0 || bus->read == 0 || bus->write == 0) {
        return kAdrenoGmuCallerV1Invalid;
    }

    st = AdrenoGmuBootV1Execute(ops, count, bus);
    while (st == kAdrenoGmuBootV1PollTimeout && attempt < poll_retries) {
        attempt = attempt + 1u;
        st = AdrenoGmuBootV1Execute(ops, count, bus);
    }
    if (st == kAdrenoGmuBootV1Success) {
        return kAdrenoGmuCallerV1Ok;
    }
    /* Map the failure to a flattened op index. A PollTimeout can come from
     * ANY poll op in the stream; a BusError is reported at its op. Re-run a
     * cheap scan: find the first op whose semantics could have failed. For
     * diagnostics only; the status code is authoritative. */
    {
        uint32_t i;
        for (i = 0u; i < count; i++) {
            if (ops[i].kind == kAdrenoGmuBootV1OpPoll) {
                *fail_idx = i;
                break;
            }
        }
    }
    if (st == kAdrenoGmuBootV1PollTimeout) {
        return kAdrenoGmuCallerV1PollTimeout;
    }
    return kAdrenoGmuCallerV1BusError;
}

/* ---- full cold boot --------------------------------------------------------- */

AdrenoGmuCallerV1Status AdrenoGmuCallerV1ColdBoot(
    const struct AdrenoGmuCallerV1Pre *pre,
    const struct AdrenoGmuCallerV1Gpucc *gpucc,
    const AdrenoGmuBootV1Inputs *in,
    const uint32_t *rsc_ucode, const uint32_t *pdc_ucode,
    const AdrenoGmuBootV1Bus *bus,
    uint32_t poll_retries,
    uint32_t *first_fail_op)
{
    AdrenoGmuCallerV1Status pre_st = kAdrenoGmuCallerV1Ok;
    AdrenoGmuCallerV1Status st = kAdrenoGmuCallerV1Ok;
    AdrenoGmuBootV1Status p_st = kAdrenoGmuBootV1Success;
    AdrenoGmuBootV1Op ops[ADRENO_GMU_BOOT_V1_MAX_OPS];
    uint32_t count = 0u;
    uint32_t boot_state = 0u;
    uint32_t fail_idx = 0u;
    uint32_t rpmh_ops = 0u;

    if (in == (const AdrenoGmuBootV1Inputs *)0 ||
        bus == (const AdrenoGmuBootV1Bus *)0) {
        return kAdrenoGmuCallerV1Invalid;
    }

    pre_st = AdrenoGmuCallerV1Precheck(pre, gpucc);
    if (pre_st != kAdrenoGmuCallerV1Ok) {
        return pre_st;
    }

    st = AdrenoGmuCallerV1BootState(bus, &boot_state);
    if (st != kAdrenoGmuCallerV1Ok) {
        return st;
    }
    if (boot_state == ADRENO_GMU_BOOT_V1_WARM) {
        /* warm resume (upstream :1049-1068 non-legacy branch work) is out of
         * scope for the cold-only model; fail closed BEFORE any further op. */
        return kAdrenoGmuCallerV1WarmUnsupported;
    }

    /* Probe-time RSC/PDC sequencer setup, when the caller supplies the two
     * ucode tables (upstream a6xx_gmu_rpmh_init, run once at probe). */
    if (rsc_ucode != (const uint32_t *)0 && pdc_ucode != (const uint32_t *)0) {
        p_st = AdrenoGmuBootV1PlanRpmhInit(rsc_ucode, pdc_ucode, ops,
            ADRENO_GMU_BOOT_V1_MAX_OPS, &rpmh_ops);
        if (p_st != kAdrenoGmuBootV1Success) {
            return kAdrenoGmuCallerV1Invalid;
        }
        st = caller_v1_execute_retry(ops, rpmh_ops, bus, poll_retries,
                                     &fail_idx);
        if (st != kAdrenoGmuCallerV1Ok) {
            if (first_fail_op != (uint32_t *)0) {
                *first_fail_op = fail_idx;
            }
            return st;
        }
    }

    /* Cold plan. */
    p_st = AdrenoGmuBootV1PlanCold(in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS, &count);
    if (p_st != kAdrenoGmuBootV1Success) {
        return kAdrenoGmuCallerV1Invalid;
    }
    st = caller_v1_execute_retry(ops, count, bus, poll_retries, &fail_idx);
    if (st != kAdrenoGmuCallerV1Ok) {
        if (first_fail_op != (uint32_t *)0) {
            /* flatten: cold ops follow the rpmh block (when present) */
            *first_fail_op = rpmh_ops + fail_idx;
        }
        return st;
    }
    if (first_fail_op != (uint32_t *)0) {
        *first_fail_op = 0u;
    }
    return kAdrenoGmuCallerV1Ok;
}
