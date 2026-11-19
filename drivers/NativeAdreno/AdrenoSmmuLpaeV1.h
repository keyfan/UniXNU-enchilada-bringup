// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuLpaeV1 -- the ARM LPAE stage-1 page-table layout for the kgsl SMMU.
//
// This is the CONTENT of the table that `kAdrenoSmmuInputPageTable` has been
// missing since round 75: geometry, bit positions, descriptor encodings and the
// ordered list of writes a builder must perform.
//
// BOUNDARY (read this before using the module)
// -------------------------------------------
//   modelled : the geometry derivation (arm_lpae_alloc_pgtable), the level shift
//              and block-size macros, the index extraction, the address folding in
//              paddr_to_iopte, the table/leaf descriptor encodings, and the order in
//              which a 4 KiB mapping is installed (three tables then a leaf).
//   NOT done : allocation. The caller owns every table page and passes its physical
//              address in. Nothing here maps memory, writes MMIO, or claims that a
//              populated table has ever been handed to the SMMU.
//
// Why that split matters: the descriptors can be verified on the host today, while
// allocating a page-aligned table inside this kernel cannot -- and mixing the two
// would make the verifiable part untestable.

#ifndef ADRENO_SMMU_LPAE_V1_H
#define ADRENO_SMMU_LPAE_V1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- descriptor bits (P1:23-31) ---------------------------------------- */
#define kAdrenoLpaePteValid       0x1ull       /* BIT(0)                */
#define kAdrenoLpaePteTypeShift   0u           /* ARM_LPAE_PTE_TYPE_SHIFT */
#define kAdrenoLpaePteTypeMask    0x3u         /* ARM_LPAE_PTE_TYPE_MASK  */
#define kAdrenoLpaePteTypeBlock   1u           /* ARM_LPAE_PTE_TYPE_BLOCK */
#define kAdrenoLpaePteTypeTable   3u           /* ARM_LPAE_PTE_TYPE_TABLE */
#define kAdrenoLpaePteTypePage    3u           /* ARM_LPAE_PTE_TYPE_PAGE  */
#define kAdrenoLpaePteAf          (1ull << 10) /* BIT(10)               */
#define kAdrenoLpaePteShIs        (3ull << 8)  /* (3 << 8)              */
#define kAdrenoLpaePteAttrIndxShift 2u         /* ATTRINDX_SHIFT        */

/* ---- geometry limits (P1:55, :91) -------------------------------------- */
#define kAdrenoLpaeMaxLevels      4u
#define kAdrenoLpaeMaxAddrBits    52u
#define kAdrenoLpaeIopteLog2      3u           /* ilog2(sizeof(u64))    */
#define kAdrenoLpaePteAddrMask    0x0000fffffffff000ull /* GENMASK_ULL(47,12) */

/* The geometry for one (ias, granule) pair. For this device -- ias = 48 measured
 * in round 101 and re-confirmed by 103 `stage6=id1` windows -- with the 4 KiB
 * granule the values are:
 *
 *   bits_per_level = 9    levels = 4    start_level = 0
 *   va_bits        = 36   pgd_bits = 9  ptes_per_table = 512
 *   table_bytes    = 4096    level shifts 39 / 30 / 21 / 12
 */
struct adreno_lpae_geo {
    uint32_t page_shift;
    uint32_t bits_per_level;
    uint32_t va_bits;         /* ias - page_shift, NOT ias */
    uint32_t levels;
    uint32_t start_level;     /* MAX_LEVELS - levels */
    uint32_t pgd_bits;
    uint32_t ptes_per_table;
    uint32_t table_bytes;
};

/* One descriptor write. `table_paddr` names the 4 KiB table, `index` the slot
 * inside it, `value` the 64-bit word. A non-zero `is_leaf` marks the mapping's
 * final descriptor. */
struct adreno_lpae_write {
    uint64_t table_paddr;
    uint32_t index;
    uint64_t value;
    uint32_t level;
    uint32_t is_leaf;
};

/* Derive the geometry. Returns 0 on success, non-zero with no geometry written:
 *   1 null out, 2 granule is not 4K/16K/64K, 3 ias above 52,
 *   4 ias not above the granule, 5 level count out of range. */
uint32_t adreno_lpae_geometry(uint32_t ias_bits, uint32_t page_shift,
                              struct adreno_lpae_geo *out);

/* ((MAX_LEVELS - level) * bits_per_level) + ilog2(sizeof(u64)).
 * Level 0 is the ROOT and therefore the LARGEST block: with 4 levels that is
 * shift 39 (512 GiB), and level 3 is shift 12 (4 KiB). Getting this backwards is
 * the single easiest mistake in this file, so it is asserted in the self-check. */
uint32_t adreno_lpae_lvl_shift(uint32_t level, const struct adreno_lpae_geo *geo);
uint64_t adreno_lpae_block_size(uint32_t level, const struct adreno_lpae_geo *geo);

/* The index this address selects at `level`, honouring the root's extra pgd bits. */
uint32_t adreno_lpae_lvl_idx(uint64_t addr, uint32_t level,
                             const struct adreno_lpae_geo *geo);

/* The level at which a mapping of `size` installs a leaf, or MAX_LEVELS when the
 * size is not a legal block size. */
uint32_t adreno_lpae_leaf_level(uint64_t size, const struct adreno_lpae_geo *geo);

/* paddr_to_iopte(): fold the overlapping high bits away, then mask to 47:12. */
uint64_t adreno_lpae_paddr_to_pte(uint64_t paddr);
uint32_t adreno_lpae_pte_type(uint64_t pte);

/* A table descriptor: TYPE_TABLE, deliberately WITHOUT VALID or AF. */
uint64_t adreno_lpae_table_pte(uint64_t table_paddr);

/* A leaf descriptor at `level`: TYPE_PAGE at the final level, TYPE_BLOCK above.
 * `prot` must already carry VALID/AF/SH/ATTRINDX; its type bits are masked off so a
 * caller cannot force the wrong kind of descriptor. */
uint64_t adreno_lpae_leaf_pte(uint32_t prot, uint32_t level, uint64_t paddr);

/* iopte_leaf(): PAGE at the final level, BLOCK above it. */
uint32_t adreno_lpae_is_leaf(uint64_t pte, uint32_t level);

/* Produce the ordered writes for one mapping. `tables` holds the physical addresses
 * of the table pages, root first; `table_count` must cover levels start..leaf-1.
 * Returns 0 and sets *count, or non-zero:
 *   1 null argument, 2 size zero, 3 size is not a block size,
 *   4 unaligned iova or paddr, 5 too few tables, 6 capacity too small. */
uint32_t adreno_lpae_plan(const struct adreno_lpae_geo *geo,
                          uint64_t iova, uint64_t paddr, uint64_t size,
                          uint32_t prot,
                          const uint64_t *tables, uint32_t table_count,
                          struct adreno_lpae_write *out, uint32_t cap,
                          uint32_t *count);

/* Self-check. Returns 0 and writes the check count on success. */
int adreno_lpae_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_SMMU_LPAE_V1_H */
