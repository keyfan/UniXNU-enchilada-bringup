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
    /* All five are genuinely undefined in the pinned set -- see the header. */
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
        return "a6xx_gpu.c:616/650 name CP_ME_INIT; the value is not in the "
               "pinned a6xx.xml / adreno_common.xml (zero occurrences)";
    case kAdrenoCpRingUndefPacketHeader:
        return "a6xx_gpu.c:616 uses OUT_PKT7; its definition is in an "
               "unpinned header";
    case kAdrenoCpRingUndefRbCntlDefault:
        return "a6xx_gpu.c:1128 names MSM_GPU_RB_CNTL_DEFAULT; value unpinned";
    case kAdrenoCpRingUndefRbCntlNoUpdate:
        return "a6xx_gpu.c:1131 names AXXX_CP_RB_CNTL_NO_UPDATE; bit unpinned";
    case kAdrenoCpRingUndefRingSize:
        return "a6xx_gpu.c never states the ring size; MSM_GPU_RINGBUFFER_SZ "
               "is absent from every pinned file";
    default:
        return "unknown";
    }
}

int adreno_cp_ring_is_executable(void)
{
    /* A ring is executable only when it can encode a packet AND knows its
     * opcode. U2 (header) and U1 (opcode) are both open, so this is 0, and it
     * stays 0 until an unpinned header is genuinely pinned. */
    return 0;
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

    if (check_count != 0) {
        *check_count = checks;
    }
    return fails == 0u ? 0 : 1;
}
