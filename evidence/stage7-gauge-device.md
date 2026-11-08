# Stage 7 power — bq27xxx fuel gauge read on device

**Status: DEVICE-VERIFIED (2026-11-09).** A read-only probe ran on the OnePlus 6
(enchilada, SDM845) inside XNU, read all seven standard bq27xxx commands over the
GENI I2C transport, and returned `rc=0`. Two independent analyses of two
different data paths agree.

- Boot: `boot_id 553807564`, uptime 899 s, payload `payload-gaugereplay.img`
  (sha256 `73549736ac9542dea03532021ed4d84b067e088ccfbbf768895303037c642704`).
- Kernel: CI #213 (`42534121427`), `kernel_sha256`
  `4e85b5d1edacfac30c2e99fa14a5df22269c444efd862380a5236ca06774ad1d`.
- Firmware revision under test: `8f4ca9bf2b386ab8414f7ca9de176d71b4f02ded`.

## The verdict line, verbatim

```
enchilada-gauge: rc=0 BG sel=bq27411 / bq27441 (mainline ti,bq27411 @0x55) ok=12/12
  a[soc=100 v=4326 i=0 t=145 f=3153 d=3240 pl=6/6]
  b[soc=3161 v=3191 i=-3402 t=-2059 f=0 d=3240 pl=3/6] (replay t=899 attempt=1)
```

## Why this settles the model conflict instead of guessing

The vendor DTB says `ti,bq27541-battery`; mainline Linux says `ti,bq27411`. Those
strings select **different register maps**, so reading one map and assuming it is
correct would produce plausible-looking garbage.

The probe therefore reads *both* maps every time and scores each field against
physical bounds (SOC 0–100 %, 2500–5000 mV, ±6000 mA, −20…80 °C, 100–6000 mAh):

| family | SOC | voltage | current | temperature | full | design | plausible |
|---|---|---|---|---|---|---|---|
| A — mainline `ti,bq27411` | 100 % | 4326 mV | 0 mA | 14.5 °C | 3153 mAh | 3240 mAh | **6/6** |
| B — vendor `ti,bq27541` | 3161 % | 3191 mV | −3402 mA | −205.9 °C | 0 mAh | 3240 mAh | 3/6 |

Family A describes a charged battery that is nearly full at 4.33 V. Family B
describes a battery at 3161 % state of charge and −205.9 °C. The selection is a
measurement, not an assumption: `pl=6/6` versus `pl=3/6`.

## Read-only contract

Only the standard-command register address byte plus a 2-byte little-endian read
appear on the bus. No `CONTROL`, `CONFIG`, unseal, or data-flash write was ever
issued. Clock voting (GCC `BIT20 | BIT21 | BIT24`) and the two pin CTLs are
controller bring-up, exactly as the device-verified stage-4 touch driver does.

## The three inconclusive runs, and why they are recorded as such

The first three flashes produced **zero** gauge lines. That was not a driver
failure — it was a collection artifact:

| payload | watchdog deadline | first ring overwrite | gauge lines found |
|---|---|---|---|
| `gaugeprobe` | 900 s | 101.2 s | 0 |
| `gaugeprobe240` | 240 s | 87.7 s | 0 |
| `gaugeprobe60` | 60 s | 58.8 s | 0 |

The 128 KiB `msgbuf` ring is filled by early boot output at 1300–3000 B/s, so
everything before ~59 s is overwritten regardless of the watchdog deadline. The
probe's output all happens before t < 8 s, so it was collected and then
discarded.

The fix was not a shorter deadline (that route was measured to be exhausted) but
**replaying the cached verdict every 1 Hz** so the newest copy always sits inside
the ring. After that the ring held 480 gauge lines across ticks 420–899.

## Two independent analyses

| path | script | result |
|---|---|---|
| decoded log files | `analyze_gaugereplay_device_logs.py` | `read_only_probe_device_verified: true`, `map_a_all_plausible: true` |
| raw window bytes + serial watcher | `analyze_gaugereplay_serial.py` | `read_only_probe_device_verified: true`, same numbers |

`GA1`/`GA2` stage reports were not recovered (they precede the ring's reachable
window); that reduces the diagnostic detail available but does not affect the
verdict, which is the `rc=0` line itself.

## Honest boundaries

- **Read-only.** No charger state, no `IOPMPowerSource` publication, no OS power
  source service. Nothing inside iOS consumes the reading yet.
- **One boot, one unit, one charge level.** This proves the register protocol
  and the transport; it does not prove accuracy across charge cycles.
- **Audio remains blocked.** The ADSP firmware set is absent, and the sheng
  SM8550 AudioReach/GPR + SoundWire stack cannot be reused for this board's
  SLIMBUS WCD9340 + Q6AFE.

## Reproduction

```
python scripts/check-enchilada-gauge-probe-host.py \
    --cc oneplus6-ios/tools/llvm-mingw/bin/clang.exe \
    --cxx oneplus6-ios/tools/llvm-mingw/bin/clang++.exe
python -X utf8 oneplus6-ios/scripts/test-cycle.py gaugereplay --wait-return 400
python -X utf8 oneplus6-ios/work/tmp/collect_gaugereplay.py
python -X utf8 oneplus6-ios/work/tmp/analyze_gaugereplay_device_logs.py
python -X utf8 oneplus6-ios/work/tmp/analyze_gaugereplay_serial.py
```
