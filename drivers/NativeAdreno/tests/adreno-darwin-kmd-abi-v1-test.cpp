// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the ROUND 66 A630 Darwin-KMD ABI directory contract.
// Pure host, no IOKit, no hardware. Proves that the directory's single
// NativeAdrenoABI.h mirrors the upstream A740 wire ABI in symbol names,
// enum values and record layouts, and that the A630 DeviceInfo carries ONLY
// sha256-pinned fields. See AdrenoDarwinKmdAbiV1/NativeAdrenoABI.h.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "AdrenoDarwinKmdAbiV1/NativeAdrenoABI.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { ++failures; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void testUpstreamSymbolMirror()
{
    /* Same names/values as oneplus6-ios/work/UniXNU/drivers/Adreno740/
     * NativeAdrenoABI.h:13-45. Turnip's tu_knl_darwin.cc includes this header
     * by name and uses these exact symbols. */
    CHECK(NativeAdrenoAbiVersion == 2);
    CHECK(NativeAdrenoAbiMaxBuffers == 128);
    CHECK(NativeAdrenoAbiMaxBoBytesShift == 28);
    CHECK(NativeAdrenoAbiMetadataBytes == 64);
    CHECK(NativeAdrenoAbiMaxQueues == 8);
    CHECK(NativeAdrenoParamsHostImport == 1);
    CHECK(NativeAdrenoDeviceClientIova == 1);
    CHECK(NativeAdrenoDeviceCachedCoherent == 2);
    CHECK(NativeAdrenoMethodQuery == 1);
    CHECK(NativeAdrenoMethodBoAlloc == 2);
    CHECK(NativeAdrenoMethodBoMap == 3);
    CHECK(NativeAdrenoMethodBoFree == 4);
    CHECK(NativeAdrenoMethodSubmit == 5);
    CHECK(NativeAdrenoMethodWaitFence == 6);
    CHECK(NativeAdrenoMethodGetTimestamp == 7);
    CHECK(NativeAdrenoMethodGetStatus == 8);
    CHECK(NativeAdrenoMethodSetMetadata == 9);
    CHECK(NativeAdrenoMethodGetMetadata == 10);
    CHECK(NativeAdrenoMethodCreateQueue == 11);
    CHECK(NativeAdrenoMethodCloseQueue == 12);
    CHECK(NativeAdrenoMethodBoCpuMap == 13);
    CHECK(NativeAdrenoMethodBoCpuUnmap == 14);
    CHECK(NativeAdrenoMethodGetDeviceInfo == 15);
    CHECK(NativeAdrenoMethodBoImport == 16);
    printf("  testUpstreamSymbolMirror: version 2, 16 methods, 3 flags\n");
}

static void testUpstreamLayouts()
{
    /* The sixteen P1:147-163 static_asserts are inside the header itself
     * (both the C++ and the C11 branch); reconfirm the load-bearing sizes. */
    CHECK(sizeof(struct NativeAdrenoAbiParams) == 24u);
    CHECK(sizeof(struct NativeAdrenoAbiBoAlloc) == 16u);
    CHECK(sizeof(struct NativeAdrenoAbiBoMap) == 32u);
    CHECK(sizeof(struct NativeAdrenoAbiBoImport) == 16u);
    CHECK(sizeof(struct NativeAdrenoAbiSubmitBuffer) == 24u);
    CHECK(sizeof(struct NativeAdrenoAbiSubmit) == 32u + 24u * 128u);
    CHECK(sizeof(struct NativeAdrenoAbiMetadata) == 80u);
    CHECK(sizeof(struct NativeAdrenoAbiWaitFence) == 16u);
    CHECK(sizeof(struct NativeAdrenoAbiTimestamp) == 8u);
    CHECK(sizeof(struct NativeAdrenoAbiStatus) == 16u);
    CHECK(sizeof(struct NativeAdrenoAbiDeviceInfo) == 64u);
    CHECK(sizeof(struct NativeAdrenoAbiBoCpuMap) == 8u);
    CHECK(sizeof(struct NativeAdrenoAbiBoCpuMapReply) == 16u);
    CHECK(sizeof(struct NativeAdrenoAbiBoCpuUnmap) == 16u);
    CHECK(sizeof(struct NativeAdrenoAbiQueue) == 8u);
    CHECK(sizeof(struct NativeAdrenoAbiQueueReply) == 8u);
    printf("  testUpstreamLayouts: 16 sizes, device_info=64 submit=%zu\n",
           sizeof(struct NativeAdrenoAbiSubmit));
}

