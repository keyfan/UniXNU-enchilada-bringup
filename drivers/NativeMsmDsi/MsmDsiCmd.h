// SPDX-License-Identifier: BSD-2-Clause-Patent
// MDSS DSI0 command-channel transmit path for the OnePlus 6 (enchilada, SDM845).
// This is stage-3 layer 2: it turns a stage-3 layer-1 DCS packet
// (drivers/NativeDsiBacklight/DsiDcs.h) into the MSM-specific CMD DMA buffer and
// emits the software-trigger register sequence that pushes it out over DSI0.
//
// Authoritative sources, checked against each other (two independent verifications):
//   * Vendor DTS: Mu-Silicium/Resources/DTBs/enchilada.dts:3404
//       qcom,mdss_dsi_ctrl0@ae94000, compatible "qcom,dsi-ctrl-hw-v2.2",
//       reg = <0xae94000 0x400>, qcom,mdss-dsi-dma-trigger = "trigger_sw" (:2635),
//       panel-type "dsi_cmd_mode" (:2619), 4 lanes, virtual-channel-id 0.
//   * Linux v6.12 drivers/gpu/drm/msm/dsi/dsi_cfg.c  sdm845_dsi_cfg
//       .io_start = { { 0xae94000, 0xae96000 } }, .io_offset = DSI_6G_REG_SHIFT (4);
//     dsi_cfg.h  MSM_DSI_VER_MAJOR_6G=0x03, MSM_DSI_6G_VER_MINOR_V2_2_1=0x20020001;
//     dsi.h / dsi_host.c  dsi_cmd_dma_add(), msm_dsi_host_cmd_xfer_commit(),
//       msm_dsi_host_xfer_prepare/restore(), dsi_ctrl_enable() (CMD branch);
//     drivers/gpu/drm/msm/registers/display/dsi.xml  (domain DSI, width 32).
//
// The 6G register map has a 6G_HW_VERSION register at offset 0 that shifts every
// other register down by 4 bytes: msm_dsi_host_init() maps 0xae94000 into
// ctrl_base, then does "ctrl_base += cfg->io_offset" (= +4). dsi_get_version()
// reads 6G_HW_VERSION from the UNSHIFTED base (0xae94000) and REG_DSI_VERSION
// from the SHIFTED base, before the shift is applied for all other accesses.
// Therefore the runtime MMIO window for this driver is 0xae94004, and every
// MSM_DSI_REG_* below is the dsi.xml offset used directly on that shifted base.
//
// Design decision (why we do NOT reprogram the whole controller): the panel is
// already lit by the UEFI GOP framebuffer (enchilada-facts/parsed.txt cont_splash
// region 0x9d400000 + 0x2400000; NativeLegacyDisplay.cpp:562). Firmware already
// enabled command mode, clocks, lanes, PHY timings and the software DMA trigger.
// Re-running dsi_ctrl_enable() on a live controller would disturb the scanout.
// So this module implements ONLY the per-transfer CMD DMA path that
// msm_dsi_manager_cmd_xfer() performs on every DCS write: prepare -> commit ->
// wait -> restore. The cold-boot full config is left to the layer-3 runtime probe
// (documented in README.md) and is NOT part of this host-verified unit.
//
// Everything here is pure C over an injected register bus (struct msm_dsi_io),
// copied from the fake-MMIO pattern of drivers/NativeGeniI2c/GeniI2c.h:124-129,
// so it compiles and asserts on the host before any IOKit/Mach-O ABI is involved.

#ifndef ENCHILADA_MSM_DSI_CMD_H
#define ENCHILADA_MSM_DSI_CMD_H

#include <stddef.h>
#include <stdint.h>

#include "DsiDcs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* dsi_ctrl0 physical window; the runtime probe maps (MSM_DSI0_CTRL_PHYS + shift). */
#define MSM_DSI0_CTRL_PHYS 0x0ae94000u
#define MSM_DSI0_REG_SHIFT 4u

/* Register offsets from dsi.xml, relative to the shifted base 0xae94004. */
#define MSM_DSI_REG_CTRL         0x000u
#define MSM_DSI_REG_STATUS0      0x004u
#define MSM_DSI_REG_CMD_DMA_CTRL 0x038u
#define MSM_DSI_REG_CMD_CFG0     0x03cu
#define MSM_DSI_REG_CMD_CFG1     0x040u
#define MSM_DSI_REG_DMA_BASE     0x044u
#define MSM_DSI_REG_DMA_LEN      0x048u
#define MSM_DSI_REG_TRIG_CTRL    0x080u
#define MSM_DSI_REG_TRIG_DMA     0x08cu
#define MSM_DSI_REG_INTR_CTRL    0x10cu
#define MSM_DSI_REG_CLK_CTRL     0x118u

