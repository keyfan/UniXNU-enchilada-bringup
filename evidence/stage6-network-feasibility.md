# Stage 6 (network / Wi-Fi) host evidence: WCN3990 identification + feasibility audit

**Status: HOST-VERIFIED only. No CI compile, no flash, no device verification,
no firmware load, no register access, no IO80211/Apple80211 ABI work, no scan
or association.**

- Date: 2026-11-08
- Device: OnePlus 6 (enchilada, SDM845), iPadOS 17.7.11 (21H461, iPad7,1)
- Record parent commit at time of writing: `9c6904c1d3ad855c392780a6c933fa18ff079a6e`
- Module: `drivers/NativeWcn/` (`WcnChip.h/.c`, `tests/wcn-chip-test.cpp`,
  `README.md`) + `scripts/check-enchilada-wcn-host.py`
- Host runner command (verification 1):
  `python scripts/check-enchilada-wcn-host.py --cc oneplus6-ios/tools/llvm-mingw/bin/clang.exe --cxx oneplus6-ios/tools/llvm-mingw/bin/clang++.exe --out work/tmp/wcn-host-out`
- Independent verification 2 (strict compile + byte/verdict dump):
  `clang -std=c11 -Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion -Wshadow -O2 -c drivers/NativeWcn/WcnChip.c` → exit 0;
  linked dump prints the WCN3990 SNOC / WCN7850 PCIe entries and
  `assess(WCN3990,fw=0,b=0,boot=0,abi=0)=1e`,
  `assess(WCN3990,fw=ALL,b=1,boot=1,abi=1)=0c` (structural gate),
  `assess(WCN7850,fw=ALL,b=1,boot=1,abi=1)=00` (feasible reference).

## Why this audit was done

`porting.toml` network area was `planned`, listing sheng's four A740-era
components (`Ath12kWifi`, `WifiProbe`, `WifiNet`, `MhiBoot`) and an old
`next_action` that asked to "select the WCN3990 firmware set (board data +
ath10k/WCN3990 MHI)". Per the standing constraints ("每步两次验证、先核验再上机"),
four independent research passes were completed **before** any code or flash.
The old `inputs`/`next_action` claim that WCN3990 uses an "MHI boot path" is
**incorrect** and is corrected below.

## Four independent research passes

