# NativeWcn — enchilada stage 6 (network) feasibility study (HOST-VERIFIED)

**This is not a Wi-Fi driver.** It is a small, freestanding, pure-C encoding of
the authoritative stage-6 hardware/driver facts plus classification and gating
functions that a future WCN3990 SNOC backend could call. It maps nothing, writes
no register, loads no firmware, joins no network, and is **not linked into the
kernel**. It exists so the stage-6 decision is recorded in tested code and can
be host-verified before any ABI-sensitive work.

## Why this module exists

`porting.toml` network area was `planned` with sheng's four PCIe/ath12k
components (`Ath12kWifi`, `WifiProbe`, `WifiNet`, `MhiBoot`). Four independent
research passes (2026-11-08) established that the full stage-6 Wi-Fi path is
**blocked on this device now**, because enchilada's WCN3990 is a different
generation AND a different bus than sheng's WCN7850:

| # | Source | Load-bearing fact |
|---|--------|-------------------|
| 1 | `https://unixnu.com/zh/porting/network` | Four components target WCN7850 on SM8550 **PCIe0**: endpoint `17cb:1107`, QMP PHY `0x1c06000`, MSI→SPI 141, **MHI** BHI/BHIE firmware boot, `sheng-wifi=5`. §1: "选择定义前，核对驱动中的端点硬件版本和固件支持". |
| 2 | vendor DTB `Mu-Silicium/Resources/DTBs/enchilada.dts` `:7908-7924` | `qcom,icnss@18800000` `compatible="qcom,icnss"`, memory-mapped `membase` `0x18800000/0x800000`, 12 CE interrupts (SPI 414..425), `qcom,wlan-msa-memory=<0x100000>`, 5 `vdd-*` supplies, `qcom,smmu-s1-bypass`. **No PCIe endpoint, no `17cb:1107`.** |
| 3 | Linux v6.12 `drivers/net/wireless/ath/ath10k/snoc.c`, `hw.h`, `qmi.c` | WCN3990 = `ATH10K_BUS_SNOC` (`of_match "qcom,wcn3990-wifi"`), 12 Copy Engines, `DMA_BIT_MASK(35)`, `msa_size 0x100000`, `WCN3990_HW_1_0_FW_DIR "ath10k/WCN3990/hw1.0"`, `board-2.bin`/`firmware-N.bin`; boot = **QRTR/WLFW QMI** + `qcom_scm_assign_mem` SCM MSA permissions (TrustZone) + SDM845-specific `msleep(20)` XPU hack; `qcom,snoc-host-cap-8bit-quirk`. **No PCIe/MHI/ath12k.** |
| 4 | sheng `package/WifiPcieTables.h` + `MhiBoot.h`, `analyze_network_local.py`, 246 decoded logs (`analyze_network_logs.py`) | sheng method is pure PCIe QMP PHY + MHI BHI/BHIE state machine; all 16 WCN3990 firmware/board/cal names **absent**; no `[network]` section / no Wi-Fi `CONFIG_*=y`; ZERO Wi-Fi-driver literals (`IO80211`/`Apple80211`/`ath10k`/`ath12k`/`WCN3990`/`wlan`/`MHI`/`QRTR`/`QMI`/`nw_wifi`/`board.bin`/`BSS`/`SSID` = 0). `WiFi`/`wifi` hits are userspace daemons (`wifid`), `scan` hits are display `[scanout prepare]`, `en0` hits are touch HID hex noise. |

**Decision:** no WCN3990 Wi-Fi path is feasible in this environment today.
Rather than copy sheng's cross-generation/cross-bus archives (which the official
guide forbids before verifying the endpoint), this module records the WCN3990
SNOC identity, the exact firmware roles, and a `wcn_chip_assess()` gate that
returns `FIRMWARE|BACKEND|FW_BOOT|OS_ABI` (`0x1e`) for enchilada and stays
`BACKEND|FW_BOOT` (`0x0c`) even when every boolean is forced to 1; feasible
(`0x00`) only for the matching PCIe/MHI WCN7850 reference case.

**Honest correction of the old record:** `porting.toml` network `inputs` /
`next_action` previously said "WCN3990 ... MHI boot path". That is wrong — MHI
is sheng's WCN7850 PCIe path; WCN3990 SNOC boots via QMI/WLFW + SCM.

## Files

- `WcnChip.h` / `WcnChip.c` — catalog + classification + gating.
- `tests/wcn-chip-test.cpp` — 12 host test groups (WCN3990/WCN7850 lookup,
  cross-bus gap, unknown-chip refusal, firmware names/masks, enchilada-today
  verdict, structural gate, WCN7850 reference feasible case, DTB constants,
  bus/boot names, SNOC helpers).
- `scripts/check-enchilada-wcn-host.py` — C11 + C++17 host runner, mirrors
  `scripts/check-enchilada-adreno-host.py` (sanitizers when available).

## Verification (HOST-VERIFIED)

```
# from oneplus6-ios/work/UniXNU-enchilada, TMP/TEMP=work/tmp/ctmp,
# PATH += oneplus6-ios/tools/llvm-mingw/bin
python scripts/check-enchilada-wcn-host.py \
    --cc oneplus6-ios/tools/llvm-mingw/bin/clang.exe \
    --cxx oneplus6-ios/tools/llvm-mingw/bin/clang++.exe \
    --out work/tmp/wcn-host-out
```

Strict compile (independent verification):

```
clang -std=c11 -Wall -Wextra -Werror -pedantic \
    -Wconversion -Wsign-conversion -Wshadow -O2 \
    -c drivers/NativeWcn/WcnChip.c -o /tmp/WcnChip.o
```

Both must pass. This is a **host** claim only: no CI compile, no flash, no
device verification. `porting.toml` network area is `host-verified` with a
`host` evidence entry; `evidence/stage6-network-feasibility.md` records the
audit.

## Proves / Does NOT prove

- **Proves:** WCN3990 (ath10k SNOC / QMI-WLFW / SCM MSA) and WCN7850 (ath12k
  PCIe / MHI) differ in bus, generation, firmware and boot chain; sheng's four
  components cannot be reused; the three WCN3990 firmware roles are absent;
  246 logs contain no Wi-Fi driver; the catalog + gate encode this faithfully
  (12 host tests, strict compile).
- **Does NOT prove:** any SNOC register value read on the device, any firmware
  load, QMI/WLFW service discovery, SCM/TrustZone MSA assignment, CE ring
  bring-up, scan/association, or any OS network-ABI behaviour. No CI compile,
  no flash, no device readback. None of the four sheng network components are
  implemented.

## Next action (stage 6)

To make stage 6 device-verifiable one would first need: (a) the WCN3990
`board-2.bin` (BDF), `firmware-N.bin` and `qcom,xo-cal-data` blobs from a
matching firmware set; (b) a new **ath10k SNOC** host backend (membase map +
12 Copy Engine rings + 5 regulators + MSA reserved memory) — not a port of
sheng's PCIe/MHI; (c) an OS equivalent of the QRTR/WLFW QMI + SCM/TrustZone
MSA-permission boot chain (including the SDM845 20 ms XPU delay); and (d) a
selected OS `IO80211`/`Apple80211`/Skywalk network ABI. Only then could a
read-only membase probe be flashed, followed by QMI firmware bring-up, a BSS
scan, association and sustained traffic. Until (a)–(d) exist, stage 6 stays
`host-verified` and blocked on firmware + backend + firmware-boot chain + OS
ABI; strict 8-stage order then continues with audio / cellular / the remaining
areas.
