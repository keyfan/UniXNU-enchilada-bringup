// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoBoardInputs -- enchilada stage 5 (GPU) HOST-VERIFIED board-input study.
//
// This is NOT a GPU driver. It is a freestanding, pure-C encoding of the GPU
// board-level facts extracted from the vendored Mu-Silicium enchilada DTB
// (the SAME DTB behind the device-verified stage-3 sofef00 bl_ctrl_dcs panel
// at :2661, the stage-4 touch facts and the stage-7 gauge facts), plus a
// read-only classification/gating helper a future A630 backend could call.
// It maps nothing, writes no register, loads no firmware, is NOT linked into
// the kernel, and is HOST-VERIFIED only (a DTB is a firmware description, NOT
// a device measurement). No CI, no flash, no device verification.
//
// Provenance (independently pinned; see evidence/stage5-gpu-a630-board.md)
// ------------------------------------------------------------------------
//  Mu-Silicium/Resources/DTBs/enchilada.dts, 784857 bytes,
//  full sha256 e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275,
//  present in 4 byte-identical local copies (notes/mu-silicium/enchilada.dts,
//  work/Mu-Silicium-full/..., work/Mu-Silicium/..., work/mock-mu-silicium/...).
//
//  Two independent verifications (scripts/extract-enchilada-gpu-board.py and
//  scripts/verify-enchilada-gpu-board.py) re-derive these constants from the
//  pinned DTB and cross-check them against (a) device-verified C constants in
//  this repo (AdrenoCatalog.h, SmmuSurvey.h, Dwc3Registers.h) and (b) the
//  sha256-pinned Linux v6.12 a6xx_catalog.c. 21 cross-domain checks PASS.
//
// Load-bearing correction (Round 21, 2026-11-09)
// -----------------------------------------------
//  Round 20 recorded "GPU SMMU SID/mask UNVERIFIED (Mu-Silicium DTB 404)"
//  because a subagent could not FETCH the DTB. It exists locally. enchilada's
//  GPU does NOT use the apps_smmu @ 0x15000000 (flash #8's survey target,
//  #iommu-cells=2, SID+mask). It uses a DEDICATED kgsl SMMU:
//    arm,smmu-kgsl@5040000, compatible="qcom,smmu-v2",
//    reg=<0x5040000 0x10000>, #iommu-cells=<0x01>, phandle=<0x1ac>.
//  Because #iommu-cells=1, each GPU iommus tuple is (phandle, SID) -- there
//  is NO mask cell. Context-bank SIDs (from qcom,kgsl-iommu / qcom,gmu):
//    gfx3d_user=0x00, gfx3d_secure=0x02,0x01, gmu_user=0x04, gmu_kernel=0x05.
//  ALL GPU iommus use phandle 0x1ac; NONE use apps_smmu phandle 0x29.
//  => "a stream id / mask must be sourced before any GPU MMIO" is RESOLVED at
//  the board-description level; a stage-1 SMMU context can now be designed.
//  It is STILL not device-verified: no register was read on the device.
//
//  Vendor-vs-mainline distinction (broad lookup, honest): the DTB is a
//  downstream KGSL/vendor description. Mainline Linux v6.12 a6xx_gmu.c:1277
//  uses `msm_iommu_new(gmu->dev, 0)` (generic IOMMU handle) rather than naming
//  kgsl context banks. The vendor DTB SIDs are therefore the authoritative
//  BOARD facts for this port, not mainline driver constants.

#ifndef ADRENO_BOARD_INPUTS_H
#define ADRENO_BOARD_INPUTS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- DEDICATED kgsl SMMU (NOT apps_smmu) -------------------------------- */
#define ADRENO_BOARD_KGSL_SMMU_BASE   0x05040000ull
#define ADRENO_BOARD_KGSL_SMMU_SIZE   0x00010000ull
#define ADRENO_BOARD_KGSL_SMMU_PHANDLE 0x1acu
#define ADRENO_BOARD_KGSL_SMMU_IOMMU_CELLS 1u

