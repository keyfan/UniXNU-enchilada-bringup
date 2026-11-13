// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoIommuProbeV1 -- enchilada stage 5 (GPU) HOST-VERIFIED KERNEL-SIDE SMMU
// PROBE CONTRACT (round 86, 2026-11-13).
//
// One sentence: this module turns an ID2 register WORD into `ias`, and refuses
// to let anyone read that word until the preconditions that make the read
// non-fatal are all satisfied. It does not perform the read.
//
// WHY THIS ROUND, AND WHY IT IS THE KERNEL AND NOT ANOTHER DEVICE SURVEY
// ---------------------------------------------------------------------
// Round 77 proved the chain `ias = smmu->va_size <- arm_smmu_id_size_to_bits(
// FIELD_GET(ARM_SMMU_ID2_UBS, ID2))` and showed it terminates in a RUNTIME
// REGISTER READ of ID2 at probe (P2:1879, P2:1904-1905). Round 83 then derived
// the nine context-bank WORDS that need that `ias` -- and left it an ARGUMENT,
// because nothing could supply it. Round 85 went to the phone and MEASURED that
// no userspace route exists: this kernel is built with `CONFIG_DEVMEM is not
// set`, so /dev/mem cannot exist at all (mknod succeeds, every access fails
// ENXIO), and the iommu debugfs exposes no address-size node whatsoever.
//
// That measurement is what makes this module the right next artifact. `ias` is
// not obtainable by a better command; it is obtainable ONLY by code running in
// the kernel. So the deliverable is no longer "another derivation" -- it is the
// CONTRACT that a kernel caller must honour: which register, at which offset,
// decoded exactly how, gated behind exactly which preconditions.
//
// PROVENANCE (4 sha256 pins, re-checked by
// scripts/verify-enchilada-iommu-probe.py)
// ----------------------------------------
//  (P1) drivers/iommu/arm/arm-smmu/arm-smmu.h            16630 B
//       ed85998794695d1a824dc1b86f680c8d124546688fd2665e00e3cbcfa9546b53
//       :45 GR0_ID0 0x20; :54 ID0_CTTW BIT(14); :46-57 the ID0 fields;
//       :59 GR0_ID1 0x24; :65 GR0_ID2 0x28; :66 ID2_VMID16 BIT(15);
//       :67 ID2_PTFS_64K BIT(14); :68 ID2_PTFS_16K BIT(13);
//       :69 ID2_PTFS_4K BIT(12); :70 ID2_UBS GENMASK(11,8);
//       :71 ID2_OAS GENMASK(7,4); :72 ID2_IAS GENMASK(3,0);
//       :259-263 enum arm_smmu_arch_version {V1, V1_64K, V2};
//       :295-307 the FEAT_* bits, including VMID16 (1<<6) and the three
//       AARCH64 granule features (1<<7, 1<<8, 1<<9).
//  (P2) drivers/iommu/arm/arm-smmu/arm-smmu.c            67693 B
//       d5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a
//       :1727-1744 arm_smmu_id_size_to_bits(), whose DEFAULT arm collapses 5
//       AND every other value to 48 -- the trap that makes a table wrong;
//       :1746 arm_smmu_device_cfg_probe(); :1758 ID0 is read FIRST;
//       :1766-1774 ID0.S1TS/S2TS gate the stage features;
//       :1799-1805 ID0.CTTW, overridden by firmware;
//       :1853 ID1; :1879 the ID2 READ itself; :1880-1881 ipa_size from ID2.IAS;
//       :1884-1885 pa_size from ID2.OAS; :1887-1888 VMID16 feature;
//       :1899-1902 for v1 va_size = ipa_size; :1904-1905 for v2+ va_size comes
//       from ID2.UBS; :1906-1911 the three PTFS granule features;
//       :1965-1975 arm_smmu_of_match[], the compatible -> version table;
//       :2129-2269 arm_smmu_device_probe(), the ORDER this module encodes:
//       :2152 ioremap, :2189-2198 clocks BEFORE :2200 cfg_probe, :2255 reset;
//       :735-736 a stage-1 domain takes ias = va_size and oas = ipa_size;
//       :737-746 the AArch64 branch does NOT clamp while AARCH32 does;
//       :545-548 the TTBR1 quirk puts the TABLE in TTBR1 and leaves TTBR0
//       carrying only the ASID; :616-621 TCR2 is written before TCR;
//       :650-652 SCTLR goes through impl->write_sctlr when the impl has one.
//  (P3) drivers/iommu/arm/arm-smmu/arm-smmu-qcom.c       20423 B
//       75760784bbf68942a4055acdc0886c3559c4993fd49e39e0a3abc20f5f52be44
//       :358 the Adreno impl CLEARS ARM_SMMU_FEAT_FMT_AARCH64_16K, so a set
//       ID2.PTFS_16K does not mean 16 KiB pages are usable here;
//       :200-208 qcom_adreno_can_do_ttbr1() is false ONLY for msm8996;
//       :226-229 with the TTBR1 quirk enabled for SID 0;
//       :464-471 qcom_adreno_smmu_v2_impl, whose .write_sctlr (used at P2:650)
//       ORs ARM_SMMU_SCTLR_HUPCF into every SCTLR (:53) and CFCFG when stalling;
//       :535-538 qcom_smmu_v2_data.adreno_impl; :492-495 the compatible
//       "qcom,adreno-smmu" selects adreno_impl; :628-647 qcom_smmu_impl_init.
//  (P4) arch/arm64/boot/dts/qcom/sdm845.dtsi            151194 B
//       e7af1c639d7548cb03a728aa85367a68c4964f18ef1d9ad39f91c5090dac8457
//       :4831-4844 the GPU node, reg 0x05000000 + 0x509e000, iommus SID 0;
//       :4903-4923 adreno_smmu: compatible "qcom,sdm845-smmu-v2",
//       "qcom,adreno-smmu", "qcom,smmu-v2"; reg 0x05040000 + 0x10000;
//       #iommu-cells = <1>; power-domains = <&gpucc GPU_CX_GDSC>;
//       clocks "bus"/"iface"; :4925-4945 the GMU at 0x0506a000 with the CX/GX
//       power domains and SID 5; :5092-5095 apps_smmu at 0x15000000 with
//       #iommu-cells = <2> -- a DIFFERENT unit the GPU does not use.
//
// WHAT THIS MODULE DERIVES (and what it deliberately leaves as an INPUT)
// --------------------------------------------------------------------
//  1. `id_size_to_bits()` -- P2:1727-1744 INCLUDING the collapsing default.
//  2. The ID2 decode: UBS (11..8), OAS (7..4), IAS (3..0), PTFS_4K/16K/64K
//     (12/13/14) and VMID16 (15), from the real bit positions in P1:66-72.
//  3. The VERSION, and therefore which `va_size` rule applies. The version is
//     NOT a register: P2:1965-1975 maps a DT compatible to it, and P4:4903-4904
//     gives the node three compatibles of which only "qcom,smmu-v2" is in that
//     table -- so this SMMU is ARM_SMMU_V2 (P1:262) and `va_size` comes from
//     UBS (P2:1904-1905). That is a DERIVED conclusion, not an input.
//  4. THREE sizes, kept apart on purpose: ipa_size (P2:1880-1881, from IAS),
//     pa_size (P2:1884-1885, from OAS), and va_size (P2:1899-1912).
//  5. The stage-1 `oas` -- which is `ipa_size`, NOT `pa_size` (P2:735-736).
//     This is the single most misread line in the chain and it decides the
//     TCR2.PASIZE nibble round 83 emits.
//  6. The AArch64 context decision (P2:720-724) and the 4 KiB-table gate.
//  7. The EFFECTIVE granule set, with P3:358 clearing 16 KiB on the Adreno impl.
//  8. `coherent_walk` from ID0.CTTW OR the firmware (P1:54, P2:1799-1805) --
//     which is a SECOND register read, so it is a second absent input.
//  9. The TTBR1 geometry for the GPU's SID 0 (P3:200-208, :226-229).
// 10. The probe PRECONDITIONS as a FAIL-CLOSED gate (6 bits, each cited).
// 11. The ordered caller steps, as DATA, with the rule that no register READ
//     may precede the CX-up step encoded so a test can assert it.
// 12. The composition: `adreno_iommu_stage1_words()` calls round 83's REAL
//     `adreno_smmu_ctx_tcr()` and `adreno_smmu_ctx_tcr2()` with the decoded
//     `ias`/`oas` -- composition, not re-implementation.
//
// WHAT REMAINS AN INPUT (this module cannot invent it)
// ---------------------------------------------------
// ID2 itself. The whole point of round 85 is that the word is UNREADABLE from
// userspace on this kernel, so `adreno_iommu_id2_decode()` takes the WORD, and
// the module records that nobody in this port has one. `adreno_iommu_ready()`
// is DERIVED from a 7-bit missing mask and is 0 today.
//
// HONEST BOUNDARY
// ---------------
// HOST-VERIFIED, PROBE CONTRACT ONLY. No MMIO, no mapping, no kernel caller, no
// page table, no ASID, no IOKit, no `volatile`. It does not read ID2, power CX,
// allocate anything, or program a context. `CONFIG_XNU_NATIVE_ADRENO` remains
// `n`; the gpu area STAYS "host-verified"; no device run, no flash, m39510 NOT
// exercised. Round 85's ceiling is NOT lifted by this module: the module is the
// contract that becomes executable only once a kernel caller exists, and none
// does. It does NOT unblock stage 5.
// ---------------------------------------------------------------------------

