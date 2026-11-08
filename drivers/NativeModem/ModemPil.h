// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// ModemPil -- enchilada stage 6a (baseband / cellular) HOST-VERIFIED feasibility
// and scope study.
//
// This is NOT a modem driver. It is a small, freestanding, pure-C encoding of
// the authoritative hardware/driver facts gathered during the stage-6a audit,
// plus classification + gating functions a future SDM845 MSS-PIL backend could
// call. Everything here is testable on the host; it maps nothing, writes no
// register, loads no firmware, and is NOT linked into the kernel.
//
// Why this exists at all (and why it is NOT "not-applicable")
// -----------------------------------------------------------
// The official guide (https://unixnu.com/zh/porting/baseband) lets a Wi-Fi-only
// tablet without a cellular modem skip stage 6a:
//   "Wi-Fi-only tablets without a cellular modem can skip this page. Set the
//    cellular area of porting.toml to not-applicable."
// enchilada is a PHONE with a real modem: the vendor DTB carries
// qcom,mss@4080000 (compatible "qcom,pil-q6v55-mss", qcom,firmware-name
// "modem", qcom,pil-self-auth), plus SMEM/GLINK "mpss", SMP2P remote-pid 1 and
// an IPA node. Writing not-applicable would therefore be factually false, so
// the area stays a scoped, host-verified feasibility record instead.
//
// Sources (independent research passes, 2026-11-09)
// ------------------------------------------------
//  (1) Official guide https://unixnu.com/zh/porting/baseband : QcomBaseband by
//      HoshinoYumeka, built for the Coachz reference (SC7180). It is the layer
//      that talks to iOS ABM / BBUpdater / CommCenter; build recipe
//      build-qcom-baseband.py --coachz -> build/qcom-baseband/libQcomBaseband.a;
//      runtime identifiers AppleBaseband / AppleBasebandPCIControl /
//      AppleIPAppender / coachz_modem_configure / pdp_ip0..3.
//  (2) vendor DTB Mu-Silicium/Resources/DTBs/enchilada.dts
//      :6444-6484 qcom,mss@4080000: compatible "qcom,pil-q6v55-mss",
//        qcom,firmware-name="modem", qcom,pil-self-auth, qcom,qdsp6v65-1-0,
//        qcom,mem-protect-id=<0x0f>, interrupts=<0x00 0x10a 0x01> (GIC SPI 266),
//        mboxes = mss-pil.
//      :7323 qcom,smem@86000000 ; :7381 glink-smem-native-xprt-modem (label
//        "mpss") ; :7504 ipc_router_modem_xprt "qcom,ipc_router_glink_xprt" ;
//        :14183 qcom,smp2p-modem@1799000c (remote-pid 1) ;
//        :7793 qcom,ipa@01e00000 (qcom,ipa-hw-ver=<0x0d>, iommus <0x29 0x720>) ;
//        :7852 qcom,ipa_fws (pas-id 0x0f, firmware-name "ipa_fws").
//      NOTE: the DT property is qcom,gsi-loader, and NEITHER DTB sets it, and
//      mainline sdm845.dtsi leaves &ipa status=disabled with no such property --
//      so the loader is IPA_LOADER_SELF (legacy), never "qcom,gsid-loader".
//  (3) mainline sdm845-oneplus-common.dtsi :552-556 &ipa
//      firmware-name="qcom/sdm845/OnePlus/enchilada/ipa_fws.mbn" and :604-610
//      &mss_pil firmware-name=".../mba.mbn",".../modem.mbn".
//  (4) Linux v6.12 drivers/remoteproc/q6v5_mss.c :2007
//      qproc->hexagon_mdt_image = "modem.mdt"; drivers/net/ipa/ipa_main.c :71
//      IPA_FW_PATH_DEFAULT "ipa_fws.mdt". Both are SPLIT images (.mdt + .bNN),
//      not single .mbn files -- the ".mbn" vendor names are the entry pieces.
//  (5) sheng upstream inventory: there is NO baseband driver at all. No
//      QcomBaseband / IPA / CommCenter; the MHI code is WCN7850 Wi-Fi, the
//      GLINK/QRTR/SMEM/SMP2P/MDT/SCM code is the ADSP audio helper;
//      package/tests/modem_fw_host.c is only a host-side FAT/MDT firmware
//      reader; make-sheng-adt.py:143 says "no SEP or baseband"; sheng's
//      porting.toml has 13 areas and none is cellular.
//  (6) Three decoded device-log sweeps plus a workspace file inventory: zero
//      modem firmware files (*.mbn / *.mdt / *.bNN) and zero baseband driver
//      literals; the only matching string is the userspace process name
//      CommCenterMobileHelper.
//
// Load-bearing conclusion (honest, 2026-11-09)
// -------------------------------------------
// SDM845 MSS-PIL is a DIFFERENT GENERATION from the two references:
//   SDM845  : q6v5_mss.c, "qcom,pil-q6v55-mss", SELF-AUTH (MBA first, then the
//             MPSS image), MPSS PAS id 5, IPA v3.5.1, GLINK/QRTR over SMEM.
//   SM8550  : q6v5_pas.c, "qcom,sm8550-mpss-pas", PAS auth_and_reset, pas_id 4
//             plus a separate dtb_pas_id 0x26 (modem_dtb.mdt), IPA v5.5.
//   SC7180  : QcomBaseband's own target, IPA v4.2.
// So the official component cannot be reused as-is, and the full path is
// BLOCKED four ways: (a) FIRMWARE -- no signed MBA/MPSS/IPA images anywhere in
// the workspace and the whole modem image set requires Qualcomm signing;
// (b) BACKEND -- no SDM845 MSS-PIL/IPA generation backend exists locally or
// upstream; (c) OS_ABI -- ABM/BBUpdater/CommCenter/AppleBaseband* are
// closed-source and this host has no macOS/Xcode (CI is the only ABI
// authority); (d) GENERATION -- the vendor DT self-auth flow and the SC7180
// PAS flow are not interchangeable.
// Also blocking independently of the above: qcom,pil-self-auth plus
// qcom,mem-protect-id=0x0f require TrustZone/XPU memory assignment
// (qcom_scm_assign_mem, RMB auth 0x3/0x4) that has no iOS equivalent here.
//
// This module records that decision in tested code; it claims NO device
// verification. If the user later puts baseband in scope, the FIRST milestone is
// not a data call -- it is proving that a signed MBA can be authenticated at all.

