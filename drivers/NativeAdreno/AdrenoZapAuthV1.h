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
//  (P6) drivers/firmware/qcom/qcom_scm-smc.c  5356 B   [ADDED IN ROUND 108]
//       e9121b279b3813b8ca3e0ac27fdcbe367b1e12cbfcbf39d75c754d3bc88f8c45
//       This file DEFINES __scm_smc_call(), which P3:70-74 only declares. It is
//       the file that settles -- rather than infers -- what the SMC returns:
//       :171     smc.args[1] = desc->arginfo;        x1 is the ARGINFO
//       :172-173 smc.args[i + SCM_SMC_FIRST_REG_IDX] = desc->args[i];
//       :35      #define SCM_SMC_FIRST_REG_IDX 2      so args[0] -> x2
//       :204     res->result[0] = smc_res.a1;        result[0] is a1, NOT a0
//       :209     return (long)smc_res.a0 ? qcom_scm_remap_error(smc_res.a0) : 0;
//                a0 is the STATUS, mapped through qcom_scm_remap_error (P3:160).
//       TWO CONSEQUENCES, both acted on in round 108:
//       (a) the two-channel model is CORRECT -- transport_ret comes from a0 and
//           secure_result from a1 -- but the naive reading "a0 IS result[0]" is
//           WRONG. They are different registers; that is why the contract must
//           keep them apart.
//       (b) the REGISTER LAYOUT rounds 106-107 used was off by one: they put the
//           peripheral in x1 and the first payload in x2. P6:171 puts ARGINFO in
//           x1 and P6:172 puts args[0] in x2. Corrected in the typedef and the
//           executor; the tests pin every slot.
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

/* The provenance of the base, ALWAYS "derived-unpinned": none of P1-P6 pins it.
 * P6 (added in round 108) proves the RETURN mapping and the register layout, but
 * it composes x0 from ARM_SMCCC_CALL_VAL(type, convention, OWNER, fnid) at
 * P6:166-169 -- the base's numeric value still comes from arm-smccc.h, which is
 * NOT among the pinned sources. So "derived" remains correct for the BASE even
 * though the mapping is now proven. This string is what a report must quote when
 * it prints an x0, so the derivation can never be mistaken for a pinned fact. */
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

/* ==========================================================================
 * ROUND 126: THE mem_setup SPAN, and WHY `kAdrenoZapSegBytes` IS NOT IT
 * ==========================================================================
 * THE DEFECT THIS CLOSES. Until this round the probe passed the SEGMENT'S FILE
 * LENGTH (1968, kAdrenoZapSegBytes) as mem_setup's args[2]. Upstream computes a
 * different number -- a REGION SPAN over the valid program headers -- and the
 * two differ on this blob:
 *
 *   P1 mdt_loader.c:229-233, over VALID phdrs only (P1:21-33):
 *       min_addr = min(p_paddr)
 *       max_addr = ALIGN(p_paddr + p_memsz, SZ_4K)   <- per phdr, then max
 *   P1 mdt_loader.c:252
 *       qcom_scm_pas_mem_setup(pas_id, mem_phys, max_addr - min_addr)
 *
 *   a630_zap.mdt, re-derived from the real 6684-byte file:
 *       ph[0] type=0     paddr=0x0000 memsz=0     INVALID (not PT_LOAD, memsz 0)
 *       ph[1] type=0     paddr=0x6000 memsz=8192  INVALID (not PT_LOAD; HASH)
 *       ph[2] type=1     paddr=0x5000 memsz=1968  VALID
 *     -> min_addr = 0x5000
 *     -> max_addr = ALIGN(0x5000 + 0x4E0, 0x1000) = ALIGN(0x54E0) = 0x6000
 *     -> span     = 0x1000            (NOT 1968, which is 0x7B0)
 *
 * THE NUMBER ITSELF IS NOT RE-DERIVED HERE, AND THAT IS DELIBERATE.
 * P1 mdt_loader.c:220-234 (qcom_mdt_pas_init's span loop) and P1:88-99
 * (qcom_mdt_get_size) accumulate byte-for-byte the same min/ALIGNed-max pair --
 * the ONLY difference is that pas_init also records `relocate`. So the span is
 * exactly what adreno_mdt_get_size() (AdrenoMdtV1.h:163-166) already returns,
 * and it is already host-tested against the real blob
 * (verify-enchilada-mdt.py:173 asserts it is 0x1000). Writing a SECOND parser
 * here would be two derivations of one number that could silently drift -- the
 * precise failure mode this port keeps closing. THE PROBE CALLS
 * adreno_mdt_get_size() AND THIS MODULE CHECKS THE RESULT.
 *
 * WHY A CHECK FUNCTION IS STILL NEEDED. "Call get_size and trust it" leaves the
 * original mistake available: a caller can still pass the wrong number, and
 * nothing catches it. This function makes the RULE explicit and refusable, and
 * gives the self-check one place to pin both the right and the wrong value.
 * -------------------------------------------------------------------------- */

