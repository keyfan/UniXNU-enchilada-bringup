// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test of EnchiladaTouchRpmh.h: the command-database walk against a model database built the way the AOP lays it out, the two
// RSC register layouts, and the exact write sequence of one active request and of the return to idle. No hardware.
#include "../EnchiladaTouchRpmh.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

void put16(std::vector<uint8_t> &db, size_t at, uint16_t v) {
    db[at] = (uint8_t)(v & 0xff);
    db[at + 1] = (uint8_t)(v >> 8);
}
void put32(std::vector<uint8_t> &db, size_t at, uint32_t v) {
    put16(db, at, (uint16_t)(v & 0xffff));
    put16(db, at + 2, (uint16_t)(v >> 16));
}

// A database with one slave and the given names, each at its own address: slave id 1, header at 0, data after the entry table.
struct Model {
    std::vector<uint8_t> db;
    Model(std::initializer_list<std::pair<const char *, uint32_t>> entries, uint16_t levels = 0) {
        db.assign(0x1000, 0);
        put32(db, 0, 1);
        put32(db, 4, FTB_CMD_DB_MAGIC);
        put16(db, 8, 1);                  // slave id
        put16(db, 10, 0);                 // header offset from byte 144
        put16(db, 12, (uint16_t)(entries.size() * 24));   // data offset: right after the entry table
        put16(db, 14, (uint16_t)entries.size());
        size_t e = 0;
        for (const auto &entry : entries) {
            const size_t at = 144 + 24 * e;
            std::memset(&db[at], 0, 8);
            std::strncpy(reinterpret_cast<char *>(&db[at]), entry.first, 8);
            put32(db, at + 16, entry.second);
            put16(db, at + 20, 2);        // aux length: one level
            put16(db, at + 22, 0);        // aux offset 0
            e++;
        }
        put16(db, 144 + 24 * entries.size(), levels);
    }
};

void testDatabase() {
    const Model model({{"ldoa28", 0x1234}, {"smpa2", 0x5678}}, 7);
    ftb_rpmh_resource r;
    // An eight-character name fills the id field; only an exact match is taken.
    assert(ftb_cmd_db_find(model.db.data(), model.db.size(), "ldoa28", &r));
    assert(r.address == 0x1234 && r.level_count == 1 && r.levels[0] == 7);
    assert(ftb_cmd_db_find(model.db.data(), model.db.size(), "smpa2", &r) && r.address == 0x5678);
    // A prefix of a name is a different name, not a match.
    assert(!ftb_cmd_db_find(model.db.data(), model.db.size(), "ldoa2", &r));
    assert(!ftb_cmd_db_find(model.db.data(), model.db.size(), "ldoa280", &r));
    assert(!ftb_cmd_db_find(model.db.data(), model.db.size(), "ldoa29", &r));
    // A database without the magic is not a database; nor is a truncated one.
    std::vector<uint8_t> bad = model.db;
    put32(bad, 4, 0);
    assert(!ftb_cmd_db_find(bad.data(), bad.size(), "ldoa28", &r));
    assert(!ftb_cmd_db_find(model.db.data(), 100, "ldoa28", &r));
    // The entry count is what the walk trusts: claiming more entries than the buffer holds is rejected, not read past.
    std::vector<uint8_t> greedy = model.db;
    put16(greedy, 14, 0xffff);
    assert(!ftb_cmd_db_find(greedy.data(), greedy.size(), "ldoa28", &r));
}

void testLayouts() {
    const ftb_rsc_layout old = ftb_rsc_layout_for(0x00000002);   // version 0
    assert(old.tcs_stride == 672 && old.cmd_stride == 20);
    assert(old.irq_status == 0x04 && old.irq_clear == 0x08 && old.wait_for_cmpl == 0x10);
    assert(old.control == 0x14 && old.status == 0x18 && old.cmd_enable == 0x1c);
    assert(old.cmd_msgid == 0x30 && old.cmd_addr == 0x34 && old.cmd_data == 0x38);
    const ftb_rsc_layout moved = ftb_rsc_layout_for(0x00030000);   // version 3 moved every TCS register
    assert(moved.tcs_stride == 672 && moved.cmd_stride == 24);
    assert(moved.wait_for_cmpl == 0x20 && moved.control == 0x24 && moved.status == 0x28 && moved.cmd_enable == 0x2c);
    assert(moved.cmd_msgid == 0x34 && moved.cmd_addr == 0x38 && moved.cmd_data == 0x3c);
    // The major version is bits 23:16 of the id register.
    assert(ftb_rsc_layout_for(0x0002ffff).cmd_stride == 20 && ftb_rsc_layout_for(0x0003ffff).cmd_stride == 24);
}

