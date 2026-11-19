// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuLpaeV1 -- the ARM LPAE stage-1 page-table layout for the kgsl SMMU.
//
// WHY THIS EXISTS
// ---------------
// `kAdrenoSmmuInputPageTable` has been the first missing input on the
// `kgsl-smmu-stage1` row since round 75. Every anchor for it says the same thing:
//
//   P2:1023  cfg->arm_lpae_s1_cfg.ttbr = virt_to_phys(data->pgd)
//   "TTBR is virt_to_phys(pgd) of an allocated, populated table; this port
//    allocates none"
//
// So what is missing is not a register and not a power domain -- it is the CONTENT
// of that table: which level a mapping lands at, which index inside each table, and
// which 64-bit descriptor word belongs there. This module computes exactly that
// from the pinned source.
//
// WHAT IT DOES NOT DO
// -------------------
// It allocates nothing and writes no MMIO. It takes page-aligned PHYSICAL addresses
// from the caller (the caller owns the memory) and returns the list of descriptor
// writes a builder must perform, in order. That keeps it freestanding and testable
// on the host -- which is the only place it can be tested today, because nothing in
// this port can yet allocate a page-aligned table inside the kernel.
//
// PROVENANCE (sha256-pinned; re-checked by scripts/verify-enchilada-smmu-lpae.py)
// -----------------------------------------------------------------------------
//  (P1) drivers/iommu/io-pgtable-arm.c  38934 B
//       descriptor bits :23-31, ARM_LPAE_MAX_LEVELS :55, LVL_SHIFT :59-60,
//       LVL_IDX :82-83, PTES_PER_TABLE/GRANULE :86-88, PTE_ADDR_MASK :91,
//       paddr_to_iopte :173, leaf test in __arm_lpae_map :230,
//       "no tables at the final level" :252, __arm_lpae_init_pte :300.
//  (P2) drivers/iommu/arm/arm-smmu/arm-smmu.h -- CB_TTBR0 0x20, TTBRn_ASID.
//
// Freestanding C11: no libc, no MMIO, no allocation, no globals.

#include "AdrenoSmmuLpaeV1.h"

uint32_t adreno_lpae_geometry(uint32_t ias_bits, uint32_t page_shift,
                              struct adreno_lpae_geo *out)
{
    uint32_t va_bits, levels;

    if (out == 0) {
        return 1u;
    }
    /* arm_lpae_alloc_pgtable() accepts 4K/16K/64K granules only, and refuses an
     * ias above ARM_LPAE_MAX_ADDR_BITS. */
    if (page_shift != 12u && page_shift != 14u && page_shift != 16u) {
        return 2u;
    }
    if (ias_bits > kAdrenoLpaeMaxAddrBits) {
        return 3u;
    }
    if (ias_bits <= page_shift) {
        return 4u;
    }

    out->page_shift = page_shift;
    out->bits_per_level = page_shift - kAdrenoLpaeIopteLog2;

    /* va_bits = cfg->ias - pg_shift  -- the granule consumes the low bits. */
    va_bits = ias_bits - page_shift;
    levels = (va_bits + out->bits_per_level - 1u) / out->bits_per_level;
    if (levels == 0u || levels > kAdrenoLpaeMaxLevels) {
        return 5u;
    }

    out->va_bits = va_bits;
    out->levels = levels;
    out->start_level = kAdrenoLpaeMaxLevels - levels;
    out->pgd_bits = va_bits - out->bits_per_level * (levels - 1u);
    out->ptes_per_table = 1u << out->bits_per_level;
    out->table_bytes = out->ptes_per_table * 8u;
    return 0u;
}

uint32_t adreno_lpae_lvl_shift(uint32_t level, const struct adreno_lpae_geo *geo)
{
    /* ((MAX_LEVELS - l) * bits_per_level) + ilog2(sizeof(iopte))
     * With 4 levels and 9 bits: l=0 -> 39 (512 GiB), l=3 -> 12 (4 KiB). */
    return (kAdrenoLpaeMaxLevels - level) * geo->bits_per_level +
           kAdrenoLpaeIopteLog2;
}

uint64_t adreno_lpae_block_size(uint32_t level, const struct adreno_lpae_geo *geo)
{
    return 1ull << adreno_lpae_lvl_shift(level, geo);
}

