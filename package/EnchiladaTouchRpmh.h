// SPDX-License-Identifier: BSD-2-Clause-Patent
// The RPMh half of the enchilada touchscreen's power sequence: find PM8998 LDO28 in the AOP command database and describe the
// register writes of one active-only request in a TCS of the application processor's RSC. Nothing here touches hardware -- the probe
// maps the two windows and applies the writes, so the same code is exercised by the host test against a model database.
//
// Why it exists: vreg_l28a_3p0 (PM8998 LDO28) is the controller's VDD. Linux raises it with an RPMh regulator vote; this kernel had
// no RPMh user at all, so the rail stayed wherever the bootloader left it. The 1.8 V enable on GPIO 88 powers the level shifter, not
// the controller, which is why the kernel-155 ACK probe saw two idle lines and a silent part instead of an address NACK.
//
// The layout and the write order are sheng's package/QcomRpmh.c (SM8550, verified there); the addresses and the resource name are
// this board's own:
//   cmd-db      enchilada.dts qcom,cmd-db@861e0000
//   apps RSC    enchilada.dts mailbox@179e0000, label "apps_rsc", TCS block at +0xd00, qcom,drv-id = <2>
//   resource    enchilada.dts rpmh-regulator-ldoa28, qcom,resource-name = "ldoa28"
// A VRM resource takes its voltage in mV at +0, its enable at +4 and its mode at +8.
#ifndef ENCHILADA_TOUCH_RPMH_H
#define ENCHILADA_TOUCH_RPMH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define FTB_CMD_DB_BASE 0x861e0000ULL
#define FTB_CMD_DB_SIZE 0x20000u
#define FTB_APPS_RSC_BASE 0x179e0000ULL
#define FTB_APPS_RSC_SIZE 0x10000u
#define FTB_RSC_TCS 0xd00u
#define FTB_CMD_DB_MAGIC 0x0c0330dbu
#define FTB_RPMH_MAX_LEVELS 16u
// PM8998 LDO28: the DT's init is 0x2b9440 uV (2.856 V) in mode 2, the same two values Linux sends.
#define FTB_LDO28_MV 2856u
#define FTB_LDO28_MODE 2u

typedef struct {
    uint32_t address;
    uint16_t levels[FTB_RPMH_MAX_LEVELS];
    unsigned level_count;
} ftb_rpmh_resource;

// The register layout of an RSC DRV; offsets are relative to the TCS block (DRV base + 0xd00).
typedef struct {
    uint32_t tcs_stride, cmd_stride, irq_status, irq_clear, wait_for_cmpl, control, status, cmd_enable, cmd_msgid, cmd_addr,
        cmd_data;
} ftb_rsc_layout;

typedef struct {uint32_t offset, value;} ftb_rpmh_write;

static inline uint16_t ftb_le16(const uint8_t *p) {return (uint16_t)(p[0] | (uint16_t)p[1] << 8);}
static inline uint32_t ftb_le32(const uint8_t *p) {return (uint32_t)ftb_le16(p) | (uint32_t)ftb_le16(p + 2) << 16;}
static inline bool ftb_fits(size_t at, size_t length, size_t size) {return at <= size && length <= size - at;}

// Header: u32 version, u32 magic, 8 slave records of 16 bytes (u16 id, header offset, data offset, entry count, ...), then the data
// area every offset counts from (byte 144). Entries are 24 bytes: an 8-byte id, two u32 priorities, u32 address, u16 aux length,
// u16 aux offset; the level list behind aux is a zero-padded u16 array. Returns false when the database has no such name.
static inline bool ftb_cmd_db_find(const uint8_t *db, size_t bytes, const char *name, ftb_rpmh_resource *out) {
    if (!db || !name || !out) return false;
    if (bytes < 144 || ftb_le32(db + 4) != FTB_CMD_DB_MAGIC) return false;
    char id[8] = {0};
    for (unsigned i = 0; i < 8 && name[i]; i++) id[i] = name[i];
    for (unsigned s = 0; s < 8; s++) {
        const uint8_t *slave = db + 8 + 16 * s;
        if (!ftb_le16(slave)) break;
        const size_t heads = 144u + ftb_le16(slave + 2), data = 144u + ftb_le16(slave + 4), count = ftb_le16(slave + 6);
        if (!ftb_fits(heads, count * 24, bytes)) return false;
        for (size_t e = 0; e < count; e++) {
            const uint8_t *entry = db + heads + 24 * e;
            bool same = true;
            for (unsigned i = 0; i < 8 && same; i++) same = entry[i] == (uint8_t)id[i];
            if (!same) continue;
            ftb_rpmh_resource r = {0, {0}, 0};
            r.address = ftb_le32(entry + 16);
            const size_t length = ftb_le16(entry + 20), at = data + ftb_le16(entry + 22);
            if (!r.address || !ftb_fits(at, length, bytes)) return false;
            for (size_t l = 0; l + 1 < length && r.level_count < FTB_RPMH_MAX_LEVELS; l += 2) {
                const uint16_t level = ftb_le16(db + at + l);
                if (r.level_count && !level) break;
                r.levels[r.level_count++] = (uint16_t)level;
            }
            *out = r;
            return true;
        }
    }
    return false;
}

