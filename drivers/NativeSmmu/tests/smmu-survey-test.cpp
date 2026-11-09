// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test for drivers/NativeSmmu/SmmuSurvey.c: a fake SMMUv2 register file
// driven only through the public read callback. No hardware, no MMIO.
//
// The register values in each fixture are the ones this port has actually seen
// reported, so the expectations stay tied to the device:
//   * geometry 0x40000080 / 0x50000040 / 0x5111  (SmmuTakeover.cpp:93, the
//     SDM845-shaped reset() used by the takeover tests: 128 slots, 64 banks,
//     32 global pages)
//   * slot 6 / bank 4 valid bypass for SID 0x60   (memory area inputs: UFS)
//   * SMR 0x80000740 for SID 0x740                (memory area inputs: DWC3)
//   * IDR1[31] set => 64K geometry, which must be refused
#include "SmmuSurvey.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace {

constexpr uint32_t kSlots = 128;
constexpr uint32_t kRegisterBytes = 0x1000 + 4 * kSlots;

struct Fake {
    std::vector<uint32_t> registers = std::vector<uint32_t>(kRegisterBytes / 4, 0);
    unsigned calls = 0;
    int fail_at = -1;   // 1-based call index that fails, -1 never
    bool fail = false;

    uint32_t &at(uint32_t byte_offset) { return registers[byte_offset / 4]; }

    void reset() {
        std::fill(registers.begin(), registers.end(), 0);
        calls = 0;
        fail_at = -1;
        fail = false;
        // SDM845-shaped identification, from SmmuTakeover.cpp:93.
        at(0x20) = 0x40000080u;   // 128 slots, EXIDS clear
        at(0x24) = 0x50000040u;   // 64 banks, 32 global pages
        at(0x28) = 0x00005111u;
        at(0x00) = 0x00000406u;   // sCR0 as the SMMU report expects
        at(0x48) = 0;
    }

    // Programs one valid stream-match entry (SMR[31] set, bypass S2CR).
    void add_stream(uint32_t slot, uint32_t sid, uint32_t bank, uint32_t type) {
        at(0x800 + 4 * slot) = 0x80000000u | sid;
        at(0xc00 + 4 * slot) = (type << 16) | bank;
    }
};

int read32(void *ctx, uint32_t offset, uint32_t *out) {
    auto *fake = static_cast<Fake *>(ctx);
    ++fake->calls;
    if (fake->fail || (fake->fail_at >= 0 && fake->calls == static_cast<unsigned>(fake->fail_at))) {
        return 1;
    }
    if (offset + 4 > kRegisterBytes || (offset & 3u)) return 1;
    *out = fake->registers[offset / 4];
    return 0;
}

smmu_survey_io io_for(Fake &fake) {
    smmu_survey_io io{};
    io.ctx = &fake;
    io.read32 = read32;
    return io;
}

// --- the geometric facts every later test relies on -------------------------

void testGeometrySdm845() {
    Fake fake;
    fake.reset();
    smmu_survey_geometry geometry{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyOk);
    CHECK(geometry.slots == 128u);
    CHECK(geometry.banks == 64u);
    // IDR1[30:28] = 5 -> 1 << (5 + 1) = 64 global pages, exactly
    // drivers/NativeUfs/NativeUfs.cpp:915. (64 + 64) * 4096 == 0x80000, so this
    // geometry uses the whole window and must still be accepted.
    CHECK(geometry.pages == 64u);
    CHECK(geometry.stride == 4096u);
    CHECK(geometry.exids == 0u);
    CHECK(geometry.id0 == 0x40000080u);
    CHECK(geometry.id1 == 0x50000040u);
    CHECK(geometry.id2 == 0x00005111u);
    CHECK(geometry.scr0 == 0x00000406u);
    CHECK(geometry.gfsr == 0u);
    CHECK(fake.calls == 5u);   // IDR0, IDR1, IDR2, sCR0, sGFSR
}

