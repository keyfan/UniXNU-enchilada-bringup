// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoMetalPublishV1 -- enchilada stage 5 (GPU) HOST-VERIFIED Metal
// PUBLISH / DISCOVERY / CLIENT-WINDOW POLICY contract study (round 67).
//
// This is NOT a Metal GPU driver and NOT a Metal plugin. It is a freestanding,
// pure-C mirror of the GENERATION-INDEPENDENT publish policy that upstream
// UniXNU uses to stage and select its native Metal device (Data seed markers,
// discovery chain, render-server routing, per-process allow-list, user-client
// open-type routing and connection caps), PLUS an HONEST gate stating that
// every GPU address WINDOW base/size is BOARD-DERIVED and therefore MUST stay
// ZERO for enchilada's A630. It maps nothing, writes no register, opens no
// user client, creates no MTLDevice, composites no frame, and is NOT an
// IOService. HOST-VERIFIED only: sha256-pinned upstream sources + host tests;
// no device run, no flash, no CI-built frontend.
//
// WHY THIS EXISTS (user m44634 / m44638 / m44639 / m44640 / m44642 / m44646)
// --------------------------------------------------------------------------
// m44639 described the layer where "the render server gets a whole 1 GiB GPU
// window and listed apps each get a Metal device with their own window and
// shader cache". That policy is generation-INDEPENDENT: it is the same for
// A630 and A740. The WINDOW ADDRESSES, however, are generation/board-SPECIFIC:
// NativeAdrenoPublish.cpp:105-114 hard-codes sheng's 2 GiB aperture
// 0x20000000..0x80000000 with slot0 = 1 GiB and three ~170.6 MiB slots.
// Per docs/porting/gpu section 1, a different GPU generation must first
// implement its matching kernel backend and compiler support; per
// NativeAdrenoABI.h:127-128 board-derived DeviceInfo facts "come from the
// board code ... never from a guess". So for enchilada this module refuses to
// emit ANY window base/size: all four slots stay zero until an A630 board VA
// map and a validated kgsl SMMU stage-1 context exist.
//
// DOC-CONSTANT CORRECTION (carried honestly, not copied as fact)
// docs/drivers/adreno740.md:41,45 say listed clients get "256 MiB" windows.
// That number is the PER-OBJECT cap NativeAdrenoAbiMaxBoBytesShift = 28
// (NativeAdrenoABI.h:16 "256MiB per object"), NOT the actual client window:
// kClientWindows slots 1-3 are 0x0AAA9000 = 178,905,088 B ~= 170.6 MiB. This
// module exposes both values under distinct, unambiguous names.
//
// PROVENANCE (7 sha256 pins re-checked by scripts/verify-enchilada-metal-publish.py)
// --------------------------------------------------------------------------------
//  (P1) work/UniXNU/drivers/Adreno740/NativeAdrenoPublish.cpp
//       48729 B 013e43c09db8e5d3e444a3a0f1f56d14e381df47097689478811b20a162f5183
//       :46 open-type constants; :51-52 hand-off; :59 export; :68 mailbox;
//       :105-114 kClientWindows + NativeAdrenoMaxClients; :234 backboardd slot 0;
//       :243 setScope(slot*MaxQueues, start, bytes); :577 marker-only open;
//       :662-673 hand-off / unsupported open; :689 non-render-server cap 8.
//  (P2) work/UniXNU/drivers/AdrenoMetal/NativeAdrenoMetalPlugin.mm
//       26233 B 5105c1627020da7d404c7f7454f1d4887c09c3273fc54991c47f5d64e68e3454
//       :139-158 listedClient() star / exact / trailing-star prefix over
//       delimiters " ,\t\r\n"; :226 backboardd routing; :413 publish::requested.
//  (P3) work/UniXNU/docs/drivers/adreno740.md
//       7290 B d7dec513732a66dbc0ffdd1ee707b15b71c4604664c0c09eb44fc658596f9bd3
//       :26-30 establishment chain; :40-41 --publish/--clients; :45 windows.
//  (P4) work/UniXNU/scripts/add-gpu-plugin-data.py
//       6777 B 51393cf9ec91faf885cca08658076654ad4f8409dacaa43c2b90c403292f8b49
//       :39 client-name validation; :73-79 .clients/.publish staging; :93-105
//       deep/compressed framebuffer off; :60/110 hdiutil (macOS-only); :111 NXSB.
//  (P5) work/UniXNU/drivers/Adreno740/NativeAdrenoABI.h
//       7733 B 20ac9298bb390d86788c1fc58155e1f9581ffa942e222752efc95c67cb5670d2
//       :16 MaxBoBytesShift=28 (256 MiB per object); MaxQueues=8; :127-128.
//  (P6) work/UniXNU/docs/porting/gpu.md
//       7861 B 521486857e22068cc8a87a604ab075c33d0b21d61476495ebfc0f19e5d4da7f4
//       section 1: "For another GPU generation, implement the matching kernel
//       backend and compiler support before selecting its frontend."
//  (P7) work/tmp/kernel.yml
//       14554 B 6629ee5180434b7ba8d638f50542ecd83b1f373240a0efcf4c28f7483fe7bb0b
//       macOS-26 kernel CI; contains NO build-turnip / meson / xcrun /
//       codesign / hdiutil step => the Metal FRONTEND cannot be built by this
//       CI. Round 67 "compile" therefore means: this pure-C unit joins
//       libEnchiladaAdreno.a and is host-tested + CI link-verified.
//
// BOUNDARY: even a green CI build does NOT make this a working Metal driver.
// CONFIG_XNU_NATIVE_ADRENO stays n; the gpu porting.toml area STAYS
// "host-verified"; no device run, no flash, m39510 NOT exercised.
// --------------------------------------------------------------------------

