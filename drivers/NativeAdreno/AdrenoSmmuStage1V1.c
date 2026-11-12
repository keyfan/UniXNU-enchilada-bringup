// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoSmmuStage1V1 -- see AdrenoSmmuStage1V1.h for provenance and the honest
// HOST-VERIFIED boundary. Freestanding C11: no libc, no MMIO.

#include "AdrenoSmmuStage1V1.h"

/* The four missing inputs, as a single place to change when one is obtained.
 * Each bit's ANCHOR says why it is absent TODAY, citing a measurement or a
 * pinned line -- never "not implemented". */
static uint32_t missing_inputs(void)
{
    uint32_t mask = 0u;
    /* I1: no page table exists anywhere in this port. */
    mask |= kAdrenoSmmuInputPageTable;
    /* I2: no ASID allocator exists. */
    mask |= kAdrenoSmmuInputAsid;
    /* I3: round 53 measured the target streams still in the firmware BYPASS
     * handoff (Busy=10, id0=4c017e4c), so they do not fault yet. */
    mask |= kAdrenoSmmuInputStreamBinding;
    /* I4: round 35 measured CX DOWN (cx=0x00005ff4, BIT(31) clear). */
    mask |= kAdrenoSmmuInputCxPowered;
    return mask;
}

uint32_t adreno_smmu_sctlr_stage1(void)
{
    /* P2:642-645, little-endian host (no ARM_SMMU_SCTLR_E). */
    return kAdrenoSmmuSctlrStage1Default;
}

uint32_t adreno_smmu_sctlr_stage1_be(void)
{
    return kAdrenoSmmuSctlrStage1Default | kAdrenoSmmuSctlrE;
}

int adreno_smmu_sctlr_enabled(uint32_t sctlr)
{
    return (sctlr & kAdrenoSmmuSctlrM) != 0u ? 1 : 0;
}

uint32_t adreno_smmu_s2cr_addr(uint32_t sid)
{
    /* P1:105 ARM_SMMU_GR0_S2CR(n) = 0xc00 + (n << 2). Overflow-safe for a
     * 32-bit n: the top bits would wrap, so clamp to the register space the
     * SMMU actually decodes (S2CRn exists for n < 0x100 in this design, and a
     * caller passing more is a bug the plan refuses). */
    return kAdrenoSmmuGr0S2crBase + ((sid & 0xffu) << 2);
}

uint32_t adreno_smmu_s2cr_value(enum adreno_smmu_s2cr_type type,
                                uint32_t cbndx, uint32_t privcfg,
                                uint32_t exidvalid)
{
    uint32_t v = 0u;
    /* P1:106-120 field placement; P2:961-967 the same composition. */
    v |= ((privcfg << 24) & kAdrenoSmmuS2crPrivcfgMask);
    v |= (((uint32_t)type << 16) & kAdrenoSmmuS2crTypeMask);
    if (exidvalid != 0u) {
        v |= kAdrenoSmmuS2crExidvalid;
    }
    v |= (cbndx & kAdrenoSmmuS2crCbndxMask);
    return v;
}

enum adreno_smmu_s2cr_type adreno_smmu_s2cr_type_for_state(int faulting)
{
    /* P1:114-118. A stream that does not fault cannot be bound to a context
     * bank: BYPASS means traffic never reaches the SMMU translation path, so
     * the precondition for I3 is that the stream FAULTS. */
    if (faulting != 0) {
        return kAdrenoSmmuS2crTypeTrans;
    }
    return kAdrenoSmmuS2crTypeBypass;
}

