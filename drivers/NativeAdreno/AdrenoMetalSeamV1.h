// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoMetalSeamV1 -- enchilada stage 5 (GPU) HOST-VERIFIED Metal
// establishment-chain seam study.
//
// This is NOT a Metal GPU driver. It is a freestanding, pure-C mirror of the
// GENERATION-INDEPENDENT kernel/user-client wire ABI that UniXNU's Turnip
// "darwin" KMD compiles against, plus an honest encoding of (a) which
// DeviceInfo fields are sha256-pinned for enchilada's A630, (b) which MUST
// stay unspecified until board code derives them, and (c) which stages of the
// upstream Metal establishment chain are blocked on this device today.
// It maps nothing, writes no register, loads no firmware, opens no user
// client, submits no command, and is NOT an IOService. HOST-VERIFIED only:
// pinned sources + host tests, no device run, no CI flash.
//
// WHY THIS EXISTS (user m44634 / m44638 / m44639 / m44640 / m44642 / m44646)
// --------------------------------------------------------------------------
// The upstream Metal stack splits into TWO layers:
//   (1) generation-independent: the versioned fixed-record wire ABI
//       (NativeAdrenoABI.h), the OM_* / bridge / IOGPU class shapes, the
//       NGPUADD2 payload + owner-trust staging, and the AIR->SPIR-V->NIR->IR3
//       compiler chain; and
//   (2) A740/7xx-specific: chip 0x43050a01, gmu_gen70200.bin / a740_sqe.fw /
//       a740_zap.mbn, 3 MiB GMEM, the 54/46 hwcg/protect tables, A7xx CP
//       framing and the 2 GiB aperture windows.
// docs/porting/gpu section 1 is decisive: "For another GPU generation,
// implement the matching kernel backend and compiler support before selecting
// its frontend." enchilada is A630 (6xx GEN1, legacy GMU HFI v1 / SPTPRAC),
// NOT A740. Mesa Turnip DOES already carry A630 (GPUId(630), a6xx_gen1) so
// the compiler is NOT the hard blocker; the blockers are the A6XX_GEN1 kernel
// backend, TrustZone PAS-signed ZAP, the kgsl SMMU stage-1 context, the
// selected OS Metal/IOSurface ABI, and the macOS/Xcode/codesign toolchain.
//
// PROVENANCE (all sha256 pins re-checked by scripts/verify-enchilada-metal-seam.py)
// -------------------------------------------------------------------------------
//  (P1) oneplus6-ios/work/UniXNU/drivers/Adreno740/NativeAdrenoABI.h
//       7733 bytes, sha256
//       20ac9298bb390d86788c1fc58155e1f9581ffa942e222752efc95c67cb5670d2
//       :6-12 "Versioned kernel/user-client wire ABI ... NOT the Metal, IOKit
//       or Turnip ABI ... Unknown versions, methods and flags are refused
//       rather than ignored."  :13-35 version=2, MaxBuffers=128,
//       MaxBoBytesShift=28, methods 1..16, MetadataBytes=64, MaxQueues=8.
//       :127-128 DeviceInfo "all come from the board code, which takes them
//       from the pinned sources (a6xx catalog, UBWC config, the programmed
//       UCHE ranges), never from a guess."  :147-163 sixteen layout asserts.
//  (P2) .cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_catalog.c
//       pinned in scripts/data/adreno-a630-inputs.json, sha256 6d9061ee...
//       :883-903 the A630 entry: chip_ids 0x06030001/0x06030002,
//       ADRENO_6XX_GEN1, revn 630, gmem SZ_1M, zapfw "a630_zap.mdt",
//       NO address_space_size (platform default).
//  (P3) Mu-Silicium/Resources/DTBs/enchilada.dts, 784857 bytes, sha256
//       e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275
//       (local copy oneplus6-ios/notes/mu-silicium/enchilada.dts)
//       :23641 qcom,chipid=<0x6030001>, :23647 qcom,highest-bank-bit=<0x0f>,
//       :23649 qcom,ubwc-mode=<0x02>, :23612 qcom,pas-id=<0x0d>,
//       :23613 qcom,firmware-name="a630_zap".
//  (P4) oneplus6-ios/work/UniXNU/docs/porting/gpu.md :22-28 section 1.
//  (P5) Mesa freedreno_devices.py (broad lookup, gitlab.freedesktop.org):
//       add_gpus([GPUId(630)], A6xxGPUInfo(CHIP.A6XX, [a6xx_base, a6xx_gen1],
//       ...)) => A630 compiler support exists upstream.
//
// HONEST BOUNDARY
// ---------------
//  Wire ABI mirror = HOST-VERIFIED fidelity to the pinned upstream header,
//  NOT a claim that any kernel user client exists on enchilada. The A630
//  DeviceInfo fields left unspecified are deliberate: setting gmem_base,
//  uche_trap_base, va_start, va_size, ubwc_swizzle, macrotile_mode,
//  ubwc_version or flags from a guess would violate P1:127-128. A real
//  A6XX_GEN1 backend must derive them from the programmed UCHE ranges and
//  the board's UBWC config before any frontend can be selected.
//  The 5 establishment blockers are current-state facts, not a promise.

