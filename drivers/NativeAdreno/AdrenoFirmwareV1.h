// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoFirmwareV1 -- enchilada stage 5 (GPU) HOST-VERIFIED A630 LEGACY GMU
// firmware-load contract. Round 33 (2026-11-10).
//
// This is NOT a GPU driver and NOT firmware. It is a freestanding, pure-C
// encoding of the A630 (6xx GEN1, legacy GMU) firmware-loading rules that a
// future A6XX_GEN1 kernel backend must obey. It maps nothing, writes no
// register, loads no image, and is NOT linked to any kernel caller.
//
// Source provenance (sha256-pinned Linux v6.12, cache
// .cache/adreno-a630-source/, see scripts/data/adreno-a630-inputs.json):
//   a6xx_gmu.c:708-714  struct block_header { u32 addr,size,type,value; u32 data[]; };
//               -> the MODERN segmented image has a 16-byte header per block.
//   a6xx_gmu.c:725-779  a6xx_gmu_fw_load(): for gmu->legacy,
//               "if (fw_image->size > 0x8000) return -EINVAL;"
//               gmu_write_bulk(..., REG_A6XX_GMU_CM3_ITCM_START,
//                              (u32*)fw_image->data, fw_image->size);
//               -> A630 uses ONE flat ITCM bulk write, NO block_header parse.
//   a6xx_gmu.c:733-734  itcm_base=0x00000000, dtcm_base=0x00040000.
//   a6xx_gmu.c:781-829  a6xx_gmu_fw_start() cold path: rpmh_start ->
//               gmu_fw_load -> CM3_FW_INIT_RESULT=0 -> CM3_BOOT_CONFIG=0x02 ->
//               HFI_QTBL_ADDR=hfi.iova -> HFI_QTBL_INFO=1.
//   a6xx_gmu.xml:45-46  GMU_CM3_ITCM_START offset 0xc00,
//               GMU_CM3_DTCM_START offset 0x1c00 (word offsets).
//   a6xx_gpu.c:17       #define GPU_PAS_ID 13.
//   a6xx_gpu.c:694-759  a6xx_ucode_check_version(): A630 SQE whereami gate
//               (((buf[0]&0xf)==0xa && (buf[2]&0xf)>=1) || (buf[0]&0xfff)>=0x190).
//   a6xx_catalog.c:883-903  .zapfw="a630_zap.mdt" (signed split chain);
//               :1047-1049  MODULE_FIRMWARE("qcom/a630_zap.mbn") (public single).
//
// Upstream sheng contrast (work/UniXNU/kits/AdrenoFirmware/AdrenoFirmware.c):
//   AdrenoFirmwarePlanGmu() parses the MODERN 16-byte block_header image into
//   ITCM/DTCM/icache/dcache/dummy regions. Its README says "GMU cold start and
//   HFI work on sheng hardware." It does NOT cover the A630 legacy flat ITCM.
//
// Honest boundaries (2026-11-10, load-bearing):
//   - Device is offline; NOTHING here is device-verified. gpu stays
//     host-verified. A CI green build+link is a build artifact, not a measurement.
//   - The 3 public blob sizes/sha256 below are OBSERVATIONS from ONE reachable
//     mirror (endlessm/linux-firmware@master), cross-checked by two network
//     paths (jsdelivr + GitHub API base64). They are NOT asserted canonical or
//     official: several other mirrors were unreachable in this environment.
//   - The raw a630_sqe.fw dword[0]==0 observation is NOT a verdict: upstream
//     a6xx_ucode_check_version() reads the MAPPED BO via msm_gem_get_vaddr()
//     after adreno_fw_create_bo() may relocate/parse it. This module only
//     implements the pure predicate; a caller applies it to the real image.
//   - The vendor-signed ZAP .mdt/.b00/.b01 chain (GPU_PAS_ID 13) is ABSENT.
//     adreno_fw_v1_ready() is FAIL-CLOSED and returns 0 today.
//   - Round 22/28 power-up gate is unchanged: no cold MMIO to 0x5000000 /
//     0x506a000 / 0x5040000. First legal device action is a GATED read of
//     0x05091540 / 0x05091010 after the full power sequence.

#ifndef ADRENO_FIRMWARE_V1_H
#define ADRENO_FIRMWARE_V1_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Legacy GMU load: a6xx_gmu.c:739-750 rejects an image bigger than the ITCM
 * region and writes the WHOLE image as ONE flat bulk write. 0x8000 == 32768,
 * which is exactly the observed a630_gmu.bin size. */
#define ADRENO_FW_V1_GMU_MAX_BYTES        0x8000u
#define ADRENO_FW_V1_ITCM_BASE            0x00000000u
#define ADRENO_FW_V1_DTCM_BASE            0x00040000u
#define ADRENO_FW_V1_ITCM_START_REG       0x0c00u
#define ADRENO_FW_V1_DTCM_START_REG       0x1c00u
/* MODERN-only: sheng AdrenoFirmwarePlanGmu() walks 16-byte block_header units.
 * The legacy path NEVER parses this. Recorded for the contrast, not for use. */
#define ADRENO_FW_V1_MODERN_BLOCK_HEADER_BYTES 16u

/* a6xx_gmu_fw_start() cold-boot register writes (a6xx_gmu.c:821-826). */
#define ADRENO_FW_V1_BOOT_CONFIG_VALUE    0x02u
#define ADRENO_FW_V1_HFI_QTBL_INFO        0x01u

