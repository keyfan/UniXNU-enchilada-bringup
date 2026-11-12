// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuStage1V1 -- enchilada stage 5 (GPU) HOST-VERIFIED ARM SMMU v2
// STAGE-1 CONTEXT WORD MODEL (round 75, 2026-11-12).
//
// This is NOT a driver and NOT an MMIO sequence. It is the missing middle layer
// between round 53's SAFETY GATE ("may we touch this SMMU at all?" -- it answers
// no, and only decides) and a future context programmer: for one SMMU, one
// context bank and one stream, it computes the EXACT register words and the
// EXACT order, and then states honestly which of the needed INPUTS are still
// absent on enchilada.
//
// WHY THIS ROUND (admission table, prerequisite `kgsl-smmu-stage1`)
// ----------------------------------------------------------------
// That row has read `modeled = 0` since round 69, with the reason "round 53's
// work is a fail-closed READ-safety gate, not a stage-1 context". Re-reading
// round 53 confirms that is exactly right: AdrenoKGSLSmmuGate exposes
// permission predicates and a verdict enum, and its own header says it "merely
// DECIDES whether a future read would be safe; it never performs one". Nothing
// in the tree computed a TTBR0/TCR/SCTLR word. This round adds precisely that,
// so the row can stop reading "not modelled" without pretending a context has
// been programmed.
//
// PROVENANCE (3 sha256 pins re-checked by scripts/verify-enchilada-smmu-stage1.py)
// ------------------------------------------------------------------------------
//  (P1) drivers/iommu/arm/arm-smmu/arm-smmu.h  16630 B
//       ed85998794695d1a824dc1b86f680c8d124546688fd2665e00e3cbcfa9546b53
//       :145 ARM_SMMU_CB_SCTLR 0x00; :167 CB_TTBR0 0x20; :168 CB_TTBR1 0x28;
//       :171 CB_TCR 0x30; :191 CB_CONTEXTIDR 0x34; :192 CB_S1_MAIR0 0x38;
//       :193 CB_S1_MAIR1 0x3c; :198 CB_FSR 0x58.
//       :146-154 SCTLR bits: S1_ASIDPNE BIT(12), CFCFG BIT(7), HUPCF BIT(8),
//       CFIE BIT(6), CFRE BIT(5), E BIT(4), AFE BIT(2), TRE BIT(1), M BIT(0).
//       :105 ARM_SMMU_GR0_S2CR(n) 0xc00 + (n<<2); :123 GR1_CBAR(n) n<<2.
//       :106-120 S2CR fields: PRIVCFG GENMASK(25,24), TYPE GENMASK(17,16),
//       EXIDVALID BIT(10), CBNDX GENMASK(7,0); types TRANS 0, BYPASS 1, FAULT 2.
//  (P2) drivers/iommu/arm/arm-smmu/arm-smmu.c  67693 B
//       d5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a
//       :641-651 the SCTLR default for a context bank:
//       CFIE | CFRE | AFE | TRE | M, plus S1_ASIDPNE when stage1, plus E only
//       for a big-endian CPU. That mask is the load-bearing constant here.
//       :951-968 arm_smmu_write_s2cr(): TYPE | CBNDX | PRIVCFG (+EXIDVALID).
//       :65-66 s2cr_init_val: BYPASS when !disable_bypass, else FAULT.
//  (P3) drivers/iommu/io-pgtable-arm.c  38934 B
//       b78adbf87352e574556638fae1206d62bfbc06787a85125f75b9d5da0abab278
//       the stage-1 page-table format the TCR describes (ARM LPAE, 4 KiB granule
//       as this board's kgsl SMMU declares single-cell SIDs with 4 KiB pages per
//       the round-15 ArmSmmu4K kit).
//
// WHAT THIS ROUND *DOES NOT* DO, AND WHY THAT IS THE POINT
// --------------------------------------------------------
// Computing the words is not the same as being able to program them. FOUR
// INPUTS are still absent, and the module names each one instead of assuming it:
//   I1 a page table: TTBR0 must point at a real, populated, 4 KiB-granule LPAE
//      table. Nothing in this port allocates or walks one.
//   I2 the ASID: CONTEXTIDR encodes it, and the caller must own it. enchilada
//      has no ASID allocator.
//   I3 the stream -> context-bank binding: S2CR must be programmed for the
//      target SID FIRST, and round 53 measured that the target streams are still
//      in the firmware BYPASS handoff (`Busy=10`, `id0=4c017e4c`), i.e. they do
//      not fault yet. A context cannot be established until they do.
//   I4 a powered CX: every one of these writes needs the GPUCC CX domain up, and
//      round 35 measured CX DOWN (`cx=0x00005ff4`, BIT(31) clear).
// So `adreno_smmu_stage1_ready()` is derived from a four-bit input mask and
// returns 0, and the admission row moves to modelled=1 / satisfied=0.
//
// HONEST BOUNDARY
// ---------------
// HOST-VERIFIED, WORD MODEL ONLY. No MMIO, no mapping, no page table, no ASID,
// no IOKit, no caller. CONFIG_XNU_NATIVE_ADRENO remains n; the gpu area STAYS
// "host-verified"; no device run, no flash, m39510 NOT exercised. It does NOT
// unblock stage 5.
// ---------------------------------------------------------------------------

