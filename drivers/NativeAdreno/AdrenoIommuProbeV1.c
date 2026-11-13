// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoIommuProbeV1 -- see AdrenoIommuProbeV1.h for provenance and the honest
// HOST-VERIFIED boundary. Freestanding C11: no libc, no MMIO, no volatile.
//
// This file contains NO register access of any kind. It decodes a word it is
// GIVEN and refuses to bless a read it cannot gate. The composition at the
// bottom calls round 83's real word builders rather than re-deriving them.

#include "AdrenoIommuProbeV1.h"
#include "AdrenoSmmuCtxWordV1.h"

/* ---- P2:1727-1744, transcribed with the default arm INTACT ------------ */
uint32_t adreno_iommu_id_size_to_bits(uint32_t size)
{
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
        /* 5 AND every other value collapse to 48. Writing this as a table of
         * six entries and letting everything else fall off the end would return
         * garbage for ID2 fields the silicon is entitled to report as reserved,
         * and 48 is what upstream deliberately chose instead. */
        return 48u;
    }
}

/* FIELD_GET() for the ID2 fields: shift down, mask to the field width. */
static uint32_t field_get(uint32_t raw, uint32_t mask, uint32_t shift)
{
    return (raw & mask) >> shift;
}

int adreno_iommu_id2_decode(uint32_t raw, struct adreno_iommu_id2 *out)
{
    if (out == 0) {
        return 1;
    }
    out->raw = raw;
    /* P1:70-72 the three size fields, at their real bit positions. */
    out->ubs = field_get(raw, kAdrenoIommuId2UbsMask, kAdrenoIommuId2UbsShift);
    out->oas = field_get(raw, kAdrenoIommuId2OasMask, kAdrenoIommuId2OasShift);
    out->ias = field_get(raw, kAdrenoIommuId2IasMask, kAdrenoIommuId2IasShift);
    /* P2:1880-1885, :1904-1905 the three WIDTHS. Note that each encoding is
     * 0..15 and each width is 32..48, so the two can never be confused by
     * accident once both are present. */
    out->ias_bits = adreno_iommu_id_size_to_bits(out->ias);
    out->oas_bits = adreno_iommu_id_size_to_bits(out->oas);
    out->ubs_bits = adreno_iommu_id_size_to_bits(out->ubs);
    /* P1:66-69 the four single-bit flags. */
    out->ptfs_4k = ((raw & kAdrenoIommuId2Ptfs4K) != 0u) ? 1u : 0u;
    out->ptfs_16k = ((raw & kAdrenoIommuId2Ptfs16K) != 0u) ? 1u : 0u;
    out->ptfs_64k = ((raw & kAdrenoIommuId2Ptfs64K) != 0u) ? 1u : 0u;
    out->vmid16 = ((raw & kAdrenoIommuId2Vmid16) != 0u) ? 1u : 0u;
    return 0;
}

uint32_t adreno_iommu_ipa_size(uint32_t raw)
{
    /* P2:1880-1881: ipa_size = arm_smmu_id_size_to_bits(FIELD_GET(ID2_IAS)). */
    return adreno_iommu_id_size_to_bits(
        field_get(raw, kAdrenoIommuId2IasMask, kAdrenoIommuId2IasShift));
}

uint32_t adreno_iommu_pa_size(uint32_t raw)
{
    /* P2:1884-1885: pa_size = arm_smmu_id_size_to_bits(FIELD_GET(ID2_OAS)). */
    return adreno_iommu_id_size_to_bits(
        field_get(raw, kAdrenoIommuId2OasMask, kAdrenoIommuId2OasShift));
}

uint32_t adreno_iommu_va_size(uint32_t raw, uint32_t version, int *ok)
{
    if (ok != 0) {
        *ok = 0;
    }
    /* P2:1899-1902: for anything BELOW ARM_SMMU_V2 the VA space is the IPA
     * space -- there is no separate UBS field to read. */
    if (version == (uint32_t)kAdrenoIommuVersionV1 ||
        version == (uint32_t)kAdrenoIommuVersionV1_64K) {
        if (ok != 0) {
            *ok = 1;
        }
        return adreno_iommu_ipa_size(raw);
    }
    /* P2:1903-1905: for V2 and later, va_size comes from UBS, NOT from IAS.
     * This is the branch round 77 identified as the load-bearing one. */
    if (version == (uint32_t)kAdrenoIommuVersionV2) {
        if (ok != 0) {
            *ok = 1;
        }
        return adreno_iommu_id_size_to_bits(
            field_get(raw, kAdrenoIommuId2UbsMask, kAdrenoIommuId2UbsShift));
    }
    /* An unknown version is REFUSED rather than defaulted. Guessing "probably
     * v2" would invent exactly the number this round exists to obtain. */
    return 0u;
}

uint32_t adreno_iommu_stage1_oas(uint32_t raw)
{
    /* P2:735-736: `ias = smmu->va_size; oas = smmu->ipa_size;` for a STAGE-1
     * domain. pa_size is used only by stage 2 (P2:759). Reading ID2.OAS here
     * would be the wrong field: OAS describes what the table WALKER can output,
     * while a stage-1 context's output is an IPA, bounded by IAS. */
    return adreno_iommu_ipa_size(raw);
}

uint32_t adreno_iommu_version_of_compatible(const char *compatible)
{
    /* P2:1965-1975 arm_smmu_of_match[], transcribed. Only the entries that
     * select a DIFFERENT arm_smmu_arch_version matter here; the model
     * (GENERIC_SMMU / ARM_MMU500 / ...) does not change the va_size rule. */
    static const struct {
        const char *name;
        uint32_t version;
    } table[] = {
        { "arm,smmu-v1", (uint32_t)kAdrenoIommuVersionV1 },      /* P2:1966 */
        { "arm,smmu-v2", (uint32_t)kAdrenoIommuVersionV2 },      /* P2:1967 */
        { "arm,mmu-400", (uint32_t)kAdrenoIommuVersionV1 },      /* P2:1968 */
        { "arm,mmu-401", (uint32_t)kAdrenoIommuVersionV1_64K },  /* P2:1969 */
        { "arm,mmu-500", (uint32_t)kAdrenoIommuVersionV2 },      /* P2:1970 */
        { "cavium,smmu-v2", (uint32_t)kAdrenoIommuVersionV2 },   /* P2:1971 */
        { "nvidia,smmu-500", (uint32_t)kAdrenoIommuVersionV2 },  /* P2:1972 */
        { "qcom,smmu-v2", (uint32_t)kAdrenoIommuVersionV2 }      /* P2:1973 */
    };
    uint32_t i;

    if (compatible == 0) {
        return kAdrenoIommuVersionUnknown;
    }
    for (i = 0u; i < (uint32_t)(sizeof(table) / sizeof(table[0])); ++i) {
        const char *a = compatible;
        const char *b = table[i].name;
        uint32_t k = 0u;
        uint32_t same = 1u;
        /* No strcmp: this module is freestanding. A hand-rolled compare is
         * correct here because both sides are NUL-terminated literals. */
        while (a[k] != '\0' || b[k] != '\0') {
            if (a[k] != b[k]) {
                same = 0u;
                break;
            }
            k += 1u;
        }
        if (same != 0u) {
            return table[i].version;
        }
    }
    return kAdrenoIommuVersionUnknown;
}

uint32_t adreno_iommu_version_select(const char *const *compatibles,
                                     uint32_t count)
{
    uint32_t i;

    if (compatibles == 0) {
        return kAdrenoIommuVersionUnknown;
    }
    /* of_match_device() scans the node's compatible list IN ORDER and takes the
     * first hit, so this does too. The order matters on this node: P4:4904
     * lists "qcom,sdm845-smmu-v2" (claimed by arm-smmu-qcom.c, NOT by
     * arm-smmu.c's table), then "qcom,adreno-smmu" (also not in it), then
     * "qcom,smmu-v2" (P2:1973). Only the THIRD entry resolves, which is why a
     * first-entry lookup would return "unknown" and a last-entry-only lookup
     * would be right by luck rather than by rule. */
    for (i = 0u; i < count; ++i) {
        uint32_t v = adreno_iommu_version_of_compatible(compatibles[i]);
        if (v != kAdrenoIommuVersionUnknown) {
            return v;
        }
    }
    return kAdrenoIommuVersionUnknown;
}

uint32_t adreno_iommu_aarch64_context(uint32_t raw)
{
    /* P2:720-724: the AArch64 context is selected when the kernel is 64-bit
     * (CONFIG_64BIT -- a board fact for this arm64 port) AND the SMMU reports
     * at least one of the three AArch64 granule features. Those three features
     * come from the three PTFS bits (P2:1906-1911), so an SMMU with none of
     * them set cannot host an AArch64 context and the AArch32 path would be
     * taken instead -- a different page-table format entirely. */
    if ((raw & (kAdrenoIommuId2Ptfs4K | kAdrenoIommuId2Ptfs16K |
                kAdrenoIommuId2Ptfs64K)) != 0u) {
        return 1u;
    }
    return 0u;
}

