// SPDX-License-Identifier: BSD-2-Clause-Patent
// enchilada stage-3 layer-3 PREREQUISITE: READ-ONLY ARM SMMUv2 survey of the
// apps_smmu@0x15000000 display context banks. This is the pure-C, host-testable
// core that answers the one question blocking the first DCS brightness packet:
// does the display's DMA use PHYSICAL addresses (context bank in bypass /
// translation off) or an SMMU IOVA (translation on)?
//
// Why this exists (two independent authoritative sources, cross-checked):
//   * Linux v6.12 drivers/gpu/drm/msm/dsi/dsi_host.c (SDM845 = 6G/v2.2 host ops):
//       dsi_tx_buf_alloc_6g() -> msm_gem_kernel_new(..., aspace, ..., &iova)
//       dsi_dma_base_get_6g() -> msm_gem_get_and_pin_iova()  (an SMMU IOVA)
//       dsi_cmd_dma_tx() writes that value into REG_DSI_DMA_BASE.
//     i.e. mainline 6G feeds DSI_DMA_BASE an IOVA, NOT a CPU physical address
//     (the old v2 path dsi_dma_base_get_v2() used dma_alloc_coherent physical).
//   * This whole repo (drivers/NativeUfs/NativeUfs.cpp:949-954, Dwc3Gadget.cpp:
//     196-221) uses PHYSICAL, cached, contiguous DMA addresses on the assumption
//     that the stream is in an apps_smmu bypass context bank (SCTLR.M == 0).
//     sheng's display bank was measured with "SCTLR 0xe0 = translation off"
//     (NativeUfs.cpp:1196). When M==0 an IOVA == the physical address, so the
//     two sources agree ONLY IF the enchilada display bank is also in bypass.
//
// The catch: enchilada/SDM845 display stream IDs (vendor DTS :853 -> 0x880,
// 0xc80; :993 -> 0x881, 0xc81; :3392-3399 -> 0x1090, 0x1091) are DIFFERENT from
// sheng/SM8550's 0x1c00/0x1c02, and there is NO on-device diagnostic row for
// the display bank (verified against the backlight208/console206 boot logs:
// they only print XNU's own MMU "TCR=... TTBR0=... SCTLR=3454593d" and
// "[display state]"). So the bypass assumption is UNCERTAIN on this board and
// must be confirmed with a real, read-only device survey before any DMA trigger
// (先核验再上机). This module is the survey logic, fully host-verified now; the
// runtime mode-2 wiring + CI compile + device read are the next step (see
// drivers/NativeMsmDsi/README.md and porting.toml display.next_action).
//
// Register offsets are ARM SMMUv2 (ARM IHI 0062D), taken VERBATIM from two
// device-proven local sources:
//   * drivers/NativeUfs/SmmuTakeover.h  (the planner that actually talks to
//     this SMMU on device): IDR0 0x20, IDR2 0x28, IDR1 0x24, SMR(n) 0x800+4n,
//     S2CR(n) 0xc00+4n, CBAR 0x1000+4n, CBA2R 0x1800+4n, context bank at
//     (globalPages+bank)*0x1000, SCTLR reg 0 bit0 = M.
//   * drivers/NativeUfs/NativeUfs.cpp:862-944  (read-only dumps): sCR0 0x0,
//     IDR0 0x20, IDR1 0x24, sGFSR 0x48, SMR(n)/S2CR(n), CBAR 0x1000+4n,
//     SCTLR at context bank +0x0, TTBR0 lo/hi 0x20/0x24, TCR 0x30, FSR 0x58.
//
// The survey is READ-ONLY BY CONSTRUCTION: struct msm_smmu_io exposes only a
// read callback (no write, no delay, no wmb). It never writes a register.

#ifndef ENCHILADA_MSM_DSI_SMMU_H
#define ENCHILADA_MSM_DSI_SMMU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* apps_smmu@0x15000000 (vendor DTS :25897, Dwc3Registers.h:35). The survey
 * window must cover the global register space plus every context bank;
 * (globalPages + numCb) * 0x1000 with globalPages<=2 and numCb<=128 needs
 * 0x82000, so the 0x80000 DWC3 window is illustrative only -- the caller
 * passes the real mapped window and msm_smmu_survey() validates the geometry. */
#define MSM_SMMU_BASE   0x15000000ull
#define MSM_SMMU_WINDOW 0x80000ull

/* Global (GR0/GR1) register offsets relative to MSM_SMMU_BASE. */
#define MSM_SMMU_sCR0      0x000u
#define MSM_SMMU_IDR0      0x020u
#define MSM_SMMU_IDR1      0x024u
#define MSM_SMMU_IDR2      0x028u
#define MSM_SMMU_sGFSR     0x048u
#define MSM_SMMU_SMR(n)    (0x800u  + 4u * (uint32_t)(n))
#define MSM_SMMU_S2CR(n)   (0xc00u  + 4u * (uint32_t)(n))
#define MSM_SMMU_CBAR(n)   (0x1000u + 4u * (uint32_t)(n))
#define MSM_SMMU_CBA2R(n)  (0x1800u + 4u * (uint32_t)(n))

