// SPDX-License-Identifier: BSD-2-Clause-Patent
// Stage 7 READ-ONLY bq27xxx fuel-gauge probe orchestrator. Pure C11; see
// BqGaugeProbe.h for the read-only contract, the two-source model conflict and
// the design rationale. No register I/O here: the callback does the reads.
//
// FREESTANDING: this file is compiled BOTH by the host test runner (C11) and by
// the XNU kernel C command (osfmk/startup.o.json, no libc). It therefore uses
// NO libc calls at all (no <stdio.h>/<string.h>, no snprintf/memset): zeroing
// and the bounded verdict formatting are hand-written below. The exact output
// is byte-identical to what a snprintf-based formatter would produce, so the
// host assertions on the verdict line remain valid in the kernel.
#include "BqGaugeProbe.h"

const unsigned kBqProbeFieldBits[BQ_PROBE_FIELDS] = {
    BQ_FIELD_SOC, BQ_FIELD_VOLT, BQ_FIELD_CURRENT,
    BQ_FIELD_TEMP, BQ_FIELD_FULL, BQ_FIELD_DESIGN,
};

static const enum bq_fuel_chip kMapChips[BQ_PROBE_MAPS] = {
    kBqChipBq27411, kBqChipBq27541,
};

/* Freestanding zero fill (replaces memset). */
static void zbytes(void *p, size_t n)
{
    unsigned char *b = (unsigned char *)p;
    for (size_t i = 0; i < n; i++) b[i] = 0u;
}

/* Sane Li-ion pack bounds, from the units in bq27xxx_battery.c:
   SOC 0..100 %, VOLT 2500..5000 mV, AI is signed mA (|I| < 6000),
   TEMP 0.1 K -> 0.1 degC = -200..800 (i.e. -20..80 C),
   FCC / DCAP 100..6000 mAh. A value outside these means the register bytes
   came from the WRONG map (or a bogus chip), not a real measurement. */
int bq_probe_plausible(unsigned field_bit, uint16_t raw)
{
    const uint32_t v = (uint32_t)raw;   /* unsigned comparisons only (no -Wsign-conversion) */
    switch (field_bit) {
    case BQ_FIELD_SOC:
        return v <= 100u;
    case BQ_FIELD_VOLT:
        return v >= 2500u && v <= 5000u;
    case BQ_FIELD_CURRENT: {
        int ma = bq_gauge_current_ma(raw);
        return ma > -6000 && ma < 6000;
    }
    case BQ_FIELD_TEMP: {
        int dc = bq_gauge_temperature_dc(raw);
        return dc >= -200 && dc <= 800;
    }
    case BQ_FIELD_FULL:
    case BQ_FIELD_DESIGN:
        return v >= 100u && v <= 6000u;
    default:
        return 0;
    }
}

/* Read the 6 standard commands of one chip family, decode, and score. */
static void run_map(const struct bq_probe_io *io, enum bq_fuel_chip chip,
                    struct bq_probe_map_result *m, unsigned *total_ok)
{
    zbytes(m, sizeof(*m));
    m->chip = chip;
    for (unsigned i = 0; i < BQ_PROBE_FIELDS; i++) {
        unsigned bit = kBqProbeFieldBits[i];
        m->reg[i] = bq_gauge_reg(chip, bit);
        m->rc[i] = -1;
        if (m->reg[i] == 0xffu) continue;          /* field absent in this map */
        uint8_t raw[2] = {0, 0};
        int rc = io->read(io->ctx, BQ_PROBE_ADDR, m->reg[i], raw, 2);
        m->rc[i] = rc;
        m->raw[i][0] = raw[0];
        m->raw[i][1] = raw[1];
        if (rc) continue;                           /* failed read: no publish */
        m->reads_ok++;
        (*total_ok)++;
        m->value[i] = bq_gauge_le16(raw);
        /* The decoder is reused verbatim (stage-7 host-verified). */
        (void)bq_gauge_apply(&m->snap, chip, m->reg[i], raw, 0);
        if (bq_probe_plausible(bit, m->value[i])) m->plausible++;
    }
}

