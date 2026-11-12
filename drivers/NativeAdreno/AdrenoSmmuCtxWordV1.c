// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoSmmuCtxWordV1 -- see AdrenoSmmuCtxWordV1.h for provenance and the honest
// HOST-VERIFIED boundary. Freestanding C11: no libc, no MMIO.

#include "AdrenoSmmuCtxWordV1.h"

/* ---- SCTLR bits (P1:145-154) and the stage-1 default (P2:641-652) -----
 * Recomputed here rather than imported, so this module stands alone and the
 * cross-verifier can check the two independent derivations agree. */
#define kCtxSctlrS1Asidpne 0x00001000u /* P1:146 BIT(12) */
#define kCtxSctlrCfie      0x00000040u /* P1:149 BIT(6)  */
#define kCtxSctlrCfre      0x00000020u /* P1:150 BIT(5)  */
#define kCtxSctlrE         0x00000010u /* P1:151 BIT(4)  */
#define kCtxSctlrAfe       0x00000004u /* P1:152 BIT(2)  */
#define kCtxSctlrTre       0x00000002u /* P1:153 BIT(1)  */
#define kCtxSctlrM         0x00000001u /* P1:154 BIT(0)  */

/* P2:642-645: CFIE | CFRE | AFE | TRE | M, plus S1_ASIDPNE for a stage-1 bank.
 * P2:646-647 adds E only when CONFIG_CPU_BIG_ENDIAN, which this board is not. */
#define kCtxSctlrStage1 \
    (kCtxSctlrCfie | kCtxSctlrCfre | kCtxSctlrAfe | kCtxSctlrTre | \
     kCtxSctlrM | kCtxSctlrS1Asidpne)

/* The three absent inputs, in one place. Each anchor says why it is absent
 * TODAY, citing a measurement or a pinned line -- never "not implemented". */
static uint32_t missing_inputs(void)
{
    uint32_t mask = 0u;
    /* ias comes from ID2.UBS, a probe-time register read (round 77). */
    mask |= kAdrenoSmmuCtxInputIas;
    /* No ASID allocator exists in this port (round 75 I2, unchanged). */
    mask |= kAdrenoSmmuCtxInputAsid;
    /* No populated LPAE page table exists in this port (round 75 I1, unchanged). */
    mask |= kAdrenoSmmuCtxInputPageTable;
    return mask;
}

uint32_t adreno_smmu_ctx_id_size_to_bits(uint32_t size)
{
    /* P2:1727-1744 verbatim, including the default arm of the switch: 5 AND
     * every other value collapse to 48. Encoding this as a table without the
     * default would silently invent a width for an undefined IDR field. */
    switch (size) {
    case 0u:
        return 32u;
    case 1u:
        return 36u;
    case 2u:
        return 40u;
    case 3u:
        return 42u;
    case 4u:
        return 44u;
    case 5u:
    default:
        return 48u;
    }
}

uint64_t adreno_smmu_ctx_mair(void)
{
    /* P3:123-130 the four attribute bytes and their indices; P3:1002-1009 the
     * register composition, which shifts each byte by (idx << 3). */
    uint64_t reg = 0u;
    reg |= ((uint64_t)kAdrenoSmmuMairAttrNc <<
            ((uint64_t)kAdrenoSmmuMairIdxNc << 3));
    reg |= ((uint64_t)kAdrenoSmmuMairAttrWbrwa <<
            ((uint64_t)kAdrenoSmmuMairIdxCache << 3));
    reg |= ((uint64_t)kAdrenoSmmuMairAttrDevice <<
            ((uint64_t)kAdrenoSmmuMairIdxDev << 3));
    reg |= ((uint64_t)kAdrenoSmmuMairAttrIncOwbrwa <<
            ((uint64_t)kAdrenoSmmuMairIdxIncOcache << 3));
    return reg;
}

uint32_t adreno_smmu_ctx_mair0(void)
{
    /* P2:560 S1_MAIR0 gets cb->mair[0], the low half. */
    return (uint32_t)(adreno_smmu_ctx_mair() & 0xffffffffull);
}

uint32_t adreno_smmu_ctx_mair1(void)
{
    /* P2:561 S1_MAIR1 gets cb->mair[1] = mair >> 32. On this board every
     * attribute lives in the low 32 bits, so this is 0 -- asserted, not assumed,
     * because a non-zero value here would mean an attribute index changed. */
    return (uint32_t)((adreno_smmu_ctx_mair() >> 32) & 0xffffffffull);
}

