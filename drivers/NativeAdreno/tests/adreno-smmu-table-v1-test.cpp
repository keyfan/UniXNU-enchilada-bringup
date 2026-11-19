// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for AdrenoSmmuTableV1 -- the wiring between an allocated block and the
// LPAE layout.
//
// This test uses a real, page-aligned, stand-in block and checks the RESULTING MEMORY,
// not just the return codes: that the root descriptor points at the second table, that
// the leaf is a PAGE descriptor carrying the mapped physical address, and that exactly
// four 64-bit words were written and nothing else.

#include <cstdio>
#include <cstring>

#include "../AdrenoSmmuTableV1.h"

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

static uint64_t load64(const unsigned char *base, unsigned index)
{
    uint64_t v = 0;
    for (unsigned b = 0; b < 8u; ++b) {
        v |= static_cast<uint64_t>(base[index * 8u + b]) << (8u * b);
    }
    return v;
}

int main(void)
{
    struct adreno_lpae_geo geo;
    struct adreno_table_result res;
    static unsigned char block[4u * 4096u] __attribute__((aligned(4096)));
    unsigned int needed = 0;

    check(adreno_lpae_geometry(48u, 12u, &geo) == 0u, "geometry derives");

    // ---- how many pages a mapping consumes -------------------------------
    check(adreno_table_bytes_needed(&geo, 0x1000u, &needed) == 0u && needed == 4u,
          "a 4 KiB mapping needs 4 pages");
    check(adreno_table_bytes_needed(&geo, 0x200000u, &needed) == 0u && needed == 3u,
          "a 2 MiB mapping needs 3 pages");
    check(adreno_table_bytes_needed(&geo, 0x1008u, &needed) != 0u,
          "a non-block size is refused");
    check(adreno_table_bytes_needed(nullptr, 0x1000u, &needed) != 0u,
          "null geometry is refused");

    // ---- build a real table set -----------------------------------------
    std::memset(block, 0, sizeof block);
    const uint64_t vbase = reinterpret_cast<uint64_t>(block);
    const uint64_t pbase = 0x90000000ull;
    const uint64_t iova = 0x40000000ull;

    check(adreno_table_build(&geo, vbase, pbase, iova, pbase, 0x1000u,
                             kAdrenoLpaePteValid | kAdrenoLpaePteAf, &res) == 0u,
          "the table set builds");
    check(res.root_paddr == pbase, "the root is the block's first page");
    check(res.tables == 4u, "four table pages were laid out");
    check(res.writes == 4u, "three table descriptors plus one leaf");

    // ---- the root's entry for this iova points at table 1 ---------------
    {
        const unsigned idx = adreno_lpae_lvl_idx(iova, 0u, &geo);
        const uint64_t word = load64(block, idx);
        check(word == adreno_lpae_table_pte(pbase + 4096ull),
              "root entry points at the second table");
        check(adreno_lpae_pte_type(word) == kAdrenoLpaePteTypeTable,
              "and it is a TABLE descriptor");
    }

    // ---- the leaf is a PAGE descriptor carrying the paddr ---------------
    {
        const unsigned idx = adreno_lpae_lvl_idx(iova, 3u, &geo);
        const uint64_t word = load64(block + 3u * 4096u, idx);
        check(adreno_lpae_pte_type(word) == kAdrenoLpaePteTypePage,
              "the leaf is a PAGE descriptor");
        check((word & kAdrenoLpaePteAddrMask) == pbase,
              "the leaf carries the mapped physical address");
        check((word & kAdrenoLpaePteValid) != 0ull, "the leaf is VALID");
        check((word & kAdrenoLpaePteAf) != 0ull, "the leaf carries AF");
    }

    // ---- exactly four 64-bit words were written ------------------------
    {
        unsigned words = 0;
        for (unsigned i = 0; i < sizeof block; i += 8u) {
            if (load64(block, i / 8u) != 0ull) {
                ++words;
            }
        }
        check(words == 4u, "exactly four descriptors were written");
    }

    // ---- a second mapping into the same block reuses the root ----------
    // (this is what a real caller does: one root, many children)
    check(adreno_table_build(&geo, vbase, pbase, iova + 0x1000u, pbase + 0x1000u,
                             0x1000u, kAdrenoLpaePteValid | kAdrenoLpaePteAf,
                             &res) == 0u,
          "a second page in the same 2 MiB window builds");
    check(res.root_paddr == pbase, "the root is still the same page");

    // ---- refusals ------------------------------------------------------
    check(adreno_table_build(nullptr, vbase, pbase, 0u, 0u, 0x1000u, 0u, &res) != 0u,
          "null geometry is refused");
    check(adreno_table_build(&geo, vbase + 1u, pbase, 0u, 0u, 0x1000u, 0u, &res) != 0u,
          "a misaligned virtual base is refused");
    check(adreno_table_build(&geo, vbase, pbase + 1u, 0u, 0u, 0x1000u, 0u, &res) != 0u,
          "a misaligned physical base is refused");
    check(adreno_table_build(&geo, vbase, pbase, 0u, 0u, 0x1008u, 0u, &res) != 0u,
          "a non-block size is refused");

    // ---- the module's own self-check -----------------------------------
    unsigned int checks = 0;
    check(adreno_table_verify(&checks) == 0, "module self-check passes");
    check(checks >= 20u, "self-check covers the layout and the refusals");
    check(adreno_table_verify(nullptr) == 0, "self-check tolerates a null counter");

    std::printf("%u/%u checks passed\n", g_checks - g_fails, g_checks);
    if (g_fails != 0) {
        std::printf("FAIL: AdrenoSmmuTableV1 (%u failures)\n", g_fails);
        return 1;
    }
    std::printf("PASS: AdrenoSmmuTableV1 LPAE table set inside an allocated block\n");
    return 0;
}
