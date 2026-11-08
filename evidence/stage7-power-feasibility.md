# Stage 7 (power/battery) — HOST-VERIFIED ONLY

**OnePlus 6 / enchilada / SDM845**, iPadOS 17.7.11 (21H461, iPad7,1).
Strict unixnu.com 8-stage order, stage 7 = DSP / audio / battery / cpufreq /
power. Date 2026-11-08. Parent commit (revision of the host evidence):
`d8e3e3c85516ec818c6e9ba05d12beeff5b7f5a5`.

**NO CI, NO FLASH, NO DEVICE VERIFICATION.** This is an honest host feasibility
record. `power` changes from `implemented` to `host-verified` only because a
host evidence entry now exists; the decoder is NOT linked into the kernel and
never touched the device.

## Why this test exists

After stage 3 (display), stage 4 (input), stage 5 (GPU) and stage 6 (network)
were closed, the strict order reaches stage 7. Two `porting.toml` areas are
relevant: `audio` (`planned`, `AdspProbe`/`AudioProbe`) and `power`
(`implemented`, `CpuFreqProbe`/`ShengPlatformPower`/`ShengBattery`). The latter
has a long-standing `next_action`: *"implement an I2C bq27441 fuel-gauge reader
(Standard Commands: StateOfCharge, Voltage, Temperature) and publish
AppleSmartBattery-style properties"*. Per m12247 ("先核验再上机") and m03546
("每做一步之前进行两次验证"), broad research + two independent verifications
were done before any code and no flashing was performed.

## Four independent research passes

| Pass | Source | Decisive fact |
| --- | --- | --- |
| 1 — Official | `https://unixnu.com/zh/porting/power` | "直接连接 I2C 电量计的板型实现该芯片的寄存器协议，使用 DSP 电池管理器的板型实现其消息协议。先确定传输方式，再选择参考代码。" Transport / device protocol / OS service must be implemented separately; a directly-connected I2C gauge uses the chip's register protocol, PMIC GLINK battmgr uses a message protocol. Power-source measurement fields = present / external / charging / capacity / voltage / current / temperature / time, with explicit units and separate valid/missing state. |
| 2 — Linux v6.12 | `drivers/power/supply/bq27xxx_battery.c` + `bq27xxx_battery_i2c.c` | `bq27xxx_battery_i2c_read()` = write 1 register byte, repeated START, read 2 bytes LE (`get_unaligned_le16`); `bq27421_regs` (SOC 0x1c, VOLT 0x04, TEMP 0x02, AI 0x10, FCC 0x0e, DCAP 0x3c) vs `bq27541_regs` (SOC 0x2c, VOLT 0x08, TEMP 0x06, AI 0x14, FCC 0x12, DCAP 0x3c); TEMP 0.1 K − 2731 → 0.1 °C; AI signed 16-bit mA; VOLT mV; 60 s poll. |
| 3 — Hardware (two independent sources) | vendor DTB `enchilada.dts:4213-4218` + mainline Linux v6.12 `sdm845-oneplus-common.dtsi:361-370` | **Model conflict, honestly unresolved**: vendor says `bq27541-battery@55` `ti,bq27541-battery` on `i2c@a80000` (qupv3_se8); mainline says `&i2c10` `bq27441-battery@55` `ti,bq27411`. The strings select DIFFERENT register maps. Linux OF table has `ti,bq27541`/`ti,bq27411`/`ti,bq27441` but no `-battery` suffix. `porting.toml` historically said "bq27441 on i2c10". Both maps implemented + tested; no single part number asserted. |
| 4 — sheng method + logs + inventory | `src/UniXNU-sheng-main` `AdspProbe.cpp`, `ShengAdsp.h`, `ShengBattmgr.h`, `ShengBattery.cpp`; 246 decoded logs `work/log/**/{latest,msgbuf}.txt`; `configs/enchilada_defconfig`, `board.toml`, `glob` | sheng = SM8550 `qcom,sm8550-adsp-pas` + AudioReach/GPR + SoundWire WCD938x + PMIC GLINK battmgr (owner 32778, `PMIC_RTR_ADSP_APPS`), all different addresses/generation. 246 logs have ZERO audio/DSP/battery driver literals (IOAudio, CoreAudio, AudioReach, GPR, q6apm, GLINK, WCD9340, tavil, SLIMBUS, MAX98927, bq27441, AppleSmartBattery, IOPMPowerSource, sheng-adsp/audio/battery, `[audio`/`[mic`/`[adsp`/`[battery` all 0). No audio CONFIG, no `[audio]` board section, no real `.mbn` firmware blobs, zero device-repo audio files. |

### Hardware inventory (vendor DTB, stage-7 audio side, for completeness)

`max98927@3a` (`:3790`, `maxim,max98927L`); `qcom,mss@4080000` (`:6444`,
`pil-q6v55-mss`, firmware `"modem"`); `qcom,lpass@17300000` (`:6488`,
`pil-tz-generic`, pas-id 1, firmware `"adsp"`); `slim@171c0000` (`:6547`,
`qcom,slim-ngd`); `tavil_codec` (`:6570`, `qcom,tavil-slim-pgd` = WCD9340,
micbias 1800/2700/1800/1800 mV, mclk 9.6 MHz); `wsa881x` speakers (`:6789`);
`qcom,glink-smem-native-xprt-adsp@86000000` label `"lpass"` (`:7390`);
Linux v6.12 `sound/soc/qcom/sdm845.c` `QUATERNARY_MI2S_RX` bit clock 1.536 MHz,
48 kHz / 2 ch / S16_LE.

