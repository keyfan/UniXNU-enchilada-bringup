// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoCatalog -- enchilada stage 5 (GPU) HOST-VERIFIED feasibility / identification study.
//
// This is NOT a GPU driver. It is a small, freestanding, pure-C encoding of the
// authoritative hardware/driver facts gathered during the stage-5 audit, plus
// classification + gating functions that a future A630 kernel backend could
// call. Everything here is testable on the host; it maps nothing, writes no
// register, loads no firmware, and is NOT linked into the kernel.
//
// Sources (three independent research passes, 2026-11-08)
// --------------------------------------------------------
//  (1) Linux v6.12 drivers/gpu/drm/msm/adreno/a6xx_catalog.c
//      A630: .chip_ids = ADRENO_CHIP_IDS(0x06030001, 0x06030002),
//            .family = ADRENO_6XX_GEN1, .revn = 630, .gmem = SZ_1M,
//            .fw = { a630_sqe.fw, a630_gmu.bin }, .zapfw = a630_zap.mdt,
//            .a6xx = { .gmu_cgc_mode = 0x00020202 }.
//      A740: .chip_ids = ADRENO_CHIP_IDS(0x43050a01) /* "C510v2" */,
//            .family = ADRENO_7XX_GEN2, .gmem = 3*SZ_1M,
//            .fw = { a740_sqe.fw, gmu_gen70200.bin }, .zapfw = a740_zap.mdt,
//            .gmu_chipid = 0x7020100, .address_space_size = SZ_16G.
//  (2) Linux v6.12 drivers/gpu/drm/msm/adreno/a6xx_gmu.c
//      a6xx_gmu_init(): "} else if (adreno_is_a630_family(adreno_gpu)) {
//          /* HFI v1, has sptprac */ gmu->legacy = true; ..."  -> A630 = legacy GMU.
//  (3) vendor DTB Mu-Silicium/Resources/DTBs/enchilada.dts
//      :23632 qcom,kgsl-3d0@5000000 reg=<0x5000000 0x40000 ...>,
//             qcom,chipid=<0x6030001>, 6 clocks incl gmu_clk, qcom,gmu present.
//      :23610 qcom,kgsl-hyp qcom,firmware-name="a630_zap".
//      plus https://unixnu.com/zh/porting/gpu section 1: other GPU generations
//      need a matching-generation kernel backend + compiler before any frontend.
//
// Load-bearing conclusion (honest, 2026-11-08)
// -------------------------------------------
// enchilada is A630 (6xx GEN1, legacy GMU HFI v1 / SPTPRAC). sheng's six GPU
// archives target A740 (7xx GEN2). The official guide requires a matching-gen
// backend + compiler before a Metal frontend. Three proprietary A630 firmware
// blobs (a630_zap.mbn, a630_gmu.bin, a630_sqe.fw) are absent from the workspace,
// and 246 decoded device logs contain ZERO GPU-driver literals. => the full
// stage-5 Metal path is BLOCKED on this device now. This module records that
// decision in tested code; it does not claim any device verification.
//
// The exact chip-id REGISTER offset was deliberately NOT asserted: the a6xx.xml
// register database has no CHIP_ID field. qcom,chipid=<0x6030001> is a DT
// property, not a proven register value; a future probe must source the offset.

#ifndef ADRENO_CATALOG_H
#define ADRENO_CATALOG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Chip IDs as they appear in the A6XX chip-id space (a6xx_catalog.c). */
#define ADRENO_CHIP_A630_V1  0x06030001u
#define ADRENO_CHIP_A630_V2  0x06030002u
#define ADRENO_CHIP_A740     0x43050a01u

/* kgsl-3d0 register window from the enchilada vendor DTB :23632. */
#define ADRENO_KGSL3D0_BASE  0x05000000u
#define ADRENO_KGSL3D0_SIZE  0x00040000u
/* qcom,chipid = <0x6030001> (DT property, NOT a register value). */
#define ADRENO_A630_DTB_CHIPID 0x06030001u

/* Firmware presence bitmask (bit i == firmware index i). */
#define ADRENO_FW_ZAP  (1u << 0) /* a630_zap.mbn  / a740_zap.mbn */
#define ADRENO_FW_GMU  (1u << 1) /* a630_gmu.bin  / gmu_gen70200.bin */
#define ADRENO_FW_SQE  (1u << 2) /* a630_sqe.fw   / a740_sqe.fw */
#define ADRENO_FW_ALL  (ADRENO_FW_ZAP | ADRENO_FW_GMU | ADRENO_FW_SQE)

/* adreno_catalog_assess() verdict: a bitmask of blockers, 0 == feasible. */
#define ADRENO_FEASIBLE           0x00u
#define ADRENO_BLOCK_UNKNOWN_CHIP 0x01u
#define ADRENO_BLOCK_FIRMWARE     0x02u
#define ADRENO_BLOCK_BACKEND      0x04u

enum adreno_generation {
    kAdrenoGenerationUnknown = 0,
    kAdrenoGen1_A6xx = 1, /* A630: 6xx GEN1 */
    kAdrenoGen2_A7xx = 2  /* A740: 7xx GEN2 */
};

enum adreno_gmu_kind {
    kAdrenoGmuUnknown = 0,
    kAdrenoGmuLegacy = 1, /* A630: legacy GMU, HFI v1, SPTPRAC */
    kAdrenoGmuModern = 2  /* A740: GEN2 GMU */
};

struct adreno_catalog_entry {
    uint32_t chip_id;
    enum adreno_generation generation;
    enum adreno_gmu_kind gmu;
    uint16_t revn;               /* 630 / 740 */
    uint32_t gmem_bytes;         /* SZ_1M vs 3*SZ_1M */
    uint64_t address_space_size; /* 0 == platform default, else SZ_16G */
    uint8_t  legacy_gmu;         /* 1 == HFI v1 / SPTPRAC (A630) */
    const char *fw_zap;
    const char *fw_gmu;
    const char *fw_sqe;
};

/* Classify a raw chip id. Returns 0 on success and fills *out; -1 if unknown.
 * On -1, *out is zeroed with generation/gmu set to Unknown. NULL out -> -1. */
int adreno_catalog_lookup(uint32_t chip_id, struct adreno_catalog_entry *out);

/* The A630 firmware set required for enchilada (guide section 2), by index
 * 0..2; returns NULL for any other index. */
const char *adreno_catalog_firmware_name(unsigned index);
const char *adreno_catalog_generation_name(enum adreno_generation gen);

/* 1 iff all three firmware bits are present, else 0. */
int adreno_catalog_firmware_complete(unsigned firmware_mask);
/* The subset of ADRENO_FW_ALL bits that are missing. */
unsigned adreno_catalog_missing_firmware(unsigned firmware_mask);

/* Feasibility gate for a full Metal path. firmware_mask = ADRENO_FW_* bits
 * actually present; backend_available = 1 only when a matching-generation
 * kernel backend + compiler exist (sheng's is A740-only, so 0 for A630).
 * Returns an OR of ADRENO_BLOCK_* flags; ADRENO_FEASIBLE means no blocker.
 * enchilada today: lookup(A630), firmware=0, backend=0 -> 0x06 (both). */
unsigned adreno_catalog_assess(uint32_t chip_id, unsigned firmware_mask,
                               int backend_available);

/* Read-only sanity: does a raw chip-id reading equal the DTB qcom,chipid for
 * the A630? The register offset is intentionally unspecified (see header). */
int adreno_catalog_chipid_matches_a630(uint32_t raw_chip_id);

#ifdef __cplusplus
}
#endif
#endif
