// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoZapAuthV1 -- enchilada stage 5 (GPU) HOST-VERIFIED QUALCOMM PAS
// (Peripheral Authentication Service) SHAPE for the A630 ZAP shader
// (round 73, 2026-11-12).
//
// This is NOT a PAS implementation and NOT a GPU driver. It encodes the exact
// SCM call SHAPE the ZAP handshake uses, the ordered prerequisite chain that
// must exist before that call can be made, and an explicit inventory of what is
// still missing. It makes no SCM call, maps nothing, loads no firmware, touches
// no MMIO, and is NOT an IOService.
//
// WHY THIS ROUND (round-69 admission table, prerequisite `zap-auth`)
// ----------------------------------------------------------------
// The table has recorded `zap-auth` as modelled=0 since round 69, with the note
// that the A740/sheng tree contains NO PAS code at all, so nothing could be
// inherited. Round 71 confirmed `GPU_PAS_ID 13` from the device tree. What was
// never written down is the SHAPE of the call and the exact set of things that
// must exist around it -- which is what a backend author actually needs, and
// what makes the remaining gap concrete instead of "PAS is unimplemented".
//
// PROVENANCE (4 sha256 pins re-checked by scripts/verify-enchilada-zap-auth.py)
// ---------------------------------------------------------------------------
//  (P1) include/linux/firmware/qcom/qcom_scm.h  5203 B
//       47390ea0f7d1fb6c3c298ed0fd13ee0275618e84d68ea43f0f2ac10448c448a1
//       :75 qcom_scm_pas_init_image(); :79 qcom_scm_pas_auth_and_reset();
//       :80 qcom_scm_pas_shutdown(); :81 qcom_scm_pas_supported().
//       NOTE: the numeric SVC/command ids are NOT in this public header.
//  (P2) drivers/firmware/qcom/qcom_scm.c  53938 B
//       8f1fbf64363590ea3c55b5bf4267a301792948473df9d837f4efa0206b9a5001
//       :703-731 qcom_scm_pas_auth_and_reset(): desc{svc=PIL, cmd=
//       PAS_AUTH_AND_RESET, arginfo=ARGS(1), args[0]=peripheral,
//       owner=ARM_SMCCC_OWNER_SIP}; enable clk, enable bw, call, then disable in
//       reverse; returns `ret ?: res.result[0]`. :669 the MEM_SETUP call.
//  (P3) drivers/firmware/qcom/qcom_scm.h (the INTERNAL one)  4977 B
//       d9cc584a5ff50cf8701a7775ad0f2e94f1b46683d0549e8871847c9122394881
//       :97 QCOM_SCM_SVC_PIL 0x02; :98 PAS_INIT_IMAGE 0x01;
//       :99 PAS_MEM_SETUP 0x02; :100 PAS_AUTH_AND_RESET 0x05;
//       :101 PAS_SHUTDOWN 0x06; :102 PAS_IS_SUPPORTED 0x07.
//  (P4) drivers/gpu/drm/msm/adreno/adreno_gpu.c  29592 B
//       e6b5203fb56358d22542197bd251c1513dc2b2f1ae8aaf3f9d9fca129f6f00b7
//       :30-174 zap_shader_load_mdt(): the ordered chain this module encodes --
//       CONFIG_ARCH_QCOM (:43), the "zap-shader" child node (:48), its
//       memory-region phandle (:54), of_address_to_resource (:61), the
//       firmware-name property (:82) with the request_firmware_direct path
//       (:85), qcom_mdt_get_size (:111), the region-too-small refusal (:117),
//       memremap MEMREMAP_WC (:125), qcom_mdt_load (:141/:148), and only THEN
//       qcom_scm_pas_auth_and_reset (:156) with the -EOPNOTSUPP quiet-fail
//       (:162). :176 adreno_zap_shader_load() is the entry point.
//
// THE THING THIS ROUND MAKES HONEST
// ---------------------------------
// The ZAP handshake is NOT "call qcom_scm_pas_auth_and_reset(13)". It is an
// eight-step chain, and every step has a precondition that enchilada does NOT
// currently satisfy. Encoding the chain turns "PAS is unimplemented" into eight
// named, individually-checkable absences. The first three are fatal on this
// board today and are called out explicitly:
//   S1 CONFIG_ARCH_QCOM      -- XNU is not Linux; there is no such Kconfig, so
//                               the real work is writing the SCM transport, not
//                               enabling a symbol.
//   S2 a "zap-shader" child node -- round 71 read the LIVE device tree and
//                               confirmed there is none under qcom,kgsl-3d0: the
//                               vendor tree carries qcom,kgsl-hyp instead, so
//                               the mainline discovery step has nothing to find.
//   S5 the firmware-name property -- the vendor tree DOES carry this
//                               (firmware-name = "a630_zap"), which is the one
//                               piece already present.
// A module that claimed "step 6 done" without steps 1-5 would be exactly the
// kind of claim this port has spent seventy rounds refusing to make.
//
// HONEST BOUNDARY
// ---------------
// HOST-VERIFIED, SHAPE + CHAIN + INVENTORY ONLY. No SCM call, no firmware load,
// no memremap, no secure-world handshake, no MMIO. `adreno_zap_auth_ready()`
// returns 0 and the admission row stays modelled=1 / satisfied=0.
// CONFIG_XNU_NATIVE_ADRENO remains n; the gpu area STAYS "host-verified"; no
// device run, no flash, m39510 NOT exercised. It does NOT unblock stage 5.
// ---------------------------------------------------------------------------