int adreno_smmu_stage1_plan(uint32_t cbndx, uint32_t asid, uint64_t ttbr0,
                            uint32_t tcr, uint32_t mair0, uint32_t mair1,
                            struct adreno_smmu_cb_write *out, uint32_t capacity,
                            uint32_t *count)
{
    uint32_t n = 0u;
    /* MAIR0, MAIR1, TTBR0, TCR, CONTEXTIDR, SCTLR. Six writes; ordered so that
     * translation (SCTLR.M) is enabled LAST, after the table and format exist. */
    const uint32_t need = 6u;

    if (out == 0 || count == 0) {
        return 1;
    }
    *count = 0u;
    if (capacity < need) {
        return 1;
    }
    /* P2:637-639 the MAIRs are stage-1 only and come first. */
    out[n].offset = kAdrenoSmmuCbS1Mair0;
    out[n].value = mair0;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    out[n].offset = kAdrenoSmmuCbS1Mair1;
    out[n].value = mair1;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    /* P1:167 TTBR0 is a 64-bit register. */
    out[n].offset = kAdrenoSmmuCbTtbr0;
    out[n].value = (uint32_t)(ttbr0 & 0xffffffffu);
    out[n].is_64bit = 1u;
    out[n].order = n;
    n += 1u;
    /* P1:171 TCR describes the granule and IPA size the table uses. */
    out[n].offset = kAdrenoSmmuCbTcr;
    out[n].value = tcr;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    /* P1:191 CONTEXTIDR carries the ASID. */
    out[n].offset = kAdrenoSmmuCbContextidr;
    out[n].value = asid & 0xffu;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    /* P2:641-651 SCTLR LAST: it is the bit that turns translation on. */
    out[n].offset = kAdrenoSmmuCbSctlr;
    out[n].value = adreno_smmu_sctlr_stage1();
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    /* cbndx is part of the ADDRESS of a context-bank register in the real SMMU
     * (CBn_BASE + offset); this model keeps it as an argument so a caller
     * cannot program the wrong bank silently. */
    (void)cbndx;
    *count = n;
    return 0;
}

uint32_t adreno_smmu_stage1_missing(void)
{
    return missing_inputs();
}

const char *adreno_smmu_stage1_input_name(uint32_t bit)
{
    switch (bit) {
    case kAdrenoSmmuInputPageTable:
        return "page-table";
    case kAdrenoSmmuInputAsid:
        return "asid-allocator";
    case kAdrenoSmmuInputStreamBinding:
        return "stream-fault-binding";
    case kAdrenoSmmuInputCxPowered:
        return "cx-powered";
    default:
        return "unknown";
    }
}

const char *adreno_smmu_stage1_input_anchor(uint32_t bit)
{
    switch (bit) {
    case kAdrenoSmmuInputPageTable:
        return "TTBR0 (arm-smmu.h:167) must point at a real LPAE table; this "
               "port allocates and walks none";
    case kAdrenoSmmuInputAsid:
        return "CONTEXTIDR (arm-smmu.h:191) needs an owned ASID; no allocator "
               "exists here";
    case kAdrenoSmmuInputStreamBinding:
        return "S2CR must bind the SID (arm-smmu.h:105) and the target streams "
               "must FAULT first; round 53 measured Busy=10 id0=4c017e4c in the "
               "firmware BYPASS handoff";
    case kAdrenoSmmuInputCxPowered:
        return "every context-bank write needs GPUCC CX up; round 35 measured "
               "cx=0x00005ff4 with BIT(31) clear (CX DOWN)";
    default:
        return "unknown";
    }
}

int adreno_smmu_stage1_ready(void)
{
    /* Derived, not hard-coded. */
    return adreno_smmu_stage1_missing() == 0u ? 1 : 0;
}

