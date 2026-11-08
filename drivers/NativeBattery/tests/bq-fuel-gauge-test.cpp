// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the stage-7 (power/battery) HOST-VERIFIED feasibility study.
// Pure host, no IOKit, no hardware, no firmware. Verifies the byte-exact
// bq27xxx I2C decoder (Linux v6.12 bq27xxx_battery.c + _i2c.c), the two
// register-map families, the valid/missing-state rule, and the honest
// feasible-gauge / blocked-DSP-audio verdict.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "BqFuelGauge.h"

static uint8_t lo(uint16_t v) { return (uint8_t)(v & 0xffu); }
static uint8_t hi(uint16_t v) { return (uint8_t)((v >> 8) & 0xffu); }

static void testLe16()
{
    const uint8_t raw[2] = {0x34, 0x12};
    assert(bq_gauge_le16(raw) == 0x1234u);
    const uint8_t zero[2] = {0, 0};
    assert(bq_gauge_le16(zero) == 0u);
    const uint8_t max[2] = {0xff, 0xff};
    assert(bq_gauge_le16(max) == 0xffffu);
    assert(bq_gauge_le16(nullptr) == 0u);
    printf("  testLe16: little-endian 34 12 -> 1234 (I2C 2-byte reply)\n");
}

static void testTemperature()
{
    /* 2981 = 0.1 K -> 298.1 K = 25.0 degC -> 250 (0.1 degC). */
    assert(bq_gauge_temperature_dc(2981u) == 250);
    /* 2731 = 273.1 K = 0 degC -> 0. */
    assert(bq_gauge_temperature_dc(2731u) == 0);
    /* Below freezing: 2631 = 263.1 K = -10 degC -> -100. */
    assert(bq_gauge_temperature_dc(2631u) == -100);
    printf("  testTemperature: 2981(0.1K) -> 250(0.1degC), 2731 -> 0\n");
}

static void testCurrent()
{
    assert(bq_gauge_current_ma(1500u) == 1500);       /* charging +1500 mA */
    assert(bq_gauge_current_ma((uint16_t)(-400)) == -400); /* discharging */
    assert(bq_gauge_current_ma(0u) == 0);
    printf("  testCurrent: signed 16-bit mA (+1500 charge, -400 discharge)\n");
}

static void testRegisterMaps()
{
    /* mainline ti,bq27411 / bq27441 map (bq27421_regs). */
    assert(bq_gauge_reg(kBqChipBq27411, BQ_FIELD_SOC) == BQ274XX_REG_SOC);
    assert(bq_gauge_reg(kBqChipBq27411, BQ_FIELD_VOLT) == BQ274XX_REG_VOLT);
    assert(bq_gauge_reg(kBqChipBq27411, BQ_FIELD_TEMP) == BQ274XX_REG_TEMP);
    assert(bq_gauge_reg(kBqChipBq27411, BQ_FIELD_FULL) == BQ274XX_REG_FCC);
    assert(bq_gauge_reg(kBqChipBq27411, BQ_FIELD_DESIGN) == BQ274XX_REG_DCAP);
    /* vendor-DTB ti,bq27541-battery map (bq27541_regs) — DIFFERENT offsets. */
    assert(bq_gauge_reg(kBqChipBq27541, BQ_FIELD_SOC) == BQ27541_REG_SOC);
    assert(bq_gauge_reg(kBqChipBq27541, BQ_FIELD_VOLT) == BQ27541_REG_VOLT);
    assert(bq_gauge_reg(kBqChipBq27541, BQ_FIELD_TEMP) == BQ27541_REG_TEMP);
    assert(bq_gauge_reg(kBqChipBq27541, BQ_FIELD_FULL) == BQ27541_REG_FCC);
    assert(bq_gauge_reg(kBqChipBq27541, BQ_FIELD_DESIGN) == BQ27541_REG_DCAP);
    /* The load-bearing gap: SOC/VOLT/TEMP/AI/FCC addresses differ by family. */
    assert(BQ274XX_REG_SOC != BQ27541_REG_SOC);      /* 0x1c vs 0x2c */
    assert(BQ274XX_REG_VOLT != BQ27541_REG_VOLT);    /* 0x04 vs 0x08 */
    assert(BQ274XX_REG_TEMP != BQ27541_REG_TEMP);    /* 0x02 vs 0x06 */
    assert(BQ274XX_REG_AI != BQ27541_REG_AI);        /* 0x10 vs 0x14 */
    assert(BQ274XX_REG_FCC != BQ27541_REG_FCC);      /* 0x0e vs 0x12 */
    assert(BQ274XX_REG_DCAP == BQ27541_REG_DCAP);    /* 0x3c shared */
    /* Unknown chip / unmapped field -> 0xff. */
    assert(bq_gauge_reg(kBqChipUnknown, BQ_FIELD_SOC) == 0xffu);
    assert(bq_gauge_reg(kBqChipBq27411, (1u << 7)) == 0xffu);
    printf("  testRegisterMaps: two families differ (SOC 1c/2c, VOLT 04/08, TEMP 02/06); DCAP 3c shared\n");
}

