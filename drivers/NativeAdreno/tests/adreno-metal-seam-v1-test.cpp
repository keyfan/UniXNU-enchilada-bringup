// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the A630 Metal establishment-chain seam study. Pure host,
// no IOKit, no hardware. Verifies fidelity to the pinned upstream wire ABI,
// the honesty of the A630 DeviceInfo (only sha256-pinned fields filled), and
// the section-1 frontend gate. See AdrenoMetalSeamV1.h.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "AdrenoMetalSeamV1.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { ++failures; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void testWireAbiMirror()
{
    CHECK(enchilada_metal_abi_version() == 2u);
    CHECK(kEnchiladaMetalAbiMaxBuffers == 128);
    CHECK(kEnchiladaMetalAbiMaxBoBytesShift == 28);
    CHECK(kEnchiladaMetalAbiMetadataBytes == 64);
    CHECK(kEnchiladaMetalAbiMaxQueues == 8);
    /* methods 1..16 all known; 0, 17, 0xffffffff refused */
    CHECK(!enchilada_metal_method_is_known(0u));
    for (uint32_t m = 1; m <= 16; ++m) {
        CHECK(enchilada_metal_method_is_known(m));
        CHECK(enchilada_metal_method_name(m) != 0);
    }
    CHECK(!enchilada_metal_method_is_known(17u));
    CHECK(!enchilada_metal_method_is_known(0xffffffffu));
    CHECK(enchilada_metal_method_name(0) == 0);
    CHECK(std::strcmp(enchilada_metal_method_name(kEnchiladaMetalMethodGetDeviceInfo),
                      "GetDeviceInfo") == 0);
    CHECK(std::strcmp(enchilada_metal_method_name(kEnchiladaMetalMethodBoImport),
                      "BoImport") == 0);
    printf("  testWireAbiMirror: version 2, 16 methods, unknown refused\n");
}

static void testLayouts()
{
    /* The sixteen P1 static_asserts live in the header; here we reconfirm the
     * three most load-bearing sizes from the compiled C ABI. */
    CHECK(sizeof(struct enchilada_metal_abi_submit) == 32u + 24u * 128u);
    CHECK(sizeof(struct enchilada_metal_abi_device_info) == 64u);
    CHECK(sizeof(struct enchilada_metal_abi_metadata) == 80u);
    CHECK(sizeof(struct enchilada_metal_abi_params) == 24u);
    printf("  testLayouts: submit=%zu device_info=64 metadata=80 params=24\n",
           sizeof(struct enchilada_metal_abi_submit));
}

static void testParamsValid()
{
    struct enchilada_metal_abi_params p;
    std::memset(&p, 0, sizeof(p));
    CHECK(!enchilada_metal_abi_params_valid(0)); /* NULL refused */
    p.abi_version = 2u;
    p.max_bo_bytes = 1ull << 28;
    CHECK(enchilada_metal_abi_params_valid(&p));
    p.flags = kEnchiladaMetalParamsHostImport;
    CHECK(enchilada_metal_abi_params_valid(&p));
    p.abi_version = 3u; /* unknown version refused */
    CHECK(!enchilada_metal_abi_params_valid(&p));
    p.abi_version = 2u;
    p.flags = 2u; /* unknown flag refused */
    CHECK(!enchilada_metal_abi_params_valid(&p));
    p.flags = 0u;
    p.max_bo_bytes = 0u; /* zero refused */
    CHECK(!enchilada_metal_abi_params_valid(&p));
    p.max_bo_bytes = (1ull << 28) + 1u; /* >256 MiB refused */
    CHECK(!enchilada_metal_abi_params_valid(&p));
    printf("  testParamsValid: version/flags/max_bo_bytes gated\n");
}

static void testA630DeviceInfo()
{
    struct enchilada_metal_abi_device_info info;
    uint32_t mask = 0xdeadbeefu;
    std::memset(&info, 0xff, sizeof(info));
    enchilada_metal_device_info_a630(&info, &mask);
    CHECK(mask == ENCHILADA_METAL_A630_SPECIFIED_FIELDS);
    CHECK(info.chip_id == 0x06030001u);
    CHECK(info.gmem_bytes == 0x00100000u); /* SZ_1M */
    CHECK(info.highest_bank_bit == 15u);
    /* All eight board-derived fields MUST remain zero (never guessed). */
    CHECK(info.gmem_base == 0u);
    CHECK(info.uche_trap_base == 0u);
    CHECK(info.va_start == 0u);
    CHECK(info.va_size == 0u);
    CHECK(info.ubwc_swizzle == 0u);
    CHECK(info.macrotile_mode == 0u);
    CHECK(info.ubwc_version == 0u);
    CHECK(info.flags == 0u);
    CHECK(info.reserved == 0u);
    CHECK(enchilada_metal_device_info_consistent(&info, mask));
    /* A fabricated field breaks consistency. */
    info.va_size = 0x1000u;
    CHECK(!enchilada_metal_device_info_consistent(&info, mask));
    info.va_size = 0u;
    info.chip_id = 0x43050a01u; /* A740 chip id is NOT the A630 */
    CHECK(!enchilada_metal_device_info_consistent(&info, mask));
    /* Wrong mask is refused. */
    CHECK(!enchilada_metal_device_info_consistent(&info, 0u));
    CHECK(!enchilada_metal_device_info_consistent(0, mask));
    /* Masks: 3 specified, 8 board-derived, disjoint, union == all. */
    CHECK(enchilada_metal_device_info_specified_fields() == 0x00000043u); /* bits 0,1,6 */
    CHECK(enchilada_metal_device_info_board_derived_fields() == 0x000007bcu); /* other 8 */
    CHECK((enchilada_metal_device_info_specified_fields() &
           enchilada_metal_device_info_board_derived_fields()) == 0u);
    CHECK((enchilada_metal_device_info_specified_fields() |
           enchilada_metal_device_info_board_derived_fields()) ==
          (uint32_t)ENCHILADA_METAL_DEVICE_FIELD_ALL);
    /* NULL out is a no-op and reports an empty mask. */
    mask = 0xdeadbeefu;
    enchilada_metal_device_info_a630(0, &mask);
    CHECK(mask == 0u);
    printf("  testA630DeviceInfo: 3 pinned fields, 8 board-derived left zero\n");
}

