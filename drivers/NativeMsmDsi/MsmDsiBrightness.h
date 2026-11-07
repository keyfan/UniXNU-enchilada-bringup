// SPDX-License-Identifier: BSD-2-Clause-Patent
// enchilada stage-3 layer 3a: ONE DCS brightness transmit over the MSM DSI0 CMD DMA
// channel. This is the pure-C, host-testable orchestrator that chains
//   layer 1  drivers/NativeDsiBacklight/DsiDcs.c     (DCS packet bytes)
//   layer 2  drivers/NativeMsmDsi/MsmDsiCmd.c        (CMD DMA buffer + trigger sequence)
// with the two runtime preconditions that were settled by the DEVICE-VERIFIED
// read-only probes, and publishes a result into native_backlight_stats[10].
//
// Why a separate orchestrator (instead of calling msm_dsi_cmd_send directly):
//   1. The 32-bit REG_DSI_DMA_BASE. Linux v6.12 dsi_host.c:2245 declares
//        void msm_dsi_host_cmd_xfer_commit(struct mipi_dsi_host *host, u32 dma_base, u32 len)
//      and dsi.xml <reg32 offset="0x00044" name="DMA_BASE">. The register is 32 bits
//      wide. enchilada's display SMMU context bank is in BYPASS (translation OFF,
//      SCTLR.M=0) -- DEVICE-CONFIRMED by evidence/backlight209mode2.md (CI #209,
//      run 42362111447, 510 rows `L st=4 ... irq=e0 ...`) -- so a CPU PHYSICAL address
//      passes through unchanged; it is NOT an IOVA that could be chosen low on purpose.
//      NativeUfs.cpp:1197 warns "every kernel allocation is above 4 GiB" (sheng/SM8550),
//      while enchilada-facts/parsed.txt:36-56 lists all SDM845 reserved-memory carveouts
//      in 0x85700000..0xad200000, i.e. below 4 GiB. Those two facts disagree about where
//      a fresh IOBufferMemoryDescriptor allocation lands, so the question is UNCERTAIN
//      until runtime. This module therefore takes the FULL 64-bit physical address and
//      REFUSES (without touching any register) when (phys >> 32) != 0. That is the
//      "先核验再上机" gate: a truncated high address must never reach TRIG_DMA.
//   2. Cache maintenance. The DMA buffer is written by the CPU through a cached kernel
//      mapping; the DSI controller reads it through the bypass SMMU. The DWC3 gadget
//      (Dwc3Gadget.cpp:304-313 cleanDma) and UFS (NativeUfs.cpp:1150-1155) both do
//      `dc civac` per 64-byte line + `dsb sy` BEFORE handing a buffer to the engine.
//      msm_dsi_cmd_send() fills + commits internally with no hook, so this orchestrator
//      calls the layer-2 primitives directly and invokes a clean callback between the
//      fill and the trigger.
//
// This unit is READ/WRITE only on the injected struct msm_dsi_io register bus and on
// the caller-owned DMA buffer. It performs NO allocation, NO mapping and NO IOKit call:
// the mode-3 C++ runtime probe (the next layer) owns those. Everything here compiles
// and asserts on the host before any IOKit/Mach-O ABI is involved.
//
// Two independent verifications of the register/DMA facts are recorded in
// MsmDsiCmd.h (vendor DTS + Linux v6.12) and evidence/backlight209mode2.md (device).

#ifndef ENCHILADA_MSM_DSI_BRIGHTNESS_H
#define ENCHILADA_MSM_DSI_BRIGHTNESS_H

#include <stddef.h>
#include <stdint.h>

#include "MsmDsiCmd.h"   /* struct msm_dsi_io, msm_dsi_cmd_* primitives, MSM_DSI_CMD_* */