static void testApplySuccessBq27411()
{
    struct bq_gauge_snapshot s;
    std::memset(&s, 0, sizeof(s));
    const uint8_t soc[2] = {58, 0};                 /* 58% */
    const uint8_t volt[2] = {lo(3820), hi(3820)};   /* 3820 mV */
    const uint8_t temp[2] = {lo(2981), hi(2981)};   /* 298.1 K */
    const uint8_t cur[2] = {lo((uint16_t)(-400)), hi((uint16_t)(-400))};
    const uint8_t fcc[2] = {lo(3100), hi(3100)};
    const uint8_t dcap[2] = {lo(3300), hi(3300)};
    assert(bq_gauge_apply(&s, kBqChipBq27411, BQ274XX_REG_SOC, soc, 0) == 0);
    assert(bq_gauge_apply(&s, kBqChipBq27411, BQ274XX_REG_VOLT, volt, 0) == 0);
    assert(bq_gauge_apply(&s, kBqChipBq27411, BQ274XX_REG_TEMP, temp, 0) == 0);
    assert(bq_gauge_apply(&s, kBqChipBq27411, BQ274XX_REG_AI, cur, 0) == 0);
    assert(bq_gauge_apply(&s, kBqChipBq27411, BQ274XX_REG_FCC, fcc, 0) == 0);
    assert(bq_gauge_apply(&s, kBqChipBq27411, BQ274XX_REG_DCAP, dcap, 0) == 0);
    assert(s.valid == BQ_FIELD_ALL);
    assert(s.present == 1);
    assert(s.percent == 58 && s.voltage_mv == 3820);
    assert(s.temperature_dc == 250 && s.current_ma == -400);
    assert(s.full_mah == 3100 && s.design_mah == 3300);
    printf("  testApplySuccessBq27411: 58%%, 3820mV, 25.0degC, -400mA, 3100/3300mAh, valid=0x7f\n");
}

static void testApplySuccessBq27541()
{
    struct bq_gauge_snapshot s;
    std::memset(&s, 0, sizeof(s));
    const uint8_t soc[2] = {77, 0};
    const uint8_t volt[2] = {lo(4100), hi(4100)};
    assert(bq_gauge_apply(&s, kBqChipBq27541, BQ27541_REG_SOC, soc, 0) == 0);
    assert(bq_gauge_apply(&s, kBqChipBq27541, BQ27541_REG_VOLT, volt, 0) == 0);
    assert(s.percent == 77 && s.voltage_mv == 4100);
    assert((s.valid & (BQ_FIELD_SOC | BQ_FIELD_VOLT | BQ_FIELD_PRESENT)) ==
           (BQ_FIELD_SOC | BQ_FIELD_VOLT | BQ_FIELD_PRESENT));
    /* A bq27411 register (0x1c) is NOT the SOC register in the bq27541 map:
       feeding it as a bq27541 read must not publish SOC. */
    assert(bq_gauge_apply(&s, kBqChipBq27541, BQ274XX_REG_SOC, soc, 0) == -1);
    assert((s.valid & BQ_FIELD_SOC) != 0u);   /* unchanged from the real 0x2c read */
    printf("  testApplySuccessBq27541: 77%% @0x2c, 4100mV @0x08; 0x1c is not SOC on this map\n");
}

static void testApplyFailureKeepsMissingState()
{
    struct bq_gauge_snapshot s;
    std::memset(&s, 0, sizeof(s));
    const uint8_t raw[2] = {0x64, 0x00};
    /* A failed I2C read (ok != 0) must NOT set any valid bit or publish a value. */
    assert(bq_gauge_apply(&s, kBqChipBq27411, BQ274XX_REG_SOC, raw, -6) == -1);
    assert(s.valid == 0u);
    assert(s.present == 0 && s.percent == 0);
    /* Null snapshot/raw and unknown chip are also refused. */
    assert(bq_gauge_apply(nullptr, kBqChipBq27411, BQ274XX_REG_SOC, raw, 0) == -1);
    assert(bq_gauge_apply(&s, kBqChipBq27411, BQ274XX_REG_SOC, nullptr, 0) == -1);
    assert(bq_gauge_apply(&s, kBqChipUnknown, BQ274XX_REG_SOC, raw, 0) == -1);
    /* An unmapped register is refused without touching valid. */
    assert(bq_gauge_apply(&s, kBqChipBq27411, 0x99u, raw, 0) == -1);
    assert(s.valid == 0u);
    printf("  testApplyFailureKeepsMissingState: failed/unknown reads publish nothing (valid stays 0)\n");
}

