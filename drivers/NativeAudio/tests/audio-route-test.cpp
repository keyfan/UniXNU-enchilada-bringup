/* Host tests for the stage-7 audio feasibility study.
 *
 * These assert the board facts, not a working driver: if a constant here drifts
 * from the vendor DTB or from Linux sdm845.c, the port would plan against
 * hardware that is not there.
 */
#include "AudioRoute.h"

#include <cstdio>
#include <cstring>

static unsigned gFailures;
static unsigned gChecks;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++gFailures;                                                       \
        }                                                                      \
    } while (0)

static void testLookupSdm845()
{
    struct audio_soc_entry e;
    std::memset(&e, 0, sizeof e);

    CHECK(audio_route_lookup(kAudioSocSdm845, &e) == 0);
    CHECK(e.dsp == kAudioDspPilTz);
    CHECK(e.bus == kAudioBusSlimbus);
    CHECK(e.lpass_base == 0x17300000u);
    CHECK(e.pas_id == 1u);
    CHECK(e.irq_spi == 0xa2u);
    CHECK(e.slim_rx_ports == 13u);
    CHECK(e.slim_tx_ports == 16u);
    CHECK(e.board_has_adsp == 1);
    CHECK(std::strcmp(e.firmware_name, "adsp") == 0);

    /* An unknown SoC must not silently look like a known one. */
    std::memset(&e, 0, sizeof e);
    CHECK(audio_route_lookup(kAudioSocUnknown, &e) == -1);
    CHECK(audio_route_lookup(static_cast<audio_soc>(99), &e) == -1);
    /* NULL out-param is still a valid query. */
    CHECK(audio_route_lookup(kAudioSocSdm845, nullptr) == 0);
}

static void testDtbConstants()
{
    /* notes/mu-silicium/enchilada.dts:6488 lpass + :6547 slim + :3790 amp. */
    CHECK(AUDIO_SDM845_LPASS_BASE == 0x17300000u);
    CHECK(AUDIO_SDM845_LPASS_SIZE == 0x100u);
    CHECK(AUDIO_SDM845_IRQ_SPI == 0xa2u);
    CHECK(AUDIO_SDM845_PAS_ID == 1u);
    CHECK(AUDIO_SDM845_SMEM_ID == 0x1a7u);
    CHECK(AUDIO_SDM845_SYSMON_ID == 1u);
    CHECK(AUDIO_SDM845_SSCTL_INSTANCE == 0x14u);
    CHECK(AUDIO_SDM845_PROXY_TIMEOUT_MS == 10000u);
    CHECK(AUDIO_SDM845_SLIM_BASE == 0x171c0000u);
    CHECK(AUDIO_SDM845_MAX98927_BUS == 4u);
    CHECK(AUDIO_SDM845_MAX98927_ADDR == 0x3au);
    /* qcom,tavil-mclk-clk-freq = <0x927c00> = 9600000 */
    CHECK(AUDIO_SDM845_MCLK_HZ == 0x927c00u);
    /* qcom,wsa-max-devs = <0x00>: declared but unused. */
    CHECK(AUDIO_SDM845_WSA_DEVS == 0u);
}

static void testFormatMath()
{
    unsigned bytes = 0;

    /* 48 kHz / 2 ch / S16_LE is the board's declared format. */
    CHECK(AUDIO_SDM845_RATE_HZ == 48000u);
    CHECK(audio_route_bytes_per_frame(2u, 16u, &bytes) == 0);
    CHECK(bytes == 4u);

    /* And it is exactly the MI2S bit clock Linux programs. */
    CHECK(audio_route_mi2s_bclk(2u, 16u, 48000u) == 1536000u);
    CHECK(audio_route_bclk_matches(2u, 16u, 48000u) == 1);

    /* A different word size on the same rate must NOT claim to match. */
    CHECK(audio_route_mi2s_bclk(2u, 24u, 48000u) == 2304000u);
    CHECK(audio_route_bclk_matches(2u, 24u, 48000u) == 0);
    CHECK(audio_route_bclk_matches(1u, 16u, 48000u) == 0);

    /* Rejections. */
    CHECK(audio_route_bytes_per_frame(0u, 16u, &bytes) == -1);
    CHECK(audio_route_bytes_per_frame(2u, 0u, &bytes) == -1);
    CHECK(audio_route_bytes_per_frame(2u, 12u, &bytes) == -1);
    CHECK(audio_route_bytes_per_frame(2u, 16u, nullptr) == -1);
    CHECK(audio_route_mi2s_bclk(0u, 16u, 48000u) == 0u);
    CHECK(audio_route_mi2s_bclk(2u, 16u, 0u) == 0u);
}

