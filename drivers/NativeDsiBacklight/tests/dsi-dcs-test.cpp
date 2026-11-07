// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for the enchilada DSI DCS backlight transport. The expected bytes are
// derived from Linux v6.12 drivers/gpu/drm/drm_mipi_dsi.c and include/video/mipi_display.h,
// and from panel-samsung-sofef00.c (the OnePlus 6 sofef00 panel). No hardware is modelled:
// the packet builder is pure byte manipulation, so the assertions are exact wire bytes.
// Build: scripts/check-enchilada-dsi-backlight-host.py
#include "DsiDcs.h"
#include <cassert>
#include <cstdio>

static void testWriteType() {
    /* mipi_dsi_dcs_write_buffer(): 0 invalid, 1 -> 0x05, 2 -> 0x15, >=3 -> 0x39. */
    assert(dsi_dcs_write_type(0) == 0);
    assert(dsi_dcs_write_type(1) == kDsiDcsShortWrite);
    assert(dsi_dcs_write_type(2) == kDsiDcsShortWriteParam);
    assert(dsi_dcs_write_type(3) == kDsiDcsLongWrite);
    assert(dsi_dcs_write_type(8) == kDsiDcsLongWrite);
}

static void testClamp() {
    /* Vendor DTS: bl-min-level = 1, bl-max-level = 0x3ff (1023). */
    assert(dsi_dcs_clamp_brightness(0) == 1);
    assert(dsi_dcs_clamp_brightness(1) == 1);
    assert(dsi_dcs_clamp_brightness(512) == 512);
    assert(dsi_dcs_clamp_brightness(1023) == 1023);
    assert(dsi_dcs_clamp_brightness(1024) == 1023);
    assert(dsi_dcs_clamp_brightness(65535) == 1023);
}

static void testBrightnessPayloadBigEndian() {
    /* The LARGE form is {b>>8, b&0xff}. The non-large helper is the opposite order;
     * sofef00 uses the large one, so a reversed result here would break the panel. */
    uint8_t out[2];
    dsi_dcs_brightness_payload(1023, out);
    assert(out[0] == 0x03 && out[1] == 0xff);
    dsi_dcs_brightness_payload(512, out);
    assert(out[0] == 0x02 && out[1] == 0x00);
    dsi_dcs_brightness_payload(1, out);
    assert(out[0] == 0x00 && out[1] == 0x01);
}

static void testSetBrightnessLarge() {
    struct dsi_dcs_packet p;
    /* 1023 -> tx [0x51,0x03,0xff], channel 0, long write 0x39, word count 3. */
    assert(dsi_dcs_set_brightness_large(0, 1023, &p) == 0);
    assert(p.header[0] == 0x39);           /* ((0&3)<<6) | 0x39 */
    assert(p.header[1] == 3 && p.header[2] == 0);
    assert(p.header[3] == 0);              /* ECC left to hardware */
    assert(p.payload[0] == 0x51 && p.payload[1] == 0x03 && p.payload[2] == 0xff);
    assert(p.payload_length == 3 && p.size == 7);
    for (unsigned i = 3; i < kDsiDcsPayloadBytes; i++) assert(p.payload[i] == 0);

    /* Channel 2 changes only the top bits of header[0]. */
    assert(dsi_dcs_set_brightness_large(2, 512, &p) == 0);
    assert(p.header[0] == 0xb9);           /* (2<<6)|0x39 == 0x80|0x39 */
    assert(p.header[1] == 3);
    assert(p.payload[0] == 0x51 && p.payload[1] == 0x02 && p.payload[2] == 0x00);

    /* Off-range values are clamped before encoding, never truncated. */
    assert(dsi_dcs_set_brightness_large(0, 0, &p) == 0);
    assert(p.payload[1] == 0x00 && p.payload[2] == 0x01);   /* 1 */
    assert(dsi_dcs_set_brightness_large(0, 4095, &p) == 0);
    assert(p.payload[1] == 0x03 && p.payload[2] == 0xff);   /* 1023 */
}

