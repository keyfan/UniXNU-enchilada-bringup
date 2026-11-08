// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the stage-6 (network) feasibility study. Pure host, no IOKit,
// no hardware. Verifies the WCN3990-SNOC / WCN7850-PCIe encoding + the honest
// blocked verdict.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "WcnChip.h"

static void testWcn3990Lookup()
{
    struct wcn_chip_entry r;
    assert(wcn_chip_lookup(kWcnChipWcn3990, &r) == 0);
    assert(r.id == kWcnChipWcn3990);
    assert(std::strcmp(r.name, "WCN3990 (enchilada / SDM845)") == 0);
    assert(r.bus == kWcnBusSnoc);
    assert(r.boot == kWcnBootQmiWlfw);
    assert(r.mem_base == 0x18800000u);
    assert(r.mem_size == 0x00800000u);
    assert(r.msa_size == 0x00100000u);
    assert(r.ce_irq_count == 12u);
    assert(r.regulator_count == 5u);
    assert(r.pci_vendor == 0u && r.pci_device == 0u);
    assert(std::strcmp(r.fw_dir, "ath10k/WCN3990/hw1.0") == 0);
    assert(r.snoc_host_cap_8bit_quirk == 1u);
    assert(r.smmu_s1_bypass == 1u);
    printf("  testWcn3990Lookup: WCN3990 = ath10k SNOC, membase 0x18800000, 12 CE, MSA 1MiB\n");
}

static void testWcn7850Lookup()
{
    struct wcn_chip_entry r;
    assert(wcn_chip_lookup(kWcnChipWcn7850, &r) == 0);
    assert(r.id == kWcnChipWcn7850);
    assert(r.bus == kWcnBusPcie);
    assert(r.boot == kWcnBootMhiBhiBhie);
    assert(r.mem_base == 0u && r.msa_size == 0u);
    assert(r.pci_vendor == 0x17cbu);
    assert(r.pci_device == 0x1107u);
    assert(r.snoc_host_cap_8bit_quirk == 0u);
    printf("  testWcn7850Lookup: WCN7850 = ath12k PCIe endpoint 17cb:1107, MHI boot\n");
}

static void testCrossBusGenerationGap()
{
    struct wcn_chip_entry a, b;
    assert(wcn_chip_lookup(kWcnChipWcn3990, &a) == 0);
    assert(wcn_chip_lookup(kWcnChipWcn7850, &b) == 0);
    assert(a.bus != b.bus);
    assert(a.boot != b.boot);
    assert(a.mem_base != b.mem_base);
    assert(a.pci_device != b.pci_device);
    assert(std::strcmp(a.fw_dir, b.fw_dir) != 0);
    printf("  testCrossBusGenerationGap: SNOC/ath10k/QMI vs PCIe/ath12k/MHI => no reuse\n");
}

static void testUnknownChipRefusal()
{
    struct wcn_chip_entry r;
    r.id = kWcnChipWcn7850;
    r.bus = kWcnBusPcie;
    r.boot = kWcnBootMhiBhiBhie;
    assert(wcn_chip_lookup(kWcnChipUnknown, &r) == -1);
    assert(r.id == kWcnChipUnknown);
    assert(r.bus == kWcnBusUnknown);
    assert(r.boot == kWcnBootUnknown);
    assert(wcn_chip_lookup((enum wcn_chip_id)99, &r) == -1);
    assert(wcn_chip_lookup(kWcnChipWcn3990, 0) == -1);
    printf("  testUnknownChipRefusal: unknown chip / null out refused, out zeroed\n");
}

static void testFirmwareNames()
{
    assert(std::strcmp(wcn_chip_firmware_name(0), "board-2.bin") == 0);
    assert(std::strcmp(wcn_chip_firmware_name(1), "firmware-5.bin") == 0);
    assert(std::strcmp(wcn_chip_firmware_name(2), "qcom,xo-cal-data") == 0);
    assert(wcn_chip_firmware_name(3) == 0);
    assert(wcn_chip_firmware_name(99) == 0);
    printf("  testFirmwareNames: 3 WCN3990 firmware roles (board / image / cal)\n");
}

static void testFirmwareMask()
{
    assert(wcn_chip_firmware_complete(WCN_FW_ALL) == 1);
    assert(wcn_chip_firmware_complete(0u) == 0);
    assert(wcn_chip_firmware_complete(WCN_FW_BOARD | WCN_FW_IMAGE) == 0);
    assert(wcn_chip_missing_firmware(0u) == WCN_FW_ALL);
    assert(wcn_chip_missing_firmware(WCN_FW_ALL) == 0u);
    assert(wcn_chip_missing_firmware(WCN_FW_BOARD) == (WCN_FW_IMAGE | WCN_FW_CAL));
    assert(wcn_chip_missing_firmware(WCN_FW_ALL | 0x80000000u) == 0u);
    printf("  testFirmwareMask: complete/missing masks correct, unknown bits ignored\n");
}

