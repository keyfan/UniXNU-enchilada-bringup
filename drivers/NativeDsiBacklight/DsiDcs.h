// SPDX-License-Identifier: BSD-2-Clause-Patent
// DSI DCS packet construction for the OnePlus 6 (enchilada) Samsung sofef00 AMOLED panel.
// This is the transport layer of the stage-3 backlight: the panel's brightness is a DSI
// DCS command, not an I2C backlight IC. The board's vendor DTS says
//   qcom,mdss-dsi-bl-pmic-control-type = "bl_ctrl_dcs";
//   qcom,mdss-brightness-max-level = <0x3ff>;
// and mainline drivers/gpu/drm/panel/panel-samsung-sofef00.c calls
// mipi_dsi_dcs_set_display_brightness_large(dsi, backlight_get_brightness(bl)).
//
// The byte-level rules are those of Linux v6.12 drivers/gpu/drm/drm_mipi_dsi.c and
// include/video/mipi_display.h (recorded here as offsets and constants only, not copied
// code): mipi_dsi_dcs_set_display_brightness_large() builds payload {b>>8, b&0xff};
// mipi_dsi_dcs_write() prepends the DCS command byte; mipi_dsi_dcs_write_buffer() picks
// DCS_SHORT_WRITE (0x05) / DCS_SHORT_WRITE_PARAM (0x15) / DCS_LONG_WRITE (0x39) from the
// total length; mipi_dsi_create_packet() puts ((channel&3)<<6)|(type&0x3f) in header[0],
// the word count in header[1..2] for long packets, and up to two parameter bytes there for
// short packets. ECC (header[3]) is left to the DSI host hardware, exactly as Linux does.
//
// Nothing here touches a register: it only fills a packet structure, so it can be compiled
// and asserted on the host before the MDSS DSI0 command-channel driver and the
// AppleARMBacklight-facing IOService are written.

#ifndef ENCHILADA_DSI_DCS_H
#define ENCHILADA_DSI_DCS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* MIPI DSI data types (include/video/mipi_display.h) */
    kDsiDcsShortWrite = 0x05,       /* DCS short write, no parameter */
    kDsiDcsShortWriteParam = 0x15,  /* DCS short write, one parameter */
    kDsiDcsRead = 0x06,
    kDsiDcsLongWrite = 0x39,

    /* MIPI DCS commands (include/video/mipi_display.h) */
    kDcsSetDisplayBrightness = 0x51,
    kDcsGetDisplayBrightness = 0x52,
    kDcsWriteControlDisplay = 0x53,
    kDcsGetControlDisplay = 0x54,
    kDcsWritePowerSave = 0x55,

    /* sofef00 / enchilada vendor DTS: bl_ctrl_dcs, bl-min 1, bl-max 0x3ff */
    kEnchiladaBrightnessMin = 1,
    kEnchiladaBrightnessMax = 1023,

    kDsiDcsHeaderBytes = 4,
    kDsiDcsPayloadBytes = 8, /* every DCS write this driver issues fits in 8 bytes */
};

/* A DSI packet as mipi_dsi_create_packet() describes it: a 4-byte header, an optional
 * payload, and the total size (header + payload). The payload is embedded so a packet is
 * self-contained and never dangles. */
struct dsi_dcs_packet {
    uint8_t header[kDsiDcsHeaderBytes];
    uint8_t payload[kDsiDcsPayloadBytes];
    uint16_t payload_length;
    uint16_t size;
};

/* mipi_dsi_dcs_write_buffer(): 0 -> EINVAL, 1 -> 0x05, 2 -> 0x15, >=3 -> 0x39.
 * tx_len is the TOTAL buffer length (DCS command byte + parameters). */
uint8_t dsi_dcs_write_type(uint16_t tx_len);

/* Clamp a brightness to the panel's 1..1023 DCS range. */
uint16_t dsi_dcs_clamp_brightness(uint16_t brightness);

/* mipi_dsi_dcs_set_display_brightness_large(): payload = {b>>8, b&0xff} (big-endian).
 * NOTE the non-large mipi_dsi_dcs_set_display_brightness() uses the REVERSED byte order;
 * sofef00 uses the large form, so this is the one we must issue. */
void dsi_dcs_brightness_payload(uint16_t brightness, uint8_t out[2]);

/* Build a DSI packet from a DCS write buffer. Returns 0, or -1 for invalid arguments
 * (channel > 3, tx_len == 0, tx_len > capacity, null pointers). */
int dsi_dcs_write_packet(unsigned channel, const uint8_t *tx, uint16_t tx_len,
                         struct dsi_dcs_packet *out);

/* SET_DISPLAY_BRIGHTNESS, large form, as one DCS long write:
 * tx = [0x51, b>>8, b&0xff], type = 0x39, word count = 3. */
int dsi_dcs_set_brightness_large(unsigned channel, uint16_t brightness,
                                 struct dsi_dcs_packet *out);

/* Panel-on helpers from panel-samsung-sofef00.c:sofef00_panel_on():
 *   MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20  (enable the brightness control block)
 *   MIPI_DCS_WRITE_POWER_SAVE,      0x00
 * Both are 2-byte writes -> DCS_SHORT_WRITE_PARAM (0x15). */
int dsi_dcs_write_control_display(unsigned channel, uint8_t value,
                                  struct dsi_dcs_packet *out);
int dsi_dcs_write_power_save(unsigned channel, uint8_t value,
                             struct dsi_dcs_packet *out);

#ifdef __cplusplus
}
#endif

#endif /* ENCHILADA_DSI_DCS_H */
