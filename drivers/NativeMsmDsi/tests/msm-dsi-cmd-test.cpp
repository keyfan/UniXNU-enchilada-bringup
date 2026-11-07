// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the MDSS DSI0 command-channel transmit path (stage-3 layer 2).
// It models the DSI controller registers with a fake MMIO bus (the GeniI2c
// pattern) and asserts, against the two independent sources recorded in
// MsmDsiCmd.h, the exact CMD DMA buffer bytes and the prepare/commit/poll/restore
// register sequence. No hardware or IOKit is involved.
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "DsiDcs.h"
#include "MsmDsiCmd.h"

struct FakeMsmDsi {
    uint32_t ctrl = 0x100u;                 // firmware-live CTRL (ENABLE + lanes already set)
    std::vector<std::pair<uint32_t, uint32_t>> writes;
    int wmb_calls = 0;
    int delay_calls = 0;
    int busy_remaining = 0;
    int busy_after_trigger = 3;             // STATUS0 stays busy for 3 polls, then clears
    uint32_t dma_base = 0, dma_len = 0;
    int trig_dma = 0;

    static uint32_t rd(void *ctx, uint32_t reg) {
        FakeMsmDsi *f = static_cast<FakeMsmDsi *>(ctx);
        if (reg == MSM_DSI_REG_CTRL) return f->ctrl;
        if (reg == MSM_DSI_REG_STATUS0) {
            if (f->busy_remaining > 0) { --f->busy_remaining; return MSM_DSI_STATUS0_CMD_DMA_BUSY; }
            return 0;
        }
        return 0;
    }
    static void wr(void *ctx, uint32_t reg, uint32_t value) {
        FakeMsmDsi *f = static_cast<FakeMsmDsi *>(ctx);
        f->writes.push_back(std::make_pair(reg, value));
        if (reg == MSM_DSI_REG_CTRL) f->ctrl = value;
        else if (reg == MSM_DSI_REG_DMA_BASE) f->dma_base = value;
        else if (reg == MSM_DSI_REG_DMA_LEN) f->dma_len = value;
        else if (reg == MSM_DSI_REG_TRIG_DMA) { ++f->trig_dma; f->busy_remaining = f->busy_after_trigger; }
    }
    static void delay_us(void *ctx, unsigned) { static_cast<FakeMsmDsi *>(ctx)->delay_calls++; }
    static void wmb(void *ctx) { static_cast<FakeMsmDsi *>(ctx)->wmb_calls++; }
};

static msm_dsi_io fresh(FakeMsmDsi &f) {
    msm_dsi_io io = {};
    io.ctx = &f;
    io.rd = FakeMsmDsi::rd;
    io.wr = FakeMsmDsi::wr;
    io.delay_us = FakeMsmDsi::delay_us;
    io.wmb = FakeMsmDsi::wmb;
    return io;
}

static void testLongWriteDmaBuffer() {
    // Layer 1: SET_DISPLAY_BRIGHTNESS large, b=1023, channel 0 -> 39 03 00 00 51 03 ff
    dsi_dcs_packet pkt;
    assert(dsi_dcs_set_brightness_large(0, 1023, &pkt) == 0);
    assert(pkt.header[0] == 0x39 && pkt.header[1] == 0x03 && pkt.header[2] == 0x00);
    assert(pkt.payload_length == 3 && pkt.size == 7);

    uint8_t buf[MSM_DSI_CMD_DMA_BUF_MAX];
    std::memset(buf, 0xa5, sizeof buf);
    const int len = msm_dsi_cmd_dma_fill(&pkt, buf, sizeof buf);
    assert(len == 8);
    // dsi_cmd_dma_add(): data[0]=WC low, [1]=WC high, [2]=DI, [3]=0x80|0x40, payload, 0xff pad
    const uint8_t want[8] = {0x03, 0x00, 0x39, 0xc0, 0x51, 0x03, 0xff, 0xff};
    assert(std::memcmp(buf, want, 8) == 0);
    assert(buf[7] == 0xff);                 // len 8, packet.size 7 -> one pad byte
}

static void testShortWriteDmaBuffer() {
    // Layer 1: WRITE_CONTROL_DISPLAY 0x53/0x20 -> short packet 15 53 20 00, size 4
    dsi_dcs_packet pkt;
    assert(dsi_dcs_write_control_display(0, 0x20, &pkt) == 0);
    assert(pkt.header[0] == 0x15 && pkt.header[1] == 0x53 && pkt.header[2] == 0x20);
    assert(pkt.payload_length == 0 && pkt.size == 4);

    uint8_t buf[MSM_DSI_CMD_DMA_BUF_MAX];
    std::memset(buf, 0xa5, sizeof buf);
    const int len = msm_dsi_cmd_dma_fill(&pkt, buf, sizeof buf);
    assert(len == 4);                       // a short packet is 4 bytes, NO 0xff padding
    const uint8_t want[4] = {0x53, 0x20, 0x15, 0x80};
    assert(std::memcmp(buf, want, 4) == 0);
    assert(buf[4] == 0xa5);                 // bytes beyond len untouched
}

static void testChannelAndBrightness() {
    // Channel 2, b=512 -> DI=0xb9, payload 51 02 00, DMA buf 03 00 b9 c0 51 02 00 ff
    dsi_dcs_packet pkt;
    assert(dsi_dcs_set_brightness_large(2, 512, &pkt) == 0);
    assert(pkt.header[0] == 0xb9);
    uint8_t buf[MSM_DSI_CMD_DMA_BUF_MAX] = {};
    const int len = msm_dsi_cmd_dma_fill(&pkt, buf, sizeof buf);
    assert(len == 8);
    const uint8_t want[8] = {0x03, 0x00, 0xb9, 0xc0, 0x51, 0x02, 0x00, 0xff};
    assert(std::memcmp(buf, want, 8) == 0);
}

