/* Stage 7 audio: feasibility study, not a driver.
 *
 * This module exists so the audio area can carry a checkable claim instead of a
 * prose note. It encodes what the board actually declares (vendor DTB), what the
 * firmware/host interface would require, and the generation gap against the
 * reference port. It performs no I/O and has no device entry point: audio is
 * BLOCKED, and the module proves why rather than pretending otherwise.
 *
 * Sources, all in this tree:
 *   notes/mu-silicium/enchilada.dts:6488  qcom,lpass@17300000  (compatible
 *       "qcom,pil-tz-generic", pas-id 1, smem-id 0x1a7, sysmon-id 1,
 *       ssctl-instance-id 0x14, firmware-name "adsp", mbox "adsp-pil",
 *       proxy-timeout-ms 0x2710, IRQ GIC_SPI 0xa2)
 *   notes/mu-silicium/enchilada.dts:6547  slim@171c0000  (compatible
 *       "qcom,slim-ngd")
 *   notes/mu-silicium/enchilada.dts:6570  tavil_codec  (compatible
 *       "qcom,tavil-slim-pgd" = WCD9340)
 *   notes/mu-silicium/enchilada.dts:3790  max98927@3a  (compatible
 *       "maxim,max98927L")
 *   notes/mu-silicium/enchilada.dts:23254 sound-tavil  (compatible
 *       "qcom,sdm845-asoc-snd-tavil", model "sdm845-tavil-snd-card",
 *       qcom,tavil-mclk-clk-freq 0x927c00, qcom,wsa-max-devs 0)
 *   Linux v6.12 sound/soc/qcom/sdm845.c: rx_ch 144..156, tx_ch 128..143,
 *       WCD934X_DEFAULT_MCLK_RATE 9600000, MI2S_BCLK_RATE 1536000,
 *       48 kHz / 2 ch / S16_LE.
 */
#ifndef ENCHILADA_AUDIO_ROUTE_H
#define ENCHILADA_AUDIO_ROUTE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum audio_soc {
    kAudioSocUnknown = 0,
    kAudioSocSdm845  = 1,
    kAudioSocSm8550  = 2,
    kAudioSocSc7180  = 3
};

/* How the DSP image is authenticated and released. */
enum audio_dsp_transport {
    kAudioDspUnknown = 0,
    kAudioDspPilTz   = 1,   /* qcom,pil-tz-generic: TrustZone PIL, PAS id in DT */
    kAudioDspPasOnly = 2    /* qcom,*-pas: firmware authenticated by PAS alone */
};

/* How the codec is attached. This is the part that does not port across. */
enum audio_codec_bus {
    kAudioBusUnknown   = 0,
    kAudioBusSlimbus   = 1, /* SDM845: WCD9340 on slimbus */
    kAudioBusSoundwire = 2, /* SM8550: WCD938x on SoundWire */
    kAudioBusI2s       = 3
};

/* SDM845 LPASS remote processor, from the vendor DTB. */
#define AUDIO_SDM845_LPASS_BASE      0x17300000u
#define AUDIO_SDM845_LPASS_SIZE      0x00000100u
#define AUDIO_SDM845_IRQ_SPI         0x00a2u
#define AUDIO_SDM845_PAS_ID          1u
#define AUDIO_SDM845_SMEM_ID         0x01a7u
#define AUDIO_SDM845_SYSMON_ID       1u
#define AUDIO_SDM845_SSCTL_INSTANCE  0x14u
#define AUDIO_SDM845_PROXY_TIMEOUT_MS 10000u

/* Slimbus controller and the speaker amplifier. */
#define AUDIO_SDM845_SLIM_BASE       0x171c0000u
#define AUDIO_SDM845_MAX98927_BUS    4u
#define AUDIO_SDM845_MAX98927_ADDR   0x3au

/* Clocking, cross-checked against Linux sdm845.c. */
#define AUDIO_SDM845_MCLK_HZ         9600000u
#define AUDIO_SDM845_MI2S_BCLK_HZ    1536000u
#define AUDIO_SDM845_RATE_HZ         48000u
#define AUDIO_SDM845_CHANNELS        2u
#define AUDIO_SDM845_SAMPLE_BITS     16u

/* Slimbus port allocation, from Linux sdm845.c:sdm845_slim_snd_hw_params(). */
#define AUDIO_SDM845_SLIM_RX_PORTS   13u
#define AUDIO_SDM845_SLIM_TX_PORTS   16u
#define AUDIO_SDM845_SLIM_RX_FIRST   144u
#define AUDIO_SDM845_SLIM_TX_FIRST   128u

/* QUATERNARY_MI2S is DAI index 3 (sdm845.c uses it for the speaker path). */
#define AUDIO_SDM845_MI2S_QUATERNARY 3u

/* qcom,wsa-max-devs = 0: the WSA881x nodes are declared but unused on this
 * board, so a speaker route must not assume them. */
#define AUDIO_SDM845_WSA_DEVS        0u

/* Blocker bits, so "why is this not working" has one answer shape. */
#define AUDIO_FEASIBLE     0x00u
#define AUDIO_BLOCK_UNKNOWN_SOC 0x01u
#define AUDIO_BLOCK_FIRMWARE    0x02u
#define AUDIO_BLOCK_BACKEND    0x04u
#define AUDIO_BLOCK_OS_ABI      0x08u
#define AUDIO_BLOCK_GENERATION  0x10u

#define AUDIO_FW_ADSP 0x01u

struct audio_soc_entry {
    enum audio_soc soc;
    enum audio_dsp_transport dsp;
    enum audio_codec_bus bus;
    uint32_t lpass_base;
    uint32_t pas_id;
    uint16_t irq_spi;
    uint8_t slim_rx_ports;
    uint8_t slim_tx_ports;
    const char *firmware_name;
    int board_has_adsp;
};

/* NULL soc entry when the SoC is not in the table. */
int audio_route_lookup(enum audio_soc soc, struct audio_soc_entry *out);

const char *audio_route_dsp_name(unsigned transport);
const char *audio_route_bus_name(unsigned bus);

/* 2 ch * 16 bit = 4 bytes per frame. */
int audio_route_bytes_per_frame(unsigned channels, unsigned bits, unsigned *out);

/* The MI2S bit clock a format implies, and whether it matches the board's. */
unsigned audio_route_mi2s_bclk(unsigned channels, unsigned bits, unsigned rate_hz);
int audio_route_bclk_matches(unsigned channels, unsigned bits, unsigned rate_hz);

/* Slimbus channel numbers. Index is the port position, not the channel id. */
unsigned audio_route_slim_channel(int tx, unsigned index);
int audio_route_slim_ports_valid(int tx, unsigned count);

/* Blocker set for today's tree: no ADSP image, no GLINK/AFE transport, no OS
 * audio ABI, and the reference port is a different generation. */
unsigned audio_route_assess(enum audio_soc soc, unsigned firmware_mask,
                            int backend_available, int os_abi_available);

int audio_route_needs_trustzone_pil(enum audio_soc soc);
int audio_route_board_has_adsp(enum audio_soc soc);

#ifdef __cplusplus
}
#endif

#endif /* ENCHILADA_AUDIO_ROUTE_H */