/* P1:229-233 / P1:252. Decide whether `bytes` is the mem_setup span for a blob
 * whose valid phdrs span [min_paddr, max_paddr_end). `max_paddr_end` is the
 * ALIGNed end (P1:232), i.e. the same top edge adreno_mdt_get_size() returns
 * added back to its minimum. Returns:
 *   1  OK         bytes == max_paddr_end - min_paddr   (the upstream answer)
 *   0  REFUSED    anything else, INCLUDING the segment-file-length confusion:
 *                 for this blob (0x5000, 0x6000, 1968) returns 0 while
 *                 (0x5000, 0x6000, 0x1000) returns 1.
 * Also refuses a non-page-aligned span, an inverted/empty range, and 32-bit
 * overflow. Pure arithmetic: no parse, no allocation, no SCM. */
int adreno_zap_pas_mem_setup_span_ok(uint32_t min_paddr, uint32_t max_paddr_end,
                                    uint32_t bytes);

/* The page both this module and the MDT parser align to (P1:98/:233 SZ_4K).
 * Named here so a caller never spells 0x1000 as a literal, and asserted equal
 * to AdrenoMdtV1's kAdrenoMdtPageBytes by the self-check. */
#define kAdrenoZapSpanPageBytes        0x1000u

/* ==========================================================================
 * ROUND 126: the PAS_IS_SUPPORTED probe -- a READ-ONLY secure-world question
 * ==========================================================================
 * WHY THIS IS SEPARATE FROM THE THREE-STAGE CHAIN, AND WHY THAT MATTERS.
 * The three-stage chain asks the secure world to DO something (authenticate an
 * image, prepare a region, release a processor). Every one of those can fail
 * for reasons that say nothing about whether the peripheral is even supported.
 * PAS_IS_SUPPORTED asks only "is secure PIL implemented for this peripheral?"
 * -- P2 qcom_scm.c:776-796 issues it with ONE argument and writes nothing
 * anywhere. Its answer is the first fact that can DISTINGUISH "our arguments
 * were wrong" from "this target will never accept PAS 13", and until round 125
 * the port had no way to ask it.
 *
 * THE CALL SHAPE, with its two independent sources:
 *   svc/cmd   0x02 / 0x07   P3:102 QCOM_SCM_PIL_PAS_IS_SUPPORTED
 *   arginfo   ARGS(1) = 0x01  P2:782 -- ONE argument
 *   args[0]   the peripheral  P2:783
 *   x0        base | FNID(0x02, 0x07) = base | 0x0207
 * Pinned TZ cross-check (scm_sip_interface.h:853-857): @smc_id 0x02000207,
 * @param_id 0x00000001, and the request struct (:3736-3741) has exactly ONE
 * field (`proc`). So the arity is confirmed by the INTERFACE, not inferred.
 *
 * WHERE THE VERDICT LIVES -- THE ONE THING A CALLER MUST NOT GET WRONG.
 * The response struct (scm_sip_interface.h:3749-3754) is
 *     tz_syscall_rsp_t common_rsp;  UINT64 verdict;
 * and `verdict` is the SECOND field, so it arrives in **a1**, i.e. the
 * SECURE_RESULT channel -- NOT in a0. a0 is the transport status, and 0 there
 * means "the call was made", not "yes". Judging this call on a0 alone reports
 * support==false for every successful probe. adreno_zap_pas_supported_verdict()
 * exists so that confusion has exactly one place to be wrong.
 * -------------------------------------------------------------------------- */

/* P2:782-783: the ONE-argument shape of PAS_IS_SUPPORTED. The command id is
 * NOT redefined here -- kAdrenoZapScmPasIsSupported (0x07, P3:102) already
 * exists above, and a second name for one fact is how two values drift. */
#define kAdrenoZapArgInfoIsSupported    0x01u /* P2:782 ARGS(1)             */

/* Build the PAS_IS_SUPPORTED call record. `out` must be non-NULL. Returns 0, or
 * 1 on a NULL out or a base that fails adreno_zap_smc_base_gate() -- the same
 * gate as every other stage, so a caller cannot probe with a wrong x0. Sets
 * cmd 0x07, arginfo ARGS(1), and x0 = base | 0x0207. Makes no SCM call. */
int adreno_zap_pas_is_supported_call(uint32_t base,
                                     struct adreno_zap_pas_call *out);

