// SPDX-License-Identifier: BSD-2-Clause-Patent
// DSI DCS packet construction for the OnePlus 6 sofef00 AMOLED panel (stage-3 backlight
// transport). Register-independent on purpose: see DsiDcs.h for the mainline source and
// the vendor-DTS evidence behind every constant and byte order below.
#include "DsiDcs.h"

uint8_t dsi_dcs_write_type(uint16_t tx_len) {
    if (tx_len == 0) return 0;                 /* invalid: mipi_dsi_dcs_write_buffer -> -EINVAL */
    if (tx_len == 1) return kDsiDcsShortWrite;       /* 0x05 */
    if (tx_len == 2) return kDsiDcsShortWriteParam;  /* 0x15 */
    return kDsiDcsLongWrite;                          /* 0x39 */
}

uint16_t dsi_dcs_clamp_brightness(uint16_t brightness) {
    if (brightness < (uint16_t)kEnchiladaBrightnessMin) return (uint16_t)kEnchiladaBrightnessMin;
    if (brightness > (uint16_t)kEnchiladaBrightnessMax) return (uint16_t)kEnchiladaBrightnessMax;
    return brightness;
}

void dsi_dcs_brightness_payload(uint16_t brightness, uint8_t out[2]) {
    /* mipi_dsi_dcs_set_display_brightness_large(): u8 payload[2] = { b>>8, b&0xff }.
     * The deprecated non-large helper is the opposite order: never use it for sofef00. */
    out[0] = (uint8_t)(brightness >> 8);
    out[1] = (uint8_t)(brightness & 0xffu);
}

int dsi_dcs_write_packet(unsigned channel, const uint8_t *tx, uint16_t tx_len,
                         struct dsi_dcs_packet *out) {
    if (!tx || !out || channel > 3u || tx_len == 0u ||
        tx_len > kDsiDcsPayloadBytes) return -1;

    const uint8_t type = dsi_dcs_write_type(tx_len);
    if (type == 0) return -1;

    /* mipi_dsi_create_packet(): zero the whole packet, then fill header[0] and the
     * format-dependent header[1..2]. header[3] (ECC) stays 0 -- hardware fills it. */
    for (unsigned i = 0; i < kDsiDcsHeaderBytes; i++) out->header[i] = 0;
    for (unsigned i = 0; i < kDsiDcsPayloadBytes; i++) out->payload[i] = 0;
    out->payload_length = 0;
    out->size = kDsiDcsHeaderBytes;

    out->header[0] = (uint8_t)(((channel & 3u) << 6) | (type & 0x3fu));

    if (type == kDsiDcsLongWrite) {
        /* Long packet: word count is the full tx_len, payload follows the header. */
        out->header[1] = (uint8_t)(tx_len & 0xffu);
        out->header[2] = (uint8_t)((tx_len >> 8) & 0xffu);
        for (unsigned i = 0; i < tx_len; i++) out->payload[i] = tx[i];
        out->payload_length = tx_len;
        out->size = (uint16_t)(kDsiDcsHeaderBytes + tx_len);
    } else {
        /* Short packet: up to two parameter bytes live in header[1..2]. For a DCS short
         * write the first tx byte is the DCS command itself, so tx_len 1 -> header[1]=cmd,
         * tx_len 2 -> header[1]=cmd, header[2]=param. */
        out->header[1] = tx[0];
        if (tx_len > 1u) out->header[2] = tx[1];
    }
    return 0;
}

int dsi_dcs_set_brightness_large(unsigned channel, uint16_t brightness,
                                 struct dsi_dcs_packet *out) {
    uint8_t tx[3];
    uint8_t payload[2];
    dsi_dcs_brightness_payload(dsi_dcs_clamp_brightness(brightness), payload);
    /* mipi_dsi_dcs_write(dsi, 0x51, payload, 2): tx = [0x51, hi, lo]. */
    tx[0] = kDcsSetDisplayBrightness;
    tx[1] = payload[0];
    tx[2] = payload[1];
    return dsi_dcs_write_packet(channel, tx, 3u, out);
}

int dsi_dcs_write_control_display(unsigned channel, uint8_t value,
                                  struct dsi_dcs_packet *out) {
    uint8_t tx[2] = {kDcsWriteControlDisplay, value};
    return dsi_dcs_write_packet(channel, tx, 2u, out);
}

int dsi_dcs_write_power_save(unsigned channel, uint8_t value,
                             struct dsi_dcs_packet *out) {
    uint8_t tx[2] = {kDcsWritePowerSave, value};
    return dsi_dcs_write_packet(channel, tx, 2u, out);
}