uint32_t adreno_iommu_ptfs_effective(uint32_t raw, uint32_t qcom_adreno_impl)
{
    /* P1:67-69 as a mask so a caller can test membership without three
     * comparisons: 4K = bit 0, 16K = bit 1, 64K = bit 2. */
    uint32_t mask = 0u;
    if ((raw & kAdrenoIommuId2Ptfs4K) != 0u) {
        mask |= 1u;
    }
    if ((raw & kAdrenoIommuId2Ptfs16K) != 0u) {
        mask |= 2u;
    }
    if ((raw & kAdrenoIommuId2Ptfs64K) != 0u) {
        mask |= 4u;
    }
    if (qcom_adreno_impl != 0u) {
        /* P3:358: qcom_adreno_smmuv2_cfg_probe() does
         *   smmu->features &= ~ARM_SMMU_FEAT_FMT_AARCH64_16K;
         * with the comment "Support for 16K pages is advertised on some SoCs,
         * but it doesn't seem to work". So on the Adreno SMMU a SET PTFS_16K
         * bit is advertised capability that the implementation then REMOVES.
         * Reporting it as usable would program a granule upstream refuses. */
        mask &= ~2u;
    }
    return mask;
}

uint32_t adreno_iommu_coherent_walk(uint32_t id0_raw, uint32_t fw_says_coherent)
{
    uint32_t reg = ((id0_raw & kAdrenoIommuId0Cttw) != 0u) ? 1u : 0u;
    /* P2:1799-1805: `cttw_reg = !!(id & ARM_SMMU_ID0_CTTW);` and then
     * `if (cttw_fw || cttw_reg)` -- an OR, with the firmware able to claim
     * coherent walk the register denies. P2:1803-1805 logs when the register is
     * overridden, so upstream treats the register as the weaker source. */
    if (fw_says_coherent != 0u) {
        return 1u;
    }
    return reg;
}

uint32_t adreno_iommu_ttbr1_for_gpu(int *ok)
{
    if (ok != 0) {
        *ok = 1;
    }
    /* P3:200-208 qcom_adreno_can_do_ttbr1(): returns false ONLY for
     * "qcom,msm8996-smmu-v2", so every other Adreno SMMU -- this one included --
     * returns true. P3:226-229 then sets IO_PGTABLE_QUIRK_ARM_TTBR1 when the
     * domain is stage 1 and the context format is AArch64, which is the case
     * P4:4831-4844's GPU node drives. */
    return 1u;
}

uint64_t adreno_iommu_ttbr_idle_word(uint32_t asid)
{
    /* P2:539-548: for an AArch64 stage-1 bank, upstream seeds BOTH TTBRs with
     * FIELD_PREP(ARM_SMMU_TTBRn_ASID, cfg->asid) and then adds the table pointer
     * to ONLY ONE of them -- the one the quirk did NOT select. So the selected
     * bank is asid|table and the other is asid|0.
     *
     * Round 83 emits asid|table for both. That is not a contradiction: its TCR
     * sets exactly one of EPD0/EPD1 (asserted by its own test), so the bank it
     * left table-less is disabled and the word is never walked. Round 86 keeps
     * round 83's plan byte-identical and exposes upstream's word here so the
     * difference is measurable instead of argued. */
    return ((uint64_t)(asid & 0xffffu)) << kAdrenoSmmuTtbrAsidShift;
}

int adreno_iommu_window_is_kgsl(uint64_t base, uint64_t size)
{
    /* Exact equality, not containment: a window that merely OVERLAPS the kgsl
     * SMMU is not the kgsl SMMU, and treating it as one would let a caller
     * reach a different unit's registers under this gate's blessing. */
    return (base == kAdrenoIommuKgslSmmuBase &&
            size == kAdrenoIommuKgslSmmuSize) ? 1 : 0;
}

int adreno_iommu_addr_is_pre_cx_forbidden(uint64_t addr)
{
    /* Rounds 22/28, as arithmetic: the SAME six windows AdrenoDomainState.c:18-25
     * pins from the board DTB. Every one lives on the collapsible gpu_cx/gpu_gx
     * rails, so reading any of them while CX is collapsed can bus-abort. */
    static const struct {
        uint64_t base;
        uint64_t size;
    } windows[6] = {
        { kAdrenoIommuGpuWindowBase, kAdrenoIommuGpuWindowSize },
        { kAdrenoIommuGpuDbgcBase,   kAdrenoIommuGpuDbgcSize   },
        { kAdrenoIommuCxMemBase,     kAdrenoIommuCxMemSize     },
        { kAdrenoIommuGmuWindowBase, kAdrenoIommuGmuWindowSize },
        { kAdrenoIommuGmuVbifBase,   kAdrenoIommuGmuVbifSize   },
        { kAdrenoIommuKgslSmmuBase,  kAdrenoIommuKgslSmmuSize  }
    };
    uint32_t i;

    /* GPUCC carve-out (round 28). The GMU node at 0x0506a000 + 0x30000 reaches
     * 0x0509a000 and NUMERICALLY swallows GPUCC at 0x05090000 + 0x9000, but the
     * two are physically distinct blocks: GPUCC is the controller FOR the
     * domain, sits on always-on PMIC rails, and its status MUST be readable
     * while the domain is collapsed. So anything inside GPUCC is not forbidden
     * even though a numeric overlap says otherwise. */
    if (addr >= kAdrenoIommuGpuccBase &&
        addr < kAdrenoIommuGpuccBase + kAdrenoIommuGpuccSize) {
        return 0;
    }
    for (i = 0u; i < 6u; ++i) {
        if (addr >= windows[i].base &&
            addr < windows[i].base + windows[i].size) {
            return 1;
        }
    }
    return 0;
}

uint32_t adreno_iommu_forbidden_window_count(void)
{
    return 6u;
}

/* The CX status decode, re-derived here from AdrenoDomainState.h's pinned
 * constants rather than imported, so this module stands alone. Round 35
 * measured cx = 0x00005ff4, whose BIT(31) is CLEAR, i.e. DOWN. */
static int cx_is_on(uint32_t status_word)
{
    return ((status_word & 0x80000000u) != 0u) ? 1 : 0;
}

enum adreno_iommu_probe_verdict adreno_iommu_assess(
    const struct adreno_iommu_gate_inputs *in)
{
    if (in == 0) {
        /* No facts means no permission. This is the whole fail-closed rule. */
        return kAdrenoIommuProbeRefusedInvalid;
    }
    /* 1. The window must be a non-empty power-of-two size, and EXACTLY the
     *    kgsl SMMU. Pure arithmetic: this check touches nothing, which is why
     *    it can run before CX is up without any hazard. */
    if (in->window_size == 0ull ||
        (in->window_size & (in->window_size - 1ull)) != 0ull) {
        return kAdrenoIommuProbeRefusedInvalid;
    }
    if (adreno_iommu_window_is_kgsl(in->window_base, in->window_size) == 0) {
        return kAdrenoIommuProbeRefusedWrongWindow;
    }
    /* 2. CX. This is the PHYSICAL hazard and it sorts before every merely
     *    incorrect condition: touching this window while the rail is collapsed
     *    can bus-abort the phone, whereas the checks below only produce a
     *    wrong answer. Round 35 measured cx=0x00005ff4 (BIT(31) clear). */
    if (cx_is_on(in->cx_status_word) == 0) {
        return kAdrenoIommuProbeRefusedCxDown;
    }
    /* 3. The SMMU's own clocks. P2:2196-2198 enables them BEFORE P2:2200 calls
     *    arm_smmu_device_cfg_probe(), which is where the ID2 read lives. */
    if (in->clocks_enabled == 0u) {
        return kAdrenoIommuProbeRefusedClocksOff;
    }
    /* 4. The mapping. P2:2152 is the kernel's ioremap; round 85 measured that
     *    the userspace equivalent does not exist on this kernel at all. */
    if (in->kmem_mapped == 0u) {
        return kAdrenoIommuProbeRefusedUnmapped;
    }
    /* 5. The context bank must not already be owned. P3:360-363 hides TZ-owned
     *    banks and P3:468 shows the Adreno impl handing bank 0 to the GPU. */
    if (in->cb_already_owned != 0u) {
        return kAdrenoIommuProbeRefusedCbOwned;
    }
    /* 6. The stream must actually arrive. Round 53 measured the GPU streams in
     *    the firmware BYPASS handoff (`Busy=10`), i.e. they do not fault yet. */
    if (in->stream_faults == 0u) {
        return kAdrenoIommuProbeRefusedNoStream;
    }
    return kAdrenoIommuProbePermitted;
}

int adreno_iommu_read_permitted(const struct adreno_iommu_gate_inputs *in)
{
    return (adreno_iommu_assess(in) == kAdrenoIommuProbePermitted) ? 1 : 0;
}

