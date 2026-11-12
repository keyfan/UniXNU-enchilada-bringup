/* SPDX-License-Identifier: MIT */
/* ROUND 66 - A630 Darwin-KMD ABI DIRECTORY CONTRACT (HOST-VERIFIED).
 *
 * This directory is a candidate for meson's -Dtu-darwin-abi-dir=<dir> when
 * building Mesa Turnip with -Dfreedreno-kmds=darwin for enchilada (SDM845,
 * Adreno A630). It is NOT a GPU driver: it defines the wire ABI only.
 *
 * WHY THIS IS A VALID DROP-IN:
 *   oneplus6-ios/work/UniXNU/patches/mesa/turnip-darwin-kmd.patch:290 adds
 *       #include "NativeAdrenoABI.h"
 *   to tu_knl_darwin.cc and :52-59 points include_directories() at whatever
 *   directory the caller passes. That is the ONLY project header Turnip takes
 *   from the directory - the other 30 NativeAdreno*.h files in
 *   drivers/Adreno740/ are kernel-backend internals, not compiler inputs.
 *   The wire ABI is generation-independent, so the A630 board must publish
 *   the SAME symbol names and record layouts the upstream A740 board does.
 *   This file therefore mirrors oneplus6-ios/work/UniXNU/drivers/Adreno740/
 *   NativeAdrenoABI.h verbatim in names, values and 16 static_asserts.
 *
 * WHAT IS NEW FOR A630 (the only chip-specific injection point, :127-146):
 *   Four DeviceInfo fields are sha256-pinned and may be filled by board code;
 *   the other seven MUST be derived by board code and are left ZERO here.
 *   Upstream :127-128 is explicit: "all come from the board code, which takes
 *   them from the pinned sources (a6xx catalog, UBWC config, the programmed
 *   UCHE ranges), never from a guess."
 *
 * SHA256-PINNED SOURCES (all re-verified this round at the EXACT build pin):
 *   P1 oneplus6-ios/work/UniXNU/drivers/Adreno740/NativeAdrenoABI.h
 *      7733 B 20ac9298bb390d86788c1fc58155e1f9581ffa942e222752efc95c67cb5670d2
 *   P2 .cache/adreno-a630-source/freedreno_devices.85fda3b3.py
 *      53914 B 3a343e614b30509a01db4a22380eaa4604abafbb59552cd8b177c5912b29aef6
 *      Mesa commit 85fda3b36405bcaea8bffc64aa4f2651dc90770d (the commit
 *      scripts/data/adreno-compiler-inputs.json actually builds, NOT main).
 *      Two independent web_fetch reads of the GitLab /-/raw/ and
 *      /api/v4/.../raw endpoints both returned HTTP 200 with this content.
 *   P3 oneplus6-ios/notes/mu-silicium/enchilada.dts
 *      784857 B e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275
 *   P4 .cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_catalog.c
 *      51692 B 6d9061ee08308126adc7b7250810b0223ee01c678e7afab6eb1738e53524c24c
 *   P5 oneplus6-ios/work/UniXNU/scripts/data/adreno-compiler-inputs.json
 *      mesa.commit = 85fda3b36405bcaea8bffc64aa4f2651dc90770d
 *      patches[] includes "patches/mesa/turnip-darwin-kmd.patch"
 *
 * HONEST BOUNDARY: HOST-VERIFIED source/layout work. No device run, no flash,
 * no MMIO, no firmware load, no MTLDevice, no IOKit user client, no Turnip
 * dylib was built (xcrun/codesign/hdiutil require macOS, absent here). The
 * four hard blockers of docs/porting/gpu.md section 1 remain open. The gpu
 * area of porting.toml STAYS "host-verified".
 */
#ifndef NATIVE_ADRENO_ABI_H
#define NATIVE_ADRENO_ABI_H
#include <stdint.h>