| Pass | Source | Key finding |
|------|--------|-------------|
| 1 | Official guide `https://unixnu.com/zh/porting/network` | The four components target **WCN7850 on SM8550 PCIe0**: endpoint `17cb:1107`, QMP PHY `0x1c06000` (Gen2 x1), MSI→SPI 141, six PMU power votes, **MHI** BHI/BHIE firmware boot, `sheng-wifi=5` / `sheng-wifi-aj=0`; callbacks `lx_platform`/`nw_board`/`nw_wifi_start/step/stop`; §1 逐字「选择定义前，核对驱动中的端点硬件版本和固件支持」. WCN7850 reference table notes firmware `c7-00108` works, `c2-00259` stalls, MAC zero → generated from chip id. |
| 2 | vendor DTB `Mu-Silicium/Resources/DTBs/enchilada.dts` `:7908-7924` | `qcom,icnss@18800000` `compatible="qcom,icnss"`, `reg=<0x18800000 0x800000 0xa0000000 0x10000000 0xb0000000 0x10000>`, `reg-names="membase\0smmu_iova_base\0smmu_iova_ipa"`, `iommus=<0x29 0x40 0x01>`, 12 CE interrupts SPI 414..425, `qcom,wlan-msa-memory=<0x100000>`, 5 `vdd-*-supply` (`0.8-cx-mx/1.8-xo/1.3-rfa/3.3-ch0`), `qcom,smmu-s1-bypass`. **Memory-mapped SNOC platform device, NOT a PCIe endpoint.** `wcn3990` at `:6850` is `qcom,btfmslim_slave` (Bluetooth SLIMbus) and `bt_wcn3990` at `:25057` is `qca,wcn3990` with 5 BT supplies — neither is the Wi-Fi MAC. |
| 3 | Linux v6.12 `drivers/net/wireless/ath/ath10k/snoc.c`, `hw.h`, `qmi.c` | `ath10k_snoc_dt_match[] = { { .compatible = "qcom,wcn3990-wifi", .data = &drv_priv } }`, `drv_priv.hw_rev = ATH10K_HW_WCN3990`, `.dma_mask = DMA_BIT_MASK(35)`, `.msa_size = 0x100000`; 5 `ath10k_regulators[]`; 12 `WLAN_CE_0..11`; `ath10k_core_create(..., ATH10K_BUS_SNOC, ...)`; `WCN3990_HW_1_0_FW_DIR = "ath10k/WCN3990/hw1.0"`, `board-2.bin`, `firmware-N.bin`; `ath10k_snoc_hif_power_up()` = power on → `ath10k_qmi_wlan_enable()` → CE RRI → init pipes → enable interrupts; `qmi.c` boot chain = QRTR/WLFW QMI service lookup → ind_register → host_cap (8bit quirk) → msa_info → **`msleep(20)` SDM845 XPU hack** → `qcom_scm_assign_mem()` MSA permissions (TrustZone) → msa_ready → cap → BDF download → cal report → WLAN mode. |
| 4 | sheng `package/WifiPcieTables.h` + `MhiBoot.h`, workspace inventory, 246 logs | sheng method = pure PCIe QMP PHY init tables + MHI BHI/BHIE state machine (`MhiStateReset/Ready/M0/Bhi/SysErr`, `BHI_IMGADDR_*`, `BHIE_TXVEC*`) — unusable for SNOC. `analyze_network_local.py`: `defconfig_has_wifi_y=false`, `board_has_network_section=false`, all 16 WCN3990 firmware/board/cal names **absent** (`board.bin, board-2.bin, firmware-2..6.bin, wlanmdsp.mbn, mba.b00, Data.msc, wlfw, bdwlan.bin, otp.bin, utf.bin, qwlan30.bin, qwlan.bin`), `enchilada_wifi_named_files=[]`, sheng wifi method files = 8 (`MhiBoot.c/h`, `WifiNet.cpp`, `WifiPcieTables.h`, `WifiProbe.cpp`, `tests/mhi_boot_host.c`, `build-ath12k-wifi.py`, `check-mhi-boot-host.py`). `analyze_network_logs.py` over 246 files: ZERO `IO80211`/`Apple80211`/`IO80211Controller`/`IOSkywalk`/`AirPort`/`ath10k`/`ath12k`/`WCN3990`/`WCN7850`/`17cb:1107`/`wlan`/`WLAN`/`MHI`/`QRTR`/`wlfw`/`QMI`/`nw_wifi`/`sheng-wifi`/`board.bin`/`BSS`/`SSID`/`association`/`IOEthernet`/`IONetworking` = 0. Non-zero `WiFi` 291/116 files (`wifid`, `WiFiCloudAssetsXPCService` userspace daemons), `wifi` 305/117, `scan` 196/125 (`[scanout prepare]` display), `en0` 79/74 (touch HID hex) are NOT driver evidence. |

## Honest corrections of prior findings

1. **"WCN3990 ... MHI boot path" is wrong.** `porting.toml` network `inputs`
   (`MHI boot path`) and `next_action` (`ath10k/WCN3990 MHI`) mixed sheng's
   WCN7850 PCIe MHI path into the enchilada WCN3990 description. Linux v6.12
   `ath10k/snoc.c` + `qmi.c` show WCN3990 = SNOC + QMI/WLFW + SCM MSA; MHI
   belongs only to sheng's PCIe endpoint. Corrected in this record and in the
   module.
2. The old finding "sheng's Ath12kWifi is WCN785x / SM8550; enchilada's
   WCN3990 is a different generation. The 8-stage order places Wi-Fi after
   GPU, so this work is gated on stage 5 anyway." is retained as HISTORICAL;
   stage 5 is now a completed (host-verified, blocked) audit, so the gate is
   satisfied. The real blocker is the bus/generation/firmware/OS-ABI gap.

## Decision (load-bearing)

The full stage-6 Wi-Fi path is **BLOCKED on this device now**:

