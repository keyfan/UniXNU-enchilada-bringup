// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoGen1BackendV1 -- see AdrenoGen1BackendV1.h for provenance and the
// honest HOST-VERIFIED boundary. Freestanding C11: no libc, no MMIO.

#include "AdrenoGen1BackendV1.h"

/* Region name ids, kept local: the header only needs them opaque. */
enum {
    kRegionDebug = 0u,
    kRegionLog = 1u,
    kRegionHfi = 2u,
    kRegionDummy = 3u
};

static void zero_admission(struct enchilada_gen1_admission *out)
{
    uint32_t i;
    for (i = 0u; i < kEnchiladaGen1PrereqCount; ++i) {
        out->items[i].id = i;
        out->items[i].modeled = 0u;
        out->items[i].satisfied = 0u;
    }
    out->modeled_count = 0u;
    out->satisfied_count = 0u;
    out->verdict = kEnchiladaGen1AdmitBlocked;
}

int enchilada_gen1_backend_a630_pins(uint32_t *chip_id, uint32_t *chip_id_alt,
                                     uint32_t *revn, uint32_t *gmem_bytes)
{
    if (chip_id == 0 || chip_id_alt == 0 || revn == 0 || gmem_bytes == 0) {
        return 1;
    }
    *chip_id = kEnchiladaGen1ChipId;
    *chip_id_alt = kEnchiladaGen1ChipIdAlt;
    *revn = kEnchiladaGen1Revn;
    *gmem_bytes = kEnchiladaGen1GmemBytes;
    return 0;
}

int enchilada_gen1_backend_has_gbif(void)
{
    /* a6xx_gpu.h:71-77: a6xx_has_gbif() returns false for adreno_is_a630(). */
    return 0;
}

const char *enchilada_gen1_backend_gbif_reason(void)
{
    return "a6xx_has_gbif() returns false for A630 (a6xx_gpu.h:71-77)";
}

int enchilada_gen1_backend_gmu_resume(struct enchilada_gen1_resume_op *ops,
                                      uint32_t capacity, uint32_t *count)
{
    if (ops == 0 || count == 0) {
        return 1;
    }
    *count = 0u;
    if (capacity < kEnchiladaGen1ResumeStepCount) {
        return 1;
    }
    /* P2:1033 CX runtime get. */
    ops[0].step = kEnchiladaGen1ResumeCxRuntimeGet;
    ops[0].takes_value = 0u;
    ops[0].value = 0u;
    ops[0].guarded = 0u;
    /* P2:1040-1041 GX runtime get, guarded by !IS_ERR_OR_NULL(gmu->gxpd) --
     * on enchilada GX software power-on is gdsc_gx_do_nothing_enable (P6:135),
     * so this op is expected to be skipped, not to fail. */
    ops[1].step = kEnchiladaGen1ResumeGxRuntimeGet;
    ops[1].takes_value = 0u;
    ops[1].value = 0u;
    ops[1].guarded = 1u;
    /* P2:1044 core rate, unconditional. */
    ops[2].step = kEnchiladaGen1ResumeCoreRate;
    ops[2].takes_value = 1u;
    ops[2].value = kEnchiladaGen1CoreRateHz;
    ops[2].guarded = 0u;
    /* P2:1045-1046 hub rate, generation-dependent. */
    ops[3].step = kEnchiladaGen1ResumeHubRate;
    ops[3].takes_value = 1u;
    ops[3].value = kEnchiladaGen1HubRateHz;
    ops[3].guarded = 0u;
    /* P2:1047 clock bulk enable. */
    ops[4].step = kEnchiladaGen1ResumeClocks;
    ops[4].takes_value = 0u;
    ops[4].value = 0u;
    ops[4].guarded = 0u;
    *count = kEnchiladaGen1ResumeStepCount;
    return 0;
}

uint32_t enchilada_gen1_backend_hub_rate_hz(int a740_family)
{
    /* P2:1045-1046 adreno_is_a740_family(...) ? 200000000 : 150000000. */
    return a740_family ? kEnchiladaGen1HubRateHzA740 : kEnchiladaGen1HubRateHz;
}

