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
 * peripheral id. P2:711 the owner is the SIP SMC owner. */
#define kAdrenoZapScmArgCount          1u
#define kAdrenoZapScmOwnerSip          1u /* ARM_SMCCC_OWNER_SIP is an SMC owner */

/* ---- The peripheral id, DEVICE-confirmed in round 71 ------------------ */
#define kAdrenoZapPasId                13u /* enchilada.dts:23612; qcom_scm.h:79 */

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

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_ZAP_AUTH_V1_H */
