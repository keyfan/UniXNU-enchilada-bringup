// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the stage-7 READ-ONLY bq27xxx gauge probe orchestrator
// (BqGaugeProbe.c). Pure host: a fake I2C slave at 0x55 emulates either the
// mainline bq27411 register map or the vendor bq27541 map, so the test proves
// (a) both maps are read, (b) the more physically plausible map wins, (c) the
// read-only contract (no gauge writes, only standard-command register bytes),
// (d) missing/failed reads keep their valid state empty, (e) the verdict line.
// No IOKit, no MMIO, no hardware: HOST-VERIFIED core, driven by a callback.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
#include "BqFuelGauge.h"
#include "BqGaugeProbe.h"

static uint8_t lo(uint16_t v) { return (uint8_t)(v & 0xffu); }
static uint8_t hi(uint16_t v) { return (uint8_t)((v >> 8) & 0xffu); }

/* Local stand-ins for the fake slave's return codes (the core only checks
   rc==0 vs rc!=0; the kernel shim will return the real GENI I2C errnos). */
enum { kFakeNack = -6, kFakeIo = -5, kFakeParam = -22 };

/* Realistic Li-ion pack readings. */
enum { kSoc = 62, kVolt = 3987, kTemp = 2981 /* 25.0 degC */, kFcc = 3250, kDcap = 3300 };
static const int16_t kCur = -430;   /* discharging */

/* A fake gauge that owns ONE register map with real values. Every other
   register answers 0xffff (implausible), which is what a wrong-map read looks
   like. DCAP (0x3c) is shared by both maps, so it is real in both. The callback
   only ever performs READ transactions, exactly as the device shim will. */
struct fake_gauge {
    enum bq_fuel_chip chip = kBqChipUnknown;
    bool no_ack = false;
    uint8_t fail_reg = 0xffu;
    unsigned reads = 0;
    std::set<uint8_t> seen;
    int bad_addr = 0;
};

static int fake_read(void *ctx, uint8_t addr, uint8_t reg, uint8_t *out, size_t len) {
    auto *g = (struct fake_gauge *)ctx;
    g->reads++;
    g->seen.insert(reg);
    if (addr != BQ_PROBE_ADDR) g->bad_addr++;
    if (g->no_ack) return kFakeNack;
    if (reg == g->fail_reg) return kFakeIo;
    if (len != 2) return kFakeParam;
    uint16_t v = 0xffffu;   /* default: implausible garbage */
    bool have = false;
    if (reg == BQ274XX_REG_DCAP || reg == BQ27541_REG_DCAP) { v = kDcap; have = true; }
    if (g->chip == kBqChipBq27411) {
        switch (reg) {
        case BQ274XX_REG_SOC:  v = kSoc;  have = true; break;
        case BQ274XX_REG_VOLT: v = kVolt; have = true; break;
        case BQ274XX_REG_AI:   v = (uint16_t)kCur; have = true; break;
        case BQ274XX_REG_TEMP: v = kTemp; have = true; break;
        case BQ274XX_REG_FCC:  v = kFcc;  have = true; break;
        default: break;
        }
    } else {
        switch (reg) {
        case BQ27541_REG_SOC:  v = kSoc;  have = true; break;
        case BQ27541_REG_VOLT: v = kVolt; have = true; break;
        case BQ27541_REG_AI:   v = (uint16_t)kCur; have = true; break;
        case BQ27541_REG_TEMP: v = kTemp; have = true; break;
        case BQ27541_REG_FCC:  v = kFcc;  have = true; break;
        default: break;
        }
    }
    if (!have) v = 0xffffu;
    out[0] = lo(v);
    out[1] = hi(v);
    return 0;
}

