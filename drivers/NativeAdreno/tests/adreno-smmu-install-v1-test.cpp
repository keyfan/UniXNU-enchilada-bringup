// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for AdrenoSmmuInstallV1 -- CBn_BASE from ID1, and absolute offsets.
//
// Every expected value here is derived from the MEASURED ID1 word this device reports
// (0x20008005, identical in 103 retained windows) or from the pinned upstream formula,
// never from the module's own output.

#include <cstdio>
#include <cstring>

#include "../AdrenoSmmuInstallV1.h"

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
    struct adreno_install_geo geo;
    std::memset(&geo, 0, sizeof geo);

    // ---- the measured word: ID1 = 0x20008005 ----------------------------
    check(adreno_install_cb_geometry(0x20008005u, &geo) == 0u, "geometry derives");
    check(geo.num_context_banks == 5u, "NUMCB = 5");
    check(geo.pgshift == 12u, "PAGESIZE clear -> 4 KiB pages");
    check(geo.numpage == 8u, "NUMPAGENDXB 2 -> 8 pages of registers");
    check(geo.cb_base == 0x8000u, "CBn_BASE = 0x8000");

    // ---- per-bank bases -------------------------------------------------
    {
        uint64_t b = 0;
        check(adreno_install_bank_base(&geo, 0u, &b) == 0u && b == 0x8000u,
              "bank 0 sits at 0x8000");
        check(adreno_install_bank_base(&geo, 1u, &b) == 0u && b == 0x9000u,
              "bank 1 sits at 0x9000");
        check(adreno_install_bank_base(&geo, 4u, &b) == 0u && b == 0xc000u,
              "bank 4 sits at 0xc000");
        // cbndx == NUMCB is not a bank
        check(adreno_install_bank_base(&geo, 5u, &b) != 0u, "bank 5 is refused");
        check(adreno_install_bank_base(&geo, 0u, nullptr) != 0u,
              "a null out pointer is refused");
    }

    // ---- a 16 KiB page SMMU moves the frame up -------------------------
    {
        struct adreno_install_geo g16;
        // PAGESIZE bit set with NUMPAGENDXB still 2: 8 pages of 64 KiB
        check(adreno_install_cb_geometry(0xa0008005u, &g16) == 0u, "16K geometry derives");
        check(g16.pgshift == 16u, "PAGESIZE set -> 64 KiB pages");
        check(g16.cb_base == (8u << 16), "CBn_BASE scales with the page size");
    }

    // ---- refusals -------------------------------------------------------
    {
        struct adreno_install_geo bad;
        std::memset(&bad, 0xff, sizeof bad);
        check(adreno_install_cb_geometry(0x20008000u, &bad) != 0u, "zero NUMCB is refused");
        check(bad.num_context_banks == 0u && bad.cb_base == 0u && bad.pgshift == 0u,
              "a refused call leaves the output zeroed, not half-filled");
    }
    check(adreno_install_cb_geometry(0x20008005u, nullptr) != 0u, "null out is refused");

    // ---- the nine-word plan, made absolute ------------------------------
    struct adreno_smmu_ctx_params p;
    std::memset(&p, 0, sizeof p);
    p.cbndx = 0;
    p.asid = 0;
    p.ttbr = 0x90000000ull;   // a page-aligned root, as the builder returns
    p.ias = 36;               // measured stage-1 va width (ID2.UBS -> 48)
    p.oas = 36;
    p.coherent_walk = 1;      // measured: ID0.CTTW = 1
    p.aarch64 = 1;

    struct adreno_smmu_ctx_write plan[12];
    struct adreno_install_write abs[12];
    unsigned int n = 0, w = 0;
    check(adreno_smmu_ctx_plan(&p, plan, 12, &n) == 0, "the context plan derives");
    check(n == 9u, "nine writes, as upstream orders them");
    check(adreno_install_absolute(&geo, 0u, plan, n, abs, 12, &w) == 0u,
          "the plan is made absolute");
    check(w == n, "every write was converted");

    // TTBR0 is bank-relative at 0x20, so it must land at 0x8000 + 0x20.
    {
        unsigned ttbr0 = 0;
        for (unsigned i = 0; i < n; ++i) {
            if (plan[i].offset == kAdrenoInstallCbTtbr0 && plan[i].gr1 == 0u) {
                ttbr0 = static_cast<unsigned>(abs[i].offset);
                check(abs[i].value == static_cast<uint32_t>(p.ttbr),
                      "TTBR0 carries the root table's physical address");
            }
        }
        check(ttbr0 == 0x8020u, "TTBR0 offset is base + 0x20");
    }

    // CBAR is GR1 at 0x0 + (0 << 2) = 0, and must NOT have moved.
    {
        unsigned gr1_seen = 0, cbar_abs = 0xdead;
        for (unsigned i = 0; i < n; ++i) {
            if (plan[i].gr1) {
                gr1_seen = 1;
                if (plan[i].offset == 0u) {
                    cbar_abs = static_cast<unsigned>(abs[i].offset);
                }
            }
        }
        check(gr1_seen == 1, "at least one GR1 write exists");
        // A found-flag rather than a zero test: CBAR legitimately lives at offset 0.
        check(cbar_abs == 0u, "GR1 CBAR stays at offset 0");
    }

    // CB SCTLR is bank-relative at 0x00, so it must move to the bank base.
    {
        int saw_sctlr = 0;
        for (unsigned i = 0; i < n; ++i) {
            if (plan[i].gr1 == 0u && plan[i].offset == 0u) {
                saw_sctlr = 1;
                check(abs[i].offset == static_cast<uint64_t>(geo.cb_base),
                      "CB SCTLR moves to the bank base");
            }
        }
        check(saw_sctlr == 1, "the bank-relative SCTLR write exists");
    }

    // ---- refusals on the conversion ------------------------------------
    check(adreno_install_absolute(nullptr, 0u, plan, n, abs, 12, &w) != 0u,
          "null geometry is refused");
    check(adreno_install_absolute(&geo, 0u, plan, n, abs, 2, &w) != 0u,
          "too small a capacity is refused");
    check(adreno_install_absolute(&geo, 9u, plan, n, abs, 12, &w) != 0u,
          "a bank beyond NUMCB is refused");

    // ---- the module's own self-check -----------------------------------
    unsigned int checks = 0;
    check(adreno_install_verify(&checks) == 0, "module self-check passes");
    check(checks >= 28u, "self-check covers the geometry and the GR1 split");
    check(adreno_install_verify(nullptr) == 0, "self-check tolerates a null counter");

    std::printf("%u/%u checks passed\n", g_checks - g_fails, g_checks);
    if (g_fails != 0) {
        std::printf("FAIL: AdrenoSmmuInstallV1 (%u failures)\n", g_fails);
        return 1;
    }
    std::printf("PASS: AdrenoSmmuInstallV1 CBn_BASE from ID1 and absolute context offsets\n");
    return 0;
}