#ifndef ADRENO_METAL_SEAM_V1_H
#define ADRENO_METAL_SEAM_V1_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- P1 generation-independent wire ABI (faithful mirror) ---------------- */

enum {
    kEnchiladaMetalAbiVersion       = 2,
    kEnchiladaMetalAbiMaxBuffers    = 128,  /* a frame's IBs in one submission */
    kEnchiladaMetalAbiMaxBoBytesShift = 28, /* 256 MiB per object */
    kEnchiladaMetalAbiMetadataBytes = 64,
    kEnchiladaMetalAbiMaxQueues     = 8
};

enum {
    kEnchiladaMetalParamsHostImport = 1     /* BoImport pins the caller's pages */
};

enum enchilada_metal_method {
    kEnchiladaMetalMethodQuery         = 1,
    kEnchiladaMetalMethodBoAlloc       = 2,
    kEnchiladaMetalMethodBoMap         = 3,
    kEnchiladaMetalMethodBoFree        = 4,
    kEnchiladaMetalMethodSubmit        = 5,
    kEnchiladaMetalMethodWaitFence     = 6,
    kEnchiladaMetalMethodGetTimestamp  = 7,
    kEnchiladaMetalMethodGetStatus     = 8,
    kEnchiladaMetalMethodSetMetadata   = 9,
    kEnchiladaMetalMethodGetMetadata   = 10,
    kEnchiladaMetalMethodCreateQueue   = 11,
    kEnchiladaMetalMethodCloseQueue    = 12,
    kEnchiladaMetalMethodBoCpuMap      = 13,
    kEnchiladaMetalMethodBoCpuUnmap    = 14,
    kEnchiladaMetalMethodGetDeviceInfo = 15,
    kEnchiladaMetalMethodBoImport      = 16
};