int enchilada_gen1_backend_gmu_regions(struct enchilada_gen1_region *out,
                                       uint32_t capacity, uint32_t *count)
{
    if (out == 0 || count == 0) {
        return 1;
    }
    *count = 0u;
    if (capacity < kEnchiladaGen1GmuRegionCount) {
        return 1;
    }
    /* P2:1741 A630 debug region is SZ_16K at IOVA 0 (allocator chooses). */
    out[0].name_id = kRegionDebug;
    out[0].bytes = kEnchiladaGen1GmuDebugBytes;
    out[0].iova = kEnchiladaGen1GmuUnassignedIova;
    out[0].executable = 0u;
    /* P2:1747 log region SZ_16K at 0. */
    out[1].name_id = kRegionLog;
    out[1].bytes = kEnchiladaGen1GmuLogBytes;
    out[1].iova = kEnchiladaGen1GmuUnassignedIova;
    out[1].executable = 0u;
    /* P2:1752 HFI queues SZ_16K at 0. */
    out[2].name_id = kRegionHfi;
    out[2].bytes = kEnchiladaGen1GmuHfiBytes;
    out[2].iova = kEnchiladaGen1GmuUnassignedIova;
    out[2].executable = 0u;
    /* P2:1696/:1708-1709 dummy page stays SZ_4K at 0x60000000: the SZ_8K widening
     * at :1704 is inside the a660/a7xx branch and must NOT be applied to A630. */
    out[3].name_id = kRegionDummy;
    out[3].bytes = kEnchiladaGen1GmuDummyBytes;
    out[3].iova = kEnchiladaGen1GmuDummyIova;
    out[3].executable = 0u;
    *count = kEnchiladaGen1GmuRegionCount;
    return 0;
}

int enchilada_gen1_backend_has_icache_region(void)
{
    /* P2:1721-1725: legacy ("pre-650-family") GPUs take neither the a650/a660
     * icache/dcache branch nor the a640 branch; A630 (:1736) allocates debug
     * only. No executable region exists, so nothing here is XN-cleared. */
    return 0;
}

uint32_t enchilada_gen1_backend_gmu_region_bytes(void)
{
    return kEnchiladaGen1GmuRegionBytes;
}

int enchilada_gen1_backend_sptprac(uint32_t enable, uint32_t *write_value,
                                   uint32_t *poll_mask, uint32_t *poll_target)
{
    if (write_value == 0 || poll_mask == 0 || poll_target == 0) {
        return 1;
    }
    if (enable != 0u) {
        /* P2:373 enable write; P2:375-376 poll (val & 0x38) == 0x28. */
        *write_value = kEnchiladaGen1SptpracEnable;
    } else {
        /* P2:398 disable write. It polls the same register/shape. */
        *write_value = kEnchiladaGen1SptpracDisable;
    }
    *poll_mask = kEnchiladaGen1SptpracPollMask;
    *poll_target = kEnchiladaGen1SptpracPollTarget;
    return 0;
}

uint32_t enchilada_gen1_backend_pas_id(void)
{
    return kEnchiladaGen1GPasId;
}

const char *enchilada_gen1_backend_pas_origin(void)
{
    return "a6xx_gpu.c:17 GPU_PAS_ID 13, used at :819; enchilada.dts:23612 "
           "qcom,pas-id = <0x0d>. NOT inherited from the A740/sheng tree, which "
           "has no PAS concept in code.";
}

int enchilada_gen1_backend_cp_ring_offsets(uint32_t *rb_base, uint32_t *rb_cntl,
                                           uint32_t *rptr_addr, uint32_t *rptr,
                                           uint32_t *wptr)
{
    if (rb_base == 0 || rb_cntl == 0 || rptr_addr == 0 || rptr == 0 || wptr == 0) {
        return 1;
    }
    *rb_base = kEnchiladaGen1CpRbBase;
    *rb_cntl = kEnchiladaGen1CpRbCntl;
    *rptr_addr = kEnchiladaGen1CpRbRptrAddr;
    *rptr = kEnchiladaGen1CpRbRptr;
    *wptr = kEnchiladaGen1CpRbWptr;
    return 0;
}

