// SPDX-License-Identifier: BSD-2-Clause-Patent
// enchilada stage-3 layer 2.5: read-only runtime session over the MSM DSI0 command
// channel. This is the pure-C, host-testable core of package/EnchiladaBacklightProbe.cpp.
//
// Why read-only first: the panel is already lit by the UEFI GOP framebuffer and the
// firmware owns the DSI0 controller (command mode, clocks, lanes, PHY, DMA trigger all
// configured). Before any DCS packet is sent we must VERIFY on the device that XNU can
// map and read the controller at its 6G-shifted base 0xae94004. This mirrors the staged,
// default-off probes that are already device-proven on this board:
//   * package/EnchiladaTouchProbe.cpp   stage 1 = "read only"
//   * package/EnchiladaCpuFreq.cpp      mode 1  = "read only"
// A read-only snapshot cannot disturb the scanout or hang the DSI bus; sending a packet
// (stage 3 / AppleARMBacklight) still needs the CMD-DMA address question settled (physical
// vs the MDSS SMMU IOVA that Linux 6G uses) and cache maintenance. See README.md.
//
// Register offsets are the dsi.xml values in MsmDsiCmd.h, relative to 0xae94004. All
// register access goes through the injected struct msm_dsi_io, so this compiles and is
// asserted on the host; the C++ shim supplies real MMIO callbacks over the mapped window.

#ifndef ENCHILADA_MSM_DSI_SESSION_H
#define ENCHILADA_MSM_DSI_SESSION_H

#include <stdint.h>

#include "MsmDsiCmd.h"   /* MSM_DSI_REG_* offsets + struct msm_dsi_io */

#ifdef __cplusplus
extern "C" {
#endif

/* The live firmware configuration of the DSI0 controller, read once per poll. */
struct msm_dsi_snapshot {
    uint32_t ctrl;           /* MSM_DSI_REG_CTRL          0x000 */
    uint32_t status0;        /* MSM_DSI_REG_STATUS0       0x004 */
    uint32_t cmd_dma_ctrl;   /* MSM_DSI_REG_CMD_DMA_CTRL  0x038 */
    uint32_t cmd_cfg0;       /* MSM_DSI_REG_CMD_CFG0      0x03c */
    uint32_t cmd_cfg1;       /* MSM_DSI_REG_CMD_CFG1      0x040 */
    uint32_t trig_ctrl;      /* MSM_DSI_REG_TRIG_CTRL     0x080 */
    uint32_t clk_ctrl;       /* MSM_DSI_REG_CLK_CTRL      0x118 */
};

/* Stage code published to native_backlight_stats[0]; 3 = mapped + snapshot OK (matches
 * sheng's AppleARMBacklight.cpp stage 3 "published"). Lower values are failure steps. */
enum {
    kMsmDsiSessionStageAbsent = 0,
    kMsmDsiSessionStageWaitPlatform = 1,
    kMsmDsiSessionStageMapFailed = -1,
    kMsmDsiSessionStageMapped = 3,
};

/* Read the seven config/status registers into *out. io->rd is the ONLY thing used; no
 * register is written. Returns 0, or -1 if io or out is null. */
int msm_dsi_session_snapshot(struct msm_dsi_io *io, struct msm_dsi_snapshot *out);

/* Publish a snapshot to the BootLog.c L row buffer (native_backlight_stats[10]).
 * The L row format is "L st=%d req=%d lvl=%d a=%d b=%d xerr=%d irq=%x ea=%x eb=%x
 * err=%d" reading stats[0..9]. The read-only stage deliberately reuses the diagnostic
 * slots to expose the live DSI0 register values so a device boot proves the MMIO path:
 *   [0]=stage(3) [1]=polls [2]=0 (no brightness sent) [3]=CTRL [4]=STATUS0
 *   [5]=CMD_DMA_CTRL [6]=TRIG_CTRL [7]=CMD_CFG0 [8]=CMD_CFG1 [9]=CLK_CTRL.
 * Returns 0, or -1 if snap or stats is null. */
int msm_dsi_session_publish(const struct msm_dsi_snapshot *snap, uint32_t polls,
                            volatile int32_t stats[10]);

#ifdef __cplusplus
}
#endif

#endif /* ENCHILADA_MSM_DSI_SESSION_H */