// "Never guess 64K register geometry" (NativeUfs/tests/SmmuTakeover.cpp:33).
void testRefuses64KGeometry() {
    Fake fake;
    fake.reset();
    fake.at(0x24) |= 0x80000000u;
    smmu_survey_geometry geometry{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyUnsupported);
    // Still fully initialised: the identification reads are reported even when
    // the geometry is refused, so a kernel log can say WHY it refused.
    CHECK(geometry.id1 == 0xd0000040u);
    CHECK(geometry.slots == 128u);
}

void testRefusesEmptyGeometry() {
    Fake fake;
    fake.reset();
    fake.at(0x20) = 0x40000000u;   // zero slots
    smmu_survey_geometry geometry{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyUnsupported);

    fake.reset();
    fake.at(0x24) = 0x50000000u;   // zero banks
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyUnsupported);

    // 129+ global pages with 64 banks would need more than the 0x80000 window.
    fake.reset();
    fake.at(0x24) = 0x50000040u | (7u << 28);   // pages = 1 << (7+1) = 256
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyUnsupported);
}

void testNullAndIoErrors() {
    smmu_survey_geometry geometry{};
    CHECK(smmu_survey_geometry_read(nullptr, &geometry) == kSmmuSurveyNull);
    smmu_survey_io io{};
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyNull);
    smmu_survey_result result{};
    CHECK(smmu_survey_scan(nullptr, &geometry, &result) == kSmmuSurveyNull);

    Fake fake;
    fake.reset();
    smmu_survey_io good = io_for(fake);
    CHECK(smmu_survey_geometry_read(&good, &geometry) == kSmmuSurveyOk);
    fake.calls = 0;
    fake.fail_at = 1;
    CHECK(smmu_survey_geometry_read(&good, &geometry) == kSmmuSurveyIo);
}

// --- the scan itself -------------------------------------------------------

void testFindsUfsAndUsbStreams() {
    Fake fake;
    fake.reset();
    fake.add_stream(6, 0x0060u, 4, kSmmuSurveyBypass);   // UFS, as console206 reported
    fake.add_stream(7, 0x0740u, 4, kSmmuSurveyBypass);   // DWC3, SMR 0x80000740
    smmu_survey_geometry geometry{};
    smmu_survey_result result{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyOk);
    CHECK(smmu_survey_scan(&io, &geometry, &result) == kSmmuSurveyOk);

    CHECK(result.valid == 2u);
    CHECK(result.type_count[kSmmuSurveyBypass] == 2u);
    CHECK(result.type_count[kSmmuSurveyTranslation] == 0u);
    CHECK(result.type_count[kSmmuSurveyFault] == 0u);
    CHECK(result.first_free == 0u);   // slot 0 is free in this fixture

    CHECK(result.ufs.found == 1);
    CHECK(result.ufs.sid == 0x0060u);
    CHECK(result.ufs.slot == 6u);
    CHECK(result.ufs.bank == 4u);
    CHECK(result.ufs.type == kSmmuSurveyBypass);
    CHECK(result.ufs.translated == 0u);
    CHECK(result.ufs.smr == 0x80000060u);

    CHECK(result.usb.found == 1);
    CHECK(result.usb.sid == 0x0740u);
    CHECK(result.usb.slot == 7u);
    CHECK(result.usb.bank == 4u);
    CHECK(result.usb.smr == 0x80000740u);
    // 128 SMR reads + 2 S2CR reads: free slots never read their S2CR.
    CHECK(result.reads == 130u);
}

void testAbsentStreamsAreReportedNotFound() {
    Fake fake;
    fake.reset();
    smmu_survey_geometry geometry{};
    smmu_survey_result result{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyOk);
    CHECK(smmu_survey_scan(&io, &geometry, &result) == kSmmuSurveyOk);
    CHECK(result.valid == 0u);
    CHECK(result.first_free == 0u);
    CHECK(result.ufs.found == 0);
    CHECK(result.usb.found == 0);
    CHECK(result.ufs.slot == 0u);
    CHECK(result.reads == 128u);   // every slot is free, so only SMR reads
}

