# NativeDsiBacklight — enchilada stage 3, layer 1 (DCS packet transport)

OnePlus 6 (enchilada, SDM845) drives its Samsung `sofef00` AMOLED backlight with a
**DSI DCS command**, not an I2C backlight IC. Evidence, checked twice:

- `Mu-Silicium/Resources/DTBs/enchilada.dts:2613` node
  `qcom,mdss_dsi_samsung_sofef00_m_cmd`, `:2661`
  `qcom,mdss-dsi-bl-pmic-control-type = "bl_ctrl_dcs";`, `:2662`
  `qcom,mdss-brightness-max-level = <0x3ff>;`, `:2663` `bl-min-level = <0x01>`.
  (The `bl_ctrl_wled` / `bl_ctrl_pwm` hits in that file belong to *other* panel
  variants in the 17-entry multi-panel table.)
- Linux v6.12 `drivers/gpu/drm/panel/panel-samsung-sofef00.c`:
  `sofef00_create_backlight()` uses max 1023 and
  `sofef00_panel_bl_update_status()` calls
  `mipi_dsi_dcs_set_display_brightness_large()`.
- Official https://unixnu.com/zh/drivers/sheng-backlight: DSI-command or PWM
  backlights need their own transport under the OS brightness service. sheng's
  two KTZ8866 I2C controllers are **not** applicable to this board.

This directory is intentionally only the **wire-format layer**. `DsiDcs.c` is
register-independent pure C (BSD-2-Clause-Patent, like `NativeGeniI2c/GeniI2c.c`),
so it can be compiled and asserted on the host before the two ABI-sensitive pieces
exist:

1. **layer 2**: an MDSS DSI0 command-channel driver for `dsi_ctrl0 @ 0xae94000`
   (no such host driver exists anywhere in the enchilada/fajita trees), plus a
   `[kernel] archives` build recipe;
2. **layer 3**: the `AppleARMBacklight`-facing IOService (sheng's
   `ShengBacklight : IOService` shape: `IODisplayParameters.brightness`,
   `backlight-control`, `NativeSandboxGrantProcess=backboardd`, a 16 ms fade work
   loop, late-object linkage) with the DCS transport instead of KTZ8866 I2C.

## Byte rules (from Linux v6.12 `drm_mipi_dsi.c` / `include/video/mipi_display.h`)

| Rule | Value |
|---|---|
| `mipi_dsi_dcs_write_buffer()` len 1 / 2 / >=3 | `0x05` / `0x15` / `0x39` |
| `mipi_dsi_dcs_set_display_brightness_large(b)` | payload `{b>>8, b&0xff}` (big-endian) |
| resulting tx buffer | `[0x51, hi, lo]` → DCS long write `0x39`, word count 3 |
| `mipi_dsi_create_packet()` header[0] | `((channel&3)<<6) \| (type&0x3f)` |
| long header[1..2] | word count LE (`tx_len`, then `tx_len>>8`) |
| short header[1..2] | up to two parameter bytes |
| header[3] (ECC) | left zero; DSI host hardware fills it |

**Do not** use the deprecated non-large `mipi_dsi_dcs_set_display_brightness()`:
it encodes `{b&0xff, b>>8}`, the reverse byte order.

Panel-on sequence additionally needs `WRITE_CONTROL_DISPLAY 0x53 / 0x20`
(enable brightness control block bit5) and `WRITE_POWER_SAVE 0x55 / 0x00` — both
`0x15` short writes, provided here by `dsi_dcs_write_control_display()` and
`dsi_dcs_write_power_save()`.

## Host test

```
python scripts/check-enchilada-dsi-backlight-host.py \
  --cc tools/llvm-mingw/bin/clang.exe --cxx tools/llvm-mingw/bin/clang++.exe \
  --out work/tmp/dsi-host-out
```

Set `TMP`/`TEMP` to a writable workspace dir first (clang cannot write to
`AppData\Local\Temp` under this harness). It compiles `DsiDcs.c` as C11 and
`tests/dsi-dcs-test.cpp` as C++17 with `-Wall -Wextra -Werror` and the
address/UB sanitizers when available, then asserts exact wire bytes. Verified
output: `PASS: enchilada DSI DCS backlight transport (7 test groups)`.

Layer-1 status in `porting.toml`: `display` area stays `implemented`; the DCS
transport is recorded under findings/evidence. It is **not** yet linked into the
kernel (`[kernel] archives`) — that is layer 2.
