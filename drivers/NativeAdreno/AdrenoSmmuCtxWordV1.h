// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoSmmuCtxWordV1 -- enchilada stage 5 (GPU) HOST-VERIFIED derivation of the
// ARM SMMU v2 stage-1 context-bank WORDS themselves (round 83, 2026-11-13).
//
// This closes a REAL GAP left by round 75, and the gap is not cosmetic: it was
// measured, not guessed.
//
// WHAT ROUND 75 DID, AND WHAT IT LEFT OUT
// ---------------------------------------
// AdrenoSmmuStage1V1 answers "in what ORDER do the context-bank registers get
// written" and it answers that correctly. But every VALUE it writes is an
// ARGUMENT: `adreno_smmu_stage1_plan(cbndx, asid, ttbr0, tcr, mair0, mair1, ...)`
// takes `tcr`, `mair0` and `mair1` from the caller and never computes one. Its
// own test therefore passes `tcr = 0x1234` -- a number that appears in no pinned
// source and that no correct context bank can use.
//
// The consequence is specific and it is a defect in the ORDER, not only in the
// values. Upstream arm-smmu.c:566-653 (`arm_smmu_write_context_bank`) writes, in
// order: CBA2R, CBAR, TCR2, TCR, TTBR0, TTBR1, S1_MAIR0, S1_MAIR1, SCTLR. Round
// 75 emits SIX writes and omits FOUR of those registers while adding one that
// upstream never writes in this branch:
//   - CBAR  (GR1, 0x00 + n<<2)  -- P2:594-612. Without it the context bank has no
//                                    type, so the stage-1 page-table format is
//                                    never declared. This is not optional.
//   - CBA2R (GR1, 0x800 + n<<2) -- P2:581-592. VA64 selects the 64-bit table
//                                    format; without it an AArch64 context is
//                                    not 64-bit.
//   - TCR2  (CB + 0x10)         -- P2:619-620. Carries PASIZE (the output address
//                                    size) and SEP. Upstream writes it BEFORE TCR
//                                    for a stated reason: TCR decides how ASID[15:8]
//                                    is interpreted.
//   - TTBR1  (CB + 0x28)        -- P2:630-632. Stage-1 writes BOTH TTBRs, and
//                                    BOTH carry the ASID in bits 63..48.
//   + CONTEXTIDR (CB + 0x34)    -- P2:625. Round 75 writes it UNCONDITIONALLY, but
//                                    upstream writes it ONLY in the AArch32
//                                    short-descriptor branch; the AArch64 branch
//                                    writes no CONTEXTIDR at all -- the ASID
//                                    reaches the bank through TTBRn_ASID.
// And round 75's TTBR0 write carries no ASID at all, although P2:540-548 puts
// FIELD_PREP(ARM_SMMU_TTBRn_ASID, cfg->asid) into bits 63..48. So the plan cannot
// be made correct by supplying better arguments: it is missing registers.
//
// Net effect: 6 -> 9 writes. FOUR registers added, ONE removed, so the net delta
// (+3) is deliberately smaller than the register delta (+4); both are pinned as
// numbers in the C and asserted by the test, so this stays measurable.
//
// WHY THE OMISSION SURVIVED SEVEN ROUNDS
// --------------------------------------
// Because the order it does emit is a SUBSEQUENCE of the correct one, and every
// test asserted the subsequence. SCTLR-last -- the one rule round 75 documents --
// is genuinely correct and is preserved here unchanged. A correct rule plus a
// correct subsequence is not a correct sequence.
//
// WHAT THIS MODULE DERIVES (all from sha256-pinned v6.12 sources)
// --------------------------------------------------------------
// PROVENANCE (4 sha256 pins, re-checked by scripts/verify-enchilada-smmu-ctx-word.py)
//  (P1) drivers/iommu/arm/arm-smmu/arm-smmu.h  16630 B
//       ed85998794695d1a824dc1b86f680c8d124546688fd2665e00e3cbcfa9546b53
//       :123 GR1_CBAR 0x0 + (n<<2); :141 GR1_CBA2R 0x800 + (n<<2);
//       :124-136 CBAR fields and the four CBAR types; :161-165 TCR2 fields
//       (SEP 17..15, AS BIT(4), PASIZE 3..0); :171-180 TCR fields; :169
//       TTBRn_ASID GENMASK_ULL(63,48); :393-412 arm_smmu_lpae_tcr();
//       :414-418 arm_smmu_lpae_tcr2().
//  (P2) drivers/iommu/arm/arm-smmu/arm-smmu.c  67693 B
//       d5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a
//       :525-527 TCR2_AS for AArch64; :540-548 TTBRn carries the ASID and the
//       table; :560-561 the MAIR split; :566-653 arm_smmu_write_context_bank(),
//       the NINE-site sequence this round models; :608-611 the 8-bit VMID is in
//       CBAR only when 16-bit VMIDs are absent; :625 CONTEXTIDR is written ONLY
//       in the AArch32 branch; :733 a stage-1 domain selects type 1;
//       :1727-1744 arm_smmu_id_size_to_bits(), whose DEFAULT arm returns 48.
//  (P3) drivers/iommu/io-pgtable-arm.c  38934 B
//       b78adbf87352e574556638fae1206d62bfbc06787a85125f75b9d5da0abab278
//       :123-130 the four MAIR attribute bytes and their indices;
//       :944-999 the TCR default construction (coherent_walk branch, TG0/TG1,
//       IPS from oas, T0SZ = 64 - ias); :1002-1011 the MAIR register.
//  (P4) drivers/iommu/io-pgtable-arm.h  812 B
//       34520ecc0fa7f29c7bc03c38c125e67bd66229ddc5fc44be0c59408e0389a379
//       :8-13 the granule encodings (TG0_4K=0, TG0_64K=1, TG0_16K=2, TG1_16K=1,
//       TG1_4K=2, TG1_64K=3 -- note TG1_4K is 2, NOT 3); :16-18 shareability;
//       :20-23 cacheability.
//
//   1. TCR   -- arm-smmu.h:393-412 `arm_smmu_lpae_tcr()` + io-pgtable-arm.c:944-999,
//               from (ias, coherent_walk, ttbr1): TG0, SH0, ORGN0, IRGN0, T0SZ,
//               and the EPD1/EPD0 exclusive choice.
//   2. TCR2  -- arm-smmu.h:414-418 `arm_smmu_lpae_tcr2()`: PASIZE from `oas` via
//               arm_smmu_id_size_to_bits(), SEP always 0x7, plus TCR2_AS for an
//               AArch64 context (P2:525-527).
//   3. CBAR  -- P2:594-612: TYPE (S1_TRANS_S2_BYPASS for a stage-1 domain, P2:733),
//               S1_MEMATTR=WB and S1_BPSHCFG=NSH always, VMID only for stage 2.
//   4. CBA2R -- P2:581-592: VA64 for AArch64; VMID16 only when the IDR says so.
//   5. MAIR  -- io-pgtable-arm.c:1001-1011, the four-attribute register, then split
//               into MAIR0/MAIR1 exactly as P2:560-561 does.
//   6. TTBRn -- P2:540-548: the ASID occupies bits 63..48 and the table pointer the
//               low bits, for BOTH TTBRs.
//   7. The full NINE-write sequence in upstream order, with SCTLR last.
//
// HONEST BOUNDARY
// ---------------
// HOST-VERIFIED, WORD DERIVATION ONLY. This module still cannot program anything:
// it emits values and an order, and it names the inputs it must be given. It reads
// no register, allocates no page table, owns no ASID, and does NOT move the
// admission table -- round 75 already records `kgsl-smmu-stage1` as modelled, and
// computing one more word does not satisfy a single one of its four inputs.
// `ias` in particular stays an INPUT, not a derived value: round 77 established
// that it comes from ID2.UBS at probe and no document can supply it.
// CONFIG_XNU_NATIVE_ADRENO remains n; the gpu area STAYS "host-verified"; no
// device run, no flash, mMMIO. It does NOT unblock stage 5.
// ---------------------------------------------------------------------------

