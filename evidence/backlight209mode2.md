# backlight209mode2 — stage 3 layer-3 prerequisite, SMMU display-bank survey (device-verified)

**Date:** 2026-11-07 (Asia/Shanghai)
**Device:** OnePlus 6 (enchilada, SDM845), serial `27f4f45d`
**Commit:** `2b14dc4ad6780e43362f77657c56c656ca14d181` (parent `5ccbbec0155b4ac7616bd0822bbb80a9863df593`)
**CI:** [unixnu-build run #209](https://github.com/keyfan/unixnu-build/actions/runs/42362111447) — `completed success`, "Build kernel" step success, 20/20 archive logs with 0 `error:`, `archive-20-build-enchilada-msm-dsi.log` = `PASS: libEnchiladaMsmDsi.a (5 objects, sha256 4fdd2b93f72cc3f4)`.
**Kernel:** `kernel.macho` 125,943,808 B, sha256 `1597e8bbbc7011b77eee2df326dd58b6615f6b24a9b732ad56c5d4aee8de1f8f` (equals `payload.json` `kernel_sha256`; `payload_kernel_sha256` `286ad0d9...` is the *prelinked* inner kernel and is deliberately different).
**Flash gate:** `enchilada-backlight=2` (mode 2), `enchilada-wdog=3`, `sheng-data-reset=1`, `enchilada-deadline=600`. Arguments 997/1000 B. `sheng-gpu-addon=1` dropped to fit. Mode 1 (`=1`) is NOT present.
**Flash command:** `python -X utf8 scripts/test-cycle.py backlight209mode2 --wait-return 900` (payload built by `work/tmp/_build_backlight_mode2.py` from CI #209 artifacts, with the device-proven `--no-reset` probe patch and `/chosen/boot-manifest-hash` injection).

## Boot window (device decoder)

```
version     4  header 152 bytes
boot_id     0x21146238   (554983992)  — new, distinct from console206 (552221472) and backlight208 (549524113)
uptime      599 s   slots 1163   rounds 1163   write_errors 0
gpt         open 0 last 0  state 2
recovery after 641s
```

510 `L` rows, **all `st=4`** (stage = `kMsmSmmuStageSurveyed`). Verbatim row:

```
L st=4 req=599 lvl=1 a=4194349 b=2 xerr=0 irq=e0 ea=10003 eb=0 err=2176
```

`req` (1 Hz poll count) spans 337..599 across 263 distinct values, so the probe really ran; the decoded tuple is a single coherent static value (read-only hardware state).

## Decoded SMMUv2 display-bank state

`BootLog.c:749` prints `native_backlight_stats[0..9]` as `st/req/lvl/a/b/xerr/irq/ea/eb/err`. Mode 2's `msm_smmu_survey_publish()` fills them as follows (`MsmDsiSmmu.c:155-180`):

| slot | field | value | meaning |
|---|---|---|---|
| 0 | st | 4 | `kMsmSmmuStageSurveyed` — apps_smmu mapped and surveyed |
| 1 | req | 337..599 | 1 Hz poll count (overwrites `matched`) |
| 2 | lvl | 1 | `kMsmSmmuVerdictBypass` |
| 3 | a | 4194349 = `0x0040002d` | `num_cb=45`, `global_pages=64` |
| 4 | b | 2 | `bypass=2`, `translating=0` (two display streams matched, both bypass) |
| 5 | xerr | 0 | `first_s2cr=0x00000000` → `TYPE=0` (translation context bank), `CBNDX=0` |
| 6 | irq | `e0` | `first_sctlr=0x000000e0` → **`M=0` (translation OFF)** |
| 7 | ea | `10003` | `first_cbar=0x00010003` |
| 8 | eb | 0 | `first_ttbr0_lo=0x00000000` |
| 9 | err | 2176 = `0x880` | `first_stream` = enchilada MDP display SID |

**Independent re-derivation (second verifier):** from the raw row, `S2CR.TYPE=(0>>16)&3=0`, `SCTLR.M=0xe0&1=0`, `CBNDX=0 < num_cb=45` ⇒ translation is OFF, so a CPU physical address passes through the SMMU unchanged. This is the same state as `NativeUfs.cpp:1196` ("SCTLR 0xe0 = translation off, so the display fetched physical addresses"). The matched stream `0x880` is one of the six enchilada/SDM845 display SIDs (`0x880/0xc80/0x881/0xc81/0x1090/0x1091`, vendor DTS `:853/:995/:3393/:3399`) — **not** sheng/SM8550's `0x1c00/0x1c02`.

## Why this matters (the decision it gates)

Linux v6.12 `dsi_tx_buf_alloc_6g()` / `dsi_dma_base_get_6g()` feed `REG_DSI_DMA_BASE` a **display-SMMU IOVA** (`msm_gem_get_and_pin_iova`), whereas all of this port's DMA (`Dwc3Gadget.cpp:196-221`, `NativeUfs.cpp:949-954`) uses **CPU physical addresses**. The two are equal only when the stream's context bank has translation off / bypass. Before this run that state was UNCERTAIN for enchilada (sheng's display bank was known to be bypass, but SDM845 uses different SIDs and no device diagnostic row existed). With 510 device rows the assumption is now **DEVICE-CONFIRMED**: physical DMA addresses are safe for the DSI0 command channel.

## Proves / does NOT prove

- **Proves:** the mode-2 shim is force-loaded into the real kernel (`-force_load`), maps `apps_smmu 0x15000000` (window `0x80000`), walks SMR/S2CR for the display SIDs, reads the context bank, and publishes a coherent bypass verdict 1 Hz for the whole 599 s boot. The three boot windows are distinct (`console206` all `st=0`, `backlight208` all `st=3`, this run all `st=4`).
- **Does NOT prove:** any brightness change, any register write, any DCS packet transmission, any DMA. Mode 2 is strictly read-only (the `msm_smmu_io` struct has no write callback). Sending the first `mipi_dsi_dcs_set_display_brightness_large()` packet — `dc civac` + `dsb sy` + `msm_dsi_cmd_send()` + layer-3 `AppleARMBacklight` IOService — is the NEXT step and needs its own CI + device verification.

## Two independent verifications

1. `work/tmp/verify_device_mode2.py` — 510 rows, stages `[4]`, verdict `{Bypass}`, 263 distinct poll counts, controls `[0]`/`[3]` → `VERDICT: PASS - device-verified mode-2 SMMU survey`.
2. `work/tmp/verify_device_mode2_independent.py` — boot freshness (new `boot_id`), re-derives physical/bypass from raw `S2CR.TYPE`/`SCTLR.M`/`CBNDX` instead of trusting the packed verdict, asserts the stream set is enchilada-only and controls unchanged → `INDEPENDENT VERDICT: PASS - device-confirmed Bypass on enchilada`.

Both pre-flash gates were also green: `PREFLASH: PASS` (watchdog=3, misc armed, image kernel sha == noreset kernel sha) and `payload.json` `kernel_sha256` == downloaded `kernel.macho` sha256.
