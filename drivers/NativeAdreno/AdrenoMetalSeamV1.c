// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoMetalSeamV1 -- host-verified A630 Metal establishment-chain seam.
// See AdrenoMetalSeamV1.h for provenance and the honest boundary.
// Freestanding: no libc, no constructors, no MMIO, no IOKit, no firmware.
#include "AdrenoMetalSeamV1.h"

static void zero_bytes(unsigned char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        b[i] = 0u;
    }
}

uint32_t enchilada_metal_abi_version(void)
{
    return (uint32_t)kEnchiladaMetalAbiVersion;
}

int enchilada_metal_method_is_known(uint32_t method)
{
    return (method >= (uint32_t)kEnchiladaMetalMethodQuery &&
            method <= (uint32_t)kEnchiladaMetalMethodBoImport) ? 1 : 0;
}

const char *enchilada_metal_method_name(uint32_t method)
{
    switch (method) {
    case kEnchiladaMetalMethodQuery:         return "Query";
    case kEnchiladaMetalMethodBoAlloc:       return "BoAlloc";
    case kEnchiladaMetalMethodBoMap:         return "BoMap";
    case kEnchiladaMetalMethodBoFree:        return "BoFree";
    case kEnchiladaMetalMethodSubmit:        return "Submit";
    case kEnchiladaMetalMethodWaitFence:     return "WaitFence";
    case kEnchiladaMetalMethodGetTimestamp:  return "GetTimestamp";
    case kEnchiladaMetalMethodGetStatus:     return "GetStatus";
    case kEnchiladaMetalMethodSetMetadata:   return "SetMetadata";
    case kEnchiladaMetalMethodGetMetadata:   return "GetMetadata";
    case kEnchiladaMetalMethodCreateQueue:   return "CreateQueue";
    case kEnchiladaMetalMethodCloseQueue:    return "CloseQueue";
    case kEnchiladaMetalMethodBoCpuMap:      return "BoCpuMap";
    case kEnchiladaMetalMethodBoCpuUnmap:    return "BoCpuUnmap";
    case kEnchiladaMetalMethodGetDeviceInfo: return "GetDeviceInfo";
    case kEnchiladaMetalMethodBoImport:      return "BoImport";
    default:                                 return 0;
    }
}

int enchilada_metal_abi_params_valid(const struct enchilada_metal_abi_params *p)
{
    if (!p) {
        return 0;
    }
    if (p->abi_version != (uint32_t)kEnchiladaMetalAbiVersion) {
        return 0; /* unknown versions are refused, not ignored (P1:11-12) */
    }
    if (p->flags != 0u && p->flags != (uint32_t)kEnchiladaMetalParamsHostImport) {
        return 0; /* unknown flags refused */
    }
    if (p->max_bo_bytes == 0u ||
        p->max_bo_bytes > (1ull << kEnchiladaMetalAbiMaxBoBytesShift)) {
        return 0;
    }
    return 1;
}

void enchilada_metal_device_info_a630(struct enchilada_metal_abi_device_info *out,
                                      uint32_t *specified_mask)
{
    if (!out) {
        if (specified_mask) {
            *specified_mask = 0u;
        }
        return;
    }
    zero_bytes((unsigned char *)out, sizeof(*out));
    out->chip_id = ENCHILADA_METAL_A630_CHIP_ID;
    out->gmem_bytes = ENCHILADA_METAL_A630_GMEM_BYTES;
    out->highest_bank_bit = ENCHILADA_METAL_A630_HIGHEST_BANK_BIT;
    if (specified_mask) {
        *specified_mask = ENCHILADA_METAL_A630_SPECIFIED_FIELDS;
    }
}

uint32_t enchilada_metal_device_info_specified_fields(void)
{
    return (uint32_t)ENCHILADA_METAL_A630_SPECIFIED_FIELDS;
}

uint32_t enchilada_metal_device_info_board_derived_fields(void)
{
    return (uint32_t)ENCHILADA_METAL_A630_BOARD_DERIVED_FIELDS;
}

int enchilada_metal_device_info_consistent(const struct enchilada_metal_abi_device_info *info,
                                           uint32_t specified_mask)
{
    if (!info) {
        return 0;
    }
    if (specified_mask != (uint32_t)ENCHILADA_METAL_A630_SPECIFIED_FIELDS) {
        return 0;
    }
    if (info->chip_id != ENCHILADA_METAL_A630_CHIP_ID) {
        return 0;
    }
    if (info->gmem_bytes != ENCHILADA_METAL_A630_GMEM_BYTES) {
        return 0;
    }
    if (info->highest_bank_bit != ENCHILADA_METAL_A630_HIGHEST_BANK_BIT) {
        return 0;
    }
    /* No board-derived field may be fabricated (P1:127-128 "never from a guess"). */
    if (info->gmem_base != 0u || info->uche_trap_base != 0u ||
        info->va_start != 0u || info->va_size != 0u ||
        info->ubwc_swizzle != 0u || info->macrotile_mode != 0u ||
        info->ubwc_version != 0u || info->flags != 0u || info->reserved != 0u) {
        return 0;
    }
    return 1;
}