uint32_t adreno_lpae_lvl_idx(uint64_t addr, uint32_t level,
                             const struct adreno_lpae_geo *geo)
{
    uint32_t extra = 0u;
    if (level == geo->start_level && geo->pgd_bits > geo->bits_per_level) {
        extra = geo->pgd_bits - geo->bits_per_level;
    }
    return (uint32_t)((addr >> adreno_lpae_lvl_shift(level, geo)) &
                      ((1ull << (geo->bits_per_level + extra)) - 1ull));
}

uint32_t adreno_lpae_leaf_level(uint64_t size, const struct adreno_lpae_geo *geo)
{
    uint32_t level;
    for (level = geo->start_level; level < kAdrenoLpaeMaxLevels; ++level) {
        if (adreno_lpae_block_size(level, geo) == size) {
            return level;
        }
    }
    return kAdrenoLpaeMaxLevels; /* not a legal block size */
}

/* ---- descriptor words -------------------------------------------------- */

uint64_t adreno_lpae_paddr_to_pte(uint64_t paddr)
{
    /* "Of the bits which overlap, either 51:48 or 15:12 are always RES0":
     * (pte | (pte >> (48 - 12))) & GENMASK_ULL(47,12). */
    return (paddr | (paddr >> (48u - 12u))) & kAdrenoLpaePteAddrMask;
}

uint32_t adreno_lpae_pte_type(uint64_t pte)
{
    return (uint32_t)((pte >> kAdrenoLpaePteTypeShift) & kAdrenoLpaePteTypeMask);
}

uint64_t adreno_lpae_table_pte(uint64_t table_paddr)
{
    /* new = paddr_to_iopte(__pa(table)) | TYPE_TABLE -- no VALID, no AF. */
    return adreno_lpae_paddr_to_pte(table_paddr) | kAdrenoLpaePteTypeTable;
}

uint32_t adreno_lpae_is_leaf(uint64_t pte, uint32_t level)
{
    const uint32_t type = adreno_lpae_pte_type(pte);
    if (level == kAdrenoLpaeMaxLevels - 1u) {
        return type == kAdrenoLpaePteTypePage ? 1u : 0u;
    }
    return type == kAdrenoLpaePteTypeBlock ? 1u : 0u;
}

uint64_t adreno_lpae_leaf_pte(uint32_t prot, uint32_t level, uint64_t paddr)
{
    /* prot carries VALID/AF/SH/ATTRINDX already; the type is appended: PAGE at the
     * final level, BLOCK above it (non-Mali format). */
    uint64_t pte = (uint64_t)prot & ~(uint64_t)kAdrenoLpaePteTypeMask;
    if (level == kAdrenoLpaeMaxLevels - 1u) {
        pte |= kAdrenoLpaePteTypePage;
    } else {
        pte |= kAdrenoLpaePteTypeBlock;
    }
    return pte | adreno_lpae_paddr_to_pte(paddr);
}

/* ---- the write plan ---------------------------------------------------- */

uint32_t adreno_lpae_plan(const struct adreno_lpae_geo *geo,
                          uint64_t iova, uint64_t paddr, uint64_t size,
                          uint32_t prot,
                          const uint64_t *tables, uint32_t table_count,
                          struct adreno_lpae_write *out, uint32_t cap,
                          uint32_t *count)
{
    uint32_t level, leaf, n = 0u, t = 0u;

    if (geo == 0 || tables == 0 || out == 0 || count == 0) {
        return 1u;
    }
    *count = 0u;
    if (size == 0ull) {
        return 2u;
    }
    leaf = adreno_lpae_leaf_level(size, geo);
    if (leaf >= kAdrenoLpaeMaxLevels) {
        return 3u;
    }
    /* Both addresses must be aligned to the mapped block, or the descriptor would
     * address something other than what the caller asked for. */
    if ((iova & (size - 1ull)) != 0ull || (paddr & (size - 1ull)) != 0ull) {
        return 4u;
    }
    /* Every level from start_level down to leaf-1 needs a table. */
    if (table_count < (leaf - geo->start_level)) {
        return 5u;
    }
    if (cap < (leaf - geo->start_level) + 1u) {
        return 6u;
    }

    for (level = geo->start_level; level < leaf; ++level) {
        out[n].table_paddr = tables[t];
        out[n].index = adreno_lpae_lvl_idx(iova, level, geo);
        /* the descriptor points at the NEXT table down */
        out[n].value = adreno_lpae_table_pte(tables[t + 1u]);
        out[n].level = level;
        out[n].is_leaf = 0u;
        ++n;
        ++t;
    }

    out[n].table_paddr = tables[t];
    out[n].index = adreno_lpae_lvl_idx(iova, leaf, geo);
    out[n].value = adreno_lpae_leaf_pte(prot, leaf, paddr);
    out[n].level = leaf;
    out[n].is_leaf = 1u;
    ++n;

    *count = n;
    return 0u;
}

