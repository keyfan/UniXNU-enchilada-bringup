// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test for the stage-6a baseband / modem-PIL feasibility study. Exercises
// the classification tables and the blocker gate; asserts the facts that make
// the official "not-applicable" shortcut WRONG for enchilada and that make the
// three reference generations non-interchangeable.
//
// Built and run by scripts/check-enchilada-modem-host.py (host only).

#include "ModemPil.h"

#include <stdio.h>
#include <string.h>

static int gFailures;

static void check(int condition, const char *what) {
    if (!condition) {
        printf("  FAIL: %s\n", what);
        gFailures++;
    }
}

static void testLookupSdm845(void) {
    printf("  testLookupSdm845: self-auth flow, IPA v3.5.1, PAS id 5, mem-protect 0x0f\n");
    struct modem_soc_entry e;
    check(modem_pil_lookup(kModemSocSdm845, &e) == 0, "lookup succeeds");
    check(e.flow == kModemFlowSelfAuth, "SDM845 uses the self-auth flow");
    check(e.ipa == kModemIpaV351, "SDM845 is IPA v3.5.1");
    check(e.mss_base == MODEM_SDM845_MSS_BASE, "mss base 0x04080000");
    check(e.irq_spi == MODEM_SDM845_IRQ_SPI, "GIC SPI 266");
    check(e.self_auth == 1u, "qcom,pil-self-auth present");
    check(e.mem_protect_id == MODEM_SDM845_MEM_PROTECT, "mem-protect-id 0x0f");
    check(e.smp2p_remote_pid == MODEM_SDM845_SMP2P_PID, "smp2p remote-pid 1");
    check(e.pas_id == 0u, "self-auth boards do not use a single PAS id");
    check(e.dtb_name == 0, "SDM845 has no separate modem_dtb PAS id");
    check(!strcmp(e.mba_name, "mba.mbn"), "MBA loader is mba.mbn");
    check(!strcmp(e.image_name, "modem.mdt"), "main image is the SPLIT modem.mdt");
    check(modem_pil_lookup(kModemSocUnknown, &e) == -1, "unknown soc rejected");
    check(e.soc == kModemSocUnknown && e.flow == kModemFlowUnknown, "unknown zeroed");
    check(modem_pil_lookup(kModemSocSdm845, NULL) == -1, "NULL out rejected");
}

static void testGenerationGap(void) {
    printf("  testGenerationGap: SDM845 self-auth vs SM8550/SC7180 PAS are not interchangeable\n");
    struct modem_soc_entry sdm, sm8, sc7;
    check(modem_pil_lookup(kModemSocSdm845, &sdm) == 0, "SDM845 lookup");
    check(modem_pil_lookup(kModemSocSm8550, &sm8) == 0, "SM8550 lookup");
    check(modem_pil_lookup(kModemSocSc7180, &sc7) == 0, "SC7180 lookup");
    check(sdm.flow != sm8.flow, "flows differ");
    check(sdm.ipa != sm8.ipa, "IPA generations differ");
    check(sdm.ipa != sc7.ipa, "SDM845 != Coachz IPA");
    check(sm8.pas_id == MODEM_SM8550_PAS_ID, "SM8550 PAS id 4");
    check(sm8.dtb_pas_id == MODEM_SM8550_DTB_PAS_ID, "SM8550 dtb pas id 0x26");
    check(sdm.dtb_pas_id == 0u, "SDM845 has no modem_dtb pas id");
    check(strcmp(sdm.image_name, sc7.image_name) || sdm.flow != sc7.flow,
          "SDM845 cannot be driven by the Coachz configuration unchanged");
    check(modem_pil_needs_self_auth(kModemSocSdm845) == 1, "SDM845 needs self-auth");
    check(modem_pil_needs_self_auth(kModemSocSm8550) == 0, "SM8550 does not");
    check(modem_pil_needs_self_auth(kModemSocSc7180) == 0, "SC7180 does not");
    check(modem_pil_needs_self_auth(kModemSocUnknown) == 0, "unknown does not");
}

static void testSplitImageNames(void) {
    printf("  testSplitImageNames: real descriptors are .mdt; .mbn spellings are single files\n");
    check(!strcmp(modem_pil_firmware_name(0), "mba.mbn"), "role 0 is mba.mbn");
    check(!strcmp(modem_pil_firmware_name(1), "modem.mdt"), "role 1 is modem.mdt");
    check(!strcmp(modem_pil_firmware_name(2), "ipa_fws.mdt"), "role 2 is ipa_fws.mdt");
    check(modem_pil_firmware_name(3) == 0, "role 3 is out of range");
    check(modem_pil_firmware_role("mba.mbn") == MODEM_FW_MBA, "mba.mbn -> MBA");
    check(modem_pil_firmware_role("modem.mdt") == MODEM_FW_MPSS, "modem.mdt -> MPSS");
    check(modem_pil_firmware_role("modem.mbn") == MODEM_FW_MPSS, "modem.mbn -> MPSS too");
    check(modem_pil_firmware_role("ipa_fws.mdt") == MODEM_FW_IPA, "ipa_fws.mdt -> IPA");
    check(modem_pil_firmware_role("modem_dtb.mdt") == 0u,
          "modem_dtb.mdt is NOT a role we track (separate SM8550 PAS id)");
    check(modem_pil_firmware_role("random.bin") == 0u, "unrecognised -> 0");
    check(modem_pil_firmware_role(NULL) == 0u, "NULL -> 0");
    check(modem_pil_is_split_image("modem.mdt") == 1, "modem.mdt is split");
    check(modem_pil_is_split_image("ipa_fws.mdt") == 1, "ipa_fws.mdt is split");
    check(modem_pil_is_split_image("mba.mbn") == 0, "mba.mbn is a single file");
    check(modem_pil_is_split_image("modem.mbn") == 0, ".mbn spelling is not split");
    check(modem_pil_is_split_image(NULL) == 0, "NULL is not split");
}