/* Fixed-size records copied across external methods. Little endian, ARM64. */
struct enchilada_metal_abi_params {
    uint32_t abi_version;
    uint32_t input_bits;
    uint32_t output_bits;
    uint32_t flags;
    uint64_t max_bo_bytes;
};
struct enchilada_metal_abi_bo_alloc {
    uint64_t bytes;
    uint32_t align_log2;
    uint32_t contiguous;
};
struct enchilada_metal_abi_bo_alloc_reply { uint64_t handle; };
struct enchilada_metal_abi_bo_import {
    uint64_t address;
    uint64_t bytes;
};
struct enchilada_metal_abi_bo_map {
    uint64_t handle;
    uint64_t iova;
    uint32_t permissions;
    uint32_t writeback;
    uint32_t shareability;
    uint32_t reserved;
};
struct enchilada_metal_abi_bo_map_reply { uint64_t iova; };
struct enchilada_metal_abi_bo_free { uint64_t handle; };
struct enchilada_metal_abi_submit_buffer {
    uint64_t handle;
    uint64_t iova;
    uint64_t bytes;
};
struct enchilada_metal_abi_submit {
    uint32_t buffer_count;
    uint32_t reserved;
    uint64_t queue;
    uint64_t wait_fence;
    uint64_t wait_timeout_ns;
    struct enchilada_metal_abi_submit_buffer buffers[kEnchiladaMetalAbiMaxBuffers];
};
struct enchilada_metal_abi_metadata {
    uint64_t handle;
    uint32_t bytes;
    uint32_t reserved;
    char text[kEnchiladaMetalAbiMetadataBytes];
};
struct enchilada_metal_abi_submit_reply { uint64_t fence; };
struct enchilada_metal_abi_wait_fence {
    uint64_t fence;
    uint64_t timeout_ns;
};
struct enchilada_metal_abi_timestamp { uint64_t ticks; };
struct enchilada_metal_abi_bo_cpu_map { uint64_t handle; };
struct enchilada_metal_abi_bo_cpu_map_reply {
    uint32_t token;
    uint32_t reserved;
    uint64_t bytes;
};
struct enchilada_metal_abi_bo_cpu_unmap {
    uint64_t handle;
    uint32_t token;
    uint32_t reserved;
};
struct enchilada_metal_abi_queue { uint64_t queue; };
struct enchilada_metal_abi_queue_reply { uint64_t queue; };
struct enchilada_metal_abi_status {
    uint64_t faults;
    uint64_t suspends;
};

enum {
    kEnchiladaMetalDeviceClientIova    = 1,
    kEnchiladaMetalDeviceCachedCoherent = 2
};

/* The ONLY chip-specific injection point of the whole wire ABI (P1:127-146). */
struct enchilada_metal_abi_device_info {
    uint32_t chip_id;
    uint32_t gmem_bytes;
    uint64_t gmem_base;
    uint64_t uche_trap_base;
    uint64_t va_start;
    uint64_t va_size;
    uint32_t highest_bank_bit;
    uint32_t ubwc_swizzle;
    uint32_t macrotile_mode;
    uint32_t ubwc_version;
    uint32_t flags;
    uint32_t reserved;
};

#if defined(__cplusplus)
/* The sixteen layout asserts of P1:147-163, verbatim sizes. */
static_assert(sizeof(struct enchilada_metal_abi_params) == 24, "ABI params layout");
static_assert(sizeof(struct enchilada_metal_abi_bo_alloc) == 16, "ABI alloc layout");
static_assert(sizeof(struct enchilada_metal_abi_bo_map) == 32, "ABI map layout");
static_assert(sizeof(struct enchilada_metal_abi_bo_import) == 16, "ABI import layout");
static_assert(sizeof(struct enchilada_metal_abi_submit_buffer) == 24, "ABI buffer layout");
static_assert(sizeof(struct enchilada_metal_abi_submit) ==
              32 + 24 * kEnchiladaMetalAbiMaxBuffers, "ABI submit layout");