// A translation-enabled or faulting entry must be classified, not hidden: the
// takeover decision depends on telling bypass from translation.
void testClassifiesTranslationAndFault() {
    Fake fake;
    fake.reset();
    fake.add_stream(2, 0x0060u, 9, kSmmuSurveyTranslation);
    fake.add_stream(3, 0x0740u, 11, kSmmuSurveyFault);
    smmu_survey_geometry geometry{};
    smmu_survey_result result{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyOk);
    CHECK(smmu_survey_scan(&io, &geometry, &result) == kSmmuSurveyOk);

    CHECK(result.valid == 2u);
    CHECK(result.type_count[kSmmuSurveyTranslation] == 1u);
    CHECK(result.type_count[kSmmuSurveyFault] == 1u);
    CHECK(result.type_count[kSmmuSurveyBypass] == 0u);

    CHECK(result.ufs.found == 1);
    CHECK(result.ufs.slot == 2u);
    CHECK(result.ufs.bank == 9u);
    CHECK(result.ufs.type == kSmmuSurveyTranslation);
    CHECK(result.ufs.translated == 1u);

    CHECK(result.usb.found == 1);
    CHECK(result.usb.slot == 3u);
    CHECK(result.usb.type == kSmmuSurveyFault);
    CHECK(result.usb.translated == 1u);
}

// The match must honour the mask bits: an SMR carrying a mask that covers the
// SID still matches, and one that conflicts does not.
void testHonoursSmrMask() {
    Fake fake;
    fake.reset();
    // Mask bits [16..] = 0x0003, id 0x0060: covers SIDs 0x60..0x63.
    fake.at(0x800 + 4 * 5) = 0x80000000u | (0x0003u << 16) | 0x0060u;
    fake.at(0xc00 + 4 * 5) = 0u | 7u;
    // A non-matching entry for a nearby SID that the mask does NOT cover.
    fake.at(0x800 + 4 * 8) = 0x80000000u | (0x0001u << 16) | 0x0004u;
    fake.at(0xc00 + 4 * 8) = 0u | 2u;
    smmu_survey_geometry geometry{};
    smmu_survey_result result{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyOk);
    CHECK(smmu_survey_scan(&io, &geometry, &result) == kSmmuSurveyOk);
    CHECK(result.ufs.found == 1);
    CHECK(result.ufs.slot == 5u);
    CHECK(result.ufs.bank == 7u);
    CHECK(result.usb.found == 0);
    CHECK(result.valid == 2u);
}

// First match wins, and the scan is deterministic: two runs over the same
// register file produce byte-identical results.
void testDeterministic() {
    Fake fake;
    fake.reset();
    fake.add_stream(6, 0x0060u, 4, kSmmuSurveyBypass);
    fake.add_stream(7, 0x0740u, 4, kSmmuSurveyBypass);
    smmu_survey_geometry g1{}, g2{};
    smmu_survey_result r1{}, r2{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &g1) == kSmmuSurveyOk);
    CHECK(smmu_survey_scan(&io, &g1, &r1) == kSmmuSurveyOk);
    CHECK(smmu_survey_geometry_read(&io, &g2) == kSmmuSurveyOk);
    CHECK(smmu_survey_scan(&io, &g2, &r2) == kSmmuSurveyOk);
    CHECK(std::memcmp(&g1, &g2, sizeof g1) == 0);
    CHECK(std::memcmp(&r1, &r2, sizeof r1) == 0);
}

// --- formatting ------------------------------------------------------------

