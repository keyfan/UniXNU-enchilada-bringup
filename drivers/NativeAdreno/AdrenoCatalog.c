// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoCatalog -- host-verified stage-5 GPU feasibility study. See header.
#include "AdrenoCatalog.h"

static const struct adreno_catalog_entry kA630 = {
    ADRENO_CHIP_A630_V1,
    kAdrenoGen1_A6xx,
    kAdrenoGmuLegacy,
    630u,
    0x00100000u,        /* SZ_1M */
    0u,                 /* platform default address space */
    1u,                 /* legacy GMU: HFI v1 / SPTPRAC */
    "a630_zap.mbn",
    "a630_gmu.bin",
    "a630_sqe.fw"
};

static const struct adreno_catalog_entry kA740 = {
    ADRENO_CHIP_A740,
    kAdrenoGen2_A7xx,
    kAdrenoGmuModern,
    740u,
    0x00300000u,        /* 3 * SZ_1M */
    0x0000000400000000ull, /* SZ_16G */
    0u,                 /* GEN2 GMU */
    "a740_zap.mbn",
    "gmu_gen70200.bin",
    "a740_sqe.fw"
};

static void zero_entry(struct adreno_catalog_entry *out)
{
    unsigned char *b = (unsigned char *)out;
    size_t i;
    for (i = 0; i < sizeof(*out); ++i) {
        b[i] = 0u;
    }
    out->generation = kAdrenoGenerationUnknown;
    out->gmu = kAdrenoGmuUnknown;
}

int adreno_catalog_lookup(uint32_t chip_id, struct adreno_catalog_entry *out)
{
    const struct adreno_catalog_entry *entry;

    if (!out) {
        return -1;
    }
    if (chip_id == ADRENO_CHIP_A630_V1 || chip_id == ADRENO_CHIP_A630_V2) {
        entry = &kA630;
    } else if (chip_id == ADRENO_CHIP_A740) {
        entry = &kA740;
    } else {
        zero_entry(out);
        return -1;
    }
    zero_entry(out);
    out->chip_id = entry->chip_id;
    out->generation = entry->generation;
    out->gmu = entry->gmu;
    out->revn = entry->revn;
    out->gmem_bytes = entry->gmem_bytes;
    out->address_space_size = entry->address_space_size;
    out->legacy_gmu = entry->legacy_gmu;
    out->fw_zap = entry->fw_zap;
    out->fw_gmu = entry->fw_gmu;
    out->fw_sqe = entry->fw_sqe;
    /* A630 v2 shares the same properties, only the chip id differs. */
    out->chip_id = chip_id;
    return 0;
}

const char *adreno_catalog_firmware_name(unsigned index)
{
    switch (index) {
    case 0u: return kA630.fw_zap;
    case 1u: return kA630.fw_gmu;
    case 2u: return kA630.fw_sqe;
    default: return 0;
    }
}

const char *adreno_catalog_generation_name(enum adreno_generation gen)
{
    switch (gen) {
    case kAdrenoGen1_A6xx: return "A6XX_GEN1 (A630)";
    case kAdrenoGen2_A7xx: return "A7XX_GEN2 (A740)";
    default: return "unknown";
    }
}

int adreno_catalog_firmware_complete(unsigned firmware_mask)
{
    return (firmware_mask & ADRENO_FW_ALL) == ADRENO_FW_ALL ? 1 : 0;
}

unsigned adreno_catalog_missing_firmware(unsigned firmware_mask)
{
    return (~firmware_mask) & ADRENO_FW_ALL;
}

unsigned adreno_catalog_assess(uint32_t chip_id, unsigned firmware_mask,
                               int backend_available)
{
    unsigned blockers = ADRENO_FEASIBLE;
    struct adreno_catalog_entry entry;

    if (adreno_catalog_lookup(chip_id, &entry) != 0) {
        blockers |= ADRENO_BLOCK_UNKNOWN_CHIP;
        return blockers;
    }
    if (!adreno_catalog_firmware_complete(firmware_mask)) {
        blockers |= ADRENO_BLOCK_FIRMWARE;
    }
    /* A matching-generation backend + compiler are mandatory (guide section 1).
     * sheng's six archives are A740/GEN2 only; a 6xx GEN1 backend is required
     * for A630 regardless of what the caller passes for backend_available. */
    if (!backend_available || entry.generation == kAdrenoGen1_A6xx) {
        blockers |= ADRENO_BLOCK_BACKEND;
    }
    return blockers;
}

int adreno_catalog_chipid_matches_a630(uint32_t raw_chip_id)
{
    return (raw_chip_id == ADRENO_A630_DTB_CHIPID ||
            raw_chip_id == ADRENO_CHIP_A630_V2) ? 1 : 0;
}