/* ---- self-check -------------------------------------------------------- */

int adreno_lpae_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    struct adreno_lpae_geo geo;
    struct adreno_lpae_write plan[5];
    uint32_t n = 0u;
    const uint64_t tables[4] = { 0x800000ull, 0x801000ull, 0x802000ull, 0x803000ull };

    /* ---- constants, straight from the pinned source --------------------- */
    checks += 1u;
    if (kAdrenoLpaePteValid != 0x1ull || kAdrenoLpaePteTypeShift != 0u ||
        kAdrenoLpaePteTypeMask != 0x3u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoLpaePteTypeBlock != 1u || kAdrenoLpaePteTypeTable != 3u ||
        kAdrenoLpaePteTypePage != 3u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoLpaePteAf != (1ull << 10) || kAdrenoLpaePteShIs != (3ull << 8) ||
        kAdrenoLpaePteAttrIndxShift != 2u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoLpaeMaxLevels != 4u || kAdrenoLpaeMaxAddrBits != 52u ||
        kAdrenoLpaeIopteLog2 != 3u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoLpaePteAddrMask != 0x0000fffffffff000ull) {
        fails += 1u;
    }

    /* ---- geometry for THIS device: ias=48 (round 101), 4 KiB ------------- */
    checks += 1u;
    if (adreno_lpae_geometry(48u, 12u, &geo) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (geo.bits_per_level != 9u || geo.va_bits != 36u) {
        fails += 1u; /* va_bits is ias - pg_shift, NOT ias */
    }
    checks += 1u;
    if (geo.levels != 4u || geo.start_level != 0u || geo.pgd_bits != 9u) {
        fails += 1u;
    }
    checks += 1u;
    if (geo.ptes_per_table != 512u || geo.table_bytes != 4096u) {
        fails += 1u;
    }

    /* ---- level shifts: level 0 is the ROOT (largest block) ------------- */
    checks += 1u;
    if (adreno_lpae_lvl_shift(0u, &geo) != 39u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_lvl_shift(1u, &geo) != 30u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_lvl_shift(2u, &geo) != 21u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_lvl_shift(3u, &geo) != 12u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_block_size(0u, &geo) != 0x8000000000ull) {
        fails += 1u; /* 512 GiB at the root */
    }
    checks += 1u;
    if (adreno_lpae_block_size(3u, &geo) != 0x1000ull) {
        fails += 1u; /* 4 KiB at the leaf */
    }

    /* ---- indices -------------------------------------------------------- */
    checks += 1u;
    if (adreno_lpae_lvl_idx(0x1ff000ull, 3u, &geo) != 511u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_lvl_idx(0x1000ull, 3u, &geo) != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_lvl_idx(0ull, 0u, &geo) != 0u) {
        fails += 1u;
    }

    /* ---- leaf level selection ------------------------------------------ */
    checks += 1u;
    if (adreno_lpae_leaf_level(0x1000ull, &geo) != 3u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_leaf_level(0x200000ull, &geo) != 2u) {
        fails += 1u; /* 2 MiB */
    }
    checks += 1u;
    if (adreno_lpae_leaf_level(0x1000ull + 8ull, &geo) != kAdrenoLpaeMaxLevels) {
        fails += 1u; /* not a block size */
    }

    /* ---- descriptor encoding ------------------------------------------- */
    checks += 1u;
    if (adreno_lpae_table_pte(0x4000ull) != 0x4003ull) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_pte_type(adreno_lpae_table_pte(0x4000ull)) !=
        kAdrenoLpaePteTypeTable) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_paddr_to_pte(0x1000ull) != 0x1000ull) {
        fails += 1u;
    }
    checks += 1u;
    if ((adreno_lpae_paddr_to_pte(0xffff000ull) & ~kAdrenoLpaePteAddrMask) != 0ull) {
        fails += 1u;
    }
    /* a leaf at the final level is a PAGE; above it, a BLOCK */
    checks += 1u;
    if (adreno_lpae_pte_type(adreno_lpae_leaf_pte(kAdrenoLpaePteValid, 3u, 0x1000ull))
        != kAdrenoLpaePteTypePage) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_pte_type(adreno_lpae_leaf_pte(kAdrenoLpaePteValid, 2u, 0ull))
        != kAdrenoLpaePteTypeBlock) {
        fails += 1u;
    }
    /* the typed bits are cleared before the type is written, so a caller passing
     * a BLOCK-typed prot at the final level still gets a PAGE */
    checks += 1u;
    if (adreno_lpae_pte_type(adreno_lpae_leaf_pte(kAdrenoLpaePteTypeBlock, 3u, 0ull))
        != kAdrenoLpaePteTypePage) {
        fails += 1u;
    }

    /* ---- is_leaf -------------------------------------------------------- */
    checks += 1u;
    if (adreno_lpae_is_leaf(adreno_lpae_table_pte(0x4000ull), 2u) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_is_leaf(adreno_lpae_leaf_pte(kAdrenoLpaePteValid, 3u, 0x1000ull),
                            3u) != 1u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_is_leaf(adreno_lpae_leaf_pte(kAdrenoLpaePteValid, 2u, 0ull),
                            2u) != 1u) {
        fails += 1u;
    }

    /* ---- a full 4 KiB plan: three tables then the leaf ------------------ */
    checks += 1u;
    if (adreno_lpae_plan(&geo, 0x40000000ull, 0x90000000ull, 0x1000ull,
                         kAdrenoLpaePteValid | kAdrenoLpaePteAf,
                         tables, 4u, plan, 5u, &n) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (n != 4u) {
        fails += 1u; /* levels 0,1,2 are tables; level 3 is the leaf */
    }
    checks += 1u;
    if (plan[0].level != 0u || plan[1].level != 1u || plan[2].level != 2u ||
        plan[3].level != 3u) {
        fails += 1u;
    }
    checks += 1u;
    if (plan[3].is_leaf != 1u || plan[0].is_leaf != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_pte_type(plan[0].value) != kAdrenoLpaePteTypeTable) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_pte_type(plan[3].value) != kAdrenoLpaePteTypePage) {
        fails += 1u;
    }

    /* ---- refusals ------------------------------------------------------- */
    checks += 1u;
    if (adreno_lpae_geometry(48u, 12u, 0) == 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_geometry(48u, 13u, &geo) == 0u) {
        fails += 1u; /* 8 KiB is not an LPAE granule */
    }
    checks += 1u;
    if (adreno_lpae_geometry(53u, 12u, &geo) == 0u) {
        fails += 1u; /* above ARM_LPAE_MAX_ADDR_BITS */
    }
    checks += 1u;
    if (adreno_lpae_plan(0, 0ull, 0ull, 0x1000ull, 0u, tables, 4u, plan, 5u, &n) == 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_lpae_plan(&geo, 0x40000001ull, 0x90000000ull, 0x1000ull, 0u,
                         tables, 4u, plan, 5u, &n) == 0u) {
        fails += 1u; /* misaligned iova */
    }
    checks += 1u;
    if (adreno_lpae_plan(&geo, 0x40000000ull, 0x90000000ull, 0x1000ull, 0u,
                         tables, 2u, plan, 5u, &n) == 0u) {
        fails += 1u; /* too few tables */
    }
    checks += 1u;
    if (adreno_lpae_plan(&geo, 0x40000000ull, 0x90000000ull, 0x1000ull, 0u,
                         tables, 4u, plan, 2u, &n) == 0u) {
        fails += 1u; /* capacity too small */
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return (fails == 0u) ? 0 : 1;
}