const char *adreno_iommu_verdict_name(enum adreno_iommu_probe_verdict v)
{
    switch (v) {
    case kAdrenoIommuProbePermitted:
        return "permitted";
    case kAdrenoIommuProbeRefusedInvalid:
        return "refused-invalid";
    case kAdrenoIommuProbeRefusedWrongWindow:
        return "refused-wrong-window";
    case kAdrenoIommuProbeRefusedCxDown:
        return "refused-cx-down";
    case kAdrenoIommuProbeRefusedClocksOff:
        return "refused-clocks-off";
    case kAdrenoIommuProbeRefusedUnmapped:
        return "refused-unmapped";
    case kAdrenoIommuProbeRefusedCbOwned:
        return "refused-cb-owned";
    case kAdrenoIommuProbeRefusedNoStream:
        return "refused-no-stream";
    default:
        return "unknown";
    }
}

const char *adreno_iommu_precondition_name(uint32_t bit)
{
    switch (bit) {
    case kAdrenoIommuPreCxPowered:
        return "cx-powered";
    case kAdrenoIommuPreClocksOn:
        return "clocks-on";
    case kAdrenoIommuPreKmemMapped:
        return "kmem-mapped";
    case kAdrenoIommuPreWindowIsKgsl:
        return "window-is-kgsl";
    case kAdrenoIommuPreCbFree:
        return "context-bank-free";
    case kAdrenoIommuPreStreamFaults:
        return "stream-faults";
    default:
        return "unknown";
    }
}

const char *adreno_iommu_precondition_anchor(uint32_t bit)
{
    switch (bit) {
    case kAdrenoIommuPreCxPowered:
        return "P4:4922 power-domains = <&gpucc GPU_CX_GDSC>; round 35 MEASURED "
               "cx=0x00005ff4 with BIT(31) clear, i.e. CX DOWN";
    case kAdrenoIommuPreClocksOn:
        return "P2:2196-2198 clk_bulk_prepare_enable() runs BEFORE P2:2200 "
               "arm_smmu_device_cfg_probe(), where ID2 is read";
    case kAdrenoIommuPreKmemMapped:
        return "P2:2152 devm_platform_get_and_ioremap_resource(); round 85 "
               "MEASURED CONFIG_DEVMEM is not set, so no userspace route exists";
    case kAdrenoIommuPreWindowIsKgsl:
        return "P4:4905 reg = <0 0x05040000 0 0x10000> is the kgsl SMMU; "
               "P4:5094's 0x15000000 is apps_smmu, a different unit";
    case kAdrenoIommuPreCbFree:
        return "P3:360-363 TZ protects the last context banks and P3:468 gives "
               "bank 0 to the GPU, so a bank may already be owned";
    case kAdrenoIommuPreStreamFaults:
        return "round 53 MEASURED the GPU streams still in the firmware BYPASS "
               "handoff (Busy=10), i.e. they do not fault yet";
    default:
        return "unknown";
    }
}

uint32_t adreno_iommu_required_preconditions(void)
{
    /* Derived from the enum, so a bit added to the enum cannot leave this
     * stale. Assembled by OR rather than written as 0x3f. */
    return (uint32_t)kAdrenoIommuPreCxPowered |
           (uint32_t)kAdrenoIommuPreClocksOn |
           (uint32_t)kAdrenoIommuPreKmemMapped |
           (uint32_t)kAdrenoIommuPreWindowIsKgsl |
           (uint32_t)kAdrenoIommuPreCbFree |
           (uint32_t)kAdrenoIommuPreStreamFaults;
}

/* The step table. Declared `static const` in one place so the count and the
 * contents cannot drift apart. */
static const struct adreno_iommu_step kSteps[] = {
    { 0u, kAdrenoIommuStepGate, "confirm-window-is-kgsl-smmu",
      "P4:4905 vs P4:5094: 0x05040000 is the kgsl SMMU, 0x15000000 is apps_smmu" },
    { 1u, kAdrenoIommuStepGate, "confirm-cx-powered",
      "P4:4922 + round 35 (cx=0x00005ff4, BIT(31) clear): CX must be UP first" },
    { 2u, kAdrenoIommuStepGate, "confirm-smmu-clocks-enabled",
      "P2:2196-2198: clocks are enabled BEFORE cfg_probe reads a register" },
    { 3u, kAdrenoIommuStepGate, "confirm-kmem-mapping-permitted",
      "P2:2152 ioremap; round 85: no userspace mapping route exists at all" },
    { 4u, kAdrenoIommuStepGate, "confirm-context-bank-free",
      "P3:360-363 + P3:468: a TZ- or GPU-owned bank must not be reprogrammed" },
    { 5u, kAdrenoIommuStepGate, "confirm-stream-faults",
      "round 53: the GPU streams are still in the firmware BYPASS handoff" },
    { 6u, kAdrenoIommuStepRead, "read-id0-cttw",
      "P1:45/P1:54 + P2:1758: ID0 is read FIRST, and CTTW decides coherency" },
    { 7u, kAdrenoIommuStepRead, "read-id2",
      "P1:65/P2:1879: the single load-bearing read; everything below is offline" },
    { 8u, kAdrenoIommuStepDerive, "decode-id2-fields",
      "P1:66-72: UBS 11..8, OAS 7..4, IAS 3..0, PTFS 12/13/14, VMID16 15" },
    { 9u, kAdrenoIommuStepDerive, "derive-ipa-and-pa-size",
      "P2:1880-1885 + P2:1727-1744: ipa from IAS, pa from OAS, default 48" },
    { 10u, kAdrenoIommuStepDerive, "derive-va-size-version-gated",
      "P2:1899-1912: v1 takes IPA, v2+ takes UBS; the version comes from the "
      "DT compatible table at P2:1965-1975" },
    { 11u, kAdrenoIommuStepDerive, "derive-stage1-oas-from-ipa",
      "P2:735-736: a STAGE-1 domain's oas is ipa_size, NOT pa_size" },
    { 12u, kAdrenoIommuStepDerive, "resolve-aarch64-context-and-granule",
      "P2:720-724 + P3:358: AArch64 needs a granule the impl did not clear" },
    { 13u, kAdrenoIommuStepHandoff, "compose-round83-stage1-words",
      "P1:393-418: adreno_smmu_ctx_tcr()/adreno_smmu_ctx_tcr2() consume ias/oas" }
};

uint32_t adreno_iommu_step_count(void)
{
    return (uint32_t)(sizeof(kSteps) / sizeof(kSteps[0]));
}

int adreno_iommu_steps(struct adreno_iommu_step *out, uint32_t capacity,
                       uint32_t *count)
{
    uint32_t need = adreno_iommu_step_count();
    uint32_t i;

    if (out == 0 || count == 0) {
        return 1;
    }
    *count = 0u;
    if (capacity < need) {
        /* Refused, not truncated. A caller that got 11 of 14 steps and ran them
         * would skip the handoff -- and `count` says 0 so it cannot mistake a
         * short buffer for a complete plan. */
        return 1;
    }
    for (i = 0u; i < need; ++i) {
        out[i] = kSteps[i];
    }
    *count = need;
    return 0;
}

int adreno_iommu_stage1_words(uint32_t id2_raw, uint32_t version,
                              uint32_t coherent_walk,
                              struct adreno_iommu_stage1_words *out)
{
    struct adreno_iommu_id2 d;
    int ok_va = 0;
    int ok_tcr = 0;
    int ok_tcr2 = 0;
    int ok_ttbr1 = 0;
    uint32_t aarch64;
    uint32_t ias;
    uint32_t oas;

    if (out == 0) {
        return 1;
    }
    out->ias = 0u;
    out->oas = 0u;
    out->coherent_walk = 0u;
    out->ttbr1 = 0u;
    out->tcr = 0u;
    out->tcr2 = 0u;
    out->ok = 0u;

    if (adreno_iommu_id2_decode(id2_raw, &d) != 0) {
        return 1;
    }
    /* The version gate is not optional: a v1 SMMU's va_size is its IPA size, so
     * skipping it would read UBS on hardware that has no such field. */
    ias = adreno_iommu_va_size(id2_raw, version, &ok_va);
    if (ok_va == 0) {
        return 1;
    }
    /* P2:736: oas = ipa_size. Deliberately NOT pa_size. */
    oas = adreno_iommu_stage1_oas(id2_raw);

    /* The granule has to be one round 83's TCR can actually encode. Its 4 KiB
     * geometry is the one this port models (round 15's ArmSmmu4K kit) and the
     * one P2:722-724 puts first in the AArch64 feature test, so a word with no
     * 4 KiB granule is refused rather than silently given TG0_4K. */
    if ((adreno_iommu_ptfs_effective(id2_raw, 1u) & 1u) == 0u) {
        return 1;
    }
    aarch64 = adreno_iommu_aarch64_context(id2_raw);
    if (aarch64 == 0u) {
        /* AArch32 descriptors are a different pinned-unpinned format
         * (arm_v7s_cfg, P2:520-521). Round 83 refuses them too; so does this. */
        return 1;
    }