static void testMethodApi()
{
    CHECK(enchilada_darwin_abi_version() == 2u);
    CHECK(!enchilada_darwin_method_is_known(0u));
    for (uint32_t m = 1; m <= 16; ++m) {
        CHECK(enchilada_darwin_method_is_known(m));
        CHECK(enchilada_darwin_method_name(m) != 0);
    }
    CHECK(!enchilada_darwin_method_is_known(17u));
    CHECK(!enchilada_darwin_method_is_known(0xffffffffu));
    CHECK(std::strcmp(enchilada_darwin_method_name(0), "unknown") == 0);
    CHECK(std::strcmp(enchilada_darwin_method_name(NativeAdrenoMethodGetDeviceInfo),
                      "GetDeviceInfo") == 0);
    CHECK(std::strcmp(enchilada_darwin_method_name(NativeAdrenoMethodBoImport),
                      "BoImport") == 0);
    printf("  testMethodApi: 1..16 known, 0/17/0xffffffff refused\n");
}

static void testA630DeviceInfoPins()
{
    struct NativeAdrenoAbiDeviceInfo info;
    uint32_t mask = 0;
    /* Poison first: the producer must zero everything it does not pin. */
    std::memset(&info, 0xa5, sizeof(info));
    enchilada_darwin_device_info_a630(&info, &mask);
    /* Four pinned fields, mask = bits 0,1,6,8 = 0x143. */
    CHECK(mask == 0x00000143u);
    CHECK(enchilada_darwin_specified_fields() == 0x00000143u);
    CHECK(enchilada_darwin_board_derived_fields() == 0x000006bcu);
    CHECK(info.chip_id == ENCHILADA_A630_CHIP_ID);
    CHECK(info.chip_id == 0x06030001u);
    CHECK(info.gmem_bytes == ENCHILADA_A630_GMEM_BYTES);
    CHECK(info.gmem_bytes == 0x00100000u);
    CHECK(info.highest_bank_bit == 15u);
    CHECK(info.macrotile_mode == 0u);
    /* Seven board-derived fields MUST stay zero (P1:127-128 never from a guess). */
    CHECK(info.gmem_base == 0u);
    CHECK(info.uche_trap_base == 0u);
    CHECK(info.va_start == 0u);
    CHECK(info.va_size == 0u);
    CHECK(info.ubwc_swizzle == 0u);
    CHECK(info.ubwc_version == 0u);
    CHECK(info.flags == 0u);
    CHECK(info.reserved == 0u);
    CHECK(enchilada_darwin_device_info_consistent(&info, mask) == 1);
    printf("  testA630DeviceInfoPins: mask=0x143 derived=0x6bc, 8 fields zero\n");
}

static void testHonestyGate()
{
    struct NativeAdrenoAbiDeviceInfo info;
    uint32_t mask = 0;
    enchilada_darwin_device_info_a630(&info, &mask);
    CHECK(enchilada_darwin_device_info_consistent(&info, mask) == 1);
    /* A fabricated board-derived field is refused. */
    info.gmem_base = 0x1000000ull;
    CHECK(enchilada_darwin_device_info_consistent(&info, mask) == 0);
    info.gmem_base = 0u;
    /* A wrong pin is refused. */
    info.chip_id = ENCHILADA_A630_CHIP_ID_ALT;
    CHECK(enchilada_darwin_device_info_consistent(&info, mask) == 0);
    info.chip_id = ENCHILADA_A630_CHIP_ID;
    /* A NULL record is refused; a wrong mask is refused. */
    CHECK(enchilada_darwin_device_info_consistent(0, mask) == 0);
    CHECK(enchilada_darwin_device_info_consistent(&info, 0u) == 0);
    CHECK(enchilada_darwin_device_info_consistent(&info, 0x7ffu) == 0);
    /* NULL out is a documented no-op, not a crash. */
    enchilada_darwin_device_info_a630(0, &mask);
    CHECK(mask == ENCHILADA_A630_SPECIFIED_FIELDS);
    printf("  testHonestyGate: fabricated/wrong/NULL/wrong-mask all refused\n");
}

static void testPinnedCompiler()
{
    /* P2 (pinned Mesa 85fda3b3) carries GPUId(630)/CHIP.A6XX/a6xx_gen1, so
     * the compiler is NOT the hard blocker at the exact build commit. */
    CHECK(enchilada_darwin_compiler_a630_available() == 1);
    CHECK(ENCHILADA_A630_CHIP_ID_ALT == 0x06030002u);
    CHECK(ENCHILADA_A630_UBWC_MODE == 2u);
    CHECK(ENCHILADA_A630_ZAP_PAS_ID == 13u);
    printf("  testPinnedCompiler: A630 present at the pinned Mesa commit\n");
}

int main()
{
    testUpstreamSymbolMirror();
    testUpstreamLayouts();
    testMethodApi();
    testA630DeviceInfoPins();
    testHonestyGate();
    testPinnedCompiler();
    if (failures) {
        printf("FAIL: enchilada A630 Darwin-KMD ABI directory contract (%d failures)\n", failures);
        return 1;
    }
    printf("PASS: enchilada A630 Darwin-KMD ABI directory contract (6 test groups, 0 failures)\n");
    return 0;
}