static_assert(sizeof(struct enchilada_metal_abi_bo_cpu_map) == 8, "ABI cpu map layout");
static_assert(sizeof(struct enchilada_metal_abi_bo_cpu_map_reply) == 16, "ABI cpu map reply layout");
static_assert(sizeof(struct enchilada_metal_abi_bo_cpu_unmap) == 16, "ABI cpu unmap layout");
static_assert(sizeof(struct enchilada_metal_abi_queue) == 8, "ABI queue layout");
static_assert(sizeof(struct enchilada_metal_abi_queue_reply) == 8, "ABI queue reply layout");
static_assert(sizeof(struct enchilada_metal_abi_metadata) == 80, "ABI metadata layout");
static_assert(sizeof(struct enchilada_metal_abi_wait_fence) == 16, "ABI wait layout");
static_assert(sizeof(struct enchilada_metal_abi_timestamp) == 8, "ABI timestamp layout");
static_assert(sizeof(struct enchilada_metal_abi_status) == 16, "ABI status layout");
static_assert(sizeof(struct enchilada_metal_abi_device_info) == 64, "ABI device info layout");
/* Four reply records P1 defines but does not assert; asserted here for a full mirror. */
static_assert(sizeof(struct enchilada_metal_abi_bo_alloc_reply) == 8, "ABI alloc reply layout");
static_assert(sizeof(struct enchilada_metal_abi_bo_map_reply) == 8, "ABI map reply layout");
static_assert(sizeof(struct enchilada_metal_abi_bo_free) == 8, "ABI free layout");
static_assert(sizeof(struct enchilada_metal_abi_submit_reply) == 8, "ABI submit reply layout");
#endif

/* ---- P2/P3 A630 board-pinned facts --------------------------------------- */
#define ENCHILADA_METAL_A630_CHIP_ID          0x06030001u  /* DTB :23641, catalog :883-886 */
#define ENCHILADA_METAL_A630_CHIP_ID_ALT      0x06030002u  /* catalog :885 */
#define ENCHILADA_METAL_A630_GMEM_BYTES       0x00100000u  /* SZ_1M, catalog :893 */
#define ENCHILADA_METAL_A630_HIGHEST_BANK_BIT 15u          /* DTB :23647 (0x0f) */
#define ENCHILADA_METAL_A630_UBWC_MODE        2u           /* DTB :23649 (raw board fact) */
#define ENCHILADA_METAL_A630_ZAP_PAS_ID       13u          /* DTB :23612 (0x0d) */

/* Every DeviceInfo field a backend may map. "reserved" is always zero and is
 * deliberately not a field here. */
enum enchilada_metal_device_field {
    kEnchiladaMetalFieldChipId          = 1u << 0,
    kEnchiladaMetalFieldGmemBytes       = 1u << 1,
    kEnchiladaMetalFieldGmemBase        = 1u << 2,
    kEnchiladaMetalFieldUcheTrapBase    = 1u << 3,
    kEnchiladaMetalFieldVaStart         = 1u << 4,
    kEnchiladaMetalFieldVaSize          = 1u << 5,
    kEnchiladaMetalFieldHighestBankBit  = 1u << 6,
    kEnchiladaMetalFieldUbwcSwizzle     = 1u << 7,
    kEnchiladaMetalFieldMacrotileMode   = 1u << 8,
    kEnchiladaMetalFieldUbwcVersion     = 1u << 9,
    kEnchiladaMetalFieldFlags           = 1u << 10
};
#define ENCHILADA_METAL_DEVICE_FIELD_ALL      0x000007ffu
/* C enum enumerators have type int, so force unsigned masks to keep
 * -Wsign-conversion clean under a strict freestanding C11 build. */
/* Only three fields are sha256-pinned for the A630 today. */
#define ENCHILADA_METAL_A630_SPECIFIED_FIELDS \
    ((uint32_t)(kEnchiladaMetalFieldChipId | kEnchiladaMetalFieldGmemBytes | \
                kEnchiladaMetalFieldHighestBankBit))
/* The other eight MUST be derived by board code; never guessed (P1:127-128). */
#define ENCHILADA_METAL_A630_BOARD_DERIVED_FIELDS \
    ((uint32_t)(ENCHILADA_METAL_DEVICE_FIELD_ALL & \
                ~(uint32_t)ENCHILADA_METAL_A630_SPECIFIED_FIELDS))

