// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// WcnChip -- enchilada stage 6 (network / Wi-Fi) HOST-VERIFIED feasibility /
// identification study.
//
// This is NOT a Wi-Fi driver. It is a small, freestanding, pure-C encoding of
// the authoritative hardware/driver facts gathered during the stage-6 audit,
// plus classification + gating functions that a future WCN3990 SNOC backend
// could call. Everything here is testable on the host; it maps nothing, writes
// no register, loads no firmware, joins no network, and is NOT linked into the
// kernel.
//
// Sources (four independent research passes, 2026-11-08)
// -------------------------------------------------------
//  (1) Official guide https://unixnu.com/zh/porting/network
//      The four components (Ath12kWifi / WifiProbe / WifiNet / MhiBoot) target
//      WCN7850 on SM8550 PCIe0: endpoint 17cb:1107, QMP PHY 0x1c06000,
//      MSI -> SPI 141, MHI BHI/BHIE firmware boot, sheng-wifi=5. Section 1:
//      "选择定义前，核对驱动中的端点硬件版本和固件支持" -- verify the endpoint
//      hardware version + firmware support before choosing the definitions.
//  (2) vendor DTB Mu-Silicium/Resources/DTBs/enchilada.dts :7908-7924
//      qcom,icnss@18800000: compatible="qcom,icnss",
//      reg=<0x18800000 0x800000 ...>, reg-names "membase\0smmu_iova_base\0...",
//      12 CE interrupts (SPI 414..425), qcom,wlan-msa-memory=<0x100000>,
//      5 vdd-* supplies, qcom,smmu-s1-bypass. NO PCIe endpoint, NO 17cb:1107.
//  (3) Linux v6.12 drivers/net/wireless/ath/ath10k/snoc.c + hw.h + qmi.c
//      WCN3990 = ATH10K_BUS_SNOC (of_match "qcom,wcn3990-wifi"), 12 Copy
//      Engines, 5 regulators, DMA_BIT_MASK(35), msa_size 0x100000,
//      WCN3990_HW_1_0_FW_DIR = "ath10k/WCN3990/hw1.0", board-2.bin,
//      firmware-N.bin; boot = QRTR/WLFW QMI + SCM qcom_scm_assign_mem MSA
//      permissions (TrustZone) + SDM845-specific msleep(20) XPU hack;
//      qcom,snoc-host-cap-8bit-quirk -> ATH10K_SNOC_FLAG_8BIT_HOST_CAP_QUIRK.
//  (4) sheng upstream package/WifiPcieTables.h + MhiBoot.h, workspace
//      inventory (analyze_network_local.py) and 246 decoded device logs
//      (analyze_network_logs.py): sheng method is pure PCIe/MHI; all WCN3990
//      firmware/board/cal names absent; no [network] section / no Wi-Fi
//      CONFIG; ZERO Wi-Fi-driver literals in the logs (IO80211/Apple80211/
//      ath10k/ath12k/WCN3990/wlan/MHI/QRTR/QMI/nw_wifi/board.bin/BSS/SSID = 0).
//
// Load-bearing conclusion (honest, 2026-11-08)
// -------------------------------------------
// enchilada WCN3990 = ath10k SNOC (memory-mapped membase, 12 CE, QMI/WLFW
// firmware + SCM MSA). sheng's four components are ath12k PCIe0 + MHI
// (WCN7850/SM8550). Different bus AND different generation => cannot be
// reused. The full Wi-Fi path is BLOCKED: (a) WCN3990 firmware/board/cal
// blobs absent; (b) no matching SNOC backend; (c) the QMI/QRTR/SCM/TrustZone
// firmware-boot chain has no iOS equivalent; (d) iOS IO80211/Apple80211/
// Skywalk ABI is closed-source and this host has no macOS/Xcode (CI is the
// only ABI authority); (e) 246 logs confirm no Wi-Fi driver present.
// This module records that decision in tested code; it claims NO device
// verification.
//
// The SNOC device is a DT-attested memory window, NOT a PCI vendor/device id;
// no register offset is asserted here (a future probe must source it).

#ifndef WCN_CHIP_H
#define WCN_CHIP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Chip selector (enum, not a raw id: WCN3990 has no single chip-id REGISTER;
 * its identity is the DT compatible + SNOC membase window). */
enum wcn_chip_id {
    kWcnChipUnknown = 0,
    kWcnChipWcn3990 = 1,  /* enchilada / SDM845 */
    kWcnChipWcn7850 = 2   /* sheng / SM8550 (reference) */
};

enum wcn_bus {
    kWcnBusUnknown = 0,
    kWcnBusSnoc = 1,   /* ath10k ATH10K_BUS_SNOC, memory-mapped membase */
    kWcnBusPcie = 2    /* ath12k PCIe endpoint */
};

enum wcn_fw_boot {
    kWcnBootUnknown = 0,
    kWcnBootQmiWlfw = 1,    /* QRTR + WLFW QMI + SCM MSA permissions */
    kWcnBootMhiBhiBhie = 2  /* MHI BHI/BHIE over PCIe BAR */
};

