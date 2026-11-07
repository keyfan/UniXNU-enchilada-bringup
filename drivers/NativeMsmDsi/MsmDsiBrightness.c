// SPDX-License-Identifier: BSD-2-Clause-Patent
// enchilada stage-3 layer 3a: ONE DCS brightness transmit over the MSM DSI0 CMD DMA
// channel. See MsmDsiBrightness.h for the 32-bit DMA_BASE gate + cache-maintenance
// rationale and the two independent verifications they rest on.
//
// This file is pure C over the injected struct msm_dsi_io register bus and the
// caller-owned DMA buffer. It performs NO allocation, NO mapping, NO IOKit call.
// It is freestanding (no libc): the sibling pure-C units MsmDsiSmmu.c and
// DsiDcs.c do the same, because the arm64-apple-darwin target used by the CI
// archive build has no host libc headers available to these objects.

#include "MsmDsiBrightness.h"

/* No memset: this object is compiled freestanding for the kernel archive. */
static void zero_result(struct msm_dsi_brightness_result *r) {
    uint8_t *b = (uint8_t *)r;
    for (size_t i = 0; i < sizeof(*r); ++i) b[i] = 0u;
}

static int refuse(struct msm_dsi_brightness_result *out, int stage, int rc) {
    if (out) {
        zero_result(out);
        out->stage = stage;
        out->rc = rc;
    }
    return rc;
}

int msm_dsi_brightness_send(struct msm_dsi_io *io, uint8_t *dma_buf, size_t dma_cap,
                            uint64_t dma_phys,
                            void (*clean)(void *ctx, const void *va, size_t len),
                            void *clean_ctx, uint16_t brightness,
                            struct msm_dsi_brightness_result *out) {
    /* Prepare a defined result even on the earliest refusal. */
    if (out) zero_result(out);

    /* ---- 1. Validate the register bus and the DMA buffer. Nothing is written. ---- */
    if (!io || !io->rd || !io->wr || !dma_buf || dma_cap < MSM_DSI_CMD_DMA_BUF_MAX)
        return refuse(out, kMsmDsiBrightnessStageNoIo, kMsmDsiBrightnessStageNoIo);

    /* ---- 2. THE 32-bit REG_DSI_DMA_BASE GATE (先核验再上机). -------------------
     * dsi.xml <reg32 offset="0x00044" name="DMA_BASE">; dsi_host.c:2245 commits a
     * u32 dma_base. enchilada's display SMMU context bank is in BYPASS (SCTLR.M=0,
     * DEVICE-CONFIRMED by evidence/backlight209mode2.md), so the value fed to this
     * register is a CPU PHYSICAL address that passes through unchanged. A fresh
     * IOBufferMemoryDescriptor may land above 4 GiB on this kernel (NativeUfs.cpp:1197
     * warns of exactly that on the sibling SoC), while enchilada-facts/parsed.txt
     * lists the SDM845 carveouts below 4 GiB. Those two facts disagree, so the only
     * safe behaviour is to REFUSE a high address BEFORE touching any register: a
     * silently truncated high address would make the controller DMA the wrong bytes.
     * The mode-3 runtime probe owns the allocation and must keep retrying / fall
     * back to a low carveout until this gate passes. */
    if ((dma_phys >> 32) != 0u)
        return refuse(out, kMsmDsiBrightnessStageAddrTooHigh,
                      kMsmDsiBrightnessStageAddrTooHigh);

    const uint32_t dma_addr_lo = (uint32_t)(dma_phys & 0xffffffffu);

    /* ---- 3. Build the layer-1 DCS packet (SET_DISPLAY_BRIGHTNESS, large form). -- */
    struct dsi_dcs_packet pkt;
    {
        uint8_t *b = (uint8_t *)&pkt;
        for (size_t i = 0; i < sizeof(pkt); ++i) b[i] = 0u;
    }
    if (dsi_dcs_set_brightness_large(0u, brightness, &pkt) != 0)
        return refuse(out, kMsmDsiBrightnessStagePacket, kMsmDsiBrightnessStagePacket);

    /* ---- 4. Fill the MSM CMD DMA buffer (layer 2). ---------------------------- */
    const int len = msm_dsi_cmd_dma_fill(&pkt, dma_buf, dma_cap);
    if (len <= 0)
        return refuse(out, kMsmDsiBrightnessStagePacket, kMsmDsiBrightnessStagePacket);

    /* ---- 5. Cache maintenance BETWEEN fill and trigger. -----------------------
     * The CPU wrote dma_buf through a cached mapping; the DSI controller reads it
     * through the bypass SMMU. Dwc3Gadget.cpp:304-313 cleanDma() and
     * NativeUfs.cpp:1150-1155 both `dc civac` each 64-byte line then `dsb sy`.
     * The mode-3 runtime probe supplies a clean callback doing exactly that. A NULL
     * callback is accepted only for the host test (which models the cache as coherent). */
    if (clean) clean(clean_ctx, dma_buf, (size_t)len);

    /* ---- 6. Per-transfer register sequence (layer 2 primitives). ------------- */
    const uint32_t saved = msm_dsi_cmd_prepare(io);
    msm_dsi_cmd_commit(io, dma_addr_lo, (unsigned)len);
    const int rc = msm_dsi_cmd_wait_idle(io, 0u, 0u);
    msm_dsi_cmd_restore(io, saved);   /* ALWAYS, including on timeout */

    /* ---- 7. Publish the result. ----------------------------------------------- */
    if (out) {
        out->rc = rc;
        out->stage = (rc == kMsmDsiCmdOk) ? kMsmDsiBrightnessStageTx
                                          : kMsmDsiBrightnessStageDma;
        out->dma_len = (uint8_t)len;
        out->dma_addr_lo = dma_addr_lo;
        out->level = dsi_dcs_clamp_brightness(brightness);
    }
    return rc;
}

int msm_dsi_brightness_publish(const struct msm_dsi_brightness_result *r,
                               const struct msm_dsi_brightness_counters *c,
                               volatile int32_t *stats) {
    if (!r || !c || !stats) return -1;
    /* BootLog.c:749 "L st=%d req=%d lvl=%d a=%d b=%d xerr=%d irq=%x ea=%x eb=%x err=%d" */
    stats[0] = (int32_t)r->stage;                    /* st  */
    stats[1] = (int32_t)c->polls;                    /* req */
    stats[2] = (int32_t)r->level;                    /* lvl */
    stats[3] = (int32_t)r->dma_len;                  /* a   */
    stats[4] = (int32_t)r->dma_addr_lo;              /* b   */
    stats[5] = (int32_t)r->rc;                       /* xerr */
    stats[6] = (int32_t)c->sent;                     /* irq */
    stats[7] = (int32_t)c->addr_high;                /* ea  */
    stats[8] = (int32_t)c->dma_err;                  /* eb  */
    stats[9] = 0;                                    /* err */
    return 0;
}
