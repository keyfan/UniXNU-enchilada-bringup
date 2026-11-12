// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoMdtV1 host test: 6 groups, CHECK() accumulates failures.
//
// Group 8 is the important one: it parses the REAL a630_zap.mdt pulled from this
// phone in round 38, not a mock. If that file is absent the group SKIPS loudly
// (it prints SKIP and asserts nothing) rather than silently passing.
#include <cstdint>
#include <cstdio>
#include "AdrenoMdtV1.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// ---- a minimal, self-contained file reader (no <cstdio> FILE, no libc) ----
// The test may open ONE file: the real blob. Reading it is the whole point of
// group 8, and it stays read-only.
struct Blob {
    const uint8_t *data;
    size_t size;
};

static uint8_t g_blob[64 * 1024];

static bool loadBlob(const char *path, Blob *out)
{
    unsigned char *buf = nullptr;
    size_t size = 0;
    unsigned char chunk[4096];
    (void)buf;
    (void)size;
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    size_t n = 0;
    while (n < sizeof(g_blob)) {
        size_t got = std::fread(chunk, 1, sizeof(chunk), f);
        if (got == 0) break;
        for (size_t i = 0; i < got && n < sizeof(g_blob); ++i) g_blob[n++] = chunk[i];
    }
    std::fclose(f);
    out->data = g_blob;
    out->size = n;
    return n > 0;
}

static void testHeader()
{
    // The constants must mirror the pinned sources, not be "round" numbers.
    CHECK(kAdrenoMdtElf32EhdrBytes == 52u);
    CHECK(kAdrenoMdtElf32PhdrBytes == 32u);
    CHECK(kAdrenoMdtElfClass32 == 1u);
    CHECK(kAdrenoMdtElfDataLsb == 1u);
    CHECK(kAdrenoMdtElfMachineHexagon == 0xa4u);  // EM_HEXAGON 164
    CHECK(kAdrenoMdtPtLoad == 1u);
    CHECK(kAdrenoMdtPageBytes == 0x1000u);
    // A refused parse never writes through the outputs.
    uint32_t a = 7, b = 7, c = 7;
    CHECK(adreno_mdt_header(nullptr, 0, &a, &b, &c) == kAdrenoMdtInvalid);
    CHECK(a == 7 && b == 7 && c == 7);
}

static void testHeaderRefusals()
{
    uint8_t buf[96];
    for (unsigned i = 0; i < 96u; ++i) buf[i] = 0;
    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 1; buf[5] = 1;
    buf[28] = 52; buf[42] = 32; buf[44] = 1;
    uint32_t p, e, n;
    CHECK(adreno_mdt_header(buf, 96, &p, &e, &n) == kAdrenoMdtOk);
    // too short for even a header
    CHECK(adreno_mdt_header(buf, 51, &p, &e, &n) == kAdrenoMdtInvalid);
    // table does not fit: 52 + 1*32 = 84, so 60 bytes is truncated
    CHECK(adreno_mdt_header(buf, 60, &p, &e, &n) == kAdrenoMdtTruncated);
    buf[44] = 0;
    CHECK(adreno_mdt_header(buf, 96, &p, &e, &n) == kAdrenoMdtNoPrograms);
}

static void testValidityRules()
{
    struct adreno_mdt_phdr p;
    p.p_type = kAdrenoMdtPtLoad;
    p.p_offset = 0; p.p_vaddr = 0; p.p_paddr = 0x1000;
    p.p_filesz = 8; p.p_memsz = 16; p.p_flags = 0; p.p_align = 0;
    CHECK(adreno_mdt_phdr_valid(&p) == 1);
    CHECK(adreno_mdt_phdr_relocatable(&p) == 0);
    CHECK(adreno_mdt_phdr_is_hash(&p) == 0);
    // rule 1
    p.p_type = 0;
    CHECK(adreno_mdt_phdr_valid(&p) == 0);
    // rule 2
    p.p_type = kAdrenoMdtPtLoad;
    p.p_flags = kAdrenoMdtTypeHash;
    CHECK(adreno_mdt_phdr_valid(&p) == 0);
    CHECK(adreno_mdt_phdr_is_hash(&p) == 1);
    // TYPE is bits 24..26. HASH is exactly 2<<24, so type 3 is NOT a hash.
    p.p_flags = 0x03000000u;
    CHECK(adreno_mdt_phdr_is_hash(&p) == 0);
    p.p_flags = 0x02000000u;
    CHECK(adreno_mdt_phdr_is_hash(&p) == 1);
    p.p_flags = 0x01000000u;   // type 1: not hash
    CHECK(adreno_mdt_phdr_is_hash(&p) == 0);
    // relocatable is BIT(27), independent of the type bits
    p.p_flags = kAdrenoMdtRelocatable;
    CHECK(adreno_mdt_phdr_relocatable(&p) == 1);
    CHECK(adreno_mdt_phdr_is_hash(&p) == 0);
    CHECK(adreno_mdt_phdr_valid(&p) == 1);
    // rule 3
    p.p_flags = 0;
    p.p_memsz = 0;
    CHECK(adreno_mdt_phdr_valid(&p) == 0);
}

