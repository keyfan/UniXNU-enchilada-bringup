# Stage 1 — appSMMU read-only survey (HOST-VERIFIED observation core)

OnePlus 6 / enchilada / SDM845, iPadOS 17.7.11 (21H461). Strict unixnu.com
8-stage driver order, stage 1 = CPU / interrupt controller / timer / RAM / DMA.

**This is an observation, NOT a translation driver.** It reads the SMMUv2
identification registers and every stream-match slot, and reports what it finds.
It does not program a stage-1 context, does not enable a context bank, does not
write SMR or S2CR, loads no firmware, and cannot recover a synchronous bus
abort. It also does **not** mean the `ArmSmmu4K` kit is linked or that this port
owns translation.

## Why this module exists

`porting.toml`'s memory area says, in `limits[0]`, exactly what was missing:

> ArmSmmu4K is listed as the intended component but the kit is NOT yet linked;
> the current implementation reuses the firmware bypass slots.

That is still true of translation. But the honest first step is not to seize the
SMMU — it is to *observe* what the firmware and UEFI actually left in place, and
to turn the investigation into code that a device boot can prove. A survey has
three properties that make it the right first step:

1. it cannot brick the boot: every access is a read of an APB register inside a
   window this port already maps for UFS and DWC3;
2. it produces evidence that is directly comparable to what the two existing
   arms of this port already assert (`drivers/NativeUfs/SmmuTakeover.h:141-148`
   and `drivers/NativeDwc3/Dwc3SmmuPlan.h:44-129`);
3. it is testable on the host with a fake register file, so the decision logic is
   verified before it ever touches hardware.

## Register map (three sources in the tree, all agreeing)

| Register | Offset | Corroborating source |
| --- | --- | --- |
| `sCR0` | `0x000` | `drivers/NativeUfs/SmmuTakeover.h` firmware check |
| `sIDR0` | `0x020` | `drivers/NativeUfs/NativeUfs.cpp:873` (`inspectSmmu`) |
| `sIDR1` | `0x024` | `drivers/NativeUfs/NativeUfs.cpp:913` (bank/page geometry) |
| `sIDR2` | `0x028` | `drivers/NativeDwc3/Dwc3SmmuPlan.h:47` |
| `sGFSR` | `0x048` | `drivers/NativeUfs/NativeUfs.cpp:874` |
| `SMR(n)` | `0x800 + 4n` | `drivers/NativeUfs/NativeUfs.cpp:882` |
| `S2CR(n)` | `0xc00 + 4n` | `drivers/NativeUfs/NativeUfs.cpp:883` |

Window: `apps_smmu 0x15000000 size 0x80000`, stated twice in this tree —
`drivers/NativeDwc3/Dwc3Registers.h:35-36` (`kAppsSmmuBase`/`kAppsSmmuSize`) and
`drivers/NativeUfs/NativeUfs.cpp:432` (`mapPhys(0x15000000ULL, ...)`).

Streams surveyed: UFS `0x60` (console206 reported it at slot 6 / bank 4) and
DWC3 `0x740` (`<&apps_smmu 0x740 0>`, observed SMR `0x80000740`).

## What it refuses

The geometry is derived, never assumed, and anything this device has not already
demonstrated is refused rather than guessed:

* `sIDR1[31]` set means 64 KiB register padding — refused. The rule comes
  verbatim from `drivers/NativeUfs/tests/SmmuTakeover.cpp:33`: *"Never guess 64K
  register geometry."*
* zero slots, zero banks, more than 128 of either, or `(global_pages + banks) *
  4096` exceeding the `0x80000` window — refused.

On the SDM845 values this port has seen (`sIDR0 0x40000080`, `sIDR1 0x50000040`,
`sIDR2 0x00005111`, the same constants `SmmuTakeover.cpp:93` uses) the derived
geometry is 128 slots, 64 banks, 64 global pages (`1 << (5 + 1)`, the identical
expression as `NativeUfs.cpp:915`), stride 4096.

## Read-only contract

The `struct smmu_survey_io` the core consumes has **only** `read32`. There is no
write member to call, and `SmmuSurvey.c` contains no store into the mapping.
`work/tmp/verify_smmu_survey_2.py` asserts both properties from the API shape and
the implementation text, then compiles the core with `-fno-builtin` at `-O0` and
`-O2` to prove it needs no libc.

A free slot's S2CR is never read: the SMR-valid test gates it, which keeps the
bus traffic minimal and makes the read count an assertion (`128 SMR + one S2CR
per valid entry`).

## Host verification (two independent methods)

```sh
python scripts/check-enchilada-smmu-host.py --cc <clang> --cxx <clang++> --out <dir>
python work/tmp/verify_smmu_survey_2.py
```

* Method 1 — `scripts/check-enchilada-smmu-host.py`: compiles the C core as C11
  and drives it from C++17 with a fake register file (13 test groups, sanitized
  when the sanitizers are available).
* Method 2 — `work/tmp/verify_smmu_survey_2.py`: re-derives every offset, window
  and stream id from *other* files in the tree instead of trusting the constants
  under test, proves read-only from the API shape, compiles freestanding with
  `-fno-builtin`, and links its own harness with a translation-enabled fixture
  (the opposite classification to method 1's bypass fixture).

Both expected output lines in this README are the ones the runs actually printed:

```
PASS: enchilada apps_smmu read-only survey (13 test groups)
SS slots=128 banks=64 pages=64 exids=0 valid=2 t0=0 t1=1 t2=1 scr0=00000406 gfsr=00000000 ufs=12/9/translation usb=40/11/fault
```

## Kernel side (default off)

`package/EnchiladaSmmuProbe.cpp` is the thin shim: it maps the window once with
`armcpu_map_physical` (kept at file scope forever — the DWC3 bug where a
function-local `OSSharedPtr<IOMemoryMap>` was destroyed on return), runs the
survey exactly once, and caches the verdict.

It then **re-emits that cached text once per second**. That is not cosmetic: the
kernel msgbuf is a 128 KB ring that keeps only the newest bytes and, measured on
device across three single-variable boots, saturated at 101.2 s / 87.7 s / 58.8 s
(`work/tmp/GAUGE-DIAGNOSIS-3.md`). A verdict logged once at t<8 s is therefore
always overwritten before the log can be collected. Replay performs no MMIO, so
the measurement still happens exactly once; replays are labelled
`(replay t=<tick> attempt=<n>)`.

Boot argument: `enchilada-smmu=1` (absent or any other value = no-op).

Log line (`IOLog`, lands in `msgbuf.txt`):

```
enchilada-smmu: rc=0 SS slots=128 banks=64 pages=64 exids=0 valid=N t0=… t1=… t2=… scr0=… gfsr=… ufs=<slot>/<bank>/<type> usb=<slot>/<bank>/<type>
```

`<type>` is `bypass`, `translation`, `fault` or `reserved`, and an unmatched
stream prints `none` rather than `0/0`.

## Status

HOST-VERIFIED decision core + wiring. **Not device-verified**: no CI kernel has
been built with this archive yet and no device boot has reported the `SS` line.
The claims in `porting.toml` must not be promoted past `host-verified` until a
device log shows that line.