uint32_t adreno_smmu_ctx_tcr(uint32_t ias, uint32_t coherent_walk,
                             uint32_t ttbr1, int *ok)
{
    uint32_t tcr = 0u;
    uint32_t sh;
    uint32_t rgn;
    uint32_t tg;

    if (ok != 0) {
        *ok = 0;
    }
    /* A T0SZ of 64-ias only fits the 6-bit field for ias in 16..48, and the
     * LPAE granule this board uses is 4 KiB (round 15 ArmSmmu4K kit). */
    if (ias < 16u || ias > 48u || coherent_walk > 1u || ttbr1 > 1u) {
        return 0u;
    }
    /* P2:944-958: a coherent walk takes inner-shareable WB-WA on both sides; a
     * non-coherent walk takes outer-shareable and non-cacheable. The two
     * branches are NOT free variations -- they describe who maintains
     * coherency, so picking one without IDR0.CTTW would lie about the silicon. */
    if (coherent_walk != 0u) {
        sh = kAdrenoSmmuTcrShIs;
        rgn = kAdrenoSmmuTcrRgnWbwa;
    } else {
        sh = kAdrenoSmmuTcrShOs;
        rgn = kAdrenoSmmuTcrRgnNc;
    }
    /* P2:961-971: TG1 when the TTBR1 geometry is selected, else TG0. */
    tg = (ttbr1 != 0u) ? (uint32_t)kAdrenoSmmuTg1_4K : (uint32_t)kAdrenoSmmuTg0_4K;

    tcr = (tg << 14) | (sh << 12) | (rgn << 10) | (rgn << 8) |
          ((64u - ias) & kAdrenoSmmuTcrT0szMask);
    /* P1:401-409: the TTBR1 geometry shifts the low half into the high half,
     * clears A1, and disables TTBR0 (EPD0). The other geometry disables TTBR1
     * (EPD1). These are exclusive -- setting both would disable all translation. */
    if (ttbr1 != 0u) {
        tcr = ((tcr << 16) & ~kAdrenoSmmuTcrA1) | kAdrenoSmmuTcrEpd0;
    } else {
        tcr |= kAdrenoSmmuTcrEpd1;
    }
    if (ok != 0) {
        *ok = 1;
    }
    return tcr;
}

uint32_t adreno_smmu_ctx_tcr2(uint32_t oas, uint32_t aarch64, int *ok)
{
    uint32_t v;

    if (ok != 0) {
        *ok = 0;
    }
    /* P2:973-997 only seven widths are encodable; the IDR field is 4 bits but
     * arm_smmu_id_size_to_bits() never yields 52 from a 4-bit read, so allowing
     * 52 here would accept a width this SMMU cannot report. */
    if (oas != 32u && oas != 36u && oas != 40u && oas != 42u &&
        oas != 44u && oas != 48u) {
        return 0u;
    }
    /* P1:416-417: PASIZE is the 4-bit IDR encoding of `oas` (the inverse of
     * arm_smmu_id_size_to_bits()), and SEP is always upstream's 0x7. */
    v = (uint32_t)((oas == 32u) ? 0u : (oas == 36u) ? 1u : (oas == 40u) ? 2u :
                   (oas == 42u) ? 3u : (oas == 44u) ? 4u : 5u);
    v |= kAdrenoSmmuTcr2SepUpstream;
    /* P2:525-527: an AArch64 context also sets TCR2_AS. */
    if (aarch64 != 0u) {
        v |= kAdrenoSmmuTcr2As;
    }
    if (ok != 0) {
        *ok = 1;
    }
    return v;
}

uint32_t adreno_smmu_ctx_cbar(uint32_t vmid16, uint32_t vmid)
{
    /* P2:594-608: TYPE = S1_TRANS_S2_BYPASS for a stage-1 domain (P2:733), and
     * for a stage-1 bank the two "weakest" attributes are always set so the
     * page-table entries override them. */
    uint32_t reg = kAdrenoSmmuCbarS1MemattrWb | kAdrenoSmmuCbarS1BpshcfgNsh;
    reg |= ((uint32_t)kAdrenoSmmuCbarTypeS1TransS2Bypass << 16);
    /* P2:608-611: the 8-bit VMID goes in CBAR ONLY when 16-bit VMIDs are absent;
     * the 16-bit form lives in CBA2R (P2:587-589). Writing both would place the
     * same value in two registers. */
    if (vmid16 == 0u) {
        reg |= (vmid & kAdrenoSmmuCbarVmidMask);
    }
    return reg;
}

uint32_t adreno_smmu_ctx_cba2r(uint32_t aarch64, uint32_t vmid16, uint32_t vmid)
{
    /* P2:582-591: only written when the SMMU is newer than v1, VA64 for AArch64,
     * and a 16-bit VMID here when the IDR supports it. */
    uint32_t reg = 0u;
    if (aarch64 != 0u) {
        reg |= kAdrenoSmmuCba2rVa64;
    }
    if (vmid16 != 0u) {
        reg |= ((vmid << 16) & kAdrenoSmmuCba2rVmid16Mask);
    }
    return reg;
}