static void testSlimbusChannels()
{
    /* Linux sdm845.c: rx_ch {144..156}, tx_ch {128..143}. */
    CHECK(audio_route_slim_channel(0, 0u) == 144u);
    CHECK(audio_route_slim_channel(0, 12u) == 156u);
    CHECK(audio_route_slim_channel(0, 13u) == 0u);   /* out of range */
    CHECK(audio_route_slim_channel(1, 0u) == 128u);
    CHECK(audio_route_slim_channel(1, 15u) == 143u);
    CHECK(audio_route_slim_channel(1, 16u) == 0u);

    CHECK(audio_route_slim_ports_valid(0, 13u) == 1);
    CHECK(audio_route_slim_ports_valid(0, 14u) == 0);
    CHECK(audio_route_slim_ports_valid(1, 16u) == 1);
    CHECK(audio_route_slim_ports_valid(1, 17u) == 0);
    CHECK(audio_route_slim_ports_valid(0, 0u) == 0);

    /* The two ranges must not overlap: rx starts above tx's last channel. */
    CHECK(AUDIO_SDM845_SLIM_RX_FIRST > AUDIO_SDM845_SLIM_TX_FIRST);
    CHECK(AUDIO_SDM845_SLIM_TX_FIRST + AUDIO_SDM845_SLIM_TX_PORTS - 1u
          < AUDIO_SDM845_SLIM_RX_FIRST);
}

static void testAssessToday()
{
    /* No ADSP image, no backend, no OS audio ABI: exactly what this tree has. */
    unsigned blocks = audio_route_assess(kAudioSocSdm845, 0u, 0, 0);

    CHECK((blocks & AUDIO_BLOCK_FIRMWARE) != 0u);
    CHECK((blocks & AUDIO_BLOCK_BACKEND) != 0u);
    CHECK((blocks & AUDIO_BLOCK_OS_ABI) != 0u);
    CHECK((blocks & AUDIO_BLOCK_GENERATION) != 0u);
    CHECK(blocks != AUDIO_FEASIBLE);

    /* Unknown SoC reports only the unknown bit. */
    CHECK(audio_route_assess(kAudioSocUnknown, 0u, 0, 0) == AUDIO_BLOCK_UNKNOWN_SOC);

    /* With firmware + backend + ABI the SM8550 side clears. */
    CHECK(audio_route_assess(kAudioSocSm8550, AUDIO_FW_ADSP, 1, 1) == AUDIO_FEASIBLE);
    /* SDM845 asks one more question: is the SDM845-specific backend there? */
    CHECK(audio_route_assess(kAudioSocSdm845, AUDIO_FW_ADSP, 1, 1) == AUDIO_FEASIBLE);
}

static void testTrustzonePil()
{
    /* The vendor DTB uses qcom,pil-tz-generic for the ADSP, which is the
     * difference that makes sheng's PAS-only path inapplicable. */
    CHECK(audio_route_needs_trustzone_pil(kAudioSocSdm845) == 1);
    CHECK(audio_route_needs_trustzone_pil(kAudioSocSm8550) == 0);
    CHECK(audio_route_needs_trustzone_pil(kAudioSocUnknown) == 0);
    CHECK(std::strcmp(audio_route_dsp_name(kAudioDspPilTz), "pil-tz-generic") == 0);
    CHECK(std::strcmp(audio_route_dsp_name(kAudioDspPasOnly), "pas") == 0);
    CHECK(std::strcmp(audio_route_dsp_name(77u), "unknown") == 0);
}

static void testGenerationGap()
{
    struct audio_soc_entry ours;
    struct audio_soc_entry theirs;

    CHECK(audio_route_lookup(kAudioSocSdm845, &ours) == 0);
    CHECK(audio_route_lookup(kAudioSocSm8550, &theirs) == 0);

    /* The three things that make sheng's audio sources unusable here. */
    CHECK(ours.bus != theirs.bus);
    CHECK(ours.dsp != theirs.dsp);
    CHECK(std::strcmp(audio_route_bus_name(ours.bus), "slimbus") == 0);
    CHECK(std::strcmp(audio_route_bus_name(theirs.bus), "soundwire") == 0);

    /* The board does have an ADSP, so "audio not applicable" would be a false
     * statement -- the same trap the cellular area had. */
    CHECK(audio_route_board_has_adsp(kAudioSocSdm845) == 1);
    CHECK(audio_route_board_has_adsp(kAudioSocUnknown) == 0);
}

static void testDeterministic()
{
    unsigned a = audio_route_assess(kAudioSocSdm845, 0u, 0, 0);
    unsigned b = audio_route_assess(kAudioSocSdm845, 0u, 0, 0);

    CHECK(a == b);
    CHECK(audio_route_mi2s_bclk(2u, 16u, 48000u) == audio_route_mi2s_bclk(2u, 16u, 48000u));
}

int main()
{
    testLookupSdm845();
    testDtbConstants();
    testFormatMath();
    testSlimbusChannels();
    testAssessToday();
    testTrustzonePil();
    testGenerationGap();
    testDeterministic();

    if (gFailures != 0u) {
        std::printf("FAIL: audio route feasibility (%u checks, %u failures)\n",
                    gChecks, gFailures);
        return 1;
    }
    std::printf("PASS: enchilada stage-7 audio route feasibility (8 test groups)\n");
    return 0;
}