/* Context-bank register offsets relative to (globalPages+bank)*0x1000. */
#define MSM_SMMU_CB_SCTLR     0x000u
#define MSM_SMMU_CB_TTBR0_LO  0x020u
#define MSM_SMMU_CB_TTBR0_HI  0x024u
#define MSM_SMMU_CB_TCR       0x030u
#define MSM_SMMU_CB_FSR       0x058u

/* SMR / S2CR / SCTLR bit extraction (SmmuTakeover.h:26-29, :66-75, :98). */
#define MSM_SMMU_SMR_VALID       0x80000000u
#define MSM_SMMU_SMR_MASK(smr)   (((smr) >> 16) & 0x7fffu)
#define MSM_SMMU_S2CR_TYPE(s2cr) (((s2cr) >> 16) & 3u)  /* 0 = bypass/translation-off */
#define MSM_SMMU_S2CR_CBNDX(s2cr) ((s2cr) & 0xffu)
#define MSM_SMMU_SCTLR_M         1u                       /* bit0: translation enable */

/* Injectable, READ-ONLY register bus. reg is a byte offset into the SMMU window.
 * No write callback exists, so a survey cannot mutate SMMU state. */
struct msm_smmu_io {
    void *ctx;
    uint32_t (*rd)(void *ctx, uint32_t reg);
};

/* A stream-match request, exactly the <phandle SID MASK> cells of a vendor DTS
 * iommus property. mask is the set of low ID bits the SMMU ignores. */
struct msm_smmu_stream {
    uint16_t id;
    uint16_t mask;
};

/* A completed read-only survey. All fields are filled on a 0 return (even for
 * NotFound/Geometry verdicts); the caller can publish them to a diagnostic row. */
struct msm_smmu_survey {
    uint32_t scr0, idr0, idr1, idr2, gfsr;   /* global registers */
    uint32_t num_smr, num_cb, global_pages;  /* decoded geometry */
    uint32_t matched;      /* display streams that have a valid SMR */
    uint32_t bypass;       /* ... whose bank is bypass (type 0 + SCTLR.M=0) */
    uint32_t translating;  /* ... whose bank has translation enabled */
    uint16_t first_stream; /* first matched display stream id (0xffff if none) */
    uint32_t first_slot;   /* its SMR/S2CR slot (0xffffffff if none) */
    uint32_t first_smr, first_s2cr;
    uint32_t first_cbar, first_sctlr, first_ttbr0_lo, first_ttbr0_hi, first_tcr;
    uint32_t first_stream_valid;  /* 1 once a bank was actually read for first_stream */
    int      verdict;      /* enum below */
};

/* verdict: Bypass => IOVA==physical (this repo's physical DMA is correct);
 * Translate => an SMMU IOVA is required (physical would be wrong);
 * NotFound => no display stream mapping observed (cannot conclude);
 * Geometry => window too small / unsupported SMMU, no SMR or bank was read. */
enum {
    kMsmSmmuVerdictTranslate = 0,
    kMsmSmmuVerdictBypass    = 1,
    kMsmSmmuVerdictNotFound  = -1,
    kMsmSmmuVerdictGeometry  = -2,
};

unsigned msm_smmu_num_smr(uint32_t idr0);
unsigned msm_smmu_num_cb(uint32_t idr1);
unsigned msm_smmu_global_pages(uint32_t idr1);

/* Full read-only survey. streams/nstreams are the (id, mask) requests to look
 * for; window is the mapped SMMU byte length. Stream matching uses the same
 * rule as SmmuTakeover.h:68 -- ((smr ^ id) & 0xffff & ~(smrMask|mask)) == 0.
 * Returns 0 on a completed survey (*out written, verdict in the enum), or -1
 * on invalid arguments (io/rd/out/streams null or nstreams==0) with *out left
 * untouched. Never reads an offset >= window. */
int msm_smmu_survey(struct msm_smmu_io *io, uint64_t window,
                    const struct msm_smmu_stream *streams, size_t nstreams,
                    struct msm_smmu_survey *out);

/* Convenience wrapper using the built-in enchilada display stream list. */
int msm_smmu_survey_display(struct msm_smmu_io *io, uint64_t window,
                            struct msm_smmu_survey *out);

/* The built-in display (id, mask) list (vendor DTS :853/:995/:3393/:3399):
 * {0x880,0x08},{0xc80,0x08},{0x881,0x08},{0xc81,0x08},{0x1090,0x00},
 * {0x1091,0x00}. */
const struct msm_smmu_stream *msm_smmu_display_streams(size_t *count);

/* 1 if the survey concluded all matched display banks are bypass. */
int msm_smmu_verdict_is_bypass(const struct msm_smmu_survey *s);

/* Pure packing helper (NO BootLog/volatile coupling): fill out10 with a compact
 * survey result, ready for a caller to copy into native_backlight_stats[10].
 * Layout: [0]=4 (stage "surveyed") [1]=matched [2]=verdict
 * [3]=num_cb|(global_pages<<16) [4]=bypass|(translating<<8) [5]=first_s2cr
 * [6]=first_sctlr [7]=first_cbar [8]=first_ttbr0_lo [9]=first_stream.
 * Returns 0, or -1 on a null pointer (out10 untouched). */
int msm_smmu_survey_pack(const struct msm_smmu_survey *s, int32_t *out10);

#ifdef __cplusplus
}
#endif

#endif /* ENCHILADA_MSM_DSI_SMMU_H */
