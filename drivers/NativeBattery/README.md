# Stage 7 — bq27xxx fuel-gauge decoder (HOST-VERIFIED feasibility study)

OnePlus 6 / enchilada / SDM845, iPadOS 17.7.11 (21H461). Strict unixnu.com
8-stage driver order, stage 7 = DSP / audio / battery / cpufreq / power.

**This is a decision record and a byte-exact decoder, NOT a device driver.** It
is not linked into the kernel, maps no register, reads no I2C bus, loads no
firmware, publishes no `IOPMPowerSource`, and is **not device-verified**.

## Why this module exists

Stage 7 splits into two very different halves:

| Item | enchilada (SDM845) hardware | sheng (SM8550) reference | Verdict here |
| --- | --- | --- | --- |
| DSP / audio | `pil-tz-generic` LPASS `"adsp"`, `pil-q6v55-mss` `"modem"`, `qcom,slim-ngd` SLIMBUS, `qcom,tavil-slim-pgd` WCD9340, WSA881x, `maxim,max98927L`, Q6AFE/ASM, GLINK/SMEM/SMP2P | `qcom,sm8550-adsp-pas`, AudioReach/GPR, SoundWire WCD938x, CoreAudio HAL | **BLOCKED** — cross-generation, needs proprietary `.mbn` firmware + closed OS audio ABI |
| Battery | plain I2C `bq27xxx` fuel gauge @0x55 | PMIC GLINK `battmgr` message protocol | **the gauge decoder is FEASIBLE** — plain I2C, no firmware/DSP |
| cpufreq | 8 CPU clocks, RPMh | EPSS LUT / RPMh | `CpuFreqProbe`/`ShengPlatformPower` already `implemented` |

The official power guide (§1) says it verbatim: *"直接连接 I2C 电量计的板型
实现该芯片的寄存器协议，使用 DSP 电池管理器的板型实现其消息协议。先确定传输
方式，再选择参考代码。"* A directly-connected I2C gauge implements the chip's
register protocol; the PMIC/DSP battery manager is a different transport. So
the bq27xxx gauge is the ONE stage-7 sub-item that can be honestly advanced on
this host without firmware, a DSP, or a closed OS ABI.

The transport is already **device-verified**: `drivers/NativeGeniI2c/GeniI2c.c`
brought up the stage-4 RMI4 touch. A gauge read is just
`geni_i2c_write_read(g, 0x55, &reg, 1, rx2, 2)` (write register byte, repeated
START, read 2 bytes LE). No new hardware path is needed.

## Independent research (4 passes, done BEFORE any code)

1. **Official** `https://unixnu.com/zh/porting/power` — transport/protocol/OS
   service separation; power-source measurement fields + units; `IOPMPowerSource`
   + valid/missing-state rule.
2. **Linux v6.12** `drivers/power/supply/bq27xxx_battery.c` +
   `bq27xxx_battery_i2c.c` — two register maps, LE 16-bit decode, 0.1 K
   temperature, signed mA current, 60 s poll.
3. **Vendor DTB** `Mu-Silicium/Resources/DTBs/enchilada.dts` `:4213` and
   **mainline Linux v6.12** `sdm845-oneplus-common.dtsi` `:361-370` — see the
   *Model conflict* below.
4. **246 decoded device logs** (`work/log/**/{latest,msgbuf}.txt`) — ZERO
   audio/DSP/battery driver literals; sheng `AdspProbe.cpp` /
   `ShengBattmgr.h` are SM8550 GLINK/AudioReach with different addresses.

## Model conflict (honest, unresolved — both maps implemented)

Two independent sources disagree about which bq27xxx chip is on the board:

- Vendor DTB `enchilada.dts:4213-4218`: `bq27541-battery@55`,
  `compatible = "ti,bq27541-battery"`, on `i2c@a80000` (qupv3_se8).
- Mainline Linux v6.12 `sdm845-oneplus-common.dtsi:361-370`: `&i2c10`,
  `bq27441-battery@55`, `compatible = "ti,bq27411"`.

The two strings select **different register maps** in Linux
(`bq27541_regs` SOC `0x2c` vs `bq27421_regs` SOC `0x1c`). Linux's OF table has
`ti,bq27541` / `ti,bq27411` / `ti,bq27441` but **no** `-battery` suffix, so the
vendor string is a downstream variant. `porting.toml` historically says
"bq27441 on i2c10" (mainline). This module therefore implements and tests BOTH
families and does **not** assert a single part number. A real driver must read
`CONTROL`/`DEVICE_TYPE` to disambiguate at runtime.

## Decision

- `bq_gauge_assess(chip, transport)` → `BQ_FEASIBLE_GAUGE (0x00)` for either
  known chip with GENI I2C linked + ACK at 0x55. Firmware/DSP/OS-ABI are
  deliberately **not** inputs: the plain-I2C gauge does not need them.
- `bq_stage7_assess(...)` adds `BQ_BLOCK_FIRMWARE | BQ_BLOCK_OS_ABI |
  BQ_BLOCK_DSP_AUDIO` when the DSP/audio/GLINK stack is unavailable (the truth
  on this device today → `0x1c`).
- Everything else in stage 7 (ADSP PIL, SLIMBUS WCD9340 codec, Q6AFE routing,
  CoreAudio HAL, PMIC GLINK battery manager, RPMh voting) stays **BLOCKED**.

## Proves / Does NOT prove

**Proves:** LE 16-bit I2C decode; 0.1 K→0.1 °C, signed mA, mV, mAh, % field
decoding; both register maps; the valid/missing-state rule on failed reads;
the honest feasible-gauge vs blocked-DSP-audio verdict. All in tested pure C11.

**Does NOT prove:** any real I2C read, chip identity, battery present, a
power-source service, charger state, audio playback/capture, DSP boot, cpufreq,
CI compilation, or anything on the device.

## Two independent host verifications

```
python scripts/check-enchilada-battery-host.py \
  --cc oneplus6-ios/tools/llvm-mingw/bin/clang.exe \
  --cxx oneplus6-ios/tools/llvm-mingw/bin/clang++.exe \
  --out work/tmp/battery-host-out
clang -std=c11 -Wall -Wextra -Werror -pedantic -Wconversion \
  -Wsign-conversion -Wshadow -O2 -c BqFuelGauge.c   # + independent dump
```

## Next action (only if unblocking stage 7)

Link this decoder to the device-verified `NativeGeniI2c` transport, read
`CONTROL`/`DEVICE_TYPE` at 0x55 to fix the chip family, poll the 7 standard
commands, publish `IOPMPowerSource`-style properties with explicit units, then
CI-compile and flash a read-only gauge probe with two independent device
verifications. Full audio additionally needs the ADSP/`.mbn` firmware set, a
new SLIMBUS WCD9340 + Q6AFE backend, and a selected CoreAudio OS ABI.
