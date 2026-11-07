// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the enchilada stage-3 layer 3a brightness-transmit orchestrator.
// It models the DSI0 registers with a fake MMIO bus (the GeniI2c / MsmDsiCmd
// pattern) and asserts, against the two independent sources recorded in
// MsmDsiBrightness.h, the 32-bit DMA_BASE gate, the cache-clean-before-trigger
// ordering, the exact DCS long-write DMA bytes, the register sequence, and the
// native_backlight_stats[10] L-row layout. No hardware or IOKit is involved.
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "DsiDcs.h"
#include "MsmDsiCmd.h"
#include "MsmDsiBrightness.h"

/* Event codes for an ordered timeline. Register writes are recorded by their dsi.xml
 * offset; the two non-register events get private high codes. */
enum {
    kEvClean = 0x1000,
    kEvWmb   = 0x1001,
};

struct FakeBrightness {
    uint32_t ctrl = 0x100u;                 // firmware-live CTRL (ENABLE + lanes already set)
    std::vector<int> timeline;
    std::vector<std::pair<uint32_t, uint32_t>> writes;
    int busy_remaining = 0;
    int busy_after_trigger = 3;             // STATUS0 stays busy for 3 polls, then clears
    int clean_calls = 0;
    const void *clean_va = nullptr;
    size_t clean_len = 0;
    uint32_t dma_base = 0, dma_len = 0;
    int trig_dma = 0;

    static uint32_t rd(void *ctx, uint32_t reg) {
        FakeBrightness *f = static_cast<FakeBrightness *>(ctx);
        if (reg == MSM_DSI_REG_CTRL) return f->ctrl;
        if (reg == MSM_DSI_REG_STATUS0) {
            if (f->busy_remaining > 0) { --f->busy_remaining; return MSM_DSI_STATUS0_CMD_DMA_BUSY; }
            return 0;
        }
        return 0;
    }
    static void wr(void *ctx, uint32_t reg, uint32_t value) {
        FakeBrightness *f = static_cast<FakeBrightness *>(ctx);
        f->timeline.push_back((int)reg);
        f->writes.push_back(std::make_pair(reg, value));
        if (reg == MSM_DSI_REG_CTRL) f->ctrl = value;
        else if (reg == MSM_DSI_REG_DMA_BASE) f->dma_base = value;
        else if (reg == MSM_DSI_REG_DMA_LEN) f->dma_len = value;
        else if (reg == MSM_DSI_REG_TRIG_DMA) { ++f->trig_dma; f->busy_remaining = f->busy_after_trigger; }
    }
    static void delay_us(void *, unsigned) {}
    static void wmb(void *ctx) { static_cast<FakeBrightness *>(ctx)->timeline.push_back(kEvWmb); }
    static void clean(void *ctx, const void *va, size_t len) {
        FakeBrightness *f = static_cast<FakeBrightness *>(ctx);
        f->timeline.push_back(kEvClean);
        ++f->clean_calls;
        f->clean_va = va;
        f->clean_len = len;
    }
};

static msm_dsi_io fresh(FakeBrightness &f) {
    msm_dsi_io io = {};
    io.ctx = &f;
    io.rd = FakeBrightness::rd;
    io.wr = FakeBrightness::wr;
    io.delay_us = FakeBrightness::delay_us;
    io.wmb = FakeBrightness::wmb;
    return io;
}

/* A physically-low, 4 KiB-aligned DMA address like the real cont_splash carveout. */
static const uint64_t kLowPhys = 0x9d400000ull;

static void testSuccessLowAddress() {
    FakeBrightness f;
    msm_dsi_io io = fresh(f);
    uint8_t dma[MSM_DSI_CMD_DMA_BUF_MAX];
    std::memset(dma, 0xa5, sizeof dma);
    msm_dsi_brightness_result r;

    const int rc = msm_dsi_brightness_send(&io, dma, sizeof dma, kLowPhys,
                                           FakeBrightness::clean, &f, 1023, &r);
    assert(rc == kMsmDsiCmdOk);
    assert(r.stage == kMsmDsiBrightnessStageTx);
    assert(r.rc == kMsmDsiCmdOk);
    assert(r.dma_len == 8u);
    assert(r.dma_addr_lo == (uint32_t)kLowPhys);
    assert(r.level == 1023);

    /* Exact layer-1 + layer-2 bytes: DCS long write 0x51 {03,ff} -> MSM DMA buffer. */
    const uint8_t want[8] = {0x03, 0x00, 0x39, 0xc0, 0x51, 0x03, 0xff, 0xff};
    assert(std::memcmp(dma, want, 8) == 0);
    assert(dma[7] == 0xff);                 // one 0xff pad byte (packet.size 7 -> len 8)

    /* The register sequence: prepare CTRL, DMA_BASE, DMA_LEN, TRIG_DMA, restore CTRL. */
    assert(f.writes.size() == 5);
    const uint32_t saved = 0x100u;
    assert(f.writes[0].first == MSM_DSI_REG_CTRL);
    assert(f.writes[0].second == (saved | MSM_DSI_CTRL_CMD_MODE_EN | MSM_DSI_CTRL_ENABLE));
    assert(f.writes[1].first == MSM_DSI_REG_DMA_BASE && f.writes[1].second == (uint32_t)kLowPhys);
    assert(f.writes[2].first == MSM_DSI_REG_DMA_LEN && f.writes[2].second == 8u);
    assert(f.writes[3].first == MSM_DSI_REG_TRIG_DMA && f.writes[3].second == 1u);
    assert(f.writes[4].first == MSM_DSI_REG_CTRL && f.writes[4].second == saved);
    assert(f.ctrl == saved);                // restored even on success
    assert(f.trig_dma == 1);
    assert(f.dma_base == (uint32_t)kLowPhys && f.dma_len == 8u);

    /* Cache maintenance: exactly once, over the 8-byte buffer, BEFORE the trigger. */
    assert(f.clean_calls == 1);
    assert(f.clean_va == dma && f.clean_len == 8u);
    int clean_pos = -1, trig_pos = -1;
    for (size_t i = 0; i < f.timeline.size(); ++i) {
        if (f.timeline[i] == kEvClean) clean_pos = (int)i;
        if (f.timeline[i] == (int)MSM_DSI_REG_TRIG_DMA) trig_pos = (int)i;
    }
    assert(clean_pos >= 0 && trig_pos >= 0 && clean_pos < trig_pos);
    assert(f.timeline.back() == (int)MSM_DSI_REG_CTRL);   // last event = restore
}