uint64_t adreno_smmu_ctx_ttbrn(uint32_t asid, uint64_t table)
{
    /* P2:540-548: the ASID occupies bits 63..48 and the table pointer the low
     * bits, and the SAME composition is applied to BOTH TTBRs. */
    return (((uint64_t)(asid & (uint32_t)kAdrenoSmmuTtbrAsidMask))
            << kAdrenoSmmuTtbrAsidShift) | table;
}

uint64_t adreno_smmu_ctx_ttbrn_checked(uint32_t asid, uint64_t table, int *ok)
{
    if (ok != 0) {
        *ok = 0;
    }
    /* The root table must be 4 KiB aligned: bits 11..0 of TTBRn are the size and
     * other fields on real hardware, so an unaligned pointer is not merely
     * imprecise, it programs a different register value than intended. */
    if ((table & 0xfffull) != 0ull) {
        return 0ull;
    }
    /* And the ASID must fit the field, else the shift would silently drop bits. */
    if ((asid & ~(uint32_t)kAdrenoSmmuTtbrAsidMask) != 0u) {
        return 0ull;
    }
    if (ok != 0) {
        *ok = 1;
    }
    return adreno_smmu_ctx_ttbrn(asid, table);
}

uint32_t adreno_smmu_ctx_plan_writes(void)
{
    /* P2:581-653 for an AArch64 stage-1 bank, counted from the source:
     * CBA2R, CBAR, TCR2, TCR, TTBR0, TTBR1, S1_MAIR0, S1_MAIR1, SCTLR. */
    return 9u;
}

uint32_t adreno_smmu_ctx_round75_writes(void)
{
    /* AdrenoSmmuStage1V1.c `const uint32_t need = 6u`. Recorded as a NUMBER so
     * the gap stays measurable rather than becoming folklore. */
    return 6u;
}

int adreno_smmu_ctx_plan(const struct adreno_smmu_ctx_params *p,
                         struct adreno_smmu_ctx_write *out, uint32_t capacity,
                         uint32_t *count)
{
    uint32_t n = 0u;
    uint32_t tcr = 0u;
    uint32_t tcr2 = 0u;
    uint64_t ttbr = 0ull;
    int ok_tcr = 0;
    int ok_tcr2 = 0;
    int ok_ttbr = 0;
    const uint32_t need = adreno_smmu_ctx_plan_writes();

    if (p == 0 || out == 0 || count == 0) {
        return 1;
    }
    *count = 0u;
    if (capacity < need) {
        return 1;
    }
    /* Only the AArch64 form is modelled: the AArch32 short-descriptor form takes
     * its values from arm_v7s_cfg (P2:520-521, :556-558), which is a DIFFERENT
     * page-table format this port does not use and has not pinned. Refusing is
     * the honest answer. */
    if (p->aarch64 == 0u) {
        return 1;
    }
    tcr = adreno_smmu_ctx_tcr(p->ias, p->coherent_walk, p->ttbr1, &ok_tcr);
    if (ok_tcr == 0) {
        return 1;
    }
    tcr2 = adreno_smmu_ctx_tcr2(p->oas, p->aarch64, &ok_tcr2);
    if (ok_tcr2 == 0) {
        return 1;
    }
    ttbr = adreno_smmu_ctx_ttbrn_checked(p->asid, p->ttbr, &ok_ttbr);
    if (ok_ttbr == 0) {
        return 1;
    }

    /* 1. CBA2R -- P2:581-592, GR1, before CBAR. */
    out[n].gr1 = 1u;
    out[n].offset = kAdrenoSmmuGr1Cba2r + ((p->cbndx & 0xffu) << 2);
    out[n].value = adreno_smmu_ctx_cba2r(p->aarch64, p->vmid16, p->vmid);
    out[n].value_hi = 0u;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    /* 2. CBAR -- P2:594-612, GR1, the register that DECLARES the stage-1 type.
     * Its GR1 offset base (0x0) is numerically the same as CB SCTLR (0x00); only
     * the frame distinguishes them, which is why `gr1` exists. */
    out[n].gr1 = 1u;
    out[n].offset = kAdrenoSmmuGr1Cbar + ((p->cbndx & 0xffu) << 2);
    out[n].value = adreno_smmu_ctx_cbar(p->vmid16, p->vmid);
    out[n].value_hi = 0u;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    /* 3. TCR2 -- P2:619-620, BEFORE TCR, because TCR decides how ASID[15:8] is
     * read (P2:614-618). */
    out[n].gr1 = 0u;
    out[n].offset = 0x0010u; /* P1:161 ARM_SMMU_CB_TCR2 */
    out[n].value = tcr2;
    out[n].value_hi = 0u;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    /* 4. TCR -- P2:621. */
    out[n].gr1 = 0u;
    out[n].offset = 0x0030u; /* P1:171 ARM_SMMU_CB_TCR */
    out[n].value = tcr;
    out[n].value_hi = 0u;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    /* 5. TTBR0 -- P2:629, a 64-bit store carrying the ASID. */
    out[n].gr1 = 0u;
    out[n].offset = 0x0020u; /* P1:167 ARM_SMMU_CB_TTBR0 */
    out[n].value = (uint32_t)(ttbr & 0xffffffffull);
    out[n].value_hi = (uint32_t)((ttbr >> 32) & 0xffffffffull);
    out[n].is_64bit = 1u;
    out[n].order = n;
    n += 1u;
    /* 6. TTBR1 -- P2:630-632, stage-1 writes BOTH, and both carry the ASID. */
    out[n].gr1 = 0u;
    out[n].offset = 0x0028u; /* P1:168 ARM_SMMU_CB_TTBR1 */
    out[n].value = (uint32_t)(ttbr & 0xffffffffull);
    out[n].value_hi = (uint32_t)((ttbr >> 32) & 0xffffffffull);
    out[n].is_64bit = 1u;
    out[n].order = n;
    n += 1u;
    /* 7. S1_MAIR0 -- P2:637, AFTER the TTBRs. */
    out[n].gr1 = 0u;
    out[n].offset = 0x0038u; /* P1:192 ARM_SMMU_CB_S1_MAIR0 */
    out[n].value = adreno_smmu_ctx_mair0();
    out[n].value_hi = 0u;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    /* 8. S1_MAIR1 -- P2:638. */
    out[n].gr1 = 0u;
    out[n].offset = 0x003cu; /* P1:193 ARM_SMMU_CB_S1_MAIR1 */
    out[n].value = adreno_smmu_ctx_mair1();
    out[n].value_hi = 0u;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;
    /* 9. SCTLR -- P2:641-652, LAST: it is the bit that turns translation on. */
    out[n].gr1 = 0u;
    out[n].offset = 0x0000u; /* P1:145 ARM_SMMU_CB_SCTLR */
    out[n].value = kCtxSctlrStage1;
    out[n].value_hi = 0u;
    out[n].is_64bit = 0u;
    out[n].order = n;
    n += 1u;

    *count = n;
    return 0;
}