void testRequests() {
    const ftb_rsc_layout l = ftb_rsc_layout_for(0x00000002);   // this board's DRV 2 reads version 0
    const uint32_t address = 0x1234, value = 2856;
    ftb_rpmh_write w[16];
    const unsigned n = ftb_rpmh_active_request(&l, 1, &address, &value, 1, w, 16);
    assert(n == 9);
    const uint32_t base = 672;   // TCS 1
    // Clear the control register, clear this TCS's done bit, then the three command words.
    assert(w[0].offset == base + l.control && w[0].value == 0);
    assert(w[1].offset == l.irq_clear && w[1].value == 1u << 1);
    assert(w[2].offset == base + l.cmd_msgid && w[2].value == FTB_MSGID_WRITE_RESPONSE);
    assert(w[3].offset == base + l.cmd_addr && w[3].value == address);
    assert(w[4].offset == base + l.cmd_data && w[4].value == value);
    // One completion mask for the one command, then active mode, and the trigger only after that.
    assert(w[5].offset == base + l.wait_for_cmpl && w[5].value == 1);
    assert(w[6].offset == base + l.cmd_enable && w[6].value == 1);
    assert(w[7].offset == base + l.control && w[7].value == FTB_CONTROL_ACTIVE_MODE);
    assert(w[8].offset == base + l.control && w[8].value == (FTB_CONTROL_ACTIVE_MODE | FTB_CONTROL_TRIGGER));
    // Four commands is the widest request; five is refused, and a buffer that cannot hold 3n+6 is refused too.
    const uint32_t a[5] = {1, 2, 3, 4, 5}, v[5] = {10, 20, 30, 40, 50};
    ftb_rpmh_write wide[18];
    assert(ftb_rpmh_active_request(&l, 0, a, v, 4, wide, 18) == 18);
    assert(ftb_rpmh_active_request(&l, 0, a, v, 4, wide, 17) == 0);
    assert(ftb_rpmh_active_request(&l, 0, a, v, 5, wide, 18) == 0);
    assert(ftb_rpmh_active_request(&l, 0, a, v, 1, w, 8) == 0);
    assert(ftb_rpmh_active_request(&l, 0, a, v, 0, w, 16) == 0);
    ftb_rpmh_write done[3];
    assert(ftb_rpmh_finish(&l, 1, done, 3) == 3);
    assert(done[0].offset == l.irq_clear && done[0].value == 1u << 1);
    assert(done[1].offset == base + l.control && done[1].value == 0);
    assert(done[2].offset == base + l.cmd_enable && done[2].value == 0);
    assert(ftb_rpmh_finish(&l, 1, done, 2) == 0);
}

void testVotes() {
    // The three votes in the DT's order and values, each at its own register of the resource.
    const ftb_rpmh_resource res = {0x1234, {0}, 0};
    uint32_t addresses[3], values[3];
    assert(ftb_ldo28_votes(&res, addresses, values));
    assert(addresses[0] == 0x1234 && values[0] == 2856);
    assert(addresses[1] == 0x1238 && values[1] == 1);
    assert(addresses[2] == 0x123c && values[2] == 2);
    assert(!ftb_ldo28_votes(nullptr, addresses, values));
    const ftb_rpmh_resource empty = {0, {0}, 0};
    assert(!ftb_ldo28_votes(&empty, addresses, values));
    // The board's own addresses: cmd-db and the apps RSC's TCS block.
    assert(FTB_CMD_DB_BASE == 0x861e0000ULL && FTB_APPS_RSC_BASE == 0x179e0000ULL && FTB_RSC_TCS == 0xd00);
    assert(FTB_CMD_DB_SIZE == 0x20000 && FTB_APPS_RSC_SIZE == 0x10000);
}

}   // namespace

int main() {
    testDatabase();
    testLayouts();
    testRequests();
    testVotes();
    std::printf("PASS: enchilada touch RPMh (%d checks)\n", 4);
    return 0;
}