int enchilada_gen1_backend_admit(struct enchilada_gen1_admission *out)
{
    uint32_t i;

    if (out == 0) {
        return 1;
    }
    zero_admission(out);

    /* modeled: a host-verified freestanding model of the item exists here.
     * satisfied: a device-capable backend HOLDS it. Nothing is satisfied today;
     * the two columns are set independently so a future round can move one
     * without silently moving the other. */
    for (i = 0u; i < kEnchiladaGen1PrereqCount; ++i) {
        out->items[i].id = i;
        out->items[i].modeled = 0u;
        out->items[i].satisfied = 0u;
    }

    /* rounds 51/56/57/58/59/61/62/63/64: ordered power path + windows. */
    out->items[kEnchiladaGen1PrereqPowerPath].modeled = 1u;
    out->items[kEnchiladaGen1PrereqWindows].modeled = 1u;
    /* round 42 cold-boot op stream; round 44 bus; round 46 caller. */
    out->items[kEnchiladaGen1PrereqGmuBoot].modeled = 1u;
    /* round 31 legacy HFI v1 transport (2 queues, no padding, no alignment). */
    out->items[kEnchiladaGen1PrereqHfiV1].modeled = 1u;
    /* round 38 device-sourced blob inventory: file presence proven, load not. */
    out->items[kEnchiladaGen1PrereqFirmware].modeled = 1u;
    /* ZAP_AUTH: modelled 0. Nothing in this tree models qcom_scm PAS auth; the
     * sheng tree has no PAS code to port, and the 13 is a Linux-side fact. */
    out->items[kEnchiladaGen1PrereqZapAuth].modeled = 0u;
    /* round 53: a fail-closed READ-safety gate, not a stage-1 context. */
    out->items[kEnchiladaGen1PrereqSmmuStage1].modeled = 0u;
    /* round 70: AdrenoCpRingV1 now encodes the 6-step CP bring-up order, the
     * exact 8-dword CP_ME_INIT payload and the doorbell barrier rule, so the
     * ORDER is modelled. It is still NOT executable: five constants, including
     * the packet header encoding and the ME_INIT opcode, are undefined in every
     * pinned source (AdrenoCpRingV1.c adreno_cp_ring_is_executable() == 0).
     * modeled = 1 therefore records the order, not a working ring. */
    out->items[kEnchiladaGen1PrereqCpRing].modeled = 1u;
    /* round 67: the zero gate that WAITS for a board VA map; the map itself is
     * the missing input, so this is not "modelled" as an available input. */
    out->items[kEnchiladaGen1PrereqBoardVaMap].modeled = 0u;
    /* rounds 35/49/50/52: narrow read-only sub-results only. */
    out->items[kEnchiladaGen1PrereqDeviceProof].modeled = 0u;
    /* CONFIG_XNU_NATIVE_ADRENO stays n. */
    out->items[kEnchiladaGen1PrereqConfigOn].modeled = 0u;
    /* macOS-only frontend toolchain. */
    out->items[kEnchiladaGen1PrereqMacosFrontend].modeled = 0u;

    for (i = 0u; i < kEnchiladaGen1PrereqCount; ++i) {
        if (out->items[i].modeled != 0u) {
            out->modeled_count += 1u;
        }
        if (out->items[i].satisfied != 0u) {
            out->satisfied_count += 1u;
        }
    }

    out->verdict = (out->satisfied_count == kEnchiladaGen1PrereqCount)
                       ? kEnchiladaGen1AdmitReady
                       : kEnchiladaGen1AdmitBlocked;
    return 0;
}

const char *enchilada_gen1_backend_prereq_name(uint32_t id)
{
    switch (id) {
    case kEnchiladaGen1PrereqPowerPath:
        return "power-path";
    case kEnchiladaGen1PrereqWindows:
        return "gpucc-windows";
    case kEnchiladaGen1PrereqGmuBoot:
        return "gmu-cold-boot";
    case kEnchiladaGen1PrereqHfiV1:
        return "hfi-v1-transport";
    case kEnchiladaGen1PrereqFirmware:
        return "firmware-blobs";
    case kEnchiladaGen1PrereqZapAuth:
        return "zap-auth";
    case kEnchiladaGen1PrereqSmmuStage1:
        return "kgsl-smmu-stage1";
    case kEnchiladaGen1PrereqCpRing:
        return "cp-ring";
    case kEnchiladaGen1PrereqBoardVaMap:
        return "board-va-map";
    case kEnchiladaGen1PrereqDeviceProof:
        return "device-proof";
    case kEnchiladaGen1PrereqConfigOn:
        return "config-on";
    case kEnchiladaGen1PrereqMacosFrontend:
        return "macos-frontend";
    default:
        return "unknown";
    }
}

