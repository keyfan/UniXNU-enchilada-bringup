# NativeMsmDsi — enchilada stage 3, layer 2 (MDSS DSI0 command channel)

This is the register-level transmit path that turns a stage-3 layer-1 DCS packet
(`drivers/NativeDsiBacklight/DsiDcs.h`) into the MSM-specific CMD DMA buffer and
pushes it out over `dsi_ctrl0` (`0xae94000`) on the OnePlus 6 (SDM845). It is pure C
over an injected `struct msm_dsi_io` register bus (the `NativeGeniI2c` fake-MMIO
pattern), so it is host-testable locally before the ABI-sensitive layer 3
`AppleARMBacklight` IOService exists.

## Two independent verifications (all facts cross-checked before any code was written)

1. **Vendor DTS** `Mu-Silicium/Resources/DTBs/enchilada.dts`:
   - `:3404 qcom,mdss_dsi_ctrl0@ae94000`, `:3405 compatible = "qcom,dsi-ctrl-hw-v2.2"`,
     `:3408 reg = <0xae94000 0x400>`, `:3409 reg-names = "dsi_ctrl\0disp_cc_base"`;
   - `:2619 qcom,mdss-dsi-panel-type = "dsi_cmd_mode"`,
     `:2620 virtual-channel-id = <0x00>`, `:2631-2634` four lanes,
     `:2635 qcom,mdss-dsi-dma-trigger = "trigger_sw"` (software DMA trigger),
     `:2661 bl-pmic-control-type = "bl_ctrl_dcs"`, `:2662 max 0x3ff`;
   - `enchilada-facts/parsed.txt` cont_splash region `0x9d400000 + 0x2400000` and
     `NativeLegacyDisplay.cpp:562` prove the panel is **already lit** by the UEFI GOP
     framebuffer, so no cold-boot power-on/reset sequence is needed.
2. **Linux v6.12 mainline** (via `cdn.jsdelivr.net/gh/torvalds/linux@v6.12`):
   - `drivers/gpu/drm/msm/dsi/dsi_cfg.c` `sdm845_dsi_cfg`: `.io_start = { { 0xae94000,
     0xae96000 } }`, `.io_offset = DSI_6G_REG_SHIFT` (= 4); `dsi_cfg.h`
     `MSM_DSI_6G_VER_MINOR_V2_2_1 = 0x20020001` (matches `dsi-ctrl-hw-v2.2`);
   - `drivers/gpu/drm/msm/registers/display/dsi.xml` (domain DSI, width 32) register
     offsets; `drivers/gpu/drm/msm/dsi/dsi_host.c` the CMD DMA format and trigger
     sequence; `drivers/gpu/drm/msm/dsi/dsi.h` the host struct.

### The `+4` register shift (the one easy-to-miss detail)

`dsi_get_version()` reads `6G_HW_VERSION` at the **unshifted** base (`0xae94000 +
0x0`) and `REG_DSI_VERSION` at the shifted offset; then `msm_dsi_host_init()` does
`ctrl_base += cfg->io_offset` (= +4) and `dsi_read()` = `readl(ctrl_base + reg)`.
So the runtime MMIO window for all other registers is **`0xae94004`**, and every
`MSM_DSI_REG_*` in `MsmDsiCmd.h` is the `dsi.xml` offset used directly on that
shifted base. `MsmDsiCmd.h:0x0` (`CTRL`) and the XML's `6G_HW_VERSION` both list
offset `0x0` — they are the same physical word read from different bases at
different times, not a collision.

## CMD DMA buffer format (`dsi_host.c:dsi_cmd_dma_add()`, verbatim)

```
data[0] = packet.header[1];   // long: WC low  / short: param 1
data[1] = packet.header[2];   // long: WC high / short: param 2
data[2] = packet.header[0];   // DI = ((channel&3)<<6) | (type&0x3f)
data[3] = BIT(7);             // Last packet
if (long) data[3] |= BIT(6);
memcpy(data + 4, packet.payload, packet.payload_length);
memset(data + packet.size, 0xff, len - packet.size);  // len = (size+3) & ~3
```