#ifndef ADRENO_SMMU_CTX_WORD_V1_H
#define ADRENO_SMMU_CTX_WORD_V1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- GR1 (context-bank attribute) register offsets, P1:123 and P1:141 ---
 * These are NOT context-bank page offsets: CBAR/CBA2R live in the SMMU's GR1
 * frame and are indexed by the context-bank number, while TCR/TTBR/MAIR/SCTLR
 * live INSIDE the context-bank page. Round 75 modelled only the latter. */
#define kAdrenoSmmuGr1Cbar      0x0000u /* P1:123 0x0 + (n << 2)   */
#define kAdrenoSmmuGr1CbFrsynra 0x0400u /* P1:138 0x400 + (n << 2) */
#define kAdrenoSmmuGr1Cba2r     0x0800u /* P1:141 0x800 + (n << 2) */

/* ---- CBAR fields (P1:124-136, composition P2:594-612) ----------------- */
#define kAdrenoSmmuCbarIrptndxMask  0xff000000u /* GENMASK(31,24) */
#define kAdrenoSmmuCbarTypeMask     0x00030000u /* GENMASK(17,16) */
#define kAdrenoSmmuCbarS1MemattrMask 0x0000f000u /* GENMASK(15,12) */
#define kAdrenoSmmuCbarS1MemattrWb  0x0000f000u /* P1:133 = 0xf       */
#define kAdrenoSmmuCbarS1BpshcfgMask 0x00000300u /* GENMASK(9,8)      */
#define kAdrenoSmmuCbarS1BpshcfgNsh 0x00000300u /* P1:135 = 3         */
#define kAdrenoSmmuCbarVmidMask     0x000000ffu /* GENMASK(7,0)       */

