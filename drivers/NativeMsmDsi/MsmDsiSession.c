// SPDX-License-Identifier: BSD-2-Clause-Patent
// Read-only runtime session over the MSM DSI0 command channel (stage-3 layer 2.5).
// Pure C over an injected register bus: no IOKit, no MMIO writes. See MsmDsiSession.h.
#include "MsmDsiSession.h"

int msm_dsi_session_snapshot(struct msm_dsi_io *io, struct msm_dsi_snapshot *out) {
    if (!io || !out || !io->rd) return -1;
    out->ctrl         = io->rd(io->ctx, MSM_DSI_REG_CTRL);
    out->status0      = io->rd(io->ctx, MSM_DSI_REG_STATUS0);
    out->cmd_dma_ctrl = io->rd(io->ctx, MSM_DSI_REG_CMD_DMA_CTRL);
    out->cmd_cfg0     = io->rd(io->ctx, MSM_DSI_REG_CMD_CFG0);
    out->cmd_cfg1     = io->rd(io->ctx, MSM_DSI_REG_CMD_CFG1);
    out->trig_ctrl    = io->rd(io->ctx, MSM_DSI_REG_TRIG_CTRL);
    out->clk_ctrl     = io->rd(io->ctx, MSM_DSI_REG_CLK_CTRL);
    return 0;
}

int msm_dsi_session_publish(const struct msm_dsi_snapshot *snap, uint32_t polls,
                            volatile int32_t stats[10]) {
    if (!snap || !stats) return -1;
    stats[0] = kMsmDsiSessionStageMapped;   /* st=3: mapped + snapshot published */
    stats[1] = (int32_t)polls;              /* req=: number of 1 Hz polls so far */
    stats[2] = 0;                           /* lvl=: no brightness packet sent in this stage */
    stats[3] = (int32_t)snap->ctrl;         /* a=: live CTRL */
    stats[4] = (int32_t)snap->status0;      /* b=: live STATUS0 */
    stats[5] = (int32_t)snap->cmd_dma_ctrl; /* xerr=: live CMD_DMA_CTRL */
    stats[6] = (int32_t)snap->trig_ctrl;    /* irq=: live TRIG_CTRL (hex in L row) */
    stats[7] = (int32_t)snap->cmd_cfg0;     /* ea=: live CMD_CFG0 (hex in L row) */
    stats[8] = (int32_t)snap->cmd_cfg1;     /* eb=: live CMD_CFG1 (hex in L row) */
    stats[9] = (int32_t)snap->clk_ctrl;     /* err=: live CLK_CTRL */
    return 0;
}