#ifndef ADRENO_IOMMU_PROBE_V1_H
#define ADRENO_IOMMU_PROBE_V1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The two identification registers this contract needs (P1:45, :65) ---
 * Both live in the GR0 frame, i.e. at the SMMU base itself. The base is the
 * kgsl SMMU's 0x05040000 (P4:4905) -- NOT apps_smmu's 0x15000000 (P4:5094). */
#define kAdrenoIommuGr0Id0   0x0020u /* P1:45  ARM_SMMU_GR0_ID0 */
#define kAdrenoIommuGr0Id1   0x0024u /* P1:59  ARM_SMMU_GR0_ID1 */
#define kAdrenoIommuGr0Id2   0x0028u /* P1:65  ARM_SMMU_GR0_ID2 */

/* ---- ID2 field bit positions, taken from P1:66-72 verbatim -------------- */
#define kAdrenoIommuId2Vmid16   0x00008000u /* P1:66 BIT(15)         */
#define kAdrenoIommuId2Ptfs64K  0x00004000u /* P1:67 BIT(14)         */
#define kAdrenoIommuId2Ptfs16K  0x00002000u /* P1:68 BIT(13)         */
#define kAdrenoIommuId2Ptfs4K   0x00001000u /* P1:69 BIT(12)         */
#define kAdrenoIommuId2UbsMask  0x00000f00u /* P1:70 GENMASK(11, 8)  */
#define kAdrenoIommuId2UbsShift 8u
#define kAdrenoIommuId2OasMask  0x000000f0u /* P1:71 GENMASK(7, 4)   */
#define kAdrenoIommuId2OasShift 4u
#define kAdrenoIommuId2IasMask  0x0000000fu /* P1:72 GENMASK(3, 0)   */
#define kAdrenoIommuId2IasShift 0u