/* P1:126-131 the four valid CBAR types; a stage-1 domain uses #1 (P2:733). */
#define kAdrenoSmmuCbarTypeS2Trans       0u
#define kAdrenoSmmuCbarTypeS1TransS2Bypass 1u
#define kAdrenoSmmuCbarTypeS1TransS2Fault  2u
#define kAdrenoSmmuCbarTypeS1TransS2Trans  3u

/* ---- CBA2R fields (P1:141-143, composition P2:581-592) ---------------- */
#define kAdrenoSmmuCba2rVmid16Mask 0xffff0000u /* GENMASK(31,16) */
#define kAdrenoSmmuCba2rVa64       0x00000001u /* BIT(0)         */

/* ---- TCR2 fields (P1:161-165, composition P2:414-418) ----------------- */
#define kAdrenoSmmuTcr2SepMask     0x00038000u /* GENMASK(17,15) */
#define kAdrenoSmmuTcr2SepUpstream 0x00038000u /* P1:163 = 0x7   */
#define kAdrenoSmmuTcr2As          0x00000010u /* BIT(4)         */
#define kAdrenoSmmuTcr2PasizeMask  0x0000000fu /* GENMASK(3,0)   */

/* ---- TCR fields (P1:171-180, composition P1:393-412) ------------------ */
#define kAdrenoSmmuTcrEae       0x80000000u /* BIT(31)      */
#define kAdrenoSmmuTcrEpd1      0x00800000u /* BIT(23)      */
#define kAdrenoSmmuTcrA1        0x00400000u /* BIT(22)      */
#define kAdrenoSmmuTcrTg0Mask   0x0000c000u /* GENMASK(15,14) */
#define kAdrenoSmmuTcrSh0Mask   0x00003000u /* GENMASK(13,12) */
#define kAdrenoSmmuTcrOrgn0Mask 0x00000c00u /* GENMASK(11,10) */
#define kAdrenoSmmuTcrIrgn0Mask 0x00000300u /* GENMASK(9,8)  */
#define kAdrenoSmmuTcrEpd0      0x00000080u /* BIT(7)        */
#define kAdrenoSmmuTcrT0szMask  0x0000003fu /* GENMASK(5,0)  */

/* io-pgtable-arm.h:8-10 the granule encodings. */
#define kAdrenoSmmuTg0_4K   0u
#define kAdrenoSmmuTg0_64K  1u
#define kAdrenoSmmuTg0_16K  2u
#define kAdrenoSmmuTg1_16K  1u
#define kAdrenoSmmuTg1_4K   2u
#define kAdrenoSmmuTg1_64K  3u

/* io-pgtable-arm.h:16-18 shareability and :20-23 cacheability. */
#define kAdrenoSmmuTcrShNs 0u
#define kAdrenoSmmuTcrShOs 2u
#define kAdrenoSmmuTcrShIs 3u
#define kAdrenoSmmuTcrRgnNc   0u
#define kAdrenoSmmuTcrRgnWbwa 1u
#define kAdrenoSmmuTcrRgnWt   2u
#define kAdrenoSmmuTcrRgnWb   3u