1. WCN3990 (ath10k **SNOC**: membase `0x18800000`, 12 CE, QMI/WLFW firmware,
   SCM/TrustZone MSA) and WCN7850 (ath12k **PCIe0**: endpoint `17cb:1107`,
   MHI BHI/BHIE) differ in bus, generation, firmware and boot chain → sheng's
   four components (`Ath12kWifi`, `WifiProbe`, `WifiNet`, `MhiBoot`) cannot be
   reused; the official guide requires verifying the endpoint hardware version
   and firmware support before choosing definitions.
2. The WCN3990 firmware/board/cal blobs (`board-2.bin`, `firmware-N.bin`,
   `qcom,xo-cal-data`) are absent from the workspace.
3. The QRTR/WLFW QMI + `qcom_scm_assign_mem` SCM/TrustZone MSA-permission
   firmware-boot chain (with the SDM845-specific 20 ms XPU delay) has no iOS
   equivalent here.
4. iOS `IO80211`/`Apple80211`/Skywalk ABI is closed-source; this host has no
   macOS/Xcode (CI is the only ABI authority, cf. CI #207 / #211 lessons).
5. 246 decoded device logs confirm no Wi-Fi driver is present today.

Instead of copying cross-generation code, `drivers/NativeWcn` records the
WCN3990 SNOC identity, the exact firmware roles, and a `wcn_chip_assess()`
gate: enchilada returns `WCN_BLOCK_FIRMWARE | WCN_BLOCK_BACKEND |
WCN_BLOCK_FW_BOOT | WCN_BLOCK_OS_ABI` (`0x1e`), stays `BACKEND|FW_BOOT`
(`0x0c`) even with all firmware and every boolean forced to 1 (honest
structural gate), and `WCN_FEASIBLE` (`0x00`) only for the matching PCIe/MHI
WCN7850 reference case.

## Two verifications (both PASS)

1. `check-enchilada-wcn-host.py` → `PASS: enchilada WCN3990 identification +
   feasibility study (12 test groups)` + `PASS: enchilada WCN3990 catalog host
   tests (without sanitizers)`, `HOST_TEST_EXIT=0`.
2. Strict `-Wconversion -Wsign-conversion -Wshadow -pedantic` C11 compile
   `STRICT_C_EXIT=0` + independent `wcn-dump` byte/verdict dump
   (`WCN3990 membase=18800000 msa=00100000 ce=12 regs=5 boot=1`,
   `WCN7850 pci=17cb:1107 boot=2`, `assess=1e/0c/00`), `DUMP_RUN_EXIT=0`.

## Proves / Does NOT prove

- **Proves:** WCN3990 and WCN7850 are different buses AND generations with
  different firmware and boot chains; sheng's four PCIe/MHI components cannot
  be reused; the three WCN3990 firmware roles are absent; under the official
  guide the endpoint must be verified first; the catalog + gate encode this
  faithfully (12 host tests, strict compile).
- **Does NOT prove:** any SNOC register value read on the device, any firmware
  load, QMI/WLFW service discovery, SCM/TrustZone MSA assignment, CE ring
  bring-up, BSS scan, association, address configuration, or sustained
  traffic. No CI compile, no flash, no device readback. None of the four sheng
  network components are implemented; `status` is `host-verified`, not
  `device-verified`.

## Next action (stage 6)

To become device-verifiable, stage 6 first needs: (a) the WCN3990 firmware set
(`board-2.bin` BDF, `firmware-N.bin`, `qcom,xo-cal-data`) from a matching
firmware set; (b) a new **ath10k SNOC** host backend (membase map + 12 Copy
Engine rings + 5 regulators + MSA reserved memory + `qcom,smmu-s1-bypass`
handling), not a port of sheng's A740/PCIe work; (c) an OS equivalent of the
QRTR/WLFW QMI + SCM/TrustZone MSA-permission boot chain (incl. the SDM845
20 ms XPU delay); (d) a selected OS `IO80211`/`Apple80211`/Skywalk network ABI.
Only then could a read-only membase probe be flashed, followed by QMI firmware
bring-up, a BSS scan, association and sustained traffic — each step with CI +
two independent device verifications. Until (a)–(d) exist, stage 6 stays
`host-verified` and blocked; strict 8-stage order then continues with audio /
cellular / memory / power.
