// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuTableV1 -- turn an allocated, physically-contiguous page block into a
// populated LPAE stage-1 table set, and hand back the root's physical address.
//
// THIS IS STEP 1 + 2 OF THE `page-table` GAP
// -------------------------------------------
// AdrenoSmmuLpaeV1 (round 229) computes the LAYOUT: geometry, indices and descriptor
// words. What it deliberately does not do is own memory, because allocating inside
// this kernel is a different problem. This module closes that gap by taking the
// allocation as an INPUT: the caller passes the virtual and physical base of a block
// of physically-contiguous pages, and this module
//
//   1. lays out `levels - 1` child tables and one root table inside that block,
//   2. writes the descriptor chain down to a leaf,
//   3. returns the root's physical address -- which is exactly what
//      arm-smmu.c:1023 puts in TTBR0 (`virt_to_phys(data->pgd)`).
//
// WHY THE ALLOCATION IS AN ARGUMENT AND NOT A CALL
// ------------------------------------------------
// The tree already has a proven primitive for the allocation itself:
// `armcpu_dma_alloc_writable()` in drivers/ArmCpu/ArmCpu.cpp
// (IOBufferMemoryDescriptor, kIOMemoryPhysicallyContiguous, 4096 alignment, and it
// verifies BOTH the alignment and the contiguity before returning). Calling it from
// here would make this module untestable on the host, which is the only place it can
// be tested at all. So the split is:
//
//   armcpu_dma_alloc_writable() -> memory      (device-proven, ArmCpu.cpp)
//   AdrenoSmmuLpaeV1            -> layout      (host-verified, round 229)
//   AdrenoSmmuTableV1           -> the wiring  (this file, host-verified)
//
// A caller in the kernel does the allocation once and passes the two addresses in.
//
// THE ONE THING THAT MUST NOT BE GOT WRONG
// ----------------------------------------
// Every table page must be ZERO before any descriptor is written, and the descriptors
// must be written in order root-first so a populated child is never reachable through
// an unpopulated parent. Both are enforced here rather than left to the caller: the
// zeroing happens inside the block, and the write order is the loop's shape.
//
// Freestanding C11: no libc, no MMIO, no allocation. It writes only into the caller's
// buffer.

#include "AdrenoSmmuTableV1.h"

/* Filled in by the caller's memcpy/memset equivalent; kept local so this module has
 * no libc dependency and cannot be confused with one that allocates. */
static void table_zero(void *dst, uint32_t bytes)
{
    uint8_t *p = (uint8_t *)dst;
    uint32_t i;
    for (i = 0u; i < bytes; ++i) {
        p[i] = 0u;
    }
}

static void table_store64(void *dst, uint64_t value)
{
    /* The SMMU walks these tables as little-endian 64-bit descriptors. Writing through
     * a uint64_t* is what the kernel-side builder will do too; the host test runs on a
     * little-endian machine, and the verifier asserts the byte order so a big-endian
     * host cannot silently pass. */
    uint8_t *p = (uint8_t *)dst;
    unsigned i;
    for (i = 0u; i < 8u; ++i) {
        p[i] = (uint8_t)((value >> (8u * i)) & 0xffu);
    }
}

uint32_t adreno_table_bytes_needed(const struct adreno_lpae_geo *geo,
                                   uint64_t size, uint32_t *tables_out)
{
    uint32_t leaf;

    if (geo == 0 || tables_out == 0) {
        return 1u;
    }
    *tables_out = 0u;
    leaf = adreno_lpae_leaf_level(size, geo);
    if (leaf >= kAdrenoLpaeMaxLevels) {
        return 2u;
    }
    /* Tables are needed for every level from start_level down to leaf-1, plus the
     * table that holds the leaf itself. */
    *tables_out = (leaf - geo->start_level) + 1u;
    return 0u;
}

uint32_t adreno_table_build(const struct adreno_lpae_geo *geo,
                            uint64_t vbase, uint64_t pbase,
                            uint64_t iova, uint64_t paddr, uint64_t size,
                            uint32_t prot,
                            struct adreno_table_result *out)
{
    struct adreno_lpae_write plan[6];
    uint64_t tables[6];
    uint32_t needed = 0u;
    uint32_t count = 0u;
    uint32_t i;

