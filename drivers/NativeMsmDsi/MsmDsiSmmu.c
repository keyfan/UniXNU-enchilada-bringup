// SPDX-License-Identifier: BSD-2-Clause-Patent
// Read-only ARM SMMUv2 display-bank survey (stage-3 layer-3 prerequisite).
// Pure C over an injected READ-ONLY register bus: no IOKit, no MMIO writes.
// See MsmDsiSmmu.h for the two independent authoritative sources and the
// physical-vs-IOVA decision this survey resolves.
#include "MsmDsiSmmu.h"

/* SMMUv2 geometry, verbatim from SmmuTakeover.h:38, :48-50. */
unsigned msm_smmu_num_smr(uint32_t idr0) {
    unsigned n = (unsigned)(idr0 & 0xffu);
    return n > 128u ? 128u : n;
}

unsigned msm_smmu_num_cb(uint32_t idr1) {
    unsigned n = (unsigned)(idr1 & 0xffu);
    return n > 128u ? 128u : n;
}

unsigned msm_smmu_global_pages(uint32_t idr1) {
    return 1u << ((unsigned)((idr1 >> 28) & 7u) + 1u);
}

/* The enchilada/SDM845 display stream IDs and masks, taken VERBATIM from the
 * vendor DTS iommus cells <phandle SID MASK>:
 *   :853  qcom,mdss_mdp      <0x29 0x880 0x08 0x29 0xc80 0x08>
 *   :995  smmu_sde_sec_cb    <0x29 0x881 0x08 0x29 0xc81 0x08>
 *   :3393 smmu_rot_unsec_cb  <0x29 0x1090 0x00>
 *   :3399 smmu_rot_sec_cb    <0x29 0x1091 0x00>
 * phandle 0x29 is apps_smmu (:13996, :25897). The MASK cell is the set of low
 * ID bits the SMMU ignores (SmmuTakeover.h:24-29). */
static const struct msm_smmu_stream kDisplayStreams[] = {
    {0x880u, 0x08u}, {0xc80u, 0x08u}, {0x881u, 0x08u},
    {0xc81u, 0x08u}, {0x1090u, 0x00u}, {0x1091u, 0x00u},
};

const struct msm_smmu_stream *msm_smmu_display_streams(size_t *count) {
    if (count) *count = sizeof(kDisplayStreams) / sizeof(kDisplayStreams[0]);
    return kDisplayStreams;
}

/* Classify ONE matched stream's bank. Returns 1 -> physical/bypass is safe,
 * 0 -> an IOVA is required (translation on, fault, or out-of-range bank).
 * SMMUv2 S2CR.TYPE: 1 = bypass (addresses pass through unchanged); 0 = use the
 * translation context bank named by CBNDX, whose SCTLR.M (bit0) decides whether
 * translation is actually applied (SmmuTakeover.h:98-100, NativeUfs.cpp:926).
 * A type-0 bank with M==0 is translation-off => physical passes through (this
 * is the "SCTLR 0xe0 = translation off" state in NativeUfs.cpp:1196). */
static int bank_is_bypass(struct msm_smmu_io *io, uint64_t window,
                          unsigned global_pages, unsigned num_cb,
                          uint32_t s2cr, struct msm_smmu_survey *r,
                          int record_first) {
    uint32_t type = MSM_SMMU_S2CR_TYPE(s2cr);
    uint32_t bank = MSM_SMMU_S2CR_CBNDX(s2cr);
    if (record_first) {
        r->first_s2cr = s2cr;
        r->first_stream_valid = 0;
    }
    if (type == 1u) return 1;                 /* S2CR bypass: physical, always */
    if (type != 0u || bank >= num_cb) return 0; /* fault / bad bank: assume IOVA */
    uint64_t cb = (uint64_t)(global_pages + bank) * 0x1000ull;
    if (cb + 0x58ull > window) return 0;
    uint32_t sctlr = io->rd(io->ctx, (uint32_t)(cb + MSM_SMMU_CB_SCTLR));
    uint32_t ttbr0_lo = io->rd(io->ctx, (uint32_t)(cb + MSM_SMMU_CB_TTBR0_LO));
    uint32_t ttbr0_hi = io->rd(io->ctx, (uint32_t)(cb + MSM_SMMU_CB_TTBR0_HI));
    uint32_t tcr = io->rd(io->ctx, (uint32_t)(cb + MSM_SMMU_CB_TCR));
    uint32_t cbar = io->rd(io->ctx, (uint32_t)MSM_SMMU_CBAR(bank));
    if (record_first) {
        r->first_sctlr = sctlr;
        r->first_ttbr0_lo = ttbr0_lo;
        r->first_ttbr0_hi = ttbr0_hi;
        r->first_tcr = tcr;
        r->first_cbar = cbar;
        r->first_stream_valid = 1;
    }
    return (sctlr & MSM_SMMU_SCTLR_M) == 0u ? 1 : 0;
}