static void testGaugeGateFeasible()
{
    struct bq_gauge_transport t = {1, 1, 1};   /* GENI I2C linked + ACK + chip id */
    assert(bq_gauge_assess(kBqChipBq27411, &t) == BQ_FEASIBLE_GAUGE);  /* 0x00 */
    assert(bq_gauge_assess(kBqChipBq27541, &t) == BQ_FEASIBLE_GAUGE);  /* 0x00 */
    /* The load-bearing finding: the plain-I2C gauge needs NO firmware, NO DSP,
       NO OS ABI. bq_gauge_assess deliberately has no such inputs. */
    printf("  testGaugeGateFeasible: both chip families FEASIBLE (0x00) on GENI I2C + ACK\n");
}

static void testGaugeGateBlockers()
{
    struct bq_gauge_transport ok = {1, 1, 1};
    struct bq_gauge_transport no_i2c = {0, 1, 1};
    struct bq_gauge_transport no_ack = {1, 0, 1};
    assert(bq_gauge_assess(kBqChipUnknown, &ok) & BQ_BLOCK_UNKNOWN_CHIP);
    assert(bq_gauge_assess(kBqChipBq27411, &no_i2c) & BQ_BLOCK_NO_I2C_TRANSPORT);
    assert(bq_gauge_assess(kBqChipBq27411, &no_ack) & BQ_BLOCK_NO_I2C_TRANSPORT);
    assert(bq_gauge_assess(kBqChipBq27411, nullptr) & BQ_BLOCK_NO_I2C_TRANSPORT);
    printf("  testGaugeGateBlockers: unknown chip / no I2C / no ACK correctly block\n");
}

static void testStage7Verdict()
{
    struct bq_gauge_transport t = {1, 1, 1};
    /* Truth on enchilada today: gauge feasible, but no firmware, no closed OS
       ABI and no DSP/audio stack -> FIRMWARE|OS_ABI|DSP_AUDIO = 0x1c. */
    unsigned today = bq_stage7_assess(kBqChipBq27411, &t, 0, 0, 0);
    assert(today == (BQ_BLOCK_FIRMWARE | BQ_BLOCK_OS_ABI | BQ_BLOCK_DSP_AUDIO));
    assert(today == 0x1cu);
    /* Even with all DSP/audio inputs forced to 1, an unknown chip with no I2C
       still cannot reach 0x00 — the gauge gate is independent and honest. */
    unsigned forced = bq_stage7_assess(kBqChipUnknown, nullptr, 1, 1, 1);
    assert(forced & (BQ_BLOCK_UNKNOWN_CHIP | BQ_BLOCK_NO_I2C_TRANSPORT));
    /* Fully supplied stack -> 0x00 (reference case, not this device). */
    assert(bq_stage7_assess(kBqChipBq27411, &t, 1, 1, 1) == BQ_FEASIBLE_GAUGE);
    printf("  testStage7Verdict: today=0x1c (FIRMWARE|OS_ABI|DSP_AUDIO); full reference=0x00\n");
}

static void testChipAndMapNames()
{
    assert(std::strstr(bq_gauge_chip_name(kBqChipBq27411), "bq27411") != nullptr);
    assert(std::strstr(bq_gauge_chip_name(kBqChipBq27541), "bq27541") != nullptr);
    assert(std::strcmp(bq_gauge_chip_name(kBqChipUnknown), "unknown") == 0);
    assert(std::strstr(bq_gauge_register_map_name(kBqChipBq27411), "SOC 0x1c") != nullptr);
    assert(std::strstr(bq_gauge_register_map_name(kBqChipBq27541), "SOC 0x2c") != nullptr);
    printf("  testChipAndMapNames: two chip/register-map identities reported\n");
}

int main()
{
    printf("enchilada stage-7 bq27xxx fuel-gauge feasibility study (host)\n");
    testLe16();
    testTemperature();
    testCurrent();
    testRegisterMaps();
    testApplySuccessBq27411();
    testApplySuccessBq27541();
    testApplyFailureKeepsMissingState();
    testGaugeGateFeasible();
    testGaugeGateBlockers();
    testStage7Verdict();
    testChipAndMapNames();
    printf("PASS: enchilada stage-7 bq27xxx fuel-gauge decoder + feasibility study (11 test groups)\n");
    return 0;
}
