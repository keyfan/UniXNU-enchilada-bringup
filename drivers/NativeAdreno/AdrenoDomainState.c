// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * AdrenoDomainState.c - pure predicate implementation for the enchilada GPU
 *                       domain-state probe design. No MMIO, no IOKit, no
 *                       platform headers: this file must remain compilable and
 *                       testable on any host.
 */

#include "AdrenoDomainState.h"

/* Forbidden windows: the GPU windows that live INSIDE the collapsible
 * gpu_cx / gpu_gx domains. Reading any of these with the domain down can hang
 * the bus, so no probe may touch them before the full power-up sequence.
 * Bases/sizes come from the board DTB qcom,kgsl-3d0 / qcom,gmu nodes. */
static const struct {
    uint64_t base;
    uint64_t size;
} gAdrenoDomainForbidden[ADRENO_DOMAIN_FORBIDDEN_COUNT] = {
    { 0x05000000ull, 0x40000ull },  /* :23636 kgsl-3d0 reg[0]           */
    { 0x05061000ull, 0x800ull   },  /* :23636 kgsl_3d0_cx_dbgc_memory   */
    { 0x0509e000ull, 0x1000ull  },  /* :23636 cx_misc                   */
    { 0x0506a000ull, 0x30000ull },  /* :23875 qcom,gmu reg[0]           */
    { 0x0b200000ull, 0x300000ull},  /* :23875 qcom,gmu reg[1] (vbif)    */
    { 0x05040000ull, 0x10000ull },  /* :13960 arm,smmu-kgsl             */
};

int adreno_domain_addr_is_probe_window(uint64_t addr)
{
    if (addr < ADRENO_DOMAIN_GPUCC_BASE) {
        return 0;
    }
    return (addr - ADRENO_DOMAIN_GPUCC_BASE) < ADRENO_DOMAIN_GPUCC_SIZE;
}

int adreno_domain_cx_is_on(uint32_t status_word)
{
    return (status_word & (1u << ADRENO_DOMAIN_CX_ON_BIT)) != 0u;
}

int adreno_domain_gx_is_on(uint32_t status_word)
{
    return (status_word & (1u << ADRENO_DOMAIN_GX_ON_BIT)) != 0u;
}

int adreno_domain_gx_is_off(uint32_t status_word)
{
    return (status_word & (1u << ADRENO_DOMAIN_GX_OFF_BIT)) != 0u;
}

int adreno_domain_window_is_forbidden(uint64_t base, uint64_t size)
{
    uint32_t i;

    if (size == 0ull) {
        return 0;
    }

    /* GPUCC carve-out.
     *
     * The board DTB declares qcom,gmu reg = <0x506a000 0x30000>, i.e.
     * 0x0506a000..0x0509a000, which SWALLOWS the GPUCC register file
     * 0x05090000..0x05099000 declared by qcom,gpucc@5090000. The two are
     * physically distinct blocks: the GMU node's size simply over-declares.
     * GPUCC is the power/clock controller FOR the domain, so mainline must be
     * able to read its GDSC status registers while the domain is collapsed
     * (gdsc.c:103-117). A range that lies entirely inside GPUCC is therefore
     * NOT forbidden even though a numeric window overlap says otherwise.
     */
    {
        const uint64_t gbegin = ADRENO_DOMAIN_GPUCC_BASE;
        const uint64_t gend   = ADRENO_DOMAIN_GPUCC_BASE + ADRENO_DOMAIN_GPUCC_SIZE;
        const uint64_t end    = base + size;

        if (end >= base && base >= gbegin && end <= gend) {
            return 0;
        }
    }

    for (i = 0u; i < ADRENO_DOMAIN_FORBIDDEN_COUNT; i++) {
        const uint64_t wbase = gAdrenoDomainForbidden[i].base;
        const uint64_t wend  = wbase + gAdrenoDomainForbidden[i].size;
        const uint64_t end   = base + size;

        /* Overflow-safe overlap test: [base,end) vs [wbase,wend). */
        if (base < wend && wbase < end) {
            return 1;
        }
        /* end wrapped past 2^64: treat as covering everything above base. */
        if (end < base && wend > base) {
            return 1;
        }
    }

    return 0;
}

uint32_t adreno_domain_forbidden_count(void)
{
    return ADRENO_DOMAIN_FORBIDDEN_COUNT;
}
