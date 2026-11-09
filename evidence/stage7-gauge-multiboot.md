# Stage 7 power — bq27xxx gauge verdict reproduced across six device boots

**Status: device-verified** (read-only re-analysis of archived device logs; no new
kernel code, no new CI build, no new flash).

## What this document answers

`porting.toml` power-area `limits[2]` recorded, verbatim:

> "The device evidence is a single boot (boot_id 553807564) on one unit at one
> charge level; it proves the register protocol and the transport, not long-term
> accuracy across charge cycles."

That was accurate when `evidence/stage7-gauge-device.md` was written (one
`gaugereplay` boot). After CI #213 added the 1 Hz verdict replay, every
later boot that enabled the gauge carries the same verdict line. A full
read-only survey of the archived device logs shows **six distinct boots** with a
parseable gauge verdict, not one. Following the standing "先核验再上机 /
verify before flashing" rule, the correction is made by re-reading existing
device evidence rather than by flashing anything new. This is the same
"stale record, not missing code" pattern as the cpu per-core (round 13),
cpu load (round 17) and memory kit (round 15) corrections.

## The measurement (verbatim)

Method 1 (`work/tmp/_gauge_multiboot_scan.py`): walk every boot directory under
`work/log/` that has a `header.txt`; read `msgbuf.txt` + `latest.txt` as
latin-1; regex-extract the `enchilada-gauge: rc=0 BG sel=... a[...] b[...]`
verdict; de-duplicate the family-A 8-field tuple with a set (the 1 Hz replay
repeats one verdict many times per boot).

`GAUGE_MULTIBOOT_SCAN: boot_dirs_total=133 boots_with_verdict=6`
`all_soc_values=[100]  distinct_charge_levels=1`
`family-A plausible set (num/den) = [[6, 6]]`

Six boots with a verdict (all `magic=SHLOG001`, `write_errors=0`):

| boot dir | boot_id | uptime | soc | v (mV) | i (mA) | t (0.1K) | f (mAh) | d (mAh) | pl |
|---|---|---|---|---|---|---|---|---|---|
| gaugereplay/untagged-21026ecc | 553807564 | 899 | 100 | 4326 | 0 | 145 | 3153 | 3240 | 6/6 |
| persist/untagged-20eab26c | 552252012 | 899 | 100 | 4328 | 26 | 182 | 3149 | 3240 | 6/6 |
| persist/untagged-20ffcb44 | 553634628 | 899 | 100 | 4327 | 29 | 157 | 3153 | 3240 | 6/6 |
| smmu/untagged-20c8106a | 549982314 | 898 | 100 | 4326 | 0 | 150 | 3153 | 3240 | 6/6 |
| smmu2/untagged-20fc669d | 553412253 | 899 | 100 | 4326 | 0 | 158 | 3153 | 3240 | 6/6 |
| smmu3/untagged-20e344b3 | 551765171 | 898 | 100 | 4325 | 0 | 157 | 3149 | 3240 | 6/6 |

These are six distinct `boot_id` values across four payload families
(`gaugereplay`, `persist` x2, `smmu`, `smmu2`, `smmu3`). The physical readings
vary across boots: voltage 4325-4328 mV, current 0-29 mA, temperature
145-182 (0.1 K units), full capacity 3149-3153 mAh. Design capacity 3240 mAh
is constant. Family A (`ti,bq27411`) is plausible 6/6 on every boot; family B
decodes to impossible values on every boot — the model-conflict resolution in
`evidence/stage7-gauge-device.md` is reproduced on all six, not assumed once.

**Why the varying values matter:** the probe does not replay a cached constant.
Voltage, current, temperature and full-capacity differ from boot to boot while
each boot's own replay ticks are identical, which is exactly what a fresh I2C
read of live hardware on each boot produces. A cached string would be
byte-identical across boots.

A representative verdict line, verbatim (from
`persist/untagged-20ffcb44/msgbuf.txt`):

