/* SPDX-License-Identifier: MIT */
/* Round 44 host tests for the A6XX_GEN1 bus adapter (AdrenoGmuBusV1).
 * Run by scripts/check-enchilada-adreno-host.py; asserts with abort(). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "AdrenoGmuBusV1.h"

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); abort(); } } while (0)

static uint32_t g_mem[64];

int main(void)
{
    /* Group 1: Make() validation */
    {
        AdrenoGmuBusV1 bus;
        struct AdrenoGmuBusV1Windows w;
        memset(&w, 0, sizeof w);
        CHECK(AdrenoGmuBusV1Make(0, &bus) != 0);
        CHECK(AdrenoGmuBusV1Make(&w, 0) != 0);
        CHECK(AdrenoGmuBusV1Make(&w, &bus) != 0); /* NULL gmu base */
        w.gmu_mmio = (volatile uint8_t *)g_mem;
        w.gmu_bytes = sizeof g_mem;
        CHECK(AdrenoGmuBusV1Make(&w, &bus) == 0);
        CHECK(bus.cookie != 0 && bus.write && bus.read && bus.write_bulk);
    }

    /* Group 2: resolve bounds (word-offset addressing) */
    {
        AdrenoGmuBusV1 bus;
        struct AdrenoGmuBusV1Windows w;
        volatile uint32_t *p = 0;
        memset(&w, 0, sizeof w);
        w.gmu_mmio = (volatile uint8_t *)g_mem;
        w.gmu_bytes = sizeof g_mem; /* 256 B = 64 words */
        CHECK(AdrenoGmuBusV1Make(&w, &bus) == 0);
        /* word 63 = last addressable */
        CHECK(AdrenoGmuBusV1Resolve(bus.cookie, kAdrenoGmuBootV1DomainGmu, 63, &p) == 0);
        CHECK(p == (volatile uint32_t *)((uint8_t *)g_mem + 252));
        /* word 64 leaves the window */
        CHECK(AdrenoGmuBusV1Resolve(bus.cookie, kAdrenoGmuBootV1DomainGmu, 64, &p) != 0);
        /* unknown domain */
        CHECK(AdrenoGmuBusV1Resolve(bus.cookie, 99, 0, &p) != 0);
        /* NULL cookie */
        CHECK(AdrenoGmuBusV1Resolve(0, kAdrenoGmuBootV1DomainGmu, 0, &p) != 0);
    }

    /* Group 3: RSCC is derived from the GMU window (+0x23000 delta) and its
     * byte guard shrinks accordingly */
    {
        AdrenoGmuBusV1 bus;
        struct AdrenoGmuBusV1Windows w;
        volatile uint32_t *p = 0;
        static uint8_t big[ADRENO_GMU_BUS_V1_RSCC_DELTA + 64];
        memset(&w, 0, sizeof w);
        w.gmu_mmio = big;
        w.gmu_bytes = ADRENO_GMU_BUS_V1_RSCC_DELTA + 64;
        CHECK(AdrenoGmuBusV1Make(&w, &bus) == 0);
        /* RSCC word 0 resolves into the slice right after the delta */
        CHECK(AdrenoGmuBusV1Resolve(bus.cookie, kAdrenoGmuBootV1DomainRscc, 0, &p) == 0);
        CHECK((const volatile uint8_t *)p == big + ADRENO_GMU_BUS_V1_RSCC_DELTA);
        /* RSCC words 0..15 fit (64 B / 4) */
        CHECK(AdrenoGmuBusV1Resolve(bus.cookie, kAdrenoGmuBootV1DomainRscc, 15, &p) == 0);
        CHECK(AdrenoGmuBusV1Resolve(bus.cookie, kAdrenoGmuBootV1DomainRscc, 16, &p) != 0);
        /* a GMU window exactly 0x23000 B has an EMPTY RSCC slice */
        w.gmu_bytes = ADRENO_GMU_BUS_V1_RSCC_DELTA;
        CHECK(AdrenoGmuBusV1Make(&w, &bus) == 0);
        CHECK(AdrenoGmuBusV1Resolve(bus.cookie, kAdrenoGmuBootV1DomainRscc, 0, &p) != 0);
    }

    /* Group 4: read/write round trip + unmapped PDC refuses */
    {
        AdrenoGmuBusV1 bus;
        struct AdrenoGmuBusV1Windows w;
        uint32_t v = 0;
        memset(&w, 0, sizeof w);
        w.gmu_mmio = (volatile uint8_t *)g_mem;
        w.gmu_bytes = sizeof g_mem;
        CHECK(AdrenoGmuBusV1Make(&w, &bus) == 0);
        CHECK(bus.write(bus.cookie, kAdrenoGmuBootV1DomainGmu, 10, 0x4052) == 0);
        CHECK(bus.read(bus.cookie, kAdrenoGmuBootV1DomainGmu, 10, &v) == 0);
        CHECK(v == 0x4052);
        /* PDC windows are NULL -> every PDC op fails closed */
        CHECK(bus.write(bus.cookie, kAdrenoGmuBootV1DomainPdc, 0, 1) != 0);
        CHECK(bus.write(bus.cookie, kAdrenoGmuBootV1DomainPdcSeq, 0, 1) != 0);
        CHECK(bus.read(bus.cookie, kAdrenoGmuBootV1DomainPdc, 0, &v) != 0);
    }

    /* Group 5: wired PDC window works; bulk refuses NULL data and OOB spans */
    {
        AdrenoGmuBusV1 bus;
        struct AdrenoGmuBusV1Windows w;
        static uint32_t pdc_mem[8192];
        uint32_t v = 0;
        uint32_t data[2] = {0x4520u, 0x4510u};
        memset(&w, 0, sizeof w);
        w.gmu_mmio = (volatile uint8_t *)g_mem;
        w.gmu_bytes = sizeof g_mem;
        w.pdc_mmio = (volatile uint8_t *)pdc_mem;
        w.pdc_bytes = sizeof pdc_mem;
        CHECK(AdrenoGmuBusV1Make(&w, &bus) == 0);
        CHECK(bus.write(bus.cookie, kAdrenoGmuBootV1DomainPdc,
                        ADRENO_GMU_BOOT_V1_PDC_ENABLE_PDC,
                        ADRENO_GMU_BOOT_V1_ENABLE_PDC_VALUE) == 0);
        CHECK(bus.read(bus.cookie, kAdrenoGmuBootV1DomainPdc,
                       ADRENO_GMU_BOOT_V1_PDC_ENABLE_PDC, &v) == 0);
        CHECK(v == ADRENO_GMU_BOOT_V1_ENABLE_PDC_VALUE);
        CHECK(bus.write_bulk(bus.cookie, kAdrenoGmuBootV1DomainPdc, 0, data, 2) == 0);
        CHECK(bus.write_bulk(bus.cookie, kAdrenoGmuBootV1DomainPdc, 0, 0, 2) != 0);
        CHECK(bus.write_bulk(bus.cookie, kAdrenoGmuBootV1DomainPdc, 0, data, 0) != 0);
        /* 8193 words from word 0 exceeds the 8192-word PDC window */
        CHECK(bus.write_bulk(bus.cookie, kAdrenoGmuBootV1DomainPdc, 0, data, 8193) != 0);
    }

    /* Group 6: the adapter executes a REAL PlanRpmhInit stream end-to-end
     * against simulated windows (5 RSC ucode words + 5 PDC ucode words +
     * hidden-TCS + ENABLE_PDC), proving model->bus translation coherence. */
    {
        AdrenoGmuBusV1 bus;
        struct AdrenoGmuBusV1Windows w;
        static uint8_t gmu_win[ADRENO_GMU_BUS_V1_RSCC_DELTA + 2048];
        static uint32_t pdc_win[8192];
        static uint32_t pdc_seq_win[64];
        AdrenoGmuBootV1Op ops[ADRENO_GMU_BOOT_V1_MAX_OPS];
        uint32_t count = 0;
        /* upstream a6xx_gmu.c:561-575 ucode + :611-643 TCS/enable values */
        static const uint32_t rsc_ucode[ADRENO_GMU_BOOT_V1_RSC_UCODE_WORDS] = {
            0xa7a506a0u, 0x1e7aa6eu, 0x1000u, 0x0u, 0x0u };
        static const uint32_t pdc_ucode[ADRENO_GMU_BOOT_V1_PDC_UCODE_WORDS] = {
            0xfebea1e1u, 0x2u, 0x1000u, 0x0u, 0x0u };
        memset(&w, 0, sizeof w);
        w.gmu_mmio = gmu_win;
        w.gmu_bytes = sizeof gmu_win;
        w.pdc_mmio = (volatile uint8_t *)pdc_win;
        w.pdc_bytes = sizeof pdc_win;
        w.pdc_seq_mmio = (volatile uint8_t *)pdc_seq_win;
        w.pdc_seq_bytes = sizeof pdc_seq_win;
        CHECK(AdrenoGmuBusV1Make(&w, &bus) == 0);
        CHECK(AdrenoGmuBootV1PlanRpmhInit(rsc_ucode, pdc_ucode, ops,
                                          ADRENO_GMU_BOOT_V1_MAX_OPS,
                                          &count) == kAdrenoGmuBootV1Success);
        CHECK(count > 0 && count <= ADRENO_GMU_BOOT_V1_MAX_OPS);
        CHECK(AdrenoGmuBootV1Execute(ops, count, &bus) == kAdrenoGmuBootV1Success);
        /* ENABLE_PDC got the exact upstream value :643 */
        {
            uint32_t v = 0;
            CHECK(bus.read(bus.cookie, kAdrenoGmuBootV1DomainPdc,
                           ADRENO_GMU_BOOT_V1_PDC_ENABLE_PDC, &v) == 0);
            CHECK(v == ADRENO_GMU_BOOT_V1_ENABLE_PDC_VALUE);
        }
    }

    /* Group 7: a bounds violation inside a plan aborts the execute (fail
     * closed) instead of silently skipping. */
    {
        AdrenoGmuBusV1 bus;
        struct AdrenoGmuBusV1Windows w;
        static uint32_t small[4];
        AdrenoGmuBootV1Op ops[2];
        memset(&w, 0, sizeof w);
        w.gmu_mmio = (volatile uint8_t *)small;
        w.gmu_bytes = sizeof small;
        CHECK(AdrenoGmuBusV1Make(&w, &bus) == 0);
        memset(ops, 0, sizeof ops);
        ops[0].domain = kAdrenoGmuBootV1DomainGmu;
        ops[0].kind = kAdrenoGmuBootV1OpWrite;
        ops[0].offset = 3;                    /* last valid word */
        ops[0].value = 1;
        ops[1].domain = kAdrenoGmuBootV1DomainGmu;
        ops[1].kind = kAdrenoGmuBootV1OpWrite;
        ops[1].offset = 4;                    /* OUT of the 4-word window */
        ops[1].value = 2;
        CHECK(AdrenoGmuBootV1Execute(ops, 2, &bus) == kAdrenoGmuBootV1BusError);
        /* the first write DID land, the second did not */
        CHECK(small[3] == 1 && small[0] == 0);
    }

    printf("PASS: A6XX_GEN1 GMU bus adapter (7 test groups)\n");
    return 0;
}