`packet.size = 4 + payload_length`, ECC (`header[3]`) is left to hardware. So:

| layer-1 packet | MSM DMA buffer |
|---|---|
| DCS long write `39 03 00 00 51 03 ff` (b=1023, ch0) | `03 00 39 c0 51 03 ff ff` (8 B) |
| DCS long write `b9 03 00 00 51 02 00` (b=512, ch2) | `03 00 b9 c0 51 02 00 ff` (8 B) |
| short `15 53 20 00` (WRITE_CONTROL_DISPLAY 0x20) | `53 20 15 80` (4 B, **no padding**) |
| short `15 55 00 00` (WRITE_POWER_SAVE 0x00) | `55 00 15 80` (4 B) |

## Trigger sequence (`msm_dsi_host_cmd_xfer_commit()` + `xfer_prepare/restore()`)

```
prepare: saved = CTRL;  CTRL |= CMD_MODE_EN | ENABLE
commit:  DMA_BASE = dma_addr;  DMA_LEN = len;  TRIG_DMA = 1;  wmb()
wait:    poll STATUS0.CMD_MODE_DMA_BUSY == 0 with a CPU spin (NOT XNU IODelay/WFE —
         RootCommands.h:22: on enchilada a WFE-based poll never came back)
restore: CTRL = saved   (also on timeout, so CMD_MODE_EN is never left stuck)
```

`dsi_ctrl0` is configured by firmware for command mode with `TRIGGER_SW` DMA, so
re-running the full `dsi_ctrl_enable()` on a live controller is deliberately avoided.
The cold-boot config constants (`CMD_DMA_CTRL`, `TRIG_CTRL`, `CLK_CTRL`,
`CMD_CFG0/1`) are kept in `MsmDsiCmd.h` as reference for the layer-3 runtime probe,
which may need to reassert them after a cold boot.

## Host test (local, two independent verifications)

```
# env: TMP=TEMP=work/tmp/ctmp ; PATH includes tools/llvm-mingw/bin (runtime DLLs)
python scripts/check-enchilada-msm-dsi-host.py \
  --cc tools/llvm-mingw/bin/clang.exe --cxx tools/llvm-mingw/bin/clang++.exe \
  --out work/tmp/msm-dsi-host-out
```

- verification 1: `PASS: enchilada MDSS DSI0 command-channel transport (7 test groups)`
  + `PASS: enchilada MDSS DSI0 command-channel host tests` (fake-MMIO register model).
- verification 2: strict `clang -std=c11 -Wall -Wextra -Werror -pedantic
  -Wconversion -Wsign-conversion -Wshadow` compile exit 0, plus an independent byte
  dump program (`msm_dump.c`) printing the four buffers above byte-for-byte.

## CI / archive (stage-3 layer 2)

`scripts/build-enchilada-msm-dsi.py` compiles `MsmDsiCmd.c` + `DsiDcs.c` as pure-C
arm64 objects (the `osfmk/DEVELOPMENT/startup.o` flags + `-mgeneral-regs-only`) into
`libEnchiladaMsmDsi.a`. `board.toml [kernel] archives` only orders the build; the
kernel actually links it through the `-Wl,-force_load,{tree}/build/enchilada-msm-dsi/libEnchiladaMsmDsi.a`
entry in `configs/enchilada_defconfig` (`CONFIG_XNU_EXTRA_LD_ARCHIVES`), guarded by
`scripts/check-enchilada-archives-linked.py`. Without `-force_load` these pure-C objects
have no strong external reference and the linker drops them.
`py_compile` of the recipe passes and both C units compile clean as
`arm64-apple-darwin` objects locally. **This archive is CI evidence only until
layer 3 provides the runtime probe** that maps `0xae94004`, allocates the
DMA-coherent buffer with `IOBufferMemoryDescriptor`, and calls `msm_dsi_cmd_send()`.
No hardware claim is made for layer 2 (no flash, no `L` row verification yet).
