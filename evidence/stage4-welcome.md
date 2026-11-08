# Stage 4 input device evidence: welcome-screen re-test (backlight + touch in one boot)

- **Date:** 2026-11-08 (Asia/Shanghai)
- **Device / OS profile:** OnePlus 6 (enchilada, SDM845), iPadOS 17.7.11 (21H461, iPad7,1)
- **Shipped kernel source commit:** `23d2428b69d52817fb4d3972f62c805d3d8c8823` (CI #211 built this; `794cead5` and `4bd917b8` after it are record/evidence-only, no kernel code change).
- **Record commit at flash time (device repo main):** `4bd917b8a6d4d7c64bafac286b3e1a5be26d8f87`
- **CI build:** `keyfan/unixnu-build` run #42451762272 (macOS 26 / Xcode 26.6), `completed success`, `archive-20-build-enchilada-msm-dsi.log`: `PASS: libEnchiladaMsmDsi.a (8 objects, sha256 f9da0b7cd36d30af)`, 0 `error:` across 20 archive logs.
- **Artifact kernel:** sha256 `a40c2c4e83f6be515c7467ceafe07b403a5dd4ef3a5c1a95d359a6b48fd8ede3` (matches `payload.json kernel_sha256`; prelinked inner image differs by design). Reused from CI #211 — no new CI needed.
- **Flashed image:** `work/out/payload-stage4welcome.img` sha256 `b0033261680f6aa715ddd18a1f7003d8379cdbd6451c66608a9d1f18d88bbc02`, 150994944 B; no-reset probe kernel sha `9b26ab1ebe79c6da`; `/chosen/boot-manifest-hash` injected into `kernel.macho.adt`.
- **boot-arg gates (49 items, 995/1000 B):** `enchilada-backlight=4`, `enchilada-touch=4`, `sheng-touch-hid=1`, `enchilada-touch-swipe=480`, `sheng-gpu-addon=1`, `enchilada-deadline=900`, `sheng-data-reset=1`, `enchilada-wdog=3` (auto-return). Derived from the device-verified `payload-dataswipe.img` arg string: cosmetic `sheng-no-mac-label=1` dropped, `enchilada-backlight=4` added.
- **Flash / collection:** `python -X utf8 oneplus6-ios/scripts/test-cycle.py stage4welcome --wait-return 900` returned `CYCLE_EXIT=1` **only** because the inherited `enchilada-deadline=900` made the watchdog auto-return land at ~941 s, just beyond the 900 s `wait_for` window; the phone did return to recovery on its own (`adb devices` -> `27f4f45d recovery`). The log window was then collected **READ-ONLY** (no reboot, no reflash) with `collect_stage4welcome.py`: `uid=0(root)`, `dd bs=4096 skip=13207739 count=8192` -> `window-stage4welcome.bin` 33554432 B, decoded by `read-enchilada-log.py --decode`.
- **Boot window:** `boot_id=548302479`, `uptime_s=898`, `slots_written=1737`, `rounds=1737`, `write_errors=0`, `state=2`, `msgbuf=131072 B`.

## Why this test exists

The input area `next_action` (before this test) said: *"Once the DCS backlight is in place, re-run a present/welcome-screen test to confirm whether the SpringBoard/CoreImage crash still blocks the first-run UI."* Stage 3d-beta (CI #211 + `backlight211mode4`) satisfied the DCS-backlight prerequisite, so this is that re-test: the dataswipe touch chain **plus** the DCS backlight service, in one boot.

Two independent log analyses before flashing (`analyze_input_retrospective.py`, `analyze_touch_liveness.py` + `analyze_crash_signatures.py`) established: `backlight211mode4` carried `enchilada-touch=4` but **no** `enchilada-touch-swipe`, so its `[touch edge] installed=0` / `touch-reports=0` was a boot-arg configuration difference, not a broken driver; `payload-dataswipe.img` carried the full swipe chain. The candidate args combine them.

## Device observation (decoded `latest.txt`, `boot_id 548302479`)

Backlight control plane (`BootLog.c:749` L row, `MsmDsiControl.c:81-97`):

- **510 L rows, all `st=7`**; first and last verbatim: `L st=7 req=0 lvl=512 a=0 b=512 xerr=0 irq=1 ea=3ff eb=200 err=0`
- stage 7 = Published, `impersonated=1`, `max=0x3ff` (1023, the enchilada DCS range, **not** sheng 2047), `req=0`.

Input chain (same boot):

- `[touch edge] installed=1 contacts=4 from_edge=4 skipped=0`
- `TOUCH HID started=1 driver=0 touch-reports=337 result=0 key-reports=0 | pad dots=0`
- `[touch] strokes=4 extent x 1522..1522 y 0..2031 (px of 3048x2032)`
- `FT5 hid up1 chk897 rep337 res0 n337 edge1/4/4 svc897 open1 eso1 uc1 tdm- pub1 mapL1080x2280`

Process / crash rows:

- `SpringBoard p27 n15 r0 w15 b0 t1 s1/47 ...`, `backboardd p56 n18 ...`, `STKS#2 x1 p27(SpringBoard) ...`, `STKB#13 x1 p56(backboardd) ...` — both alive with backtraces.
- `Setup` process lines: **0** (no `Setup` row anywhere in `latest.txt`).
- `PROC pass449 seen10 worker2 attempt449 busy0 age0s; trace phase0 index15; EXIT 483 nonzero105 crashsig17 drop9`
- All CRASH rows are `CRASH p397 PerfPowerServices sig6 PC1d98d6f74 LR1eb770fa4 ...` (SIGABRT). `CoreImage` / `GLContext` / `mach_vm_allocate_kernel` / `EXC_BAD_ACCESS` literal hits across 244 decoded logs: **0**; the wide `panic`=170 hits are benign `DumpPanic` process-name / ustate container path noise.

## Controls (each is a separate boot)

| control boot | observed L stages | note |
| --- | --- | --- |
| `console206` | st=0 | all-zero weak stub |
| `backlight208` | st=3 | read-only DSI0 snapshot |
| `backlight209mode2` | st=4 | SMMU Bypass |
| `backlight210mode3` | st=5 | first DCS write |
| `backlight211mode4` | st=7 | IOService published, NO swipe arg -> `[touch edge] installed=0`, `touch-reports=0` |
| `dataswipe` | (no backlight gate) | `installed=1`, `touch-reports=84`, `strokes=1`, `Setup 2/0@299` present |
| **`stage4welcome`** | **st=7** | **installed=1, touch-reports=337, strokes=4 — backlight + touch live together** |

boot freshness: this `boot_id 548302479` differs from all five L-row controls; header `write_errors=0`.

## Interpretation

- The stage 3 display service (st=7 Published, max=1023) and the stage 4 RMI4/HID touch chain (edge wrapper installed, 337 HID reports delivered, 4 full-height strokes) are **both live in the same boot**. This is the strict-order stage-4 re-test that the old `next_action` asked for.
- `installed=1 contacts=4 from_edge=4` here vs `installed=0` in `backlight211mode4` confirms the causal chain: `EnchiladaTouchProbe.cpp:795` needs `enchilada-touch=4` (present in both), and `playSwipe()` / `edge::install()` only fire when `enchilada-touch-swipe` is set (present here, absent in mode 4). The mode-4 zero was a boot-arg difference, **not** a driver regression.
- **`req=0`**: no `setProperties()` arrived from userspace, so CoreBrightness/backboardd acceptance of the published `IODisplayParameters.brightness` remains **UNCONFIRMED** (same honest limit as `evidence/backlight211mode4.md`).
- **No `Setup` process line and no optical readback**: whether the first-run Setup Assistant welcome screen actually advanced is **UNCONFIRMED**. We cannot claim the UI moved forward.
- **CoreImage crash correction**: the earlier input `findings` claimed Setup Assistant was blocked by "SpringBoard crashing in CoreImage GLContext creation (mach_vm_allocate_kernel failure)". A scan of 244 decoded `latest.txt`/`msgbuf.txt` files found **zero** literal hits for `CoreImage`, `GLContext`, `mach_vm_allocate_kernel`, or `EXC_BAD_ACCESS`; the only real CRASH rows are `PerfPowerServices sig6`. That claim was an external inference (screen photo / other channel), **not reproduced by the decoded boot logs** — so this evidence neither confirms nor disproves it, and the record must not present it as a reproduced or resolved fact.

## Proves

- CI #211 kernel (real 21H461 / macOS-Xcode headers) booted far enough to run the 1 Hz `EnchiladaBacklight` service AND the `EnchiladaTouchProbe` synthetic swipe in one payload; both diagnostic planes are live `native_backlight_stats[10]` and `native_hid_input_*` counters, not weak stubs.
- The DCS backlight control plane publishes the correct 0..1023 range on device while the touch chain simultaneously delivers HID reports.
- `enchilada-touch=4` + `enchilada-touch-swipe=480` + `sheng-touch-hid=1` + `sheng-gpu-addon=1` together reproduce the device-verified dataswipe input path with the DCS backlight added, within the 1000 B kernel-arg limit.

## Does NOT prove

- **First-run UI advance / optical brightness change**: no `Setup` row, no framebuffer/optical readback in this harness.
- **CoreBrightness userspace acceptance**: `req=0`, no `setProperties()` captured.
- **The SpringBoard CoreImage GLContext crash**: no literal log evidence; actual CRASH rows are `PerfPowerServices sig6`. The earlier finding is an unverified external inference.
- **ptrauth branch selection**: decided by the CI SDK; both branches were syntax-checked locally.

## Verification chain (two independent verifications at each gate)

1. **Pre-build** (before flashing): `verify_stage4_prebuild.py` (re-derive candidate args from two on-disk images, 995/1000 B budget + 8 gates, CI #211 artifacts + kernel sha) + `argbudget_stage4welcome.py` / `preflash_check_stage4welcome.py` (gate uniqueness, `enchilada-wdog=3` auto-return, misc armed, image inner kernel sha == noreset kernel sha). All PASS.
2. **Parser smoke test**: `smoke_stage4_parsers.py` decodes the dataswipe baseline (`installed=1`, `touch-reports=84`, `strokes=1`, `Setup lines=536`) -> PASS, proving the verification regexes.
3. **Device**: `verify_device_stage4welcome.py` (L-row decode, edge/HID/stroke decode, controls, header, crash honesty) -> `VERDICT: PASS` + `verify_device_stage4welcome_independent.py` (remote source format strings from `BootLog.c` + `BootProgress.c`, raw header fields, FT5 counters, flashed-image gates, boot freshness, crash literals) -> `INDEPENDENT VERDICT: PASS`.
4. **Facts**: `_collect_stage4welcome_data.py` -> `work/tmp/stage4welcome_facts.json` (structured evidence input, no network/device access).
