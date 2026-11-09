// SPDX-License-Identifier: BSD-2-Clause-Patent
// Read-only apps_smmu (SMMUv2) survey core for enchilada. See SmmuSurvey.h for
// the provenance of every register offset and the refusal rules.
//
// Freestanding: no libc, no allocation, no MMIO of its own. The only I/O is the
// caller's read32 callback, which the kernel shim points at the mapped window.
#include "SmmuSurvey.h"

// SMMUv2 register offsets, identical to owners in this tree.
#define OFF_SCR0 0x000u   // NativeUfs.cpp:873
#define OFF_IDR0 0x020u   // NativeUfs.cpp:873
#define OFF_IDR1 0x024u   // NativeUfs.cpp:873,913
#define OFF_IDR2 0x028u   // Dwc3SmmuPlan.h:47
#define OFF_GFSR 0x048u   // NativeUfs.cpp:874
#define OFF_SMR  0x800u   // NativeUfs.cpp:882 ; SmmuTakeover.h:141
#define OFF_S2CR 0xc00u   // NativeUfs.cpp:883 ; SmmuTakeover.h:142

#define SMR_VALID 0x80000000u
#define IDR0_EXIDS 0x00000100u
#define S2CR_TYPE_MASK 0x00030000u
#define S2CR_TYPE_SHIFT 16u

// Bounded unsigned formatting: the freestanding build has no snprintf, and the
// kernel-side string helpers are not part of this layer.
static size_t put_char(char *out, size_t at, size_t cap, char value)
{
    if (at + 1u < cap) {
        out[at] = value;
        return at + 1u;
    }
    return at;
}

static size_t put_text(char *out, size_t at, size_t cap, const char *text)
{
    for (size_t i = 0; text[i] && at + 1u < cap; ++i) out[at++] = text[i];
    return at;
}

static size_t put_hex32(char *out, size_t at, size_t cap, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[8];
    for (unsigned i = 0; i < 8u; ++i) text[i] = digits[(value >> (28u - 4u * i)) & 0xfu];
    for (unsigned i = 0; i < 8u; ++i) at = put_char(out, at, cap, text[i]);
    return at;
}

