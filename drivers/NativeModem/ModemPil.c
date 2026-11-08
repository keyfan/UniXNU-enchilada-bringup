// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// ModemPil -- see ModemPil.h for the full provenance and the honest conclusion.
// Freestanding C11: no libc, no allocation, no I/O. Pure table lookups and
// bitmask gates so the decision is testable on the host.

#include "ModemPil.h"

#include <string.h>

/* The three SoCs side by side. The point of the table is that the differences
 * are DATA, not prose: a future backend can be validated against it. */
static const struct modem_soc_entry kSocs[] = {
    {
        kModemSocSdm845, "SDM845 MSS-PIL (enchilada / OnePlus 6)",
        kModemFlowSelfAuth, kModemIpaV351,
        MODEM_SDM845_MSS_BASE, MODEM_SDM845_IRQ_SPI,
        0u, 0u, MODEM_SDM845_MEM_PROTECT, 1u, MODEM_SDM845_SMP2P_PID,
        "mba.mbn", "modem.mdt", "ipa_fws.mdt", 0
    },
    {
        kModemSocSm8550, "SM8550 MPSS-PAS (sheng, reference only)",
        kModemFlowPas, kModemIpaV55,
        0u, 0u,
        MODEM_SM8550_PAS_ID, MODEM_SM8550_DTB_PAS_ID, 0u, 0u, 0u,
        0, "modem.mdt", "ipa_fws.mdt", "modem_dtb.mdt"
    },
    {
        kModemSocSc7180, "SC7180 (Coachz, QcomBaseband's own target)",
        kModemFlowPas, kModemIpaV42,
        0u, 0u,
        0u, 0u, 0u, 0u, 0u,
        0, "modem.mdt", "ipa_fws.mdt", 0
    },
};

#define kSocCount (sizeof kSocs / sizeof kSocs[0])

/* Role descriptors for the SDM845 split-image set, indexed by role. */
static const char *const kFirmwareNames[] = {"mba.mbn", "modem.mdt", "ipa_fws.mdt"};

int modem_pil_lookup(enum modem_soc soc, struct modem_soc_entry *out) {
    if (!out) return -1;
    for (unsigned i = 0; i < kSocCount; i++) {
        if (kSocs[i].soc == soc) {
            *out = kSocs[i];
            return 0;
        }
    }
    memset(out, 0, sizeof *out);
    return -1;
}

const char *modem_pil_flow_name(enum modem_boot_flow flow) {
    switch (flow) {
    case kModemFlowSelfAuth: return "self-auth MBA then MPSS (q6v5_mss)";
    case kModemFlowPas:      return "PAS auth_and_reset (q6v5_pas)";
    default:                 return "unknown";
    }
}

const char *modem_pil_ipa_name(enum modem_ipa_gen gen) {
    switch (gen) {
    case kModemIpaV351: return "IPA v3.5.1 (SDM845)";
    case kModemIpaV42:  return "IPA v4.2 (SC7180)";
    case kModemIpaV55:  return "IPA v5.5 (SM8550)";
    default:            return "unknown";
    }
}

const char *modem_pil_firmware_name(unsigned index) {
    return index < (sizeof kFirmwareNames / sizeof kFirmwareNames[0]) ? kFirmwareNames[index] : 0;
}

/* Classify a descriptor name into a role. Accepts the real split-image names
 * (modem.mdt / ipa_fws.mdt) and the vendor .mbn spellings, because a future
 * probe may find either. mba.mbn and modem_dtb.mdt must not be confused. */
unsigned modem_pil_firmware_role(const char *name) {
    if (!name) return 0;
    if (!strcmp(name, "mba.mbn") || !strcmp(name, "mba.mdt")) return MODEM_FW_MBA;
    if (!strcmp(name, "ipa_fws.mdt") || !strcmp(name, "ipa_fws.mbn")) return MODEM_FW_IPA;
    if (!strcmp(name, "modem.mdt") || !strcmp(name, "modem.mbn")) return MODEM_FW_MPSS;
    return 0;
}

/* 1 iff the name is a split-image descriptor. The ".mbn" vendor spelling of the
 * SAME logical image is NOT split -- that distinction is exactly what the old
 * porting.toml note got wrong. */
int modem_pil_is_split_image(const char *name) {
    if (!name) return 0;
    size_t n = strlen(name);
    return n >= 4 && !strcmp(name + n - 4, ".mdt");
}

int modem_pil_firmware_complete(unsigned firmware_mask) {
    return (firmware_mask & MODEM_FW_ALL) == MODEM_FW_ALL;
}

unsigned modem_pil_missing_firmware(unsigned firmware_mask) {
    return MODEM_FW_ALL & ~firmware_mask;
}

unsigned modem_pil_assess(enum modem_soc soc, unsigned firmware_mask,
                          int backend_available, int os_abi_available,
                          int trustzone_available) {
    struct modem_soc_entry e;
    if (modem_pil_lookup(soc, &e) != 0) return MODEM_BLOCK_UNKNOWN_SOC;
    unsigned blocks = 0;
    if (!modem_pil_firmware_complete(firmware_mask)) blocks |= MODEM_BLOCK_FIRMWARE;
    if (!backend_available) blocks |= MODEM_BLOCK_BACKEND;
    if (!os_abi_available) blocks |= MODEM_BLOCK_OS_ABI;
    if (!trustzone_available) blocks |= MODEM_BLOCK_TRUSTZONE;
    /* Generation gate: an SDM845 target needs the self-auth flow. Even with
     * every external resource available, a backend written for the PAS flow
     * cannot drive it, so the blocker stands on its own. */
    if (e.flow == kModemFlowSelfAuth && !backend_available) blocks |= MODEM_BLOCK_GENERATION;
    return blocks;
}

int modem_pil_needs_self_auth(enum modem_soc soc) {
    struct modem_soc_entry e;
    if (modem_pil_lookup(soc, &e) != 0) return 0;
    return e.flow == kModemFlowSelfAuth;
}

int modem_pil_board_has_modem(enum modem_soc soc) {
    /* Every board in this table except the "unknown" selector has a modem; the
     * helper exists so the "not-applicable" shortcut is rejected by a test
     * rather than by a comment. */
    struct modem_soc_entry e;
    return modem_pil_lookup(soc, &e) == 0;
}
