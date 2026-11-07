// SPDX-License-Identifier: BSD-2-Clause-Patent
// MDSS DSI0 command-channel transmit path (stage-3 layer 2). See MsmDsiCmd.h for
// the two independent sources (vendor DTS + Linux v6.12) and why the runtime MMIO
// window is 0xae94004 rather than 0xae94000.
#include "MsmDsiCmd.h"

/* mipi_dsi_packet_format_is_long(msg->type) is decided by the data type, which is
 * exactly header[0] & 0x3f (mipi_dsi_create_packet puts ((channel&3)<<6)|type there).
 * Layer 1 emits only DCS 0x05/0x15 (short) and 0x39 (long); 0x29 is the generic
 * long write, listed for completeness of the MSM buffer format. */
static int msm_dsi_cmd_is_long(const struct dsi_dcs_packet *pkt) {
    uint8_t type = (uint8_t)(pkt->header[0] & 0x3fu);
    return type == kDsiDcsLongWrite || type == 0x29u;
}

int msm_dsi_cmd_dma_fill(const struct dsi_dcs_packet *pkt, uint8_t *buf, size_t cap) {
    if (!pkt || !buf) return -1;
    if (pkt->payload_length > kDsiDcsPayloadBytes) return -1;

    const int is_long = msm_dsi_cmd_is_long(pkt);
    const size_t payload_len = is_long ? (size_t)pkt->payload_length : 0u;
    const size_t size = 4u + payload_len;              /* packet.size from layer 1 */
    if (pkt->size != (uint16_t)size) return -1;         /* inconsistent layer-1 packet */

    /* dsi_host.c: len = (packet.size + 3) & ~3 */
    const size_t len = (size + 3u) & ~((size_t)3u);
    if (len > cap || len > MSM_DSI_CMD_DMA_BUF_MAX) return -1;

    /* All validation is done; nothing above touched buf, so a refusal leaves it intact. */
    buf[0] = pkt->header[1];                            /* long: WC low / short: param 1 */
    buf[1] = pkt->header[2];                            /* long: WC high / short: param 2 */
    buf[2] = pkt->header[0];                            /* DI = ((channel&3)<<6)|(type&0x3f) */
    buf[3] = 0x80u;                                     /* BIT(7) Last packet */
    if (is_long) buf[3] |= 0x40u;                       /* BIT(6) long */
    /* BIT(5) would mean a read/BTA request; layer 1 emits writes only, so it stays 0. */

    for (size_t i = 0; i < payload_len; ++i) buf[4u + i] = pkt->payload[i];
    for (size_t i = size; i < len; ++i) buf[i] = 0xffu;/* 0xff pad to 4-byte multiple */

    return (int)len;
}

uint32_t msm_dsi_cmd_prepare(struct msm_dsi_io *io) {
    /* msm_dsi_host_xfer_prepare(): save CTRL, then set CMD_MODE_EN | ENABLE.
     * We poll STATUS0 for completion instead of arming the CMD_DMA_DONE interrupt
     * (this pure-C unit has no IRQ handler); the firmware's controller is already
     * in command mode, so only these two bits need to be reasserted. */
    const uint32_t saved = io->rd(io->ctx, MSM_DSI_REG_CTRL);
    io->wr(io->ctx, MSM_DSI_REG_CTRL, saved | MSM_DSI_CTRL_CMD_MODE_EN | MSM_DSI_CTRL_ENABLE);
    return saved;
}

void msm_dsi_cmd_commit(struct msm_dsi_io *io, uint32_t dma_addr, unsigned len) {
    /* msm_dsi_host_cmd_xfer_commit(), verbatim order. The wmb() (dsb on arm64) makes
     * the posted TRIG_DMA write visible before the first STATUS0 read in the poll. */
    io->wr(io->ctx, MSM_DSI_REG_DMA_BASE, dma_addr);
    io->wr(io->ctx, MSM_DSI_REG_DMA_LEN, len);
    io->wr(io->ctx, MSM_DSI_REG_TRIG_DMA, 1u);
    if (io->wmb) io->wmb(io->ctx);
}

int msm_dsi_cmd_wait_idle(struct msm_dsi_io *io, unsigned max_polls, unsigned poll_us) {
    if (!max_polls) max_polls = MSM_DSI_CMD_POLLS;
    if (!poll_us) poll_us = MSM_DSI_CMD_POLL_US;
    /* dsi_host.c waits on the CMD_DMA_DONE completion; here we spin on the level
     * status STATUS0.CMD_MODE_DMA_BUSY. delay_us MUST be a CPU spin, NOT an XNU
     * IODelay/WFE wait: RootCommands.h:22 records that on enchilada a WFE-based
     * poll never returned. */
    for (unsigned i = 0; i < max_polls; ++i) {
        if (!(io->rd(io->ctx, MSM_DSI_REG_STATUS0) & MSM_DSI_STATUS0_CMD_DMA_BUSY))
            return kMsmDsiCmdOk;
        if (io->delay_us) io->delay_us(io->ctx, poll_us);
    }
    return kMsmDsiCmdErrTimeout;
}

void msm_dsi_cmd_restore(struct msm_dsi_io *io, uint32_t saved) {
    /* msm_dsi_host_xfer_restore(): put CTRL back. We never armed an interrupt, so
     * there is no dsi_intr_ctrl() counterpart here. */
    io->wr(io->ctx, MSM_DSI_REG_CTRL, saved);
}

int msm_dsi_cmd_send(struct msm_dsi_io *io, const struct dsi_dcs_packet *pkt,
                     uint8_t *dma_buf, size_t dma_cap, uint32_t dma_addr) {
    const int len = msm_dsi_cmd_dma_fill(pkt, dma_buf, dma_cap);
    if (len < 0) return kMsmDsiCmdErrPacket;

    const uint32_t saved = msm_dsi_cmd_prepare(io);
    msm_dsi_cmd_commit(io, dma_addr, (unsigned)len);
    const int rc = msm_dsi_cmd_wait_idle(io, 0, 0);
    msm_dsi_cmd_restore(io, saved);   /* also on timeout: never leave CMD_MODE_EN stuck */
    return rc;
}
