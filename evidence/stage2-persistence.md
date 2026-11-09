# Stage 2 storage — Data survives an unmodified reboot

Device: OnePlus 6 (enchilada), SDM845, `ipados-17.7.11-21H461`.

This closes the open item that `porting.toml` carried in `storage.limits[0]`:
"the survival of user writes across an unmodified reboot has not been
demonstrated in this configuration." Two boots with the same payload are the
demonstration.

## The change under test

One boot argument, nothing else. The kernel is byte-identical to the one the
appSMMU run (flash #7) already booted, so the kernel is not a variable.

| | flash #7 (already device-verified) | this run |
|---|---|---|
| args | 49 tokens, 986 B | 48 tokens, 967 B |
| `sheng-data-persist=2` | present | present |
| `sheng-data-reset=1` | **present** | **removed** |
| kernel | CI #215 + `--no-reset` | same bytes (`d4c46fcd1ed751a8`) |

`sheng-data-reset` is read by `drivers/NativeRamBlock/NativeRamBlock.cpp:86`:

```c
static bool discardRequested (void)
{
  uint32_t v = 0;
  return PE_parse_boot_argn("sheng-data-reset", &v, sizeof v) && (v == 1 || v == 2);
}
```

so removing the token makes `discardRequested()` false. `sheng-data-persist`
reaches the store through `drivers/NativeUfs/NativeUfs.cpp:1309`:

```c
static unsigned storeMode (void)
{
  static int cached = -1;
  if (cached < 0) {
    uint32_t value = 0;
    if (!PE_parse_boot_argn ("sheng-data-persist", &value, sizeof (value)) || value > 2) value = 0;
    cached = static_cast<int> (value);
  }
  return static_cast<unsigned> (cached);
}
```

Mode 2 is what opens the overlay read-write; the decision itself is
`PersistOverlay.h`'s `open()`, where

```c
if (header == 0) { ... return Open::Resumed; }
if (header == 0 && reset && !ro) header = 3;
...
if (!reset) return Open::Kept;
```

means: a valid header for this (pages, 32-byte seed) identity plus `reset ==
false` resumes the store, and `reset == true` forcibly downgrades the header so
the store is rebuilt.

## Two boots, same payload

Both runs used `payload-persist.img`
(sha256 `b037538b0c84ab0fc782829a38bdf0d458cfff31e044e2fcbd495538c93a1abb`),
built by `_build_persist_payload.py` and gated by `preflash_check_persist.py`
(25 checks) and `verify_persist_prebuild_2.py`.

| | boot 1 | boot 2 |
|---|---|---|
| boot_id | `553634628` | `552252012` |
| uptime | 899 s | 899 s |
| `D st=` | **2 (resumed)** | **2 (resumed)** |
| `hdr=` | 0 (valid, this seed) | 0 (valid, this seed) |
| `pres=` | 319245 pages | 320399 pages |
| `wr=` | 141358 | 29592 |
| `cm=` | 19991 | 7346 |
| `er=` | 0 | 0 |
| `pages=` | 4194304 | 4194304 |

Verbatim rows:

```
D st=2 hdr=0 gpt=0/0 lba=9e177a pres=319245 ld=32057 wr=141358 cm=19991 er=0 pages=4194304
D st=2 hdr=0 gpt=0/0 lba=9e177a pres=320399 ld=29655 wr=29592 cm=7346 er=0 pages=4194304
```

and the independent BootReset readback on each:

```
data state=2 hdr=0 present=319245 writes=141358 errors=0 flushes=494
data state=2 hdr=0 present=320399 writes=29592 errors=0 flushes=428
```

## What each observation rules out

* **`st=2` rather than `st=3`** — the store was not rebuilt. `st=3` (fresh) is
  exactly what every previous run printed, because the reset flag was in the
  args; `st=3` here would mean the flag was still in effect.
* **`hdr=0` rather than `hdr=3`** — the header validated against this
  installation's `(pages, seed)` identity. `hdr=3` is the downgraded state, and
  `st=7` (kept) would have meant the store belonged to a *different* seed, which
  `evidence/stage2-...` would have had to report as a failure rather than a
  resume.
* **`pres=319245` on the first boot, before this run had written anything** — the
  pages came from the previous boot (#7) which wrote 322365 times and left
  `hdr=3` behind. This is the actual persistence claim: content authored in one
  boot is visible in the next.
* **`pres` grew 319245 → 320399 (+1154 pages)** — boot 1's own dirty pages also
  came back, and are visible on boot 2. Growth rather than shrinkage also rules
  out a torn or partially written bitmap.
* **`pages=4194304` on both boots** — the store identity includes the page count;
  an identical count is the observable half of "same store", the other half
  being the 32-byte seed that `checkHeader` compares and that this run cannot
  read out.
* **`er=0` on both boots and `write_errors=0` in both ring headers** — none of
  this is a degraded path (`st=5`).

## Read-only / safety

The two runs only add and remove one boot argument; no driver code changed, no
store layout changed, and no medium was erased. `enchilda-wdog=3` plus the armed
`boot-recovery` BCB made both boots end by themselves after ~900 s
(`enchilada-deadline=900`), and both ended in recovery, not fastboot
(`gpt=0/0`, `state=2`).

## How this was checked twice

Two independent analyses, on disjoint data paths:

* `analyze_persist_device_logs.py` (method A) parses the decoded `latest.txt`
  per boot dir and orders boots by file mtime. PASS.
* `analyze_persist_serial.py` (method B) scans the raw 32 MB window
  (`window-persist.bin`, 509 `D` rows found in raw bytes) and the per-second
  `slots.txt`, never reading `latest.txt`'s `D` row. PASS.

Both agree: `st=2` on both boots, same page count, `present` non-decreasing,
zero errors.

Method B also documents a measurement detail worth keeping: the 128 KB message
ring saturates within 58.8–101.2 s of boot, so `msgbuf.txt` no longer contains
the `D` row at all (`per-boot rows from msgbuf: 0`). The slot file keeps one
entry per second for the whole boot and still carries 509 copies, which is why
the persistence verdict must be read from `slots.txt`/the raw window.

## Honest boundaries

* What is demonstrated is that the *overlay* for this seed survived and was
  resumed: page content authored in one boot is readable in the next.
* It is **not** a demonstration that a userland file written through APFS
  round-trips unchanged; that needs a filesystem-level write/read across the
  two boots, which this run did not perform.
* The store lives inside the reserved window below the root image
  (`NativeUfs.cpp` `native_ufs_store_open`, 1024 MiB default reserve); nothing
  here says anything about surviving a *reflash*, which rewrites `system_b`.
* `t1`/`t2` style translation counters are not part of this claim; this is
  storage, not SMMU.

## Reproduce

```
python -X utf8 oneplus6-ios/work/tmp/_prep_persist_args.py
python -X utf8 oneplus6-ios/work/tmp/_build_persist_payload.py
python -X utf8 oneplus6-ios/work/tmp/preflash_check_persist.py
python -X utf8 oneplus6-ios/work/tmp/verify_persist_prebuild_2.py
python -X utf8 oneplus6-ios/scripts/test-cycle.py persist --wait-return 1000
python -X utf8 oneplus6-ios/scripts/test-cycle.py persist --wait-return 1000
python -X utf8 oneplus6-ios/work/tmp/analyze_persist_device_logs.py
python -X utf8 oneplus6-ios/work/tmp/analyze_persist_serial.py
python -X utf8 oneplus6-ios/work/tmp/_record_persist_facts.py
```
