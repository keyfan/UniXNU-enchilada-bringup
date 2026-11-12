// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoCpRingV1 -- see AdrenoCpRingV1.h for provenance and the honest
// HOST-VERIFIED boundary. Freestanding C11: no libc, no MMIO.

#include "AdrenoCpRingV1.h"

int adreno_cp_ring_offsets(uint32_t *rb_base, uint32_t *rb_cntl,
                           uint32_t *rptr_addr, uint32_t *rptr,
                           uint32_t *wptr, uint32_t *sqe_cntl,
                           uint32_t *sqe_instr_base)
{
    if (rb_base == 0 || rb_cntl == 0 || rptr_addr == 0 || rptr == 0 ||
        wptr == 0 || sqe_cntl == 0 || sqe_instr_base == 0) {
        return 1;
    }
    *rb_base = kAdrenoCpRingRbBase;
    *rb_cntl = kAdrenoCpRingRbCntl;
    *rptr_addr = kAdrenoCpRingRbRptrAddr;
    *rptr = kAdrenoCpRingRbRptr;
    *wptr = kAdrenoCpRingRbWptr;
    *sqe_cntl = kAdrenoCpRingSqeCntl;
    *sqe_instr_base = kAdrenoCpRingSqeInstrBase;
    return 0;
}

int adreno_cp_ring_plan(uint32_t has_shadow_bo,
                        struct adreno_cp_ring_step_desc *ops, uint32_t capacity,
                        uint32_t *count)
{
    uint32_t n = 0u;
    /* The required capacity is the number of steps actually emitted: the
     * shadow-RPTR write is present only when a shadow BO exists (P1:1134). A
     * caller must not be forced to reserve the unused slot. */
    uint32_t need = (has_shadow_bo != 0u)
                        ? kAdrenoCpRingStepCount
                        : (kAdrenoCpRingStepCount - 1u);

    if (ops == 0 || count == 0) {
        return 1;
    }
    *count = 0u;
    if (capacity < need) {
        return 1;
    }

    /* P1:1118 SQEs must be pointed at their microcode before the CP starts. */
    ops[n].step = kAdrenoCpRingStepSqeInstrBase;
    ops[n].reg_offset = kAdrenoCpRingSqeInstrBase;
    ops[n].is_iova = 1u;
    ops[n].conditional = 0u;
    n += 1u;

    /* P1:1121 the ring base is a 64-bit IOVA. */
    ops[n].step = kAdrenoCpRingStepRbBase;
    ops[n].reg_offset = kAdrenoCpRingRbBase;
    ops[n].is_iova = 1u;
    ops[n].conditional = 0u;
    n += 1u;

    /* P1:1127-1131 RB_CNTL differs by whether extended APRIV exists. */
    ops[n].step = kAdrenoCpRingStepRbCntl;
    ops[n].reg_offset = kAdrenoCpRingRbCntl;
    ops[n].is_iova = 0u;
    ops[n].conditional = 0u;
    n += 1u;

    /* P1:1134-1136 the shadow-RPTR address is written ONLY when a shadow BO
     * exists. This is the one conditional step. */
    if (has_shadow_bo != 0u) {
        ops[n].step = kAdrenoCpRingStepRptrAddr;
        ops[n].reg_offset = kAdrenoCpRingRbRptrAddr;
        ops[n].is_iova = 1u;
        ops[n].conditional = 1u;
        n += 1u;
    }

    /* P1:1151 "Enable the SQE to start the CP engine". */
    ops[n].step = kAdrenoCpRingStepSqeEnable;
    ops[n].reg_offset = kAdrenoCpRingSqeCntl;
    ops[n].is_iova = 0u;
    ops[n].conditional = 0u;
    n += 1u;

    /* P1:1153 the ME_INIT packet is last and touches no register directly. */
    ops[n].step = kAdrenoCpRingStepMeInit;
    ops[n].reg_offset = 0u;
    ops[n].is_iova = 0u;
    ops[n].conditional = 0u;
    n += 1u;

    *count = n;
    return 0;
}

