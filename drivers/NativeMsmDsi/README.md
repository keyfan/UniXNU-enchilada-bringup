# NativeMsmDsi — enchilada stage 3, layers 2 + 2.5 + layer-3 prerequisite (MDSS DSI0 command channel)

This is the register-level transmit path that turns a stage-3 layer-1 DCS packet
(`drivers/NativeDsiBacklight/DsiDcs.h`) into the MSM-specific CMD DMA buffer and
pushes it out over `dsi_ctrl0` (`0xae94000`) on the OnePlus 6 (SDM845). The core is
pure C over an injected `struct msm_dsi_io` register bus (the `NativeGeniI2c` fake-MMIO
pattern), so it is host-testable locally before the ABI-sensitive layer 3
`AppleARMBacklight` IOService exists.

**Layer 2.5** adds a **READ-ONLY runtime probe** (`MsmDsiSession.c/.h` + the thin
`package/EnchiladaBacklightProbe.cpp` shim). Gated by `enchilada-backlight=1` (default
off) and driven 1 Hz from `package/BootProgress.c monitor()`, it maps `0xae94004` with
the device-proven `armcpu_map_physical`, reads seven live config registers, and publishes
them into a strong `native_backlight_stats[10]` so the boot-log `L st=` row becomes
non-zero on the real device. It writes **nothing** to the controller and triggers **no**
DMA — this is the "先核验再上机" step: prove XNU can map and read the firmware-owned
controller before any packet is sent. See "Layer 2.5" below.

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
  + `PASS: enchilada MDSS DSI0 read-only runtime session (3 test groups)`
  + `PASS: enchilada read-only SMMUv2 display-bank survey (11 test groups)`
  + `PASS: enchilada MDSS DSI0 command-channel + session + SMMU survey host tests`
  (fake-MMIO register models).
- verification 2: strict `clang -std=c11 -Wall -Wextra -Werror -pedantic
  -Wconversion -Wsign-conversion -Wshadow` compile exit 0, plus an independent byte
  dump program (`msm_dump.c`) printing the four buffers above byte-for-byte; for the
  new SMMU unit also `-std=c++20 -Wdeprecated-volatile` header syntax and an
  `-target arm64-apple-darwin -mgeneral-regs-only` object compile (exit 0).

## CI / archive (stage-3 layers 2 + 2.5)

`scripts/build-enchilada-msm-dsi.py` compiles five arm64 objects into
`libEnchiladaMsmDsi.a` (mixed C/C++, mirroring `build-enchilada-touch.py`):
`EnchiladaBacklightProbe.cpp` (`iokit/DEVELOPMENT/IOPlatformExpert.cpo.json` flags) and
`MsmDsiSession.c` + `MsmDsiSmmu.c` + `MsmDsiCmd.c` + `DsiDcs.c`
(`osfmk/DEVELOPMENT/startup.o.json` flags + `-mgeneral-regs-only`).
`board.toml [kernel] archives` only orders the build;
the kernel actually links it through the
`-Wl,-force_load,{tree}/build/enchilada-msm-dsi/libEnchiladaMsmDsi.a` entry in
`configs/enchilada_defconfig` (`CONFIG_XNU_EXTRA_LD_ARCHIVES`), guarded by
`scripts/check-enchilada-archives-linked.py`. Without `-force_load` these objects
have no strong external reference and the linker drops them (this exact bug — layer 2
CI-compiled but not linked — was found and fixed with the guard; see porting.toml).

The strong `native_backlight_stats[10]` and `enchilada_backlight_poll()` in this archive
take precedence over the weak stand-ins of `package/BootProgressStubs.c`
(`libBootProgress.a`); `armcpu_map_physical` is a strong `extern "C"` symbol in the
force-loaded ArmCpu archive (`drivers/ArmCpu/ArmCpu.cpp:207`).

## Layer 2.5: read-only runtime probe (default off)

`enchilada-backlight=1` opts in. `enchilada_backlight_poll()` (weak_import declared in
`BootProgress.c`, weak stub in `BootProgressStubs.c`, strong definition here) is called
1 Hz; on the first poll it maps the 0x400-byte `dsi_ctrl` window at `0xae94000` and adds
`MSM_DSI0_REG_SHIFT` (4) to reach `0xae94004`. `msm_dsi_session_snapshot()` then reads
exactly these seven `dsi.xml` offsets through `io->rd` only — CTRL(0x000), STATUS0(0x004),
CMD_DMA_CTRL(0x038), CMD_CFG0(0x03c), CMD_CFG1(0x040), TRIG_CTRL(0x080),
CLK_CTRL(0x118) — and `msm_dsi_session_publish()` writes them into the
`native_backlight_stats[10]` slots the BootLog.c `L st=...` row reads:

```
L st=3 req=<polls> lvl=0 a=<CTRL> b=<STATUS0> xerr=<CMD_DMA_CTRL> \
  irq=<TRIG_CTRL hex> ea=<CMD_CFG0 hex> eb=<CMD_CFG1 hex> err=<CLK_CTRL>
```