#ifndef ADRENO_ZAP_AUTH_V1_H
#define ADRENO_ZAP_AUTH_V1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- SCM shape (P3:97-102, P2:703-731) -------------------------------- */
#define kAdrenoZapScmSvcPil            0x02u /* P3:97  QCOM_SCM_SVC_PIL */
#define kAdrenoZapScmPasInitImage      0x01u /* P3:98 */
#define kAdrenoZapScmPasMemSetup       0x02u /* P3:99 */
#define kAdrenoZapScmPasAuthAndReset   0x05u /* P3:100 */
#define kAdrenoZapScmPasShutdown       0x06u /* P3:101 */
#define kAdrenoZapScmPasIsSupported    0x07u /* P3:102 */
/* P2:709 QCOM_SCM_ARGS(1): one argument, and P2:710 that argument is the
 * peripheral id. P2:711 the owner is the SIP SMC owner.
 *
 * ROUND 106 CORRECTION: this was 1u through rounds 73-105 and 1 is NOT
 * ARM_SMCCC_OWNER_SIP. The value is 2, and the round-106 arithmetic makes the
 * error self-evident: the SMC function base given to this round (0xC2000000) is
 * FAST_CALL BIT(31) | SMC_64 BIT(30) | owner << 24, and 0xC2000000 >> 24 & 0x3F
 * == 2. With owner 1 the base would have been 0xC1000000. arm-smccc.h is NOT
 * one of the pinned sources (P1-P4), so this value is NOT pinned here -- it is
 * derived FROM the base and cross-checked against the base's owner field by
 * adreno_zap_smc_base_owner(), which is exactly why that API exists. */
#define kAdrenoZapScmArgCount          1u
#define kAdrenoZapScmOwnerSip          2u /* ARM_SMCCC_OWNER_SIP; see the note above */

/* ---- The peripheral id, DEVICE-confirmed in round 71 ------------------ */
#define kAdrenoZapPasId                13u /* enchilada.dts:23612; qcom_scm.h:79 */