    if (geo == 0 || out == 0 || vbase == 0ull || pbase == 0ull) {
        return 1u;
    }
    out->root_paddr = 0ull;
    out->tables = 0u;
    out->writes = 0u;

    /* The block must be page aligned at both ends, or the descriptors would point
     * into the middle of somebody else's page. */
    if ((vbase & 4095ull) != 0ull || (pbase & 4095ull) != 0ull) {
        return 2u;
    }

    if (adreno_table_bytes_needed(geo, size, &needed) != 0u) {
        return 3u;
    }
    if (needed == 0u || needed > 6u) {
        return 3u;
    }

    /* Table k lives at vbase + k * table_bytes, and its physical twin at pbase + the
     * same offset. Because the block is physically contiguous (checked by the
     * allocator, not here) the offset is the same in both spaces. */
    for (i = 0u; i < needed; ++i) {
        tables[i] = pbase + (uint64_t)i * (uint64_t)geo->table_bytes;
        table_zero((void *)(uintptr_t)(vbase + (uint64_t)i * (uint64_t)geo->table_bytes),
                   geo->table_bytes);
    }

    if (adreno_lpae_plan(geo, iova, paddr, size, prot, tables, needed,
                         plan, 6u, &count) != 0u) {
        return 4u;
    }
    if (count == 0u || count > 6u) {
        return 4u;
    }

    /* Write root-first: a child that a descriptor points at must already be zeroed,
     * and it is (the loop above zeroed all of them before any write). */
    for (i = 0u; i < count; ++i) {
        const uint64_t slot = plan[i].table_paddr;
        uint64_t vaddr;
        if (slot < pbase || slot >= pbase + (uint64_t)needed * (uint64_t)geo->table_bytes) {
            return 5u; /* the plan named a table outside the block */
        }
        if ((slot & 4095ull) != 0ull) {
            return 5u;
        }
        vaddr = vbase + (slot - pbase);
        if (plan[i].index >= geo->ptes_per_table) {
            return 6u;
        }
        table_store64((void *)(uintptr_t)(vaddr + (uint64_t)plan[i].index * 8ull),
                      plan[i].value);
    }

    out->root_paddr = tables[0];
    out->tables = needed;
    out->writes = count;
    return 0u;
}

/* ---- self-check -------------------------------------------------------- */