static void testFirmwareMask(void) {
    printf("  testFirmwareMask: completeness + missing roles are bitmasks\n");
    check(modem_pil_firmware_complete(0) == 0, "empty is incomplete");
    check(modem_pil_firmware_complete(MODEM_FW_MBA | MODEM_FW_MPSS) == 0, "missing IPA");
    check(modem_pil_firmware_complete(MODEM_FW_ALL) == 1, "all three is complete");
    check(modem_pil_firmware_complete(MODEM_FW_ALL | 0x80u) == 1, "extra bits ignored");
    check(modem_pil_missing_firmware(0) == MODEM_FW_ALL, "all missing when empty");
    check(modem_pil_missing_firmware(MODEM_FW_MBA) == (MODEM_FW_MPSS | MODEM_FW_IPA),
          "missing MPSS+IPA when only MBA present");
    check(modem_pil_missing_firmware(MODEM_FW_ALL) == 0u, "nothing missing when complete");
}

static void testAssessToday(void) {
    printf("  testAssessToday: enchilada today blocks FIRMWARE|BACKEND|OS_ABI|GENERATION|TRUSTZONE\n");
    const unsigned today = modem_pil_assess(kModemSocSdm845, 0u, 0, 0, 0);
    check(today == (MODEM_BLOCK_FIRMWARE | MODEM_BLOCK_BACKEND | MODEM_BLOCK_OS_ABI |
                    MODEM_BLOCK_GENERATION | MODEM_BLOCK_TRUSTZONE),
          "all five blockers present");
    check(!(today & MODEM_BLOCK_UNKNOWN_SOC), "soc is not unknown");
    check(modem_pil_assess(kModemSocUnknown, MODEM_FW_ALL, 1, 1, 1) == MODEM_BLOCK_UNKNOWN_SOC,
          "unknown soc blocks immediately");
}

static void testAssessGenerationGate(void) {
    printf("  testAssessGenerationGate: an SDM845 target stays blocked even with everything available\n");
    /* Force every external resource to 1 and still require the generation gate
     * for the self-auth flow -- the honest core of the cross-generation claim. */
    const unsigned sdm = modem_pil_assess(kModemSocSdm845, MODEM_FW_ALL, 1, 1, 1);
    check(sdm == MODEM_FEASIBLE, "with a matching backend SDM845 becomes feasible");
    const unsigned sdm_no_backend = modem_pil_assess(kModemSocSdm845, MODEM_FW_ALL, 0, 1, 1);
    check(sdm_no_backend & MODEM_BLOCK_GENERATION, "no backend => generation blocker");
    check(sdm_no_backend & MODEM_BLOCK_BACKEND, "no backend => backend blocker");
    const unsigned sc7 = modem_pil_assess(kModemSocSc7180, MODEM_FW_ALL, 1, 1, 1);
    check(sc7 == MODEM_FEASIBLE, "a PAS board with a matching backend is feasible");
    check(!(modem_pil_assess(kModemSocSc7180, MODEM_FW_ALL, 0, 1, 1) & MODEM_BLOCK_GENERATION),
          "a PAS board without a backend is NOT generation-blocked");
}

static void testNotApplicableIsWrong(void) {
    printf("  testNotApplicableIsWrong: enchilada HAS a modem, so the official shortcut is false\n");
    check(modem_pil_board_has_modem(kModemSocSdm845) == 1,
          "SDM845/enchilada has qcom,mss@4080000 -> not-applicable would be a false claim");
    check(modem_pil_board_has_modem(kModemSocUnknown) == 0, "unknown board -> 0");
    struct modem_soc_entry e;
    check(modem_pil_lookup(kModemSocSdm845, &e) == 0 && e.mss_base == 0x04080000u,
          "the modem DT node base is recorded");
}

static void testNames(void) {
    printf("  testNames: flow/IPA strings distinguish the generations\n");
    check(strstr(modem_pil_flow_name(kModemFlowSelfAuth), "self-auth") != 0, "self-auth named");
    check(strstr(modem_pil_flow_name(kModemFlowPas), "PAS") != 0, "PAS named");
    check(!strcmp(modem_pil_flow_name(kModemFlowUnknown), "unknown"), "unknown flow named");
    check(strstr(modem_pil_ipa_name(kModemIpaV351), "v3.5.1") != 0, "IPA v3.5.1 named");
    check(strstr(modem_pil_ipa_name(kModemIpaV42), "v4.2") != 0, "IPA v4.2 named");
    check(strstr(modem_pil_ipa_name(kModemIpaV55), "v5.5") != 0, "IPA v5.5 named");
    check(!strcmp(modem_pil_ipa_name(kModemIpaUnknown), "unknown"), "unknown IPA named");
}

int main(void) {
    printf("enchilada stage-6a baseband / modem-PIL feasibility study (host)\n");
    testLookupSdm845();
    testGenerationGap();
    testSplitImageNames();
    testFirmwareMask();
    testAssessToday();
    testAssessGenerationGate();
    testNotApplicableIsWrong();
    testNames();
    if (gFailures) {
        printf("FAIL: %d assertion(s) failed\n", gFailures);
        return 1;
    }
    printf("PASS: enchilada stage-6a modem-PIL feasibility study (8 test groups)\n");
    return 0;
}