const char *enchilada_metal_stage_name(uint32_t stage)
{
    switch (stage) {
    case kEnchiladaMetalStageFetch:         return "Fetch (sha-pinned inputs)";
    case kEnchiladaMetalStageBridge:        return "Bridge (metal2vulkan staticlib)";
    case kEnchiladaMetalStageTurnip:        return "Turnip (freedreno darwin-KMD dylib)";
    case kEnchiladaMetalStagePlugin:        return "Plugin (owner-signed Metal bundle)";
    case kEnchiladaMetalStageTrust:         return "Trust (owner trust module)";
    case kEnchiladaMetalStageSeed:          return "Seed (.dbr / NGPUADD2 gpu-addon)";
    case kEnchiladaMetalStageKernelBackend: return "Kernel backend (A6XX_GEN1 KMD archive)";
    case kEnchiladaMetalStageAccelerator:   return "Accelerator (staged IOAcceleratorES)";
    case kEnchiladaMetalStageDiscovery:     return "Discovery (MetalPluginName/.publish)";
    default:                                return 0;
    }
}

const char *enchilada_metal_blocker_name(uint32_t bit)
{
    switch (bit) {
    case kEnchiladaMetalBlockerMacosToolchain: return "macOS/Xcode/codesign toolchain";
    case kEnchiladaMetalBlockerGen1Backend:    return "A6XX_GEN1 kernel backend";
    case kEnchiladaMetalBlockerSignedZap:      return "TrustZone PAS-signed ZAP firmware";
    case kEnchiladaMetalBlockerSmmuContext:    return "kgsl SMMU stage-1 context";
    case kEnchiladaMetalBlockerOsAbi:          return "selected Metal/IOSurface OS ABI";
    default:                                   return 0;
    }
}

uint32_t enchilada_metal_stage_blockers(uint32_t stage)
{
    /* Honest per-stage mapping. The OR of all nine equals
     * ENCHILADA_METAL_TODAY_BLOCKERS (0x1f) -- a host-test invariant. */
    switch (stage) {
    case kEnchiladaMetalStageFetch:
        return 0u; /* pinned inputs are fetched locally, host-verified */
    case kEnchiladaMetalStageBridge:
        return (uint32_t)kEnchiladaMetalBlockerMacosToolchain;
    case kEnchiladaMetalStageTurnip:
        return (uint32_t)(kEnchiladaMetalBlockerMacosToolchain |
                          kEnchiladaMetalBlockerOsAbi);
    case kEnchiladaMetalStagePlugin:
        return (uint32_t)kEnchiladaMetalBlockerMacosToolchain;
    case kEnchiladaMetalStageTrust:
        return 0u; /* make-owner-trust-bundle.py is pure Python, portable */
    case kEnchiladaMetalStageSeed:
        return 0u; /* make-gpu-addon.py is pure Python, portable */
    case kEnchiladaMetalStageKernelBackend:
        return (uint32_t)(kEnchiladaMetalBlockerGen1Backend |
                          kEnchiladaMetalBlockerSignedZap |
                          kEnchiladaMetalBlockerSmmuContext);
    case kEnchiladaMetalStageAccelerator:
        return (uint32_t)(kEnchiladaMetalBlockerGen1Backend |
                          kEnchiladaMetalBlockerOsAbi);
    case kEnchiladaMetalStageDiscovery:
        return (uint32_t)(kEnchiladaMetalBlockerOsAbi |
                          kEnchiladaMetalBlockerMacosToolchain);
    default:
        return 0u;
    }
}

uint32_t enchilada_metal_establishment_blockers(void)
{
    uint32_t stage, all = 0u;
    for (stage = 0; stage < (uint32_t)kEnchiladaMetalStageCount; ++stage) {
        all |= enchilada_metal_stage_blockers(stage);
    }
    return all;
}

int enchilada_metal_compiler_a630_available(void)
{
    /* Mesa freedreno_devices.py: add_gpus([GPUId(630)], A6xxGPUInfo(CHIP.A6XX,
     * [a6xx_base, a6xx_gen1], ...)). HOST-VERIFIED source fact (P5); the
     * upstream iOS cross-build is still macOS-bound, but the compiler SUPPORT
     * for A630 exists, so the compiler is NOT the section-1 hard blocker. */
    return 1;
}

int enchilada_metal_frontend_allowed(int gen1_backend, int compiler_a630,
                                     int signed_zap, int smmu_context, int os_abi)
{
    /* docs/porting/gpu section 1: a frontend may be selected only AFTER a
     * matching-generation kernel backend AND compiler support exist; plus the
     * board's signed-ZAP, SMMU-context and OS-ABI prerequisites. All five must
     * be present. enchilada today: compiler=1, the other four=0 -> BLOCKED. */
    return (gen1_backend && compiler_a630 && signed_zap && smmu_context && os_abi) ? 1 : 0;
}
