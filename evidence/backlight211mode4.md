# Layer 3d-beta device evidence: `EnchiladaBacklight : IOService` (mode 4)

- **Date:** 2026-11-08 (Asia/Shanghai)
- **Device / OS profile:** OnePlus 6 (enchilada, SDM845), iPadOS 17.7.11 (21H461, iPad7,1)
- **Source commit (device repo main):** `23d2428b69d52817fb4d3972f62c805d3d8c8823`
- **CI build:** `keyfan/unixnu-build` run #42451762272 (macOS 26 / Xcode 26.6), `JOB build completed success`, `archive-20-build-enchilada-msm-dsi.log`: `PASS: libEnchiladaMsmDsi.a (8 objects, sha256 f9da0b7cd36d30af)`, 0 `error:` across 20 archive logs.
- **Artifact kernel:** `kernel.macho` sha256 `a40c2c4e83f6be515c7467ceafe07b403a5dd4ef3a5c1a95d359a6b48fd8ede3` (matches `payload.json kernel_sha256`; raw artifact, prelinked inner image differs by design).
- **Flashed image:** `work/out/payload-backlight211-mode4.img` sha256 `b27c6d11bf347259f4aa0b4fdcaebef1865ea220b8d9dd89f7476ed4a597a701`, 150994944 B; no-reset probe kernel sha `9b26ab1ebe79c6da`; `/chosen/boot-manifest-hash` injected into `kernel.macho.adt`.
- **boot-arg gate:** `enchilada-backlight=4` (49 items, 997/1000 B); auto-return safety `enchilada-wdog=3` + `misc` armed; `sheng-gpu-addon=1` dropped.
- **Flash command:** `python -X utf8 oneplus6-ios/scripts/test-cycle.py backlight211mode4 --wait-return 900`
- **Boot window:** `boot_id=549546031`, `uptime_s=599`, `slots_written=1163`, `rounds=1163`, `write_errors=0`.

## What mode 4 adds

`enchilada-backlight=4` runs `EnchiladaBacklight : IOService` (a THIN OS-facing
service, deliberately NOT named `AppleARMBacklight` — that metaclass already
exists in `AppleARMPlatform` and a duplicate registration panics with int-229).
On the first 1 Hz `enchilada_backlight_poll()` tick it performs
`OSMetaClass::preModLoad` -> `enchilada_backlight_static_init()` (late-objcopy
globalized `__GLOBAL__sub_I_EnchiladaBacklight.cpp`) -> `postModLoad`, allocates
the class, `impersonate()`s the built-in `AppleARMBacklight` metaclass (ptrauth
and non-ptrauth branches), publishes
`IODisplayParameters.brightness {min=0, max=1023, value}` plus
`backlight-control=true` and `NativeSandboxGrantProcess="backboardd"`, then
`registerService()`. `setProperties()` is non-blocking (it only records a goal
via `msm_dsi_control_request`); the DEVICE-VERIFIED DCS transmit path
(`msm_dsi_brightness_send`, mode 3 / CI #210) is called from the 1 Hz tick.

## Device observation

Decoded `L` rows: **510 rows**, distinct stages `[7]`, 1 distinct rows.

Verbatim first row: `L st=7 req=0 lvl=512 a=0 b=512 xerr=0 irq=1 ea=3ff eb=200 err=0`
Verbatim last row: `L st=7 req=0 lvl=512 a=0 b=512 xerr=0 irq=1 ea=3ff eb=200 err=0`

Field layout (from `MsmDsiControl.c:81-97`, `BootLog.c:749`
`"L st=%d req=%d lvl=%d a=%d b=%d xerr=%d irq=%x ea=%x eb=%x err=%d"`):

| slot | field | value (final L row) |
| --- | --- | --- |
| [0] | stage | 7 |
| [1] | requests | 0 |
| [2] | level | 512 |
| [3] | steps | 0 |
| [4] | to | 512 |
| [5] | last_rc | 0 |
| [6] | impersonated | 0x1 |
| [7] | max level | 0x3ff |
| [8] | from | 0x200 |
| [9] | period_ms | 0 |


Control-plane comparison (each earlier stage is a separate device boot):

| control boot | observed L stages |
| --- | --- |
| `console206` | st=0 |
| `backlight208` | st=3 |
| `backlight209mode2` | st=4 |
| `backlight210mode3` | st=5 |

## Interpretation

- the service reached **stage 7 (Published)**: `registerService()` ran after `impersonate()`
- `impersonate()` succeeded on device (`impersonated=1`), i.e. the built-in `AppleARMBacklight` metaclass was found and the service answers its conformance query
- published `IODisplayParameters.brightness.max = 0x3ff (1023)`, the enchilada DCS range — **not** sheng's 2047
- **no `setProperties()` requests were observed (`req=0`)** — userspace acceptance (CoreBrightness/backboardd) is therefore **UNCONFIRMED**; the service published correctly but no brightness change was driven from userspace

## Proves

- CI #211 compiled the IOService class with the real 21H461/macOS-Xcode XNU headers, force-loaded all 8 archive objects, and linked every layer-3d-beta symbol into the shipped kernel (two independent artifact verifications: hash + ASCII symbol scan).
- On device the mode-4 gated service booted XNU far enough to emit the control-plane `L` rows through the USB CDC-ACM serial console; the fields above are the live `native_backlight_stats[10]` values, not a weak stub (a weak stand-in would be all zero / `st=0`).
- The 0..1023 clamp and published `max=1023` (not sheng's 2047) are present on device in the `ea` field.

## Does NOT prove

- **Real brightness change / panel luminance:** there is no optical readback in this harness; `lvl` and `to` are the service's own level counters. A successful `msm_dsi_brightness_send()` in mode 3 (CI #210, st=5) proved the first DCS packet reached the DSI0 command channel; mode 4 layers an OS control plane on top of that same path.
- **CoreBrightness userspace acceptance** unless `req > 0` is reported above: `req=0` means no `setProperties()` call arrived, so the closed-source client behavior remains UNCONFIRMED.
- **ptrauth branch:** which of `#if __has_feature(ptrauth_calls)` / `#else` compiled is decided by the CI SDK; both were syntax-checked locally.

## Verification chain (two independent verifications at each gate)

1. Host: `verify_layer3d_beta.py` (non-ptrauth + ptrauth `-fsyntax-only -Wdeprecated-volatile -Werror`, host battery, checker, guard, LF) + `verify_layer3d_beta_independent.py` (remote-baseline `pollMode1/2/3` byte-identical, no blocking calls in `setProperties`, `text_bytes` roundtrip).
2. CI: run #42451762272 `completed success`, hash + 14-symbol link scan, 0 archive errors.
3. Pre-flash: `preflash_check_mode4.py` (gate/wdog/misc/kernel sha) + `_argbudget_mode4.py` (997/1000 B).
4. Device: `verify_device_mode4.py` (L-row decode, stages, impersonated, max=0x3ff, controls) + `verify_device_mode4_independent.py` (re-derives field layout from the exact source revision, boot freshness, monotonicity).
