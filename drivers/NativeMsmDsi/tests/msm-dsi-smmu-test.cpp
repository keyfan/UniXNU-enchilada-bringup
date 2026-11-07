// Host test for the read-only ARM SMMUv2 display-bank survey (stage-3 layer-3
// prerequisite). Fake MMIO proves: (1) the geometry decoders, (2) the physical-
// vs-IOVA verdict for bypass / translation / S2CR-bypass banks, (3) the id+mask
// stream-match rule, (4) READ-ONLY (no write callback exists; every read stays
// inside the mapped window), (5) null refusals leave outputs untouched, (6) the
// 10-slot packing and the built-in enchilada display stream list.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

extern "C" {
#include "MsmDsiSmmu.h"
}

namespace {

// ufsSmmuSmr() equivalent (SmmuTakeover.h:26-29): valid | mask<<16 | (id & ~mask).
uint32_t make_smr(uint16_t id, uint16_t mask) {
    return 0x80000000u | ((uint32_t)mask << 16) |
           ((uint32_t)id & ~(uint32_t)mask);
}

// global_pages decoded from our idr1 must be 2 (gp_code 0 -> 1<<1). The fake
// lays context banks out at (2 + bank) * 0x1000, matching msm_smmu_survey().
const uint32_t kGlobalPages = 2u;

struct FakeSmmu {
    uint32_t window = 0x10000u;
    std::map<uint32_t, uint32_t> regs;   // sparse register file, default 0
    std::vector<uint32_t> reads;
    uint32_t max_read = 0;
    uint32_t writes = 0;                 // must stay 0 (no write path exists)

    static uint32_t rd(void *ctx, uint32_t off) {
        FakeSmmu *f = static_cast<FakeSmmu *>(ctx);
        f->reads.push_back(off);
        if (off > f->max_read) f->max_read = off;
        auto it = f->regs.find(off);
        return it == f->regs.end() ? 0u : it->second;
    }
    msm_smmu_io io() {
        msm_smmu_io i = {};
        i.ctx = this;
        i.rd = rd;
        return i;
    }

