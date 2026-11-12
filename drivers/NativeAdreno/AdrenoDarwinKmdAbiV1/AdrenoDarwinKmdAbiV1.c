/* SPDX-License-Identifier: MIT */
/* ROUND 66 - A630 Darwin-KMD ABI directory contract: pure-C implementation.
 * Freestanding (no libc), no MMIO, no IOKit, no firmware. HOST-VERIFIED only.
 * See AdrenoDarwinKmdAbiV1/NativeAdrenoABI.h for the pin table and boundary.
 */
#include "NativeAdrenoABI.h"

static void zero_bytes(void *p, unsigned long n)
{
    unsigned char *b = (unsigned char *)p;
    unsigned long i;
    for (i = 0; i < n; ++i) {
        b[i] = 0u;
    }
}

uint32_t enchilada_darwin_abi_version(void)
{
    return (uint32_t)NativeAdrenoAbiVersion;
}

int enchilada_darwin_method_is_known(uint32_t method)
{
    return (method >= (uint32_t)NativeAdrenoMethodQuery &&
            method <= (uint32_t)NativeAdrenoMethodBoImport) ? 1 : 0;
}

const char *enchilada_darwin_method_name(uint32_t method)
{
    switch (method) {
    case NativeAdrenoMethodQuery:          return "Query";
    case NativeAdrenoMethodBoAlloc:        return "BoAlloc";
    case NativeAdrenoMethodBoMap:          return "BoMap";
    case NativeAdrenoMethodBoFree:         return "BoFree";
    case NativeAdrenoMethodSubmit:         return "Submit";
    case NativeAdrenoMethodWaitFence:      return "WaitFence";
    case NativeAdrenoMethodGetTimestamp:   return "GetTimestamp";
    case NativeAdrenoMethodGetStatus:      return "GetStatus";
    case NativeAdrenoMethodSetMetadata:    return "SetMetadata";
    case NativeAdrenoMethodGetMetadata:    return "GetMetadata";
    case NativeAdrenoMethodCreateQueue:    return "CreateQueue";
    case NativeAdrenoMethodCloseQueue:     return "CloseQueue";
    case NativeAdrenoMethodBoCpuMap:       return "BoCpuMap";
    case NativeAdrenoMethodBoCpuUnmap:     return "BoCpuUnmap";
    case NativeAdrenoMethodGetDeviceInfo:  return "GetDeviceInfo";
    case NativeAdrenoMethodBoImport:       return "BoImport";
    default:                               return "unknown";
    }
}

void enchilada_darwin_device_info_a630(struct NativeAdrenoAbiDeviceInfo *out,
                                        uint32_t *specified_mask)
{
    if (out == (void *)0) {
        return;
    }
    zero_bytes((void *)out, (unsigned long)sizeof(*out));
    out->chip_id = ENCHILADA_A630_CHIP_ID;
    out->gmem_bytes = ENCHILADA_A630_GMEM_BYTES;
    out->highest_bank_bit = ENCHILADA_A630_HIGHEST_BANK_BIT;
    out->macrotile_mode = ENCHILADA_A630_MACROTILE_MODE;
    if (specified_mask != (void *)0) {
        *specified_mask = ENCHILADA_A630_SPECIFIED_FIELDS;
    }
}

uint32_t enchilada_darwin_specified_fields(void)
{
    return ENCHILADA_A630_SPECIFIED_FIELDS;
}

uint32_t enchilada_darwin_board_derived_fields(void)
{
    return ENCHILADA_A630_BOARD_DERIVED_FIELDS;
}

int enchilada_darwin_device_info_consistent(const struct NativeAdrenoAbiDeviceInfo *info,
                                             uint32_t specified_mask)
{
    if (info == (void *)0) {
        return 0;
    }
    if (specified_mask != ENCHILADA_A630_SPECIFIED_FIELDS) {
        return 0;
    }
    if (info->chip_id != ENCHILADA_A630_CHIP_ID ||
        info->gmem_bytes != ENCHILADA_A630_GMEM_BYTES ||
        info->highest_bank_bit != ENCHILADA_A630_HIGHEST_BANK_BIT ||
        info->macrotile_mode != ENCHILADA_A630_MACROTILE_MODE) {
        return 0;
    }
    /* The seven board-derived fields must be ZERO: not guessed (P1:127-128). */
    if (info->gmem_base != 0u || info->uche_trap_base != 0u ||
        info->va_start != 0u || info->va_size != 0u ||
        info->ubwc_swizzle != 0u || info->ubwc_version != 0u ||
        info->flags != 0u || info->reserved != 0u) {
        return 0;
    }
    return 1;
}

int enchilada_darwin_compiler_a630_available(void)
{
    /* P2 (pinned Mesa 85fda3b3...) carries GPUId(630) / CHIP.A6XX /
     * a6xx_gen1. This is a SOURCE fact, not a device measurement. */
    return 1;
}