    /* ---- THE COMPOSITION: round 83's REAL functions, called, not copied --- */
    out->ias = ias;
    out->oas = oas;
    out->coherent_walk = (coherent_walk != 0u) ? 1u : 0u;
    out->ttbr1 = adreno_iommu_ttbr1_for_gpu(&ok_ttbr1);
    /* P1:393-412. ias feeds T0SZ = 64 - ias; nothing else in the word depends
     * on it, which round 83's own test pins. */
    out->tcr = adreno_smmu_ctx_tcr(ias, out->coherent_walk, out->ttbr1, &ok_tcr);
    /* P1:414-418. oas feeds TCR2.PASIZE (and thus the IPS nibble), which is why
     * taking pa_size here instead of ipa_size would encode the wrong nibble. */
    out->tcr2 = adreno_smmu_ctx_tcr2(oas, aarch64, &ok_tcr2);
    if (ok_tcr == 0 || ok_tcr2 == 0 || ok_ttbr1 == 0) {
        /* A refusal from round 83 is propagated, never papered over: the words
         * stay 0 and ok stays 0 so a caller cannot write a meaningless value. */
        return 1;
    }
    out->ok = 1u;
    return 0;
}

/* The seven absent inputs, in ONE place, each with the reason it is absent
 * TODAY. Nothing here says "not implemented": every bit is absent because a
 * measurement or a pinned line says so. */
static uint32_t missing_inputs(void)
{
    uint32_t mask = 0u;
    /* ID2. Round 85 MEASURED that CONFIG_DEVMEM is not set, so /dev/mem cannot
     * exist, and the iommu debugfs has no address-size node. */
    mask |= (uint32_t)kAdrenoIommuMissingId2Word;
    /* ID0.CTTW -- a second register read, behind the same gate. */
    mask |= (uint32_t)kAdrenoIommuMissingId0Word;
    /* CX. Round 35 measured it DOWN; nothing has powered it since. */
    mask |= (uint32_t)kAdrenoIommuMissingCxUp;
    /* ASID. Round 75 I2 is unchanged: this port has no allocator. */
    mask |= (uint32_t)kAdrenoIommuMissingAsid;
    /* Page table. Round 75 I1 is unchanged: nothing populates an LPAE table. */
    mask |= (uint32_t)kAdrenoIommuMissingPageTable;
    /* Stream binding. Round 53 measured the streams still in BYPASS. */
    mask |= (uint32_t)kAdrenoIommuMissingStream;
    /* A kernel caller. CONFIG_XNU_NATIVE_ADRENO is n; no code in this tree can
     * execute a single step of the sequence above. */
    mask |= (uint32_t)kAdrenoIommuMissingKernelCaller;
    return mask;
}

uint32_t adreno_iommu_missing(void)
{
    return missing_inputs();
}

const char *adreno_iommu_missing_name(uint32_t bit)
{
    switch (bit) {
    case kAdrenoIommuMissingId2Word:
        return "id2-word";
    case kAdrenoIommuMissingId0Word:
        return "id0-word";
    case kAdrenoIommuMissingCxUp:
        return "cx-powered";
    case kAdrenoIommuMissingAsid:
        return "asid";
    case kAdrenoIommuMissingPageTable:
        return "page-table";
    case kAdrenoIommuMissingStream:
        return "stream-binding";
    case kAdrenoIommuMissingKernelCaller:
        return "kernel-caller";
    default:
        return "unknown";
    }
}

const char *adreno_iommu_missing_anchor(uint32_t bit)
{
    switch (bit) {
    case kAdrenoIommuMissingId2Word:
        return "P2:1879 reads ID2 at probe; round 85 MEASURED it unreadable from "
               "userspace (CONFIG_DEVMEM is not set -> ENXIO, no debugfs node)";
    case kAdrenoIommuMissingId0Word:
        return "P1:54 ID0.CTTW decides coherent_walk (P2:1799-1805), so a SECOND "
               "register read is needed on the same powered, mapped SMMU";
    case kAdrenoIommuMissingCxUp:
        return "P4:4922 puts this SMMU on GPU_CX_GDSC; round 35 MEASURED "
               "cx=0x00005ff4 with BIT(31) clear, i.e. DOWN";
    case kAdrenoIommuMissingAsid:
        return "P2:795 cfg->asid = cfg->cbndx; this port has no context-bank "
               "allocator, so no ASID is owned (round 75 I2, unchanged)";
    case kAdrenoIommuMissingPageTable:
        return "P2:815 alloc_io_pgtable_ops() allocates the table and P2:548 puts "
               "its phys address in TTBR0; this port allocates none (round 75 I1, "
               "unchanged)";
    case kAdrenoIommuMissingStream:
        return "round 53 MEASURED the GPU streams in the firmware BYPASS "
               "handoff (Busy=10), so no SID faults into a context yet";
    case kAdrenoIommuMissingKernelCaller:
        return "CONFIG_XNU_NATIVE_ADRENO is n and no kernel code calls any "
               "adreno_iommu_* function; round 85 showed only kernel code can";
    default:
        return "unknown";
    }
}

int adreno_iommu_ready(void)
{
    /* Derived, never hard-coded. If someone closes every gap this flips by
     * itself, and until then it cannot be made to read 1 by editing a literal. */
    return (adreno_iommu_missing() == 0u) ? 1 : 0;
}