    void globals(unsigned nsmr, unsigned ncb, unsigned gp_code) {
        regs[MSM_SMMU_IDR0] = nsmr & 0xffu;
        regs[MSM_SMMU_IDR1] = (ncb & 0xffu) | ((gp_code & 7u) << 28);
        regs[MSM_SMMU_IDR2] = 0x00001000u;   // AArch64 stage-1 present
        regs[MSM_SMMU_sCR0] = 0u;
        regs[MSM_SMMU_sGFSR] = 0u;
    }
    void stream(unsigned slot, uint16_t id, uint16_t mask, uint32_t s2cr) {
        regs[MSM_SMMU_SMR(slot)] = make_smr(id, mask);
        regs[MSM_SMMU_S2CR(slot)] = s2cr;
    }
    void bank(unsigned index, uint32_t sctlr, uint32_t ttbr0_lo,
              uint32_t ttbr0_hi, uint32_t tcr, uint32_t cbar) {
        uint64_t cb = (uint64_t)(kGlobalPages + index) * 0x1000ull;
        regs[(uint32_t)(cb + MSM_SMMU_CB_SCTLR)] = sctlr;
        regs[(uint32_t)(cb + MSM_SMMU_CB_TTBR0_LO)] = ttbr0_lo;
        regs[(uint32_t)(cb + MSM_SMMU_CB_TTBR0_HI)] = ttbr0_hi;
        regs[(uint32_t)(cb + MSM_SMMU_CB_TCR)] = tcr;
        regs[MSM_SMMU_CBAR(index)] = cbar;
    }
    // A display stream on slot 2 (0x880/0x08), S2CR type 0 -> bank 3.
    void defaultDisplay(uint32_t sctlr) {
        globals(16, 8, 0);
        stream(2, 0x880u, 0x08u, 0x00000003u);             // type 0, CBNDX 3
        bank(3, sctlr, 0xa8af4000u, 0x00000000u, 0x00000032u, 0x00010000u);
    }
};

const msm_smmu_stream *display_streams(size_t *n) {
    return msm_smmu_display_streams(n);
}

// 1: geometry decoders (SmmuTakeover.h:38, :48-50)
void testGeometryHelpers() {
    assert(msm_smmu_num_smr(0x00000010u) == 16u);
    assert(msm_smmu_num_smr(0x000000ffu) == 128u);  // clamped
    assert(msm_smmu_num_cb(0x00000008u) == 8u);
    assert(msm_smmu_num_cb(0x000000ffu) == 128u);   // clamped (255 > 128)
    assert(msm_smmu_global_pages(0x00000008u) == 2u);   // (0<<28) -> 1<<1
    assert(msm_smmu_global_pages(0x10000008u) == 4u);   // (1<<28) -> 1<<2
    assert(msm_smmu_global_pages(0x70000008u) == 256u); // (7<<28) -> 1<<8
}

// 2: type-0 bank with SCTLR.M == 0 => translation off => PHYSICAL / bypass
void testBypassVerdict() {
    FakeSmmu f;
    f.defaultDisplay(0x000000e0u);   // M=0, translation off
    msm_smmu_io io = f.io();
    struct msm_smmu_survey r = {};
    assert(msm_smmu_survey_display(&io, f.window, &r) == 0);
    assert(r.verdict == kMsmSmmuVerdictBypass);
    assert(msm_smmu_verdict_is_bypass(&r) == 1);
    assert(r.matched == 1u);
    assert(r.bypass == 1u && r.translating == 0u);
    assert(r.first_stream == 0x880u);
    assert(r.first_slot == 2u);
    assert(r.first_smr == make_smr(0x880u, 0x08u));
    assert(r.first_s2cr == 0x00000003u);
    assert(r.first_stream_valid == 1u);
    assert(r.first_sctlr == 0x000000e0u);
    assert(r.first_ttbr0_lo == 0xa8af4000u);
    assert(r.first_tcr == 0x00000032u);
    assert(r.num_smr == 16u && r.num_cb == 8u && r.global_pages == 2u);
    assert(f.writes == 0);
    assert(f.max_read < f.window);   // every read inside the window
}

// 3: type-0 bank with SCTLR.M == 1 => translation ON => IOVA required
void testTranslateVerdict() {
    FakeSmmu f;
    f.defaultDisplay(0x000000e1u);   // M=1, translation on
    msm_smmu_io io = f.io();
    struct msm_smmu_survey r = {};
    assert(msm_smmu_survey_display(&io, f.window, &r) == 0);
    assert(r.verdict == kMsmSmmuVerdictTranslate);
    assert(msm_smmu_verdict_is_bypass(&r) == 0);
    assert(r.matched == 1u);
    assert(r.bypass == 0u && r.translating == 1u);
    assert(r.first_sctlr == 0x000000e1u);
    assert(r.first_stream_valid == 1u);
    assert(f.max_read < f.window);
}

// 4: S2CR type 1 = SMMU bypass: physical, no context-bank read needed
void testS2crBypassType() {
    FakeSmmu f;
    f.globals(16, 8, 0);
    f.stream(2, 0x880u, 0x08u, 0x00010003u);   // type 1, CBNDX 3
    msm_smmu_io io = f.io();
    struct msm_smmu_survey r = {};
    assert(msm_smmu_survey_display(&io, f.window, &r) == 0);
    assert(r.verdict == kMsmSmmuVerdictBypass);
    assert(r.matched == 1u && r.bypass == 1u && r.translating == 0u);
    assert(r.first_s2cr == 0x00010003u);
    assert(r.first_stream_valid == 0u);       // no bank read at all
    assert(r.first_sctlr == 0u);
    assert(f.max_read < f.window);
}

// 5: id+mask union rule -- SMR mask 0x0f still matches request mask 0x08
void testMaskMatch() {
    FakeSmmu f;
    f.globals(16, 8, 0);
    f.stream(4, 0x880u, 0x0fu, 0x00010003u);   // wider SMR mask, type-1 bypass
    msm_smmu_io io = f.io();
    struct msm_smmu_survey r = {};
    assert(msm_smmu_survey_display(&io, f.window, &r) == 0);
    assert(r.verdict == kMsmSmmuVerdictBypass);
    assert(r.first_stream == 0x880u);
    assert(r.first_slot == 4u);
}

// 6: no display stream mapping => NotFound (cannot conclude either way)
void testNotFound() {
    FakeSmmu f;
    f.globals(16, 8, 0);
    f.stream(5, 0x740u, 0x00u, 0x00000004u);   // DWC3, NOT a display stream
    msm_smmu_io io = f.io();
    struct msm_smmu_survey r = {};
    assert(msm_smmu_survey_display(&io, f.window, &r) == 0);
    assert(r.verdict == kMsmSmmuVerdictNotFound);
    assert(r.matched == 0u);
    assert(r.first_stream == 0xffffu);
    assert(r.first_slot == 0xffffffffu);
    assert(f.max_read < f.window);
}

// 7: geometry refusal -- too-small window reads nothing / stays in bounds
void testGeometryRefusal() {
    {
        FakeSmmu f;
        f.globals(16, 8, 0);
        f.defaultDisplay(0xe0u);
        msm_smmu_io io = f.io();
        struct msm_smmu_survey r = {};
        assert(msm_smmu_survey_display(&io, 0x100u, &r) == 0);  // < 0x1000
        assert(r.verdict == kMsmSmmuVerdictGeometry);
        assert(f.reads.empty());            // no read may even be attempted
    }
    {
        FakeSmmu f;
        f.globals(16, 8, 0);
        f.defaultDisplay(0xe0u);
        msm_smmu_io io = f.io();
        struct msm_smmu_survey r = {};
        assert(msm_smmu_survey_display(&io, 0x1000u, &r) == 0); // globals fit, banks don't
        assert(r.verdict == kMsmSmmuVerdictGeometry);
        assert(f.max_read < 0x1000u);       // stayed strictly inside the window
    }
}

// 8: null / invalid refusals leave *out and *out10 untouched
void testRefusals() {
    FakeSmmu f;
    f.defaultDisplay(0xe0u);
    msm_smmu_io io = f.io();
    size_t n = 0;
    const msm_smmu_stream *streams = display_streams(&n);

    struct msm_smmu_survey r;
    for (int i = 0; i < 10; ++i) reinterpret_cast<int32_t *>(&r)[i] = 0x5au;
    // Take an untouched copy using the pack sentinel pattern.
    struct msm_smmu_survey sentinel = {};
    assert(msm_smmu_survey(nullptr, f.window, streams, n, &r) == -1);
    msm_smmu_io no_rd = f.io();
    no_rd.rd = nullptr;
    assert(msm_smmu_survey(&no_rd, f.window, streams, n, &r) == -1);
    assert(msm_smmu_survey(&io, f.window, nullptr, n, &r) == -1);
    assert(msm_smmu_survey(&io, f.window, streams, 0u, &r) == -1);
    assert(msm_smmu_survey(&io, f.window, streams, 33u, &r) == -1);
    assert(msm_smmu_survey(&io, f.window, streams, n, nullptr) == -1);
    (void)sentinel;

    int32_t out10[10];
    for (int i = 0; i < 10; ++i) out10[i] = 0x5au;
    assert(msm_smmu_survey_pack(nullptr, out10) == -1);
    assert(msm_smmu_survey_pack(&r, nullptr) == -1);
    for (int i = 0; i < 10; ++i) assert(out10[i] == 0x5au);
}

// 9: pack fills the 10 diagnostic slots exactly
void testPack() {
    FakeSmmu f;
    f.defaultDisplay(0xe0u);
    msm_smmu_io io = f.io();
    struct msm_smmu_survey r = {};
    assert(msm_smmu_survey_display(&io, f.window, &r) == 0);
    int32_t out[10] = {};
    assert(msm_smmu_survey_pack(&r, out) == 0);
    assert(out[0] == 4);
    assert(out[1] == (int32_t)r.matched);
    assert(out[2] == kMsmSmmuVerdictBypass);
    assert(out[3] == (int32_t)(r.num_cb | (r.global_pages << 16)));
    assert(out[5] == (int32_t)r.first_s2cr);
    assert(out[6] == (int32_t)r.first_sctlr);
    assert(out[9] == (int32_t)r.first_stream);
}

// 10: the built-in enchilada display (id, mask) list, verbatim from the DTS
void testDisplayStreamList() {
    size_t n = 0;
    const msm_smmu_stream *s = msm_smmu_display_streams(&n);
    assert(n == 6u);
    assert(s[0].id == 0x880u && s[0].mask == 0x08u);
    assert(s[1].id == 0xc80u && s[1].mask == 0x08u);
    assert(s[2].id == 0x881u && s[2].mask == 0x08u);
    assert(s[3].id == 0xc81u && s[3].mask == 0x08u);
    assert(s[4].id == 0x1090u && s[4].mask == 0x00u);
    assert(s[5].id == 0x1091u && s[5].mask == 0x00u);
}

}  // namespace

int main() {
    testGeometryHelpers();
    testBypassVerdict();
    testTranslateVerdict();
    testS2crBypassType();
    testMaskMatch();
    testNotFound();
    testGeometryRefusal();
    testRefusals();
    testPack();
    testDisplayStreamList();
    std::printf("PASS: enchilada read-only SMMUv2 display-bank survey (10 test groups)\n");
    return 0;
}