/* ID0.CTTW (P1:54) is the ONLY bit of ID0 this contract needs: it decides
 * `coherent_walk`, and the firmware may override it (P2:1799-1805). */
#define kAdrenoIommuId0Cttw     0x00004000u /* P1:54 BIT(14) */

/* The 4-bit IDR field is at most 0xf; a value outside it is a decode refusal,
 * not a width. Exposed so a caller cannot pass a wider type by accident. */
#define kAdrenoIommuIdSizeMax   0x0fu

/* ---- enum arm_smmu_arch_version, P1:259-263, as plain numbers ----------- */
#define kAdrenoIommuVersionV1      0u /* P1:260 ARM_SMMU_V1       */
#define kAdrenoIommuVersionV1_64K  1u /* P1:261 ARM_SMMU_V1_64K   */
#define kAdrenoIommuVersionV2      2u /* P1:262 ARM_SMMU_V2       */

/* A compatible the pinned of_match table does not claim. REFUSED, never
 * guessed: probing an unknown SMMU is exactly the class of mistake this module
 * exists to prevent. */
#define kAdrenoIommuVersionUnknown 0xffffffffu

/* ---- The board facts, re-stated here so a caller cannot silently use the
 * wrong window. Both are pinned in P4 and both are PINNED NUMBERS, not
 * arguments: the GPU's SMMU is the kgsl one. ------------------------------ */
