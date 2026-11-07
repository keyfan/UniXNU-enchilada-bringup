// Host test for the read-only DSI0 runtime session core (stage-3 layer 2.5).
// Fake MMIO proves: (1) every snapshot read hits the right dsi.xml offset exactly once,
// (2) NO register is ever written, (3) publish fills the L-row stats[0..9] exactly.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" {
#include "MsmDsiSession.h"
}

namespace {

struct FakeDsi {
    // Known register values: what a live, firmware-initialised DSI0 in command mode
    // would plausibly read back (CTRL ENABLE|CMD_MODE_EN|4 lanes|CLK_EN, clocks on,
    // FROM_FRAME_BUFFER|LOW_POWER, TRIGGER_SW). Arbitrary but distinct per register.
    uint32_t ctrl         = 0x000001f3u;
    uint32_t status0      = 0x00000000u;
    uint32_t cmd_dma_ctrl = 0x14000000u;
    uint32_t cmd_cfg0     = 0x00000008u;
    uint32_t cmd_cfg1     = 0x00013c2cu;
    uint32_t trig_ctrl    = 0x80001004u;
    uint32_t clk_ctrl     = 0x0000023fu;

    std::vector<uint32_t> reads;   // register offsets in read order
    uint32_t writes = 0;           // must stay 0

    static uint32_t rd(void *ctx, uint32_t reg) {
        FakeDsi *f = static_cast<FakeDsi *>(ctx);
        f->reads.push_back(reg);
        switch (reg) {
            case MSM_DSI_REG_CTRL:         return f->ctrl;
            case MSM_DSI_REG_STATUS0:      return f->status0;
            case MSM_DSI_REG_CMD_DMA_CTRL: return f->cmd_dma_ctrl;
            case MSM_DSI_REG_CMD_CFG0:     return f->cmd_cfg0;
            case MSM_DSI_REG_CMD_CFG1:     return f->cmd_cfg1;
            case MSM_DSI_REG_TRIG_CTRL:    return f->trig_ctrl;
            case MSM_DSI_REG_CLK_CTRL:     return f->clk_ctrl;
            default:                       return 0xdeadbeefu;
        }
    }
    static void wr(void *ctx, uint32_t, uint32_t) {
        static_cast<FakeDsi *>(ctx)->writes++;
    }
    static void delay_us(void *, unsigned) {}
    static void wmb(void *) {}

    msm_dsi_io io() {
        msm_dsi_io i = {};
        i.ctx = this;
        i.rd = rd;
        i.wr = wr;
        i.delay_us = delay_us;
        i.wmb = wmb;
        return i;
    }
};

void testSnapshotReads() {
    FakeDsi f;
    msm_dsi_io io = f.io();
    struct msm_dsi_snapshot s = {};
    assert(msm_dsi_session_snapshot(&io, &s) == 0);
    assert(s.ctrl         == f.ctrl);
    assert(s.status0      == f.status0);
    assert(s.cmd_dma_ctrl == f.cmd_dma_ctrl);
    assert(s.cmd_cfg0     == f.cmd_cfg0);
    assert(s.cmd_cfg1     == f.cmd_cfg1);
    assert(s.trig_ctrl    == f.trig_ctrl);
    assert(s.clk_ctrl     == f.clk_ctrl);
    // Each of the seven registers is read exactly once, in the documented order.
    const uint32_t order[] = {
        MSM_DSI_REG_CTRL, MSM_DSI_REG_STATUS0, MSM_DSI_REG_CMD_DMA_CTRL,
        MSM_DSI_REG_CMD_CFG0, MSM_DSI_REG_CMD_CFG1, MSM_DSI_REG_TRIG_CTRL,
        MSM_DSI_REG_CLK_CTRL,
    };
    assert(f.reads.size() == 7);
    for (size_t i = 0; i < 7; ++i) assert(f.reads[i] == order[i]);
    // READ-ONLY: the session must never write a controller register.
    assert(f.writes == 0);
}

void testRefusals() {
    FakeDsi f;
    msm_dsi_io io = f.io();
    struct msm_dsi_snapshot s = {};
    assert(msm_dsi_session_snapshot(nullptr, &s) == -1);
    assert(msm_dsi_session_snapshot(&io, nullptr) == -1);
    msm_dsi_io no_rd = f.io();
    no_rd.rd = nullptr;
    assert(msm_dsi_session_snapshot(&no_rd, &s) == -1);
    // publish refusals leave stats untouched
    volatile int32_t stats[10];
    for (int i = 0; i < 10; ++i) stats[i] = 0x5au;
    assert(msm_dsi_session_publish(nullptr, 0, stats) == -1);
    assert(msm_dsi_session_publish(&s, 0, nullptr) == -1);
    for (int i = 0; i < 10; ++i) assert(stats[i] == 0x5au);
}

void testPublish() {
    FakeDsi f;
    msm_dsi_io io = f.io();
    struct msm_dsi_snapshot s = {};
    assert(msm_dsi_session_snapshot(&io, &s) == 0);
    volatile int32_t stats[10] = {};
    assert(msm_dsi_session_publish(&s, 7, stats) == 0);
    assert(stats[0] == kMsmDsiSessionStageMapped);   // st=3
    assert(stats[1] == 7);                            // req=7 polls
    assert(stats[2] == 0);                            // lvl=0 (no packet sent)
    assert(stats[3] == (int32_t)f.ctrl);
    assert(stats[4] == (int32_t)f.status0);
    assert(stats[5] == (int32_t)f.cmd_dma_ctrl);
    assert(stats[6] == (int32_t)f.trig_ctrl);
    assert(stats[7] == (int32_t)f.cmd_cfg0);
    assert(stats[8] == (int32_t)f.cmd_cfg1);
    assert(stats[9] == (int32_t)f.clk_ctrl);
}

}  // namespace

int main() {
    testSnapshotReads();
    testRefusals();
    testPublish();
    std::printf("PASS: enchilada MDSS DSI0 read-only runtime session (3 test groups)\n");
    return 0;
}