/* ---- Generation-independent wire ABI: names/values mirror P1:13-146 ------ */
enum {
    NativeAdrenoAbiVersion = 2,
    NativeAdrenoAbiMaxBuffers = 128,
    NativeAdrenoAbiMaxBoBytesShift = 28, /* 256MiB per object */
    NativeAdrenoMethodQuery = 1,
    NativeAdrenoMethodBoAlloc = 2,
    NativeAdrenoMethodBoMap = 3,
    NativeAdrenoMethodBoFree = 4,
    NativeAdrenoMethodSubmit = 5,
    NativeAdrenoMethodWaitFence = 6,
    NativeAdrenoMethodGetTimestamp = 7,
    NativeAdrenoMethodGetStatus = 8,
    NativeAdrenoMethodSetMetadata = 9,
    NativeAdrenoMethodGetMetadata = 10,
    NativeAdrenoMethodCreateQueue = 11,
    NativeAdrenoMethodCloseQueue = 12,
    NativeAdrenoMethodBoCpuMap = 13,
    NativeAdrenoMethodBoCpuUnmap = 14,
    NativeAdrenoMethodGetDeviceInfo = 15,
    NativeAdrenoMethodBoImport = 16,
    NativeAdrenoAbiMetadataBytes = 64,
    NativeAdrenoAbiMaxQueues = 8
};
struct NativeAdrenoAbiParams {
    uint32_t abi_version;
    uint32_t input_bits;
    uint32_t output_bits;
    uint32_t flags;
    uint64_t max_bo_bytes;
};
enum {
    NativeAdrenoParamsHostImport = 1
};
struct NativeAdrenoAbiBoAlloc {
    uint64_t bytes;
    uint32_t align_log2;
    uint32_t contiguous;
};
struct NativeAdrenoAbiBoAllocReply {
    uint64_t handle;
};
struct NativeAdrenoAbiBoImport {
    uint64_t address;
    uint64_t bytes;
};
struct NativeAdrenoAbiBoMap {
    uint64_t handle;
    uint64_t iova;
    uint32_t permissions;
    uint32_t writeback;
    uint32_t shareability;
    uint32_t reserved;
};
struct NativeAdrenoAbiBoMapReply {
    uint64_t iova;
};
struct NativeAdrenoAbiBoFree {
    uint64_t handle;
};
struct NativeAdrenoAbiSubmitBuffer {
    uint64_t handle;
    uint64_t iova;
    uint64_t bytes;
};
struct NativeAdrenoAbiSubmit {
    uint32_t buffer_count;
    uint32_t reserved;
    uint64_t queue;
    uint64_t wait_fence;
    uint64_t wait_timeout_ns;
    struct NativeAdrenoAbiSubmitBuffer buffers[NativeAdrenoAbiMaxBuffers];
};
struct NativeAdrenoAbiMetadata {
    uint64_t handle;
    uint32_t bytes;
    uint32_t reserved;
    char text[NativeAdrenoAbiMetadataBytes];
};
struct NativeAdrenoAbiSubmitReply {
    uint64_t fence;
};
struct NativeAdrenoAbiWaitFence {
    uint64_t fence;
    uint64_t timeout_ns;
};
struct NativeAdrenoAbiTimestamp {
    uint64_t ticks;
};
struct NativeAdrenoAbiBoCpuMap {
    uint64_t handle;
};
struct NativeAdrenoAbiBoCpuMapReply {
    uint32_t token;
    uint32_t reserved;
    uint64_t bytes;
};
struct NativeAdrenoAbiBoCpuUnmap {
    uint64_t handle;
    uint32_t token;
    uint32_t reserved;
};
struct NativeAdrenoAbiQueue {
    uint64_t queue;
};
struct NativeAdrenoAbiQueueReply {
    uint64_t queue;
};
struct NativeAdrenoAbiStatus {
    uint64_t faults;
    uint64_t suspends;
};
enum {
    NativeAdrenoDeviceClientIova = 1,
    NativeAdrenoDeviceCachedCoherent = 2
};
struct NativeAdrenoAbiDeviceInfo {
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

/* The sixteen layout asserts of P1:147-163, verbatim sizes. */
#if defined(__cplusplus)
static_assert(sizeof(struct NativeAdrenoAbiParams) == 24, "ABI params layout");
static_assert(sizeof(struct NativeAdrenoAbiBoAlloc) == 16, "ABI alloc layout");
static_assert(sizeof(struct NativeAdrenoAbiBoMap) == 32, "ABI map layout");
static_assert(sizeof(struct NativeAdrenoAbiBoImport) == 16, "ABI import layout");
static_assert(sizeof(struct NativeAdrenoAbiSubmitBuffer) == 24, "ABI buffer layout");
static_assert(sizeof(struct NativeAdrenoAbiSubmit) == 32 + 24 * NativeAdrenoAbiMaxBuffers, "ABI submit layout");
static_assert(sizeof(struct NativeAdrenoAbiBoCpuMap) == 8, "ABI cpu map layout");
static_assert(sizeof(struct NativeAdrenoAbiBoCpuMapReply) == 16, "ABI cpu map reply layout");
static_assert(sizeof(struct NativeAdrenoAbiBoCpuUnmap) == 16, "ABI cpu unmap layout");
static_assert(sizeof(struct NativeAdrenoAbiQueue) == 8, "ABI queue layout");
static_assert(sizeof(struct NativeAdrenoAbiQueueReply) == 8, "ABI queue reply layout");
static_assert(sizeof(struct NativeAdrenoAbiMetadata) == 80, "ABI metadata layout");
static_assert(sizeof(struct NativeAdrenoAbiWaitFence) == 16, "ABI wait layout");
static_assert(sizeof(struct NativeAdrenoAbiTimestamp) == 8, "ABI timestamp layout");
static_assert(sizeof(struct NativeAdrenoAbiStatus) == 16, "ABI status layout");
static_assert(sizeof(struct NativeAdrenoAbiDeviceInfo) == 64, "ABI device info layout");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/* Same sixteen assertions, enforced when the header is consumed as C11. */
_Static_assert(sizeof(struct NativeAdrenoAbiParams) == 24, "ABI params layout");
_Static_assert(sizeof(struct NativeAdrenoAbiBoAlloc) == 16, "ABI alloc layout");
_Static_assert(sizeof(struct NativeAdrenoAbiBoMap) == 32, "ABI map layout");
_Static_assert(sizeof(struct NativeAdrenoAbiBoImport) == 16, "ABI import layout");
_Static_assert(sizeof(struct NativeAdrenoAbiSubmitBuffer) == 24, "ABI buffer layout");
_Static_assert(sizeof(struct NativeAdrenoAbiSubmit) == 32 + 24 * NativeAdrenoAbiMaxBuffers, "ABI submit layout");
_Static_assert(sizeof(struct NativeAdrenoAbiBoCpuMap) == 8, "ABI cpu map layout");
_Static_assert(sizeof(struct NativeAdrenoAbiBoCpuMapReply) == 16, "ABI cpu map reply layout");
_Static_assert(sizeof(struct NativeAdrenoAbiBoCpuUnmap) == 16, "ABI cpu unmap layout");
_Static_assert(sizeof(struct NativeAdrenoAbiQueue) == 8, "ABI queue layout");
_Static_assert(sizeof(struct NativeAdrenoAbiQueueReply) == 8, "ABI queue reply layout");
_Static_assert(sizeof(struct NativeAdrenoAbiMetadata) == 80, "ABI metadata layout");
_Static_assert(sizeof(struct NativeAdrenoAbiWaitFence) == 16, "ABI wait layout");
_Static_assert(sizeof(struct NativeAdrenoAbiTimestamp) == 8, "ABI timestamp layout");
_Static_assert(sizeof(struct NativeAdrenoAbiStatus) == 16, "ABI status layout");
_Static_assert(sizeof(struct NativeAdrenoAbiDeviceInfo) == 64, "ABI device info layout");
#endif

/* ---- A630 board-pinned facts (P2/P3/P4) ---------------------------------- */
#define ENCHILADA_A630_CHIP_ID           0x06030001u /* DTB :23641, catalog :883-886 */
#define ENCHILADA_A630_CHIP_ID_ALT       0x06030002u /* catalog :885 */
#define ENCHILADA_A630_GMEM_BYTES        0x00100000u /* SZ_1M, catalog :893 */
#define ENCHILADA_A630_HIGHEST_BANK_BIT  15u         /* DTB :23647 (0x0f), P2 highest_bank_bit = 15 */
#define ENCHILADA_A630_MACROTILE_MODE    0u          /* P2 macrotile_mode = 0 (four channel) */
#define ENCHILADA_A630_UBWC_MODE         2u          /* DTB :23649 (raw board fact, NOT a DeviceInfo field) */
#define ENCHILADA_A630_ZAP_PAS_ID        13u         /* DTB :23612 (0x0d) */

/* Every DeviceInfo field a backend may map. "reserved" is always zero and is
 * deliberately not a field here. C enum enumerators have type int, so all
 * masks are forced to uint32_t to stay clean under -Wsign-conversion. */
enum enchilada_darwin_device_field {
    kEnchiladaDarwinFieldChipId         = 1u << 0,
    kEnchiladaDarwinFieldGmemBytes      = 1u << 1,
    kEnchiladaDarwinFieldGmemBase       = 1u << 2,
    kEnchiladaDarwinFieldUcheTrapBase   = 1u << 3,
    kEnchiladaDarwinFieldVaStart        = 1u << 4,
    kEnchiladaDarwinFieldVaSize         = 1u << 5,
    kEnchiladaDarwinFieldHighestBankBit = 1u << 6,
    kEnchiladaDarwinFieldUbwcSwizzle    = 1u << 7,
    kEnchiladaDarwinFieldMacrotileMode  = 1u << 8,
    kEnchiladaDarwinFieldUbwcVersion    = 1u << 9,
    kEnchiladaDarwinFieldFlags          = 1u << 10
};
#define ENCHILADA_DARWIN_DEVICE_FIELD_ALL 0x000007ffu
/* Four fields are sha256-pinned for the A630 today (P2/P3/P4). */
#define ENCHILADA_A630_SPECIFIED_FIELDS \
    ((uint32_t)(kEnchiladaDarwinFieldChipId | kEnchiladaDarwinFieldGmemBytes | \
                kEnchiladaDarwinFieldHighestBankBit | kEnchiladaDarwinFieldMacrotileMode))
/* The other seven MUST be derived by board code; never guessed (P1:127-128). */
#define ENCHILADA_A630_BOARD_DERIVED_FIELDS \
    ((uint32_t)(ENCHILADA_DARWIN_DEVICE_FIELD_ALL & \
                ~(uint32_t)ENCHILADA_A630_SPECIFIED_FIELDS))

/* ---- API (enchilada_ prefix so it never collides with upstream symbols) --- */
#ifdef __cplusplus
extern "C" {
#endif

/* P1:13-35. */
uint32_t enchilada_darwin_abi_version(void);
/* Unknown methods are REFUSED, not ignored (P1:11-12). */
int enchilada_darwin_method_is_known(uint32_t method);
const char *enchilada_darwin_method_name(uint32_t method);

/* Fills ONLY the four A630-pinned DeviceInfo fields, zeroes everything else,
 * and reports which fields it filled. A NULL out is a no-op. */
void enchilada_darwin_device_info_a630(struct NativeAdrenoAbiDeviceInfo *out,
                                        uint32_t *specified_mask);
uint32_t enchilada_darwin_specified_fields(void);
uint32_t enchilada_darwin_board_derived_fields(void);
/* 1 iff the record is honest: exactly the pinned fields filled, their values
 * equal the pins, and NO board-derived field was fabricated. */
int enchilada_darwin_device_info_consistent(const struct NativeAdrenoAbiDeviceInfo *info,
                                             uint32_t specified_mask);
/* P2: the pinned Mesa device table carries GPUId(630) / CHIP.A6XX /
 * a6xx_gen1, so the compiler is NOT the hard blocker at this exact commit. */
int enchilada_darwin_compiler_a630_available(void);

#ifdef __cplusplus
}
#endif
#endif /* NATIVE_ADRENO_ABI_H */
