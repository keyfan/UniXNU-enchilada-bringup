# Stage 7 audio: feasibility study, host-verified

Audio is **not implemented** on this port. This document records what the board
declares, what the reference port assumes, and why the gap cannot be closed by
copying the reference sources — so the `planned` status in `porting.toml` is a
measured statement rather than a placeholder.

## The board's own audio topology

All from `notes/mu-silicium/enchilada.dts` (vendor DTB, 26578 lines):

| Node | Line | What it fixes |
| --- | --- | --- |
| `qcom,lpass@17300000` | 6488 | `compatible = "qcom,pil-tz-generic"`, `reg = <0x17300000 0x100>`, IRQ `GIC_SPI 0xa2`, `qcom,pas-id = <0x01>`, `qcom,smem-id = <0x1a7>`, `qcom,sysmon-id = <0x01>`, `qcom,ssctl-instance-id = <0x14>`, `qcom,firmware-name = "adsp"`, `mbox-names = "adsp-pil"`, `qcom,proxy-timeout-ms = <0x2710>` |
| `slim@171c0000` | 6547 | `compatible = "qcom,slim-ngd"` — the NGD SLIMBUS controller |
| `tavil_codec` | 6570 | `compatible = "qcom,tavil-slim-pgd"` = **WCD9340** |
| `max98927@3a` | 3790 | `compatible = "maxim,max98927L"` speaker amplifier |
| `wsa881x@...` | 6789-6813 | four nodes declared, but the card sets `qcom,wsa-max-devs = <0x00>` |
| `sound-tavil` | 23254 | `compatible = "qcom,sdm845-asoc-snd-tavil"`, `qcom,model = "sdm845-tavil-snd-card"`, `qcom,tavil-mclk-clk-freq = <0x927c00>`, `op,smartpa = "max98927"` |
| `glink-smem-native-xprt-adsp` | 7390 | the ADSP GLINK transport endpoint |

The card node also lists the DAI names it expects, including
`msm-dai-q6-mi2s.3`, which is the QUATERNARY MI2S used for the speaker path.

## The format, cross-checked against Linux

Linux v6.12 `sound/soc/qcom/sdm845.c` pins the card to one format:

```c
#define DEFAULT_SAMPLE_RATE_48K		48000
#define MI2S_BCLK_RATE		1536000
#define SLIM_MAX_TX_PORTS 16
#define SLIM_MAX_RX_PORTS 13
#define WCD934X_DEFAULT_MCLK_RATE	9600000
```

```c
unsigned int rx_ch[SLIM_MAX_RX_PORTS] = {144, 145, 146, 147, 148, 149,
					150, 151, 152, 153, 154, 155, 156};
unsigned int tx_ch[SLIM_MAX_TX_PORTS] = {128, 129, 130, 131, 132, 133,
					    134, 135, 136, 137, 138, 139,
					    140, 141, 142, 143};
```

2 ch × 16 bit × 48 kHz = 1 536 000 Hz, exactly `MI2S_BCLK_RATE`. `AudioRoute.c`
recomputes that product and refuses to claim a match for any other shape, so the
arithmetic is checked rather than asserted in prose.

## Why the reference port does not transfer

The official stage-7 page points at sheng's `AdspProbe.cpp` and `AudioProbe.cpp`.
sheng is SM8550. This board is SDM845:

| | enchilada (SDM845) | sheng (SM8550) |
| --- | --- | --- |
| DSP release | `qcom,pil-tz-generic`, TrustZone PIL, PAS id 1 | `qcom,sm8550-adsp-pas`, PAS-only |
| Codec bus | **SLIMBUS**, WCD9340 (tavil) | **SoundWire**, WCD938x |
| Audio firmware protocol | Q6AFE / ASM over GLINK | AudioReach / GPR |
| Amplifier | MAX98927 on i2c4 @0x3a, plus WSA881x declared-but-unused | CS35L43 over I2C |
| Card driver | `sdm845-asoc-snd-tavil` | sm8550 card |

So three of the four load-bearing assumptions differ. `audio_route_assess()`
encodes exactly that: the generation bit stays set for a SLIMBUS board with no
matching backend, and `testGenerationGap` asserts the two entries disagree on bus
and DSP transport rather than merely carrying different names.

## What was built, and what was deliberately not built

Built: `drivers/NativeAudio/AudioRoute.{h,c}` — a pure-C model of the board's
declared audio topology with the DTB constants, the SLIMBUS port allocation, the
format arithmetic, and the blocker set. It performs **no I/O**, exports 11
functions, and is compiled freestanding at `-O0` and `-O2`.

Not built, on purpose: no ADSP loader, no GLINK channel, no AFE/ASM graph, no
codec driver, no amplifier driver, and **no kernel entry point**. There is no
`EnchiladaAudioProbe.cpp` and no `[kernel] archives` wiring, so nothing here can
run on the device. That is the honest state: a feasibility model, not a stub that
looks like progress.

The blocker set is the reason. Firmware (`adsp` image) is absent from the
workspace, the SDM845 Q6AFE transport does not exist in this tree, the iOS audio
ABI is closed-source with no local Xcode, and 246 decoded device logs contain zero
audio/DSP literals. `audio_route_assess(kAudioSocSdm845, 0, 0, 0)` returns a
non-zero blocker set, and the test asserts all four bits are present.

## Two verifications

1. `python scripts/check-enchilada-audio-host.py --cc <clang> --cxx <clang++>` —
   strict C11 core plus C++17 tests, **8 test groups**:
   lookup, DTB constants, format math, SLIMBUS channels, today's blocker set,
   TrustZone PIL, the generation gap, and determinism.
2. `python work/tmp/verify_audio_2.py` — a different method: it re-parses the
   vendor DTB nodes and the Linux source, re-derives each constant independently,
   compiles the core with `-ffreestanding -fno-builtin -Wconversion` at two
   optimisation levels, checks the exported symbol set is exactly the 11 declared
   functions, asserts the core contains no `volatile`/`asm`/`mmio`/`IOKit`
   tokens, and confirms the record still says `planned` rather than
   `device-verified`.

## Honest boundaries

- This is **host-verified feasibility research**, in the same shape and with the
  same standing as the GPU, network and cellular areas. It is not a driver and it
  is not device evidence.
- `porting.toml` keeps `status = "planned"` and `components = ["AdspProbe",
  "AudioProbe"]` unchanged. Neither component exists for SDM845 in any tree
  available here, and marking them implemented would be false.
- The MAX98927 is modelled only as a bus address. No register protocol was read,
  because writing one without the ADSP running would not produce sound and would
  misrepresent the state.
- Enabling translation or powering LPASS is out of scope here; that is stage 1
  work and is tracked in the memory area.

## Reproduce

```sh
python scripts/check-enchilada-audio-host.py --cc <clang> --cxx <clang++> --out <dir>
python work/tmp/verify_audio_2.py
```