int msm_smmu_survey(struct msm_smmu_io *io, uint64_t window,
                    const struct msm_smmu_stream *streams, size_t nstreams,
                    struct msm_smmu_survey *out) {
    if (!io || !out || !io->rd || !streams || nstreams == 0u || nstreams > 32u)
        return -1;

    struct msm_smmu_survey r;
    /* No libc: the sibling pure-C units (MsmDsiCmd.c / DsiDcs.c) are freestanding
     * and the CI recipe's arm64-apple-darwin compile has no string.h here. */
    uint8_t *b = (uint8_t *)&r;
    for (size_t i = 0; i < sizeof r; ++i) b[i] = 0u;
    r.first_stream = 0xffffu;
    r.first_slot = 0xffffffffu;
    r.verdict = kMsmSmmuVerdictGeometry;

    /* Globals need at least a 4 KiB window (highest read sGFSR at 0x48). */
    if (window >= 0x1000ull) {
        r.scr0 = io->rd(io->ctx, MSM_SMMU_sCR0);
        r.idr0 = io->rd(io->ctx, MSM_SMMU_IDR0);
        r.idr1 = io->rd(io->ctx, MSM_SMMU_IDR1);
        r.idr2 = io->rd(io->ctx, MSM_SMMU_IDR2);
        r.gfsr = io->rd(io->ctx, MSM_SMMU_sGFSR);
        r.num_smr = msm_smmu_num_smr(r.idr0);
        r.num_cb = msm_smmu_num_cb(r.idr1);
        r.global_pages = msm_smmu_global_pages(r.idr1);

        uint64_t cb_end = (uint64_t)(r.global_pages + r.num_cb) * 0x1000ull;
        uint64_t s2cr_end = 0xc00ull + 4ull * (uint64_t)r.num_smr;
        if (r.num_smr > 0u && r.num_cb > 0u && r.global_pages <= 128u &&
            window >= cb_end && window >= s2cr_end) {
            uint32_t matched_mask = 0u;
            for (unsigned slot = 0; slot < r.num_smr; ++slot) {
                uint32_t smr = io->rd(io->ctx, MSM_SMMU_SMR(slot));
                if (!(smr & MSM_SMMU_SMR_VALID)) continue;
                uint32_t s2cr = io->rd(io->ctx, MSM_SMMU_S2CR(slot));
                uint32_t smr_mask = MSM_SMMU_SMR_MASK(smr);
                for (size_t s = 0; s < nstreams; ++s) {
                    uint32_t bit = (uint32_t)1u << (unsigned)s;
                    if (matched_mask & bit) continue; /* stream already found */
                    uint32_t ignore = (uint32_t)(smr_mask | (uint32_t)streams[s].mask);
                    uint32_t diff = (smr ^ (uint32_t)streams[s].id) & 0xffffu & ~ignore;
                    if (diff != 0u) continue;
                    matched_mask |= bit;
                    int first = (r.matched == 0u);
                    r.matched++;
                    if (first) {
                        r.first_stream = streams[s].id;
                        r.first_slot = slot;
                        r.first_smr = smr;
                    }
                    int physical = bank_is_bypass(io, window, r.global_pages,
                                                  r.num_cb, s2cr, &r, first);
                    if (physical) r.bypass++;
                    else r.translating++;
                }
            }
            if (r.matched == 0u) r.verdict = kMsmSmmuVerdictNotFound;
            else if (r.translating == 0u) r.verdict = kMsmSmmuVerdictBypass;
            else r.verdict = kMsmSmmuVerdictTranslate;
        }
    }

    *out = r;
    return 0;
}

int msm_smmu_survey_display(struct msm_smmu_io *io, uint64_t window,
                            struct msm_smmu_survey *out) {
    size_t count = 0;
    const struct msm_smmu_stream *streams = msm_smmu_display_streams(&count);
    return msm_smmu_survey(io, window, streams, count, out);
}

int msm_smmu_verdict_is_bypass(const struct msm_smmu_survey *s) {
    return s && s->verdict == kMsmSmmuVerdictBypass ? 1 : 0;
}

int msm_smmu_survey_pack(const struct msm_smmu_survey *s, int32_t *out10) {
    if (!s || !out10) return -1;
    out10[0] = (int32_t)kMsmSmmuStageSurveyed;       /* stage = surveyed */
    out10[1] = (int32_t)s->matched;                  /* matched streams */
    out10[2] = (int32_t)s->verdict;                  /* +1 bypass / 0 translate / -1 / -2 */
    out10[3] = (int32_t)(s->num_cb | (s->global_pages << 16));
    out10[4] = (int32_t)(s->bypass | (s->translating << 8));
    out10[5] = (int32_t)s->first_s2cr;
    out10[6] = (int32_t)s->first_sctlr;
    out10[7] = (int32_t)s->first_cbar;
    out10[8] = (int32_t)s->first_ttbr0_lo;
    out10[9] = (int32_t)s->first_stream;
    return 0;
}

int msm_smmu_survey_publish(const struct msm_smmu_survey *s, uint32_t polls,
                            volatile int32_t *stats) {
    int32_t packed[10];
    if (!s || !stats) return -1;
    if (msm_smmu_survey_pack(s, packed) != 0) return -1;
    /* Slot 1 is the 1 Hz poll count (mode 1 uses the same slot); the pack helper
     * fills it with matched streams, so overwrite it here for the live row. */
    for (unsigned i = 0; i < 10u; ++i) stats[i] = packed[i];
    stats[1] = (int32_t)polls;
    return 0;
}