int bq_probe_run(const struct bq_probe_io *io, struct bq_probe_result *out)
{
    if (!io || !io->read || !out) return -1;
    zbytes(out, sizeof(*out));
    out->selected = -1;
    for (unsigned k = 0; k < BQ_PROBE_MAPS; k++)
        run_map(io, kMapChips[k], &out->maps[k], &out->total_reads_ok);
    if (!out->total_reads_ok) return -2;           /* no ACK / all reads failed */
    /* Select by plausibility, then by raw reads_ok as a deterministic tiebreak. */
    int best = -1;
    for (unsigned k = 0; k < BQ_PROBE_MAPS; k++) {
        const struct bq_probe_map_result *m = &out->maps[k];
        if (!m->reads_ok) continue;
        if (best < 0) { best = (int)k; continue; }
        const struct bq_probe_map_result *b = &out->maps[best];
        if (m->plausible > b->plausible ||
            (m->plausible == b->plausible && m->reads_ok > b->reads_ok)) best = (int)k;
    }
    out->selected = best;
    return 0;
}

/* ---- Bounded, freestanding formatter (no snprintf) -----------------------
   Tracks the would-be length in n, writes bytes only while within cap-1, and
   always NUL-terminates. Produces the same single line as:
   "BG sel=%s ok=%u/%u a[soc=%d v=%d i=%d t=%d f=%d d=%d pl=%u/%u] "
   "b[soc=%d v=%d i=%d t=%d f=%d d=%d pl=%u/%u]"                           */
struct outbuf { char *p; size_t cap, n; };
static void obl(struct outbuf *o, const char *s) {
    while (*s) {
        if (o->n + 1 < o->cap) o->p[o->n] = *s;
        o->n++; s++;
    }
}
static void obu(struct outbuf *o, unsigned v) {
    char tmp[12]; int k = 0;
    if (!v) tmp[k++] = '0';
    while (v) { tmp[k++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (k) {
        char c = tmp[--k];
        if (o->n + 1 < o->cap) o->p[o->n] = c;
        o->n++;
    }
}
static void obi(struct outbuf *o, int v) {
    if (v < 0) { obl(o, "-"); obu(o, (unsigned)(-(long)v)); }
    else obu(o, (unsigned)v);
}
static void obmap(struct outbuf *o, const struct bq_probe_map_result *m) {
    obl(o, "[soc="); obi(o, m->snap.percent);
    obl(o, " v=");   obi(o, m->snap.voltage_mv);
    obl(o, " i=");   obi(o, m->snap.current_ma);
    obl(o, " t=");   obi(o, m->snap.temperature_dc);
    obl(o, " f=");   obi(o, m->snap.full_mah);
    obl(o, " d=");   obi(o, m->snap.design_mah);
    obl(o, " pl=");  obu(o, m->plausible);
    obl(o, "/");     obu(o, m->reads_ok);
    obl(o, "]");
}

int bq_probe_format(const struct bq_probe_result *r, char *buf, size_t cap)
{
    if (!r || !buf || !cap) return 0;
    struct outbuf o = {buf, cap, 0};
    const char *sel = r->selected >= 0 ? bq_gauge_chip_name(r->maps[r->selected].chip) : "none";
    obl(&o, "BG sel="); obl(&o, sel);
    obl(&o, " ok="); obu(&o, r->total_reads_ok);
    obl(&o, "/");    obu(&o, 2u * BQ_PROBE_FIELDS);
    obl(&o, " a");   obmap(&o, &r->maps[0]);
    obl(&o, " b");   obmap(&o, &r->maps[1]);
    size_t end = o.n < o.cap ? o.n : o.cap - 1u;
    buf[end] = 0;
    return (int)o.n;
}