/* ==========================================================================
 * ROUND 106: the THREE-STAGE ORDERED PAS CALL CONTRACT (loader-consumable)
 * ==========================================================================
 * Rounds 73-105 modelled a single call (auth_and_reset) plus a prose inventory
 * of the eight prerequisites. A loader cannot execute a prose inventory: the
 * ZAP handshake is a strictly ordered three-call sequence and each call has a
 * DIFFERENT argument list. This section turns that shape into data a loader can
 * walk -- still without making any SCM call.
 *
 * THE ORDER (P1:75/:79, P3:98-100, and the composition at P5:236-258):
 *   stage 0  qcom_scm_pas_init_image      PIL cmd 0x01   metadata blob
 *   stage 1  qcom_scm_pas_mem_setup       PIL cmd 0x02   region addr + size
 *   stage 2  qcom_scm_pas_auth_and_reset  PIL cmd 0x05   peripheral id
 *
 * WHY stage 1 IS NOT OPTIONAL ON THIS BOARD: qcom_mdt_pas_init() calls
 * qcom_scm_pas_mem_setup ONLY inside `if (relocate)` (P5:251-258), and
 * `relocate` is set when any VALID phdr carries QCOM_MDT_RELOCATABLE
 * (P5:226-227). The real a630_zap.mdt ph[2] DOES carry BIT(27) (round 74,
 * independently re-parsed by scripts/verify-enchilada-mdt.py), so on enchilada
 * the conditional is TAKEN and mem_setup is mandatory. That is a DERIVATION
 * from two pinned facts, so the requirement is COMPUTED by
 * adreno_zap_pas_stage_required() from the phdr flags -- never assumed.
 *
 * PROVENANCE ADDITION FOR THIS ROUND
 * ----------------------------------
 *  (P5) drivers/soc/qcom/mdt_loader.c  12182 B
 *       ea79424add96b68c81ab9dfbde64ffac3ea5089a17259ba980c896f5d5e441ed
 *       :202-262 qcom_mdt_pas_init(): :226-227 sets `relocate` from
 *       QCOM_MDT_RELOCATABLE; :236-249 read_metadata -> pas_init_image, aborting
 *       on failure; :251-258 pas_mem_setup ONLY under `if (relocate)`, passing
 *       mem_phys and (max_addr - min_addr).
 *
 * THE SMC FUNCTION ID (x0) AND WHY ITS BASE IS NOT A PINNED FACT
 * -------------------------------------------------------------
 * P3:69 defines SCM_SMC_FNID(s, c) = ((s & 0xFF) << 8) | (c & 0xFF). The value
 * actually placed in x0 is that function id OR'd with a base whose top bits
 * select fast-call, 64-bit, and the owning entity -- exactly what P2:355-356
 * does for the IS_CALL_AVAIL probe:
 *   SCM_SMC_FNID(svc_id, cmd_id) | (ARM_SMCCC_OWNER_SIP << ARM_SMCCC_OWNER_SHIFT)
 *
 * On this family the base is 0xC2000000 = BIT(31) FAST_CALL | BIT(30) SMC_64 |
 * (ARM_SMCCC_OWNER_SIP << 24). THAT VALUE IS **DERIVED, NOT PINNED**: none of
 * P1-P5 contains it, because arm-smccc.h -- where the call-value macros live --
 * is not among the pinned files. Round 106 therefore does NOT hard-code it as a
 * fact. It is:
 *   - exposed once as kAdrenoZapSmcBaseDerived so no call site spells a literal,
 *   - labelled derived/unpinned by adreno_zap_smc_base_provenance(),
 *   - and accepted only through adreno_zap_smc_base_gate(), which RE-DERIVES
 *     the owner field out of the base and refuses a base that does not carry
 *     ARM_SMCCC_OWNER_SIP or the SMC_64 bit. A caller supplying 0xC1000000
 *     (owner 1) or 0x02000000 (no SMCCC top bits) is REFUSED rather than
 *     silently producing a wrong x0.
 * -------------------------------------------------------------------------- */

/* P3:69 SCM_SMC_FNID(s, c) = ((s & 0xFF) << 8) | ((c) & 0xFF). */
#define kAdrenoZapFnidSvcShift         8u
#define kAdrenoZapFnidSvcMask          0xFFu
#define kAdrenoZapFnidCmdMask          0xFFu

/* The SMCCC call-value fields the base is built from. DERIVED (arm-smccc.h is
 * not a pinned source); named so the arithmetic is checkable by a verifier. */
#define kAdrenoZapSmc64Bit             0x40000000u /* BIT(30) SMC_64    */
#define kAdrenoZapSmcFastCall          0x80000000u /* BIT(31) FAST_CALL */
#define kAdrenoZapSmcOwnerShift        24u
#define kAdrenoZapSmcOwnerMask         0x3Fu       /* 6-bit OWNER field  */

/* DERIVED, NOT PINNED. One named constant so no call site hard-codes 0xC2... */
#define kAdrenoZapSmcBaseDerived       0xC2000000u

