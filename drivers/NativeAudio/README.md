# NativeAudio — stage 7 audio feasibility model

This directory is **not** an audio driver. It is a pure-C model of what the
OnePlus 6 (enchilada, SDM845) declares about audio, used to make the `audio` area
of `porting.toml` a checkable claim instead of a prose note.

## What is here

| File | Purpose |
| --- | --- |
| `AudioRoute.h` | Board constants (LPASS, SLIMBUS, MAX98927, clocks), the SoC table, the blocker bits |
| `AudioRoute.c` | Lookup, format arithmetic, SLIMBUS port allocation, blocker assessment |
| `tests/audio-route-test.cpp` | 8 host test groups asserting the board facts |

No kernel shim, no `[kernel] archives` entry, no boot argument. Nothing here can
run on the device, which is the point: audio is blocked, and this models the
block rather than faking progress.

## Constants and where they come from

- `notes/mu-silicium/enchilada.dts:6488` — `qcom,lpass@17300000`:
  `compatible = "qcom,pil-tz-generic"`, `reg = <0x17300000 0x100>`, IRQ `GIC_SPI
  0xa2`, `qcom,pas-id = <0x01>`, `qcom,smem-id = <0x1a7>`, `qcom,sysmon-id =
  <0x01>`, `qcom,ssctl-instance-id = <0x14>`, `qcom,firmware-name = "adsp"`,
  `qcom,proxy-timeout-ms = <0x2710>`.
- `:6547` `slim@171c0000` (`qcom,slim-ngd`), `:6570` `tavil_codec`
  (`qcom,tavil-slim-pgd` = WCD9340), `:3790` `max98927@3a`
  (`maxim,max98927L`).
- `:23254` `sound-tavil`: `qcom,sdm845-asoc-snd-tavil`, `qcom,tavil-mclk-clk-freq
  = <0x927c00>`, `qcom,wsa-max-devs = <0x00>` (so the four `wsa881x` nodes are
  declared but unused).
- Linux v6.12 `sound/soc/qcom/sdm845.c`: `MI2S_BCLK_RATE 1536000`,
  `WCD934X_DEFAULT_MCLK_RATE 9600000`, `DEFAULT_SAMPLE_RATE_48K 48000`,
  `SLIM_MAX_RX_PORTS 13`, `SLIM_MAX_TX_PORTS 16`, `rx_ch` 144..156, `tx_ch`
  128..143, `SNDRV_PCM_FORMAT_S16_LE`.

2 ch × 16 bit × 48 kHz = 1 536 000 Hz, which is exactly the bit clock Linux
programs. `audio_route_mi2s_bclk()` recomputes that product so the equality is
verified, not asserted.

## Why the reference port does not transfer

The official stage-7 page points at sheng's `AdspProbe.cpp` / `AudioProbe.cpp`.
sheng is SM8550; this board is SDM845:

| | enchilada (SDM845) | sheng (SM8550) |
| --- | --- | --- |
| DSP release | `qcom,pil-tz-generic` (TrustZone PIL) | `qcom,sm8550-adsp-pas` (PAS only) |
| Codec bus | SLIMBUS, WCD9340 | SoundWire, WCD938x |
| Firmware protocol | Q6AFE / ASM over GLINK | AudioReach / GPR |
| Amplifier | MAX98927 @ i2c4 0x3a | CS35L43 |

`audio_route_assess()` returns `AUDIO_BLOCK_GENERATION` for a SLIMBUS board with
no SDM845 backend, and `testGenerationGap()` asserts the two SoC entries disagree
on bus and DSP transport.

## Verification

```sh
python scripts/check-enchilada-audio-host.py --cc <clang> --cxx <clang++> --out <dir>
python work/tmp/verify_audio_2.py
```

The first is the strict-build host suite (8 groups). The second re-derives every
constant from the DTB and Linux source, compiles the core freestanding at `-O0`
and `-O2`, and checks the exported symbol set is exactly the 11 declared
functions.

## Status

**Host-verified feasibility research.** Not a driver, not device-verified. The
`audio` area keeps `status = "host-verified"` and documents
`components = ["AdspProbe", "AudioProbe"]` as the intended (absent) components,
matching how the GPU, network and cellular areas record their blockers.