int adreno_cp_ring_me_init(uint32_t *dwords, uint32_t capacity, uint32_t *count)
{
    uint32_t i;

    if (dwords == 0 || count == 0) {
        return 1;
    }
    *count = 0u;
    if (capacity < kAdrenoCpRingMeInitDwords) {
        return 1;
    }
    /* P1:618/:621/:624 then five zero dwords (:627,:628,:631,:634,:635). */
    dwords[0] = kAdrenoCpRingMeInitWord0;
    dwords[1] = kAdrenoCpRingMeInitWord1;
    dwords[2] = kAdrenoCpRingMeInitWord2;
    for (i = kAdrenoCpRingMeInitZeroFrom; i < kAdrenoCpRingMeInitDwords; ++i) {
        dwords[i] = 0u;
    }
    *count = kAdrenoCpRingMeInitDwords;
    return 0;
}

int adreno_cp_ring_me_init_matches(const uint32_t *dwords, uint32_t count)
{
    uint32_t i;

    if (dwords == 0 || count != kAdrenoCpRingMeInitDwords) {
        return 0;
    }
    if (dwords[0] != kAdrenoCpRingMeInitWord0 ||
        dwords[1] != kAdrenoCpRingMeInitWord1 ||
        dwords[2] != kAdrenoCpRingMeInitWord2) {
        return 0;
    }
    for (i = kAdrenoCpRingMeInitZeroFrom; i < kAdrenoCpRingMeInitDwords; ++i) {
        if (dwords[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

AdrenoCpRingShadowMode adreno_cp_ring_shadow_mode(int hw_apriv)
{
    /* P1:1127-1131: extended-APRIV targets keep the hardware RPTR shadow; all
     * others set NO_UPDATE and rely on CP_WHERE_AM_I (P1:61-66). enchilada is
     * the second case. */
    return hw_apriv ? kAdrenoCpRingShadowHwApriv : kAdrenoCpRingShadowWhereAmI;
}

int adreno_cp_ring_doorbell_order(uint32_t *barrier_before_write,
                                  uint32_t *write_wptr_after_barrier)
{
    if (barrier_before_write == 0 || write_wptr_after_barrier == 0) {
        return 1;
    }
    /* P1:87 mb() then P1:89 the single CP_RB_WPTR store. The order is the
     * content of this function: there is no path that writes first. */
    *barrier_before_write = 1u;
    *write_wptr_after_barrier = 1u;
    return 0;
}

uint32_t adreno_cp_ring_undefined(void)
{
    /* The historical five-bit question. Three are DERIVED since round 71; use
     * adreno_cp_ring_undefined_open() for what is still open. */
    return (uint32_t)kAdrenoCpRingUndefAll;
}

const char *adreno_cp_ring_undefined_name(uint32_t bit)
{
    switch (bit) {
    case kAdrenoCpRingUndefMeInitOpcode:
        return "cp-me-init-opcode";
    case kAdrenoCpRingUndefPacketHeader:
        return "type7-packet-header";
    case kAdrenoCpRingUndefRbCntlDefault:
        return "rb-cntl-default";
    case kAdrenoCpRingUndefRbCntlNoUpdate:
        return "rb-cntl-no-update";
    case kAdrenoCpRingUndefRingSize:
        return "ring-size";
    default:
        return "unknown";
    }
}

const char *adreno_cp_ring_undefined_anchor(uint32_t bit)
{
    switch (bit) {
    case kAdrenoCpRingUndefMeInitOpcode:
        /* DERIVED round 71. */
        return "adreno_pm4.xml:248 <value name=\"CP_ME_INIT\" value=\"0x48\"/>";
    case kAdrenoCpRingUndefPacketHeader:
        /* DERIVED round 71. */
        return "adreno_gpu.h:659-665 the OUT_PKT7 body; CP_TYPE7_PKT 0x70000000 from "
               "adreno_pm4.xml:234 + PM4_PARITY 0x9669 from :637-643)";
    case kAdrenoCpRingUndefRbCntlDefault:
        /* PARTIALLY derived: formula known, field accessors open. */
        return "msm_gpu.h:314-316 gives the FORMULA "
               "(BUFSZ(ilog2(SZ/8)) | BLKSZ(ilog2(BLKSZ/8))); the "
               "AXXX_CP_RB_CNTL_BUFSZ/BLKSZ accessors are generated-header-only";
    case kAdrenoCpRingUndefRbCntlNoUpdate:
        /* STILL OPEN. */
        return "a6xx_gpu.c:1131 ORs AXXX_CP_RB_CNTL_NO_UPDATE; it is a generated "
               "a6xx.xml.h bitfield accessor (no *.xml.h in the tree) and "
               "CP_RB_CNTL has NO bitfields in a6xx.xml:2282";
    case kAdrenoCpRingUndefRingSize:
        /* DERIVED round 71. */
        return "msm_gpu.h:311 MSM_GPU_RINGBUFFER_SZ = SZ_32K, and sizes.h:26 "
               "SZ_32K = 0x8000";
    default:
        return "unknown";
    }
}

uint32_t adreno_cp_ring_parity(uint32_t value)
{
    /* P10:637-643 PM4_PARITY: fold the 32-bit word in 4-bit steps, then index
     * the 0x9669 magic constant. */
    uint32_t x = value ^ (value >> 4) ^ (value >> 8) ^ (value >> 12) ^
                 (value >> 16) ^ (value >> 20) ^ (value >> 24) ^ (value >> 28);
    return (kAdrenoCpRingParityLut >> (0xfu & x)) & 1u;
}

uint32_t adreno_cp_ring_type7_header(uint32_t opcode, uint32_t count)
{
    /* P10:663-664 verbatim, with the masks the source applies made explicit. */
    uint32_t cnt = count & kAdrenoCpRingCntMask;
    uint32_t op = opcode & kAdrenoCpRingOpcodeMask;
    return kAdrenoCpRingType7Pkt |
           (cnt << 0) |
           (adreno_cp_ring_parity(cnt) << kAdrenoCpRingParityCntBit) |
           (op << 16) |
           (adreno_cp_ring_parity(op) << kAdrenoCpRingParityOpBit);
}

int adreno_cp_ring_me_init_message(uint32_t *dwords, uint32_t capacity,
                                   uint32_t *count)
{
    uint32_t n = 0u;

    if (dwords == 0 || count == 0) {
        return 1;
    }
    *count = 0u;
    /* ONE header dword + the 8 payload dwords = 9, not 10. */
    if (capacity < kAdrenoCpRingMeInitDwords + 1u) {
        return 1;
    }
    dwords[0] = adreno_cp_ring_type7_header(kAdrenoCpRingMeInitOpcode,
                                            kAdrenoCpRingMeInitDwords);
    if (adreno_cp_ring_me_init(&dwords[1], capacity - 1u, &n) != 0) {
        return 1;
    }
    if (n != kAdrenoCpRingMeInitDwords) {
        return 1;
    }
    *count = n + 1u;
    return 0;
}

int adreno_cp_ring_me_init_message_matches(const uint32_t *dwords, uint32_t count)
{
    if (dwords == 0 || count != kAdrenoCpRingMeInitDwords + 1u) {
        return 0;
    }
    if (dwords[0] != kAdrenoCpRingMeInitHeader) {
        return 0;
    }
    return adreno_cp_ring_me_init_matches(&dwords[1], kAdrenoCpRingMeInitDwords);
}

uint32_t adreno_cp_ring_buffer_bytes(void)
{
    return kAdrenoCpRingBufferBytes;
}

uint32_t adreno_cp_ring_buffer_dwords(void)
{
    return kAdrenoCpRingBufferDwords;
}

int adreno_cp_ring_rb_cntl_fields(uint32_t *bufsz_field, uint32_t *blksz_field)
{
    if (bufsz_field == 0 || blksz_field == 0) {
        return 1;
    }
    *bufsz_field = kAdrenoCpRingBufszField;
    *blksz_field = kAdrenoCpRingBlkszField;
    return 0;
}

uint32_t adreno_cp_ring_undefined_open(void)
{
    return (uint32_t)kAdrenoCpRingUndefOpenNow;
}

int adreno_cp_ring_is_executable(void)
{
    /* The ring is programmable only when its RB_CNTL word can be COMPUTED. It
     * cannot: U4 (the NO_UPDATE bit) is still open and so are U3's two field
     * accessors. Note the asymmetry with round 70: a PACKET can now be encoded
     * (see can_encode_packet), the RING cannot be programmed. */
    return 0;
}

int adreno_cp_ring_can_encode_packet(void)
{
    /* Round 71 closed U1 and U2, so the header exists. This is what changed. */
    return (adreno_cp_ring_undefined_open() &
            (kAdrenoCpRingUndefMeInitOpcode | kAdrenoCpRingUndefPacketHeader))
               == 0u;
}

int adreno_cp_ring_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    uint32_t rb_base = 0u, rb_cntl = 0u, rptr_addr = 0u, rptr = 0u;
    uint32_t wptr = 0u, sqe_cntl = 0u, sqe_instr = 0u;
    uint32_t i = 0u;
    uint32_t count = 0u;
    uint32_t dwords[kAdrenoCpRingMeInitDwords];
    struct adreno_cp_ring_step_desc ops[kAdrenoCpRingStepCount];
    struct adreno_cp_ring_step_desc few[kAdrenoCpRingStepCount - 1u];
    uint32_t barrier = 0u, store = 0u;

    /* Offsets. */
    checks += 1u;
    if (adreno_cp_ring_offsets(&rb_base, &rb_cntl, &rptr_addr, &rptr, &wptr,
                               &sqe_cntl, &sqe_instr) != 0) {
        fails += 1u; 
    }
    checks += 1u;
    if (rb_base != 0x0800u || rb_cntl != 0x0802u || rptr_addr != 0x0804u ||
        rptr != 0x0806u || wptr != 0x0807u || sqe_cntl != 0x0808u ||
        sqe_instr != 0x0830u) {
        fails += 1u; 
    }
    /* The offsets are the XML's WORD offsets, so consecutive registers differ
     * by small deltas -- a byte-offset reading of the table would separate
     * them by 4x. */
    checks += 1u;
    if (rptr_addr != rb_base + 4u || rptr != rb_base + 6u ||
        wptr != rb_base + 7u || sqe_cntl != rb_base + 8u) {
        fails += 1u; 
    }
    checks += 1u;
    if (adreno_cp_ring_offsets(0, &rb_cntl, &rptr_addr, &rptr, &wptr,
                               &sqe_cntl, &sqe_instr) == 0) {
        fails += 1u; 
    }

    /* Plan WITH a shadow BO: six steps, exactly one conditional, two IOVAs. */
    checks += 1u;
    if (adreno_cp_ring_plan(1u, ops, kAdrenoCpRingStepCount, &count) != 0) {
        fails += 1u; 
    }
    checks += 1u;
    if (count != kAdrenoCpRingStepCount) {
        fails += 1u; 
    }
    checks += 1u;
    for (i = 0u; i < count; ++i) {
        if (ops[i].step != i) {
            fails += 1u; 
            break;
        }
    }
    checks += 1u;
    if (ops[0].reg_offset != kAdrenoCpRingSqeInstrBase || ops[0].is_iova != 1u) {
        fails += 1u; 
    }
    checks += 1u;
    if (ops[1].reg_offset != kAdrenoCpRingRbBase || ops[1].is_iova != 1u) {
        fails += 1u; 
    }
    checks += 1u;
    if (ops[2].reg_offset != kAdrenoCpRingRbCntl || ops[2].is_iova != 0u ||
        ops[2].conditional != 0u) {
        fails += 1u; 
    }
    checks += 1u;
    if (ops[3].step != kAdrenoCpRingStepRptrAddr || ops[3].conditional != 1u ||
        ops[3].is_iova != 1u) {
        fails += 1u; 
    }
    checks += 1u;
    if (ops[4].reg_offset != kAdrenoCpRingSqeCntl) {
        fails += 1u; 
    }
    checks += 1u;
    if (ops[5].step != kAdrenoCpRingStepMeInit || ops[5].reg_offset != 0u) {
        fails += 1u; 
    }
    checks += 1u;
    {
        uint32_t iovas = 0u, conds = 0u;
        for (i = 0u; i < count; ++i) {
            if (ops[i].is_iova != 0u) {
                iovas += 1u;
            }
            if (ops[i].conditional != 0u) {
                conds += 1u;
            }
        }
        if (iovas != 3u || conds != 1u) {
            fails += 1u; 
        }
    }

    /* Plan WITHOUT a shadow BO: five steps, the conditional one is dropped. */
    checks += 1u;
    if (adreno_cp_ring_plan(0u, few, kAdrenoCpRingStepCount - 1u, &count) != 0) {
        fails += 1u; 
    }
    checks += 1u;
    if (count != kAdrenoCpRingStepCount - 1u) {
        fails += 1u; 
    }
    checks += 1u;
    for (i = 0u; i < count; ++i) {
        if (few[i].conditional != 0u) {
            fails += 1u; 
        }
    }
    checks += 1u;
    if (count > 3u && few[3].step == kAdrenoCpRingStepRptrAddr) {
        fails += 1u;  /* the shadow write must be absent */
    }
    checks += 1u;
    if (adreno_cp_ring_plan(1u, ops, kAdrenoCpRingStepCount - 1u, &count) == 0) {
        fails += 1u;  /* a shadow BO demands the sixth slot */
    }
    checks += 1u;
    if (adreno_cp_ring_plan(0u, ops, kAdrenoCpRingStepCount, &count) != 0) {
        fails += 1u;  /* an oversized buffer must still succeed (capacity is a minimum) */
    }
    checks += 1u;
    if (count != kAdrenoCpRingStepCount - 1u) {
        fails += 1u;  /* ...and it still emits five, not six */
    }
    checks += 1u;
    if (adreno_cp_ring_plan(1u, 0, kAdrenoCpRingStepCount, &count) == 0) {
        fails += 1u; 
    }

    /* CP_ME_INIT payload. */
    checks += 1u;
    if (adreno_cp_ring_me_init(dwords, kAdrenoCpRingMeInitDwords, &count) != 0) {
        fails += 1u; 
    }
    checks += 1u;
    if (count != 8u) {
        fails += 1u; 
    }
    checks += 1u;
    if (dwords[0] != 0x0000002fu || dwords[1] != 0x00000003u ||
        dwords[2] != 0x20000000u) {
        fails += 1u; 
    }
    checks += 1u;
    for (i = 3u; i < 8u; ++i) {
        if (dwords[i] != 0u) {
            fails += 1u; 
            break;
        }
    }
    checks += 1u;
    if (adreno_cp_ring_me_init_matches(dwords, count) != 1) {
        fails += 1u; 
    }
    checks += 1u;
    if (adreno_cp_ring_me_init_matches(dwords, 7u) != 0) {
        fails += 1u;  /* the A7xx length is refused */
    }
    checks += 1u;
    {
        uint32_t wrong[8];
        for (i = 0u; i < 8u; ++i) {
            wrong[i] = dwords[i];
        }
        wrong[1] = 0x00000007u; /* the A7xx mask word shape */
        if (adreno_cp_ring_me_init_matches(wrong, 8u) != 0) {
            fails += 1u; 
        }
    }
    checks += 1u;
    if (adreno_cp_ring_me_init_matches(0, 8u) != 0) {
        fails += 1u; 
    }
    checks += 1u;
    if (adreno_cp_ring_me_init(dwords, 7u, &count) == 0) {
        fails += 1u; 
    }
    checks += 1u;
    if (count != 0u) {
        fails += 1u; 
    }
    checks += 1u;
    if (kAdrenoCpRingA7xxMeInitDwords == kAdrenoCpRingMeInitDwords) {
        fails += 1u; 
    }

    /* Shadow mode + doorbell order. */
    checks += 1u;
    if (adreno_cp_ring_shadow_mode(0) != kAdrenoCpRingShadowWhereAmI) {
        fails += 1u; 
    }
    checks += 1u;
    if (adreno_cp_ring_shadow_mode(1) != kAdrenoCpRingShadowHwApriv) {
        fails += 1u; 
    }
    checks += 1u;
    if (adreno_cp_ring_shadow_mode(0) == adreno_cp_ring_shadow_mode(1)) {
        fails += 1u; 
    }
    checks += 1u;
    if (adreno_cp_ring_doorbell_order(&barrier, &store) != 0) {
        fails += 1u; 
    }
    checks += 1u;
    if (barrier != 1u || store != 1u) {
        fails += 1u; 
    }
    checks += 1u;
    if (adreno_cp_ring_doorbell_order(0, &store) == 0) {
        fails += 1u; 
    }

    /* The five undefined constants: total names/anchors, distinct bits. */
    checks += 1u;
    if (adreno_cp_ring_undefined() != kAdrenoCpRingUndefAll) {
        fails += 1u; 
    }
    {
        const uint32_t bits[5] = {kAdrenoCpRingUndefMeInitOpcode,
                                  kAdrenoCpRingUndefPacketHeader,
                                  kAdrenoCpRingUndefRbCntlDefault,
                                  kAdrenoCpRingUndefRbCntlNoUpdate,
                                  kAdrenoCpRingUndefRingSize};
        uint32_t seen = 0u;
        for (i = 0u; i < 5u; ++i) {
            checks += 1u;
            if ((seen & bits[i]) != 0u) {
                fails += 1u;  /* duplicate bit */
            }
            seen |= bits[i];
            checks += 1u;
            if (adreno_cp_ring_undefined_name(bits[i])[0] == '\0' ||
                adreno_cp_ring_undefined_anchor(bits[i])[0] == '\0') {
                fails += 1u; 
            }
            checks += 1u;
            if (adreno_cp_ring_undefined_name(bits[i])[0] == 'u' &&
                adreno_cp_ring_undefined_name(bits[i])[1] == 'n') {
                fails += 1u;  /* no real bit may fall through to "unknown" */
            }
        }
        checks += 1u;
        if (seen != kAdrenoCpRingUndefAll) {
            fails += 1u; 
        }
    }
    checks += 1u;
    if (adreno_cp_ring_undefined_name(0u)[0] != 'u') {
        fails += 1u; 
    }
    checks += 1u;
    if (adreno_cp_ring_undefined_anchor(0u)[0] != 'u') {
        fails += 1u; 
    }
    checks += 1u;
    if (adreno_cp_ring_is_executable() != 0) {
        fails += 1u; 
    }

    /* ---- Round-71 encoders --------------------------------------------- */
    /* PM4_PARITY (P10:637-643). */
    checks += 1u;
    if (adreno_cp_ring_parity(0u) != 1u) {
        fails += 1u; /* 0x9669 bit 0 is SET: parity(0) is 1, not 0 */
    }
    checks += 1u;
    if (adreno_cp_ring_parity(8u) != 0u) {
        fails += 1u; /* cnt=8 folds to 0 */
    }
    checks += 1u;
    if (adreno_cp_ring_parity(0x48u) != 1u) {
        fails += 1u; /* the opcode's parity is 1 */
    }
    checks += 1u;
    if (adreno_cp_ring_parity(0x7fu) != 0u) {
        fails += 1u; /* folds to 0x8 */
    }
    checks += 1u;
    if (adreno_cp_ring_parity(0xffffffffu) != 1u) {
        fails += 1u; /* folds to 0x0 -> LUT bit 0 */
    }
    checks += 1u;
    if (adreno_cp_ring_parity(1u) != 0u) {
        fails += 1u; /* folds to 0x1 -> LUT bit 1 */
    }
    /* The type-7 header. */
    checks += 1u;
    if (adreno_cp_ring_type7_header(0x48u, 8u) != 0x70c80008u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_cp_ring_type7_header(0x48u, 8u) != kAdrenoCpRingMeInitHeader) {
        fails += 1u;
    }
    checks += 1u;
    if ((adreno_cp_ring_type7_header(0x48u, 8u) & 0x70000000u) !=
        kAdrenoCpRingType7Pkt) {
        fails += 1u;
    }
    checks += 1u;
    if (((adreno_cp_ring_type7_header(0x48u, 8u) >> 16) & 0x7fu) != 0x48u) {
        fails += 1u;
    }
    checks += 1u;
    if (((adreno_cp_ring_type7_header(0x48u, 8u) >> 15) & 1u) != 0u) {
        fails += 1u; /* count parity bit clear for cnt=8 */
    }
    checks += 1u;
    if (((adreno_cp_ring_type7_header(0x48u, 8u) >> 23) & 1u) != 1u) {
        fails += 1u; /* opcode parity bit set for 0x48 */
    }
    checks += 1u;
    if (((adreno_cp_ring_type7_header(0xc8u, 1u) >> 16) & 0x7fu) != 0x48u) {
        fails += 1u; /* the opcode is MASKED to 7 bits, not truncated */
    }
    checks += 1u;
    if ((adreno_cp_ring_type7_header(0x48u, 8u) & 0x7fffu) != 8u) {
        fails += 1u; /* count occupies bits 0..14 */
    }

    /* The full 9-dword message. */
    {
        uint32_t msg[9];
        uint32_t bad[9];
        uint32_t n = 0u;

        checks += 1u;
        if (adreno_cp_ring_me_init_message(msg, 9u, &n) != 0) {
            fails += 1u;
        }
        checks += 1u;
        if (n != 9u) {
            fails += 1u;
        }
        checks += 1u;
        if (msg[0] != 0x70c80008u) {
            fails += 1u;
        }
        checks += 1u;
        if (msg[1] != 0x0000002fu || msg[2] != 0x00000003u ||
            msg[3] != 0x20000000u) {
            fails += 1u;
        }
        checks += 1u;
        if (msg[4] != 0u || msg[5] != 0u || msg[6] != 0u || msg[7] != 0u ||
            msg[8] != 0u) {
            fails += 1u;
        }
        checks += 1u;
        if (adreno_cp_ring_me_init_message_matches(msg, 9u) != 1) {
            fails += 1u;
        }
        checks += 1u;
        if (adreno_cp_ring_me_init_message_matches(msg, 8u) != 0) {
            fails += 1u; /* the payload alone is not the message */
        }
        for (i = 0u; i < 9u; ++i) {
            bad[i] = msg[i];
        }
        bad[0] = 0x70000000u;
        checks += 1u;
        if (adreno_cp_ring_me_init_message_matches(bad, 9u) != 0) {
            fails += 1u;
        }
        checks += 1u;
        if (adreno_cp_ring_me_init_message(msg, 8u, &n) == 0) {
            fails += 1u; /* capacity must cover header + payload */
        }
        checks += 1u;
        if (n != 0u) {
            fails += 1u;
        }
        checks += 1u;
        if (adreno_cp_ring_me_init_message(0, 9u, &n) == 0) {
            fails += 1u;
        }
    }

    /* Ring geometry + the two ilog2 fields. */
    checks += 1u;
    if (adreno_cp_ring_buffer_bytes() != 0x8000u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_cp_ring_buffer_dwords() != 0x2000u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_cp_ring_buffer_bytes() != adreno_cp_ring_buffer_dwords() * 4u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_cp_ring_buffer_bytes() != kAdrenoCpRingBufferBytes) {
        fails += 1u;
    }
    {
        uint32_t bufsz = 0u, blksz = 0u;
        checks += 1u;
        if (adreno_cp_ring_rb_cntl_fields(&bufsz, &blksz) != 0) {
            fails += 1u;
        }
        checks += 1u;
        if (bufsz != 12u) {
            fails += 1u; /* ilog2(0x8000/8) */
        }
        checks += 1u;
        if (blksz != 2u) {
            fails += 1u; /* ilog2(32/8) */
        }
        checks += 1u;
        if ((1u << bufsz) != adreno_cp_ring_buffer_bytes() / 8u) {
            fails += 1u; /* the field really is ilog2 of an eighth */
        }
        checks += 1u;
        if (adreno_cp_ring_rb_cntl_fields(0, &blksz) == 0) {
            fails += 1u;
        }
    }

    /* The open set shrank to exactly U3|U4, and the two that closed are the two
     * that make a packet encodable. */
    checks += 1u;
    if (adreno_cp_ring_undefined_open() != 12u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_cp_ring_undefined_open() !=
        (kAdrenoCpRingUndefRbCntlDefault | kAdrenoCpRingUndefRbCntlNoUpdate)) {
        fails += 1u;
    }
    checks += 1u;
    if ((adreno_cp_ring_undefined_open() &
         kAdrenoCpRingUndefMeInitOpcode) != 0u) {
        fails += 1u; /* U1 closed */
    }
    checks += 1u;
    if ((adreno_cp_ring_undefined_open() &
         kAdrenoCpRingUndefPacketHeader) != 0u) {
        fails += 1u; /* U2 closed */
    }
    checks += 1u;
    if ((adreno_cp_ring_undefined_open() &
         kAdrenoCpRingUndefRingSize) != 0u) {
        fails += 1u; /* U5 closed */
    }
    checks += 1u;
    if (adreno_cp_ring_can_encode_packet() != 1) {
        fails += 1u;
    }
    checks += 1u;
    if ((adreno_cp_ring_undefined_open() & kAdrenoCpRingUndefAll) !=
        adreno_cp_ring_undefined_open()) {
        fails += 1u; /* open is a subset of the historical five */
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return fails == 0u ? 0 : 1;
}