/* ---- The three ordered stages (P1:75/:79, P3:98-100, P5:236-258) ------ */
enum adreno_zap_pas_stage {
    kAdrenoZapPasStageInitImage = 0u,    /* P1:75, P3:98, P5:243           */
    kAdrenoZapPasStageMemSetup = 1u,     /* P3:99, P5:252 (conditional)    */
    kAdrenoZapPasStageAuthAndReset = 2u, /* P1:79, P3:100, P4:156          */
    kAdrenoZapPasStageCount = 3u
};

/* The pinned arginfo of each stage, DERIVED from the pinned bodies:
 *   init_image  QCOM_SCM_ARGS(2, QCOM_SCM_VAL, QCOM_SCM_RW)   P2:584
 *   mem_setup   QCOM_SCM_ARGS(3)                              P2:670
 *   auth_reset  QCOM_SCM_ARGS(1)                              P2:709
 * P3:29-42 packs each argument's type into bits 4 + 2*i and the count into
 * bits 0..3, with QCOM_SCM_VAL = 0 (P3:23) and QCOM_SCM_RW = 2 (P3:25), so
 *   ARGS(2, VAL, RW) = (0 << 4) | (2 << 6) | 2 = 0x82
 *   ARGS(3)          = 3
 *   ARGS(1)          = 1.                                                      */
#define kAdrenoZapArgInfoInitImage     0x82u /* P2:584 ARGS(2, VAL, RW) */
#define kAdrenoZapArgInfoMemSetup      0x03u /* P2:670 ARGS(3)          */
#define kAdrenoZapArgInfoAuthAndReset  0x01u /* P2:709 ARGS(1)          */

/* One call of the contract, fully resolved: no MMIO, no SCM, no allocation. */
struct adreno_zap_pas_call {
    uint32_t stage;        /* enum adreno_zap_pas_stage                     */
    uint32_t svc;          /* 0x02 QCOM_SCM_SVC_PIL                (P3:97)  */
    uint32_t cmd;          /* 0x01 / 0x02 / 0x05            (P3:98,99,100)  */
    uint32_t arginfo;      /* QCOM_SCM_ARGS(...) PINNED for this call        */
    uint32_t arg_count;    /* arginfo & 0xF: how many args the loader fills  */
    uint32_t owner;        /* ARM_SMCCC_OWNER_SIP                  (P2:711) */
    uint32_t args0;        /* args[0] == peripheral == GPU_PAS_ID 13 (P2:710)*/
    uint32_t fnid;         /* SCM_SMC_FNID(svc, cmd)                (P3:69)  */
    uint32_t x0;           /* base | fnid; the base is DERIVED (see above)   */
    uint32_t args1_kind;   /* 0 none, 1 phys addr, 2 phys addr (metadata)    */
    int required;          /* 1 when this stage MUST run for the given blob   */
    int is_metadata_call;  /* 1 for init_image: carries the metadata blob     */
    int carries_size_arg;  /* 1 for mem_setup: args[2] is the region size     */
};

/* Caller-supplied region facts for stage 1. Kept explicit so the contract can
 * express mem_setup's arguments without inventing addresses. */
struct adreno_zap_pas_region {
    uint32_t phys;      /* region start, from adreno_mdt_plan()'s reloc base */
    uint32_t bytes;     /* region size -- adreno_mdt_get_size() on the blob  */
};

/* Per-stage outcome record, so a caller cannot conflate the two failures. */
struct adreno_zap_pas_result {
    int transport_ret;      /* what the SMC transport returned (0 == ok)    */
    uint32_t secure_result; /* res.result[0] from the secure world          */
    int transport_ok;       /* transport_ret == 0                           */
    int secure_ok;          /* secure_result == 0                           */
    int is_success;         /* transport_ok && secure_ok  (P2:729)          */
};

/* ---- The eight ordered prerequisites (P4:43-156) --------------------- */
enum adreno_zap_step {
    kAdrenoZapStepArchQcom = 0u,      /* P4:43  CONFIG_ARCH_QCOM gate        */
    kAdrenoZapStepZapShaderNode = 1u, /* P4:48  of_get_child_by_name          */
    kAdrenoZapStepMemoryRegion = 2u,  /* P4:54  of_parse_phandle              */
    kAdrenoZapStepAddressToRes = 3u,  /* P4:61  of_address_to_resource        */
    kAdrenoZapStepFirmwareName = 4u,  /* P4:82  firmware-name property        */
    kAdrenoZapStepMdtSize = 5u,       /* P4:111 qcom_mdt_get_size             */
    kAdrenoZapStepMdtLoad = 6u,       /* P4:141/:148 qcom_mdt_load            */
    kAdrenoZapStepAuthAndReset = 7u,  /* P4:156 qcom_scm_pas_auth_and_reset   */
    kAdrenoZapStepCount = 8u
};