#define kAdrenoIommuKgslSmmuBase    0x05040000ull /* P4:4905 reg[0] */
#define kAdrenoIommuKgslSmmuSize    0x00010000ull /* P4:4905 reg[1] */
#define kAdrenoIommuAppsSmmuBase    0x15000000ull /* P4:5094 -- the OTHER one */

/* Round 22/28's hard safety rule, as DATA: these six windows must never be
 * touched before GPUCC CX is powered. They are the SAME six AdrenoDomainState.c
 * :18-25 pinned from the board DTB; re-stated here so this module stands alone
 * and a diverging window list fails a test rather than drifting.
 *
 * GPUCC (0x05090000 + 0x9000) is deliberately NOT in this list: it sits on
 * always-on PMIC rails and its GDSC status IS readable with the domain collapsed
 * (round 28). AdrenoDomainState.c:58-77 carves it out of the GMU window, which
 * NUMERICALLY swallows it (0x0506a000 + 0x30000 = 0x0509a000). */
#define kAdrenoIommuGpuWindowBase   0x05000000ull /* P4:4834 reg[0] */
#define kAdrenoIommuGpuWindowSize   0x00040000ull
#define kAdrenoIommuGpuDbgcBase     0x05061000ull /* AdrenoDomainState.c:20 */
#define kAdrenoIommuGpuDbgcSize     0x00000800ull
#define kAdrenoIommuGmuWindowBase   0x0506a000ull /* P4:4928 reg[0] */
#define kAdrenoIommuGmuWindowSize   0x00030000ull
#define kAdrenoIommuGmuVbifBase     0x0b200000ull /* AdrenoDomainState.c:23 */
#define kAdrenoIommuGmuVbifSize     0x00300000ull
#define kAdrenoIommuCxMemBase       0x0509e000ull /* P4:4834 reg[1] (cx_mem) */
#define kAdrenoIommuCxMemSize       0x00001000ull
#define kAdrenoIommuGpuccBase       0x05090000ull /* P4 gpucc@5090000 */
#define kAdrenoIommuGpuccSize       0x00009000ull

/* ---- The decoded ID2 word ----------------------------------------------
 * `ubs`/`oas`/`ias` are the RAW 4-bit ENCODINGS as they sit in the register;
 * `*_bits` are the WIDTHS P2:1727-1744 produces from them. Keeping both means a
 * caller cannot mistake an encoding for a width -- the exact confusion that
 * makes a hard-coded 48 look plausible. */
struct adreno_iommu_id2 {
    uint32_t raw;      /* the register word as read (P2:1879)               */
    uint32_t ubs;      /* P1:70 encoding, 0..15                             */
    uint32_t oas;      /* P1:71 encoding, 0..15                             */
    uint32_t ias;      /* P1:72 encoding, 0..15                             */
    uint32_t ubs_bits; /* P2:1904-1905 width for va_size (v2+)              */
    uint32_t oas_bits; /* P2:1884-1885 width for pa_size                    */
    uint32_t ias_bits; /* P2:1880-1881 width for ipa_size                   */
    uint32_t ptfs_4k;  /* 0/1, P1:69                                        */
    uint32_t ptfs_16k; /* 0/1, P1:68                                        */
    uint32_t ptfs_64k; /* 0/1, P1:67                                        */
    uint32_t vmid16;   /* 0/1, P1:66                                        */
};

/* ---- The probe preconditions, as a 6-bit mask (fail-closed) ------------
 * Every bit carries a pinned or MEASURED anchor; see
 * adreno_iommu_precondition_anchor(). They are bits, not booleans, so a caller
 * cannot satisfy "some" of them and believe it satisfied "all". */