## Key finding

The bq27xxx fuel gauge is the **ONE** stage-7 sub-item that needs no proprietary
firmware, no DSP, and no closed OS ABI: it is a plain I2C register protocol over
the **device-verified** `drivers/NativeGeniI2c/GeniI2c.c` transport (used by the
stage-4 RMI4 touch). One gauge read is
`geni_i2c_write_read(g, 0x55, &reg, 1, rx2, 2)`. Everything else — ADSP PIL
`.mbn` firmware, SLIMBUS WCD9340 codec, Q6AFE/ASM routing, CoreAudio HAL, PMIC
GLINK battery manager, RPMh voting — stays BLOCKED on this host.

## Deliverable (all pure C, HOST-VERIFIED)

`drivers/NativeBattery/`: `BqFuelGauge.h` / `BqFuelGauge.c`
(`bq_gauge_le16`, `bq_gauge_temperature_dc`, `bq_gauge_current_ma`,
`bq_gauge_reg`, `bq_gauge_apply`, `bq_gauge_assess`, `bq_stage7_assess`,
chip/map names), `tests/bq-fuel-gauge-test.cpp` (11 groups),
`scripts/check-enchilada-battery-host.py`, `README.md`.

## Two independent host verifications (both PASS)

1. **Host runner** `python scripts/check-enchilada-battery-host.py
   --cc .../clang.exe --cxx .../clang++.exe --out work/tmp/battery-host-out`
   → 11 test groups all PASS, `PASS: enchilada stage-7 bq27xxx fuel-gauge host
   tests (without sanitizers)`, `HOST_TEST_EXIT=0`.
2. **Strict compile + independent dump** `clang -std=c11 -Wall -Wextra -Werror
   -pedantic -Wconversion -Wsign-conversion -Wshadow -O2 -c BqFuelGauge.c`
   → exit 0, plus a separate `bq-dump.c` build/run →
   `le16(34,12)=1234`, `temp(2981)=250`, `cur(-400)=-400`,
   `reg bq27411 SOC=1c VOLT=04 TEMP=02 FCC=0e DCAP=3c`,
   `reg bq27541 SOC=2c VOLT=08 TEMP=06 FCC=12 DCAP=3c`,
   `assess gauge(bq27411,ok)=00 gauge(bq27541,ok)=00 gauge(unknown,ok)=01`,
   `stage7 today(...)=1c full(...)=00`, `DUMP_RUN_EXIT=0`.

## Decision (load-bearing)

- `bq_gauge_assess(chip, transport)` → `BQ_FEASIBLE_GAUGE (0x00)` for either
  known chip with GENI I2C linked + ACK at 0x55. Firmware/DSP/OS-ABI are
  deliberately NOT inputs: the plain-I2C gauge does not need them.
- `bq_stage7_assess(chip, transport, fw=0, abi=0, dsp=0)` → `0x1c`
  (`FIRMWARE | OS_ABI | DSP_AUDIO`) on this device today; the full reference
  stack returns `0x00`.
- Full audio / DSP / GLINK battery / RPMh cpufreq path is **BLOCKED**: ADSP
  and modem `.mbn` firmware blobs are absent, sheng is a different SoC
  generation, iOS CoreAudio / IOPMPowerSource ABI is closed-source and this
  host has no macOS/Xcode (CI is the only ABI authority), 246 logs show zero
  driver literals.

## Honest corrections

- The old `power.inputs` "bq27441 fuel gauge on i2c10 @0x55" is kept but marked
  `[HISTORICAL, model conflict]`: the vendor DTB says `bq27541` on
  `i2c@a80000`. Both register maps are implemented; a real driver must read
  `CONTROL`/`DEVICE_TYPE` to disambiguate. No part number is asserted.
- `audio.inputs` "ADSP/SLPI firmware oneplus6/*.mbn" is kept but marked
  `[HISTORICAL, unverified]`: no such blobs exist in the workspace; the vendor
  DTB names `"adsp"` and `"modem"` via `qcom,firmware-name`.
- Old stage-4/stage-5 "CoreImage GLContext crash" remains the external
  diag2-k158 inference with ZERO literal evidence in 246 logs (not repeated
  here as a device fact).

## Proves / Does NOT prove

**Proves:** LE 16-bit I2C decode; 0.1 K→0.1 °C, signed mA, mV, mAh, % field
decoding; both bq27xxx register-map families; the valid/missing-state rule on
failed reads; the honest feasible-gauge vs blocked-DSP-audio verdict. All in
tested pure C11, twice independently verified.

**Does NOT prove:** any real I2C read, chip identity, battery present, a
power-source service, charger state, audio playback/capture, DSP boot, cpufreq,
CI compilation, a kernel link, or anything on the device.

## Next action (only if unblocking stage 7)

Link the decoder to `NativeGeniI2c`, read `CONTROL`/`DEVICE_TYPE` at 0x55 to
fix the chip family, poll the 7 standard commands, publish
`IOPMPowerSource`-style properties with explicit units, then CI-compile and
flash a read-only gauge probe with two independent device verifications. Full
audio additionally needs the ADSP/modem `.mbn` firmware set, a new SLIMBUS
WCD9340 + Q6AFE backend, and a selected CoreAudio OS ABI. After that, the
strict order continues with `memory` (ArmSmmu4K), `cellular` (QcomBaseband)
and `diagnostics`.