#ifndef ADRENO_SMMU_STAGE1_V1_H
#define ADRENO_SMMU_STAGE1_V1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Context-bank register byte offsets (P1:145-198) ------------------ */
#define kAdrenoSmmuCbSctlr      0x00u /* P1:145 */
#define kAdrenoSmmuCbActlr      0x04u /* P1:156 */
#define kAdrenoSmmuCbTcr2       0x10u /* P1:161 */
#define kAdrenoSmmuCbTtbr0      0x20u /* P1:167 */
#define kAdrenoSmmuCbTtbr1      0x28u /* P1:168 */
#define kAdrenoSmmuCbTcr        0x30u /* P1:171 */
#define kAdrenoSmmuCbContextidr 0x34u /* P1:191 */
#define kAdrenoSmmuCbS1Mair0    0x38u /* P1:192 */
#define kAdrenoSmmuCbS1Mair1    0x3cu /* P1:193 */
#define kAdrenoSmmuCbFsr        0x58u /* P1:198 */

/* ---- SCTLR bits (P1:146-154) ------------------------------------------ */
#define kAdrenoSmmuSctlrS1Asidpne 0x00001000u /* BIT(12) */
#define kAdrenoSmmuSctlrCfcfg     0x00000080u /* BIT(7)  */
#define kAdrenoSmmuSctlrHupcf     0x00000100u /* BIT(8)  */
#define kAdrenoSmmuSctlrCfie      0x00000040u /* BIT(6)  */
#define kAdrenoSmmuSctlrCfre      0x00000020u /* BIT(5)  */
#define kAdrenoSmmuSctlrE         0x00000010u /* BIT(4)  */
#define kAdrenoSmmuSctlrAfe       0x00000004u /* BIT(2)  */
#define kAdrenoSmmuSctlrTre       0x00000002u /* BIT(1)  */
#define kAdrenoSmmuSctlrM         0x00000001u /* BIT(0)  */
/* P2:642-645 the SCTLR a stage-1 context bank gets on a LITTLE-endian host:
 * CFIE | CFRE | AFE | TRE | M plus S1_ASIDPNE. E (BIT(4)) is added only when
 * CONFIG_CPU_BIG_ENDIAN, which this board is not. */
#define kAdrenoSmmuSctlrStage1Default \
    (kAdrenoSmmuSctlrCfie | kAdrenoSmmuSctlrCfre | kAdrenoSmmuSctlrAfe | \
     kAdrenoSmmuSctlrTre | kAdrenoSmmuSctlrM | kAdrenoSmmuSctlrS1Asidpne)

/* ---- GR0 S2CR and its fields (P1:105-120) ----------------------------- */
#define kAdrenoSmmuGr0S2crBase  0x0c00u /* P1:105 0xc00 + (n<<2) */
#define kAdrenoSmmuGr1CbarBase  0x0000u /* P1:123 n<<2           */
#define kAdrenoSmmuS2crPrivcfgMask 0x03000000u /* GENMASK(25,24) */
#define kAdrenoSmmuS2crTypeMask    0x00030000u /* GENMASK(17,16) */
#define kAdrenoSmmuS2crExidvalid   0x00000400u /* BIT(10)        */
#define kAdrenoSmmuS2crCbndxMask   0x000000ffu /* GENMASK(7,0)   */