enum adreno_iommu_precondition {
    /* GPUCC CX must be UP. Round 35 measured cx=0x00005ff4 with BIT(31) clear,
     * i.e. DOWN; P4:4922 puts this SMMU on <&gpucc GPU_CX_GDSC>. */
    kAdrenoIommuPreCxPowered = 1u,
    /* The SMMU's own clocks must be enabled. P2:2196-2198 enables them BEFORE
     * P2:2200 calls cfg_probe, which is where ID2 is read. */
    kAdrenoIommuPreClocksOn = 2u,
    /* The 64 KiB GR0 window must be MAPPED in the kernel. P2:2152 is the
     * kernel's ioremap; round 85 measured that no userspace mapping route
     * exists at all (CONFIG_DEVMEM is not set). */
    kAdrenoIommuPreKmemMapped = 4u,
    /* The window must be the KGSL SMMU (P4:4905), not apps_smmu (P4:5094). */
    kAdrenoIommuPreWindowIsKgsl = 8u,
    /* The target context bank must NOT already be programmed. P3:360-363 and
     * P3:468 show the firmware/TZ already owns context banks. */
    kAdrenoIommuPreCbFree = 16u,
    /* The target stream must actually reach the SMMU. Round 53 measured the
     * GPU streams still in the firmware BYPASS handoff (Busy=10). */
    kAdrenoIommuPreStreamFaults = 32u,
    kAdrenoIommuPreAll = 63u
};

/* The caller-supplied facts for ONE proposed ID2 read. Descriptions, NOT
 * values measured by this module. */
struct adreno_iommu_gate_inputs {
    uint64_t window_base;
    uint64_t window_size;
    uint32_t cx_status_word;  /* GPUCC CX status, decoded by AdrenoDomainState */
    uint32_t clocks_enabled;  /* 0/1 */
    uint32_t kmem_mapped;     /* 0/1 */
    uint32_t cb_already_owned;/* 0/1; 1 means SOMEONE ELSE owns the bank */
    uint32_t stream_faults;   /* 0/1 */
};

/* Fail-closed verdict. The default for ANY doubt is a refusal, and the numbers
 * are ordered so that the physically dangerous cases sort before the merely
 * incorrect ones. */
enum adreno_iommu_probe_verdict {
    kAdrenoIommuProbePermitted = 0,
    kAdrenoIommuProbeRefusedInvalid = 1,      /* NULL / zero / non-power-of-2  */
    kAdrenoIommuProbeRefusedWrongWindow = 2,  /* not the kgsl SMMU window      */
    kAdrenoIommuProbeRefusedCxDown = 3,       /* THE hazard: powered-down rail */
    kAdrenoIommuProbeRefusedClocksOff = 4,    /* P2:2196-2198                  */
    kAdrenoIommuProbeRefusedUnmapped = 5,     /* P2:2152 / round 85            */
    kAdrenoIommuProbeRefusedCbOwned = 6,      /* P3:360-363                    */
    kAdrenoIommuProbeRefusedNoStream = 7      /* round 53 BYPASS               */
};

/* One ordered caller step. `kind` separates the two things a caller does that
 * are NOT equivalent: touching a register, and computing from a value already
 * in hand. The distinction is checkable, so it is data. */
#define kAdrenoIommuStepGate    0u /* a precondition check; touches nothing  */
#define kAdrenoIommuStepRead    1u /* an MMIO read; CX must already be up     */
#define kAdrenoIommuStepDerive  2u /* pure arithmetic on a value in hand     */
#define kAdrenoIommuStepHandoff 3u /* hands the derived words to round 83    */

struct adreno_iommu_step {
    uint32_t order;    /* 0-based position in the sequence                    */
    uint32_t kind;     /* one of kAdrenoIommuStep*                            */
    const char *name;  /* short imperative name                               */
    const char *anchor;/* the pinned citation for WHY this step exists        */
};

/* The composition result: the decoded sizes AND the two stage-1 words round 83
 * computes from them. `ok` is 1 only when BOTH round-83 calls accepted their
 * arguments; a 0 means the words are meaningless and must not be written. */
struct adreno_iommu_stage1_words {
    uint32_t ias;           /* from va_size, P2:735                          */
    uint32_t oas;           /* = ipa_size, P2:736 -- NOT pa_size              */
    uint32_t coherent_walk; /* the caller's supplied fact                    */
    uint32_t ttbr1;         /* the GPU geometry, P3:200-208                  */
    uint32_t tcr;           /* round 83's adreno_smmu_ctx_tcr()              */
    uint32_t tcr2;          /* round 83's adreno_smmu_ctx_tcr2()             */
    uint32_t ok;            /* 1 iff both round-83 calls succeeded           */
};

/* ---- The seven inputs this port does not hold -------------------------
 * Each is a bit because readiness must be DERIVED, never asserted. */