/* Decode the two channels of a PAS_IS_SUPPORTED reply into ONE verdict.
 *   1  SUPPORTED           transport_ret == 0 && secure_result == 1
 *   0  NOT SUPPORTED       transport_ret == 0 && secure_result == 0
 *  -1  INCONCLUSIVE        the call itself failed: transport_ret != 0 means the
 *                          SMC did not complete, and secure_result != 0 with a
 *                          clean transport means the secure world answered with
 *                          an error instead of a verdict. Neither may be read as
 *                          "false" -- that is the P2:729 trap this port exists
 *                          to avoid.
 * Returns 0, or 1 on a NULL out. */
int adreno_zap_pas_supported_verdict(int transport_ret, uint32_t secure_result,
                                    int *supported);

/* ==========================================================================
 * ROUND 107: THE SCM TRANSPORT SEAM (contract layer only -- NO smc here)
 * ==========================================================================
 * WHY THIS EXISTS
 * ---------------
 * Round 106 produced an ordered three-stage contract that nobody can execute,
 * because the only real SMC in this tree is `static ScmResult scm(...)` at
 * drivers/ArmCpu/ArmCpu.cpp:126-137 -- a FILE-LOCAL function. It genuinely
 * executes `smc #0` on the phone (509 device snapshots, no -99 sentinel), but
 * being `static` it is invisible to every other translation unit, so the PAS
 * chain cannot reach it.
 *
 * THE DESIGN RULE OF THIS ROUND, WHICH IS THE WHOLE POINT
 * ------------------------------------------------------
 * CONTRACT AND TRANSPORT ARE SEPARATED. This unit declares the SHAPE of an SCM
 * call and the ORDERED ORCHESTRATION over it. It contains NO `smc` instruction,
 * NO inline assembly, NO register-pinning, and NO knowledge of which privilege
 * level the caller runs at. The actual transport is INJECTED by the caller:
 *   - production: a thin `extern "C"` wrapper that the ArmCpu owner would add
 *     around its existing `scm()` (see work/tmp/r107-scm-export-plan.md -- the
 *     plan is written and reviewed separately; ArmCpu.cpp is NOT touched here);
 *   - host test: a stub that records arguments and replays canned results.
 *
 * WHY SEPARATION IS LOAD-BEARING AND NOT TIDINESS: scripts/verify-ci-246-adreno.py
 * :204-206 fails the CI run if `_qcom_scm_call` or `adreno_zap_shader_load`
 * appears in the linked kernel image. Putting an `smc` in this unit would both
 * break that boundary and make the module untestable on the host, since an
 * `smc #0` cannot execute in a userspace test binary. Injection solves both at
 * once: the module stays pure C11 with no privileged instruction, so it is
 * fully host-testable, AND it emits no forbidden symbol.
 *
 * THE BOUNDARY IS MECHANICALLY CHECKED. scripts/verify-enchilada-zap-auth.py
 * asserts that this header and the .c contain no inline-assembly primitives at
 * all -- no assembler statement forms, no register-pinning declarations, no
 * SMCCC macro names, and no system-register mnemonics. That list is deliberately
 * NOT spelled out here: a boundary that names its own forbidden tokens in prose
 * can only be enforced with exceptions, so the verifier holds the list and these
 * files stay clean of every one of them. A future edit that "helpfully" inlines
 * the transport fails that verifier rather than silently crossing the CI
 * boundary.
 *
 * HONEST BOUNDARY: nothing here issues an SMC. `adreno_zap_pas_execute()`
 * requires a non-NULL injected call function; with NULL it fails closed and
 * calls nothing. No transport is exported, no ArmCpu file is modified, and the
 * zap-auth chain's S8 stays present = 0.
 * -------------------------------------------------------------------------- */

/* The injected transport. Returns 0 when the SMC transport itself completed;
 * nonzero is a transport failure (the remapped smc a0). `out_result0` receives
 * the secure world's result[0], which P6:204 proves is smc a1 -- NOT a0.
 *
 * THE PARAMETER NAMES ARE THE REGISTER MAP, and they were WRONG before round
 * 108. P6:166-173 fills the SMC registers in this order:
 *   x0 = CALL_VAL(type, conv, owner, FNID(svc,cmd))        <- P6:166-170
 *   x1 = desc->arginfo                                     <- P6:171
 *   x2 = desc->args[0]                                     <- P6:172-173, idx 2
 *   x3 = desc->args[1]
 *   x4 = desc->args[2]
 *   x5 = desc->args[3]
 * So the peripheral (args[0], P2:710) travels in **x2**, and **x1 carries the
 * arginfo**, not the peripheral. Rounds 106-107 had this shifted by one slot:
 * they put the peripheral in x1 and the first payload in x2, which would have
 * sent arginfo=13 and a garbage args[0] to the secure world.
 *
 * `args2` exists because mem_setup is ARGS(3): its third argument (the region
 * size, P2:673) lands in args[2] = x4. A wrapper over ArmCpu.cpp:126-137's
 * CURRENT `scm(fn, info, a2, a3)` cannot carry it -- that function pins
 * x4 = x5 = 0 (:132-133). See work/tmp/r107-scm-export-plan.md: the export must
 * widen `scm()` before mem_setup can be issued at all.
 *
 * CONTRACT FOR IMPLEMENTORS: must not retain the pointers, must be callable
 * with `cookie` == NULL only if it never dereferences it, and must return
 * nonzero rather than fabricating a success when the SMC could not be made. */