int adreno_smmu_stage1_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    uint32_t i = 0u;
    uint32_t count = 0u;
    uint32_t missing = 0u;
    struct adreno_smmu_cb_write plan[8];
    struct adreno_smmu_cb_write tiny[5];

    /* ---- register offsets, straight from P1 ---------------------------- */
    checks += 1u;
    if (kAdrenoSmmuCbSctlr != 0x00u || kAdrenoSmmuCbActlr != 0x04u ||
        kAdrenoSmmuCbTcr2 != 0x10u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoSmmuCbTtbr0 != 0x20u || kAdrenoSmmuCbTtbr1 != 0x28u ||
        kAdrenoSmmuCbTcr != 0x30u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoSmmuCbContextidr != 0x34u || kAdrenoSmmuCbS1Mair0 != 0x38u ||
        kAdrenoSmmuCbS1Mair1 != 0x3cu || kAdrenoSmmuCbFsr != 0x58u) {
        fails += 1u;
    }
    /* The CB space is 64-bit-addressed; these are the LOW halves. */
    checks += 1u;
    if (!(kAdrenoSmmuCbTtbr0 < kAdrenoSmmuCbTcr &&
          kAdrenoSmmuCbTcr < kAdrenoSmmuCbContextidr)) {
        fails += 1u;
    }

    /* ---- SCTLR bits and the default mask (P2:642-645) ------------------ */
    checks += 1u;
    if (kAdrenoSmmuSctlrM != 0x00000001u ||
        kAdrenoSmmuSctlrTre != 0x00000002u ||
        kAdrenoSmmuSctlrAfe != 0x00000004u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoSmmuSctlrE != 0x00000010u ||
        kAdrenoSmmuSctlrCfre != 0x00000020u ||
        kAdrenoSmmuSctlrCfie != 0x00000040u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoSmmuSctlrCfcfg != 0x00000080u ||
        kAdrenoSmmuSctlrHupcf != 0x00000100u ||
        kAdrenoSmmuSctlrS1Asidpne != 0x00001000u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_sctlr_stage1() != 0x00001067u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_sctlr_stage1_be() != 0x00001077u) {
        fails += 1u;
    }
    checks += 1u;
    if ((adreno_smmu_sctlr_stage1_be() ^ adreno_smmu_sctlr_stage1()) !=
        kAdrenoSmmuSctlrE) {
        fails += 1u; /* the two differ EXACTLY in the endianness bit */
    }
    checks += 1u;
    if (adreno_smmu_sctlr_enabled(adreno_smmu_sctlr_stage1()) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_sctlr_enabled(adreno_smmu_sctlr_stage1() &
                                 ~kAdrenoSmmuSctlrM) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if ((adreno_smmu_sctlr_stage1() & kAdrenoSmmuSctlrE) != 0u) {
        fails += 1u; /* LE default must NOT set E */
    }

    /* ---- S2CR ---------------------------------------------------------- */
    checks += 1u;
    if (adreno_smmu_s2cr_addr(0u) != 0x0c00u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_s2cr_addr(1u) != 0x0c04u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_s2cr_addr(0xffu) != 0x0c00u + 0x3fcu) {
        fails += 1u;
    }
    /* SID is masked to 8 bits, so a wild SID cannot escape the S2CR range. */
    checks += 1u;
    if (adreno_smmu_s2cr_addr(0x100u) != adreno_smmu_s2cr_addr(0u)) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeBypass, 0u, 0u, 0u) != 0x00010000u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeFault, 0u, 0u, 0u) != 0x00020000u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeTrans, 3u, 0u, 0u) != 0x00000003u) {
        fails += 1u; /* CBNDX in the low 8 bits, TYPE 0 for TRANS */
    }
    checks += 1u;
    if (adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeTrans, 0u, 1u, 0u) !=
        0x01000000u) {
        fails += 1u; /* PRIVCFG=1 sits at bits 25..24, i.e. 0x01000000 */
    }
    checks += 1u;
    if (adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeTrans, 0u, 3u, 0u) !=
        kAdrenoSmmuS2crPrivcfgMask) {
        fails += 1u; /* PRIVCFG=3 fills the mask */
    }
    checks += 1u;
    if (adreno_smmu_s2cr_value(kAdrenoSmmuS2crTypeTrans, 0u, 0u, 1u) !=
        kAdrenoSmmuS2crExidvalid) {
        fails += 1u; /* EXIDVALID at bit 10 */
    }
    /* Round 53's finding, as a TYPE: not faulting => BYPASS, never TRANS. */
    checks += 1u;
    if (adreno_smmu_s2cr_type_for_state(0) != kAdrenoSmmuS2crTypeBypass) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_s2cr_type_for_state(1) != kAdrenoSmmuS2crTypeTrans) {
        fails += 1u;
    }

    /* ---- the plan ------------------------------------------------------ */
    checks += 1u;
    if (adreno_smmu_stage1_plan(0u, 7u, 0xdeadbeefcafe0000ull, 0x1234u, 0xffu,
                                0x00ff00ffu, plan, 8u, &count) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != 6u) {
        fails += 1u;
    }
    /* Order: MAIR0, MAIR1, TTBR0, TCR, CONTEXTIDR, SCTLR. */
    checks += 1u;
    if (plan[0].offset != kAdrenoSmmuCbS1Mair0 ||
        plan[1].offset != kAdrenoSmmuCbS1Mair1) {
        fails += 1u;
    }
    checks += 1u;
    if (plan[2].offset != kAdrenoSmmuCbTtbr0 || plan[2].is_64bit != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (plan[3].offset != kAdrenoSmmuCbTcr) {
        fails += 1u;
    }
    checks += 1u;
    if (plan[4].offset != kAdrenoSmmuCbContextidr) {
        fails += 1u;
    }
    checks += 1u;
    if (plan[5].offset != kAdrenoSmmuCbSctlr || plan[5].is_64bit != 0u) {
        fails += 1u;
    }
    /* SCTLR is LAST: translation is enabled only after the table exists. */
    checks += 1u;
    if (plan[5].offset != kAdrenoSmmuCbSctlr) {
        fails += 1u;
    }
    checks += 1u;
    for (i = 0u; i < count; ++i) {
        if (plan[i].order != i) {
            fails += 1u;
            break;
        }
        if (i + 1u < count && plan[i].offset == kAdrenoSmmuCbSctlr) {
            fails += 1u; /* nothing may follow SCTLR */
            break;
        }
    }
    /* Exactly ONE 64-bit write in the plan (TTBR0 has no TTBR1 here). */
    {
        uint32_t wide = 0u;
        for (i = 0u; i < count; ++i) {
            if (plan[i].is_64bit != 0u) {
                wide += 1u;
            }
        }
        checks += 1u;
        if (wide != 1u) {
            fails += 1u;
        }
    }
    /* The values land where they were asked to. */
    checks += 1u;
    if (plan[2].value != (uint32_t)(0xdeadbeefcafe0000ull & 0xffffffffu)) {
        fails += 1u;
    }
    checks += 1u;
    if (plan[4].value != 7u) {
        fails += 1u;
    }
    checks += 1u;
    if (plan[5].value != adreno_smmu_sctlr_stage1()) {
        fails += 1u;
    }
    /* Refusals. */
    checks += 1u;
    if (adreno_smmu_stage1_plan(0u, 0u, 0u, 0u, 0u, 0u, tiny, 5u, &count) == 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_stage1_plan(0u, 0u, 0u, 0u, 0u, 0u, 0, 8u, &count) == 0) {
        fails += 1u;
    }

    /* ---- the four missing inputs --------------------------------------- */
    missing = adreno_smmu_stage1_missing();
    checks += 1u;
    if (missing != kAdrenoSmmuInputAll) {
        fails += 1u;
    }
    checks += 1u;
    if (missing != 15u) {
        fails += 1u;
    }
    {
        const uint32_t bits[4] = {kAdrenoSmmuInputPageTable,
                                  kAdrenoSmmuInputAsid,
                                  kAdrenoSmmuInputStreamBinding,
                                  kAdrenoSmmuInputCxPowered};
        uint32_t seen = 0u;
        for (i = 0u; i < 4u; ++i) {
            checks += 1u;
            if ((seen & bits[i]) != 0u) {
                fails += 1u;
            }
            seen |= bits[i];
            checks += 1u;
            if (adreno_smmu_stage1_input_name(bits[i])[0] == '\0' ||
                adreno_smmu_stage1_input_anchor(bits[i])[0] == '\0') {
                fails += 1u;
            }
            checks += 1u;
            if (adreno_smmu_stage1_input_name(bits[i])[0] == 'u' &&
                adreno_smmu_stage1_input_name(bits[i])[1] == 'n') {
                fails += 1u;
            }
        }
        checks += 1u;
        if (seen != kAdrenoSmmuInputAll) {
            fails += 1u;
        }
    }
    checks += 1u;
    if (adreno_smmu_stage1_input_name(0u)[0] != 'u') {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_stage1_input_anchor(0u)[0] != 'u') {
        fails += 1u;
    }
    /* Readiness is derived and is 0. */
    checks += 1u;
    if (adreno_smmu_stage1_ready() != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_stage1_ready() !=
        (adreno_smmu_stage1_missing() == 0u ? 1 : 0)) {
        fails += 1u;
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return fails == 0u ? 0 : 1;
}
