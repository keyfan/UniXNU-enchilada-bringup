// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuInstallV1 -- turn the nine-word context plan into absolute register
// offsets by resolving CBn_BASE, which is the one thing between "the plan is known"
// and "the writes can be issued".
//
// WHY THIS EXISTS
// ---------------
// AdrenoSmmuCtxWordV1 (round 83) computes NINE context-bank writes in upstream order:
// CBA2R, CBAR, TCR2, TCR, TTBR0, TTBR1, S1_MAIR0, S1_MAIR1, SCTLR. Its `gr1` flag says
// whether an offset is already absolute in the GR1 frame or relative to the bank.
// What it does NOT answer is where that bank lives, because that depends on ID1 --
// and ID1 was only decoded (round 227) after the plan was written.
//
// Upstream (arm-smmu.c, arm_smmu_device_cfg_probe) derives it as:
//
//     smmu->pgshift = (id & ARM_SMMU_ID1_PAGESIZE) ? 16 : 12;
//     size          = 1 << (FIELD_GET(ARM_SMMU_ID1_NUMPAGENDXB, id) + 1);
//     smmu->numpage = size;
//     #define ARM_SMMU_CB(s, n)  ((s)->numpage + (n))
//
// so a bank's base is  `(size + n) << pgshift`  in bytes. This module computes exactly
// that and adds it to every bank-relative offset, so a caller has absolute offsets and
// cannot apply the base twice.
//
// THE MEASURED INPUTS FOR THIS DEVICE (all from retained device rows)
// ------------------------------------------------------------------
//   ID1 = 0x20008005  -> NUMCB 5, NUMS2CB 0, NUMPAGENDXB 2, PAGESIZE 0
//   -> pgshift 12, size 8 pages, CBn_BASE = 0x8000 + n * 0x1000
//
// BOUNDARY
// --------
// MODELLED : the base derivation, the absolute-offset arithmetic, and the refusal of a
//            bank index beyond what ID1 reports.
// NOT done : no MMIO. A caller issues the writes. And nothing here asserts that any
//            write has ever reached the SMMU.

#include "AdrenoSmmuInstallV1.h"

uint32_t adreno_install_cb_geometry(uint32_t id1, struct adreno_install_geo *out)
{
    uint32_t pagesize, ndxb, size;

    if (out == 0) {
        return 1u;
    }
    out->pgshift = 0u;
    out->numpage = 0u;
    out->cb_base = 0u;
    out->num_context_banks = 0u;

    /* num_context_banks is what bounds a legitimate bank index. A zero count means
     * either the read did not land or this is not an SMMU, so refuse rather than
     * divide the address space up blindly. */
    out->num_context_banks = id1 & kAdrenoInstallId1NumcbMask;
    if (out->num_context_banks == 0u) {
        return 2u;
    }

    pagesize = (id1 & kAdrenoInstallId1Pagesize) ? 16u : 12u;
    ndxb = (id1 & kAdrenoInstallId1NumpagendxbMask) >> kAdrenoInstallId1NumpagendxbShift;
    /* size = 1 << (NUMPAGENDXB + 1); the field is three bits, so size <= 256 pages. */
    size = 1u << (ndxb + 1u);

    out->pgshift = pagesize;
    out->numpage = size;
    out->cb_base = (size << pagesize);
    return 0u;
}

uint32_t adreno_install_bank_base(const struct adreno_install_geo *geo,
                                  uint32_t cbndx, uint64_t *out)
{
    if (geo == 0 || out == 0) {
        return 1u;
    }
    *out = 0ull;
    /* A bank index at or beyond NUMCB is not a bank. Refusing beats wrapping into the
     * next frame, which on this hardware would silently target something else. */
    if (cbndx >= geo->num_context_banks) {
        return 2u;
    }
    *out = (uint64_t)geo->cb_base + ((uint64_t)cbndx << geo->pgshift);
    return 0u;
}