static void testRefusals() {
    uint8_t buf[MSM_DSI_CMD_DMA_BUF_MAX];
    std::memset(buf, 0x5a, sizeof buf);
    assert(msm_dsi_cmd_dma_fill(nullptr, buf, sizeof buf) == -1);
    dsi_dcs_packet pkt;
    assert(dsi_dcs_set_brightness_large(0, 42, &pkt) == 0);
    assert(msm_dsi_cmd_dma_fill(&pkt, nullptr, sizeof buf) == -1);
    assert(msm_dsi_cmd_dma_fill(&pkt, buf, 3) == -1);   // cap smaller than the 8-byte len
    // Every refusal left buf untouched.
    for (unsigned i = 0; i < sizeof buf; ++i) assert(buf[i] == 0x5a);
    dsi_dcs_packet bad = pkt;
    bad.size = 99;                                       // inconsistent layer-1 packet
    assert(msm_dsi_cmd_dma_fill(&bad, buf, sizeof buf) == -1);
    for (unsigned i = 0; i < sizeof buf; ++i) assert(buf[i] == 0x5a);
}

static void testSendSequence() {
    dsi_dcs_packet pkt;
    assert(dsi_dcs_set_brightness_large(0, 1023, &pkt) == 0);
    FakeMsmDsi f;
    msm_dsi_io io = fresh(f);
    uint8_t dma[MSM_DSI_CMD_DMA_BUF_MAX] = {};
    const uint32_t saved = 0x100u;
    const uint32_t dma_addr = 0x9d400000u;   // a real-looking cont_splash DMA address

    const int rc = msm_dsi_cmd_send(&io, &pkt, dma, sizeof dma, dma_addr);
    assert(rc == kMsmDsiCmdOk);

    // Register write order: prepare CTRL, DMA_BASE, DMA_LEN, TRIG_DMA, restore CTRL.
    assert(f.writes.size() == 5);
    assert(f.writes[0].first == MSM_DSI_REG_CTRL);
    assert(f.writes[0].second == (saved | MSM_DSI_CTRL_CMD_MODE_EN | MSM_DSI_CTRL_ENABLE));
    assert(f.writes[1].first == MSM_DSI_REG_DMA_BASE && f.writes[1].second == dma_addr);
    assert(f.writes[2].first == MSM_DSI_REG_DMA_LEN && f.writes[2].second == 8u);
    assert(f.writes[3].first == MSM_DSI_REG_TRIG_DMA && f.writes[3].second == 1u);
    assert(f.writes[4].first == MSM_DSI_REG_CTRL && f.writes[4].second == saved);

    assert(f.trig_dma == 1);
    assert(f.wmb_calls == 1);                // barrier between trigger and first poll
    assert(f.delay_calls == 3);              // 3 busy polls before completion
    assert(f.dma_base == dma_addr && f.dma_len == 8u);
    // The DMA buffer was actually filled with the long-write bytes.
    const uint8_t want[8] = {0x03, 0x00, 0x39, 0xc0, 0x51, 0x03, 0xff, 0xff};
    assert(std::memcmp(dma, want, 8) == 0);
    // CTRL is restored even on success.
    assert(f.ctrl == saved);
}

static void testTimeoutRestores() {
    FakeMsmDsi f;
    f.busy_after_trigger = 100000;          // never clears within the poll budget
    msm_dsi_io io = fresh(f);
    dsi_dcs_packet pkt;
    assert(dsi_dcs_write_power_save(0, 0x00, &pkt) == 0);
    uint8_t dma[MSM_DSI_CMD_DMA_BUF_MAX] = {};
    const uint32_t saved = 0x100u;
    const int rc = msm_dsi_cmd_send(&io, &pkt, dma, sizeof dma, 0x9d400000u);
    assert(rc == kMsmDsiCmdErrTimeout);
    // Even on timeout the last write restores CTRL (never leave CMD_MODE_EN stuck).
    assert(!f.writes.empty());
    assert(f.writes.back().first == MSM_DSI_REG_CTRL && f.writes.back().second == saved);
    assert(f.ctrl == saved);
}

static void testConfigConstants() {
    // The cold-boot full-config reference values for the layer-3 runtime probe.
    assert(MSM_DSI0_CTRL_PHYS == 0x0ae94000u && MSM_DSI0_REG_SHIFT == 4u);
    assert(MSM_DSI_REG_DMA_BASE == 0x044u && MSM_DSI_REG_TRIG_DMA == 0x08cu);
    assert(MSM_DSI_CMD_DMA_CTRL_CMD_MODE == 0x14000000u);
    assert(MSM_DSI_TRIG_CTRL_CMD_MODE(0) == (0x80000000u | (1u << 12) | 4u));
    assert(MSM_DSI_TRIG_CTRL_CMD_MODE(2) == (0x80000000u | (1u << 12) | 4u | (2u << 8)));
    assert(MSM_DSI_CLK_CTRL_ENABLE_CLKS == 0x23fu);
    assert(MSM_DSI_CMD_CFG1_DCS == ((1u << 16) | (0x3cu << 8) | 0x2cu));
}

int main() {
    testLongWriteDmaBuffer();
    testShortWriteDmaBuffer();
    testChannelAndBrightness();
    testRefusals();
    testSendSequence();
    testTimeoutRestores();
    testConfigConstants();
    std::printf("PASS: enchilada MDSS DSI0 command-channel transport (7 test groups)\n");
    return 0;
}