/* ---- TTBRn: the ASID lives in bits 63..48 (P1:169, P2:540-548) -------- */
#define kAdrenoSmmuTtbrAsidShift 48u
#define kAdrenoSmmuTtbrAsidMask  0xffffull

/* ---- MAIR attribute bytes (P3:123-126) and their indices (P3:127-130) - */
#define kAdrenoSmmuMairAttrDevice  0x04u
#define kAdrenoSmmuMairAttrNc      0x44u
#define kAdrenoSmmuMairAttrIncOwbrwa 0xf4u
#define kAdrenoSmmuMairAttrWbrwa   0xffu
#define kAdrenoSmmuMairIdxNc       0u
#define kAdrenoSmmuMairIdxCache    1u
#define kAdrenoSmmuMairIdxDev      2u
#define kAdrenoSmmuMairIdxIncOcache 3u

/* ---- The two input-address-size tables, P2:1727-1744 ---------------- */
/* arm_smmu_id_size_to_bits(): the 4-bit IDR field -> a bit width. Six values
 * are exact; 5 and everything else collapse to 48, which is why the function is
 * modelled rather than approximated. */
#define kAdrenoSmmuIdSizeMax 0xfu

/* ---- The three things a caller must supply; none of them is derivable --- */
enum adreno_smmu_ctx_input {
    /* ias: the input-address size. Round 77 established the chain
     * ias <- ID2.UBS <- a probe-time register read, so no document supplies it. */
    kAdrenoSmmuCtxInputIas = 1u,
    /* asid: an OWNED ASID. Round 75 I2 is unchanged -- this port has no allocator. */
    kAdrenoSmmuCtxInputAsid = 2u,
    /* ttbr: the physical address of a POPULATED 4 KiB-granule LPAE table.
     * Round 75 I1 is unchanged -- this port has no page table. */
    kAdrenoSmmuCtxInputPageTable = 4u,
    kAdrenoSmmuCtxInputAll = 7u
};

/* The parameters one stage-1 context bank is a function of. `cbndx` and `oas`
 * are board/silicon facts; `ias`, `asid` and `ttbr` are the absent inputs. */
struct adreno_smmu_ctx_params {
    uint32_t cbndx;      /* context-bank index, 0..num_context_banks-1        */
    uint32_t asid;       /* 8-bit ASID for this board (TTBRn_ASID is 16 bits) */
    uint64_t ttbr;       /* physical address of the populated LPAE root table  */
    uint32_t ias;        /* input-address bits, e.g. 40                        */
    uint32_t oas;        /* output-address bits, e.g. 36                      */
    uint32_t coherent_walk; /* 1 when IDR0.CTTW / the firmware says so (P2:801) */
    uint32_t ttbr1;      /* 1 to select the TTBR1 geometry (P1:405-409)       */
    uint32_t aarch64;    /* 1 for an AArch64 context (adds TCR2_AS, VA64)     */
    uint32_t vmid16;     /* 1 when IDR0 says 16-bit VMIDs exist (P1:66)       */
    uint32_t vmid;       /* only used when vmid16 is set                       */
};

/* One computed register write. `gr1` distinguishes the two SMMU frames: a GR1
 * offset has the context-bank index ALREADY APPLIED (base + (cbndx << 2)), and a
 * context-bank offset is relative to that bank's CBn_BASE. Round 75 could not
 * express this distinction at all, which is why it never wrote a GR1 register. */
struct adreno_smmu_ctx_write {
    uint32_t gr1;      /* 1 => offset is in the GR1 frame, 0 => in CBn_BASE  */
    uint32_t offset;   /* byte offset, with cbndx applied for the GR1 frame    */
    uint32_t value;    /* the low 32 bits of the write                        */
    uint32_t value_hi; /* the high 32 bits; meaningful only when is_64bit    */
    uint32_t is_64bit; /* TTBRn are 64-bit stores (P2:629-632)               */
    uint32_t order;    /* 0-based position in the sequence                   */
};

/* ---- API (freestanding; no libc, no MMIO, no IOKit) ------------------ */