/* Which of the eight are PRESENT on enchilada today, per the pinned sources and
 * the round-71 live device-tree read. `present` is 1 or 0, never "unknown":
 * an unverified step is recorded as 0 with its reason. */
struct adreno_zap_step_state {
    uint32_t step;        /* enum adreno_zap_step                            */
    uint32_t present;     /* 1 when the precondition exists today            */
    uint32_t reachable;   /* 1 when it is even reachable on a non-Linux OS   */
};

/* ---- API (freestanding; no libc, no MMIO, no IOKit) ------------------ */

/* The SCM command id for each PAS operation. Returns 0, or 1 on a NULL. */
int adreno_zap_scm_cmd(uint32_t op, uint32_t *svc, uint32_t *cmd,
                       uint32_t *argc, uint32_t *owner);
/* Friendly op names: 0=init_image 1=mem_setup 2=auth_and_reset 3=shutdown
 * 4=is_supported. "unknown" out of range. */
const char *adreno_zap_scm_op_name(uint32_t op);

/* The peripheral id. Always 13 on enchilada. */
uint32_t adreno_zap_pas_id(void);

/* Per-step state. `out` may be NULL when capacity is 0. Returns 0, or 1 on
 * NULL/short capacity; *count is 8 on success, 0 otherwise. */
int adreno_zap_chain(struct adreno_zap_step_state *out, uint32_t capacity,
                     uint32_t *count);
/* How many of the eight are present today (expected: 1 -- firmware-name). */
uint32_t adreno_zap_present_count(void);
/* Name of a step, or "unknown". */
const char *adreno_zap_step_name(uint32_t step);
/* Pinned citation for a step, or "unknown". */
const char *adreno_zap_step_anchor(uint32_t step);

/* 1 when the WHOLE chain is present, i.e. the auth call could legitimately be
 * attempted. 0 today: it is derived from adreno_zap_present_count(), not
 * hard-coded, so it flips only when all eight genuinely exist. */
int adreno_zap_auth_ready(void);

/* The one fact a caller must not get wrong (P2:729): the call returns
 * `ret ?: res.result[0]`, so a successful SCM transport with a non-zero
 * PCTX result is a FAILURE. Returns 0, or 1 on NULLs. */
int adreno_zap_result_semantics(int transport_ret, uint32_t pctx_result,
                                int *is_success);
/* P4:158-163: -EOPNOTSUPP is NOT an error, it means the target does not use a
 * ZAP shader at all and the driver must quietly fall back. Returns 1 for that
 * case. */
int adreno_zap_unsupported_is_quiet(int scm_error);

/* Self-check. Returns 0 when every invariant holds, else 1; *check_count
 * receives the number evaluated. */
int adreno_zap_auth_verify(uint32_t *check_count);

/* ==========================================================================
 * ROUND 106 API: the three-stage contract, the derived SMC base, and the
 * two-channel result record.
 * ========================================================================== */

/* P3:69 SCM_SMC_FNID(s, c) = ((s & 0xFF) << 8) | (c & 0xFF). Pure arithmetic:
 * the caller passes any svc/cmd, so the shift/mask can be checked against the
 * pinned macro rather than trusted. */
uint32_t adreno_zap_scm_fnid(uint32_t svc, uint32_t cmd);

/* DERIVED, NOT PINNED: the SMCCC call-value base for this family, as used by
 * P2:355-356. Returns kAdrenoZapSmcBaseDerived. */
uint32_t adreno_zap_smc_base_derived(void);

/* 1 when `base` is a base this module will accept, else 0. RE-DERIVES the
 * owner field and requires BOTH ARM_SMCCC_OWNER_SIP and the SMC_64 bit, so a
 * caller cannot slip in 0xC1000000 (owner 1) or a bare 0x...0000. */