// The layout for a DRV whose id register reads drv_id (major version in bits 23:16); version 3 moved the TCS registers.
static inline ftb_rsc_layout ftb_rsc_layout_for(uint32_t drv_id) {
    ftb_rsc_layout l;
    if (((drv_id >> 16) & 0xff) >= 3) {
        l.tcs_stride = 672; l.cmd_stride = 24; l.irq_status = 0x04; l.irq_clear = 0x08; l.wait_for_cmpl = 0x20;
        l.control = 0x24; l.status = 0x28; l.cmd_enable = 0x2c; l.cmd_msgid = 0x34; l.cmd_addr = 0x38; l.cmd_data = 0x3c;
    } else {
        l.tcs_stride = 672; l.cmd_stride = 20; l.irq_status = 0x04; l.irq_clear = 0x08; l.wait_for_cmpl = 0x10;
        l.control = 0x14; l.status = 0x18; l.cmd_enable = 0x1c; l.cmd_msgid = 0x30; l.cmd_addr = 0x34; l.cmd_data = 0x38;
    }
    return l;
}

#define FTB_MSGID_WRITE_RESPONSE (8u | 1u << 8 | 1u << 16)   /* 4-byte payload, response requested, write */
#define FTB_CONTROL_ACTIVE_MODE (1u << 16)
#define FTB_CONTROL_TRIGGER (1u << 24)

// The writes of an active request of count (<= 4) commands in TCS tcs, in order. The control register is cleared before the TCS is
// reprogrammed, then set to active mode, and only after that triggered.
static inline unsigned ftb_rpmh_active_request(const ftb_rsc_layout *l, unsigned tcs, const uint32_t *addresses,
                                               const uint32_t *values, unsigned count, ftb_rpmh_write *w, unsigned capacity) {
    if (!count || count > 4 || capacity < 3 * count + 6) return 0;
    const uint32_t base = l->tcs_stride * tcs;
    unsigned n = 0;
    w[n].offset = base + l->control; w[n++].value = 0;
    w[n].offset = l->irq_clear; w[n++].value = 1u << tcs;
    for (unsigned c = 0; c < count; c++) {
        const uint32_t cmd = base + l->cmd_stride * c;
        w[n].offset = cmd + l->cmd_msgid; w[n++].value = FTB_MSGID_WRITE_RESPONSE;
        w[n].offset = cmd + l->cmd_addr; w[n++].value = addresses[c];
        w[n].offset = cmd + l->cmd_data; w[n++].value = values[c];
    }
    const uint32_t mask = (1u << count) - 1;
    w[n].offset = base + l->wait_for_cmpl; w[n++].value = mask;
    w[n].offset = base + l->cmd_enable; w[n++].value = mask;
    w[n].offset = base + l->control; w[n++].value = FTB_CONTROL_ACTIVE_MODE;
    w[n].offset = base + l->control; w[n++].value = FTB_CONTROL_ACTIVE_MODE | FTB_CONTROL_TRIGGER;
    return n;
}

// The writes that return the TCS to idle after its done interrupt.
static inline unsigned ftb_rpmh_finish(const ftb_rsc_layout *l, unsigned tcs, ftb_rpmh_write *w, unsigned capacity) {
    if (capacity < 3) return 0;
    const uint32_t base = l->tcs_stride * tcs;
    w[0].offset = l->irq_clear; w[0].value = 1u << tcs;
    w[1].offset = base + l->control; w[1].value = 0;
    w[2].offset = base + l->cmd_enable; w[2].value = 0;
    return 3;
}

// The three votes that bring the rail up in the DT's own order and values. A VRM resource takes voltage in mV at +0, enable at +4
// and mode at +8, so each is one command of its own request (the RSC applies them in order and the enable lands last).
static inline bool ftb_ldo28_votes(const ftb_rpmh_resource *res, uint32_t addresses[3], uint32_t values[3]) {
    if (!res || !res->address || !addresses || !values) return false;
    addresses[0] = res->address; values[0] = FTB_LDO28_MV;
    addresses[1] = res->address + 4; values[1] = 1;
    addresses[2] = res->address + 8; values[2] = FTB_LDO28_MODE;
    return true;
}

#ifdef __cplusplus
}
#endif
#endif