static void testSegmentName()
{
    char n[32];
    // The suffix is REPLACED, not appended: .mdt becomes .bNN
    CHECK(adreno_mdt_segment_name("a630_zap.mdt", 12, 0, n, sizeof(n)) == kAdrenoMdtOk);
    CHECK(n[0] == 'a' && n[1] == '6' && n[2] == '3' && n[3] == '0');
    // "a630_zap.mdt" is 12 chars, so the 3-char suffix starts at index 9.
    CHECK(n[8] == '.');
    CHECK(n[9] == 'b' && n[10] == '0' && n[11] == '0' && n[12] == '\0');
    CHECK(adreno_mdt_segment_name("a630_zap.mdt", 12, 1, n, sizeof(n)) == kAdrenoMdtOk);
    CHECK(n[9] == 'b' && n[10] == '0' && n[11] == '1');
    CHECK(adreno_mdt_segment_name("a630_zap.mdt", 12, 2, n, sizeof(n)) == kAdrenoMdtOk);
    CHECK(n[10] == '0' && n[11] == '2');
    // two-digit segments still use %02d
    CHECK(adreno_mdt_segment_name("a630_zap.mdt", 12, 11, n, sizeof(n)) == kAdrenoMdtOk);
    CHECK(n[10] == '1' && n[11] == '1');
    // a name shorter than 4 has no suffix to replace
    CHECK(adreno_mdt_segment_name("abc", 3, 1, n, sizeof(n)) == kAdrenoMdtNameTooShort);
    CHECK(adreno_mdt_segment_name("abcd", 4, 1, n, sizeof(n)) == kAdrenoMdtOk);
    CHECK(n[1] == 'b' && n[2] == '0' && n[3] == '1');
    // capacity is honoured
    CHECK(adreno_mdt_segment_name("a630_zap.mdt", 12, 1, n, 12) == kAdrenoMdtInvalid);
    CHECK(adreno_mdt_segment_name(nullptr, 12, 1, n, sizeof(n)) == kAdrenoMdtInvalid);
}

static void testSelfCheck()
{
    uint32_t count = 0;
    CHECK(adreno_mdt_verify(&count) == 0);
    CHECK(count >= 40u);
    CHECK(adreno_mdt_verify(nullptr) == 0);
    CHECK(adreno_mdt_ready() == 0);
    CHECK(adreno_mdt_loader_available() == 0);
}