/* a6xx_gpu.c:17 / :811-823: zap shader is loaded through TrustZone PAS id 13. */
#define ADRENO_FW_V1_GPU_PAS_ID           13u

/* a6xx_gpu.c:731-736: A630 SQE whereami version predicate. */
#define ADRENO_FW_V1_SQE_WHEREAMI_MIN_VERSION 0x190u
#define ADRENO_FW_V1_SQE_PATCH_MARKER     0xau

#define ADRENO_FW_V1_ROLE_COUNT           3u

/* Observed public blob sizes (OBSERVATIONS, not canonical; see
 * scripts/data/adreno-a630-firmware.json + the header comment above). */
#define ADRENO_FW_V1_SQE_BYTES            34188u
#define ADRENO_FW_V1_GMU_BYTES            32768u
#define ADRENO_FW_V1_ZAP_MBN_BYTES        14256u
#define ADRENO_FW_V1_SQE_SHA256   "1c21b527d9183487cc550dabbb3f43e555df5a977a461934fc61f0635a9aa90c"
#define ADRENO_FW_V1_GMU_SHA256   "da8d9b1b1f5c1a0b311f32567093b4828f3c80031dd8435f91ac13c664e173a6"
#define ADRENO_FW_V1_ZAP_MBN_SHA256 "4aef4aff842b7db4f33faaa21e8d8281df1b0c4f09db0b0ee98f6eba9e9b43cd"

/* blobs_present bitmask (bit i == role i). */
#define ADRENO_FW_V1_PRESENT_ZAP  (1u << 0)
#define ADRENO_FW_V1_PRESENT_GMU  (1u << 1)
#define ADRENO_FW_V1_PRESENT_SQE  (1u << 2)
#define ADRENO_FW_V1_PRESENT_ALL  0x7u

enum adreno_fw_v1_role {
    kAdrenoFwV1RoleZap = 0,
    kAdrenoFwV1RoleGmu = 1,
    kAdrenoFwV1RoleSqe = 2
};

enum adreno_fw_v1_status {
    kAdrenoFwV1Success = 0,
    kAdrenoFwV1Invalid = 1,   /* NULL argument */
    kAdrenoFwV1TooLarge = 2,  /* image > 0x8000 */
    kAdrenoFwV1Unaligned = 3, /* size not a multiple of 4 (bulk writes u32) */
    kAdrenoFwV1Empty = 4      /* size == 0 */
};

struct adreno_fw_v1_blob {
    const char *public_name;     /* MODULE_FIRMWARE name */
    const char *sha256;          /* observed mirror hash, 64 lowercase hex */
    uint32_t bytes;              /* observed size */
    uint8_t  needs_signed_chain; /* 1: driver needs .mdt/.b00/.b01 + PAS 13 */
};

struct adreno_fw_v1_gmu_plan {
    uint32_t itcm_base;
    uint32_t itcm_reg;
    uint32_t bytes;
    uint32_t bulk_words;
    uint8_t  is_flat;            /* 1 == legacy single bulk write, NO header */
};

struct adreno_fw_v1_state {
    uint8_t power_ready;         /* CX/GX genpd + 6 clocks + RPMh done */
    uint8_t gmu_plan_valid;      /* plan_gmu_legacy succeeded on the real image */
    uint8_t blobs_present;       /* ADRENO_FW_V1_PRESENT_* bits */
    uint8_t zap_signed_chain;    /* vendor signed ZAP + PAS auth succeeded */
    uint8_t sqe_whereami;        /* SQE version predicate passed on mapped BO */
    uint8_t kernel_caller;       /* A6XX_GEN1 backend instantiates the transport */
};

/* 1: enchilada A630 is a legacy GMU (a6xx_gmu.c:1736-1738). */
int adreno_fw_v1_is_legacy_gmu(void);
uint32_t adreno_fw_v1_gmu_max_bytes(void);
uint32_t adreno_fw_v1_modern_block_header_bytes(void);

/* Build the LEGACY flat-ITCM load plan. image/bytes are the raw GMU blob;
 * on any refusal *out is zeroed. This NEVER parses a block_header. */
enum adreno_fw_v1_status adreno_fw_v1_plan_gmu_legacy(const uint8_t *image,
        size_t bytes, struct adreno_fw_v1_gmu_plan *out);
int adreno_fw_v1_plan_is_flat(const struct adreno_fw_v1_gmu_plan *plan);

/* Pure a6xx_gpu.c:731-736 predicate. buf is the MAPPED SQE BO vaddr (dwords).
 * NULL -> 0. This is a predicate, NOT a verdict on the raw mirror file. */
int adreno_fw_v1_sqe_whereami(const uint32_t *buf);

/* Static blob table (invalid role -> NULL). */
const struct adreno_fw_v1_blob *adreno_fw_v1_blob(enum adreno_fw_v1_role role);
/* 1 iff (role, bytes, sha256) equals the recorded observation. */
int adreno_fw_v1_blob_matches(enum adreno_fw_v1_role role, uint32_t bytes,
        const char *sha256);
int adreno_fw_v1_zap_requires_signed_chain(void);

/* All-zero state for the FAIL-CLOSED gate. */
struct adreno_fw_v1_state adreno_fw_v1_default_state(void);
/* Returns 1 ONLY when every precondition is met; 0 today (no device, no
 * signed ZAP, no kernel caller). NULL -> 0. */
int adreno_fw_v1_ready(const struct adreno_fw_v1_state *state);

#ifdef __cplusplus
}
#endif
#endif