/* P2:1727-1744 arm_smmu_id_size_to_bits(). 0->32, 1->36, 2->40, 3->42, 4->44,
 * 5 and every other value ->48. Returns the bit width. */
uint32_t adreno_smmu_ctx_id_size_to_bits(uint32_t size);

/* io-pgtable-arm.h:20-23 + P2:1002-1009: the four-attribute MAIR register
 * (IDX_NC=0x44, IDX_CACHE=0xff, IDX_DEV=0x04, IDX_INC_OCACHE=0xf4). */
uint64_t adreno_smmu_ctx_mair(void);
/* P3:1011 + P2:560-561: the register split. Both are pure views of mair(). */
uint32_t adreno_smmu_ctx_mair0(void);
uint32_t adreno_smmu_ctx_mair1(void);

/* P1:393-412 `arm_smmu_lpae_tcr()`: TG0, SH0, ORGN0, IRGN0, T0SZ, plus the
 * EPD1 (TTBR0 geometry) / EPD0 (TTBR1 geometry) exclusive pair. Refuses ias
 * outside 16..48 or coherent_walk outside 0..1 (returns 0 and sets *ok to 0). */
uint32_t adreno_smmu_ctx_tcr(uint32_t ias, uint32_t coherent_walk,
                             uint32_t ttbr1, int *ok);

/* P1:414-418 `arm_smmu_lpae_tcr2()`: PASIZE from `oas`, SEP=0x7 always, plus
 * TCR2_AS when the context is AArch64 (P2:525-527). Refuses an `oas` that is not
 * one of the five table values. */
uint32_t adreno_smmu_ctx_tcr2(uint32_t oas, uint32_t aarch64, int *ok);

/* P2:594-612 `arm_smmu_write_context_bank()` CBAR composition for a stage-1
 * domain: TYPE=S1_TRANS_S2_BYPASS, S1_MEMATTR=WB, S1_BPSHCFG=NSH, and a VMID only
 * in the 8-bit form (the 16-bit VMID goes in CBA2R, P2:588-589). */
uint32_t adreno_smmu_ctx_cbar(uint32_t vmid16, uint32_t vmid);
/* P2:581-592 CBA2R: VA64 for AArch64, VMID16 only when the IDR supports it. */
uint32_t adreno_smmu_ctx_cba2r(uint32_t aarch64, uint32_t vmid16, uint32_t vmid);

/* P2:540-548: TTBRn = (asid << 48) | table_pointer, for BOTH TTBRs. */
uint64_t adreno_smmu_ctx_ttbrn(uint32_t asid, uint64_t table);
/* The same composition, refused when `table` is not 4 KiB aligned. Returns 0 and
 * sets *ok to 0 on refusal -- an unaligned root pointer is a real hardware fault,
 * not a hypothetical one. */
uint64_t adreno_smmu_ctx_ttbrn_checked(uint32_t asid, uint64_t table, int *ok);

/* The plan: NINE writes in P2:581-653 order. Returns 0, or 1 on NULL/short
 * capacity/out-of-range parameters; *count receives the number produced. */
int adreno_smmu_ctx_plan(const struct adreno_smmu_ctx_params *p,
                         struct adreno_smmu_ctx_write *out, uint32_t capacity,
                         uint32_t *count);

/* The number of writes the full sequence contains. Exposed so a caller cannot
 * confuse "the buffer was big enough" with "the plan was complete". */
uint32_t adreno_smmu_ctx_plan_writes(void);

/* Round 75's SIX-write plan, as a NUMBER, so the gap this module closes stays
 * measurable instead of becoming folklore. */
uint32_t adreno_smmu_ctx_round75_writes(void);

/* The three absent inputs. Derived from what this port actually holds. */
uint32_t adreno_smmu_ctx_missing(void);
/* Name of one input bit, or "unknown". */
const char *adreno_smmu_ctx_input_name(uint32_t bit);
/* Citation for one input bit, or "unknown". */
const char *adreno_smmu_ctx_input_anchor(uint32_t bit);

/* 1 only when NOTHING is missing. Derived, and 0 today. */
int adreno_smmu_ctx_ready(void);

/* Self-check. Returns 0 when every invariant holds, else 1; *check_count
 * receives the number evaluated. */
int adreno_smmu_ctx_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_SMMU_CTX_WORD_V1_H */