/* ---- Upstream establishment chain (P4 + broad source audit) -------------- */
enum enchilada_metal_stage {
    kEnchiladaMetalStageFetch          = 0, /* sha-pinned source/firmware inputs */
    kEnchiladaMetalStageBridge         = 1, /* metal2vulkan Rust staticlib (Apple target) */
    kEnchiladaMetalStageTurnip         = 2, /* Mesa freedreno darwin-KMD dylib (iOS cross) */
    kEnchiladaMetalStagePlugin         = 3, /* owner-signed Metal bundle + daemon */
    kEnchiladaMetalStageTrust          = 4, /* owner trust module (CDHashes) */
    kEnchiladaMetalStageSeed           = 5, /* .dbr Data seed / NGPUADD2 gpu-addon */
    kEnchiladaMetalStageKernelBackend  = 6, /* generation-matching KMD archive */
    kEnchiladaMetalStageAccelerator    = 7, /* staged IOAcceleratorES late archive */
    kEnchiladaMetalStageDiscovery      = 8, /* backboardd MetalPluginName, .publish/.clients */
    kEnchiladaMetalStageCount          = 9
};

enum enchilada_metal_blocker {
    kEnchiladaMetalBlockerMacosToolchain = 1u << 0, /* xcrun/codesign/hdiutil/ObjC++ */
    kEnchiladaMetalBlockerGen1Backend    = 1u << 1, /* A6XX_GEN1 KMD (section 1) */
    kEnchiladaMetalBlockerSignedZap      = 1u << 2, /* TrustZone PAS + ZAP .mdt/.bNN */
    kEnchiladaMetalBlockerSmmuContext    = 1u << 3, /* kgsl SMMU stage-1 context */
    kEnchiladaMetalBlockerOsAbi          = 1u << 4  /* selected Metal/IOSurface ABI */
};
#define ENCHILADA_METAL_TODAY_BLOCKERS       0x0000001fu

/* ---- API ------------------------------------------------------------------ */

/* P1:13-35. */
uint32_t enchilada_metal_abi_version(void);
/* Unknown methods are REFUSED, not ignored (P1:11-12). */
int enchilada_metal_method_is_known(uint32_t method);
const char *enchilada_metal_method_name(uint32_t method);
/* Version 2, flags 0 or HostImport(1), 0 < max_bo_bytes <= 1<<28. */
int enchilada_metal_abi_params_valid(const struct enchilada_metal_abi_params *p);

/* Fills ONLY the three A630-pinned DeviceInfo fields, zeroes everything else,
 * and reports which fields it filled. A NULL out is a no-op. */
void enchilada_metal_device_info_a630(struct enchilada_metal_abi_device_info *out,
                                      uint32_t *specified_mask);
uint32_t enchilada_metal_device_info_specified_fields(void);
uint32_t enchilada_metal_device_info_board_derived_fields(void);
/* 1 iff the record is honest: exactly the pinned fields filled, their values
 * equal the pins, and NO board-derived field was fabricated. 0 otherwise. */
int enchilada_metal_device_info_consistent(const struct enchilada_metal_abi_device_info *info,
                                           uint32_t specified_mask);

/* Establishment chain: names, per-stage blockers, today's union, and the
 * section-1 frontend gate. */
const char *enchilada_metal_stage_name(uint32_t stage);
const char *enchilada_metal_blocker_name(uint32_t bit);
uint32_t enchilada_metal_stage_blockers(uint32_t stage);
uint32_t enchilada_metal_establishment_blockers(void);
/* Mesa Turnip carries GPUId(630) / a6xx_gen1 (P5): the compiler is NOT the
 * hard blocker. HOST-VERIFIED source fact, not a device measurement. */
int enchilada_metal_compiler_a630_available(void);
/* docs/porting/gpu section 1: a frontend may be selected only after a
 * matching-generation backend AND compiler support exist. Here the five
 * prerequisites are made explicit and all must be nonzero. */
int enchilada_metal_frontend_allowed(int gen1_backend, int compiler_a630,
                                     int signed_zap, int smmu_context, int os_abi);

#ifdef __cplusplus
}
#endif
#endif