#ifndef ENCHILADA_METAL_PUBLISH_V1_H
#define ENCHILADA_METAL_PUBLISH_V1_H

#include <stdint.h>

/* ---- Generation-independent publish constants (verbatim mirrors of P1) ---- */
#define kEnchiladaPublishPluginOpenType 0x4147u
#define kEnchiladaPublishTunnelMagic 0x4e544e4cu
#define kEnchiladaPublishHandoffBase 0x7000u
#define kEnchiladaPublishHandoffLast 0x7003u
#define kEnchiladaPublishScanoutType 0x5343u
#define kEnchiladaPublishHandoffOpenType 0x4148u
#define kEnchiladaPublishExportMethod 0x7100u
#define kEnchiladaPublishExportMaxPiece 3968u
#define kEnchiladaPublishMailboxType 0xc0deu
#define kEnchiladaPublishMailboxMagic 0x4d41494cu
#define kEnchiladaPublishMaxClients 4u
#define kEnchiladaPublishMaxQueues 8u
#define kEnchiladaPublishClientCap 8u
#define kEnchiladaPublishPerObjectBytesShift 28u
#define kEnchiladaPublishA630WindowsSpecifiedMask 0u
#define kEnchiladaPublishTodayBlockers 0x3fu

/* Seed marker file names (P4) and the render-server process name (P1/P2). */
#define ENCHILADA_PUBLISH_MARKER_PUBLISH "NativeAdrenoMetal.publish"
#define ENCHILADA_PUBLISH_MARKER_CLIENTS "NativeAdrenoMetal.clients"
#define ENCHILADA_PUBLISH_RENDER_SERVER "backboardd"

/* Sheng-only reference windows (P1:113-114). These are PROVENANCE DATA, never
 * A630 values. They are exposed under an explicitly sheng-named API so no
 * caller can mistake them for enchilada's board-derived windows. */
#define ENCHILADA_PUBLISH_SHENG_SLOT0_START 0x20000000ull
#define ENCHILADA_PUBLISH_SHENG_SLOT0_BYTES 0x40000000ull
#define ENCHILADA_PUBLISH_SHENG_SLOT1_START 0x60000000ull
#define ENCHILADA_PUBLISH_SHENG_SLOT2_START 0x6aaa9000ull
#define ENCHILADA_PUBLISH_SHENG_SLOT3_START 0x75552000ull
#define ENCHILADA_PUBLISH_SHENG_CLIENT_BYTES 0x0aaa9000ull

/* Nine ordered stages of the publish/discovery establishment chain. */
enum enchilada_publish_stage {
    kEnchiladaPublishStageSeedMarkers = 0u,
    kEnchiladaPublishStageDiscovery = 1u,
    kEnchiladaPublishStagePublishGate = 2u,
    kEnchiladaPublishStageRenderServerDevice = 3u,
    kEnchiladaPublishStageClientAllowList = 4u,
    kEnchiladaPublishStageClientDevice = 5u,
    kEnchiladaPublishStageKernelUserClient = 6u,
    kEnchiladaPublishStageGpuWindowScope = 7u,
    kEnchiladaPublishStageScanoutComposite = 8u,
    kEnchiladaPublishStageCount = 9u
};

