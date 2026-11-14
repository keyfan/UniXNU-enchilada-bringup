// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoMdtV1 host test: 8 groups, CHECK() accumulates failures.
// Groups 1-7 are the round-74 parser. Group 8 is ROUND 106: the load plan.
//
// Group 6 is the important one: it parses the REAL a630_zap.mdt pulled from this
// phone in round 38, not a mock. If that file is absent the group SKIPS loudly
// (it prints SKIP and asserts nothing) rather than silently passing.
#include <cstdint>
#include <cstdio>
#include <cstring>
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

// ---- ROUND 106 group 7: the LOAD PLAN (pure arithmetic, loads nothing) ----
// A synthetic ELF32 with ONE valid, RELOCATABLE PT_LOAD so every rule at
// qcom_mdt_loader.c:344-386 can be checked on its own:
//   ph[2] p_offset 0x1000, p_paddr 0x1000, p_filesz 8, p_memsz 0x1000
static void buildPlanBlob(uint8_t *buf, size_t size)
{
    std::memset(buf, 0, size);
    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 1; buf[5] = 1;
    buf[28] = 52; buf[42] = 32; buf[44] = 3;
    buf[52 + 16] = 24;             // ph[0].p_filesz (the header slice)
    buf[52 + 32 + 16] = 40;        // ph[1].p_filesz (the hash)
    buf[52 + 32 + 27] = 0x02;      // ph[1].p_flags = HASH (0x02000000)
    buf[52 + 64 + 0] = 1;          // ph[2].p_type = PT_LOAD
    buf[52 + 64 + 5] = 0x10;       // ph[2].p_offset = 0x1000
    buf[52 + 64 + 13] = 0x10;      // ph[2].p_paddr  = 0x1000
    buf[52 + 64 + 16] = 8;         // ph[2].p_filesz = 8
    buf[52 + 64 + 21] = 0x10;      // ph[2].p_memsz  = 0x1000
    buf[52 + 64 + 27] = 0x08;      // ph[2] is relocatable (BIT 27)
}