uint32_t adreno_install_absolute(const struct adreno_install_geo *geo,
                                 uint32_t cbndx,
                                 const struct adreno_smmu_ctx_write *in,
                                 uint32_t count,
                                 struct adreno_install_write *out,
                                 uint32_t cap, uint32_t *written)
{
    uint64_t base = 0ull;
    uint32_t i;

    if (geo == 0 || in == 0 || out == 0 || written == 0) {
        return 1u;
    }
    *written = 0u;
    if (count > cap) {
        return 2u;
    }
    if (adreno_install_bank_base(geo, cbndx, &base) != 0u) {
        return 3u;
    }

    for (i = 0u; i < count; ++i) {
        /* A GR1 offset already carries the bank index (upstream encodes it into the
         * register number), so only a bank-relative offset takes the base. Applying
         * the base to both is the mistake this function exists to prevent. */
        out[i].offset = in[i].gr1 ? (uint64_t)in[i].offset
                                  : base + (uint64_t)in[i].offset;
        out[i].value = in[i].value;
        out[i].value_hi = in[i].value_hi;
        out[i].is_64bit = in[i].is_64bit;
        out[i].gr1 = in[i].gr1;
        out[i].order = in[i].order;
    }
    *written = count;
    return 0u;
}

/* ---- self-check -------------------------------------------------------- */