typedef int (*adreno_zap_scm_fn)(void *cookie, uint32_t fn, uint32_t arginfo,
                                 uint32_t args0, uint32_t args1, uint32_t args2,
                                 uint64_t *out_result0);

/* The injection point. A caller-facing struct rather than a bare function
 * pointer so a transport can carry state (e.g. a lock, a trace buffer) without
 * a global. Mirrors the AdrenoGmuBootV1Bus convention already used in this
 * module family. */
struct adreno_zap_transport {
    void *cookie;                /* passed back verbatim; may be NULL        */
    adreno_zap_scm_fn call;      /* NULL means "no transport wired" -> refuse */
};

/* What actually happened, per stage, so a caller can log WHICH call failed
 * without re-deriving it from return codes. */
struct adreno_zap_pas_stage_result {
    uint32_t stage;              /* enum adreno_zap_pas_stage                */
    uint32_t x0;                 /* the fn id actually handed to the transport*/
    int called;                  /* 1 when the transport was invoked         */
    int transport_ret;
    uint32_t secure_result;
    int is_success;              /* transport_ret == 0 && secure_result == 0  */
};

/* The full orchestration report. `stages_run` is the number of stages the
 * transport was ACTUALLY called for, which is what makes "failed at stage 1"
 * distinguishable from "never started". */
struct adreno_zap_pas_report {
    uint32_t stages_run;         /* 0..3 -- how many were attempted           */
    uint32_t first_failure;      /* stage index, or Count when all succeeded  */
    int all_succeeded;           /* 1 only when all THREE succeeded           */
    uint32_t have_done;          /* the prefix bitset actually achieved        */
    struct adreno_zap_pas_stage_result stage[kAdrenoZapPasStageCount];
};

/* Status codes. Each names the rule that stopped the run. */
typedef enum {
    kAdrenoZapExecOk = 0,           /* all three stages succeeded            */
    kAdrenoZapExecNoTransport = 1,  /* transport or its call fn is NULL      */
    kAdrenoZapExecBadBase = 2,      /* base failed adreno_zap_smc_base_gate  */
    kAdrenoZapExecNoRegion = 3,     /* mem_setup stage has no region         */
    kAdrenoZapExecStageFailed = 4,  /* a stage's transport/secure result != 0*/
    kAdrenoZapExecNullOut = 5       /* out/report pointer was NULL           */
} AdrenoZapExecStatus;

/* 1 when the transport is usable (non-NULL struct AND non-NULL call), else 0.
 * Provided so a caller can distinguish "not wired" from "wired and failing"
 * BEFORE starting a run. */
int adreno_zap_transport_ready(const struct adreno_zap_transport *t);

/* EXECUTE the three-stage PAS chain through an injected transport.
 *
 * This is the orchestration the title of round 107 refers to:
 *   stage 0  init_image       fn = base | 0x0201, arg1 = metadata phys
 *   stage 1  mem_setup        fn = base | 0x0202, arg1 = region phys, a2 = size
 *   stage 2  auth_and_reset   fn = base | 0x0205, arg1 = 0
 *
 * FAIL-CLOSED, and this is the property the tests pin: the FIRST failing stage
 * STOPS the chain. No later stage is attempted, so a rejected metadata image
 * can never be followed by a mem_setup or an auth call. A stage fails when the
 * transport returns nonzero OR the secure world's result[0] is nonzero --
 * both channels are required to be zero (P2:729).
 *
 * `metadata_phys` is stage 0's argument (the secure-world-readable physical
 * address of the PAS metadata blob); `region` supplies stage 1's. Neither is
 * dereferenced -- they are addresses the secure world is told about, not
 * buffers this function touches.
 *
 * Returns an AdrenoZapExecStatus. `out` receives the per-stage report and is
 * written on EVERY return path that gets past the NULL check, so a caller can
 * always see how far the chain got. Makes no SMC itself: every hardware effect
 * belongs to the injected transport. */
AdrenoZapExecStatus adreno_zap_pas_execute(const struct adreno_zap_transport *t,
                                           uint32_t base, uint32_t metadata_phys,
                                           const struct adreno_zap_pas_region *region,
                                           struct adreno_zap_pas_report *out);

/* ROUND 107 self-check for the transport seam and the orchestration. Returns 0
 * when every invariant holds, else 1; *check_count receives the number
 * evaluated. Uses an internal stub transport -- no SMC is issued. */
int adreno_zap_transport_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_ZAP_AUTH_V1_H */