/* CTRL bits (dsi.xml). */
#define MSM_DSI_CTRL_ENABLE      (1u << 0)
#define MSM_DSI_CTRL_VID_MODE_EN (1u << 1)
#define MSM_DSI_CTRL_CMD_MODE_EN (1u << 2)
#define MSM_DSI_CTRL_CLK_EN      (1u << 8)
#define MSM_DSI_CTRL_LANES_4     (0xfu << 4)
/* STATUS0 bits. */
#define MSM_DSI_STATUS0_CMD_DMA_BUSY (1u << 1)
/* Cold-boot full-config reference values (for the layer-3 runtime probe; this
 * pure-C per-transfer module does not write these registers). */
#define MSM_DSI_CMD_DMA_CTRL_CMD_MODE ((1u << 28) | (1u << 26)) /* FROM_FRAME_BUFFER | LOW_POWER */
#define MSM_DSI_TRIG_CTRL_CMD_MODE(ch) (0x80000000u | (1u << 12) | 4u | ((uint32_t)(ch) << 8)) /* TE|BLOCK_DMA_WITHIN_FRAME|TRIGGER_SW|STREAM */
#define MSM_DSI_CLK_CTRL_ENABLE_CLKS 0x23fu
#define MSM_DSI_CMD_CFG0_RGB888      0x8u
#define MSM_DSI_CMD_CFG1_DCS         ((1u << 16) | (0x3cu << 8) | 0x2cu)

/* A layer-1 packet has at most 4 header + kDsiDcsPayloadBytes payload bytes; the
 * MSM DMA buffer is that rounded up to a multiple of 4. */
#define MSM_DSI_CMD_DMA_BUF_MAX (4u + kDsiDcsPayloadBytes)

/* Busy-spin defaults for msm_dsi_cmd_wait_idle(). The real delay_us callback MUST
 * be a CPU spin, not an XNU IODelay/WFE-style delay: RootCommands.h:22 records
 * that on enchilada a WFE-based poll never came back. */
#define MSM_DSI_CMD_POLLS   2000u
#define MSM_DSI_CMD_POLL_US 10u

enum {
    kMsmDsiCmdOk = 0,
    kMsmDsiCmdErrPacket = -1,
    kMsmDsiCmdErrTimeout = -2,
};

/* Register bus, injectable so the host test can model the controller. reg is the
 * dsi.xml offset (relative to 0xae94004). wmb may be NULL (the host test); in the
 * real kernel it must be a device-memory write barrier (OSSynchronizeIO / dsb). */
struct msm_dsi_io {
    void *ctx;
    uint32_t (*rd)(void *ctx, uint32_t reg);
    void (*wr)(void *ctx, uint32_t reg, uint32_t value);
    void (*delay_us)(void *ctx, unsigned us);
    void (*wmb)(void *ctx);
};

/* Build the MSM-specific CMD DMA buffer for one layer-1 DCS packet. This is the
 * byte layout of dsi_host.c:dsi_cmd_dma_add():
 *   data[0]=header[1], data[1]=header[2], data[2]=header[0] (DI),
 *   data[3]=0x80 (Last) | 0x40 (Long) [| 0x20 (BTA/read)], data[4..]=payload,
 *   then 0xff pad up to len=(packet.size+3)&~3.
 * A short write (0x05/0x15) yields a 4-byte buffer; a long write (0x39) an
 * 8-byte buffer for the packets layer 1 emits. Returns len>0, or -1 on a null
 * pointer, an inconsistent packet, or a too-small cap. On refusal buf is untouched. */
int msm_dsi_cmd_dma_fill(const struct dsi_dcs_packet *pkt, uint8_t *buf, size_t cap);

/* Per-transfer sequence (mirrors msm_dsi_manager_cmd_xfer on a live link). */
uint32_t msm_dsi_cmd_prepare(struct msm_dsi_io *io);                 /* CTRL |= CMD_MODE_EN|ENABLE; returns saved CTRL */
void msm_dsi_cmd_commit(struct msm_dsi_io *io, uint32_t dma_addr, unsigned len); /* DMA_BASE, DMA_LEN, TRIG_DMA=1, wmb */
int msm_dsi_cmd_wait_idle(struct msm_dsi_io *io, unsigned max_polls, unsigned poll_us); /* 0 ok, -1 timeout */
void msm_dsi_cmd_restore(struct msm_dsi_io *io, uint32_t saved);     /* restore saved CTRL */

/* One DCS write end to end. dma_buf/dma_cap describe the caller's DMA-coherent
 * buffer (the real runtime probe allocates it with IOBufferMemoryDescriptor);
 * dma_addr is its bus/physical address. Fills dma_buf, then prepare -> commit ->
 * wait -> restore. Returns kMsmDsiCmdOk or a negative error. */
int msm_dsi_cmd_send(struct msm_dsi_io *io, const struct dsi_dcs_packet *pkt,
                     uint8_t *dma_buf, size_t dma_cap, uint32_t dma_addr);

#ifdef __cplusplus
}
#endif

#endif /* ENCHILADA_MSM_DSI_CMD_H */
