// SPDX-License-Identifier: BSD-2-Clause-Patent
// Stage 7 HOST-VERIFIED bq27xxx fuel-gauge decoder. Pure C11; pure byte logic,
// no register I/O, no firmware, no XNU/IOKit ABI. See BqFuelGauge.h for the
// four independent research sources and the load-bearing conclusion.
#include "BqFuelGauge.h"

uint16_t bq_gauge_le16(const uint8_t *raw)
{
    if (raw == 0) {
        return 0u;
    }
    return (uint16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
}

/* Linux v6.12 bq27xxx_battery_read_temperature(): TEMP is 0.1 K; subtract
   2731 to get 0.1 degC. Returned as a signed 0.1 degC value. */
int bq_gauge_temperature_dc(uint16_t raw)
{
    return (int)raw - 2731;
}

/* Linux v6.12 bq27xxx_battery_current_and_status(): AI is a signed 16-bit mA
   value; > 0 means charging. */
int bq_gauge_current_ma(uint16_t raw)
{
    return (int)(int16_t)raw;
}

/* Register address for a field, by chip family. Only the 7 fields the official
   power guide asks a power source to publish are mapped. Returns 0xff for an
   unknown chip or a field absent from that register map. */
uint8_t bq_gauge_reg(enum bq_fuel_chip chip, unsigned field_bit)
{
    if (chip == kBqChipBq27411) {
        switch (field_bit) {
        case BQ_FIELD_SOC:     return BQ274XX_REG_SOC;
        case BQ_FIELD_VOLT:    return BQ274XX_REG_VOLT;
        case BQ_FIELD_CURRENT: return BQ274XX_REG_AI;
        case BQ_FIELD_TEMP:    return BQ274XX_REG_TEMP;
        case BQ_FIELD_FULL:    return BQ274XX_REG_FCC;
        case BQ_FIELD_DESIGN:  return BQ274XX_REG_DCAP;
        default:               return 0xffu;
        }
    }
    if (chip == kBqChipBq27541) {
        switch (field_bit) {
        case BQ_FIELD_SOC:     return BQ27541_REG_SOC;
        case BQ_FIELD_VOLT:    return BQ27541_REG_VOLT;
        case BQ_FIELD_CURRENT: return BQ27541_REG_AI;
        case BQ_FIELD_TEMP:    return BQ27541_REG_TEMP;
        case BQ_FIELD_FULL:    return BQ27541_REG_FCC;
        case BQ_FIELD_DESIGN:  return BQ27541_REG_DCAP;
        default:               return 0xffu;
        }
    }
    return 0xffu;
}

/* One I2C standard-command read into a snapshot. raw is the 2-byte LE reply;
   ok==0 means the transfer succeeded. On failure the field's valid bit is NOT
   set (the official guide: keep valid readings and missing state separately).
   Returns 0 on a published field, -1 on an unknown/failed/unmapped read. */
int bq_gauge_apply(struct bq_gauge_snapshot *snap, enum bq_fuel_chip chip,
                   uint8_t reg, const uint8_t *raw, int ok)
{
    if (snap == 0 || raw == 0 || ok != 0) {
        return -1;
    }
    if (chip != kBqChipBq27411 && chip != kBqChipBq27541) {
        return -1;
    }
    uint16_t v = bq_gauge_le16(raw);

    if (chip == kBqChipBq27411) {
        switch (reg) {
        case BQ274XX_REG_SOC:
            snap->percent = (int)v;
            snap->valid |= BQ_FIELD_SOC;
            break;
        case BQ274XX_REG_VOLT:
            snap->voltage_mv = (int)v;
            snap->valid |= BQ_FIELD_VOLT;
            break;
        case BQ274XX_REG_AI:
            snap->current_ma = bq_gauge_current_ma(v);
            snap->valid |= BQ_FIELD_CURRENT;
            break;
        case BQ274XX_REG_TEMP:
            snap->temperature_dc = bq_gauge_temperature_dc(v);
            snap->valid |= BQ_FIELD_TEMP;
            break;
        case BQ274XX_REG_FCC:
            snap->full_mah = (int)v;
            snap->valid |= BQ_FIELD_FULL;
            break;
        case BQ274XX_REG_DCAP:
            snap->design_mah = (int)v;
            snap->valid |= BQ_FIELD_DESIGN;
            break;
        default:
            return -1;
        }
    } else {
        switch (reg) {
        case BQ27541_REG_SOC:
            snap->percent = (int)v;
            snap->valid |= BQ_FIELD_SOC;
            break;
        case BQ27541_REG_VOLT:
            snap->voltage_mv = (int)v;
            snap->valid |= BQ_FIELD_VOLT;
            break;
        case BQ27541_REG_AI:
            snap->current_ma = bq_gauge_current_ma(v);
            snap->valid |= BQ_FIELD_CURRENT;
            break;
        case BQ27541_REG_TEMP:
            snap->temperature_dc = bq_gauge_temperature_dc(v);
            snap->valid |= BQ_FIELD_TEMP;
            break;
        case BQ27541_REG_FCC:
            snap->full_mah = (int)v;
            snap->valid |= BQ_FIELD_FULL;
            break;
        case BQ27541_REG_DCAP:
            snap->design_mah = (int)v;
            snap->valid |= BQ_FIELD_DESIGN;
            break;
        default:
            return -1;
        }
    }
    /* A gauge that returns any good standard-command read is present. */
    snap->present = 1;
    snap->valid |= BQ_FIELD_PRESENT;
    return 0;
}

/* Stage-7 gauge-only gate. The bq27xxx decoder is FEASIBLE only for a known
   chip with the device-verified GENI I2C transport linked AND a confirmed
   slave ACK at 0x55. Firmware/DSP/OS-ABI are irrelevant to the plain-I2C gauge
   (that is the load-bearing finding), so they are NOT blockers here. */
unsigned bq_gauge_assess(enum bq_fuel_chip chip,
                         const struct bq_gauge_transport *transport)
{
    unsigned blockers = 0u;
    if (chip != kBqChipBq27411 && chip != kBqChipBq27541) {
        blockers |= BQ_BLOCK_UNKNOWN_CHIP;
    }
    if (transport == 0 || !transport->geni_i2c_available ||
        !transport->addr_0x55_acked) {
        blockers |= BQ_BLOCK_NO_I2C_TRANSPORT;
    }
    return blockers;   /* 0x00 (BQ_FEASIBLE_GAUGE) when everything is present */
}

/* Whole stage-7 assessment. The gauge sub-item is the ONE feasible piece;
   full DSP/audio (ADSP PIL + SLIMBUS WCD9340 + Q6AFE + CoreAudio HAL), PMIC
   GLINK battery manager, and RPMh cpufreq all need proprietary firmware and a
   closed OS ABI, and sheng's SM8550 implementation is a different generation.
   The caller's dsp_audio_available represents that whole stack; when it is 0
   (the truth on this device today) the DSP/audio blockers are set. */
unsigned bq_stage7_assess(enum bq_fuel_chip chip,
                          const struct bq_gauge_transport *transport,
                          int firmware_available, int os_abi_available,
                          int dsp_audio_available)
{
    unsigned v = bq_gauge_assess(chip, transport);
    /* These gates are about the DSP/audio/GLINK/RPMh stack, not the gauge. */
    if (!firmware_available) {
        v |= BQ_BLOCK_FIRMWARE;
    }
    if (!os_abi_available) {
        v |= BQ_BLOCK_OS_ABI;
    }
    if (!dsp_audio_available) {
        v |= BQ_BLOCK_DSP_AUDIO;
    }
    return v;
}

const char *bq_gauge_chip_name(enum bq_fuel_chip chip)
{
    switch (chip) {
    case kBqChipBq27411: return "bq27411 / bq27441 (mainline ti,bq27411 @0x55)";
    case kBqChipBq27541: return "bq27541 (vendor DTB ti,bq27541-battery @0x55)";
    default:             return "unknown";
    }
}

const char *bq_gauge_register_map_name(enum bq_fuel_chip chip)
{
    switch (chip) {
    case kBqChipBq27411: return "bq27421_regs (SOC 0x1c, VOLT 0x04, TEMP 0x02)";
    case kBqChipBq27541: return "bq27541_regs (SOC 0x2c, VOLT 0x08, TEMP 0x06)";
    default:             return "none";
    }
}