uint32_t adreno_smmu_ctx_missing(void)
{
    return missing_inputs();
}

const char *adreno_smmu_ctx_input_name(uint32_t bit)
{
    switch (bit) {
    case kAdrenoSmmuCtxInputIas:
        return "ias";
    case kAdrenoSmmuCtxInputAsid:
        return "asid";
    case kAdrenoSmmuCtxInputPageTable:
        return "page-table";
    default:
        return "unknown";
    }
}

const char *adreno_smmu_ctx_input_anchor(uint32_t bit)
{
    switch (bit) {
    case kAdrenoSmmuCtxInputIas:
        /* P2:1904-1905 -> P1:70 -> P2:1879: the chain ends at a register read. */
        return "P2:1904-1905 ias <- arm_smmu_id_size_to_bits(ID2.UBS), a probe-time "
               "read (round 77); no document or DTS supplies it";
    case kAdrenoSmmuCtxInputAsid:
        /* P2:795 cfg->asid = cfg->cbndx; this port has no context-bank allocator. */
        return "P2:795 cfg->asid = cfg->cbndx; this port has no context-bank "
               "allocator, so no ASID is owned (round 75 I2, unchanged)";
    case kAdrenoSmmuCtxInputPageTable:
        /* P2:1023 cfg->arm_lpae_s1_cfg.ttbr = virt_to_phys(data->pgd). */
        return "P2:1023 TTBR is virt_to_phys(pgd) of an allocated, populated table; "
               "this port allocates none (round 75 I1, unchanged)";
    default:
        return "unknown";
    }
}

int adreno_smmu_ctx_ready(void)
{
    /* Derived, not hard-coded. */
    return adreno_smmu_ctx_missing() == 0u ? 1 : 0;
}