enum adreno_smmu_s2cr_type {
    kAdrenoSmmuS2crTypeTrans = 0,  /* P1:115 */
    kAdrenoSmmuS2crTypeBypass = 1, /* P1:116 */
    kAdrenoSmmuS2crTypeFault = 2   /* P1:117 */
};

/* ---- The four inputs a real context needs, and their absence --------- */
enum adreno_smmu_stage1_input {
    kAdrenoSmmuInputPageTable = 1u, /* I1 TTBR0 must point at a real table */
    kAdrenoSmmuInputAsid = 2u,      /* I2 CONTEXTIDR needs an owned ASID    */
    kAdrenoSmmuInputStreamBinding = 4u, /* I3 S2CR for the SID, faulting    */
    kAdrenoSmmuInputCxPowered = 8u, /* I4 GPUCC CX domain up                */
    kAdrenoSmmuInputAll = 15u
};

/* One computed context-bank write. `offset` and `value` are the literal words;
 * `is_64bit` marks TTBR0/TTBR1, which the caller must write as a 64-bit store. */
struct adreno_smmu_cb_write {
    uint32_t offset;
    uint32_t value;
    uint32_t is_64bit;
    uint32_t order;      /* position in the sequence, 0-based */
};

/* ---- API (freestanding; no libc, no MMIO, no IOKit) ------------------ */

/* The stage-1 SCTLR value P2:642-645 produces on a little-endian host. */
uint32_t adreno_smmu_sctlr_stage1(void);
/* The SCTLR value for a big-endian host: the same plus BIT(4). */
uint32_t adreno_smmu_sctlr_stage1_be(void);
/* 1 when SCTLR has M set (translation enabled), else 0. */
int adreno_smmu_sctlr_enabled(uint32_t sctlr);

/* GR0 S2CR words. `cbndx` is the context bank index the SID routes to. */
uint32_t adreno_smmu_s2cr_addr(uint32_t sid);
uint32_t adreno_smmu_s2cr_value(enum adreno_smmu_s2cr_type type,
                                uint32_t cbndx, uint32_t privcfg,
                                uint32_t exidvalid);
/* Round 53's measured precondition, expressed as an S2CR type: the target
 * streams sit in BYPASS, so nothing reaches a context bank yet. */
enum adreno_smmu_s2cr_type adreno_smmu_s2cr_type_for_state(int faulting);

/* The context-bank write sequence for one stream, in the upstream order
 * (MAIRs, TTBR0, TCR, CONTEXTIDR, and SCTLR last so translation is enabled only
 * once the table is in place). Returns 0, or 1 on NULL/short capacity; *count
 * is the number of writes produced. */
int adreno_smmu_stage1_plan(uint32_t cbndx, uint32_t asid, uint64_t ttbr0,
                            uint32_t tcr, uint32_t mair0, uint32_t mair1,
                            struct adreno_smmu_cb_write *out, uint32_t capacity,
                            uint32_t *count);

/* The four inputs still missing. Returns a mask of enum
 * adreno_smmu_stage1_input; derived from what this port actually holds. */
uint32_t adreno_smmu_stage1_missing(void);
/* Name of one input bit, or "unknown". */
const char *adreno_smmu_stage1_input_name(uint32_t bit);
/* Pinned/measured citation for one input bit, or "unknown". */
const char *adreno_smmu_stage1_input_anchor(uint32_t bit);

/* 1 only when NOTHING is missing, i.e. a context could legitimately be
 * programmed. Derived from adreno_smmu_stage1_missing(), so it flips only when
 * all four inputs genuinely exist. 0 today. */
int adreno_smmu_stage1_ready(void);

/* Self-check. Returns 0 when every invariant holds, else 1; *check_count
 * receives the number evaluated. */
int adreno_smmu_stage1_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_SMMU_STAGE1_V1_H */
