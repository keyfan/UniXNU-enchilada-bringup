// SPDX-License-Identifier: BSD-2-Clause-Patent
// Stage 7 (DSP/audio/battery/cpufreq/power) HOST-VERIFIED feasibility study for
// the OnePlus 6 (enchilada, SDM845). Pure C; no XNU/IOKit ABI, no register I/O,
// no firmware. This is a decision record and a byte-exact I2C register decoder,
// NOT a device driver and NOT device-verified.
//
// Independent research sources (four passes):
//  (1) Official guide https://unixnu.com/zh/porting/power : implement transport,
//      device protocol and OS service SEPARATELY; "直接连接 I2C 电量计的板型实现
//      该芯片的寄存器协议" (a board with a directly-connected I2C fuel gauge
//      implements that chip's register protocol); PMIC GLINK battmgr is only
//      the SM8550/sheng variant. Results: measurement decoder + power-source
//      service with explicit units.
//  (2) Vendor DTB oneplus6-ios/work/Mu-Silicium/Resources/DTBs/enchilada.dts:
//      :3790 max98927@3a (compatible "maxim,max98927L"), :4213 bq27541-battery@55
//      (compatible "ti,bq27541-battery", reg <0x55>, on i2c@a80000 = qupv3_se8),
//      :6444 qcom,mss@4080000 (pil-q6v55-mss, firmware "modem"), :6488
//      qcom,lpass@17300000 (pil-tz-generic, firmware "adsp", pas-id 1), :6547
//      slim@171c0000 (qcom,slim-ngd), :6570 tavil_codec (qcom,tavil-slim-pgd
//      = WCD9340), :6789 wsa881x speakers, :7390 glink-smem-native-xprt-adsp
//      label "lpass".
//  (3) Linux v6.12 drivers/power/supply/bq27xxx_battery.c + _i2c.c:
//      bq27xxx_battery_i2c_read() = write reg byte, repeated START, read 2 bytes
//      little-endian (get_unaligned_le16) or 1 byte for "single"; register maps
//      bq27421_regs (alias bq27411/bq27425/bq27426/bq27441/bq27621) vs
//      bq27541_regs (alias bq27542/bq27546/bq27742). Voltage is mV, temperature
//      is 0.1 K (subtract 2731 -> 0.1 degC), current is signed mA, SOC is %.
//  (4) device logs: oneplus6-ios/work/log/**/{latest,msgbuf}.txt (246 files)
//      contain ZERO audio/DSP/battery driver literals (IOAudio, CoreAudio,
//      AudioReach, GPR, q6apm, GLINK, WCD9340, tavil, SLIMBUS, MAX98927,
//      bq27441, AppleSmartBattery, IOPMPowerSource, ... all 0 hits); sheng's
//      AdspProbe.cpp / AudioProbe.cpp / ShengBattmgr.c are SM8550 AudioReach
//      + PMIC GLINK, a different generation/bus. See
//      evidence/stage7-power-feasibility.md.
//
// LOAD-BEARING CONCLUSION: the bq27xxx fuel gauge is the ONE stage-7 item that
// needs no proprietary firmware, no DSP, and no closed OS ABI to decode: it is
// a plain I2C register protocol over the DEVICE-VERIFIED GENI I2C transport
// (drivers/NativeGeniI2c/GeniI2c.c, used by stage 4 touch). Full audio (ADSP
// PIL + SLIMBUS WCD9340 + Q6AFE/ASM + CoreAudio HAL), PMIC GLINK battery, and
// RPMh cpufreq remain BLOCKED here. This module proves the pure-C decode + a
// transport-scoped feasibility gate; it does NOT touch hardware.
#ifndef BQ_FUEL_GAUGE_H
#define BQ_FUEL_GAUGE_H
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Chip register-map families from Linux v6.12 bq27xxx_battery.c. */
enum bq_fuel_chip {
    kBqChipUnknown = 0,
    kBqChipBq27411 = 1,   /* mainline OnePlus 6/6T: "ti,bq27411" @0x55 */
    kBqChipBq27541 = 2,   /* vendor DTB enchilada.dts:4215: "ti,bq27541-battery" */
};

/* Stage-7 decision gate bits (mirrors the stage-5/6 catalog shape). */
#define BQ_BLOCK_UNKNOWN_CHIP 0x01u
#define BQ_BLOCK_NO_I2C_TRANSPORT 0x02u
#define BQ_BLOCK_FIRMWARE 0x04u
#define BQ_BLOCK_OS_ABI 0x08u
#define BQ_BLOCK_DSP_AUDIO 0x10u
#define BQ_FEASIBLE_GAUGE 0x00u

/* I2C transport capability flags supplied by the board owner. */
struct bq_gauge_transport {
    int geni_i2c_available;     /* GENI I2C master (drivers/NativeGeniI2c) linked */
    int addr_0x55_acked;        /* a probe read got an ACK from slave 0x55 */
    int chip_id_confirmed;      /* CONTROL/DEVICE_TYPE matched a known bq27xxx */
};