/* apps_smmu @0x15000000 -- the one flash #8 surveyed (SmmuSurvey.h:40,
 * Dwc3Registers.h:35). #iommu-cells=2, phandle 0x29. NOT used by the GPU. */
#define ADRENO_BOARD_APPS_SMMU_BASE  0x15000000ull
#define ADRENO_BOARD_APPS_SMMU_PHANDLE 0x29u
#define ADRENO_BOARD_APPS_SMMU_IOMMU_CELLS 2u

/* ---- GPU / GMU register windows (DTB :23632 / :23875) ------------------- */
#define ADRENO_BOARD_GPU_BASE        0x05000000ull
#define ADRENO_BOARD_GPU_SIZE        0x00040000ull
#define ADRENO_BOARD_GPU_CX_DBGC     0x05061000ull
#define ADRENO_BOARD_GPU_CX_MISC     0x0509e000ull
#define ADRENO_BOARD_GMU_BASE        0x0506a000ull
#define ADRENO_BOARD_GMU_SIZE        0x00030000ull
#define ADRENO_BOARD_GMU_PDC_BASE    0x0b200000ull
#define ADRENO_BOARD_GMU_PDC_SIZE    0x00300000ull

/* IRQs (GIC SPI): GPU 300, GMU HFI 304, GMU 305. */
#define ADRENO_BOARD_GPU_IRQ         300u
#define ADRENO_BOARD_GMU_HFI_IRQ     304u
#define ADRENO_BOARD_GMU_IRQ         305u

/* board identity / config (DTB :23641, :23647, :23649, :23612-23613). */
#define ADRENO_BOARD_CHIPID          0x06030001u
#define ADRENO_BOARD_HIGHEST_BANK_BIT 15u
#define ADRENO_BOARD_UBWC_MODE        2u
#define ADRENO_BOARD_ZAP_PAS_ID       13u   /* GPU_PAS_ID, qcom,pas-id=<0x0d> */
#define ADRENO_BOARD_ZAP_FW_NAME      "a630_zap"

/* qcom,kgsl-iommu protect / micro-mmu / secure alignment (DTB :23851-23855). */
#define ADRENO_BOARD_KGSL_IOMMU_PROTECT_BASE  0x00040000ull
#define ADRENO_BOARD_KGSL_IOMMU_PROTECT_SIZE  0x0000c000ull
#define ADRENO_BOARD_KGSL_MICRO_MMU_CONTROL   0x00006000ull
#define ADRENO_BOARD_KGSL_SECURE_ALIGN_MASK   0x00000fffull

/* ---- context-bank SIDs (single-cell: NO mask) --------------------------- */
enum adreno_board_context {
    kAdrenoBoardCtxGfx3dUser   = 0,
    kAdrenoBoardCtxGfx3dSecure = 2, /* primary */
    kAdrenoBoardCtxGfx3dSecureAlt = 1,
    kAdrenoBoardCtxGmuUser     = 4,
    kAdrenoBoardCtxGmuKernel   = 5,
};

/* 1 iff the phandle is the dedicated kgsl SMMU (0x1ac), else 0. A GPU MMIO
 * probe must be gated on this: programming a stage-1 context on apps_smmu
 * (0x29) for the GPU would target the WRONG SMMU. */
int adreno_board_is_kgsl_smmu(uint32_t phandle);
/* 1 iff the DTB's GPU iommus cells are single-cell (SID only, no mask). */
int adreno_board_sid_is_unmasked(void);
/* Returns 1 and fills *out_sid for a known GPU context bank, else 0.
 * out_sid may be NULL. Never returns the apps_smmu phandle's SIDs. */
int adreno_board_context_sid(enum adreno_board_context ctx, uint32_t *out_sid);
/* Read-only gate: is the GPU board description complete enough to design a
 * stage-1 SMMU context? Returns 1 (all board facts present and consistent).
 * NOTE: this is a BOARD-DESCRIPTION gate, NOT device verification; a real
 * device register read is still required before any claim of functionality. */
int adreno_board_smmu_context_ready(void);

#ifdef __cplusplus
}
#endif
#endif