int adreno_zap_smc_base_gate(uint32_t base);

/* The owner field recovered from a base: (base >> 24) & 0x3F. This is the
 * function that makes the owner value DERIVABLE instead of asserted, and it is
 * how the verifier proves kAdrenoZapScmOwnerSip == 2 rather than 1. */
uint32_t adreno_zap_smc_base_owner(uint32_t base);

/* The provenance of the base, ALWAYS "derived-unpinned": none of P1-P5 pins it
 * because arm-smccc.h is not among the pinned sources. This string is what a
 * report must quote when it prints an x0, so the derivation can never be
 * mistaken for a pinned upstream fact. */
const char *adreno_zap_smc_base_provenance(void);

/* Fill the call record for one stage. `blob_relocatable` is the phdr-derived
 * fact (1 when any valid phdr carries QCOM_MDT_RELOCATABLE, P5:226-227); it
 * decides whether stage 1 is required. `region` supplies stage 1's arguments
 * and may be NULL only when the stage does not need it. `out` must be non-NULL.
 * Returns 0, or 1 on a bad stage or a NULL out. Makes no SCM call. */
int adreno_zap_pas_call(uint32_t stage, uint32_t base, int blob_relocatable,
                        const struct adreno_zap_pas_region *region,
                        struct adreno_zap_pas_call *out);

/* 1 when the stage MUST run for a blob with this relocatable-ness. Stage 0 and
 * stage 2 are unconditional (P5:236-249, P4:156); stage 1 is required exactly
 * when the blob is relocatable (P5:251-258). Nothing here is hard-coded. */
int adreno_zap_pas_stage_required(uint32_t stage, int blob_relocatable);

/* How many stages a loader must execute for this blob: 3 when relocatable,
 * 2 when not. Used by the verifier to prove the mem_setup requirement is
 * COMPUTED, and by a loader to size its loop. */
uint32_t adreno_zap_pas_required_count(int blob_relocatable);

/* Name/anchor of a stage, or "unknown" out of range. The anchor names the
 * pinned file:line that fixes that stage's position in the order. */
const char *adreno_zap_pas_stage_name(uint32_t stage);
const char *adreno_zap_pas_stage_anchor(uint32_t stage);

/* Enumerate the three stages IN ORDER into `out` (capacity >= 3 required).
 * This is the loader's iteration entry point: the array it fills is ordered by
 * construction, and each element carries its own x0. Returns 0, or 1 on NULL or
 * short capacity; *count is 3 on success, 0 otherwise. */
int adreno_zap_pas_sequence(uint32_t base, int blob_relocatable,
                            const struct adreno_zap_pas_region *region,
                            struct adreno_zap_pas_call *out, uint32_t capacity,
                            uint32_t *count);

/* ORDER ENFORCEMENT: 1 when `have_done` (bit i set == stage i completed) is a
 * legal prefix, i.e. stage `stage` may be attempted now. A loader that tries
 * auth_and_reset before init_image is refused HERE rather than by the secure
 * world. Returns 1 when allowed, 0 when refused, and also refuses `stage` >= 3. */
int adreno_zap_pas_order_allows(uint32_t stage, uint32_t have_done);

/* The bit a caller sets in `have_done` once a stage succeeds. Returns
 * (1u << stage), or 0 for an out-of-range stage. */
uint32_t adreno_zap_pas_done_bit(uint32_t stage);

/* The two-channel judgement for one stage (P2:729 `return ret ?: res.result[0]`).
 * Fills `out` with BOTH channels kept separate and the conjunction. This is the
 * same rule as adreno_zap_result_semantics() but with the channels recorded
 * instead of collapsed, because a loader must be able to log WHICH one failed.
 * Returns 0, or 1 on a NULL out. */
int adreno_zap_pas_judge(int transport_ret, uint32_t secure_result,
                         struct adreno_zap_pas_result *out);

/* ROUND 106 self-check for the three-stage contract. Returns 0 when every
 * invariant holds, else 1; *check_count receives the number evaluated. Kept
 * separate from adreno_zap_auth_verify() so the round-73 checks keep their
 * original meaning. */
int adreno_zap_pas_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_ZAP_AUTH_V1_H */