static void testShortWrites() {
    struct dsi_dcs_packet p;
    /* sofef00_panel_on(): WRITE_CONTROL_DISPLAY 0x53 / 0x20 (short, one parameter). */
    assert(dsi_dcs_write_control_display(0, 0x20, &p) == 0);
    assert(p.header[0] == kDsiDcsShortWriteParam);  /* 0x15 */
    assert(p.header[1] == 0x53 && p.header[2] == 0x20 && p.header[3] == 0);
    assert(p.payload_length == 0 && p.size == 4);
    for (unsigned i = 0; i < kDsiDcsPayloadBytes; i++) assert(p.payload[i] == 0);

    /* WRITE_POWER_SAVE 0x55 / 0x00. */
    assert(dsi_dcs_write_power_save(0, 0x00, &p) == 0);
    assert(p.header[0] == kDsiDcsShortWriteParam);
    assert(p.header[1] == 0x55 && p.header[2] == 0x00);

    /* A bare one-byte DCS command (e.g. EXIT_SLEEP_MODE 0x11) is a 0x05 short write. */
    const uint8_t nop[1] = {0x11};
    assert(dsi_dcs_write_packet(0, nop, 1, &p) == 0);
    assert(p.header[0] == kDsiDcsShortWrite);
    assert(p.header[1] == 0x11 && p.header[2] == 0);
    assert(p.payload_length == 0 && p.size == 4);

    /* Channel high bits on a short packet too. */
    const uint8_t ctrl[2] = {0x53, 0x20};
    assert(dsi_dcs_write_packet(3, ctrl, 2, &p) == 0);
    assert(p.header[0] == 0xd5);           /* (3<<6)|0x15 == 0xc0|0x15 */
}

static void testLongWriteGeneric() {
    /* A 5-byte DCS buffer exercises the generic long-write path: word count = 5,
     * payload copied verbatim, size = 4 + 5. */
    const uint8_t tx[5] = {0x2a, 0x00, 0x00, 0x04, 0x37};
    struct dsi_dcs_packet p;
    assert(dsi_dcs_write_packet(1, tx, 5, &p) == 0);
    assert(p.header[0] == 0x79);           /* (1<<6)|0x39 */
    assert(p.header[1] == 5 && p.header[2] == 0);
    for (unsigned i = 0; i < 5; i++) assert(p.payload[i] == tx[i]);
    assert(p.payload_length == 5 && p.size == 9);
}

static void testRefusals() {
    struct dsi_dcs_packet p;
    const uint8_t tx[2] = {0x53, 0x20};
    /* Invalid arguments leave the output untouched and return -1. */
    struct dsi_dcs_packet untouched;
    for (unsigned i = 0; i < kDsiDcsHeaderBytes; i++) untouched.header[i] = 0xa5;
    for (unsigned i = 0; i < kDsiDcsPayloadBytes; i++) untouched.payload[i] = 0x5a;
    untouched.payload_length = 77;
    untouched.size = 88;
    p = untouched;
    assert(dsi_dcs_write_packet(0, nullptr, 2, &p) == -1);
    assert(dsi_dcs_write_packet(0, tx, 2, nullptr) == -1);
    assert(dsi_dcs_write_packet(4, tx, 2, &p) == -1);
    assert(dsi_dcs_write_packet(0, tx, 0, &p) == -1);
    assert(dsi_dcs_write_packet(0, tx, 9, &p) == -1);
    assert(p.header[0] == 0xa5 && p.header[1] == 0xa5 && p.header[3] == 0xa5);
    assert(p.payload[0] == 0x5a && p.payload_length == 77 && p.size == 88);

    /* The setters propagate the refusal on bad channel. */
    assert(dsi_dcs_set_brightness_large(4, 500, &p) == -1);
    assert(dsi_dcs_write_control_display(4, 0x20, &p) == -1);
}

int main() {
    testWriteType();
    testClamp();
    testBrightnessPayloadBigEndian();
    testSetBrightnessLarge();
    testShortWrites();
    testLongWriteGeneric();
    testRefusals();
    std::printf("PASS: enchilada DSI DCS backlight transport (7 test groups)\n");
    return 0;
}