#ifndef MODEM_PIL_H
#define MODEM_PIL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SoC selector: the reference generation decides which remoteproc backend and
 * which PAS flow apply. */
enum modem_soc {
    kModemSocUnknown = 0,
    kModemSocSdm845 = 1,   /* enchilada / OnePlus 6 -- q6v5_mss SELF-AUTH */
    kModemSocSm8550 = 2,   /* sheng -- q6v5_pas + modem_dtb */
    kModemSocSc7180 = 3    /* Coachz -- QcomBaseband's own reference */
};

enum modem_boot_flow {
    kModemFlowUnknown = 0,
    kModemFlowSelfAuth = 1,  /* MBA authenticated first, then MPSS (SDM845) */
    kModemFlowPas = 2        /* qcom_scm_pas_auth_and_reset (SM8550/SC7180) */
};

enum modem_ipa_gen {
    kModemIpaUnknown = 0,
    kModemIpaV351 = 1,   /* SDM845 */
    kModemIpaV42 = 2,    /* SC7180 (QcomBaseband target) */
    kModemIpaV55 = 3     /* SM8550 */
};

/* SDM845 MSS-PIL facts (vendor DTB :6444-6484 + q6v5_mss.c). */
#define MODEM_SDM845_MSS_BASE      0x04080000u  /* qcom,mss@4080000 */
#define MODEM_SDM845_IRQ_SPI       0x010au      /* interrupts=<0 0x10a 1> */
#define MODEM_SDM845_PAS_ID        5u           /* QCOM_SCM_MP_MODEM */
#define MODEM_SDM845_MEM_PROTECT   0x0fu        /* qcom,mem-protect-id */
#define MODEM_SDM845_SMP2P_PID     1u           /* remote-pid 1 */
#define MODEM_SDM845_IPA_PAS_ID    0x0fu        /* qcom,ipa_fws pas-id */

/* SM8550 counterpart, recorded only to make the generation gap explicit. */
#define MODEM_SM8550_PAS_ID        4u
#define MODEM_SM8550_DTB_PAS_ID    0x26u

/* Firmware ROLES. The SDM845 chain is a split-image set: the .mdt is the
 * descriptor and the .bNN files carry the segments, so presence must be tracked
 * per role rather than as one filename. */
#define MODEM_FW_MBA        (1u << 0)  /* mba.mbn     : the authenticating loader */
#define MODEM_FW_MPSS       (1u << 1)  /* modem.mdt   : the modem image itself */
#define MODEM_FW_IPA        (1u << 2)  /* ipa_fws.mdt : IPA firmware */
#define MODEM_FW_ALL        (MODEM_FW_MBA | MODEM_FW_MPSS | MODEM_FW_IPA)