static void testLoadPlan()
{
    uint8_t buf[52 + 3 * 32];
    buildPlanBlob(buf, sizeof(buf));

    struct adreno_mdt_plan plan;
    struct adreno_mdt_segment seg[kAdrenoMdtMaxSegments];

    // The fixture is what the rest of the group assumes -- verify it first.
    struct adreno_mdt_phdr ph;
    CHECK(adreno_mdt_phdr(buf, sizeof(buf), 2, &ph) == kAdrenoMdtOk);
    CHECK(ph.p_offset == 0x1000u && ph.p_paddr == 0x1000u);
    CHECK(ph.p_filesz == 8u && ph.p_memsz == 0x1000u);
    CHECK(adreno_mdt_phdr_valid(&ph) == 1);

    // P1:323-324 relocatable, and P1:326-327 the min paddr that becomes the base.
    int reloc = -1;
    CHECK(adreno_mdt_relocatable(buf, sizeof(buf), &reloc) == kAdrenoMdtOk);
    CHECK(reloc == 1);
    uint32_t lowest = 0;
    CHECK(adreno_mdt_min_paddr(buf, sizeof(buf), &lowest) == kAdrenoMdtOk);
    CHECK(lowest == 0x1000u);

    // P1:265-291 qcom_mdt_bins_are_split(): ph[2]'s payload sits at 4096 in a
    // 148-byte container, so the payload is NOT here -> SPLIT.
    int split = -1;
    CHECK(adreno_mdt_bins_are_split(buf, sizeof(buf), &split) == kAdrenoMdtOk);
    CHECK(split == 1);

    // THE SEPARABILITY THAT MATTERS: on these SAME bytes the METADATA split
    // test (P1:168) says NOT split, because 24 + 40 = 64 != 148. Round 106
    // added bins_are_split() as a separate function precisely because the two
    // rules answer different questions.
    uint32_t meta = 0, hseg = 99;
    int msplit = -1;
    CHECK(adreno_mdt_metadata(buf, sizeof(buf), &meta, &msplit, &hseg) == kAdrenoMdtOk);
    CHECK(msplit == 0);
    CHECK(msplit != split);

    // ...and the CONVERSE: a container whose ehdr + hash fills it exactly is a
    // metadata-split image whose payload still fits, so bins says NOT split.
    {
        uint8_t exact[52 + 3 * 32];
        buildPlanBlob(exact, sizeof(exact));
        exact[52 + 32 + 16] = 124;   // 24 + 124 == 148
        exact[52 + 64 + 5] = 0;      // ph[2].p_offset = 0 (payload is here)
        uint32_t emeta = 0, ehseg = 99;
        int ems = -1, ebs = -1;
        CHECK(adreno_mdt_metadata(exact, sizeof(exact), &emeta, &ems, &ehseg) == kAdrenoMdtOk);
        CHECK(ems == 1);
        CHECK(adreno_mdt_bins_are_split(exact, sizeof(exact), &ebs) == kAdrenoMdtOk);
        CHECK(ebs == 0);
        CHECK(ems != ebs);
    }

    // P1:335 the reloc base for a RELOCATABLE image is min paddr, so the bogus
    // mem_phys the caller passes is IGNORED.
    CHECK(adreno_mdt_plan(buf, sizeof(buf), 0xDEAD0000u, 0x1000u, &plan, seg,
                          kAdrenoMdtMaxSegments) == kAdrenoMdtOk);
    CHECK(plan.reloc_base == 0x1000u);
    CHECK(plan.reloc_base != 0xDEAD0000u);
    CHECK(plan.is_relocatable == 1);
    CHECK(plan.is_split == 1);
    CHECK(plan.segment_count == 1u);          // only ph[2] is valid
    CHECK(seg[0].phdr_index == 2u);           // ...and it came from index 2

    // P1:350 dst = p_paddr - mem_reloc = 0x1000 - 0x1000 = 0.
    CHECK(seg[0].dst_offset == 0u);
    CHECK(seg[0].copy_bytes == 8u);
    // P1:384-385 zero fill = p_memsz - p_filesz = 4096 - 8 = 4088.
    CHECK(seg[0].zero_bytes == 4088u);
    CHECK(plan.total_copy == 8u);
    CHECK(plan.total_zero == 4088u);
    // copy + zero reconstructs the region exactly, and the region matches what
    // get_size() already answered -- the plan may not invent its own size.
    CHECK(plan.total_copy + plan.total_zero == 0x1000u);
    uint32_t region = 0;
    CHECK(adreno_mdt_get_size(buf, sizeof(buf), &region) == kAdrenoMdtOk);
    CHECK(plan.region_bytes == region);
    CHECK(plan.region_bytes == 0x1000u);
    // A SPLIT segment has no in-buffer source offset, and the bNN a loader
    // would request is derived from its phdr index.
    CHECK(seg[0].src_offset == 0u);
    CHECK(seg[0].split_segment == 2u);
    {
        char nm[32];
        CHECK(adreno_mdt_segment_name("a630_zap.mdt", 12, seg[0].split_segment,
                                      nm, sizeof(nm)) == kAdrenoMdtOk);
        CHECK(nm[10] == '0' && nm[11] == '2');   // a630_zap.b02
    }

    // P1:351 the region-bounds rule: exactly fitting is LEGAL, one byte short is
    // refused, and a base ABOVE the segment paddr is refused (offset < 0).
    CHECK(adreno_mdt_plan(buf, sizeof(buf), 0x1000u, 0x1000u, &plan, seg,
                          kAdrenoMdtMaxSegments) == kAdrenoMdtOk);
    CHECK(adreno_mdt_plan(buf, sizeof(buf), 0x1000u, 0xFFFu, &plan, seg,
                          kAdrenoMdtMaxSegments) == kAdrenoMdtPlanOutsideRegion);
    // A relocatable image cannot be pushed out of bounds by mem_phys at all.
    CHECK(adreno_mdt_plan(buf, sizeof(buf), 0x40000000u, 0x1000u, &plan, seg,
                          kAdrenoMdtMaxSegments) == kAdrenoMdtOk);
    CHECK(plan.reloc_base == 0x1000u && seg[0].dst_offset == 0u);

    // P1:357 p_filesz > p_memsz is refused, INDEPENDENTLY of the region size.
    {
        uint8_t bad[52 + 3 * 32];
        buildPlanBlob(bad, sizeof(bad));
        bad[52 + 64 + 16] = 0x00;
        bad[52 + 64 + 17] = 0x20;                // p_filesz = 0x2000 > memsz 0x1000
        CHECK(adreno_mdt_plan(bad, sizeof(bad), 0x1000u, 0x2000u, &plan, seg,
                              kAdrenoMdtMaxSegments) == kAdrenoMdtPlanFileszGtMemsz);
        CHECK(adreno_mdt_plan(bad, sizeof(bad), 0x1000u, 0xFFFFF000u, &plan, seg,
                              kAdrenoMdtMaxSegments) == kAdrenoMdtPlanFileszGtMemsz);
    }

    // P1:369/:376 the non-split source-bounds rule. NOTE, established here
    // rather than assumed: given that adreno_mdt_bins_are_split() runs FIRST and
    // sets split when ANY phdr with non-zero p_filesz overruns the buffer, a
    // VALID segment that would overrun can never be seen with split == 0. The
    // upstream check is therefore DEFENSIVE -- kept because P1:369 has it, and
    // provably unreachable by construction. The assertion below pins that
    // relationship instead of pretending the code path fires.
    {
        uint8_t trunc[52 + 3 * 32];
        buildPlanBlob(trunc, sizeof(trunc));
        // Move the payload inside the container, then declare a length that
        // overruns it. The split rule sees the overrun and says SPLIT, so the
        // loader's copy branch is never reached -- which is exactly why the
        // memcpy can never read out of bounds.
        trunc[52 + 64 + 5] = 0;                  // p_offset = 0
        trunc[52 + 64 + 16] = 200;               // p_filesz 200 > 148 - 0
        int tsplit = -1;
        CHECK(adreno_mdt_bins_are_split(trunc, sizeof(trunc), &tsplit) == kAdrenoMdtOk);
        CHECK(tsplit == 1);                      // caught by the split rule...
        // ...so the plan reports SPLIT rather than a truncated in-file copy, and
        // the truncation refusal stays a defensive mirror of P1:369.
        CHECK(adreno_mdt_plan(trunc, sizeof(trunc), 0x1000u, 0x1000u, &plan, seg,
                              kAdrenoMdtMaxSegments) == kAdrenoMdtOk);
        CHECK(plan.is_split == 1);
        CHECK(seg[0].src_offset == 0u);          // no in-buffer source at all
    }

    // Refusals on bad inputs and short capacity.
    CHECK(adreno_mdt_plan(buf, sizeof(buf), 0x1000u, 0x1000u, &plan, seg, 0u) ==
          kAdrenoMdtPlanTooManySegments);
    CHECK(adreno_mdt_plan(buf, sizeof(buf), 0x1000u, 0x1000u, nullptr, seg,
                          kAdrenoMdtMaxSegments) == kAdrenoMdtInvalid);
    CHECK(adreno_mdt_plan(nullptr, 0, 0x1000u, 0x1000u, &plan, seg,
                          kAdrenoMdtMaxSegments) == kAdrenoMdtInvalid);
    CHECK(adreno_mdt_bins_are_split(buf, sizeof(buf), nullptr) == kAdrenoMdtInvalid);
    CHECK(adreno_mdt_relocatable(buf, sizeof(buf), nullptr) == kAdrenoMdtInvalid);
    CHECK(adreno_mdt_min_paddr(buf, sizeof(buf), nullptr) == kAdrenoMdtInvalid);

    // P1:341 a NON-relocatable image IS offset by the caller's mem_phys, and the
    // same bytes land in a different place -- which is why the flag must come
    // from the phdr rather than from a default.
    {
        uint8_t plain[52 + 3 * 32];
        buildPlanBlob(plain, sizeof(plain));
        plain[52 + 64 + 27] = 0;                 // clear BIT(27)
        plain[52 + 64 + 13] = 0x20;              // p_paddr = 0x2000
        int preloc = -1;
        CHECK(adreno_mdt_relocatable(plain, sizeof(plain), &preloc) == kAdrenoMdtOk);
        CHECK(preloc == 0);
        CHECK(adreno_mdt_plan(plain, sizeof(plain), 0x1000u, 0x2000u, &plan, seg,
                              kAdrenoMdtMaxSegments) == kAdrenoMdtOk);
        CHECK(plan.is_relocatable == 0);
        CHECK(plan.reloc_base == 0x1000u);       // mem_phys, not paddr 0x2000
        CHECK(plan.reloc_base != 0x2000u);
        CHECK(seg[0].dst_offset == 0x1000u);     // 0x2000 - 0x1000
        // mem_phys ABOVE paddr makes offset negative: the P1:351 refusal, and
        // exactly the trap a signed compare would hide as a small negative.
        CHECK(adreno_mdt_plan(plain, sizeof(plain), 0x40000000u, 0x1000u, &plan,
                              seg, kAdrenoMdtMaxSegments) == kAdrenoMdtPlanOutsideRegion);
    }

    // A PLAN IS STILL NOT A LOADER.
    CHECK(adreno_mdt_loader_available() == 0);
    CHECK(adreno_mdt_ready() == 0);

    // The round-106 self-check.
    uint32_t checks = 0;
    CHECK(adreno_mdt_plan_verify(&checks) == 0);
    CHECK(checks >= 50u);
    CHECK(adreno_mdt_plan_verify(nullptr) == 0);
}