int adreno_table_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    struct adreno_lpae_geo geo;
    struct adreno_table_result res;
    uint32_t tables = 0u;
    /* A stand-in "physically contiguous" block: 4 pages, page aligned. */
    static uint8_t block[4u * 4096u] __attribute__((aligned(4096)));
    const uint64_t vbase = (uint64_t)(uintptr_t)block;
    const uint64_t pbase = 0x90000000ull;

    checks += 1u;
    if (adreno_lpae_geometry(48u, 12u, &geo) != 0u) {
        fails += 1u;
    }

    /* ---- how many tables a 4 KiB mapping needs -------------------------- */
    checks += 1u;
    if (adreno_table_bytes_needed(&geo, 0x1000ull, &tables) != 0u || tables != 4u) {
        fails += 1u; /* three levels of tables plus the leaf's table */
    }
    checks += 1u;
    if (adreno_table_bytes_needed(&geo, 0x200000ull, &tables) != 0u || tables != 3u) {
        fails += 1u; /* a 2 MiB block needs one fewer level */
    }
    checks += 1u;
    if (adreno_table_bytes_needed(&geo, 0x1008ull, &tables) == 0u) {
        fails += 1u; /* not a block size */
    }
    checks += 1u;
    if (adreno_table_bytes_needed(0, 0x1000ull, &tables) == 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_table_bytes_needed(&geo, 0x1000ull, 0) == 0u) {
        fails += 1u;
    }

    /* ---- build a real 4 KiB table set ---------------------------------- */
    checks += 1u;
    if (adreno_table_build(&geo, vbase, pbase, 0x40000000ull, 0x90000000ull,
                           0x1000ull, kAdrenoLpaePteValid | kAdrenoLpaePteAf,
                           &res) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (res.root_paddr != pbase) {
        fails += 1u; /* the root is the first page of the block */
    }
    checks += 1u;
    if (res.tables != 4u) {
        fails += 1u;
    }
    checks += 1u;
    if (res.writes != 4u) {
        fails += 1u; /* three table descriptors + one leaf */
    }

    /* ---- the root's first entry points at the second table ------------- */
    checks += 1u;
    {
        const uint32_t root_idx = adreno_lpae_lvl_idx(0x40000000ull, 0u, &geo);
        uint64_t word = 0ull;
        unsigned b;
        for (b = 0u; b < 8u; ++b) {
            word |= (uint64_t)block[root_idx * 8u + b] << (8u * b);
        }
        if (word != adreno_lpae_table_pte(pbase + 4096ull)) {
            fails += 1u;
        }
        /* and it is a TABLE descriptor, not a leaf */
        checks += 1u;
        if (adreno_lpae_pte_type(word) != kAdrenoLpaePteTypeTable) {
            fails += 1u;
        }
    }

    /* ---- the leaf is a PAGE descriptor at the iova's index ------------- */
    checks += 1u;
    {
        const uint32_t leaf_idx = adreno_lpae_lvl_idx(0x40000000ull, 3u, &geo);
        const uint64_t base = (uint64_t)(uintptr_t)(block + 3u * 4096u);
        uint64_t word = 0ull;
        unsigned b;
        for (b = 0u; b < 8u; ++b) {
            word |= (uint64_t)((const uint8_t *)(uintptr_t)base)[leaf_idx * 8u + b] << (8u * b);
        }
        if (adreno_lpae_pte_type(word) != kAdrenoLpaePteTypePage) {
            fails += 1u;
        }
        checks += 1u;
        if ((word & kAdrenoLpaePteAddrMask) != 0x90000000ull) {
            fails += 1u; /* the leaf carries the mapped paddr */
        }
        checks += 1u;
        if ((word & kAdrenoLpaePteValid) == 0ull) {
            fails += 1u;
        }
    }

    /* ---- everything the builder did not write is still zero ------------ */
    /* Count nonzero 64-BIT WORDS, not bytes: a table descriptor is 0x9000x003, whose
     * top four bytes are zero, so a byte count would be an artefact of the address
     * rather than a property of the write. Exactly four words must be non-zero. */
    checks += 1u;
    {
        unsigned i;
        uint32_t nonzero_words = 0u;
        for (i = 0u; i < 4u * 4096u; i += 8u) {
            uint64_t word = 0ull;
            unsigned b;
            for (b = 0u; b < 8u; ++b) {
                word |= (uint64_t)block[i + b] << (8u * b);
            }
            if (word != 0ull) {
                ++nonzero_words;
            }
        }
        if (nonzero_words != 4u) {
            fails += 1u;
        }
    }

    /* ---- refusals ------------------------------------------------------- */
    checks += 1u;
    if (adreno_table_build(0, vbase, pbase, 0ull, 0ull, 0x1000ull, 0u, &res) == 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_table_build(&geo, vbase + 1ull, pbase, 0ull, 0ull, 0x1000ull, 0u,
                           &res) == 0u) {
        fails += 1u; /* misaligned virtual base */
    }
    checks += 1u;
    if (adreno_table_build(&geo, vbase, pbase + 1ull, 0ull, 0ull, 0x1000ull, 0u,
                           &res) == 0u) {
        fails += 1u; /* misaligned physical base */
    }
    checks += 1u;
    if (adreno_table_build(&geo, vbase, pbase, 0ull, 0ull, 0x1008ull, 0u, &res) == 0u) {
        fails += 1u; /* not a block size */
    }
    checks += 1u;
    if (adreno_table_build(&geo, vbase, pbase, 0ull, 0ull, 0x1000ull, 0u, 0) == 0u) {
        fails += 1u;
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return (fails == 0u) ? 0 : 1;
}
