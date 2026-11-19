// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuTableV1 -- populate an allocated page block as an LPAE stage-1 table set.
//
// THE THREE PIECES AND WHO OWNS WHAT
// ---------------------------------
// The `page-table` gap needed three things; two already exist in this tree:
//
//   memory  : armcpu_dma_alloc_writable()   (drivers/ArmCpu/ArmCpu.cpp)
//             IOBufferMemoryDescriptor with kIOMemoryPhysicallyContiguous and 4096
//             alignment; it verifies both the alignment and the contiguity before
//             returning, and it is already device-proven (the PAS init_image path).
//   layout  : AdrenoSmmuLpaeV1               (round 229, host-verified)
//             geometry, level shifts, indices, descriptor encodings and the ordered
//             write plan for one mapping.
//   wiring  : this file                      (host-verified)
//             zero the block, walk the plan, write the descriptors, return the root's
//             physical address -- the value arm-smmu.c:1023 puts in TTBR0.
//
// This module deliberately does NOT allocate. The allocation is an argument, because
// calling IOKit from here would make the module untestable on the host, and the host
// is the only place these descriptors can be checked today.
//
// WHAT A KERNEL CALLER DOES
// -------------------------
//   phys = armcpu_dma_alloc_writable(4 * 4096, &virt);
//   adreno_table_build(&geo, virt, phys, iova, paddr, 4096, prot, &res);
//   write CB_TTBR0 = res.root_paddr | ASID-in-63:48
//
// BOUNDARY
// --------
// MODELLED : the table-set layout inside one contiguous block, the zero-before-write
//            rule, root-first write order, and the root physical address.
// NOT done : the allocation itself, any MMIO, and any claim that a populated table has
//            been handed to the SMMU. Steps 1 and 2 of the gap are addressed here;
//            step 3 (programming CB_TTBR0) is a separate, later act.

#ifndef ADRENO_SMMU_TABLE_V1_H
#define ADRENO_SMMU_TABLE_V1_H

#include <stdint.h>

#include "AdrenoSmmuLpaeV1.h"

#ifdef __cplusplus
extern "C" {
#endif

struct adreno_table_result {
    uint64_t root_paddr;   /* what goes in TTBR0 (before the ASID is folded in) */
    uint32_t tables;       /* how many 4 KiB tables the block must hold          */
    uint32_t writes;       /* how many descriptors were written                  */
};

/* How many pages a mapping of `size` consumes from the block. Returns 0 and sets
 * *tables_out, or non-zero: 1 null argument, 2 size is not a legal block size. */
uint32_t adreno_table_bytes_needed(const struct adreno_lpae_geo *geo,
                                   uint64_t size, uint32_t *tables_out);

/* Build the table set inside the caller's block.
 *
 * vbase and pbase are the SAME block seen in virtual and physical space, both page
 * aligned and physically contiguous (the allocator guarantees the latter). The
 * function zeroes every table page, writes the descriptor chain root-first, and
 * reports the root's physical address.
 *
 * Returns 0 on success, or:
 *   1 null argument, 2 unaligned base, 3 size is not a block size / needs too many
 *   tables, 4 the layout refused the mapping, 5 the plan named a table outside the
 *   block, 6 an index fell outside the table. */
uint32_t adreno_table_build(const struct adreno_lpae_geo *geo,
                            uint64_t vbase, uint64_t pbase,
                            uint64_t iova, uint64_t paddr, uint64_t size,
                            uint32_t prot,
                            struct adreno_table_result *out);

/* Self-check. Returns 0 and writes the check count on success. */
int adreno_table_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_SMMU_TABLE_V1_H */