int adreno_iommu_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    uint32_t i = 0u;
    uint32_t count = 0u;
    struct adreno_iommu_id2 d;
    struct adreno_iommu_stage1_words w;
    struct adreno_iommu_step steps[20];
    struct adreno_iommu_step tiny[4];
    struct adreno_iommu_gate_inputs g;
    int ok = 0;
    uint32_t first_read = 0xffffffffu;
    uint32_t cx_step = 0xffffffffu;
    uint32_t handoff = 0xffffffffu;
    uint32_t seen_gate = 0u;
    uint32_t seen_read = 0u;
    uint32_t seen_derive = 0u;
    uint32_t seen_handoff = 0u;

    /* ---- the register offsets and the board windows ---------------------- */
    checks += 1u;
    if (kAdrenoIommuGr0Id0 != 0x0020u || kAdrenoIommuGr0Id1 != 0x0024u ||
        kAdrenoIommuGr0Id2 != 0x0028u) {
        fails += 1u;
    }
    checks += 1u;
    /* The three ID registers are 4 bytes apart in the GR0 frame: a wrong stride
     * here would read ID1 when ID2 was meant and silently decode nonsense. */
    if (kAdrenoIommuGr0Id1 - kAdrenoIommuGr0Id0 != 4u ||
        kAdrenoIommuGr0Id2 - kAdrenoIommuGr0Id1 != 4u) {
        fails += 1u;
    }
    checks += 1u;
    /* The two SMMUs must be different units, or every "wrong window" refusal
     * below is vacuous. */
    if (kAdrenoIommuKgslSmmuBase == kAdrenoIommuAppsSmmuBase ||
        kAdrenoIommuKgslSmmuBase != 0x05040000ull ||
        kAdrenoIommuAppsSmmuBase != 0x15000000ull) {
        fails += 1u;
    }

    /* ---- ID2 field masks: adjacent, gapless, and exactly 4 bits each ------ */
    checks += 1u;
    if (kAdrenoIommuId2UbsMask != 0x00000f00u ||
        kAdrenoIommuId2OasMask != 0x000000f0u ||
        kAdrenoIommuId2IasMask != 0x0000000fu) {
        fails += 1u;
    }
    checks += 1u;
    /* The three fields must PARTITION bits 11..0 with no overlap and no hole.
     * P1:70-72 defines them as three adjacent GENMASKs, so a transcription that
     * moved one by a bit would still look plausible per-field. */
    if ((kAdrenoIommuId2UbsMask | kAdrenoIommuId2OasMask |
         kAdrenoIommuId2IasMask) != 0x00000fffu) {
        fails += 1u;
    }
    checks += 1u;
    if ((kAdrenoIommuId2UbsMask & kAdrenoIommuId2OasMask) != 0u ||
        (kAdrenoIommuId2OasMask & kAdrenoIommuId2IasMask) != 0u ||
        (kAdrenoIommuId2UbsMask & kAdrenoIommuId2IasMask) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    /* The four flags sit ABOVE the three fields: 12, 13, 14, 15. */
    if (kAdrenoIommuId2Ptfs4K != 0x00001000u ||
        kAdrenoIommuId2Ptfs16K != 0x00002000u ||
        kAdrenoIommuId2Ptfs64K != 0x00004000u ||
        kAdrenoIommuId2Vmid16 != 0x00008000u) {
        fails += 1u;
    }
    checks += 1u;
    if ((kAdrenoIommuId2Ptfs4K | kAdrenoIommuId2Ptfs16K |
         kAdrenoIommuId2Ptfs64K | kAdrenoIommuId2Vmid16 |
         kAdrenoIommuId2UbsMask | kAdrenoIommuId2OasMask |
         kAdrenoIommuId2IasMask) != 0x0000ffffu) {
        fails += 1u; /* bits 15..0 fully accounted for, 16..31 reserved */
    }
    checks += 1u;
    if (kAdrenoIommuId0Cttw != 0x00004000u) {
        fails += 1u; /* P1:54 BIT(14) */
    }

    /* ---- id_size_to_bits INCLUDING the collapsing default (P2:1727-1744) - */
    checks += 1u;
    if (adreno_iommu_id_size_to_bits(0u) != 32u ||
        adreno_iommu_id_size_to_bits(1u) != 36u ||
        adreno_iommu_id_size_to_bits(2u) != 40u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_id_size_to_bits(3u) != 42u ||
        adreno_iommu_id_size_to_bits(4u) != 44u ||
        adreno_iommu_id_size_to_bits(5u) != 48u) {
        fails += 1u;
    }
    checks += 1u;
    /* 6..15 are NOT widths upstream invents; they collapse to 48 via the default
     * arm. This is the single most likely transcription error. */
    if (adreno_iommu_id_size_to_bits(6u) != 48u ||
        adreno_iommu_id_size_to_bits(15u) != 48u ||
        adreno_iommu_id_size_to_bits(0xffffffffu) != 48u) {
        fails += 1u;
    }

    /* ---- decode: encodings, widths and flags kept apart ------------------ */
    checks += 1u;
    if (adreno_iommu_id2_decode(0u, 0) != 1) {
        fails += 1u; /* a NULL out is refused, not written */
    }
    {
        /* A synthetic word with a DIFFERENT value in every field, so a decode
         * that swapped two fields cannot pass by symmetry. */
        uint32_t raw = (0x9u << 8) | (0x7u << 4) | 0x2u; /* UBS=9 OAS=7 IAS=2 */
        raw |= kAdrenoIommuId2Vmid16 | kAdrenoIommuId2Ptfs4K;
        checks += 1u;
        if (adreno_iommu_id2_decode(raw, &d) != 0) {
            fails += 1u;
        } else {
            checks += 1u;
            if (d.raw != raw || d.ubs != 9u || d.oas != 7u || d.ias != 2u) {
                fails += 1u;
            }
            checks += 1u;
            /* The ENCODINGS and the WIDTHS must be different numbers here:
             * UBS=9 encodes to 48 (via the default), IAS=2 to 40. */
            if (d.ubs_bits != 48u || d.ias_bits != 40u || d.oas_bits != 48u) {
                fails += 1u;
            }
            checks += 1u;
            if (d.ptfs_4k != 1u || d.ptfs_16k != 0u || d.ptfs_64k != 0u ||
                d.vmid16 != 1u) {
                fails += 1u;
            }
        }
    }
    checks += 1u;
    /* An all-ones word: every flag set, every encoding 15 -> all 48 bits. */
    if (adreno_iommu_id2_decode(0xffffffffu, &d) != 0) {
        fails += 1u;
    } else if (d.ubs != 15u || d.oas != 15u || d.ias != 15u ||
               d.ubs_bits != 48u || d.oas_bits != 48u || d.ias_bits != 48u ||
               d.ptfs_4k != 1u || d.ptfs_16k != 1u || d.ptfs_64k != 1u ||
               d.vmid16 != 1u) {
        fails += 1u;
    }
    checks += 1u;
    /* An all-zero word: no flags, every encoding 0 -> 32 bits everywhere. */
    if (adreno_iommu_id2_decode(0u, &d) != 0) {
        fails += 1u;
    } else if (d.ubs_bits != 32u || d.oas_bits != 32u || d.ias_bits != 32u ||
               d.ptfs_4k != 0u || d.ptfs_16k != 0u || d.ptfs_64k != 0u ||
               d.vmid16 != 0u) {
        fails += 1u;
    }

    /* ---- ipa/pa/stage1-oas: three DIFFERENT quantities ------------------- */
    {
        uint32_t raw = (0x9u << 8) | (0x4u << 4) | 0x2u; /* UBS=9 OAS=4 IAS=2 */
        checks += 1u;
        if (adreno_iommu_ipa_size(raw) != 40u) {
            fails += 1u; /* IAS=2 -> 40 */
        }
        checks += 1u;
        if (adreno_iommu_pa_size(raw) != 44u) {
            fails += 1u; /* OAS=4 -> 44 */
        }
        checks += 1u;
        /* THE POINT: a stage-1 context's oas is the IPA size, so it MUST equal
         * ipa_size() and MUST differ from pa_size() on this word. A test that
         * only ever used a word where IAS == OAS could not tell them apart. */
        if (adreno_iommu_stage1_oas(raw) != adreno_iommu_ipa_size(raw)) {
            fails += 1u;
        }
        checks += 1u;
        if (adreno_iommu_stage1_oas(raw) == adreno_iommu_pa_size(raw)) {
            fails += 1u;
        }
    }

    /* ---- va_size: the VERSION GATE (P2:1899-1912) ----------------------- */
    {
        /* UBS=9 (->48), IAS=2 (->40). So v1 must say 40 and v2 must say 48;
         * if the gate were wrong BOTH would say the same number. */
        uint32_t raw = (0x9u << 8) | (0x7u << 4) | 0x2u;
        uint32_t v;
        checks += 1u;
        ok = 0;
        v = adreno_iommu_va_size(raw, kAdrenoIommuVersionV1, &ok);
        if (ok != 1 || v != 40u) {
            fails += 1u; /* v1: va_size = ipa_size = 40 */
        }
        checks += 1u;
        ok = 0;
        v = adreno_iommu_va_size(raw, kAdrenoIommuVersionV1_64K, &ok);
        if (ok != 1 || v != 40u) {
            fails += 1u; /* the 64K variant is still v1 for this rule */
        }
        checks += 1u;
        ok = 0;
        v = adreno_iommu_va_size(raw, kAdrenoIommuVersionV2, &ok);
        if (ok != 1 || v != 48u) {
            fails += 1u; /* v2: va_size = UBS width = 48 */
        }
        checks += 1u;
        /* The two branches must DISAGREE on a word where UBS and IAS differ,
         * else the gate would be untestable. */
        if (adreno_iommu_va_size(raw, kAdrenoIommuVersionV1, 0) ==
            adreno_iommu_va_size(raw, kAdrenoIommuVersionV2, 0)) {
            fails += 1u;
        }
        checks += 1u;
        /* An unknown version is REFUSED and ok cleared. */
        ok = 99;
        if (adreno_iommu_va_size(raw, kAdrenoIommuVersionUnknown, &ok) != 0u ||
            ok != 0) {
            fails += 1u;
        }
        checks += 1u;
        /* A nonsense version number is refused too, not coerced to v2. */
        ok = 99;
        if (adreno_iommu_va_size(raw, 7u, &ok) != 0u || ok != 0) {
            fails += 1u;
        }
        checks += 1u;
        /* A NULL ok must not crash: freestanding C, no libc. */
        if (adreno_iommu_va_size(raw, kAdrenoIommuVersionV2, 0) != 48u) {
            fails += 1u;
        }
    }

    /* ---- the version is DERIVED from the DT compatibles (P2:1965-1975) --- */
    checks += 1u;
    if (adreno_iommu_version_of_compatible("qcom,smmu-v2") !=
        kAdrenoIommuVersionV2) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_version_of_compatible("arm,mmu-401") !=
        kAdrenoIommuVersionV1_64K) {
        fails += 1u; /* the ONLY compatible that is not plain v1 or v2 */
    }
    checks += 1u;
    if (adreno_iommu_version_of_compatible("arm,smmu-v1") !=
        kAdrenoIommuVersionV1) {
        fails += 1u;
    }
    checks += 1u;
    /* The three compatibles this node ALSO carries are NOT in the pinned table;
     * treating them as v2 would be a guess, so they must be unknown. */
    if (adreno_iommu_version_of_compatible("qcom,sdm845-smmu-v2") !=
            kAdrenoIommuVersionUnknown ||
        adreno_iommu_version_of_compatible("qcom,adreno-smmu") !=
            kAdrenoIommuVersionUnknown) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_version_of_compatible(0) != kAdrenoIommuVersionUnknown) {
        fails += 1u;
    }
    {
        /* P4:4904's list, in DT order. Only the THIRD entry resolves. */
        static const char *const node_compat[] = {
            "qcom,sdm845-smmu-v2", "qcom,adreno-smmu", "qcom,smmu-v2"
        };
        checks += 1u;
        if (adreno_iommu_version_select(node_compat, 3u) !=
            kAdrenoIommuVersionV2) {
            fails += 1u;
        }
        checks += 1u;
        /* And the FIRST entry alone must NOT resolve -- that is the fact that
         * makes the ordered walk load-bearing rather than decorative. */
        if (adreno_iommu_version_select(node_compat, 1u) !=
            kAdrenoIommuVersionUnknown) {
            fails += 1u;
        }
        checks += 1u;
        if (adreno_iommu_version_select(0, 3u) != kAdrenoIommuVersionUnknown ||
            adreno_iommu_version_select(node_compat, 0u) !=
                kAdrenoIommuVersionUnknown) {
            fails += 1u;
        }
    }

    /* ---- the AArch64 context and the effective granule set -------------- */
    checks += 1u;
    if (adreno_iommu_aarch64_context(kAdrenoIommuId2Ptfs4K) != 1u ||
        adreno_iommu_aarch64_context(kAdrenoIommuId2Ptfs64K) != 1u ||
        adreno_iommu_aarch64_context(kAdrenoIommuId2Ptfs16K) != 1u) {
        fails += 1u;
    }
    checks += 1u;
    /* No granule bit at all -> no AArch64 context (P2:720-724). */
    if (adreno_iommu_aarch64_context(0x0000000fu) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_ptfs_effective(kAdrenoIommuId2Ptfs4K, 0u) != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_ptfs_effective(
            kAdrenoIommuId2Ptfs4K | kAdrenoIommuId2Ptfs64K, 0u) != 5u) {
        fails += 1u;
    }
    checks += 1u;
    /* P3:358: the Adreno impl CLEARS 16K. A word advertising 16K must report
     * only what survives, and 4K/64K must be untouched. */
    if (adreno_iommu_ptfs_effective(kAdrenoIommuId2Ptfs16K, 1u) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_ptfs_effective(
            kAdrenoIommuId2Ptfs4K | kAdrenoIommuId2Ptfs16K |
            kAdrenoIommuId2Ptfs64K, 1u) != 5u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_ptfs_effective(0u, 0u) != 0u ||
        adreno_iommu_ptfs_effective(0u, 1u) != 0u) {
        fails += 1u;
    }

    /* ---- coherent_walk: ID0.CTTW OR the firmware (P2:1799-1805) --------- */
    checks += 1u;
    if (adreno_iommu_coherent_walk(kAdrenoIommuId0Cttw, 0u) != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_coherent_walk(0u, 1u) != 1u) {
        fails += 1u; /* the firmware can claim it when the register does not */
    }
    checks += 1u;
    if (adreno_iommu_coherent_walk(0u, 0u) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_coherent_walk(kAdrenoIommuId0Cttw, 1u) != 1u) {
        fails += 1u;
    }

    /* ---- TTBR1 for the GPU, and upstream's idle-TTBR word ---------------- */
    checks += 1u;
    ok = 0;
    if (adreno_iommu_ttbr1_for_gpu(&ok) != 1u || ok != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_ttbr1_for_gpu(0) != 1u) {
        fails += 1u; /* NULL ok must not crash */
    }
    checks += 1u;
    /* P2:545-548: the non-selected bank carries the ASID and NO table. */
    if (adreno_iommu_ttbr_idle_word(0x12u) != 0x0012000000000000ull) {
        fails += 1u;
    }
    checks += 1u;
    /* And it differs from round 83's word only in the table pointer. */
    if (adreno_iommu_ttbr_idle_word(0x12u) ==
        adreno_smmu_ctx_ttbrn(0x12u, 0x877fe000ull)) {
        fails += 1u;
    }
    checks += 1u;
    if ((adreno_iommu_ttbr_idle_word(0x12u) & 0x0000ffffffffffffull) != 0ull) {
        fails += 1u;
    }

    /* ---- the window predicates and the pre-CX forbidden set ------------- */
    checks += 1u;
    if (adreno_iommu_window_is_kgsl(kAdrenoIommuKgslSmmuBase,
                                    kAdrenoIommuKgslSmmuSize) != 1) {
        fails += 1u;
    }
    checks += 1u;
    /* apps_smmu is the one flash #8 surveyed; it must NEVER satisfy the gate. */
    if (adreno_iommu_window_is_kgsl(kAdrenoIommuAppsSmmuBase,
                                    kAdrenoIommuKgslSmmuSize) != 0) {
        fails += 1u;
    }
    checks += 1u;
    /* A CONTAINING window is not the window: containment would let a caller
     * reach other registers under this gate's blessing. */
    if (adreno_iommu_window_is_kgsl(kAdrenoIommuKgslSmmuBase, 0x100000ull) != 0 ||
        adreno_iommu_window_is_kgsl(0x05000000ull, 0x100000ull) != 0) {
        fails += 1u;
    }
    checks += 1u;
    /* Rounds 22/28: every one of the six collapsible-rail windows is forbidden,
     * and a window that merely sits NEXT to them is not. These addresses are
     * chosen so each result distinguishes the window list from a guess. */
    if (adreno_iommu_forbidden_window_count() != 6u ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x05000000ull) != 1 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x05030000ull) != 1 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x05040000ull) != 1 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x05040028ull) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_addr_is_pre_cx_forbidden(0x05061000ull) != 1 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x0506a000ull) != 1 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x05070000ull) != 1 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x0509e000ull) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_addr_is_pre_cx_forbidden(0x0b200000ull) != 1) {
        fails += 1u; /* the GMU's VBIF window, at a completely different base */
    }
    checks += 1u;
    /* The round-28 GPUCC carve-out is a HOLE in the GMU window, not a deletion
     * of it: 0x05090000 and 0x05098000 are readable, but 0x05099000 -- just past
     * GPUCC, still inside the GMU's numeric reach -- is forbidden again. A model
     * that simply dropped the GMU window would get that last one wrong. */
    if (adreno_iommu_addr_is_pre_cx_forbidden(kAdrenoIommuGpuccBase) != 0 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x05098000ull) != 0 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x05099000ull) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_addr_is_pre_cx_forbidden(0x05050000ull) != 0 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x0509a000ull) != 0 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x0b500000ull) != 0 ||
        adreno_iommu_addr_is_pre_cx_forbidden(0x15000000ull) != 0) {
        fails += 1u; /* apps_smmu is a DIFFERENT unit on a different rail */
    }
    checks += 1u;
    /* The ID2 register itself is inside the forbidden window -- which is WHY a
     * gate has to exist rather than a "just read it" probe. */
    if (adreno_iommu_addr_is_pre_cx_forbidden(kAdrenoIommuKgslSmmuBase +
                                              kAdrenoIommuGr0Id2) != 1) {
        fails += 1u;
    }

    /* ---- the FAIL-CLOSED gate ------------------------------------------- */
    g.window_base = kAdrenoIommuKgslSmmuBase;
    g.window_size = kAdrenoIommuKgslSmmuSize;
    g.cx_status_word = 0x80005ff4u; /* the round-35 word WITH BIT(31) set */
    g.clocks_enabled = 1u;
    g.kmem_mapped = 1u;
    g.cb_already_owned = 0u;
    g.stream_faults = 1u;

    checks += 1u;
    if (adreno_iommu_assess(&g) != kAdrenoIommuProbePermitted) {
        fails += 1u; /* every precondition met -> permitted */
    }
    checks += 1u;
    if (adreno_iommu_read_permitted(&g) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_assess(0) != kAdrenoIommuProbeRefusedInvalid) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_read_permitted(0) != 0) {
        fails += 1u;
    }
    checks += 1u;
    /* THE MEASURED WORD: 0x00005ff4 has BIT(31) CLEAR, i.e. CX DOWN. This is
     * the exact word round 35 recorded, so the gate refuses the exact situation
     * the phone is actually in -- not a hypothetical one. */
    {
        struct adreno_iommu_gate_inputs c = g;
        c.cx_status_word = 0x00005ff4u;
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedCxDown) {
            fails += 1u;
        }
    }
    checks += 1u;
    if (adreno_iommu_read_permitted(&(struct adreno_iommu_gate_inputs){
            0x05040000ull, 0x10000ull, 0x00005ff4u, 1u, 1u, 0u, 1u}) != 0) {
        fails += 1u; /* the phone's ACTUAL state is refused, by value */
    }
    /* Every single missing precondition, one at a time, must produce its OWN
     * verdict -- not a generic refusal. A gate that collapsed them could not
     * tell an operator which one to fix. */
    checks += 1u;
    {
        struct adreno_iommu_gate_inputs c = g;
        c.window_base = kAdrenoIommuAppsSmmuBase;
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedWrongWindow) {
            fails += 1u;
        }
    }
    checks += 1u;
    {
        struct adreno_iommu_gate_inputs c = g;
        c.window_size = 0x80000ull; /* apps_smmu's size on the kgsl base */
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedWrongWindow) {
            fails += 1u;
        }
    }
    checks += 1u;
    {
        struct adreno_iommu_gate_inputs c = g;
        c.clocks_enabled = 0u;
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedClocksOff) {
            fails += 1u;
        }
    }
    checks += 1u;
    {
        struct adreno_iommu_gate_inputs c = g;
        c.kmem_mapped = 0u;
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedUnmapped) {
            fails += 1u;
        }
    }
    checks += 1u;
    {
        struct adreno_iommu_gate_inputs c = g;
        c.cb_already_owned = 1u;
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedCbOwned) {
            fails += 1u;
        }
    }
    checks += 1u;
    {
        struct adreno_iommu_gate_inputs c = g;
        c.stream_faults = 0u;
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedNoStream) {
            fails += 1u;
        }
    }
    checks += 1u;
    /* A zero or NON-POWER-OF-TWO window is invalid before it is a window. Note
     * that 0x8000 IS a power of two, so it is a legal size that then fails the
     * identity check -- which is a different verdict, and both are asserted so
     * the two stages cannot collapse into one. */
    {
        struct adreno_iommu_gate_inputs c = g;
        c.window_size = 0ull;
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedInvalid) {
            fails += 1u;
        }
        c = g;
        c.window_size = 0x10001ull; /* 65537: not a power of two */
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedInvalid) {
            fails += 1u;
        }
        c = g;
        c.window_size = 0x8000ull; /* legal size, WRONG window */
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedWrongWindow) {
            fails += 1u;
        }
    }
    checks += 1u;
    /* ORDER: a bad window beats a down CX, because the window check is pure.
     * If the order were reversed a caller with BOTH problems would be told to
     * power CX and only then discover it targeted the wrong SMMU. */
    {
        struct adreno_iommu_gate_inputs c = g;
        c.window_base = kAdrenoIommuAppsSmmuBase;
        c.cx_status_word = 0x00005ff4u;
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedWrongWindow) {
            fails += 1u;
        }
    }
    checks += 1u;
    /* And a down CX beats clocks/mapping/etc: the physical hazard sorts above
     * the merely incorrect. */
    {
        struct adreno_iommu_gate_inputs c = g;
        c.cx_status_word = 0x00005ff4u;
        c.clocks_enabled = 0u;
        c.kmem_mapped = 0u;
        if (adreno_iommu_assess(&c) != kAdrenoIommuProbeRefusedCxDown) {
            fails += 1u;
        }
    }

    /* ---- verdict names and precondition names/anchors ------------------ */
    {
        const uint32_t bits[6] = {
            kAdrenoIommuPreCxPowered, kAdrenoIommuPreClocksOn,
            kAdrenoIommuPreKmemMapped, kAdrenoIommuPreWindowIsKgsl,
            kAdrenoIommuPreCbFree, kAdrenoIommuPreStreamFaults
        };
        for (i = 0u; i < 6u; ++i) {
            const char *nm = adreno_iommu_precondition_name(bits[i]);
            const char *an = adreno_iommu_precondition_anchor(bits[i]);
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
            /* Distinct, so an operator cannot be told two different broken
             * things have the same name. */
            for (j = 0u; j < i; ++j) {
                const char *om = adreno_iommu_precondition_name(bits[j]);
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
        if (adreno_iommu_precondition_name(0u)[0] != 'u' ||
            adreno_iommu_precondition_anchor(0u)[0] != 'u') {
            fails += 1u;
        }
        checks += 1u;
        /* The required mask must be EXACTLY the six bits, assembled not typed. */
        if (adreno_iommu_required_preconditions() !=
            (uint32_t)kAdrenoIommuPreAll ||
            adreno_iommu_required_preconditions() != 0x3fu) {
            fails += 1u;
        }
    }
    checks += 1u;
    {
        /* Every verdict has a distinct name, and 0 is not "unknown". */
        const uint32_t verdicts[8] = {
            kAdrenoIommuProbePermitted, kAdrenoIommuProbeRefusedInvalid,
            kAdrenoIommuProbeRefusedWrongWindow, kAdrenoIommuProbeRefusedCxDown,
            kAdrenoIommuProbeRefusedClocksOff, kAdrenoIommuProbeRefusedUnmapped,
            kAdrenoIommuProbeRefusedCbOwned, kAdrenoIommuProbeRefusedNoStream
        };
        uint32_t bad = 0u;
        for (i = 0u; i < 8u; ++i) {
            const char *nm =
                adreno_iommu_verdict_name(
                    (enum adreno_iommu_probe_verdict)verdicts[i]);
            if (nm == 0 || nm[0] == '\0' || (nm[0] == 'u' && nm[1] == 'n')) {
                bad = 1u;
            }
        }
        if (bad != 0u) {
            fails += 1u;
        }
    }
    checks += 1u;
    if (adreno_iommu_verdict_name((enum adreno_iommu_probe_verdict)99)[0] !=
        'u') {
        fails += 1u;
    }

    /* ---- the ordered steps, and the ONE ordering rule that is safety ----- */
    checks += 1u;
    if (adreno_iommu_steps(steps, 20u, &count) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != adreno_iommu_step_count()) {
        fails += 1u;
    }
    checks += 1u;
    if (count != 14u) {
        fails += 1u; /* pinned so a DELETED step cannot pass silently */
    }
    checks += 1u;
    for (i = 0u; i < count; ++i) {
        if (steps[i].order != i) {
            fails += 1u;
            break;
        }
    }
    for (i = 0u; i < count; ++i) {
        if (steps[i].name == 0 || steps[i].name[0] == '\0' ||
            steps[i].anchor == 0 || steps[i].anchor[0] == '\0') {
            checks += 1u;
            fails += 1u;
            continue;
        }
        if (steps[i].kind == (uint32_t)kAdrenoIommuStepGate) {
            seen_gate += 1u;
            if (first_read == 0xffffffffu) {
                /* still before any read */
            }
            /* identify the CX step by NAME so the rule below cannot be
             * satisfied by an accidental ordering */
            {
                const char *a = "confirm-cx-powered";
                const char *b = steps[i].name;
                uint32_t k = 0u;
                uint32_t same = 1u;
                while (a[k] != '\0' || b[k] != '\0') {
                    if (a[k] != b[k]) {
                        same = 0u;
                        break;
                    }
                    k += 1u;
                }
                if (same != 0u) {
                    cx_step = i;
                }
            }
        } else if (steps[i].kind == (uint32_t)kAdrenoIommuStepRead) {
            seen_read += 1u;
            if (first_read == 0xffffffffu) {
                first_read = i;
            }
        } else if (steps[i].kind == (uint32_t)kAdrenoIommuStepDerive) {
            seen_derive += 1u;
        } else if (steps[i].kind == (uint32_t)kAdrenoIommuStepHandoff) {
            seen_handoff += 1u;
            handoff = i;
        } else {
            fails += 1u;
            break;
        }
    }
    checks += 1u;
    if (seen_gate != 6u || seen_read != 2u || seen_derive != 5u ||
        seen_handoff != 1u) {
        fails += 1u;
    }
    checks += 1u;
    /* THE SAFETY RULE, as an assertion over data rather than prose: the first
     * register READ must come AFTER the CX-powered gate. This is rounds 22/28
     * expressed so that it can fail. */
    if (cx_step == 0xffffffffu || first_read == 0xffffffffu ||
        first_read <= cx_step) {
        fails += 1u;
    }
    checks += 1u;
    if (handoff != count - 1u) {
        fails += 1u; /* the round-83 handoff is LAST */
    }
    checks += 1u;
    /* Every read is after every gate: no read may precede ANY gate. */
    {
        uint32_t last_gate = 0u;
        uint32_t bad = 0u;
        for (i = 0u; i < count; ++i) {
            if (steps[i].kind == (uint32_t)kAdrenoIommuStepGate) {
                last_gate = i;
            }
        }
        for (i = 0u; i < count; ++i) {
            if (steps[i].kind == (uint32_t)kAdrenoIommuStepRead &&
                i < last_gate) {
                bad = 1u;
            }
        }
        if (bad != 0u) {
            fails += 1u;
        }
    }
    checks += 1u;
    /* A short buffer is REFUSED with count 0, never truncated. */
    count = 99u;
    if (adreno_iommu_steps(tiny, 4u, &count) != 1 || count != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_steps(0, 20u, &count) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_iommu_steps(steps, 20u, 0) != 1) {
        fails += 1u;
    }

    /* ---- THE COMPOSITION with round 83 --------------------------------- */
    {
        /* A realistic word: UBS=2 (40), OAS=1 (36), IAS=2 (40), 4K granule. */
        uint32_t raw = (0x2u << 8) | (0x1u << 4) | 0x2u;
        raw |= kAdrenoIommuId2Ptfs4K;
        checks += 1u;
        if (adreno_iommu_stage1_words(raw, kAdrenoIommuVersionV2, 1u, &w) != 0) {
            fails += 1u;
        } else {
            checks += 1u;
            if (w.ok != 1u) {
                fails += 1u;
            }
            checks += 1u;
            if (w.ias != 40u || w.oas != 40u) {
                fails += 1u; /* va_size = UBS = 40; oas = ipa = 40 */
            }
            checks += 1u;
            /* The words ARE round 83's, called with those numbers. Recomputing
             * here would defeat the point, so this asserts identity. */
            if (w.tcr != adreno_smmu_ctx_tcr(40u, 1u, 1u, 0)) {
                fails += 1u; /* ttbr1 = 1 on the GPU (P3:200-208) */
            }
            checks += 1u;
            if (w.tcr2 != adreno_smmu_ctx_tcr2(40u, 1u, 0)) {
                fails += 1u;
            }
            checks += 1u;
            /* The concrete numbers, so a change in either module is visible. */
            if (w.tcr != 0xb5180080u) {
                fails += 1u;
            }
            checks += 1u;
            if (w.tcr2 != 0x00038012u) {
                fails += 1u; /* SEP(0x7) | PASIZE=2 (40 bits) | AS */
            }
            checks += 1u;
            /* T0SZ = 64 - ias, and it is the field ias actually moves. Because
             * the GPU selects the TTBR1 geometry, arm-smmu.h:405-409 SHIFTED the
             * whole word by 16, so T0SZ sits at bits 21..16 -- reading it at its
             * TTBR0 position returns 0, which is itself proof of the shift. */
            if ((w.tcr & kAdrenoSmmuTcrT0szMask) != 0u) {
                fails += 1u;
            }
            checks += 1u;
            if (((w.tcr >> 16) & kAdrenoSmmuTcrT0szMask) != (64u - w.ias)) {
                fails += 1u;
            }
            checks += 1u;
            /* The EPD pair round 83 exports: exactly one is set. */
            if (((w.tcr & kAdrenoSmmuTcrEpd0) != 0u) ==
                ((w.tcr & kAdrenoSmmuTcrEpd1) != 0u)) {
                fails += 1u;
            }
        }
        /* A v1 SMMU on the SAME word must produce a DIFFERENT ias (40 from IAS
         * in both cases here, so use a word where they differ). */
        {
            uint32_t raw2 = (0x4u << 8) | (0x1u << 4) | 0x2u; /* UBS=4(44) */
            raw2 |= kAdrenoIommuId2Ptfs4K;
            checks += 1u;
            if (adreno_iommu_stage1_words(raw2, kAdrenoIommuVersionV2, 1u,
                                          &w) != 0) {
                fails += 1u;
            } else if (w.ias != 44u) {
                fails += 1u; /* v2 takes UBS = 44, not IAS = 40 */
            }
            checks += 1u;
            if (adreno_iommu_stage1_words(raw2, kAdrenoIommuVersionV1, 1u,
                                          &w) != 0) {
                fails += 1u;
            } else if (w.ias != 40u) {
                fails += 1u; /* v1 takes IPA = 40, ignoring UBS entirely */
            }
        }
        /* The stage-1 oas must be driven by IAS (via ipa), NOT by OAS. A word
         * with IAS=2 (40) and OAS=6 (48) must yield PASIZE 2, not 5. */
        {
            uint32_t raw3 = (0x2u << 8) | (0x6u << 4) | 0x2u;
            raw3 |= kAdrenoIommuId2Ptfs4K;
            checks += 1u;
            if (adreno_iommu_stage1_words(raw3, kAdrenoIommuVersionV2, 1u,
                                          &w) != 0) {
                fails += 1u;
            } else if ((w.tcr2 & kAdrenoSmmuTcr2PasizeMask) != 2u) {
                fails += 1u; /* deriving oas from OAS would put 5 here */
            }
            checks += 1u;
            if (w.oas != 40u) {
                fails += 1u;
            }
        }
        /* Refusals: NULL out, unknown version, no 4 KiB granule, no AArch64. */
        checks += 1u;
        if (adreno_iommu_stage1_words(0u, kAdrenoIommuVersionV2, 1u, 0) != 1) {
            fails += 1u;
        }
        checks += 1u;
        {
            uint32_t raw4 = (0x2u << 8) | (0x1u << 4) | 0x2u;
            raw4 |= kAdrenoIommuId2Ptfs4K;
            if (adreno_iommu_stage1_words(raw4, kAdrenoIommuVersionUnknown, 1u,
                                          &w) != 1) {
                fails += 1u;
            }
            checks += 1u;
            /* A word with NO granule bit cannot host this context. */
            if (adreno_iommu_stage1_words(0x00000212u, kAdrenoIommuVersionV2,
                                          1u, &w) != 1) {
                fails += 1u;
            }
            checks += 1u;
            /* 16K only, on the Adreno impl -> cleared -> no 4 KiB -> refused. */
            if (adreno_iommu_stage1_words(
                    (0x2u << 8) | (0x1u << 4) | 0x2u | kAdrenoIommuId2Ptfs16K,
                    kAdrenoIommuVersionV2, 1u, &w) != 1) {
                fails += 1u;
            }
        }
        checks += 1u;
        /* A refusal must leave NO half-written words behind. */
        if (w.ias == 0u && w.tcr == 0u && w.tcr2 == 0u && w.ok == 0u) {
            /* deliberately allowed: the refusal zeroed the struct */
        } else {
            fails += 1u;
        }
        checks += 1u;
        /* coherent_walk is normalised to 0/1 and actually reaches the TCR: the
         * non-coherent branch sets SH0=OS where the coherent one sets IS. Under
         * the shifted TTBR1 geometry SH0 sits at bits 29..28. */
        {
            uint32_t raw5 = (0x2u << 8) | (0x1u << 4) | 0x2u;
            uint32_t raw5c = raw5 | kAdrenoIommuId2Ptfs4K;
            if (adreno_iommu_stage1_words(raw5c, kAdrenoIommuVersionV2, 0u,
                                          &w) != 0) {
                fails += 1u;
            } else if (((w.tcr >> 28) & 0x3u) !=
                       (uint32_t)kAdrenoSmmuTcrShOs) {
                fails += 1u;
            }
            checks += 1u;
            /* And the coherent branch differs at exactly that field. */
            {
                struct adreno_iommu_stage1_words wc;
                uint32_t raw5b = raw5 | kAdrenoIommuId2Ptfs4K;
                if (adreno_iommu_stage1_words(raw5b, kAdrenoIommuVersionV2, 1u,
                                              &wc) != 0) {
                    fails += 1u;
                } else if (((wc.tcr >> 28) & 0x3u) !=
                               (uint32_t)kAdrenoSmmuTcrShIs ||
                           ((wc.tcr >> 28) & 0x3u) == ((w.tcr >> 28) & 0x3u)) {
                    fails += 1u;
                }
                checks += 1u;
                /* A non-boolean is NORMALISED, not passed through into round
                 * 83's own range check, so the two modules cannot disagree. */
                {
                    struct adreno_iommu_stage1_words wn;
                    if (adreno_iommu_stage1_words(raw5b, kAdrenoIommuVersionV2,
                                                  7u, &wn) != 0 ||
                        wn.coherent_walk != 1u || wn.tcr != wc.tcr) {
                        fails += 1u;
                    }
                }
            }
        }
    }

    /* ---- the seven absent inputs, and DERIVED readiness ---------------- */
    checks += 1u;
    if (adreno_iommu_missing() != (uint32_t)kAdrenoIommuMissingAll) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoIommuMissingAll != 127u) {
        fails += 1u; /* seven bits, no more, no less */
    }
    checks += 1u;
    if (adreno_iommu_ready() != 0) {
        fails += 1u;
    }
    checks += 1u;
    /* Readiness is DERIVED, so it is 1 exactly when the mask is empty. Proving
     * that requires a mask of 0, which is reachable only by clearing every bit
     * -- so assert the equivalence over the two ends rather than faking it. */
    if ((adreno_iommu_missing() == 0u) != (adreno_iommu_ready() != 0)) {
        fails += 1u;
    }
    {
        const uint32_t bits[7] = {
            kAdrenoIommuMissingId2Word, kAdrenoIommuMissingId0Word,
            kAdrenoIommuMissingCxUp, kAdrenoIommuMissingAsid,
            kAdrenoIommuMissingPageTable, kAdrenoIommuMissingStream,
            kAdrenoIommuMissingKernelCaller
        };
        for (i = 0u; i < 7u; ++i) {
            const char *nm = adreno_iommu_missing_name(bits[i]);
            const char *an = adreno_iommu_missing_anchor(bits[i]);
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
                const char *om = adreno_iommu_missing_name(bits[j]);
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
        if (adreno_iommu_missing_name(0u)[0] != 'u' ||
            adreno_iommu_missing_anchor(0u)[0] != 'u') {
            fails += 1u;
        }
        checks += 1u;
        /* The seven bits are exactly 1..64: no two share a bit. */
        if (((uint32_t)kAdrenoIommuMissingId2Word != 1u) ||
            ((uint32_t)kAdrenoIommuMissingId0Word != 2u) ||
            ((uint32_t)kAdrenoIommuMissingCxUp != 4u) ||
            ((uint32_t)kAdrenoIommuMissingAsid != 8u) ||
            ((uint32_t)kAdrenoIommuMissingPageTable != 16u) ||
            ((uint32_t)kAdrenoIommuMissingStream != 32u) ||
            ((uint32_t)kAdrenoIommuMissingKernelCaller != 64u)) {
            fails += 1u;
        }
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return (fails != 0u) ? 1 : 0;
}
