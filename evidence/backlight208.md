# backlight208 — device-verified stage-3 layer 2.5 (read-only DSI0 runtime probe)

Recorded 2026-11-07 from `work/log/backlight208/untagged-20c11291/`
(boot_id `0x20c11291` = 549524113, uptime 599 s, slots_written 1156,
rounds 1156, write_errors 0, state 2) and `work/tmp/window-backlight208.bin`.

- Source revision of the built kernel:
  `2ba47b86853b3b1d0348daf0a8b549dc881700aa` (`keyfan/UniXNU-enchilada-bringup`, main),
  CI run #208 (`42346824677`, `completed success`, step 15 "Build kernel" success;
  `archive-20-build-enchilada-msm-dsi.log`: `PASS: libEnchiladaMsmDsi.a (4 objects, sha256 01ecdd9b9730bfbf)`).
- Kernel sha256 `757ff5bedbafdf759d6e4caec2106344b764dfa92940d804cdf1b989a1637023`
  == `payload.json` `kernel_sha256` (verified on the downloaded `kernel.macho`).
  The raw kernel binary contains the strings `enchilada_backlight_poll`,
  `native_backlight_stats`, `msm_dsi_session_snapshot`, `msm_dsi_cmd_send`,
  `armcpu_map_physical`, and `L st=%d req=%d lvl=%d` — proving the
  `-Wl,-force_load,{tree}/build/enchilada-msm-dsi/libEnchiladaMsmDsi.a` fix linked
  the layer-2.5 objects into the kernel end to end.
- Command: `python scripts/test-cycle.py backlight208 --wait-return 900`
  (fastboot flash `misc` + `system_b`, boot, `recovery after 641s` via
  `enchilada-wdog=3`, then pull the 32 MiB log window from userdata).
- Payload: `work/out/payload-backlight208.img`, built by
  `work/tmp/_build_backlight.py` from the CI #208 artifacts with the
  device-proven console206 recipe and boot arg `enchilada-backlight=1`
  (997/1000 bytes; `sheng-gpu-addon=1` dropped to fit; `enchilada-wdog=3` kept).
  The kernel is the CI #208 artifact patched `--no-reset` so the 1 Hz boot-log
  window survives (`console-k208.macho`, sha256 `b9502d49089f0394...`).

## Boot log `L` rows (verbatim, `slots.txt`)

```
L st=3 req=600 lvl=0 a=501 b=0 xerr=335544320 irq=4 ea=100008 eb=13c2c err=575
L st=3 req=336 lvl=0 a=501 b=5 xerr=335544320 irq=4 ea=100008 eb=13c2c err=575
...
```

510 `L` rows were decoded, all `st=3` (`kMsmDsiSessionStageMapped`), with 265
distinct poll counts `req=336..600`. The control run `console206` (the same
decoder on the pre-change kernel) has 510 rows all `st=0 req=0 ...` (all zeros),
so this is a new, non-zero, device-measured result. Two distinct register tuples
appear; only `STATUS0` differs (`0x0` vs `0x5`, i.e. `CMD_MODE_ENGINE_BUSY` /
`CMD_MODE_MDP_BUSY` toggling while the firmware drives the panel — never
`CMD_MODE_DMA_BUSY` bit 1).

Mapping `MsmDsiSession.c` slots to Linux v6.12 `dsi.xml` bit fields
(`a`=CTRL, `b`=STATUS0, `xerr`=CMD_DMA_CTRL, `irq`=TRIG_CTRL,
`ea`=CMD_CFG0, `eb`=CMD_CFG1, `err`=CLK_CTRL):

| slot | device value | decode |
| --- | --- | --- |
| `a`   | `0x1f5` | CTRL: ENABLE=1, CMD_MODE_EN=1, VID_MODE_EN=0, CLK_EN=1, LANE0-3=0xf (4 lanes) |
| `b`   | `0x0` / `0x5` | STATUS0: CMD_DMA_BUSY=0, engine/MDP busy toggling |
| `xerr`| `0x14000000` | CMD_DMA_CTRL: FROM_FRAME_BUFFER(bit28)=1, LOW_POWER(bit26)=1 |
| `irq` | `0x4` | TRIG_CTRL: DMA_TRIGGER=4 (`TRIGGER_SW`), MDP_TRIGGER=0 (`TRIGGER_NONE`), STREAM=0 |
| `ea`  | `0x100008` | CMD_CFG0: DST_FORMAT=8 (`CMD_DST_FORMAT_RGB888`) |
| `eb`  | `0x13c2c` | CMD_CFG1: WR_MEM_START=0x2c, WR_MEM_CONTINUE=0x3c, INSERT_DCS_COMMAND=1 |
| `err` | `0x23f` | CLK_CTRL: exactly `DSI_CLK_CTRL_ENABLE_CLKS = 0x23f` |

Every row matches mainline `dsi_ctrl_enable()`'s command-mode programming
(`drivers/gpu/drm/msm/dsi/dsi_host.c:764-847`) — 0 rows violate the constants.
The values are a coherent live DSI0 controller configuration, not a zero weak
stub and not random reads: this proves XNU can map `0xae94004` (firmware-owned
DSI0, +4 6G register shift) and read the panel's command-mode setup safely.

## What this proves and what it does not

- Proves: the stage-3 layer 2.5 read-only probe is compiled, force-loaded,
  gated by `enchilada-backlight=1`, polled 1 Hz by `BootProgress.c`, able to map
  the DSI0 MMIO window through `armcpu_map_physical`, and able to publish seven
  live registers into the strong `native_backlight_stats[10]` that overrides
  `BootProgressStubs.c`'s weak `char[4096]`. Boot, userland, and the CDC-ACM
  console are intact (COM5 `USB 串行设备` appeared, watchdog returned the phone
  to recovery at 641 s, 0 write errors).
- Does NOT prove: any brightness change. `lvl=0` by design — this stage writes
  no controller register and triggers no DMA (先核验再上机). Sending a DCS
  brightness packet still needs the CMD-DMA address question (physical vs MDSS
  SMMU IOVA) and `dc civac`/`dsb sy` cache maintenance settled, then layer 3
  (AppleARMBacklight-facing IOService + transmit-side runtime probe).