static void test32BitGateRefusesBeforeAnyWrite() {
    FakeBrightness f;
    msm_dsi_io io = fresh(f);
    uint8_t dma[MSM_DSI_CMD_DMA_BUF_MAX];
    std::memset(dma, 0x5a, sizeof dma);
    msm_dsi_brightness_result r;

    /* A DMA address above 4 GiB (the kernel-allocation case NativeUfs.cpp:1197 warns
     * about). REG_DSI_DMA_BASE is 32-bit, so feeding a truncated high address would
     * make the controller read the wrong bytes. Refuse BEFORE any register/clean. */
    const int rc = msm_dsi_brightness_send(&io, dma, sizeof dma, 0x1'0000'0000ull,
                                           FakeBrightness::clean, &f, 512, &r);
    assert(rc == kMsmDsiBrightnessStageAddrTooHigh);
    assert(r.stage == kMsmDsiBrightnessStageAddrTooHigh);
    assert(r.rc == kMsmDsiBrightnessStageAddrTooHigh);
    assert(r.dma_addr_lo == 0u && r.dma_len == 0u && r.level == 0u);  // zeroed result
    assert(f.writes.empty());               // NO register was touched
    assert(f.clean_calls == 0);             // NO cache maintenance
    assert(f.timeline.empty());
    for (unsigned i = 0; i < sizeof dma; ++i) assert(dma[i] == 0x5a);  // buffer untouched

    /* An address with only high dword bits set but low bits present also refuses. */
    assert(msm_dsi_brightness_send(&io, dma, sizeof dma, 0x2'9d40'0000ull,
                                   FakeBrightness::clean, &f, 512, nullptr)
           == kMsmDsiBrightnessStageAddrTooHigh);
    assert(f.writes.empty() && f.clean_calls == 0);
    for (unsigned i = 0; i < sizeof dma; ++i) assert(dma[i] == 0x5a);

    /* A fully-32-bit address (all high bits zero) is accepted. */
    assert(msm_dsi_brightness_send(&io, dma, sizeof dma, 0xffffffffull,
                                   FakeBrightness::clean, &f, 512, nullptr)
           == kMsmDsiCmdOk);
}

static void testNoIoRefusals() {
    FakeBrightness f;
    msm_dsi_io io = fresh(f);
    uint8_t dma[MSM_DSI_CMD_DMA_BUF_MAX] = {};
    msm_dsi_brightness_result r;

    assert(msm_dsi_brightness_send(nullptr, dma, sizeof dma, kLowPhys,
                                   FakeBrightness::clean, &f, 100, &r)
           == kMsmDsiBrightnessStageNoIo);
    assert(r.stage == kMsmDsiBrightnessStageNoIo);
    assert(f.writes.empty() && f.clean_calls == 0);

    assert(msm_dsi_brightness_send(&io, nullptr, sizeof dma, kLowPhys,
                                   FakeBrightness::clean, &f, 100, nullptr)
           == kMsmDsiBrightnessStageNoIo);
    /* A too-small DMA buffer capacity must be refused before any register write. */
    assert(msm_dsi_brightness_send(&io, dma, 4u, kLowPhys,
                                   FakeBrightness::clean, &f, 100, nullptr)
           == kMsmDsiBrightnessStageNoIo);
    assert(f.writes.empty() && f.clean_calls == 0);

    /* A missing rd/wr callback is also NoIo. */
    msm_dsi_io partial = io;
    partial.wr = nullptr;
    assert(msm_dsi_brightness_send(&partial, dma, sizeof dma, kLowPhys,
                                   FakeBrightness::clean, &f, 100, nullptr)
           == kMsmDsiBrightnessStageNoIo);
    assert(f.writes.empty());
}

