// SPDX-License-Identifier: BSD-2-Clause-Patent
// Stage 7 READ-ONLY bq27xxx fuel-gauge probe orchestrator. Pure C11: no XNU /
// IOKit ABI, no MMIO, no firmware. It is the host-testable DECISION CORE that
// the device-side kernel shim (package/EnchiladaGaugeProbe.cpp) drives through
// an abstract I2C read callback; the actual hardware I/O lives in the shim.
//
// READ-ONLY CONTRACT ("先核验再上机"): the ONLY bytes this probe ever causes on
// the I2C bus are the register-address bytes of standard-command READS, exactly
// as Linux bq27xxx_battery_i2c_read() does (write 1 reg byte, repeated START,
// read 2 LE bytes). It NEVER issues a CONTROL/CONFIG write, never unseals the
// gauge, never updates its data flash. The callback contract enforces this.
//
// MODEL CONFLICT (honestly unresolved by two independent sources):
//   vendor DTB enchilada.dts:4213  -> "ti,bq27541-battery" (bq27541_regs map)
//   mainline sdm845-oneplus-common -> "ti,bq27411"        (bq27421_regs map)
// The two register maps differ (SOC 0x1c vs 0x2c, VOLT 0x04 vs 0x08, TEMP
// 0x02 vs 0x06, AI 0x10 vs 0x14, FCC 0x0e vs 0x12). So this probe READS BOTH
// maps and scores them by physical plausibility; the device log empirically
// resolves which chip is actually present. No single source is trusted.
//
// Reuses drivers/NativeBattery/BqFuelGauge.{h,c} (the already host-verified
// byte-level decoder) via bq_gauge_reg() / bq_gauge_apply().
#ifndef BQ_GAUGE_PROBE_H
#define BQ_GAUGE_PROBE_H
#include <stdint.h>
#include <stddef.h>
#include "BqFuelGauge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BQ_PROBE_ADDR   0x55u            /* I2C slave of the fuel gauge */
#define BQ_PROBE_FIELDS 6u              /* SOC, VOLT, AI, TEMP, FCC, DCAP */
#define BQ_PROBE_MAPS   2u              /* bq27411-family, bq27541-family */

/* One combined I2C register READ. 'reg' is the single byte emitted on the bus
   (the register pointer); 'out' receives 'len' bytes (here always 2). Returns
   0 on ACK+data, negative errno on NACK/timeout/error. Implementations MUST
   issue only a read transaction (write pointer + repeated START + read). */
typedef int (*bq_probe_read_fn)(void *ctx, uint8_t addr, uint8_t reg,
                                uint8_t *out, size_t len);
struct bq_probe_io {
    void *ctx;
    bq_probe_read_fn read;
};

/* The field bits probed, in a fixed order (see bq_probe_run). These are the
   7 fields the official power guide asks a power source to publish, minus the
   trivial PRESENT bit (set when any read succeeds). */
extern const unsigned kBqProbeFieldBits[BQ_PROBE_FIELDS];

/* Raw + decoded result for ONE register-map family. */
struct bq_probe_map_result {
    enum bq_fuel_chip chip;
    uint8_t  reg[BQ_PROBE_FIELDS];        /* register address read */
    uint8_t  raw[BQ_PROBE_FIELDS][2];     /* little-endian reply bytes */
    int      rc[BQ_PROBE_FIELDS];         /* 0 = read ok */
    uint16_t value[BQ_PROBE_FIELDS];      /* bq_gauge_le16(raw) */
    unsigned reads_ok;                    /* number of rc==0 fields */
    unsigned plausible;                   /* number of plausible decoded fields */
    struct bq_gauge_snapshot snap;        /* bq_gauge_apply() accumulation */
};

/* One complete read-only probe of BOTH register maps. */
struct bq_probe_result {
    struct bq_probe_map_result maps[BQ_PROBE_MAPS];
    int      selected;                    /* index of more plausible map, or -1 */
    unsigned total_reads_ok;
};

/* Physical plausibility of one decoded standard command. field_bit is one of
   BQ_FIELD_SOC/VOLT/CURRENT/TEMP/FULL/DESIGN; raw is the LE u16. Returns 1
   when the value is within a physically sane range for a Li-ion pack, else 0.
   Pure (no I/O) so the host test can pin the boundaries exactly. */
int bq_probe_plausible(unsigned field_bit, uint16_t raw);

/* Run the whole probe over 'io': read the 6 fields of each map, decode via
   bq_gauge_apply(), score plausibility, select the more plausible map. Never
   writes a gauge register. Returns 0 if >=1 read succeeded, -1 if io/read is
   NULL, -2 if no read succeeded at all (e.g. no ACK at 0x55). */
int bq_probe_run(const struct bq_probe_io *io, struct bq_probe_result *out);

/* One-line verdict for IOLog / the persistent device log. Deterministic and
   bounded (<= ~150 chars). Returns the snprintf-style length. Example:
   "BG sel=bq27411 soc=62 v=3987 i=-430 t=251 f=3250 d=3300 ok=12/12 pl=6/6
    alt=bq27541 pl=1/6 nack=0" */
int bq_probe_format(const struct bq_probe_result *r, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