void testFormatIsStableAndBounded() {
    Fake fake;
    fake.reset();
    fake.add_stream(6, 0x0060u, 4, kSmmuSurveyBypass);
    fake.add_stream(7, 0x0740u, 4, kSmmuSurveyBypass);
    smmu_survey_geometry geometry{};
    smmu_survey_result result{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyOk);
    CHECK(smmu_survey_scan(&io, &geometry, &result) == kSmmuSurveyOk);

    char big[512];
    std::memset(big, 0x7f, sizeof big);
    const size_t length = smmu_survey_format(&geometry, &result, big, sizeof big);
    CHECK(length > 0u);
    CHECK(length < sizeof big);
    CHECK(big[length] == 0);
    CHECK(std::strstr(big, "SS slots=128 banks=64 pages=64 exids=0") == big);
    CHECK(std::strstr(big, "valid=2 t0=2 t1=0 t2=0") != nullptr);
    CHECK(std::strstr(big, "ufs=6/4/bypass") != nullptr);
    CHECK(std::strstr(big, "usb=7/4/bypass") != nullptr);

    // Every smaller buffer must be truncated, still NUL-terminated, and never
    // write past its end.
    for (size_t cap = 1; cap <= length; ++cap) {
        std::vector<char> small(cap + 8, static_cast<char>(0x7f));
        const size_t written = smmu_survey_format(&geometry, &result, small.data(), cap);
        CHECK(small[cap - 1] == 0 || written + 1 <= cap);
        CHECK(small[cap] == static_cast<char>(0x7f));   // never past the end
        CHECK(written + 1 <= cap);
    }

    // A fresh result with no streams says so explicitly rather than printing 0/0.
    smmu_survey_result empty{};
    empty.first_free = 0u;
    char line[512];
    const size_t n = smmu_survey_format(&geometry, &empty, line, sizeof line);
    CHECK(n > 0u);
    CHECK(std::strstr(line, "ufs=none") != nullptr);
    CHECK(std::strstr(line, "usb=none") != nullptr);

    // NULL arguments never dereference.
    CHECK(smmu_survey_format(nullptr, &result, big, sizeof big) == 0u);
    CHECK(smmu_survey_format(&geometry, nullptr, big, sizeof big) == 0u);
    CHECK(smmu_survey_format(&geometry, &result, nullptr, 16u) == 0u);
    CHECK(smmu_survey_format(&geometry, &result, big, 0u) == 0u);
}

void testTypeNames() {
    CHECK(std::strcmp(smmu_survey_type_name(kSmmuSurveyBypass), "bypass") == 0);
    CHECK(std::strcmp(smmu_survey_type_name(kSmmuSurveyTranslation), "translation") == 0);
    CHECK(std::strcmp(smmu_survey_type_name(kSmmuSurveyFault), "fault") == 0);
    CHECK(std::strcmp(smmu_survey_type_name(3u), "reserved") == 0);
    CHECK(std::strcmp(smmu_survey_type_name(99u), "reserved") == 0);
}

// A scan must not touch a slot's S2CR when its SMR is invalid: that is both a
// speed property and a safety property (the fewest possible bus accesses).
void testDoesNotReadS2crOfFreeSlots() {
    Fake fake;
    fake.reset();
    fake.at(0x800 + 4 * 100) = 0x80000000u | 0x0060u;
    fake.at(0xc00 + 4 * 100) = 4u;
    smmu_survey_geometry geometry{};
    smmu_survey_result result{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyOk);
    CHECK(smmu_survey_scan(&io, &geometry, &result) == kSmmuSurveyOk);
    CHECK(result.valid == 1u);
    CHECK(result.first_free == 0u);
    CHECK(result.ufs.slot == 100u);
    CHECK(result.reads == 129u);   // 128 SMR + exactly one S2CR
}

void testMidScanIoFailurePropagates() {
    Fake fake;
    fake.reset();
    fake.add_stream(6, 0x0060u, 4, kSmmuSurveyBypass);
    smmu_survey_geometry geometry{};
    smmu_survey_result result{};
    smmu_survey_io io = io_for(fake);
    CHECK(smmu_survey_geometry_read(&io, &geometry) == kSmmuSurveyOk);
    fake.fail_at = static_cast<int>(fake.calls) + 7;
    CHECK(smmu_survey_scan(&io, &geometry, &result) == kSmmuSurveyIo);
}

}  // namespace

int main() {
    testGeometrySdm845();
    testRefuses64KGeometry();
    testRefusesEmptyGeometry();
    testNullAndIoErrors();
    testFindsUfsAndUsbStreams();
    testAbsentStreamsAreReportedNotFound();
    testClassifiesTranslationAndFault();
    testHonoursSmrMask();
    testDeterministic();
    testFormatIsStableAndBounded();
    testTypeNames();
    testDoesNotReadS2crOfFreeSlots();
    testMidScanIoFailurePropagates();
    std::puts("PASS: enchilada apps_smmu read-only survey (13 test groups)");
    return 0;
}
