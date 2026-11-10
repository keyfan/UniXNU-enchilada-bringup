/* SPDX-License-Identifier: MIT */
/* Round 44: A6XX_GEN1 bus adapter (see AdrenoGmuBusV1.h for the contract).
 * Freestanding pure C; no mapping, clock, regulator or power action here. */
#include "AdrenoGmuBusV1.h"

struct AdrenoGmuBusV1Cookie {
    volatile uint8_t *base[kAdrenoGmuBusV1DomainCount];
    uint32_t          bytes[kAdrenoGmuBusV1DomainCount];
    const uint32_t   *bulk_image;   /* optional ITCM image (round 46) */
    uint32_t          bulk_words;   /* image capacity in dwords */
};

static struct AdrenoGmuBusV1Cookie g_gmu_bus_v1_cookie;

/* Word-offset addressing: upstream does mmio + (offset << 2) with the same
 * word-offset convention for GMU, RSCC (a6xx_gmu.h:104-152) and the two PDC
 * windows (PlanRpmhInit ops carry word offsets per the model contract). */
int AdrenoGmuBusV1Resolve(void *cookie, uint32_t domain, uint32_t word_offset,
                          volatile uint32_t **out)
{
    struct AdrenoGmuBusV1Cookie *c = (struct AdrenoGmuBusV1Cookie *)cookie;
    if (c == 0 || out == 0 || domain >= kAdrenoGmuBusV1DomainCount)
        return 1;
    if (c->base[domain] == 0)
        return 1;
    {
        uint64_t byte_off = (uint64_t)word_offset << 2;
        uint64_t win = c->bytes[domain];
        /* keep the last 4 bytes addressable: offset + 4 <= bytes */
        if (byte_off + 4u > win)
            return 1;
        *out = (volatile uint32_t *)(c->base[domain] + byte_off);
        return 0;
    }
}

static int bus_write(void *cookie, uint32_t domain, uint32_t offset,
                     uint32_t value)
{
    volatile uint32_t *p;
    if (AdrenoGmuBusV1Resolve(cookie, domain, offset, &p) != 0)
        return 1;
    *p = value;
    return 0;
}

static int bus_write_bulk(void *cookie, uint32_t domain, uint32_t offset,
                          const uint32_t *values, uint32_t words)
{
    volatile uint32_t *p;
    struct AdrenoGmuBusV1Cookie *c = (struct AdrenoGmuBusV1Cookie *)cookie;
    if (c == 0 || domain >= kAdrenoGmuBusV1DomainCount ||
        c->base[domain] == 0)
        return 1;
    /* Round 46: values == NULL is legal ONLY when the caller registered a
     * bulk image in AdrenoGmuBusV1Windows (the model contract sends NULL
     * and expects the cookie to resolve the ITCM image). No image and
     * NULL values -> fail closed (unchanged r44 rule for unregistered
     * callers). */
    const uint32_t *src = values;
    if (src == 0) {
        if (c->bulk_image == 0 || words == 0 || words > c->bulk_words)
            return 1;
        src = c->bulk_image;
    }
    if (words == 0)
        return 1;
    {
        uint64_t span = ((uint64_t)offset << 2) + ((uint64_t)words << 2);
        if (span > c->bytes[domain])
            return 1;
    }
    if (AdrenoGmuBusV1Resolve(cookie, domain, offset, &p) != 0)
        return 1;
    for (uint32_t i = 0; i < words; ++i)
        p[i] = src[i];
    return 0;
}

static int bus_read(void *cookie, uint32_t domain, uint32_t offset,
                    uint32_t *out_value)
{
    volatile uint32_t *p;
    if (out_value == 0)
        return 1;
    if (AdrenoGmuBusV1Resolve(cookie, domain, offset, &p) != 0)
        return 1;
    *out_value = *p;
    return 0;
}

int AdrenoGmuBusV1Make(const struct AdrenoGmuBusV1Windows *windows,
                       AdrenoGmuBusV1 *bus)
{
    if (windows == 0 || bus == 0)
        return 1;
    if (windows->gmu_mmio == 0 || windows->gmu_bytes == 0)
        return 1;
    /* RSCC lives inside the GMU mapping in upstream; it is derived, not a
     * separate window, so only the RSCC slice is bounds-checked against the
     * GMU window at resolve time. PDC windows may be NULL when the caller
     * has not mapped them yet (PlanRpmhInit is optional before PlanCold). */
    g_gmu_bus_v1_cookie.base[kAdrenoGmuBusV1DomainGmu] = windows->gmu_mmio;
    g_gmu_bus_v1_cookie.bytes[kAdrenoGmuBusV1DomainGmu] = windows->gmu_bytes;
    g_gmu_bus_v1_cookie.base[kAdrenoGmuBusV1DomainRscc] =
        windows->gmu_mmio + ADRENO_GMU_BUS_V1_RSCC_DELTA;
    g_gmu_bus_v1_cookie.bytes[kAdrenoGmuBusV1DomainRscc] =
        (windows->gmu_bytes > ADRENO_GMU_BUS_V1_RSCC_DELTA)
            ? windows->gmu_bytes - ADRENO_GMU_BUS_V1_RSCC_DELTA : 0u;
    g_gmu_bus_v1_cookie.base[kAdrenoGmuBusV1DomainPdcSeq] = windows->pdc_seq_mmio;
    g_gmu_bus_v1_cookie.bytes[kAdrenoGmuBusV1DomainPdcSeq] = windows->pdc_seq_bytes;
    g_gmu_bus_v1_cookie.base[kAdrenoGmuBusV1DomainPdc] = windows->pdc_mmio;
    g_gmu_bus_v1_cookie.bytes[kAdrenoGmuBusV1DomainPdc] = windows->pdc_bytes;
    g_gmu_bus_v1_cookie.bulk_image = windows->bulk_image;
    g_gmu_bus_v1_cookie.bulk_words = windows->bulk_words;

    bus->cookie = &g_gmu_bus_v1_cookie;
    bus->write = bus_write;
    bus->write_bulk = bus_write_bulk;
    bus->read = bus_read;
    return 0;
}