int adreno_install_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    struct adreno_install_geo geo;
    struct adreno_smmu_ctx_write plan[12];
    struct adreno_install_write abs[12];
    struct adreno_smmu_ctx_params p;
    uint32_t n = 0u;
    uint32_t i;

    /* ---- the field encodings, straight from P1 ------------------------- */
    checks += 1u;
    if (kAdrenoInstallId1Pagesize != (1u << 31)) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoInstallId1NumpagendxbMask != (0x7u << 28) ||
        kAdrenoInstallId1NumpagendxbShift != 28u) {
        fails += 1u;
    }
    checks += 1u;
    if (kAdrenoInstallId1NumcbMask != 0xffu) {
        fails += 1u;
    }

    /* ---- THE MEASURED WORD: id1 = 0x20008005 --------------------------- */
    checks += 1u;
    if (adreno_install_cb_geometry(0x20008005u, &geo) != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (geo.num_context_banks != 5u) {
        fails += 1u;
    }
    checks += 1u;
    if (geo.pgshift != 12u) {
        fails += 1u; /* PAGESIZE bit clear -> 4 KiB */
    }
    checks += 1u;
    if (geo.numpage != 8u) {
        fails += 1u; /* NUMPAGENDXB = 2 -> size = 1 << 3 */
    }
    checks += 1u;
    if (geo.cb_base != 0x8000ull) {
        fails += 1u; /* 8 pages << 12 */
    }

    /* ---- bank bases ---------------------------------------------------- */
    checks += 1u;
    {
        uint64_t b = 0ull;
        if (adreno_install_bank_base(&geo, 0u, &b) != 0u || b != 0x8000ull) {
            fails += 1u;
        }
    }
    checks += 1u;
    {
        uint64_t b = 0ull;
        /* The last of the five banks: 0x8000 + 4 * 0x1000 = 0xc000. */
        if (adreno_install_bank_base(&geo, 4u, &b) != 0u || b != 0xc000ull) {
            fails += 1u;
        }
    }
    checks += 1u;
    {
        uint64_t b = 0ull;
        if (adreno_install_bank_base(&geo, 5u, &b) == 0u) {
            fails += 1u; /* cbndx == NUMCB is not a bank */
        }
    }

    /* ---- a 16 KiB page SMMU shifts the base up ------------------------- */
    checks += 1u;
    {
        struct adreno_install_geo g16;
        /* PAGESIZE bit set, NUMPAGENDXB still 2 -> size 8 pages of 64 KiB */
        if (adreno_install_cb_geometry(0xa0008005u, &g16) != 0u) {
            fails += 1u;
        }
        checks += 1u;
        if (g16.pgshift != 16u || g16.cb_base != (8ull << 16)) {
            fails += 1u;
        }
    }

    /* ---- zero NUMCB is refused ---------------------------------------- */
    /* Use a SEPARATE geo here: a refused call leaves its output zeroed, and reusing
     * `geo` would wipe the measured geometry that the assertions below depend on. That
     * is exactly the bug this comment now prevents from coming back. */
    checks += 1u;
    {
        struct adreno_install_geo bad;
        if (adreno_install_cb_geometry(0x20008000u, &bad) == 0u) {
            fails += 1u;
        }
        /* and it must have left the output zeroed rather than half-filled */
        checks += 1u;
        if (bad.num_context_banks != 0u || bad.cb_base != 0u || bad.pgshift != 0u) {
            fails += 1u;
        }
    }
    checks += 1u;
    if (adreno_install_cb_geometry(0x20008005u, 0) == 0u) {
        fails += 1u;
    }

    /* ---- the nine-word plan, made absolute ---------------------------- */
    /* Byte-wise zero: `sizeof p / sizeof p` is the constant 1, which would clear only
     * the first word and leave every later field holding stack garbage. A byte loop
     * needs no assumption about the struct's layout at all. */
    for (i = 0u; i < (uint32_t)sizeof p; ++i) {
        ((unsigned char *)&p)[i] = 0u;
    }
    p.cbndx = 0u;
    p.asid = 0u;
    p.ttbr = 0x90000000ull;   /* a page-aligned root, as the builder returns */
    p.ias = 36u;              /* measured: ID2.UBS -> 48, the stage-1 va width */
    p.oas = 36u;
    p.coherent_walk = 1u;     /* measured: ID0.CTTW = 1 on this device */
    p.aarch64 = 1u;
    checks += 1u;
    if (adreno_smmu_ctx_plan(&p, plan, 12u, &n) != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (n != 9u) {
        fails += 1u;
    }

    checks += 1u;
    {
        /* A dedicated out-parameter: reusing the loop index here silently overwrote it
         * before the comparisons below, which is how this check first went wrong. */
        uint32_t written = 0u;
        if (adreno_install_absolute(&geo, 0u, plan, n, abs, 12u, &written) != 0u) {
            fails += 1u;
        }
        checks += 1u;
        if (written != n) {
            fails += 1u;
        }
    }
    /* TTBR0 must be a bank-relative offset, so it moved by the base; CBAR must be a
     * GR1 offset and must NOT have moved. */
    checks += 1u;
    {
        uint32_t ttbr0_off = 0u, ttbr0_val_ok = 0u, cbar_off = 0u, cbar_moved = 0u;
        /* A found-flag, because CBAR's offset is legitimately 0 for bank 0 and cannot
         * double as the "not seen" sentinel. */
        uint32_t gr1_seen = 0u;
        for (i = 0u; i < n; ++i) {
            /* TTBR0 is BANK-RELATIVE at 0x20. Matching on the offset alone would also
             * catch a GR1 register that happens to share it, so gr1 is part of the test. */
            if (plan[i].offset == kAdrenoInstallCbTtbr0 && plan[i].gr1 == 0u) {
                ttbr0_off = (uint32_t)abs[i].offset;
                ttbr0_val_ok = (abs[i].value == ((uint32_t)p.ttbr)) ? 1u : 0u;
            }
            if (plan[i].gr1) {
                cbar_off = plan[i].offset;
                gr1_seen = 1u;
                if (abs[i].offset != (uint64_t)plan[i].offset) {
                    cbar_moved = 1u;
                }
            }
            /* The offset-zero pair: GR1 CBAR must stay at 0, bank-relative CB SCTLR must
             * move to the bank base. This is the collision ctx-word's header names. */
            if (plan[i].offset == 0u) {
                if (plan[i].gr1 && abs[i].offset != 0ull) {
                    cbar_moved = 1u;
                }
                if (!plan[i].gr1 && abs[i].offset != (uint64_t)geo.cb_base) {
                    cbar_moved = 1u;
                }
            }
        }
        /* Four separate checks: this block used to have one check and four failure
         * sites, which made a single failure impossible to attribute. */
        checks += 1u;
        if (ttbr0_off != (uint32_t)(0x8020u)) {
            fails += 1u;  /* 0x8000 base + CB_TTBR0 0x20 */
        }
        checks += 1u;
        if (!ttbr0_val_ok) {
            fails += 1u;
        }
        checks += 1u;
        if (cbar_moved != 0u) {
            fails += 1u;  /* a GR1 offset must keep its absolute position */
        }
        checks += 1u;
        if (gr1_seen == 0u) {
            fails += 1u;  /* there must have been at least one GR1 write */
        }
        /* CBAR for bank 0 sits at offset 0, so this asserts the VALUE rather than
         * treating zero as absence. */
        checks += 1u;
        if (cbar_off != 0u) {
            fails += 1u;  /* GR1_CBAR(0) = 0x0 + (0 << 2) */
        }
    }

    /* ---- refusals ------------------------------------------------------ */
    checks += 1u;
    if (adreno_install_absolute(0, 0u, plan, n, abs, 12u, &i) == 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_install_absolute(&geo, 0u, plan, n, abs, 2u, &i) == 0u) {
        fails += 1u; /* capacity too small */
    }
    checks += 1u;
    if (adreno_install_absolute(&geo, 9u, plan, n, abs, 12u, &i) == 0u) {
        fails += 1u; /* bank index beyond NUMCB */
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return (fails == 0u) ? 0 : 1;
}