/* WCN3990 SNOC hardware constants from vendor DTB :7908 + Linux ath10k snoc.c. */
#define WCN3990_MEMBASE   0x18800000u  /* qcom,icnss membase */
#define WCN3990_MEMSIZE   0x00800000u
#define WCN3990_MSA_SIZE  0x00100000u  /* qcom,wlan-msa-memory=<0x100000> */
#define WCN3990_CE_IRQS   12u          /* WLAN_CE_0..11, SPI 414..425 */
#define WCN3990_REGS      5u           /* 5 ath10k_regulators vdd-* */

/* sheng WCN7850 PCIe endpoint (official network guide). */
#define WCN7850_PCIE_VENDOR 0x17cbu
#define WCN7850_PCIE_DEVICE 0x1107u

/* Firmware presence bitmask for the WCN3990 SNOC boot chain (three roles). */
#define WCN_FW_BOARD  (1u << 0)  /* board-2.bin : BDF via QMI_WLFW_BDF_DOWNLOAD_REQ */
#define WCN_FW_IMAGE  (1u << 1)  /* firmware-5.bin : ath10k FW API image */
#define WCN_FW_CAL    (1u << 2)  /* qcom,xo-cal-data : QMI_WLFW_CAL_REPORT_REQ */
#define WCN_FW_ALL    (WCN_FW_BOARD | WCN_FW_IMAGE | WCN_FW_CAL)

/* wcn_chip_assess() verdict: a bitmask of blockers, 0 == feasible. */
#define WCN_FEASIBLE           0x00u
#define WCN_BLOCK_UNKNOWN_CHIP 0x01u
#define WCN_BLOCK_FIRMWARE     0x02u
#define WCN_BLOCK_BACKEND      0x04u  /* matching-bus/generation host backend */
#define WCN_BLOCK_FW_BOOT      0x08u  /* QMI/QRTR/SCM/TrustZone equivalent */
#define WCN_BLOCK_OS_ABI       0x10u  /* IO80211/Apple80211/Skywalk */

struct wcn_chip_entry {
    enum wcn_chip_id id;
    const char *name;
    enum wcn_bus bus;
    enum wcn_fw_boot boot;
    uint32_t mem_base;     /* SNOC membase, 0 for PCIe */
    uint32_t mem_size;
    uint32_t msa_size;     /* SNOC MSA, 0 for PCIe */
    uint8_t  ce_irq_count;  /* 12 for SNOC, 0 (n/a) for PCIe */
    uint8_t  regulator_count; /* 5 for SNOC, 0 (n/a) for PCIe */
    uint16_t pci_vendor;   /* 0 for SNOC */
    uint16_t pci_device;   /* 0 for SNOC */
    const char *fw_dir;
    uint8_t  snoc_host_cap_8bit_quirk;
    uint8_t  smmu_s1_bypass;
};

/* Classify a chip selector. Returns 0 on success and fills *out; -1 if unknown.
 * On -1, *out is zeroed with id/bus/boot set to Unknown. NULL out -> -1. */
int wcn_chip_lookup(enum wcn_chip_id id, struct wcn_chip_entry *out);

const char *wcn_chip_bus_name(enum wcn_bus bus);
const char *wcn_chip_boot_name(enum wcn_fw_boot boot);
/* The WCN3990 firmware set (by role index 0..2), NULL for any other index. */
const char *wcn_chip_firmware_name(unsigned index);

/* 1 iff all three firmware bits are present, else 0. */
int wcn_chip_firmware_complete(unsigned firmware_mask);
/* The subset of WCN_FW_ALL bits that are missing. */
unsigned wcn_chip_missing_firmware(unsigned firmware_mask);

/* Feasibility gate for a full Wi-Fi path. firmware_mask = WCN_FW_* bits
 * actually present; backend_available = 1 only when a matching-bus/generation
 * host backend exists (sheng's is PCIe/MHI, so 0 for SNOC);
 * fw_boot_available = 1 only when the QMI/QRTR/SCM boot chain has an OS
 * equivalent; os_abi_available = 1 only when the OS network ABI exists.
 * Returns an OR of WCN_BLOCK_* flags; WCN_FEASIBLE means no blocker.
 * enchilada today: lookup(WCN3990), fw=0, backend=0, boot=0, abi=0 -> 0x1e,
 * and a SNOC/QMI chip stays blocked on BACKEND|FW_BOOT even when all
 * booleans are forced to 1 (honest cross-generation + firmware-boot gate). */
unsigned wcn_chip_assess(enum wcn_chip_id id, unsigned firmware_mask,
                         int backend_available, int fw_boot_available,
                         int os_abi_available);

/* Read-only sanity helpers (DT-attested, no register offset claimed). */
int wcn_chip_is_snoc_wcn3990(enum wcn_chip_id id);
int wcn_chip_matches_dtb_membase(uint32_t base, uint32_t size);

#ifdef __cplusplus
}
#endif
#endif