Host verification: `check-enchilada-msm-dsi-host.py` runs both test binaries —
`PASS: enchilada MDSS DSI0 command-channel transport (7 test groups)` (layer 2) and
`PASS: enchilada MDSS DSI0 read-only runtime session (3 test groups)` (layer 2.5, whose
FakeDsi asserts `writes == 0`). The C++ shim passes the CI-equivalent strict syntax
check `clang++ -std=c++20 -target arm64-apple-darwin -fsyntax-only -Wall -Wextra
-Werror -Wdeprecated-volatile` (the `-std=c++20` matters: the CI kernel build's
`-Wdeprecated-volatile` rejects volatile-qualified array parameters and `++` on a
volatile object; see porting.toml's run #207 note).

**Layer 2.5 hardware claim (backlight208, CI #208)**: flashed with
`enchilada-backlight=1`, 510 `L` rows all `st=3` with coherent live DSI0 registers
(CTRL=0x1f5, CMD_DMA_CTRL=0x14000000, TRIG_CTRL=0x4, CMD_CFG0=0x100008,
CMD_CFG1=0x13c2c, CLK_CTRL=0x23f); the console206 control is all zeros. See
`evidence/backlight208.md`. This stage writes nothing and triggers no DMA (`lvl=0`).

## Layer-3 prerequisite: read-only SMMUv2 display-bank survey (HOST-VERIFIED, not flashed)

`MsmDsiSmmu.c/.h` answers the one question blocking the first DCS brightness packet:
**does the display's DMA use PHYSICAL addresses or an SMMU IOVA?**

- Linux v6.12 `dsi_host.c` `dsi_tx_buf_alloc_6g()` / `dsi_dma_base_get_6g()` feed
  `REG_DSI_DMA_BASE` a **display-SMMU IOVA** (`msm_gem_get_and_pin_iova`), while this
  whole repo (`NativeUfs.cpp:949-954`, `Dwc3Gadget.cpp:196-221`) uses **physical**
  addresses on the assumption the stream is in an apps_smmu bypass context bank. The
  two agree only when `SCTLR.M == 0` (translation off) — sheng's display bank measured
  `SCTLR 0xe0 = translation off` (`NativeUfs.cpp:1196`).
- **The catch**: enchilada/SDM845 display stream IDs (vendor DTS `:853` `0x880/0x8`,
  `0xc80/0x8`; `:995` `0x881/0x8`, `0xc81/0x8`; `:3393/:3399` `0x1090/0x0`,
  `0x1091/0x0`) differ from sheng/SM8550's `0x1c00/0x1c02`, and the backlight208 /
  console206 boot logs contain **no display-SMMU diagnostic row** (only XNU's own MMU
  `TCR/TTBR/SCTLR=3454593d` and `[display state]`). So the bypass assumption is
  UNCERTAIN on this board and must be confirmed with a real, read-only device survey
  before any DMA trigger (先核验再上机).

`MsmDsiSmmu.c` is a pure-C, **READ-ONLY** survey over an injected `struct msm_smmu_io`
(a single `rd` callback — no write path). It decodes IDR0/IDR1/IDR2 geometry, scans
SMR/S2CR for the six built-in enchilada display (id, mask) requests using the same
match rule as `SmmuTakeover.h:68`, reads each matched bank's SCTLR/TTBR0/TCR/CBAR,
and returns a verdict: `Bypass` (physical OK) / `Translate` (IOVA required) /
`NotFound` / `Geometry`. Register offsets are taken verbatim from the two
device-proven local sources `SmmuTakeover.h` and `NativeUfs.cpp:862-944`.

Host verification (both independent): `msm-dsi-smmu-test.cpp` — 11 test groups —
asserts geometry decoders, bypass/translate/S2CR-bypass verdicts, id+mask matching,
ReadOnly (`writes==0`, every read < window), null refusals, the 10-slot packing,
the exact built-in stream list, and `msm_smmu_survey_publish()` writing a volatile
`int32_t[10]` L-row (stage 4 + poll count); plus strict C11, C++20 `-Wdeprecated-volatile`, and
`-target arm64-apple-darwin -mgeneral-regs-only` compiles. It is compiled into the
same force-loaded `libEnchiladaMsmDsi.a` (5 objects now).

**No hardware claim for this survey yet**: it is code + host tests + a CI-compiled
object only — NOT yet wired to a runtime mode, NOT flashed. The disciplined next step
is a gated `enchilada-backlight=2` mode-2 probe that maps `apps_smmu 0x15000000`
(read-only), runs `msm_smmu_survey_display()`, and publishes the verdict to a
diagnostic row (stage code 4) — confirmed on device BEFORE `msm_dsi_cmd_send()` is
ever called. Then `dc civac` + `dsb sy` cache maintenance (`Dwc3Gadget.cpp:306-313`)
and finally the layer-3 `AppleARMBacklight` IOService.