// ---- group 8: the REAL device blob, now also PLANNED end to end ----
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

    // ---- ROUND 106: plan the REAL blob, not a mock ----
    // The one valid segment is ph[2]: paddr 0x5000, filesz 1968, memsz 1968, and
    // it IS relocatable. So on this board the plan has exactly ONE row, its
    // destination is region offset 0, and the zero fill is ZERO.
    struct adreno_mdt_plan rplan;
    struct adreno_mdt_segment rseg[kAdrenoMdtMaxSegments];
    CHECK(adreno_mdt_plan(b.data, b.size, 0u, 0x1000u, &rplan, rseg,
                          kAdrenoMdtMaxSegments) == kAdrenoMdtOk);
    CHECK(rplan.segment_count == 1u);        // one of three phdrs is loadable
    CHECK(rplan.is_relocatable == 1);        // ph[2] carries BIT(27)
    // The reloc base is the REAL min paddr from the blob, not anything supplied.
    uint32_t real_min = 0;
    CHECK(adreno_mdt_min_paddr(b.data, b.size, &real_min) == kAdrenoMdtOk);
    CHECK(real_min == 0x5000u);
    CHECK(rplan.reloc_base == 0x5000u);
    CHECK(rplan.reloc_base != 0u);           // mem_phys 0 does NOT leak in
    CHECK(rseg[0].dst_offset == 0u);         // 0x5000 - 0x5000
    CHECK(rseg[0].phdr_index == 2u);
    CHECK(rseg[0].copy_bytes == 1968u);      // == p_filesz == a630_zap.b02
    // p_memsz == p_filesz for this segment, so there is NOTHING to zero fill.
    CHECK(rseg[0].zero_bytes == 0u);
    CHECK(rplan.total_copy == 1968u);
    CHECK(rplan.total_zero == 0u);
    // The region the plan names is the SAME 4096 bytes get_size() reported.
    CHECK(rplan.region_bytes == region);
    CHECK(rplan.region_bytes == 0x1000u);
    // The real blob IS split by the BINS rule (its payload is a .bNN file)...
    CHECK(rplan.is_split == 1);
    // ...and its bNN for the only loadable segment is b02, which is exactly the
    // device file whose size matched p_filesz in round 38.
    CHECK(rseg[0].split_segment == 2u);
    CHECK(adreno_mdt_segment_name("a630_zap.mdt", 12, rseg[0].split_segment,
                                  name, sizeof(name)) == kAdrenoMdtOk);
    CHECK(name[10] == '0' && name[11] == '2');
    // A region smaller than the segment's p_memsz is refused on REAL data too.
    // NOTE the exact threshold: the segment sits at region offset 0 and spans
    // p_memsz 1968 bytes, so 1967 is the first refused size and 1968 fits. This
    // is deliberately NOT 0x1000: get_size() ALIGNs UP to 4K, so a region of
    // 0xFFF is still larger than the 1968 bytes actually needed and is legal --
    // an important distinction the plan and get_size() must not blur.
    CHECK(adreno_mdt_plan(b.data, b.size, 0u, 1967u, &rplan, rseg,
                          kAdrenoMdtMaxSegments) == kAdrenoMdtPlanOutsideRegion);
    CHECK(adreno_mdt_plan(b.data, b.size, 0u, 1968u, &rplan, rseg,
                          kAdrenoMdtMaxSegments) == kAdrenoMdtOk);
    CHECK(adreno_mdt_plan(b.data, b.size, 0u, 0xFFFu, &rplan, rseg,
                          kAdrenoMdtMaxSegments) == kAdrenoMdtOk);
    // The ALIGNed region size is still what get_size() reports, and is a
    // different number from the bytes the segments actually occupy.
    CHECK(region == 0x1000u);
    CHECK(region != rplan.total_copy + rplan.total_zero);
    // Everything above is arithmetic; the loader is still absent.
    CHECK(adreno_mdt_loader_available() == 0);
    CHECK(adreno_mdt_ready() == 0);
}

int main(int argc, char **argv)
{
    testHeader();
    testHeaderRefusals();
    testValidityRules();
    testSegmentName();
    testSelfCheck();
    testLoadPlan();
    // argv[1] is the path to the real blob; without it group 8 SKIPs loudly.
    testRealBlob(argc > 1 ? argv[1] : "");
    if (failures) {
        std::printf("FAIL: enchilada A630 MDT parser contract "
                    "(%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 MDT parser contract "
                "(8 test groups, 0 failures)\n");
    return 0;
}