enum adreno_iommu_missing_input {
    kAdrenoIommuMissingId2Word = 1u,      /* P2:1879 -- round 85 measured that
                                           * this read is impossible from
                                           * userspace on this kernel.          */
    kAdrenoIommuMissingId0Word = 2u,      /* P1:54/P2:1799-1805 -- a SECOND
                                           * register read, for coherent_walk.  */
    kAdrenoIommuMissingCxUp = 4u,         /* round 35 measured CX DOWN          */
    kAdrenoIommuMissingAsid = 8u,         /* round 75 I2: no ASID allocator     */
    kAdrenoIommuMissingPageTable = 16u,   /* round 75 I1: no populated table    */
    kAdrenoIommuMissingStream = 32u,      /* round 53 measured BYPASS           */
    kAdrenoIommuMissingKernelCaller = 64u,/* CONFIG_XNU_NATIVE_ADRENO is n      */
    kAdrenoIommuMissingAll = 127u
};

/* ---- API (freestanding; no libc, no MMIO, no IOKit, no volatile) ------ */

/* P2:1727-1744 `arm_smmu_id_size_to_bits()` VERBATIM: 0->32, 1->36, 2->40,
 * 3->42, 4->44, and 5 AND EVERY OTHER VALUE -> 48. The default arm is the
 * reason this is a function and not a table. */
uint32_t adreno_iommu_id_size_to_bits(uint32_t size);

/* P2:1879 decode of one ID2 word. Returns 0 on success, 1 on NULL out. The
 * three encodings are guaranteed inside 0..15 by their field widths. */
int adreno_iommu_id2_decode(uint32_t raw, struct adreno_iommu_id2 *out);

/* P2:1880-1881 ipa_size <- id_size_to_bits(ID2.IAS). */
uint32_t adreno_iommu_ipa_size(uint32_t raw);
/* P2:1884-1885 pa_size <- id_size_to_bits(ID2.OAS). */
uint32_t adreno_iommu_pa_size(uint32_t raw);

/* P2:1899-1912, version-gated: for V1 and V1_64K `va_size = ipa_size`; for V2+
 * `va_size = id_size_to_bits(ID2.UBS)`. An UNKNOWN version is REFUSED (0) --
 * guessing which branch applies would silently invent the number this whole
 * round exists to obtain honestly. */
uint32_t adreno_iommu_va_size(uint32_t raw, uint32_t version, int *ok);

/* P2:735-736: a STAGE-1 domain's `oas` is `ipa_size`, not `pa_size`. Exposed
 * separately because conflating them is the most likely silent error here. */
uint32_t adreno_iommu_stage1_oas(uint32_t raw);

/* P2:1965-1975: which arm_smmu_arch_version a DT compatible resolves to.
 * Returns kAdrenoIommuVersionUnknown when the pinned table does not claim it. */
uint32_t adreno_iommu_version_of_compatible(const char *compatible);

/* The version a NODE resolves to, walking its compatibles in DT order and
 * returning the first one the pinned table claims (which is what
 * of_match_device() does). kAdrenoIommuVersionUnknown when none match. */
uint32_t adreno_iommu_version_select(const char *const *compatibles,
                                     uint32_t count);

/* P2:720-724: 1 iff the context resolves to ARM_SMMU_CTX_FMT_AARCH64, i.e. the
 * kernel is 64-bit (a board fact for this arm64 port) AND the SMMU reports at
 * least one AArch64 granule. */
uint32_t adreno_iommu_aarch64_context(uint32_t raw);

/* P1:67-69 as a 3-bit mask {4K=1, 16K=2, 64K=4}, then masked by P3:358, which
 * CLEARS 16 KiB on the Adreno implementation. `qcom_adreno_impl` is the fact
 * that the node's compatible list contains "qcom,adreno-smmu" (P4:4904). */
uint32_t adreno_iommu_ptfs_effective(uint32_t raw, uint32_t qcom_adreno_impl);

/* P1:54 + P2:1799-1805: coherent_walk is ID0.CTTW OR the firmware's claim, and
 * the firmware WINS. `fw_says_coherent` is a caller fact from the DT's
 * `dma-coherent` (P2:2075-2076). */
uint32_t adreno_iommu_coherent_walk(uint32_t id0_raw, uint32_t fw_says_coherent);