static void testEstablishmentChain()
{
    CHECK((uint32_t)kEnchiladaMetalStageCount == 9u);
    for (uint32_t s = 0; s < 9u; ++s) {
        CHECK(enchilada_metal_stage_name(s) != 0);
    }
    CHECK(enchilada_metal_stage_name(9u) == 0);
    /* Portable stages carry no macOS blocker. */
    CHECK(enchilada_metal_stage_blockers(kEnchiladaMetalStageFetch) == 0u);
    CHECK(enchilada_metal_stage_blockers(kEnchiladaMetalStageTrust) == 0u);
    CHECK(enchilada_metal_stage_blockers(kEnchiladaMetalStageSeed) == 0u);
    /* The kernel-backend stage carries the three hardware blockers. */
    CHECK((enchilada_metal_stage_blockers(kEnchiladaMetalStageKernelBackend) &
           (uint32_t)kEnchiladaMetalBlockerGen1Backend) != 0u);
    CHECK((enchilada_metal_stage_blockers(kEnchiladaMetalStageKernelBackend) &
           (uint32_t)kEnchiladaMetalBlockerSignedZap) != 0u);
    CHECK((enchilada_metal_stage_blockers(kEnchiladaMetalStageKernelBackend) &
           (uint32_t)kEnchiladaMetalBlockerSmmuContext) != 0u);
    /* OR of all nine stages == today's blocker union. */
    uint32_t all = 0u;
    for (uint32_t s = 0; s < 9u; ++s) {
        all |= enchilada_metal_stage_blockers(s);
    }
    CHECK(all == (uint32_t)ENCHILADA_METAL_TODAY_BLOCKERS);
    CHECK(enchilada_metal_establishment_blockers() == 0x0000001fu);
    CHECK(enchilada_metal_blocker_name(kEnchiladaMetalBlockerOsAbi) != 0);
    CHECK(enchilada_metal_blocker_name(0u) == 0);
    /* Compiler support for A630 EXISTS (Mesa GPUId(630)/a6xx_gen1). */
    CHECK(enchilada_metal_compiler_a630_available() == 1);
    printf("  testEstablishmentChain: 9 stages, 5 blockers, compiler available\n");
}

static void testFrontendGate()
{
    /* section 1: backend + compiler + ZAP + SMMU + OS ABI, ALL required. */
    CHECK(enchilada_metal_frontend_allowed(1, 1, 1, 1, 1) == 1);
    /* Any single missing prerequisite blocks the frontend. */
    CHECK(enchilada_metal_frontend_allowed(0, 1, 1, 1, 1) == 0);
    CHECK(enchilada_metal_frontend_allowed(1, 0, 1, 1, 1) == 0);
    CHECK(enchilada_metal_frontend_allowed(1, 1, 0, 1, 1) == 0);
    CHECK(enchilada_metal_frontend_allowed(1, 1, 1, 0, 1) == 0);
    CHECK(enchilada_metal_frontend_allowed(1, 1, 1, 1, 0) == 0);
    /* enchilada TODAY: compiler yes, everything else no -> blocked. */
    CHECK(enchilada_metal_frontend_allowed(0, 1, 0, 0, 0) == 0);
    printf("  testFrontendGate: section-1 gate blocks current enchilada state\n");
}

int main()
{
    printf("AdrenoMetalSeamV1 host test:\n");
    testWireAbiMirror();
    testLayouts();
    testParamsValid();
    testA630DeviceInfo();
    testEstablishmentChain();
    testFrontendGate();
    if (failures) {
        printf("FAIL: %d checks\n", failures);
        return 1;
    }
    printf("PASS: enchilada A630 Metal establishment-chain seam (6 test groups, 0 failures)\n");
    return 0;
}