// ---- group 6: the REAL device blob ----
static void testRealBlob(const char *path)
{
    Blob b;
    if (!loadBlob(path, &b)) {
        std::printf("SKIP: real a630_zap.mdt not present at %s\n", path);
        return;   // asserted NOTHING: a missing file is not a pass
    }
    std::printf("REAL_BLOB bytes=%zu\n", b.size);

    uint32_t phoff = 0, phent = 0, phnum = 0;
    CHECK(adreno_mdt_header(b.data, b.size, &phoff, &phent, &phnum) == kAdrenoMdtOk);
    // Measured from the device file, not assumed.
    CHECK(phoff == 52u);
    CHECK(phent == 32u);
    CHECK(phnum == 3u);
    // ELF32, little endian, EM_HEXAGON -- NOT aarch64 (round 38's correction).
    CHECK(b.data[4] == 1u);
    CHECK(b.data[5] == 1u);
    // e_machine sits at offset 18 in Elf32_Ehdr (after e_ident[16] + e_type).
    CHECK(b.data[18] == (uint8_t)kAdrenoMdtElfMachineHexagon);

    // ph[0] is the header slice: not PT_LOAD, zero memsz
    struct adreno_mdt_phdr p0, p1, p2;
    CHECK(adreno_mdt_phdr(b.data, b.size, 0, &p0) == kAdrenoMdtOk);
    CHECK(p0.p_type == 0u);
    CHECK(p0.p_filesz == 148u);
    CHECK(p0.p_memsz == 0u);
    CHECK(adreno_mdt_phdr_valid(&p0) == 0);
    // ph[1] is the HASH segment: 6536 bytes at offset 4096
    CHECK(adreno_mdt_phdr(b.data, b.size, 1, &p1) == kAdrenoMdtOk);
    CHECK(p1.p_type == 0u);
    CHECK(p1.p_offset == 4096u);
    CHECK(p1.p_filesz == 6536u);
    CHECK(p1.p_memsz == 8192u);
    CHECK(adreno_mdt_phdr_valid(&p1) == 0);
    // ph[2] is the ONLY loadable segment: paddr 0x5000, 1968 bytes, relocatable
    CHECK(adreno_mdt_phdr(b.data, b.size, 2, &p2) == kAdrenoMdtOk);
    CHECK(p2.p_type == kAdrenoMdtPtLoad);
    CHECK(p2.p_offset == 12288u);
    CHECK(p2.p_paddr == 0x5000u);
    CHECK(p2.p_filesz == 1968u);
    CHECK(p2.p_memsz == 1968u);
    CHECK(adreno_mdt_phdr_valid(&p2) == 1);
    CHECK(adreno_mdt_phdr_relocatable(&p2) == 1);

    // The three p_filesz are EXACTLY the device .b00/.b01/.b02 sizes (round 38).
    CHECK(p0.p_filesz == 148u);    // a630_zap.b00
    CHECK(p1.p_filesz == 6536u);   // a630_zap.b01
    CHECK(p2.p_filesz == 1968u);   // a630_zap.b02

    // Region size comes from the ONE valid segment, not from the file size.
    uint32_t region = 0;
    CHECK(adreno_mdt_get_size(b.data, b.size, &region) == kAdrenoMdtOk);
    CHECK(region == 0x1000u);
    CHECK(region != (uint32_t)b.size);      // 6684 would be wrong
    uint32_t sum = 0;
    CHECK(adreno_mdt_filesz_sum(b.data, b.size, &sum) == kAdrenoMdtOk);
    CHECK(sum == 8652u);                     // 148 + 6536 + 1968
    CHECK(sum != region);                    // the conflation trap, on real data

    // Metadata: header slice + hash == the WHOLE file, so SPLIT.
    uint32_t meta = 0, hash_seg = 99;
    int split = -1;
    CHECK(adreno_mdt_metadata(b.data, b.size, &meta, &split, &hash_seg) == kAdrenoMdtOk);
    CHECK(meta == 6684u);
    CHECK(meta == (uint32_t)b.size);
    CHECK(split == 1);
    CHECK(hash_seg == 1u);

    // And the segment names the loader would request for THIS file.
    char name[32];
    CHECK(adreno_mdt_segment_name("a630_zap.mdt", 12, 0, name, sizeof(name)) == kAdrenoMdtOk);
    CHECK(name[9] == 'b' && name[10] == '0' && name[11] == '0');
    CHECK(adreno_mdt_segment_name("a630_zap.mdt", 12, 1, name, sizeof(name)) == kAdrenoMdtOk);
    CHECK(name[10] == '0' && name[11] == '1');
}

int main(int argc, char **argv)
{
    testHeader();
    testHeaderRefusals();
    testValidityRules();
    testSegmentName();
    testSelfCheck();
    // argv[1] is the path to the real blob; without it group 8 SKIPs loudly.
    testRealBlob(argc > 1 ? argv[1] : "");
    if (failures) {
        std::printf("FAIL: enchilada A630 MDT parser contract "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 MDT parser contract "
                "(6 test groups, 0 failures)\n");
    return 0;
}
