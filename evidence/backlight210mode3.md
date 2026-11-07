# backlight210mode3 — stage 3 layer 3b, FIRST-WRITE DCS brightness transmit (device-verified)

**Date:** 2026-11-07 (Asia/Shanghai)
**Device:** OnePlus 6 (enchilada, SDM845), serial `27f4f45d`
**Commit:** `c745da6776d4d36d02b272689e6b77e7a05d1626` (parent `f566e737158986fe40aab7fb2aded2219aaa70b4`)
**CI:** [unixnu-build run #210](https://github.com/keyfan/unixnu-build/actions/runs/42378048594) — `completed success`, "Build kernel" step success, 20/20 archive logs with 0 `error:`/`warning:`, `archive-20-build-enchilada-msm-dsi.log` = `PASS: libEnchiladaMsmDsi.a (6 objects, sha256 88035f4e1eeb4bef)`. Chain of custody: device-repo `main` was still `c745da67` when CI #210 was dispatched with `device_ref=main` (`work/tmp/_check_remote_main.py`: `CHAIN_OF_CUSTODY: PASS`).
**Kernel:** `kernel.macho` 125,943,808 B, sha256 `5022f801045973055c422bfd23b93c78a05d7b51791034b37871dd3d3482c1b3` (equals `payload.json` `kernel_sha256`; `payload_kernel_sha256` `d9d7374c...` is the *prelinked* inner kernel and is deliberately different). Binary symbol scan found `msm_dsi_brightness_send`, `msm_dsi_brightness_publish`, `ioDelayUsSpin`, `cleanDma`, `gTransmitDone`, `IOBufferMemoryDescriptor` — the mode-3 shim is force-loaded end-to-end.
**Flash gate:** `enchilada-backlight=3` (mode 3, the FIRST WRITE), `enchilada-wdog=3`, `sheng-data-reset=1`, `enchilada-deadline=600`, `sheng-no-mac-label=1`. Arguments 997/1000 B. `sheng-gpu-addon=1` dropped to fit. Modes 1 (`=1`) and 2 (`=2`) are NOT present.
**Flash command:** `python -X utf8 scripts/test-cycle.py backlight210mode3 --wait-return 900` (payload built by `work/tmp/_build_backlight_mode3.py` from CI #210 artifacts, with the device-proven `--no-reset` probe patch → `console-k210.macho` sha `d8e083effc1b380c`, and `/chosen/boot-manifest-hash` injection; image sha256 `c9ab2d9368d39cf65f135740f06feb5dad72ecf77e7b5189b0250d064098a1b6`).

## Boot window (device decoder)

```
version     4  header 152 bytes
boot_id     0x20e662e8   (551969512)  — new, distinct from console206 (552221472),
                                       backlight208 (549524113), backlight209mode2 (554983992)
uptime      599 s   slots 1163   rounds 1163   write_errors 0
gpt         open 0 last 0  state 2
recovery after 641s
```

510 `L` rows, **all `st=5`** (stage = `kMsmDsiBrightnessStageTx`). Verbatim row:

```
L st=5 req=600 lvl=512 a=8 b=-1375625216 xerr=0 irq=1 ea=0 eb=0 err=0
```

`req` (1 Hz poll count) spans 338..600 across 263 distinct values, so the probe really ran; the decoded terminal tuple is a single coherent static value (one-shot result republished for the whole boot).

## Decoded transmit result

`BootLog.c:749` prints `native_backlight_stats[0..9]` as `st/req/lvl/a/b/xerr/irq/ea/eb/err` (`a`,`b`,`xerr`,`err` are signed `%d`; `irq`,`ea`,`eb` are `%x`). Mode 3's `msm_dsi_brightness_publish()` fills them as follows (`MsmDsiBrightness.c`):

| slot | field | value | meaning |
|---|---|---|---|
| 0 | st | 5 | `kMsmDsiBrightnessStageTx` — transmit completed, no refusal |
| 1 | req | 338..600 | 1 Hz poll count |
| 2 | lvl | 512 | level sent (`kMode3Level`), clamped to 1..1023 |
| 3 | a | 8 | `dma_len` — DCS long write `39 03 00 00 51 02 00` (size 7) padded to 8 |
| 4 | b | -1375625216 = `0xae01a000` | `dma_addr_lo`, printed as signed `%d` → CPU physical address **below 4 GiB** |
| 5 | xerr | 0 | `rc = kMsmDsiCmdOk` — `CMD_MODE_DMA_BUSY` cleared, no timeout |
| 6 | irq | 1 | `sent` counter — exactly ONE packet |
| 7 | ea | 0 | `addr_high` counter — 32-bit `REG_DSI_DMA_BASE` gate never refused |
| 8 | eb | 0 | `dma_err` counter — no DMA-stage failure |
| 9 | err | 0 | reserved |

The transmitted CMD-DMA buffer is `03 00 39 c0 51 02 00 ff`: `data[0..2] = {WClo, WChi, DI}` from `dsi_dcs_set_brightness_large(channel 0, 512)` (`39 03 00 00` header, payload `51 02 00`), `data[3] = 0xc0` (`BIT(7)` last-packet | `BIT(6)` long write), payload, `0xff` pad.

**Independent re-derivation (second verifier):** from the raw row, `b = -1375625216` as a signed int32 is `0xae01a000` = 2,919,342,080 `< 2^32`, so it fits the 32-bit `REG_DSI_DMA_BASE`; `rc=0`, `sent=1`, `dma_len=8`, `lvl=512`, `addr!=0` are mutually consistent with a single successful transmit. Over ALL 510 rows, `max(sent+addr_high+dma_err) = 1` → the send is genuinely one-shot, not repeated per poll.

## Why this matters (the decision it gates)

`REG_DSI_DMA_BASE` is a **32-bit register** (`dsi_host.c:2245` commits `u32 dma_base`, `dsi.xml` `0x00044 DMA_BASE` domain width 32). backlight209mode2 device-confirmed the display SMMU bank is Bypass (`SCTLR.M=0`), so the value must be a CPU physical address — but `NativeUfs.cpp:1197` warns kernel allocations can land above 4 GiB on the sibling SoC, while `enchilada-facts/parsed.txt` lists SDM845 carveouts below 4 GiB. That conflict was UNCERTAIN; the layer-3a orchestrator therefore carries a load-bearing gate that REFUSES (`st=-2 AddrTooHigh`) before touching any register when `(phys>>32)!=0`. **The device has now answered:** the fresh `IOBufferMemoryDescriptor` landed at `0xae01a000` (below 4 GiB), the gate PASSED, and NO stage-1 SMMU IOVA fallback (`NativeUfs.cpp:1092-1161`, `kExtraIova=0x10000000`) is needed on this device.

## Proves / does NOT prove

- **Proves:** the layer-3b mode-3 shim is force-loaded into the real kernel, allocates a 64-byte 4096-aligned physically-contiguous `IOBufferMemoryDescriptor` whose physical address fits the 32-bit DMA_BASE, maps DSI0 at `0xae94004`, runs `dc civac` + `dsb sy` cache maintenance and a CPU-spin delay (not WFE/IODelay), and commits ONE DCS long-write brightness packet to the DSI0 CMD-DMA engine. The engine reported completion (`CMD_MODE_DMA_BUSY` cleared, `rc=0`, no timeout) and `CTRL` was restored. The stored result is republished 1 Hz for the entire 599 s boot without retransmitting (`sent=1`). Controls are unchanged (`console206` all `st=0`, `backlight208` all `st=3`, `backlight209mode2` all `st=4`, this run all `st=5`).
- **Does NOT prove:** a *measured* brightness change on the panel. `rc=0` means the controller's DMA completed, not that the firmware-owned sofef00 panel applied the `0x51` value; there is no framebuffer/photo/light-sensor readback in this harness. It also does NOT prove continuous brightness control (this is a fixed one-shot level 512), nor any OS-facing service: `AppleARMBacklight` / CoreBrightness / backboardd integration (layer 3d) does not exist yet.

## Two independent verifications

1. `work/tmp/verify_device_mode3.py` — 510 rows, stages `[5]`, final `Tx`, 263 distinct poll counts, one-shot (`sent=1`), `lvl=512`, `dma_len=8`, controls `[0]`/`[3]`/`[4]` → `VERDICT: PASS - device-verified layer 3b FIRST-WRITE path`.
2. `work/tmp/verify_device_mode3_independent.py` — boot freshness (new `boot_id`), re-derives the Tx verdict from raw fields instead of trusting the packed stage, asserts `max(sent+addr_high+dma_err) <= 1` over all rows, single distinct terminal tuple, controls unchanged → `INDEPENDENT VERDICT: PASS - device-confirmed first-write execution`.

Both pre-flash gates were also green: `PREFLASH: PASS` (watchdog=3, misc armed, image kernel sha == noreset kernel sha `d8e083effc1b380c`), `payload.json` `kernel_sha256` == downloaded `kernel.macho` sha256, arg budget 997/1000 B, and `CHAIN_OF_CUSTODY: PASS`.