int adreno_smmu_ctx_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    uint32_t i = 0u;
    uint32_t count = 0u;
    uint32_t seen_gr1 = 0u;
    uint32_t seen_cb = 0u;
    uint32_t wide = 0u;
    uint32_t idx_sctlr = 0xffffffffu;
    uint32_t idx_cbar = 0xffffffffu;
    uint32_t idx_cba2r = 0xffffffffu;
    uint32_t idx_tcr2 = 0xffffffffu;
    uint32_t idx_tcr = 0xffffffffu;
    uint32_t idx_ttbr0 = 0xffffffffu;
    uint32_t idx_ttbr1 = 0xffffffffu;
    uint32_t idx_mair0 = 0xffffffffu;
    uint32_t idx_mair1 = 0xffffffffu;
    struct adreno_smmu_ctx_write plan[12];
    struct adreno_smmu_ctx_write tiny[4];
    struct adreno_smmu_ctx_params p;

    /* ---- the two frame bases, and the collision round 75 could not express --- */
    checks += 1u;
    if (kAdrenoSmmuGr1Cbar != 0x0000u || kAdrenoSmmuGr1Cba2r != 0x0800u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoSmmuGr1Cbar != 0x0000u) {
        fails += 1u; /* and CB SCTLR is also 0x00: only `gr1` separates them */
    }

    /* ---- id_size_to_bits, INCLUDING the collapsing default (P2:1727-1744) --- */
    checks += 1u;
    if (adreno_smmu_ctx_id_size_to_bits(0u) != 32u ||
        adreno_smmu_ctx_id_size_to_bits(1u) != 36u ||
        adreno_smmu_ctx_id_size_to_bits(2u) != 40u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_ctx_id_size_to_bits(3u) != 42u ||
        adreno_smmu_ctx_id_size_to_bits(4u) != 44u ||
        adreno_smmu_ctx_id_size_to_bits(5u) != 48u) {
        fails += 1u;
    }
    /* 5 and EVERY other value collapse to 48 -- the default arm, not a table. */
    checks += 1u;
    if (adreno_smmu_ctx_id_size_to_bits(6u) != 48u ||
        adreno_smmu_ctx_id_size_to_bits(15u) != 48u ||
        adreno_smmu_ctx_id_size_to_bits(0xffffffffu) != 48u) {
        fails += 1u;
    }

    /* ---- MAIR (P3:123-130, P3:1002-1011) --------------------------------- */
    checks += 1u;
    if (adreno_smmu_ctx_mair() != 0xf404ff44ull) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_ctx_mair0() != 0xf404ff44u) {
        fails += 1u;
    }
    /* Every attribute lives in the low half, so MAIR1 is 0 on this board. */
    checks += 1u;
    if (adreno_smmu_ctx_mair1() != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_ctx_mair0() != (uint32_t)(adreno_smmu_ctx_mair() & 0xffffffffull) ||
        adreno_smmu_ctx_mair1() !=
            (uint32_t)((adreno_smmu_ctx_mair() >> 32) & 0xffffffffull)) {
        fails += 1u; /* the split is a pure view of one register (P2:560-561) */
    }

    /* ---- TCR: the TTBR0 geometry, recomputed at every ias (P1:393-412) --- */
    {
        int ok = 0;
        uint32_t v = adreno_smmu_ctx_tcr(40u, 1u, 0u, &ok);
        checks += 1u;
        if (ok != 1 || v != 0x00803518u) {
            fails += 1u;
        }
        /* EPD1 is set for the TTBR0 geometry and EPD0 is NOT. */
        checks += 1u;
        if ((v & kAdrenoSmmuTcrEpd1) == 0u || (v & kAdrenoSmmuTcrEpd0) != 0u) {
            fails += 1u;
        }
        /* The coherent_walk branch changes SH0 AND both RGN fields together. */
        v = adreno_smmu_ctx_tcr(40u, 0u, 0u, &ok);
        checks += 1u;
        if (ok != 1 || v != 0x00802018u) {
            fails += 1u;
        }
        checks += 1u;
        if ((v & kAdrenoSmmuTcrSh0Mask) !=
            ((uint32_t)kAdrenoSmmuTcrShOs << 12)) {
            fails += 1u;
        }
        /* T0SZ = 64 - ias, and it is the ONLY field that moves with ias. */
        {
            uint32_t prev = 0u;
            uint32_t first = 1u;
            const uint32_t iases[6] = {32u, 36u, 40u, 42u, 44u, 48u};
            for (i = 0u; i < 6u; ++i) {
                uint32_t t = adreno_smmu_ctx_tcr(iases[i], 1u, 0u, &ok);
                if (ok != 1 || (t & kAdrenoSmmuTcrT0szMask) !=
                                ((64u - iases[i]) & 0x3fu)) {
                    fails += 1u;
                    break;
                }
                if (first == 0u && (t & ~kAdrenoSmmuTcrT0szMask) !=
                                    (prev & ~kAdrenoSmmuTcrT0szMask)) {
                    fails += 1u; /* ias must not disturb another field */
                    break;
                }
                prev = t;
                first = 0u;
            }
            checks += 1u;
        }
        /* The TTBR1 geometry: shifted, A1 clear, EPD0 instead of EPD1. */
        v = adreno_smmu_ctx_tcr(40u, 1u, 1u, &ok);
        checks += 1u;
        if (ok != 1 || v != 0xb5180080u) {
            fails += 1u;
        }
        checks += 1u;
        if ((v & kAdrenoSmmuTcrA1) != 0u || (v & kAdrenoSmmuTcrEpd0) == 0u ||
            (v & kAdrenoSmmuTcrEpd1) != 0u) {
            fails += 1u;
        }
        /* Refusals: ias outside 16..48, and a non-boolean coherent_walk. */
        checks += 1u;
        ok = 99;
        if (adreno_smmu_ctx_tcr(15u, 1u, 0u, &ok) != 0u || ok != 0) {
            fails += 1u;
        }
        checks += 1u;
        ok = 99;
        if (adreno_smmu_ctx_tcr(49u, 1u, 0u, &ok) != 0u || ok != 0) {
            fails += 1u;
        }
        checks += 1u;
        ok = 99;
        if (adreno_smmu_ctx_tcr(40u, 2u, 0u, &ok) != 0u || ok != 0) {
            fails += 1u;
        }
        /* A NULL ok must not crash: this is freestanding C. */
        checks += 1u;
        if (adreno_smmu_ctx_tcr(40u, 1u, 0u, 0) != 0x00803518u) {
            fails += 1u;
        }
    }

    /* ---- TCR2: PASIZE, SEP, and the AArch64-only AS bit (P1:414-418) ----- */
    {
        int ok = 0;
        checks += 1u;
        if (adreno_smmu_ctx_tcr2(36u, 1u, &ok) != 0x00038011u || ok != 1) {
            fails += 1u;
        }
        checks += 1u;
        /* Without the AS bit a oas=36 context is SEP(0x7 << 15) | PASIZE(1) =
         * 0x00038001 -- NOT 0x00038010, which would put the PASIZE nibble in
         * bit 4 where AS lives. */
        if (adreno_smmu_ctx_tcr2(36u, 0u, &ok) != 0x00038001u) {
            fails += 1u; /* AS is the ONLY difference */
        }
        /* And the two differ in exactly one bit. */
        checks += 1u;
        if ((adreno_smmu_ctx_tcr2(36u, 1u, &ok) ^
             adreno_smmu_ctx_tcr2(36u, 0u, &ok)) != kAdrenoSmmuTcr2As) {
            fails += 1u;
        }
        /* SEP is upstream's 0x7 in every case. */
        checks += 1u;
        if ((adreno_smmu_ctx_tcr2(40u, 1u, &ok) & kAdrenoSmmuTcr2SepMask) !=
            kAdrenoSmmuTcr2SepUpstream) {
            fails += 1u;
        }
        /* PASIZE tracks oas one-for-one. */
        {
            const uint32_t oass[6] = {32u, 36u, 40u, 42u, 44u, 48u};
            uint32_t bad = 0u;
            for (i = 0u; i < 6u; ++i) {
                uint32_t t = adreno_smmu_ctx_tcr2(oass[i], 1u, &ok);
                if (ok != 1 || (t & kAdrenoSmmuTcr2PasizeMask) != i) {
                    bad = 1u;
                    break;
                }
            }
            checks += 1u;
            if (bad != 0u) {
                fails += 1u;
            }
        }
        /* 52 is rejected: a 4-bit IDR read cannot produce it. */
        checks += 1u;
        ok = 99;
        if (adreno_smmu_ctx_tcr2(52u, 1u, &ok) != 0u || ok != 0) {
            fails += 1u;
        }
        checks += 1u;
        ok = 99;
        if (adreno_smmu_ctx_tcr2(33u, 1u, &ok) != 0u || ok != 0) {
            fails += 1u;
        }
    }

    /* ---- CBAR / CBA2R (P2:581-612) --------------------------------------- */
    checks += 1u;
    if (adreno_smmu_ctx_cbar(0u, 0x3fu) != 0x0001f33fu) {
        fails += 1u; /* TYPE=1, MEMATTR=0xf, BPSHCFG=3, VMID=0x3f */
    }
    checks += 1u;
    if (adreno_smmu_ctx_cbar(1u, 0x3fu) != 0x0001f300u) {
        fails += 1u; /* a 16-bit VMID must NOT also land in CBAR (P2:608-611) */
    }
    checks += 1u;
    if ((adreno_smmu_ctx_cbar(0u, 0x3fu) & kAdrenoSmmuCbarTypeMask) !=
        ((uint32_t)kAdrenoSmmuCbarTypeS1TransS2Bypass << 16)) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_ctx_cba2r(1u, 0u, 0u) != kAdrenoSmmuCba2rVa64) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_ctx_cba2r(1u, 1u, 0x1u) !=
        (kAdrenoSmmuCba2rVa64 | 0x00010000u)) {
        fails += 1u; /* 16-bit VMID in CBA2R bits 31..16 (P2:587-589) */
    }
    checks += 1u;
    if (adreno_smmu_ctx_cba2r(0u, 0u, 0u) != 0u) {
        fails += 1u;
    }

    /* ---- TTBRn: the ASID in bits 63..48, BOTH registers (P2:540-548) ---- */
    {
        int ok = 0;
        uint64_t t = adreno_smmu_ctx_ttbrn_checked(7u, 0x877fe000ull, &ok);
        checks += 1u;
        if (ok != 1 || t != 0x00070000877fe000ull) {
            fails += 1u;
        }
        checks += 1u;
        if ((uint32_t)((t >> kAdrenoSmmuTtbrAsidShift) & 0xffffull) != 7u) {
            fails += 1u;
        }
        /* The pointer survives bit-for-bit below the ASID field. */
        checks += 1u;
        if ((t & 0x0000ffffffffffffull) != 0x877fe000ull) {
            fails += 1u;
        }
        /* An unaligned root pointer is REFUSED, not rounded. */
        checks += 1u;
        ok = 99;
        if (adreno_smmu_ctx_ttbrn_checked(7u, 0x877fe800ull, &ok) != 0ull ||
            ok != 0) {
            fails += 1u;
        }
        /* An ASID wider than the 16-bit field is refused too. */
        checks += 1u;
        ok = 99;
        if (adreno_smmu_ctx_ttbrn_checked(0x10000u, 0x877fe000ull, &ok) != 0ull ||
            ok != 0) {
            fails += 1u;
        }
    }

    /* ---- the plan: NINE writes in upstream order, SCTLR last ------------ */
    p.cbndx = 2u;
    p.asid = 0x12u;
    p.ttbr = 0x877fe000ull;
    p.ias = 40u;
    p.oas = 36u;
    p.coherent_walk = 1u;
    p.ttbr1 = 0u;
    p.aarch64 = 1u;
    p.vmid16 = 0u;
    p.vmid = 0u;

    for (i = 0u; i < 12u; ++i) {
        plan[i].gr1 = 0u;
        plan[i].offset = 0u;
        plan[i].value = 0u;
        plan[i].value_hi = 0u;
        plan[i].is_64bit = 0u;
        plan[i].order = 0u;
    }
    checks += 1u;
    if (adreno_smmu_ctx_plan(&p, plan, 12u, &count) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != 9u) {
        fails += 1u;
    }
    checks += 1u;
    if (count != adreno_smmu_ctx_plan_writes()) {
        fails += 1u;
    }
    /* It is THREE writes longer than round 75: FOUR registers added (CBA2R,
     * CBAR, TCR2, TTBR1) minus ONE dropped (CONTEXTIDR, which upstream writes
     * only in the AARCH32_S branch at P2:625). The net delta is smaller than the
     * register delta, which is exactly why both numbers are pinned. */
    checks += 1u;
    if (count != adreno_smmu_ctx_round75_writes() + 3u) {
        fails += 1u;
    }

    /* Locate each register by identity rather than by trusting an index. The GR1
     * offsets in the plan have the context-bank index applied, so match against
     * base + (cbndx << 2) -- comparing against the bare base would leave every
     * GR1 register unlocated, which is exactly the bug this loop guards. */
    for (i = 0u; i < count; ++i) {
        if (plan[i].gr1 != 0u) {
            seen_gr1 += 1u;
            if (plan[i].offset == kAdrenoSmmuGr1Cbar + ((p.cbndx & 0xffu) << 2)) {
                idx_cbar = i;
            }
            if (plan[i].offset == kAdrenoSmmuGr1Cba2r + ((p.cbndx & 0xffu) << 2)) {
                idx_cba2r = i;
            }
        } else {
            seen_cb += 1u;
            if (plan[i].offset == 0x0000u) { idx_sctlr = i; }
            if (plan[i].offset == 0x0010u) { idx_tcr2 = i; }
            if (plan[i].offset == 0x0020u) { idx_ttbr0 = i; }
            if (plan[i].offset == 0x0028u) { idx_ttbr1 = i; }
            if (plan[i].offset == 0x0030u) { idx_tcr = i; }
            if (plan[i].offset == 0x0038u) { idx_mair0 = i; }
            if (plan[i].offset == 0x003cu) { idx_mair1 = i; }
            if (plan[i].is_64bit != 0u) { wide += 1u; }
        }
    }
    /* EXACTLY two GR1 writes and exactly seven CB writes: the split round 75
     * could not represent at all. */
    checks += 1u;
    if (seen_gr1 != 2u || seen_cb != 7u) {
        fails += 1u;
    }
    for (i = 0u; i < 9u; ++i) {
        checks += 1u;
        if ((i == 0u ? idx_cba2r : i == 1u ? idx_cbar : i == 2u ? idx_tcr2 :
             i == 3u ? idx_tcr : i == 4u ? idx_ttbr0 : i == 5u ? idx_ttbr1 :
             i == 6u ? idx_mair0 : i == 7u ? idx_mair1 : idx_sctlr) == 0xffffffffu) {
            fails += 1u;
            break;
        }
    }
    /* CBAR is written BEFORE SCTLR, and CBA2R before CBAR (P2:581-612). */
    checks += 1u;
    if (!(idx_cba2r < idx_cbar && idx_cbar < idx_sctlr)) {
        fails += 1u;
    }
    /* TCR2 BEFORE TCR -- P2:614-618 states the reason. */
    checks += 1u;
    if (!(idx_tcr2 < idx_tcr)) {
        fails += 1u;
    }
    /* BOTH TTBRs after TCR and before the MAIRs (P2:621-639). */
    checks += 1u;
    if (!(idx_tcr < idx_ttbr0 && idx_ttbr0 < idx_ttbr1 &&
          idx_ttbr1 < idx_mair0 && idx_mair0 < idx_mair1)) {
        fails += 1u;
    }
    /* SCTLR is LAST -- round 75's one rule, preserved exactly. */
    checks += 1u;
    if (idx_sctlr != count - 1u) {
        fails += 1u;
    }
    /* Exactly TWO 64-bit writes: TTBR0 and TTBR1. */
    checks += 1u;
    if (wide != 2u) {
        fails += 1u;
    }
    /* Both carry the SAME ASID. */
    checks += 1u;
    if (plan[idx_ttbr0].value_hi != plan[idx_ttbr1].value_hi ||
        plan[idx_ttbr0].value != plan[idx_ttbr1].value) {
        fails += 1u;
    }
    checks += 1u;
    /* The ASID occupies TTBR bits 63..48, which is value_hi bits 15..0. */
    if (plan[idx_ttbr0].value_hi != (p.asid << 16)) {
        fails += 1u;
    }
    /* NO CONTEXTIDR: that write lives only in the AARCH32_S branch (P2:625). */
    {
        uint32_t ctxidr = 0u;
        for (i = 0u; i < count; ++i) {
            if (plan[i].gr1 == 0u && plan[i].offset == 0x0034u) {
                ctxidr += 1u;
            }
        }
        checks += 1u;
        if (ctxidr != 0u) {
            fails += 1u;
        }
    }
    /* order is 0-based and ascending. */
    checks += 1u;
    for (i = 0u; i < count; ++i) {
        if (plan[i].order != i) {
            fails += 1u;
            break;
        }
    }

    /* ---- refusals -------------------------------------------------------- */
    checks += 1u;
    count = 99u;
    if (adreno_smmu_ctx_plan(&p, tiny, 4u, &count) != 1 || count != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_ctx_plan(0, plan, 12u, &count) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_ctx_plan(&p, 0, 12u, &count) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_ctx_plan(&p, plan, 12u, 0) != 1) {
        fails += 1u;
    }
    /* The AArch32 short-descriptor form is REFUSED, not approximated. */
    {
        struct adreno_smmu_ctx_params q = p;
        q.aarch64 = 0u;
        checks += 1u;
        if (adreno_smmu_ctx_plan(&q, plan, 12u, &count) != 1) {
            fails += 1u;
        }
    }
    /* Bad parameters propagate as refusals, never as a silent plan. */
    {
        struct adreno_smmu_ctx_params q = p;
        q.ias = 49u;
        checks += 1u;
        if (adreno_smmu_ctx_plan(&q, plan, 12u, &count) != 1) {
            fails += 1u;
        }
        q = p;
        q.oas = 52u;
        checks += 1u;
        if (adreno_smmu_ctx_plan(&q, plan, 12u, &count) != 1) {
            fails += 1u;
        }
        q = p;
        q.ttbr = 0x877fe800ull;
        checks += 1u;
        if (adreno_smmu_ctx_plan(&q, plan, 12u, &count) != 1) {
            fails += 1u;
        }
    }

    /* ---- the absent inputs, and derived readiness ------------------------ */
    checks += 1u;
    if (adreno_smmu_ctx_missing() != kAdrenoSmmuCtxInputAll) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoSmmuCtxInputAll != 7u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_smmu_ctx_ready() != 0) {
        fails += 1u;
    }
    /* Names and anchors are total, non-empty, and distinct. */
    {
        const uint32_t bits[3] = {kAdrenoSmmuCtxInputIas,
                                  kAdrenoSmmuCtxInputAsid,
                                  kAdrenoSmmuCtxInputPageTable};
        for (i = 0u; i < 3u; ++i) {
            const char *nm = adreno_smmu_ctx_input_name(bits[i]);
            const char *an = adreno_smmu_ctx_input_anchor(bits[i]);
            uint32_t j;
            checks += 1u;
            if (nm == 0 || nm[0] == '\0' || an == 0 || an[0] == '\0') {
                fails += 1u;
                continue;
            }
            checks += 1u;
            if (nm[0] == 'u' && nm[1] == 'n') {
                fails += 1u;
                continue;
            }
            for (j = 0u; j < i; ++j) {
                const char *om = adreno_smmu_ctx_input_name(bits[j]);
                uint32_t k = 0u;
                uint32_t same = 1u;
                while (nm[k] != '\0' || om[k] != '\0') {
                    if (nm[k] != om[k]) {
                        same = 0u;
                        break;
                    }
                    k += 1u;
                }
                checks += 1u;
                if (same != 0u) {
                    fails += 1u;
                }
            }
        }
        checks += 1u;
        if (adreno_smmu_ctx_input_name(0u)[0] != 'u' ||
            adreno_smmu_ctx_input_anchor(0u)[0] != 'u') {
            fails += 1u;
        }
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return (fails != 0u) ? 1 : 0;
}