/* The exact set of register bytes the probe is allowed to emit. */
static std::set<uint8_t> allowed_regs() {
    std::set<uint8_t> s;
    for (unsigned k = 0; k < BQ_PROBE_MAPS; k++) {
        enum bq_fuel_chip chip = k == 0 ? kBqChipBq27411 : kBqChipBq27541;
        for (unsigned i = 0; i < BQ_PROBE_FIELDS; i++) {
            uint8_t r = bq_gauge_reg(chip, kBqProbeFieldBits[i]);
            if (r != 0xffu) s.insert(r);
        }
    }
    return s;
}

static void testPlausibleBoundaries() {
    assert(bq_probe_plausible(BQ_FIELD_SOC, 0) == 1);
    assert(bq_probe_plausible(BQ_FIELD_SOC, 100) == 1);
    assert(bq_probe_plausible(BQ_FIELD_SOC, 101) == 0);
    assert(bq_probe_plausible(BQ_FIELD_VOLT, 2499) == 0);
    assert(bq_probe_plausible(BQ_FIELD_VOLT, 2500) == 1);
    assert(bq_probe_plausible(BQ_FIELD_VOLT, 5000) == 1);
    assert(bq_probe_plausible(BQ_FIELD_VOLT, 5001) == 0);
    assert(bq_probe_plausible(BQ_FIELD_CURRENT, (uint16_t)(-5000)) == 1);
    assert(bq_probe_plausible(BQ_FIELD_CURRENT, (uint16_t)(6000)) == 0);
    assert(bq_probe_plausible(BQ_FIELD_TEMP, 2731) == 1);   /* 0 degC */
    assert(bq_probe_plausible(BQ_FIELD_TEMP, 2981) == 1);   /* 25 degC */
    assert(bq_probe_plausible(BQ_FIELD_TEMP, 0) == 0);       /* -273 degC */
    assert(bq_probe_plausible(BQ_FIELD_FULL, 99) == 0);
    assert(bq_probe_plausible(BQ_FIELD_FULL, 3300) == 1);
    assert(bq_probe_plausible(BQ_FIELD_DESIGN, 6001) == 0);
    assert(bq_probe_plausible((1u << 7), 50) == 0);         /* unknown field */
    printf("  testPlausibleBoundaries: SOC/VOLT/AI/TEMP/FCC/DCAP sane ranges pinned\n");
}

static void testSelectBq27411() {
    struct fake_gauge g; g.chip = kBqChipBq27411;
    struct bq_probe_io io = {&g, fake_read};
    struct bq_probe_result r;
    assert(bq_probe_run(&io, &r) == 0);
    assert(r.total_reads_ok == 2u * BQ_PROBE_FIELDS);   /* 12 reads, all ACKed */
    assert(r.selected == 0);                            /* bq27411 family wins */
    assert(r.maps[0].plausible == BQ_PROBE_FIELDS);     /* 6/6 plausible */
    assert(r.maps[0].reads_ok == BQ_PROBE_FIELDS);
    assert(r.maps[1].plausible < r.maps[0].plausible);  /* wrong map loses */
    assert(r.maps[0].snap.percent == kSoc);
    assert(r.maps[0].snap.voltage_mv == kVolt);
    assert(r.maps[0].snap.temperature_dc == 250);
    assert(r.maps[0].snap.current_ma == kCur);
    assert(r.maps[0].snap.full_mah == kFcc);
    assert(r.maps[0].snap.design_mah == kDcap);
    assert(r.maps[0].snap.valid == BQ_FIELD_ALL);
    /* Read-only contract: only standard-command register bytes, addr always 0x55. */
    assert(g.bad_addr == 0);
    for (uint8_t reg : g.seen) assert(allowed_regs().count(reg) == 1);
    printf("  testSelectBq27411: mainline map wins 6/6 plausible, 12 reads, addr 0x55 only\n");
}

