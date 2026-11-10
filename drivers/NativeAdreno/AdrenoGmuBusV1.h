/* SPDX-License-Identifier: MIT */
#ifndef ADRENO_GMU_BUS_V1_H
#define ADRENO_GMU_BUS_V1_H

#include <stdint.h>
#include "AdrenoGmuBootV1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Round 44: A6XX_GEN1 bus adapter between AdrenoGmuBootV1's domain/offset op
 * model and the real MMIO windows. This is the TRANSLATION layer only: it
 * performs NO mapping, NO clock, NO regulator and NO power action. The caller
 * must have (1) CX/GX genpd on, (2) the 6 GPU clocks on, (3) RPMh/PDC setup
 * executed (PlanRpmhInit), BEFORE wiring a real cookie into this adapter.
 *
 * Upstream contract (Linux v6.12 a6xx_gmu.h:104-152):
 *   gmu_read/write(rscc):  mmio + (offset << 2)  -- word-offset addressing
 *   gmu->rscc:             gmu->mmio + 0x23000   -- a6xx_gmu.c:1771
 * So every op offset is a WORD offset; the adapter multiplies by 4.
 *
 * AdrenoGmuBootV1Domain -> window:
 *   kAdrenoGmuBootV1DomainGmu    -> gmu_mmio            (GMU core window)
 *   kAdrenoGmuBootV1DomainRscc   -> gmu_mmio + 0x23000  (RSCC delta window)
 *   kAdrenoGmuBootV1DomainPdcSeq -> pdc_seq_mmio        ("gmu_pdc_seq" MMIO)
 *   kAdrenoGmuBootV1DomainPdc    -> pdc_mmio            ("gmu_pdc" MMIO)
 * Domains are numbered in the exact AdrenoGmuBootV1.h order above.
 *
 * HOST-VERIFIED: source text is a specification, not a device measurement.
 */

#define ADRENO_GMU_BUS_V1_RSCC_DELTA   0x23000u  /* a6xx_gmu.c:1771 */

enum {
    kAdrenoGmuBusV1DomainGmu    = 0,
    kAdrenoGmuBusV1DomainRscc   = 1,
    kAdrenoGmuBusV1DomainPdcSeq = 2,
    kAdrenoGmuBusV1DomainPdc    = 3,
    kAdrenoGmuBusV1DomainCount  = 4,
};

struct AdrenoGmuBusV1Windows {
    volatile uint8_t *gmu_mmio;      /* GMU core window base (0x506a000) */
    uint32_t           gmu_bytes;    /* size guard, bytes */
    volatile uint8_t *pdc_seq_mmio;  /* "gmu_pdc_seq" window base */
    uint32_t           pdc_seq_bytes;/* size guard, bytes */
    volatile uint8_t *pdc_mmio;      /* "gmu_pdc" window base */
    uint32_t           pdc_bytes;    /* size guard, bytes */
};

/* The bus the plan executor consumes. cookie points at the resolved window
 * table (windows pointer + byte guards); all callbacks bounds-check against
 * the per-window byte size and FAIL CLOSED (return 1) on any violation. */
typedef AdrenoGmuBootV1Bus AdrenoGmuBusV1;

/* Resolve a domain + word offset to a byte address inside the caller's
 * windows. Returns 0 and stores the pointer on success; nonzero (and NULL)
 * when the domain is unknown or the access leaves the window. */
int AdrenoGmuBusV1Resolve(void *cookie, uint32_t domain, uint32_t word_offset,
                          volatile uint32_t **out);

/* Fill a ready-to-use bus whose callbacks implement read/write/write_bulk
 * against the windows above. Returns 0 on success; nonzero when windows is
 * NULL or either window base/size is invalid. */
int AdrenoGmuBusV1Make(const struct AdrenoGmuBusV1Windows *windows,
                       AdrenoGmuBusV1 *bus);

#ifdef __cplusplus
}
#endif
#endif /* ADRENO_GMU_BUS_V1_H */