static void testBrightnessClampAndBytes() {
    FakeBrightness f;
    msm_dsi_io io = fresh(f);
    uint8_t dma[MSM_DSI_CMD_DMA_BUF_MAX] = {};
    msm_dsi_brightness_result r;

    /* 0 clamps to the panel min 1 -> payload {0x00,0x01}. */
    assert(msm_dsi_brightness_send(&io, dma, sizeof dma, kLowPhys,
                                   FakeBrightness::clean, &f, 0, &r) == kMsmDsiCmdOk);
    assert(r.level == kEnchiladaBrightnessMin);
    const uint8_t want1[8] = {0x03, 0x00, 0x39, 0xc0, 0x51, 0x00, 0x01, 0xff};
    assert(std::memcmp(dma, want1, 8) == 0);

    /* 5000 clamps to the panel max 1023 -> payload {0x03,0xff}. */
    assert(msm_dsi_brightness_send(&io, dma, sizeof dma, kLowPhys,
                                   FakeBrightness::clean, &f, 5000, &r) == kMsmDsiCmdOk);
    assert(r.level == kEnchiladaBrightnessMax);
    assert(dma[5] == 0x03 && dma[6] == 0xff);
}

static void testTimeoutRestoresAndStage() {
    FakeBrightness f;
    f.busy_after_trigger = 100000;          // never clears within the poll budget
    msm_dsi_io io = fresh(f);
    uint8_t dma[MSM_DSI_CMD_DMA_BUF_MAX] = {};
    msm_dsi_brightness_result r;

    const int rc = msm_dsi_brightness_send(&io, dma, sizeof dma, kLowPhys,
                                           FakeBrightness::clean, &f, 800, &r);
    assert(rc == kMsmDsiCmdErrTimeout);
    assert(r.stage == kMsmDsiBrightnessStageDma);
    assert(r.rc == kMsmDsiCmdErrTimeout);
    assert(f.clean_calls == 1);             // clean still ran (buffer was filled)
    const uint32_t saved = 0x100u;
    assert(!f.writes.empty());
    assert(f.writes.back().first == MSM_DSI_REG_CTRL && f.writes.back().second == saved);
    assert(f.ctrl == saved);                // CTRL restored even on timeout
}

static void testNullCleanAccepted() {
    /* NULL clean is allowed for host tests only (no cache modelling). The device
     * runtime probe MUST supply a real dc civac + dsb sy callback. */
    FakeBrightness f;
    msm_dsi_io io = fresh(f);
    uint8_t dma[MSM_DSI_CMD_DMA_BUF_MAX] = {};
    assert(msm_dsi_brightness_send(&io, dma, sizeof dma, kLowPhys,
                                   nullptr, nullptr, 400, nullptr) == kMsmDsiCmdOk);
    assert(f.clean_calls == 0);
    assert(f.trig_dma == 1);
}

static void testPublish() {
    msm_dsi_brightness_result r = {};
    r.rc = kMsmDsiCmdOk;
    r.stage = kMsmDsiBrightnessStageTx;
    r.dma_len = 8;
    r.dma_addr_lo = 0x9d400000u;
    r.level = 1023;
    msm_dsi_brightness_counters c = {};
    c.polls = 0x1234;
    c.sent = 7;
    c.addr_high = 2;
    c.dma_err = 3;

    volatile int32_t stats[10];
    for (int i = 0; i < 10; ++i) stats[i] = -9;
    assert(msm_dsi_brightness_publish(&r, &c, stats) == 0);
    assert(stats[0] == kMsmDsiBrightnessStageTx);   // st
    assert(stats[1] == 0x1234);                      // req = polls
    assert(stats[2] == 1023);                        // lvl
    assert(stats[3] == 8);                           // a = dma_len
    assert(stats[4] == (int32_t)0x9d400000u);        // b = dma_addr_lo
    assert(stats[5] == kMsmDsiCmdOk);                // xerr = rc
    assert(stats[6] == 7);                           // irq = sent
    assert(stats[7] == 2);                           // ea = addr_high
    assert(stats[8] == 3);                           // eb = dma_err
    assert(stats[9] == 0);                           // err reserved

    /* Null arguments refuse and leave the row untouched. */
    for (int i = 0; i < 10; ++i) stats[i] = -9;
    assert(msm_dsi_brightness_publish(nullptr, &c, stats) == -1);
    assert(msm_dsi_brightness_publish(&r, nullptr, stats) == -1);
    assert(msm_dsi_brightness_publish(&r, &c, nullptr) == -1);
    for (int i = 0; i < 10; ++i) assert(stats[i] == -9);
}

int main() {
    testSuccessLowAddress();
    test32BitGateRefusesBeforeAnyWrite();
    testNoIoRefusals();
    testBrightnessClampAndBytes();
    testTimeoutRestoresAndStage();
    testNullCleanAccepted();
    testPublish();
    std::printf("PASS: enchilada brightness transmit (32-bit gate, clean, DCS bytes, publish) (7 test groups)\n");
    return 0;
}