/* Six honest blockers. The OR of every stage's blockers is 0x3f today. */
enum enchilada_publish_blocker {
    kEnchiladaPublishBlockerMacosToolchain = 1,
    kEnchiladaPublishBlockerGen1Backend = 2,
    kEnchiladaPublishBlockerSignedZap = 4,
    kEnchiladaPublishBlockerSmmuContext = 8,
    kEnchiladaPublishBlockerOsAbi = 16,
    kEnchiladaPublishBlockerBoardVaMap = 32
};

/* One GPU address window. start/bytes are BOARD-DERIVED. */
struct enchilada_publish_window {
    uint64_t start;
    uint64_t bytes;
};

#if defined(__cplusplus)
static_assert(sizeof(struct enchilada_publish_window) == 16u, "window must be two uint64_t");
static_assert(kEnchiladaPublishMaxClients == 4u, "P1:112 NativeAdrenoMaxClients=4");
static_assert(kEnchiladaPublishMaxQueues == 8u, "P5 NativeAdrenoAbiMaxQueues=8");
static_assert(kEnchiladaPublishPerObjectBytesShift == 28u, "P5:16 256MiB per object");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(struct enchilada_publish_window) == 16u, "window must be two uint64_t");
_Static_assert(kEnchiladaPublishMaxClients == 4u, "P1:112 NativeAdrenoMaxClients=4");
_Static_assert(kEnchiladaPublishMaxQueues == 8u, "P5 NativeAdrenoAbiMaxQueues=8");
_Static_assert(kEnchiladaPublishPerObjectBytesShift == 28u, "P5:16 256MiB per object");
#endif

/* ---- API (freestanding; no libc, no MMIO, no IOKit) ---- */

#ifdef __cplusplus
extern "C" {
#endif

/* P1:46/:51-52/:59/:68 open-type routing. */
int enchilada_publish_open_type_is_plugin(uint32_t type);
int enchilada_publish_open_type_is_handoff(uint32_t type);
/* P1:577 marker-only open: (type>>16)==PluginOpenType && (type&0xffff)!=0. */
int enchilada_publish_open_type_is_marker(uint32_t type);
const char *enchilada_publish_open_type_name(uint32_t type);

/* P2:226 render-server routing (length-based, no strlen). */
int enchilada_publish_is_render_server(const char *name, uint32_t length);
/* P4:39 client-name validation: alnum or one of '._-*'. */
int enchilada_publish_client_name_valid(const char *name, uint32_t length);
/* P2:139-158 allow-list policy: "*" | exact | trailing-'*' prefix, over the
 * five delimiters " ,\t\r\n". list is a raw text blob (NOT NUL-terminated). */
int enchilada_publish_client_listed(const char *list, uint32_t list_length,
                                    const char *self, uint32_t self_length);
/* P1:689 connection cap: render server keeps the caller limit; any other
 * process is capped at 8 (a crashed client must not loop the system process). */
uint32_t enchilada_publish_client_cap(int is_render_server, uint32_t limit);
/* P1:243 per-slot queue range base = slot * MaxQueues. */
uint32_t enchilada_publish_queue_base(uint32_t slot);

/* WINDOW HONESTY GATE. For A630 all four slots are zero and the specified
 * mask is zero: the board VA map is NOT allowed to be guessed. */
void enchilada_publish_windows_a630(struct enchilada_publish_window out[4],
                                    uint32_t *specified_mask);
int enchilada_publish_windows_consistent(const struct enchilada_publish_window w[4],
                                         uint32_t mask);
int enchilada_publish_windows_are_board_derived(void);
/* Provenance-only accessor for the SHENG reference windows; returns 0 for an
 * out-of-range slot. NEVER use the result as an enchilada window. */
int enchilada_publish_sheng_reference(uint32_t slot, uint64_t *start, uint64_t *bytes);
uint32_t enchilada_publish_per_object_cap_bytes_shift(void);

/* Stage / blocker map. */
const char *enchilada_publish_stage_name(uint32_t stage);
const char *enchilada_publish_blocker_name(uint32_t bit);
uint32_t enchilada_publish_stage_blockers(uint32_t stage);
uint32_t enchilada_publish_establishment_blockers(void);
/* Seven prerequisites must ALL be non-zero before any A630 Metal frontend may
 * be selected. enchilada today: compiler=1, the other six 0 => returns 0. */
int enchilada_publish_frontend_allowed(int gen1_backend, int compiler,
                                       int signed_zap, int smmu_context,
                                       int os_abi, int macos_toolchain,
                                       int board_va_map);

#ifdef __cplusplus
}
#endif

#endif /* ENCHILADA_METAL_PUBLISH_V1_H */