/* P3:200-208 + P3:226-229: the GPU's SID 0 selects the TTBR1 geometry on
 * sdm845 (only msm8996 is excluded). Returns 1 with the citation, 0 on NULL. */
uint32_t adreno_iommu_ttbr1_for_gpu(int *ok);

/* P2:545-548: upstream puts the page TABLE in whichever TTBR the quirk
 * selects, and leaves the OTHER TTBR carrying only the ASID. Round 83 writes
 * the same ASID|table word to both. Both statements are true at once because
 * round 83's TCR also sets exactly one of EPD0/EPD1, so the "wrong" bank is
 * disabled. This function returns the word upstream would put in that
 * OTHER bank, so the divergence is measurable rather than argued. */
uint64_t adreno_iommu_ttbr_idle_word(uint32_t asid);

/* THE COMPOSITION. Decode one ID2 word, derive ias/oas, and call round 83's
 * REAL adreno_smmu_ctx_tcr() / adreno_smmu_ctx_tcr2() with them. Returns 0 on
 * success, 1 on NULL / unknown version / no usable 4 KiB granule / a round-83
 * refusal. Never re-implements a round-83 word. */
int adreno_iommu_stage1_words(uint32_t id2_raw, uint32_t version,
                              uint32_t coherent_walk,
                              struct adreno_iommu_stage1_words *out);

/* P4:4905 vs P4:5094: 1 iff (base,size) is EXACTLY the kgsl SMMU window. */
int adreno_iommu_window_is_kgsl(uint64_t base, uint64_t size);
/* Round 22/28's rule as a predicate: 1 iff addr falls in one of the six windows
 * that sit on the collapsible rails -- every one of which is forbidden before
 * CX. The GPUCC carve-out of round 28 is applied, so this returns 0 for GPUCC. */
int adreno_iommu_addr_is_pre_cx_forbidden(uint64_t addr);
/* The number of forbidden windows, exposed so a test can assert the carve-out
 * is a real carve-out rather than a missing entry. */
uint32_t adreno_iommu_forbidden_window_count(void);

/* The fail-closed gate. NULL `in` -> RefusedInvalid. The CHECK ORDER is
 * load-bearing and is asserted: the window is validated first (pure
 * arithmetic, touches nothing), then CX (the physical hazard), then clocks,
 * mapping, bank ownership and the stream. Only if EVERY bit passes is the
 * verdict Permitted. No step is skippable. */
enum adreno_iommu_probe_verdict adreno_iommu_assess(
    const struct adreno_iommu_gate_inputs *in);
/* Convenience: 1 iff assess() == Permitted. Still pure, still caller-gated. */
int adreno_iommu_read_permitted(const struct adreno_iommu_gate_inputs *in);
/* Human-readable verdict name; static strings, never NULL. */
const char *adreno_iommu_verdict_name(enum adreno_iommu_probe_verdict v);

/* Name / anchor of one precondition bit, or "unknown". */
const char *adreno_iommu_precondition_name(uint32_t bit);
const char *adreno_iommu_precondition_anchor(uint32_t bit);
/* The mask a permitted read requires. Derived from the enum, never 0. */
uint32_t adreno_iommu_required_preconditions(void);

/* The ordered steps a kernel caller must perform. Returns 0, or 1 on NULL /
 * short capacity; *count receives the number produced. The sequence is
 * strictly ascending in `order` and NO kAdrenoIommuStepRead precedes the
 * CX-up step -- a property the test asserts over the returned data. */
int adreno_iommu_steps(struct adreno_iommu_step *out, uint32_t capacity,
                       uint32_t *count);
uint32_t adreno_iommu_step_count(void);

/* The seven absent inputs, DERIVED from what this port actually holds. */
uint32_t adreno_iommu_missing(void);
const char *adreno_iommu_missing_name(uint32_t bit);
const char *adreno_iommu_missing_anchor(uint32_t bit);

/* 1 only when NOTHING is missing. Derived from adreno_iommu_missing(), so it
 * flips only when all seven inputs genuinely exist. 0 today. */
int adreno_iommu_ready(void);

/* Self-check. Returns 0 when every invariant holds, else 1; *check_count
 * receives the number evaluated. */
int adreno_iommu_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_IOMMU_PROBE_V1_H */