```
[  761.994190]: enchilada-gauge: rc=0 BG sel=bq27411 / bq27441 (mainline ti,bq27411 @0x55) ok=12/12 a[soc=100 v=4327 i=29 t=157 f=3153 d=3240 pl=6/6] b[soc=3165 v=3173 i=-3402 t=-2059 f=0 d=3240 pl=3/6] (replay t=734 attempt=1)
```

## Two independent readers (m03546 — verify twice before acting)

Method 2 (`work/tmp/_gauge_multiboot_verify2.py`) uses a deliberately
different data path: scan the raw bytes of **every file** in each boot
directory for `b"enchilada-gauge:"` and `b"BG sel="` (a loose byte detector
that also catches truncated/concatenated replay lines), parse the family-A
`a[...]` fields with a whitespace split parser (not a regex), validate each
`header.txt` for `magic`/`write_errors`/`boot_id`/`uptime_s`, and cross-check
the boot set against method 1's `gauge_multiboot_scan.json`.

`GAUGE_MULTIBOOT_VERIFY2: PASS (12/12)` — the twelve checks:

- boot dirs == 133 (method 1 saw 133)
- exactly 6 boots carry a parseable gauge verdict
- method-2 boot set == method-1 boot set
- all 6 boot_ids are distinct
- all 6 headers have magic SHLOG001
- all 6 headers have write_errors=0
- voltage varies across boots (>=3 distinct): re-read hardware
- current varies across boots (>=2 distinct)
- temperature varies across boots (>=3 distinct)
- full-capacity varies across boots (>=2 distinct)
- SOC is 100 on every boot (single charge level - honest boundary)
- loose byte detector finds gauge lines in every verdict boot

Raw byte hit counts per boot
(`gauge_hits`/`bg_hits`): gaugereplay 960, persist-20eab26c 576,
persist-20ffcb44 332, smmu 584, smmu2 578, smmu3 508. Each boot's de-duplicated
family-A tuple is unique and agrees with method 1.

## What the evidence does and does not establish

**Establishes:** the read-only bq27xxx probe runs and returns a full 12/12
register set on six distinct device boots spanning four payload variants;
family A is physically sane (6/6) on every boot; the probe re-reads live
hardware each boot (values vary); every log has `magic=SHLOG001` and
`write_errors=0`. The `porting.toml` "single boot" limit is therefore a stale
record, not a current property.

**Does NOT establish (honest boundaries — do not overclaim):**

- **SOC is 100 on every boot.** This is still a single charge level on a single
  unit. The correction is "one boot" -> "six boots", **not** "six charge
  levels". No long-term accuracy across charge/discharge cycles is claimed.
- The gauge is still **READ-ONLY**: no CONTROL / CONFIG / unseal write, no
  charger state, no IOPMPowerSource publication, no OS power-source service.
  Nothing inside iOS consumes the reading yet.
- No new kernel code, no CI build and no flash were performed for this record;
  it is a read-only re-analysis of logs already on disk.
- Audio / DSP / GLINK-battery / RPMh-cpufreq remain BLOCKED (unchanged from the
  power-area decision).
- The 128 KiB msgbuf ring still wraps early; the 1 Hz replay is what keeps the
  verdict observable. Some concatenated/truncated replay lines exist in the
  ring and are handled by the loose byte detector.

## Reproduce

```
python -X utf8 work/tmp/_gauge_multiboot_scan.py
python -X utf8 work/tmp/_gauge_multiboot_verify2.py
# Both write JSON to work/tmp/ and print their PASS verdicts.
# Cross-check the underlying verdict lines:
#   grep "BG sel" oneplus6-ios/work/log/*/untagged-*/msgbuf.txt
```

Analyzer output captured in `work/tmp/gauge_multiboot_scan.json` and
`work/tmp/gauge_multiboot_verify2.json`. Original single-boot device evidence:
`evidence/stage7-gauge-device.md` (boot_id 553807564, payload
`payload-gaugereplay.img`, sha256
`73549736ac9542dea03532021ed4d84b067e088ccfbbf768895303037c642704`, CI #213
kernel_sha256
`4e85b5d1edacfac30c2e99fa14a5df22269c444efd862380a5236ca06774ad1d`).

GAUGE_MULTIBOOT_VERIFY2: PASS
