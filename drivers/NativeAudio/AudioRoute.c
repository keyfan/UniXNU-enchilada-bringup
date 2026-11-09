/* Stage 7 audio feasibility: the board's declared audio topology, in C.
 *
 * No I/O, no device entry point. Every constant here is transcribed from the
 * vendor DTB or from Linux v6.12 sdm845.c, and the tests re-derive them.
 */
#include "AudioRoute.h"

static const struct audio_soc_entry kSocs[] = {
    {
        /* notes/mu-silicium/enchilada.dts:6488 and :23254 */
        kAudioSocSdm845, kAudioDspPilTz, kAudioBusSlimbus,
        AUDIO_SDM845_LPASS_BASE, AUDIO_SDM845_PAS_ID, AUDIO_SDM845_IRQ_SPI,
        AUDIO_SDM845_SLIM_RX_PORTS, AUDIO_SDM845_SLIM_TX_PORTS,
        "adsp", 1
    },
    {
        /* sheng: qcom,sm8550-adsp-pas + SoundWire WCD938x */
        kAudioSocSm8550, kAudioDspPasOnly, kAudioBusSoundwire,
        0u, 0u, 0u, 0u, 0u,
        "adsp", 1
    },
    {
        /* Coachz (sc7180): lpass-wpss-cdsp-pas + WCD938x SoundWire */
        kAudioSocSc7180, kAudioDspPasOnly, kAudioBusSoundwire,
        0u, 0u, 0u, 0u, 0u,
        "adsp", 1
    }
};

#define kSocCount ((unsigned)(sizeof kSocs / sizeof kSocs[0]))

int audio_route_lookup(enum audio_soc soc, struct audio_soc_entry *out)
{
    unsigned i;

    if (soc == kAudioSocUnknown) {
        return -1;
    }
    for (i = 0; i < kSocCount; ++i) {
        if (kSocs[i].soc == soc) {
            if (out != NULL) {
                *out = kSocs[i];
            }
            return 0;
        }
    }
    return -1;
}

const char *audio_route_dsp_name(unsigned transport)
{
    switch (transport) {
    case kAudioDspPilTz:   return "pil-tz-generic";
    case kAudioDspPasOnly: return "pas";
    default:               return "unknown";
    }
}

const char *audio_route_bus_name(unsigned bus)
{
    switch (bus) {
    case kAudioBusSlimbus:   return "slimbus";
    case kAudioBusSoundwire: return "soundwire";
    case kAudioBusI2s:       return "i2s";
    default:                 return "unknown";
    }
}

int audio_route_bytes_per_frame(unsigned channels, unsigned bits, unsigned *out)
{
    unsigned bytes;

    if (out == NULL || channels == 0u || bits == 0u || (bits % 8u) != 0u) {
        return -1;
    }
    if (channels > 32u || bits > 64u) {
        return -1;
    }
    bytes = (channels * bits) / 8u;
    if (bytes == 0u) {
        return -1;
    }
    *out = bytes;
    return 0;
}

unsigned audio_route_mi2s_bclk(unsigned channels, unsigned bits, unsigned rate_hz)
{
    /* One bit clock carries channels * bits per frame, frames at rate_hz. */
    if (channels == 0u || bits == 0u || rate_hz == 0u) {
        return 0u;
    }
    return channels * bits * rate_hz;
}

int audio_route_bclk_matches(unsigned channels, unsigned bits, unsigned rate_hz)
{
    return audio_route_mi2s_bclk(channels, bits, rate_hz) == AUDIO_SDM845_MI2S_BCLK_HZ;
}

unsigned audio_route_slim_channel(int tx, unsigned index)
{
    if (tx) {
        if (index >= AUDIO_SDM845_SLIM_TX_PORTS) {
            return 0u;
        }
        return AUDIO_SDM845_SLIM_TX_FIRST + index;
    }
    if (index >= AUDIO_SDM845_SLIM_RX_PORTS) {
        return 0u;
    }
    return AUDIO_SDM845_SLIM_RX_FIRST + index;
}

int audio_route_slim_ports_valid(int tx, unsigned count)
{
    if (tx) {
        return count > 0u && count <= AUDIO_SDM845_SLIM_TX_PORTS;
    }
    return count > 0u && count <= AUDIO_SDM845_SLIM_RX_PORTS;
}

unsigned audio_route_assess(enum audio_soc soc, unsigned firmware_mask,
                            int backend_available, int os_abi_available)
{
    struct audio_soc_entry entry;
    unsigned blocks = AUDIO_FEASIBLE;

    if (audio_route_lookup(soc, &entry) != 0) {
        return AUDIO_BLOCK_UNKNOWN_SOC;
    }
    if ((firmware_mask & AUDIO_FW_ADSP) == 0u) {
        blocks |= AUDIO_BLOCK_FIRMWARE;
    }
    if (!backend_available) {
        blocks |= AUDIO_BLOCK_BACKEND;
    }
    if (!os_abi_available) {
        blocks |= AUDIO_BLOCK_OS_ABI;
    }
    /* The reference port is SM8550 (AudioReach/GPR/SoundWire); this board is
     * SDM845 (PIL/SLIMBUS). Without a matching-generation backend the route
     * cannot be built from the reference sources at all. */
    if (entry.bus == kAudioBusSlimbus && !backend_available) {
        blocks |= AUDIO_BLOCK_GENERATION;
    }
    return blocks;
}

int audio_route_needs_trustzone_pil(enum audio_soc soc)
{
    struct audio_soc_entry entry;

    if (audio_route_lookup(soc, &entry) != 0) {
        return 0;
    }
    return entry.dsp == kAudioDspPilTz;
}

int audio_route_board_has_adsp(enum audio_soc soc)
{
    struct audio_soc_entry entry;

    if (audio_route_lookup(soc, &entry) != 0) {
        return 0;
    }
    return entry.board_has_adsp;
}