/* modem_pil_assess() verdict: a bitmask of blockers, 0 == feasible. */
#define MODEM_FEASIBLE           0x00u
#define MODEM_BLOCK_UNKNOWN_SOC  0x01u
#define MODEM_BLOCK_FIRMWARE     0x02u  /* signed images absent */
#define MODEM_BLOCK_BACKEND      0x04u  /* matching-generation remoteproc backend */
#define MODEM_BLOCK_OS_ABI       0x08u  /* ABM/BBUpdater/CommCenter/AppleBaseband* */
#define MODEM_BLOCK_GENERATION   0x10u  /* self-auth vs PAS flow cannot be swapped */
#define MODEM_BLOCK_TRUSTZONE    0x20u  /* pil-self-auth + mem-protect-id XPU */

struct modem_soc_entry {
    enum modem_soc soc;
    const char *name;
    enum modem_boot_flow flow;
    enum modem_ipa_gen ipa;
    uint32_t mss_base;        /* remoteproc register window, 0 if n/a here */
    uint16_t irq_spi;         /* GIC SPI, 0 if not recorded */
    uint8_t  pas_id;          /* PAS id used for auth_and_reset, 0 if self-auth */
    uint8_t  dtb_pas_id;      /* separate modem_dtb PAS id (SM8550), 0 otherwise */
    uint8_t  mem_protect_id;  /* qcom,mem-protect-id, 0 if absent */
    uint8_t  self_auth;       /* 1 iff qcom,pil-self-auth is present */
    uint8_t  smp2p_remote_pid;
    const char *mba_name;     /* authenticating loader, NULL if the flow has none */
    const char *image_name;   /* main modem image descriptor */
    const char *ipa_fw_name;
    const char *dtb_name;     /* separate modem DTB descriptor, NULL if none */
};

/* Classify a SoC selector. Returns 0 on success and fills *out; -1 if unknown.
 * On -1, *out is zeroed with soc/flow/ipa set to Unknown. NULL out -> -1. */
int modem_pil_lookup(enum modem_soc soc, struct modem_soc_entry *out);

const char *modem_pil_flow_name(enum modem_boot_flow flow);
const char *modem_pil_ipa_name(enum modem_ipa_gen gen);
/* Firmware descriptor name for a role index 0..2 on SDM845, NULL otherwise. */
const char *modem_pil_firmware_name(unsigned index);
/* The firmware role a descriptor name corresponds to, 0 if unrecognised.
 * Understands the real split-image names (modem.mdt / ipa_fws.mdt) AND the
 * vendor .mbn spellings, so a future probe can classify whatever it finds. */
unsigned modem_pil_firmware_role(const char *name);
/* 1 if the name denotes a SPLIT image (.mdt descriptor), 0 for a single file. */
int modem_pil_is_split_image(const char *name);

/* 1 iff all three firmware roles are present, else 0. */
int modem_pil_firmware_complete(unsigned firmware_mask);
/* The subset of MODEM_FW_ALL bits that are missing. */
unsigned modem_pil_missing_firmware(unsigned firmware_mask);

/* Feasibility gate for a full baseband path. firmware_mask = MODEM_FW_* bits
 * actually present; backend_available = 1 only when a matching-generation
 * remoteproc/IPA backend exists; os_abi_available = 1 only when the OS baseband
 * ABI (ABM/BBUpdater/CommCenter) is available; trustzone_available = 1 only when
 * the XPU/SCM memory-protect flow can be reproduced.
 * Returns an OR of MODEM_BLOCK_* flags; MODEM_FEASIBLE means no blocker.
 * enchilada today: SDM845, fw=0, backend=0, abi=0, tz=0 -> 0x3e, and an SDM845
 * target stays blocked on BACKEND|GENERATION even when every boolean is forced
 * to 1 (honest cross-generation gate). */
unsigned modem_pil_assess(enum modem_soc soc, unsigned firmware_mask,
                          int backend_available, int os_abi_available,
                          int trustzone_available);

/* 1 iff this SoC needs the two-step MBA-then-MPSS self-auth flow (SDM845) and
 * therefore CANNOT be driven by a plain qcom_scm_pas_auth_and_reset call. */
int modem_pil_needs_self_auth(enum modem_soc soc);
/* 1 iff the board actually has a modem, which is what makes the official
 * "not-applicable" shortcut WRONG for this device. */
int modem_pil_board_has_modem(enum modem_soc soc);

#ifdef __cplusplus
}
#endif
#endif