const char *enchilada_gen1_backend_prereq_anchor(uint32_t id)
{
    switch (id) {
    case kEnchiladaGen1PrereqPowerPath:
        return "a6xx_gmu.c:1033-1052 resume order; gpucc-sdm845.c:119-139";
    case kEnchiladaGen1PrereqWindows:
        return "enchilada.dts:23632-23657 six GPU clocks; :23889-23890 four GMU";
    case kEnchiladaGen1PrereqGmuBoot:
        return "a6xx_gmu.c:1066-1068 legacy cold/warm via GMU_GENERAL_7";
    case kEnchiladaGen1PrereqHfiV1:
        return "a6xx_hfi.c:59 and :90-92 legacy queue rules; a6xx_gmu.c:1736-1738";
    case kEnchiladaGen1PrereqFirmware:
        return "a6xx_catalog.c:889-892 fw names; :897 zapfw a630_zap.mdt";
    case kEnchiladaGen1PrereqZapAuth:
        return "a6xx_gpu.c:17 GPU_PAS_ID 13 and :819; enchilada.dts:23612-23613";
    case kEnchiladaGen1PrereqSmmuStage1:
        return "enchilada.dts:23848-23872 kgsl-smmu-v2 at 0x5040000";
    case kEnchiladaGen1PrereqCpRing:
        return "a6xx.xml:2281-2285 CP_RB_BASE/CNTL/RPTR_ADDR/RPTR/WPTR";
    case kEnchiladaGen1PrereqBoardVaMap:
        return "NativeAdrenoABI.h:127-128 board-derived, never from a guess";
    case kEnchiladaGen1PrereqDeviceProof:
        return "round 35 CX=0x00005ff4 BIT(31) clear; window-zero gate round 67";
    case kEnchiladaGen1PrereqConfigOn:
        return "configs/enchilada_defconfig CONFIG_XNU_NATIVE_ADRENO is not set";
    case kEnchiladaGen1PrereqMacosFrontend:
        return "docs/porting/gpu.md section 1 backend-before-frontend rule";
    default:
        return "unknown";
    }
}

