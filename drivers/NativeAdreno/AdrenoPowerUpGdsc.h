// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoPowerUpGdsc.h -- COMPOSITION of the round-57 derived GDSC step plans
// with the round-51 ordered genpd/clock power-up plan (A6XX_GEN1 / A630).
//
// Why this module exists
// ----------------------
// Round 57 DERIVED what the CPU actually writes when it turns a GPUCC domain on
// or off. Round 51 built the ordered 5-step power-up. Until now nothing tied the
// two together: AdrenoPowerUpV1Execute() hands step 0 to bus->genpd_on(cookie,0)
// and step 1 to bus->genpd_on(cookie,1) and simply TRUSTS that the callback
// performs the derived writes. If a future bus implementation turned out to be a
// silent no-op, every existing test would still pass. This module turns that
// trust into a checkable contract.
//
// The load-bearing derived invariant
// ----------------------------------
// For EVERY genpd step of the power-up plan,
//
//     StepGuarded(step) == (the CPU's register writes for that domain == 0)
//
// holds on both domains, and it is DERIVED, not asserted:
//   * CX: AdrenoPowerUpV1StepGuarded(CX) == 0 and the CX CPU enable plan does
//     real work: ONE register write (clear the collapse bit at 0x106c) followed
//     by a status POLL at 0x1540, which is a READ. Counting a read as a write
//     was the first mistake this module caught: register_writes is 1, not 2.
//   * GX: AdrenoPowerUpV1StepGuarded(GX) == 1 and the GX CPU enable plan writes
//     0 registers, because gpu_gx_gdsc.pd.power_on is gdsc_gx_do_nothing_enable()
//     and the descriptor has neither .supply nor .cxcs.
// So the guarded bit is not a stylistic choice: it marks exactly the step the
// CPU CANNOT act on. That is the upstream reason it is guarded too
// (a6xx_gmu.c:1040-1041 guards on !IS_ERR_OR_NULL(gmu->gxpd) -- the GX genpd
//  exists or not, but either way the CPU never pokes the GX GDSC).
//
// The second finding (a safety boundary, not a failure)
// -----------------------------------------------------
// CX disable writes 1 register and polls NOTHING (gdsc.c:153-161 VOTABLE
// short-cut, mirrored by the board DTB qcom,no-status-check-on-disable). So
// after a successful CX power-off the CPU has NO confirmation that the domain
// actually collapsed. Downstream code must therefore never infer "CX is quiet"
// from "CX-disable returned 0". This module exposes that as
// AdrenoPowerUpGdscDisableIsUnverified() rather than leaving it as folklore,
// because round 53 established that touching a powered-down GPU-adjacent block
// (the kgsl SMMU) may bus-abort.
//
// Honest boundary: this is a HOST-VERIFIED derivation from sha256-pinned Linux
// v6.12 sources plus the vendored board DTB. No MMIO is performed, no domain is
// powered, no firmware is loaded, and nothing here is a device measurement.

#ifndef ADRENO_POWER_UP_GDSC_H
#define ADRENO_POWER_UP_GDSC_H

#include <stdint.h>

#include "AdrenoGdscDecode.h"

// Derived facts about the composite plan. Every field is COMPUTED at call time
// from AdrenoGdscDecode + AdrenoPowerUpV1; none is a stored constant.
struct adreno_power_up_gdsc_contract {
    // Register-write counts for the CPU path of each domain's ENABLE plan.
    uint32_t cx_enable_writes;
    uint32_t gx_enable_writes;
    // Step counts (including non-writing steps such as UDELAY).
    uint32_t cx_enable_steps;
    uint32_t gx_enable_steps;
    // The CX status poll, read back out of the derived plan.
    uint32_t cx_poll_offset;   // 0 when the plan has no POLL_ON
    uint32_t cx_poll_mask;
    // Register-write counts for the CPU path of each domain's DISABLE plan.
    uint32_t cx_disable_writes;
    uint32_t gx_disable_writes;
    uint32_t cx_disable_polls; // 0 => the VOTABLE short-cut applies
    uint32_t gx_disable_polls; // >= 1 => GX polls its down-complete bit
    // The power-up plan's guarded bits, copied from AdrenoPowerUpV1.
    uint32_t cx_step_guarded;
    uint32_t gx_step_guarded;
};

// Fills *out with the derived contract. Returns 0 on success, non-zero when an
// argument is NULL or a plan could not be built (fail-closed: the caller must
// not proceed on a non-zero return).
int AdrenoPowerUpGdscContract(struct adreno_power_up_gdsc_contract *out);

// The number of CPU register writes the power-up step performs, derived from the
// domain's enable plan. Returns 0 for a guarded/absent domain (which is the
// point) and also for an unknown step index.
uint32_t AdrenoPowerUpGdscStepWrites(uint32_t step);

// 1 when the guarded flag of a genpd step agrees with "the CPU writes nothing",
// for both steps. This is the invariant above, evaluated live.
int AdrenoPowerUpGdscGuardMatchesWrites(void);

// 1 when the domain's CPU power-off cannot be confirmed by polling, i.e. the
// VOTABLE short-cut applies. 0 for a domain that polls its off status.
int AdrenoPowerUpGdscDisableIsUnverified(uint32_t which);

// Cross-check the whole contract. Returns 0 when every derived invariant holds,
// otherwise the 1-based index of the first failing check (so a host test can
// report which one). check_count receives the number of checks evaluated.
int AdrenoPowerUpGdscVerify(uint32_t *check_count);

#endif /* ADRENO_POWER_UP_GDSC_H */