static void testSelectBq27541() {
    struct fake_gauge g; g.chip = kBqChipBq27541;
    struct bq_probe_io io = {&g, fake_read};
    struct bq_probe_result r;
    assert(bq_probe_run(&io, &r) == 0);
    assert(r.selected == 1);                            /* vendor map wins */
    assert(r.maps[1].plausible == BQ_PROBE_FIELDS);
    assert(r.maps[1].snap.percent == kSoc);
    assert(r.maps[1].snap.voltage_mv == kVolt);
    assert(r.maps[0].plausible < r.maps[1].plausible);
    printf("  testSelectBq27541: vendor DTB map wins 6/6 plausible\n");
}

static void testNoAck() {
    struct fake_gauge g; g.chip = kBqChipBq27411; g.no_ack = true;
    struct bq_probe_io io = {&g, fake_read};
    struct bq_probe_result r;
    assert(bq_probe_run(&io, &r) == -2);                /* no ACK at 0x55 */
    assert(r.total_reads_ok == 0u);
    assert(r.selected == -1);
    assert(r.maps[0].snap.valid == 0u);                 /* nothing published */
    assert(r.maps[0].snap.present == 0);
    printf("  testNoAck: total NACK -> -2, no readings published, selected=-1\n");
}

static void testNullInputs() {
    struct bq_probe_result r;
    assert(bq_probe_run(nullptr, &r) == -1);
    struct fake_gauge g;
    struct bq_probe_io io = {&g, nullptr};
    assert(bq_probe_run(&io, &r) == -1);
    struct bq_probe_io ok = {&g, fake_read};
    assert(bq_probe_run(&ok, nullptr) == -1);
    printf("  testNullInputs: NULL io/read/out all refused (-1)\n");
}

static void testFailedReadKeepsMissingState() {
    struct fake_gauge g; g.chip = kBqChipBq27411;
    g.fail_reg = BQ274XX_REG_VOLT;       /* one failed standard-command read */
    struct bq_probe_io io = {&g, fake_read};
    struct bq_probe_result r;
    assert(bq_probe_run(&io, &r) == 0);
    /* The failed field's valid bit must NOT be set (official guide: separate
       valid readings from missing state). */
    assert((r.maps[0].snap.valid & BQ_FIELD_VOLT) == 0u);
    assert(r.maps[0].rc[1] != 0);        /* VOLT is field index 1 */
    assert(r.maps[0].reads_ok == BQ_PROBE_FIELDS - 1u);
    printf("  testFailedReadKeepsMissingState: failed VOLT read publishes nothing for it\n");
}

static void testFormat() {
    struct fake_gauge g; g.chip = kBqChipBq27411;
    struct bq_probe_io io = {&g, fake_read};
    struct bq_probe_result r;
    assert(bq_probe_run(&io, &r) == 0);
    char buf[256];
    int n = bq_probe_format(&r, buf, sizeof buf);
    assert(n > 0 && (size_t)n < sizeof buf);
    assert(std::strstr(buf, "BG sel=") != nullptr);
    assert(std::strstr(buf, "bq27411") != nullptr);
    assert(std::strstr(buf, "ok=12/12") != nullptr);
    assert(std::strstr(buf, "a[soc=62 v=3987") != nullptr);
    assert(std::strstr(buf, "pl=6/6") != nullptr);
    /* A bounded, deterministic, NUL-terminated single line. */
    assert(std::strchr(buf, '\n') == nullptr);
    printf("  testFormat: verdict line 'BG sel=bq27411 ... ok=12/12 ... pl=6/6'\n");
    printf("    %s\n", buf);
    assert(bq_probe_format(nullptr, buf, sizeof buf) == 0);
}

int main() {
    printf("enchilada stage-7 READ-ONLY bq27xxx gauge probe core (host)\n");
    testPlausibleBoundaries();
    testSelectBq27411();
    testSelectBq27541();
    testNoAck();
    testNullInputs();
    testFailedReadKeepsMissingState();
    testFormat();
    printf("PASS: enchilada bq27xxx read-only gauge probe orchestrator (7 test groups)\n");
    return 0;
}