#ifdef __cplusplus
extern "C" {
#endif

/* Stage code published to native_backlight_stats[0], the BootLog.c L row st= field.
 * 5 = a brightness packet was pushed and STATUS0.CMD_DMA_BUSY cleared (transmit OK).
 * The negative codes are failure steps; the register bus is left RESTORED in every
 * case. Distinct from the read-only stages (3 = mode 1 snapshot, 4 = mode 2 SMMU). */
enum {
    kMsmDsiBrightnessStageTx          = 5,
    kMsmDsiBrightnessStageNoIo        = -1,  /* io / dma buffer / required callbacks missing */
    kMsmDsiBrightnessStageAddrTooHigh = -2,  /* (dma_phys >> 32) != 0: 32-bit DMA_BASE */
    kMsmDsiBrightnessStagePacket      = -3,  /* DCS packet build or CMD DMA fill failed */
    kMsmDsiBrightnessStageDma         = -4,  /* msm_dsi_cmd_wait_idle timed out */
};

/* Result of one msm_dsi_brightness_send() call. dma_addr_lo is the value actually
 * written to REG_DSI_DMA_BASE; level is the DCS brightness after layer-1 clamping. */
struct msm_dsi_brightness_result {
    int rc;               /* kMsmDsiCmdOk (0) or a negative error */
    int stage;            /* kMsmDsiBrightnessStageTx or a negative stage */
    uint8_t dma_len;      /* CMD DMA buffer length committed (4 short / 8 long) */
    uint32_t dma_addr_lo; /* low 32 bits of dma_phys, written to DMA_BASE */
    uint16_t level;       /* clamped 1..1023 DCS level that was sent */
};

/* Cumulative counters owned by the caller (the 1 Hz probe), published for diagnosis. */
struct msm_dsi_brightness_counters {
    uint32_t polls;
    uint32_t sent;
    uint32_t addr_high;
    uint32_t dma_err;
};

/* One DCS SET_DISPLAY_BRIGHTNESS (large form) transmit.
 *
 *   io         : live DSI0 register bus. io->rd and io->wr are REQUIRED. io->wmb is
 *                called by msm_dsi_cmd_commit() after the three writes (dsb sy).
 *                io->delay_us should be a CPU spin, NOT an XNU IODelay/WFE wait
 *                (RootCommands.h:22: a WFE-based poll never returned on enchilada).
 *   dma_buf    : CPU-visible, physically contiguous DMA buffer. Written with the MSM
 *                CMD DMA bytes, then cache-cleaned, then read by the controller.
 *   dma_cap    : capacity of dma_buf (>= MSM_DSI_CMD_DMA_BUF_MAX).
 *   dma_phys   : the buffer's 64-bit PHYSICAL bus address. Must satisfy
 *                (dma_phys >> 32) == 0 or the call REFUSES with AddrTooHigh before
 *                any register write or cache maintenance.
 *   clean      : cache-maintenance callback invoked AFTER dma_buf is filled and BEFORE
 *                the trigger. Must `dc civac` over every 64-byte line covering
 *                [va, va+len) then `dsb sy`. May be NULL for host tests ONLY; a real
 *                device must supply it or the controller may read stale cache lines.
 *   clean_ctx  : opaque argument forwarded to clean().
 *   brightness : requested level; layer 1 clamps it to 1..1023.
 *   out        : optional result (may be NULL). On ANY refusal out->stage/out->rc are
 *                set and no register is touched.
 *
 * Returns kMsmDsiCmdOk (0) on a completed transmit, kMsmDsiCmdErrTimeout (-2) when
 * STATUS0 stayed busy, or a negative kMsmDsiBrightnessStage* code. CTRL is always
 * restored via msm_dsi_cmd_restore() even on a timeout. */
int msm_dsi_brightness_send(struct msm_dsi_io *io, uint8_t *dma_buf, size_t dma_cap,
                            uint64_t dma_phys,
                            void (*clean)(void *ctx, const void *va, size_t len),
                            void *clean_ctx, uint16_t brightness,
                            struct msm_dsi_brightness_result *out);

/* Publish a transmit result into native_backlight_stats[10] for the BootLog.c L row
 * (format "L st=%d req=%d lvl=%d a=%d b=%d xerr=%d irq=%x ea=%x eb=%x err=%d"):
 *   [0]=stage(5 / negative) [1]=polls [2]=level [3]=dma_len [4]=dma_addr_lo
 *   [5]=rc [6]=sent [7]=addr_high [8]=dma_err [9]=0 (reserved).
 * `stats` is a POINTER to volatile (C++20 P1152 / CI #207 -Wdeprecated-volatile:
 * never spell a volatile-qualified array parameter). Returns 0 or -1 on null. */
int msm_dsi_brightness_publish(const struct msm_dsi_brightness_result *r,
                               const struct msm_dsi_brightness_counters *c,
                               volatile int32_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* ENCHILADA_MSM_DSI_BRIGHTNESS_H */
