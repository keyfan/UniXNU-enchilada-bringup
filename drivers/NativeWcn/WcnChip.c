// SPDX-License-Identifier: BSD-2-Clause-Patent
// WcnChip -- host-verified stage-6 (network) feasibility study. See header.
#include "WcnChip.h"

static const struct wcn_chip_entry kWcn3990 = {
    kWcnChipWcn3990,
    "WCN3990 (enchilada / SDM845)",
    kWcnBusSnoc,
    kWcnBootQmiWlfw,
    WCN3990_MEMBASE,        /* 0x18800000, qcom,icnss membase */
    WCN3990_MEMSIZE,        /* 0x00800000 */
    WCN3990_MSA_SIZE,       /* 0x00100000 */
    (uint8_t)WCN3990_CE_IRQS,   /* 12 */
    (uint8_t)WCN3990_REGS,      /* 5 */
    0u, 0u,                 /* SNOC: no PCI vendor/device id */
    "ath10k/WCN3990/hw1.0", /* WCN3990_HW_1_0_FW_DIR */
    1u,                     /* qcom,snoc-host-cap-8bit-quirk */
    1u                      /* qcom,smmu-s1-bypass */
};

static const struct wcn_chip_entry kWcn7850 = {
    kWcnChipWcn7850,
    "WCN7850 (sheng / SM8550)",
    kWcnBusPcie,
    kWcnBootMhiBhiBhie,
    0u, 0u, 0u,             /* PCIe: no SNOC membase/MSA */
    0u, 0u,                 /* PCIe MSI, no CE/regulator arrays here */
    WCN7850_PCIE_VENDOR,    /* 0x17cb */
    WCN7850_PCIE_DEVICE,    /* 0x1107 */
    "ath12k (PCIe/MHI)",    /* label, not a SNOC fw dir */
    0u,
    0u
};

static void zero_entry(struct wcn_chip_entry *out)
{
    unsigned char *b = (unsigned char *)out;
    size_t i;
    for (i = 0; i < sizeof(*out); ++i) {
        b[i] = 0u;
    }
    out->id = kWcnChipUnknown;
    out->bus = kWcnBusUnknown;
    out->boot = kWcnBootUnknown;
}

int wcn_chip_lookup(enum wcn_chip_id id, struct wcn_chip_entry *out)
{
    const struct wcn_chip_entry *entry;

    if (!out) {
        return -1;
    }
    if (id == kWcnChipWcn3990) {
        entry = &kWcn3990;
    } else if (id == kWcnChipWcn7850) {
        entry = &kWcn7850;
    } else {
        zero_entry(out);
        return -1;
    }
    zero_entry(out);
    out->id = entry->id;
    out->name = entry->name;
    out->bus = entry->bus;
    out->boot = entry->boot;
    out->mem_base = entry->mem_base;
    out->mem_size = entry->mem_size;
    out->msa_size = entry->msa_size;
    out->ce_irq_count = entry->ce_irq_count;
    out->regulator_count = entry->regulator_count;
    out->pci_vendor = entry->pci_vendor;
    out->pci_device = entry->pci_device;
    out->fw_dir = entry->fw_dir;
    out->snoc_host_cap_8bit_quirk = entry->snoc_host_cap_8bit_quirk;
    out->smmu_s1_bypass = entry->smmu_s1_bypass;
    return 0;
}

const char *wcn_chip_bus_name(enum wcn_bus bus)
{
    switch (bus) {
    case kWcnBusSnoc: return "SNOC (ath10k, membase)";
    case kWcnBusPcie: return "PCIe (ath12k endpoint)";
    default: return "unknown";
    }
}

const char *wcn_chip_boot_name(enum wcn_fw_boot boot)
{
    switch (boot) {
    case kWcnBootQmiWlfw: return "QMI/WLFW + SCM MSA (TrustZone)";
    case kWcnBootMhiBhiBhie: return "MHI BHI/BHIE over PCIe";
    default: return "unknown";
    }
}

const char *wcn_chip_firmware_name(unsigned index)
{
    switch (index) {
    case 0u: return "board-2.bin";      /* BDF via QMI_WLFW_BDF_DOWNLOAD_REQ */
    case 1u: return "firmware-5.bin";   /* ath10k FW API image */
    case 2u: return "qcom,xo-cal-data"; /* QMI_WLFW_CAL_REPORT_REQ */
    default: return 0;
    }
}

int wcn_chip_firmware_complete(unsigned firmware_mask)
{
    return (firmware_mask & WCN_FW_ALL) == WCN_FW_ALL ? 1 : 0;
}

unsigned wcn_chip_missing_firmware(unsigned firmware_mask)
{
    return (~firmware_mask) & WCN_FW_ALL;
}

unsigned wcn_chip_assess(enum wcn_chip_id id, unsigned firmware_mask,
                         int backend_available, int fw_boot_available,
                         int os_abi_available)
{
    unsigned blockers = WCN_FEASIBLE;
    struct wcn_chip_entry entry;

    if (wcn_chip_lookup(id, &entry) != 0) {
        return WCN_BLOCK_UNKNOWN_CHIP;
    }
    if (!wcn_chip_firmware_complete(firmware_mask)) {
        blockers |= WCN_BLOCK_FIRMWARE;
    }
    if (!backend_available) {
        blockers |= WCN_BLOCK_BACKEND;
    }
    if (!fw_boot_available) {
        blockers |= WCN_BLOCK_FW_BOOT;
    }
    if (!os_abi_available) {
        blockers |= WCN_BLOCK_OS_ABI;
    }
    /* Cross-generation / cross-bus honesty: a SNOC/WCN3990 device can NEVER
     * reuse sheng's PCIe/MHI ath12k backend, and the QMI/QRTR/SCM/TrustZone
     * boot chain has no iOS equivalent in this environment. Even if a caller
     * forces every boolean to 1, these two structural blockers remain. */
    if (entry.bus == kWcnBusSnoc || entry.boot == kWcnBootQmiWlfw) {
        blockers |= WCN_BLOCK_BACKEND | WCN_BLOCK_FW_BOOT;
    }
    return blockers;
}

int wcn_chip_is_snoc_wcn3990(enum wcn_chip_id id)
{
    return id == kWcnChipWcn3990 ? 1 : 0;
}

int wcn_chip_matches_dtb_membase(uint32_t base, uint32_t size)
{
    return (base == WCN3990_MEMBASE && size == WCN3990_MEMSIZE) ? 1 : 0;
}