static size_t put_dec(char *out, size_t at, size_t cap, unsigned value)
{
    char text[10];
    unsigned count = 0;
    if (!value) return put_char(out, at, cap, '0');
    while (value && count < sizeof text) {
        text[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (count) at = put_char(out, at, cap, text[--count]);
    return at;
}

const char *smmu_survey_type_name(unsigned type)
{
    switch (type) {
    case kSmmuSurveyBypass: return "bypass";
    case kSmmuSurveyTranslation: return "translation";
    case kSmmuSurveyFault: return "fault";
    default: return "reserved";
    }
}

static int read_reg(const struct smmu_survey_io *io, uint32_t offset,
                    uint32_t *out, unsigned *reads)
{
    if (!io || !io->read32 || !out) return kSmmuSurveyNull;
    if (io->read32(io->ctx, offset, out)) return kSmmuSurveyIo;
    if (reads) ++*reads;
    return kSmmuSurveyOk;
}

int smmu_survey_geometry_read(const struct smmu_survey_io *io,
                              struct smmu_survey_geometry *out)
{
    if (!io || !io->read32 || !out) return kSmmuSurveyNull;
    *out = (struct smmu_survey_geometry){0};
    unsigned reads = 0;
    int status = read_reg(io, OFF_IDR0, &out->id0, &reads);
    if (!status) status = read_reg(io, OFF_IDR1, &out->id1, &reads);
    if (!status) status = read_reg(io, OFF_IDR2, &out->id2, &reads);
    if (!status) status = read_reg(io, OFF_SCR0, &out->scr0, &reads);
    if (!status) status = read_reg(io, OFF_GFSR, &out->gfsr, &reads);
    if (status) return status;

    out->slots = out->id0 & 0xffu;
    out->banks = out->id1 & 0xffu;
    out->pages = 1u << (((out->id1 >> 28) & 7u) + 1u);
    out->exids = (out->id0 & IDR0_EXIDS) ? 1u : 0u;
    // Never guess the 64K geometry (NativeUfs/tests/SmmuTakeover.cpp:33).
    if (out->id1 & 0x80000000u) return kSmmuSurveyUnsupported;
    out->stride = 4096u;
    if (!out->slots || !out->slots || out->slots > SMMU_SURVEY_MAX_SLOTS) {
        return kSmmuSurveyUnsupported;
    }
    if (!out->banks || out->banks > SMMU_SURVEY_MAX_SLOTS) {
        return kSmmuSurveyUnsupported;
    }
    if (out->pages > 128u ||
        ((uint64_t)out->pages + out->banks) * out->stride >
            SMMU_SURVEY_SDM845_SIZE) {
        return kSmmuSurveyUnsupported;
    }
    return kSmmuSurveyOk;
}

// Standard stream-match comparison, the same shape as NativeUfs.cpp:887-888 and
// Dwc3SmmuPlan.h:74-75: the SMR holds the SID in [15:0] and a mask in [30:16];
// a stream matches when every bit outside the mask agrees. The caller's own mask
// is OR-ed in, because the DTB's `iommus = <&apps_smmu 0x100 0xf>` names bits
// the firmware is allowed to vary and the hardware alone would not know that.
static int stream_matches(uint16_t sid, uint16_t want_mask, uint32_t smr,
                          unsigned exids, uint32_t *matched_id,
                          uint32_t *matched_mask)
{
    uint32_t mask = exids ? ((smr >> 16) & 0xffffu) : ((smr >> 16) & 0x7fffu);
    mask |= (uint32_t)(want_mask & 0x7fffu);
    const uint32_t id = smr & 0xffffu;
    if ((((uint32_t)sid ^ id) & ~mask) & 0xffffu) return 0;
    if (matched_id) *matched_id = id;
    if (matched_mask) *matched_mask = mask;
    return 1;
}

int smmu_survey_scan(const struct smmu_survey_io *io,
                     const struct smmu_survey_geometry *geometry,
                     const struct smmu_survey_targets *targets,
                     struct smmu_survey_result *out)
{
    if (!io || !io->read32 || !geometry || !out) return kSmmuSurveyNull;
    const struct smmu_survey_targets defaults = smmu_survey_default_targets();
    const struct smmu_survey_targets t = targets ? *targets : defaults;
    *out = (struct smmu_survey_result){0};
    out->first_free = 0xffffu;
    unsigned reads = 0;
    for (unsigned slot = 0; slot < geometry->slots; ++slot) {
        uint32_t smr = 0, s2cr = 0;
        int status = read_reg(io, OFF_SMR + 4u * slot, &smr, &reads);
        if (status) return status;
        // A slot whose SMR is not valid is free and cannot match anything, so
        // its S2CR is not read at all -- one fewer bus access per empty slot.
        if (!(smr & SMR_VALID)) {
            if (out->first_free == 0xffffu) out->first_free = slot;
            continue;
        }
        status = read_reg(io, OFF_S2CR + 4u * slot, &s2cr, &reads);
        if (status) return status;
        ++out->valid;
        const unsigned type = (s2cr & S2CR_TYPE_MASK) >> S2CR_TYPE_SHIFT;
        if (type < 4u) ++out->type_count[type];

        struct smmu_survey_stream *target = NULL;
        uint16_t want = 0;
        if (stream_matches(t.ufs.sid, t.ufs.mask, smr, geometry->exids, NULL, NULL)) {
            target = &out->ufs;
            want = t.ufs.sid;
        } else if (stream_matches(t.usb.sid, t.usb.mask, smr, geometry->exids, NULL, NULL)) {
            target = &out->usb;
            want = t.usb.sid;
        }
        if (target && !target->found) {
            target->sid = want;
            target->found = 1;
            target->slot = slot;
            target->bank = s2cr & 0xffu;
            target->smr = smr;
            target->s2cr = s2cr;
            target->type = type;
            target->translated = type != kSmmuSurveyBypass;
        }
    }
    out->reads = reads;
    return kSmmuSurveyOk;
}

size_t smmu_survey_format(const struct smmu_survey_geometry *geometry,
                          const struct smmu_survey_result *result,
                          char *buffer, size_t cap)
{
    if (!buffer || !cap) return 0;
    size_t at = 0;
    buffer[0] = 0;
    if (!geometry || !result) return 0;

    at = put_text(buffer, at, cap, "SS slots=");
    at = put_dec(buffer, at, cap, geometry->slots);
    at = put_text(buffer, at, cap, " banks=");
    at = put_dec(buffer, at, cap, geometry->banks);
    at = put_text(buffer, at, cap, " pages=");
    at = put_dec(buffer, at, cap, geometry->pages);
    at = put_text(buffer, at, cap, " exids=");
    at = put_dec(buffer, at, cap, geometry->exids);
    at = put_text(buffer, at, cap, " valid=");
    at = put_dec(buffer, at, cap, result->valid);
    at = put_text(buffer, at, cap, " t0=");
    at = put_dec(buffer, at, cap, result->type_count[kSmmuSurveyBypass]);
    at = put_text(buffer, at, cap, " t1=");
    at = put_dec(buffer, at, cap, result->type_count[kSmmuSurveyTranslation]);
    at = put_text(buffer, at, cap, " t2=");
    at = put_dec(buffer, at, cap, result->type_count[kSmmuSurveyFault]);
    at = put_text(buffer, at, cap, " scr0=");
    at = put_hex32(buffer, at, cap, geometry->scr0);
    at = put_text(buffer, at, cap, " gfsr=");
    at = put_hex32(buffer, at, cap, geometry->gfsr);

    // The two streams that matter for this port, reported with the bank and
    // translation state a takeover decision would have to respect.
    at = put_text(buffer, at, cap, " ufs=");
    if (result->ufs.found) {
        at = put_dec(buffer, at, cap, result->ufs.slot);
        at = put_char(buffer, at, cap, '/');
        at = put_dec(buffer, at, cap, result->ufs.bank);
        at = put_char(buffer, at, cap, '/');
        at = put_text(buffer, at, cap, smmu_survey_type_name(result->ufs.type));
    } else {
        at = put_text(buffer, at, cap, "none");
    }
    at = put_text(buffer, at, cap, " usb=");
    if (result->usb.found) {
        at = put_dec(buffer, at, cap, result->usb.slot);
        at = put_char(buffer, at, cap, '/');
        at = put_dec(buffer, at, cap, result->usb.bank);
        at = put_char(buffer, at, cap, '/');
        at = put_text(buffer, at, cap, smmu_survey_type_name(result->usb.type));
    } else {
        at = put_text(buffer, at, cap, "none");
    }
    buffer[at < cap ? at : cap - 1u] = 0;
    return at;
}