int enchilada_gen1_backend_verify(uint32_t *check_count)
{
    uint32_t checks = 0u;
    uint32_t fails = 0u;
    uint32_t id = 0u;
    uint32_t revn = 0u;
    uint32_t gmem = 0u;
    uint32_t alt = 0u;
    uint32_t count = 0u;
    uint32_t value = 0u;
    uint32_t mask = 0u;
    uint32_t target = 0u;
    uint32_t base = 0u;
    uint32_t cntl = 0u;
    uint32_t rptr_addr = 0u;
    uint32_t rptr = 0u;
    uint32_t wptr = 0u;
    struct enchilada_gen1_resume_op ops[kEnchiladaGen1ResumeStepCount];
    struct enchilada_gen1_region regions[kEnchiladaGen1GmuRegionCount];
    struct enchilada_gen1_admission adm;

    /* Catalog pins. */
    checks += 1u;
    if (enchilada_gen1_backend_a630_pins(&id, &alt, &revn, &gmem) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (id != 0x06030001u) {
        fails += 1u;
    }
    checks += 1u;
    if (alt != 0x06030002u) {
        fails += 1u;
    }
    checks += 1u;
    if (revn != 630u) {
        fails += 1u;
    }
    checks += 1u;
    if (gmem != 0x00100000u) {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_a630_pins(0, &alt, &revn, &gmem) == 0) {
        fails += 1u;
    }

    /* GBIF: A630 has none. */
    checks += 1u;
    if (enchilada_gen1_backend_has_gbif() != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_gbif_reason()[0] == '\0') {
        fails += 1u;
    }

    /* Resume plan: five ordered ops, exactly two carry a rate. */
    checks += 1u;
    if (enchilada_gen1_backend_gmu_resume(ops, kEnchiladaGen1ResumeStepCount,
                                          &count) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != kEnchiladaGen1ResumeStepCount) {
        fails += 1u;
    }
    checks += 1u;
    if (ops[0].step != kEnchiladaGen1ResumeCxRuntimeGet || ops[0].guarded != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (ops[1].step != kEnchiladaGen1ResumeGxRuntimeGet || ops[1].guarded != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (ops[2].value != 200000000u || ops[2].takes_value != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (ops[3].value != 150000000u || ops[3].takes_value != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (ops[4].step != kEnchiladaGen1ResumeClocks || ops[4].takes_value != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_gmu_resume(ops, 4u, &count) == 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != 0u) {
        fails += 1u; /* short capacity must leave *count at 0 */
    }
    checks += 1u;
    if (enchilada_gen1_backend_hub_rate_hz(1) != 200000000u) {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_hub_rate_hz(0) != 150000000u) {
        fails += 1u;
    }

    /* GMU regions: four, no icache, total 0xd000. */
    checks += 1u;
    if (enchilada_gen1_backend_gmu_regions(regions, kEnchiladaGen1GmuRegionCount,
                                           &count) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (count != kEnchiladaGen1GmuRegionCount) {
        fails += 1u;
    }
    checks += 1u;
    if (regions[0].bytes != 0x4000u || regions[0].iova != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (regions[1].bytes != 0x4000u || regions[1].iova != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (regions[2].bytes != 0x4000u || regions[2].iova != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (regions[3].bytes != 0x1000u ||
        regions[3].iova != 0x60000000u) {
        fails += 1u; /* the a660/a7xx SZ_8K widening must NOT be applied */
    }
    checks += 1u;
    if (regions[0].executable || regions[1].executable || regions[2].executable ||
        regions[3].executable) {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_has_icache_region() != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_gmu_region_bytes() != 0xd000u) {
        fails += 1u;
    }
    checks += 1u;
    if (regions[0].bytes + regions[1].bytes + regions[2].bytes + regions[3].bytes
        != enchilada_gen1_backend_gmu_region_bytes()) {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_gmu_regions(regions, 3u, &count) == 0) {
        fails += 1u;
    }

    /* SPTPRAC. */
    checks += 1u;
    if (enchilada_gen1_backend_sptprac(1u, &value, &mask, &target) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (value != 0x778000u) {
        fails += 1u;
    }
    checks += 1u;
    if (mask != 0x38u || target != 0x28u) {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_sptprac(0u, &value, &mask, &target) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (value != 0x778001u) {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_sptprac(1u, 0, &mask, &target) == 0) {
        fails += 1u;
    }

    /* PAS 13 + attribution. */
    checks += 1u;
    if (enchilada_gen1_backend_pas_id() != 13u) {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_pas_origin()[0] == '\0') {
        fails += 1u;
    }
    checks += 1u;
    if (enchilada_gen1_backend_pas_id() != kEnchiladaGen1GPasId) {
        fails += 1u;
    }

    /* CP ring offsets. */
    checks += 1u;
    if (enchilada_gen1_backend_cp_ring_offsets(&base, &cntl, &rptr_addr, &rptr,
                                               &wptr) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (base != 0x0800u || cntl != 0x0802u || rptr_addr != 0x0804u ||
        rptr != 0x0806u || wptr != 0x0807u) {
        fails += 1u; /* word offsets, and RPTR/WPTR are the +4/+5 pair */
    }
    checks += 1u;
    if (enchilada_gen1_backend_cp_ring_offsets(0, &cntl, &rptr_addr, &rptr,
                                               &wptr) == 0) {
        fails += 1u;
    }

    /* Admission: 6 modelled (round 70 added CP_RING's order), 0 satisfied,
     * BLOCKED. */
    checks += 1u;
    if (enchilada_gen1_backend_admit(&adm) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adm.modeled_count != 6u) {
        fails += 1u;
    }
    checks += 1u;
    if (adm.satisfied_count != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adm.verdict != kEnchiladaGen1AdmitBlocked) {
        fails += 1u;
    }
    checks += 1u;
    if (adm.modeled_count + 6u != kEnchiladaGen1PrereqCount) {
        fails += 1u;
    }
    checks += 1u;
    if (adm.items[kEnchiladaGen1PrereqZapAuth].modeled != 0u) {
        fails += 1u; /* ZAP_AUTH is NOT modelled: sheng has no PAS code */
    }
    checks += 1u;
    if (adm.items[kEnchiladaGen1PrereqCpRing].modeled != 1u) {
        fails += 1u; /* round 70 modelled the ORDER (and only the order) */
    }
    checks += 1u;
    if (adm.items[kEnchiladaGen1PrereqHfiV1].modeled != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (adm.items[kEnchiladaGen1PrereqPowerPath].modeled != 1u ||
        adm.items[kEnchiladaGen1PrereqWindows].modeled != 1u ||
        adm.items[kEnchiladaGen1PrereqGmuBoot].modeled != 1u ||
        adm.items[kEnchiladaGen1PrereqFirmware].modeled != 1u) {
        fails += 1u;
    }
    for (id = 0u; id < kEnchiladaGen1PrereqCount; ++id) {
        checks += 1u;
        if (adm.items[id].satisfied != 0u) {
            fails += 1u;
        }
    }
    checks += 1u;
    if (enchilada_gen1_backend_admit(0) == 0) {
        fails += 1u;
    }

    /* Names/anchors are total: no NULL, no empty, and id 12 is out of range. */
    for (id = 0u; id < kEnchiladaGen1PrereqCount; ++id) {
        checks += 1u;
        if (enchilada_gen1_backend_prereq_name(id)[0] == '\0' ||
            enchilada_gen1_backend_prereq_anchor(id)[0] == '\0') {
            fails += 1u;
        }
    }
    checks += 1u;
    if (enchilada_gen1_backend_prereq_name(kEnchiladaGen1PrereqCount)[0] != 'u') {
        fails += 1u; /* the out-of-range sentinel reaches "unknown" */
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return fails == 0u ? 0 : 1;
}
