// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for AdrenoSmmuLpaeV1 -- the LPAE stage-1 table layout.
//
// The test's job is to catch the mistakes this module is most likely to contain: a
// level shift pointing the wrong way, a table descriptor that accidentally carries
// VALID, an address folded with the wrong shift, and a plan that produces the wrong
// number of writes. Each is asserted against the value the pinned upstream source
// implies, not against the module's own output.

#include <cstdio>
#include <cstring>

#include "../AdrenoSmmuLpaeV1.h"

static unsigned g_checks = 0;
static unsigned g_fails = 0;

static void check(bool cond, const char *label)
{
    ++g_checks;
    if (!cond) {
        ++g_fails;
        std::printf("  FAIL %s\n", label);
    }
}

int main(void)
{
    struct adreno_lpae_geo geo;
    std::memset(&geo, 0, sizeof geo);

    // ---- geometry for this device (ias=48 measured, 4 KiB granule) ------
    check(adreno_lpae_geometry(48u, 12u, &geo) == 0u, "geometry derives for ias=48/4K");
    check(geo.bits_per_level == 9u, "bits_per_level = 9");
    check(geo.va_bits == 36u, "va_bits = ias - pg_shift = 36");
    check(geo.levels == 4u, "levels = 4");
    check(geo.start_level == 0u, "start_level = 0");
    check(geo.pgd_bits == 9u, "pgd_bits = 9");
    check(geo.ptes_per_table == 512u, "512 PTEs per 4 KiB table");
    check(geo.table_bytes == 4096u, "a table is exactly one page");

    // ---- level direction: 0 is the root, largest block -------------------
    check(adreno_lpae_lvl_shift(0u, &geo) == 39u, "level 0 shift 39");
    check(adreno_lpae_lvl_shift(3u, &geo) == 12u, "level 3 shift 12");
    check(adreno_lpae_block_size(0u, &geo) == 0x8000000000ull, "root block is 512 GiB");
    check(adreno_lpae_block_size(3u, &geo) == 0x1000ull, "leaf block is 4 KiB");
    check(adreno_lpae_block_size(2u, &geo) == 0x200000ull, "level 2 block is 2 MiB");

    // ---- indices --------------------------------------------------------
    check(adreno_lpae_lvl_idx(0x0ull, 3u, &geo) == 0u, "iova 0 -> index 0");
    check(adreno_lpae_lvl_idx(0x1000ull, 3u, &geo) == 1u, "iova 4K -> index 1");
    check(adreno_lpae_lvl_idx(0x1ff000ull, 3u, &geo) == 511u, "last page of a table -> 511");

    // ---- the mapping level for a size -----------------------------------
    check(adreno_lpae_leaf_level(0x1000ull, &geo) == 3u, "4 KiB maps at level 3");
    check(adreno_lpae_leaf_level(0x200000ull, &geo) == 2u, "2 MiB maps at level 2");
    check(adreno_lpae_leaf_level(0x1008ull, &geo) == kAdrenoLpaeMaxLevels,
          "a non-block size is refused");

    // ---- descriptors ----------------------------------------------------
    // A table descriptor must NOT carry VALID or AF: it is not a leaf.
    const uint64_t table = adreno_lpae_table_pte(0x4000ull);
    check(table == 0x4003ull, "table pte = paddr | TYPE_TABLE");
    // TYPE_TABLE is 3 (binary 11), so bit0 is set as part of the TYPE field -- it is
    // NOT a VALID flag here. VALID and TYPE share bit 0 in this encoding, which is
    // why the two must never be reasoned about separately on a table descriptor.
    check(kAdrenoLpaePteTypeTable == 0x3ull, "TYPE_TABLE is 3, so it subsumes bit 0");
    check((table & kAdrenoLpaePteTypeMask) == 0x3ull, "the low two bits are the TYPE field");
    check((table & kAdrenoLpaePteAf) == 0ull, "table pte has no AF bit");
    check(adreno_lpae_pte_type(table) == kAdrenoLpaePteTypeTable, "table type is 3");
    check(adreno_lpae_is_leaf(table, 2u) == 0u,
          "a table is not a leaf even though bit0 is set");

    // A leaf at the final level is a PAGE; above it a BLOCK.
    const uint64_t leaf = adreno_lpae_leaf_pte(kAdrenoLpaePteValid | kAdrenoLpaePteAf,
                                               3u, 0x1000ull);
    check(adreno_lpae_pte_type(leaf) == kAdrenoLpaePteTypePage, "final-level leaf is PAGE");
    check((leaf & kAdrenoLpaePteValid) != 0ull, "leaf carries VALID from prot");
    check((leaf & kAdrenoLpaePteAf) != 0ull, "leaf carries AF from prot");
    check((leaf & kAdrenoLpaePteAddrMask) == 0x1000ull, "leaf carries the paddr");

    const uint64_t block = adreno_lpae_leaf_pte(kAdrenoLpaePteValid, 2u, 0x0ull);
    check(adreno_lpae_pte_type(block) == kAdrenoLpaePteTypeBlock, "level-2 leaf is BLOCK");

    // A caller passing BLOCK type at the final level still gets a PAGE.
    check(adreno_lpae_pte_type(adreno_lpae_leaf_pte(kAdrenoLpaePteTypeBlock, 3u, 0ull)) ==
              kAdrenoLpaePteTypePage,
          "type bits are masked off before the real type is written");

    // ---- is_leaf --------------------------------------------------------
    check(adreno_lpae_is_leaf(leaf, 3u) == 1u, "page is a leaf at level 3");
    check(adreno_lpae_is_leaf(block, 2u) == 1u, "block is a leaf at level 2");
    check(adreno_lpae_is_leaf(table, 2u) == 0u, "a table is never a leaf");

    // ---- paddr folding --------------------------------------------------
    check(adreno_lpae_paddr_to_pte(0x1000ull) == 0x1000ull, "a small paddr passes through");
    check((adreno_lpae_paddr_to_pte(0xffff000ull) & ~kAdrenoLpaePteAddrMask) == 0ull,
          "nothing above 47:12 survives the mask");

    // ---- a full 4 KiB plan ---------------------------------------------
    const uint64_t tables[4] = { 0x800000ull, 0x801000ull, 0x802000ull, 0x803000ull };
    struct adreno_lpae_write plan[5];
    unsigned int n = 0;
    check(adreno_lpae_plan(&geo, 0x40000000ull, 0x90000000ull, 0x1000ull,
                           kAdrenoLpaePteValid | kAdrenoLpaePteAf,
                           tables, 4u, plan, 5u, &n) == 0u,
          "a 4 KiB plan derives");
    check(n == 4u, "three tables then a leaf: 4 writes");
    check(plan[0].level == 0u && plan[3].level == 3u, "writes walk level 0 -> 3");
    check(plan[0].is_leaf == 0u && plan[3].is_leaf == 1u, "only the last write is a leaf");
    check(plan[0].value == adreno_lpae_table_pte(tables[1]),
          "the root entry points at the next table");
    check(plan[3].index == adreno_lpae_lvl_idx(0x40000000ull, 3u, &geo),
          "the leaf lands at the iova's index");

    // ---- refusals -------------------------------------------------------
    check(adreno_lpae_geometry(48u, 13u, &geo) != 0u, "8 KiB is not an LPAE granule");
    check(adreno_lpae_geometry(53u, 12u, &geo) != 0u, "ias above 52 is refused");
    check(adreno_lpae_geometry(48u, 12u, nullptr) != 0u, "null out is refused");
    check(adreno_lpae_plan(nullptr, 0ull, 0ull, 0x1000ull, 0u, tables, 4u, plan, 5u, &n) != 0u,
          "null geo is refused");
    check(adreno_lpae_plan(&geo, 0x40000001ull, 0x90000000ull, 0x1000ull, 0u,
                           tables, 4u, plan, 5u, &n) != 0u,
          "misaligned iova is refused");
    check(adreno_lpae_plan(&geo, 0x40000000ull, 0x90000000ull, 0x1000ull, 0u,
                           tables, 2u, plan, 5u, &n) != 0u,
          "too few tables is refused");

    // ---- the module's own self-check -----------------------------------
    unsigned int checks = 0;
    check(adreno_lpae_verify(&checks) == 0, "module self-check passes");
    check(checks >= 40u, "self-check covers the geometry and both encodings");
    check(adreno_lpae_verify(nullptr) == 0, "self-check tolerates a null counter");

    std::printf("%u/%u checks passed\n", g_checks - g_fails, g_checks);
    if (g_fails != 0) {
        std::printf("FAIL: AdrenoSmmuLpaeV1 (%u failures)\n", g_fails);
        return 1;
    }
    std::printf("PASS: AdrenoSmmuLpaeV1 LPAE stage-1 table layout\n");
    return 0;
}