/* Standard Commands (bq27421_regs / bq27441_regs alias) — Linux v6.12. */
#define BQ274XX_REG_CTRL     0x00u
#define BQ274XX_REG_TEMP     0x02u
#define BQ274XX_REG_VOLT     0x04u
#define BQ274XX_REG_FLAGS    0x06u
#define BQ274XX_REG_NAC      0x08u
#define BQ274XX_REG_RC       0x0cu
#define BQ274XX_REG_FCC      0x0eu
#define BQ274XX_REG_AI       0x10u
#define BQ274XX_REG_AP       0x18u
#define BQ274XX_REG_SOC      0x1cu
#define BQ274XX_REG_INT_TEMP 0x1eu
#define BQ274XX_REG_DCAP     0x3cu

/* Standard Commands (bq27541_regs) — Linux v6.12. NOTE the different offsets:
   SOC 0x2c (not 0x1c), VOLT 0x08 (not 0x04), TEMP 0x06 (not 0x02),
   AI 0x14 (not 0x10), FCC 0x12 (not 0x0e). Only CTRL and DCAP coincide. */
#define BQ27541_REG_CTRL     0x00u
#define BQ27541_REG_TEMP     0x06u
#define BQ27541_REG_VOLT     0x08u
#define BQ27541_REG_FLAGS    0x0au
#define BQ27541_REG_NAC      0x0cu
#define BQ27541_REG_RC       0x10u
#define BQ27541_REG_FCC      0x12u
#define BQ27541_REG_TTE      0x16u
#define BQ27541_REG_AI       0x14u
#define BQ27541_REG_AE       0x22u
#define BQ27541_REG_AP       0x24u
#define BQ27541_REG_SOC      0x2cu
#define BQ27541_REG_CYCT     0x2au
#define BQ27541_REG_INT_TEMP 0x28u
#define BQ27541_REG_DCAP     0x3cu

/* The 7 fields the official power guide asks a power source to publish. */
struct bq_gauge_snapshot {
    int present;
    int percent;            /* StateOfCharge, 0..100 */
    int voltage_mv;         /* VOLT, mV */
    int current_ma;         /* average current, signed mA */
    int temperature_dc;     /* 0.1 degC (TEMP 0.1 K minus 2731) */
    int full_mah;           /* FullChargeCapacity, mAh */
    int design_mah;         /* DesignCapacity, mAh */
    uint32_t valid;         /* bit per field, set only when a read succeeded */
};

#define BQ_FIELD_PRESENT   (1u << 0)
#define BQ_FIELD_SOC       (1u << 1)
#define BQ_FIELD_VOLT      (1u << 2)
#define BQ_FIELD_CURRENT   (1u << 3)
#define BQ_FIELD_TEMP      (1u << 4)
#define BQ_FIELD_FULL      (1u << 5)
#define BQ_FIELD_DESIGN    (1u << 6)
#define BQ_FIELD_ALL       0x7fu

/* Little-endian 16-bit decode from the 2-byte I2C reply. */
uint16_t bq_gauge_le16(const uint8_t *raw);
int bq_gauge_temperature_dc(uint16_t raw);     /* 0.1 K -> 0.1 degC */
int bq_gauge_current_ma(uint16_t raw);         /* signed 16-bit mA */

/* One register read into the snapshot. raw is the 2-byte little-endian reply;
   'ok' is 0 when the I2C read succeeded, non-zero otherwise (no value published
   in that case — the official guide requires separate valid/missing state). */
int bq_gauge_apply(struct bq_gauge_snapshot *snap, enum bq_fuel_chip chip,
                   uint8_t reg, const uint8_t *raw, int ok);

/* Register address for a field, by chip family. Returns 0xff when the field is
   not present in that map. */
uint8_t bq_gauge_reg(enum bq_fuel_chip chip, unsigned field_bit);

/* Stage-7 gate: returns 0 (BQ_FEASIBLE_GAUGE) only for a confirmed plain-I2C
   gauge with the GENI I2C transport linked. DSP audio / PMIC GLINK battery /
   RPMh cpufreq are represented by the caller's dsp_audio_* flags separately. */
unsigned bq_gauge_assess(enum bq_fuel_chip chip,
                         const struct bq_gauge_transport *transport);
unsigned bq_stage7_assess(enum bq_fuel_chip chip,
                          const struct bq_gauge_transport *transport,
                          int firmware_available, int os_abi_available,
                          int dsp_audio_available);

const char *bq_gauge_chip_name(enum bq_fuel_chip chip);
const char *bq_gauge_register_map_name(enum bq_fuel_chip chip);

#ifdef __cplusplus
}
#endif
#endif