static void testAssessmentEnchiladaToday()
{
    /* enchilada today: no firmware, no SNOC backend, no QMI/QRTR/SCM chain,
     * no OS network ABI -> 0x1e (FIRMWARE|BACKEND|FW_BOOT|OS_ABI). */
    unsigned v = wcn_chip_assess(kWcnChipWcn3990, 0u, 0, 0, 0);
    assert(v == (WCN_BLOCK_FIRMWARE | WCN_BLOCK_BACKEND |
                 WCN_BLOCK_FW_BOOT | WCN_BLOCK_OS_ABI));
    assert((v & WCN_BLOCK_UNKNOWN_CHIP) == 0u);
    printf("  testAssessmentEnchiladaToday: verdict 0x%02x (firmware+backend+boot+abi)\n", v);
}

static void testAssessmentStructuralGate()
{
    /* Honest structural gate: even with all firmware + every boolean forced to
     * 1, a SNOC/QMI-WLFW chip stays blocked on BACKEND|FW_BOOT (sheng is
     * PCIe/MHI; the QMI/QRTR/SCM/TrustZone chain has no iOS equivalent). */
    unsigned v = wcn_chip_assess(kWcnChipWcn3990, WCN_FW_ALL, 1, 1, 1);
    assert(v == (WCN_BLOCK_BACKEND | WCN_BLOCK_FW_BOOT));
    assert((v & WCN_BLOCK_FIRMWARE) == 0u);
    assert((v & WCN_BLOCK_OS_ABI) == 0u);
    printf("  testAssessmentStructuralGate: SNOC stays 0x%02x even when forced feasible\n", v);
}

static void testAssessmentWcn7850Reference()
{
    /* WCN7850 with matching PCIe/MHI backend, MHI boot, OS ABI and all firmware
     * is the only feasible case -- exactly what sheng/SM8550 has. */
    unsigned v = wcn_chip_assess(kWcnChipWcn7850, WCN_FW_ALL, 1, 1, 1);
    assert(v == WCN_FEASIBLE);
    v = wcn_chip_assess(kWcnChipWcn7850, 0u, 1, 1, 1);
    assert((v & WCN_BLOCK_FIRMWARE) != 0u);
    assert((v & WCN_BLOCK_BACKEND) == 0u);
    v = wcn_chip_assess(kWcnChipUnknown, WCN_FW_ALL, 1, 1, 1);
    assert(v == WCN_BLOCK_UNKNOWN_CHIP);
    printf("  testAssessmentWcn7850Reference: feasible only with PCIe backend + fw + abi\n");
}

static void testDTBConstants()
{
    assert(WCN3990_MEMBASE == 0x18800000u);
    assert(WCN3990_MEMSIZE == 0x00800000u);
    assert(WCN3990_MSA_SIZE == 0x00100000u);
    assert(WCN3990_CE_IRQS == 12u);
    assert(WCN3990_REGS == 5u);
    assert(WCN7850_PCIE_VENDOR == 0x17cbu);
    assert(WCN7850_PCIE_DEVICE == 0x1107u);
    printf("  testDTBConstants: vendor DTB + official guide numeric constants\n");
}

static void testBusBootNames()
{
    assert(std::strcmp(wcn_chip_bus_name(kWcnBusSnoc), "SNOC (ath10k, membase)") == 0);
    assert(std::strcmp(wcn_chip_bus_name(kWcnBusPcie), "PCIe (ath12k endpoint)") == 0);
    assert(std::strcmp(wcn_chip_bus_name(kWcnBusUnknown), "unknown") == 0);
    assert(std::strcmp(wcn_chip_boot_name(kWcnBootQmiWlfw), "QMI/WLFW + SCM MSA (TrustZone)") == 0);
    assert(std::strcmp(wcn_chip_boot_name(kWcnBootMhiBhiBhie), "MHI BHI/BHIE over PCIe") == 0);
    printf("  testBusBootNames: human-readable bus / firmware-boot labels\n");
}

static void testSnoCHelpers()
{
    assert(wcn_chip_is_snoc_wcn3990(kWcnChipWcn3990) == 1);
    assert(wcn_chip_is_snoc_wcn3990(kWcnChipWcn7850) == 0);
    assert(wcn_chip_is_snoc_wcn3990(kWcnChipUnknown) == 0);
    assert(wcn_chip_matches_dtb_membase(0x18800000u, 0x00800000u) == 1);
    assert(wcn_chip_matches_dtb_membase(0x1c06000u, 0x1000u) == 0);
    assert(wcn_chip_matches_dtb_membase(0x18800000u, 0x1000u) == 0);
    printf("  testSnoCHelpers: DT-attested membase check (no register offset claimed)\n");
}

int main()
{
    printf("enchilada stage 6 (network) WCN3990 catalog / feasibility study:\n");
    testWcn3990Lookup();
    testWcn7850Lookup();
    testCrossBusGenerationGap();
    testUnknownChipRefusal();
    testFirmwareNames();
    testFirmwareMask();
    testAssessmentEnchiladaToday();
    testAssessmentStructuralGate();
    testAssessmentWcn7850Reference();
    testDTBConstants();
    testBusBootNames();
    testSnoCHelpers();
    printf("PASS: enchilada WCN3990 identification + feasibility study (12 test groups)\n");
    return 0;
}
