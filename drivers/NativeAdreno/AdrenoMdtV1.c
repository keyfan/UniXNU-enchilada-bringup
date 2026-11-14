// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoMdtV1 -- see AdrenoMdtV1.h for provenance and the honest HOST-VERIFIED
// boundary. Freestanding C11: no libc, no I/O, no allocation.

#include "AdrenoMdtV1.h"

/* Little-endian readers. The container is explicitly EI_DATA == 1, so a plain
 * shift-compose is correct and avoids any unaligned uint32_t load, which matters
 * because the caller may hand a byte pointer into a packed firmware buffer. */
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

AdrenoMdtStatus adreno_mdt_header(const uint8_t *data, size_t size,
                                  uint32_t *phoff, uint32_t *phentsize,
                                  uint32_t *phnum)
{
    uint32_t e_phoff = 0u;
    uint16_t e_phentsize = 0u;
    uint16_t e_phnum = 0u;

    if (data == 0 || phoff == 0 || phentsize == 0 || phnum == 0) {
        return kAdrenoMdtInvalid;
    }
    if (size < kAdrenoMdtElf32EhdrBytes) {
        return kAdrenoMdtInvalid;
    }
    if (data[0] != kAdrenoMdtElfMagic0 || data[1] != kAdrenoMdtElfMagic1 ||
        data[2] != kAdrenoMdtElfMagic2 || data[3] != kAdrenoMdtElfMagic3) {
        return kAdrenoMdtBadMagic;
    }
    if (data[4] != kAdrenoMdtElfClass32) {
        return kAdrenoMdtNotElf32;
    }
    if (data[5] != kAdrenoMdtElfDataLsb) {
        return kAdrenoMdtNotLittleEndian;
    }
    /* e_phoff at 28, e_phentsize at 42, e_phnum at 44 (Elf32_Ehdr layout). */
    e_phoff = rd32(data + 28);
    e_phentsize = rd16(data + 42);
    e_phnum = rd16(data + 44);
    if (e_phentsize != kAdrenoMdtElf32PhdrBytes) {
        return kAdrenoMdtBadPhdrSize;
    }
    if (e_phnum == 0u) {
        return kAdrenoMdtNoPrograms;
    }
    /* The whole table must be inside the buffer. Overflow-safe form. */
    if (e_phoff > size) {
        return kAdrenoMdtTruncated;
    }
    if ((size - e_phoff) / kAdrenoMdtElf32PhdrBytes < e_phnum) {
        return kAdrenoMdtTruncated;
    }
    *phoff = e_phoff;
    *phentsize = e_phentsize;
    *phnum = (uint32_t)e_phnum;
    return kAdrenoMdtOk;
}

AdrenoMdtStatus adreno_mdt_phdr(const uint8_t *data, size_t size,
                                uint32_t index, struct adreno_mdt_phdr *out)
{
    uint32_t phoff = 0u, phentsize = 0u, phnum = 0u;
    const uint8_t *p = 0;
    AdrenoMdtStatus st;

    if (out == 0) {
        return kAdrenoMdtInvalid;
    }
    st = adreno_mdt_header(data, size, &phoff, &phentsize, &phnum);
    if (st != kAdrenoMdtOk) {
        return st;
    }
    if (index >= phnum) {
        return kAdrenoMdtIndexRange;
    }
    p = data + phoff + (size_t)index * kAdrenoMdtElf32PhdrBytes;
    out->p_type = rd32(p + 0);
    out->p_offset = rd32(p + 4);
    out->p_vaddr = rd32(p + 8);
    out->p_paddr = rd32(p + 12);
    out->p_filesz = rd32(p + 16);
    out->p_memsz = rd32(p + 20);
    out->p_flags = rd32(p + 24);
    out->p_align = rd32(p + 28);
    return kAdrenoMdtOk;
}

int adreno_mdt_phdr_valid(const struct adreno_mdt_phdr *phdr)
{
    if (phdr == 0) {
        return 0;
    }
    /* P1:23 not PT_LOAD -> not loadable. */
    if (phdr->p_type != kAdrenoMdtPtLoad) {
        return 0;
    }
    /* P1:26 the HASH segment is metadata, not payload. */
    if ((phdr->p_flags & kAdrenoMdtTypeMask) == kAdrenoMdtTypeHash) {
        return 0;
    }
    /* P1:29 a zero-sized segment carries nothing. */
    if (phdr->p_memsz == 0u) {
        return 0;
    }
    return 1;
}

int adreno_mdt_phdr_relocatable(const struct adreno_mdt_phdr *phdr)
{
    if (phdr == 0) {
        return 0;
    }
    return (phdr->p_flags & kAdrenoMdtRelocatable) != 0u ? 1 : 0;
}

int adreno_mdt_phdr_is_hash(const struct adreno_mdt_phdr *phdr)
{
    if (phdr == 0) {
        return 0;
    }
    return (phdr->p_flags & kAdrenoMdtTypeMask) == kAdrenoMdtTypeHash ? 1 : 0;
}

AdrenoMdtStatus adreno_mdt_get_size(const uint8_t *data, size_t size,
                                    uint32_t *region_bytes)
{
    uint32_t phnum = 0u;
    uint32_t i = 0u;
    uint32_t min_addr = 0u;
    uint32_t max_addr = 0u;
    uint32_t found = 0u;
    uint32_t phoff = 0u;
    uint32_t phentsize = 0u;
    struct adreno_mdt_phdr ph;
    AdrenoMdtStatus st;

    if (region_bytes == 0) {
        return kAdrenoMdtInvalid;
    }
    st = adreno_mdt_header(data, size, &phoff, &phentsize, &phnum);
    if (st != kAdrenoMdtOk) {
        return st;
    }
    for (i = 0u; i < phnum; ++i) {
        st = adreno_mdt_phdr(data, size, i, &ph);
        if (st != kAdrenoMdtOk) {
            return st;
        }
        if (!adreno_mdt_phdr_valid(&ph)) {
            continue;
        }
        if (found == 0u || ph.p_paddr < min_addr) {
            min_addr = ph.p_paddr;
        }
        {
            /* ALIGN(p_paddr + p_memsz, SZ_4K), overflow-safe. */
            uint32_t end = ph.p_paddr + ph.p_memsz;
            if (end < ph.p_paddr) {
                return kAdrenoMdtInvalid; /* 32-bit overflow */
            }
            end = (end + kAdrenoMdtPageBytes - 1u) & ~(kAdrenoMdtPageBytes - 1u);
            if (end < ph.p_paddr) {
                return kAdrenoMdtInvalid;
            }
            if (found == 0u || end > max_addr) {
                max_addr = end;
            }
        }
        found += 1u;
    }
    if (found == 0u) {
        return kAdrenoMdtNoValidSegment;
    }
    if (max_addr < min_addr) {
        return kAdrenoMdtInvalid;
    }
    *region_bytes = max_addr - min_addr;
    return kAdrenoMdtOk;
}

AdrenoMdtStatus adreno_mdt_segment_name(const char *name, uint32_t name_len,
                                        uint32_t segment, char *out,
                                        uint32_t capacity)
{
    uint32_t i = 0u;

    if (name == 0 || out == 0) {
        return kAdrenoMdtInvalid;
    }
    /* P1:43-44 strlen(fw_name) < 4 -> -EINVAL: there is no 3-char suffix to
     * replace. */
    if (name_len < 4u) {
        return kAdrenoMdtNameTooShort;
    }
    if (capacity < name_len + 1u) {
        return kAdrenoMdtInvalid;
    }
    for (i = 0u; i < name_len; ++i) {
        out[i] = name[i];
    }
    /* P1:50 sprintf(seg_name + strlen(fw_name) - 3, "b%02d", segment). */
    out[name_len - 3u] = 'b';
    out[name_len - 2u] = (char)('0' + (char)((segment / 10u) % 10u));
    out[name_len - 1u] = (char)('0' + (char)(segment % 10u));
    out[name_len] = '\0';
    return kAdrenoMdtOk;
}

AdrenoMdtStatus adreno_mdt_metadata(const uint8_t *data, size_t size,
                                    uint32_t *meta_len, int *is_split,
                                    uint32_t *hash_segment)
{
    uint32_t phnum = 0u, phoff = 0u, phentsize = 0u;
    uint32_t hash_seg = 0u;
    uint32_t ehdr_size = 0u;
    uint32_t hash_size = 0u;
    uint32_t i = 0u;
    struct adreno_mdt_phdr ph0, ph;
    AdrenoMdtStatus st;

    if (meta_len == 0 || is_split == 0) {
        return kAdrenoMdtInvalid;
    }
    st = adreno_mdt_header(data, size, &phoff, &phentsize, &phnum);
    if (st != kAdrenoMdtOk) {
        return st;
    }
    /* P1:140 a metadata blob needs at least a header segment and a hash. */
    if (phnum < 2u) {
        return kAdrenoMdtNoMetadata;
    }
    st = adreno_mdt_phdr(data, size, 0u, &ph0);
    if (st != kAdrenoMdtOk) {
        return st;
    }
    /* P1:143 phdrs[0] must NOT be PT_LOAD: it is the ELF header slice. */
    if (ph0.p_type == kAdrenoMdtPtLoad) {
        return kAdrenoMdtNoMetadata;
    }
    /* P1:146-151 the hash segment is the FIRST phdr with the HASH type bits. */
    for (i = 1u; i < phnum; ++i) {
        st = adreno_mdt_phdr(data, size, i, &ph);
        if (st != kAdrenoMdtOk) {
            return st;
        }
        if (adreno_mdt_phdr_is_hash(&ph) != 0) {
            hash_seg = i;
            break;
        }
    }
    if (hash_seg == 0u) {
        return kAdrenoMdtNoMetadata;
    }
    st = adreno_mdt_phdr(data, size, hash_seg, &ph);
    if (st != kAdrenoMdtOk) {
        return st;
    }
    ehdr_size = ph0.p_filesz;
    hash_size = ph.p_filesz;
    /* P1:168 the split test: header slice + hash fills the file exactly. */
    if ((size_t)ehdr_size + (size_t)hash_size == size) {
        *is_split = 1;
    } else if ((size_t)ph.p_offset + (size_t)hash_size <= size) {
        /* P1:172 the hash lives at its own p_offset inside the file. */
        *is_split = 0;
    } else {
        return kAdrenoMdtNoMetadata;
    }
    *meta_len = ehdr_size + hash_size;
    if (hash_segment != 0) {
        *hash_segment = hash_seg;
    }
    return kAdrenoMdtOk;
}

AdrenoMdtStatus adreno_mdt_filesz_sum(const uint8_t *data, size_t size,
                                      uint32_t *sum)
{
    uint32_t phnum = 0u, phoff = 0u, phentsize = 0u;
    uint32_t i = 0u;
    uint32_t total = 0u;
    struct adreno_mdt_phdr ph;
    AdrenoMdtStatus st;

    if (sum == 0) {
        return kAdrenoMdtInvalid;
    }
    st = adreno_mdt_header(data, size, &phoff, &phentsize, &phnum);
    if (st != kAdrenoMdtOk) {
        return st;
    }
    for (i = 0u; i < phnum; ++i) {
        st = adreno_mdt_phdr(data, size, i, &ph);
        if (st != kAdrenoMdtOk) {
            return st;
        }
        if (total + ph.p_filesz < total) {
            return kAdrenoMdtInvalid;
        }
        total += ph.p_filesz;
    }
    *sum = total;
    return kAdrenoMdtOk;
}

int adreno_mdt_loader_available(void)
{
    /* No storage loader is wired to this parser: it parses a buffer the caller
     * already holds, and nothing reads a630_zap.bNN from anywhere. Round 106
     * added the load PLAN (where each segment goes) but still reads no file. */
    return 0;
}

int adreno_mdt_ready(void)
{
    /* A parser is not a loader. Derived so it flips only when the wiring
     * genuinely exists. */
    return adreno_mdt_loader_available() != 0 ? 1 : 0;
}

int adreno_mdt_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    uint32_t phoff = 0u, phentsize = 0u, phnum = 0u;
    uint32_t region = 0u, meta = 0u, sum = 0u, hash_seg = 99u;
    uint32_t i = 0u;
    int split = -1;
    char name[32];
    struct adreno_mdt_phdr ph;

    /* A synthetic ELF32 built entirely in this function, so the parser is
     * exercised without any file and without libc. Three phdrs mirroring the
     * real a630_zap.mdt shape: a header slice, a HASH segment, one PT_LOAD. */
    uint8_t buf[52 + 3 * 32];
    for (i = 0u; i < (uint32_t)sizeof(buf); ++i) {
        buf[i] = 0u;
    }
    /* NOTE: the synthetic container is 52 + 3*32 = 148 bytes. The metadata rule
     * fires the SPLIT branch only when ehdr_size + hash_size == the FILE size,
     * so the two filesz values below must sum to 148, not to 64. */
    buf[0] = 0x7fu; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 1u; /* ELFCLASS32 */
    buf[5] = 1u; /* ELFDATA2LSB */
    buf[24] = (uint8_t)kAdrenoMdtElfMachineHexagon; /* e_machine = 0xa4 */
    buf[28] = 52u;              /* e_phoff */
    buf[42] = 32u;              /* e_phentsize */
    buf[44] = 3u;               /* e_phnum */
    /* ph[0]: type 0, filesz 24 (the header slice) */
    buf[52 + 16] = 64u;
    /* ph[1]: type 0, filesz 40, flags 0x02000000 (a HASH segment).
     * p_flags occupies bytes 24..27 of the phdr, little endian, so the 0x02 that
     * matters is byte 27 minus 3 = byte 26 only when the value is 0x00020000...
     * It is NOT: 0x02000000 has its byte at index 27 of the WORD, i.e. byte 24+3.
     * Writing byte 26 would produce 0x00020000, which is NOT a HASH type. */
    buf[52 + 32 + 16] = 84u;
    buf[52 + 32 + 24] = 0x00u;
    buf[52 + 32 + 25] = 0x00u;
    buf[52 + 32 + 26] = 0x00u;
    buf[52 + 32 + 27] = 0x02u; /* flags = 0x02000000 */
    /* ph[2]: type PT_LOAD, paddr 0x1000, filesz 8, memsz 4096, relocatable */
    buf[52 + 64 + 0] = 1u;
    buf[52 + 64 + 12] = 0x00u;
    buf[52 + 64 + 13] = 0x10u;
    buf[52 + 64 + 16] = 8u;
    buf[52 + 64 + 20] = 0x00u;
    buf[52 + 64 + 21] = 0x10u;
    buf[52 + 64 + 27] = 0x08u; /* flags = 0x08000000 (relocatable) */

    /* Header parsing. */
    checks += 1u;
    if (adreno_mdt_header(buf, sizeof(buf), &phoff, &phentsize, &phnum) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (phoff != 52u || phentsize != 32u || phnum != 3u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_header(0, sizeof(buf), &phoff, &phentsize, &phnum) != kAdrenoMdtInvalid) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_header(buf, sizeof(buf), 0, &phentsize, &phnum) != kAdrenoMdtInvalid) {
        fails += 1u;
    }

    /* Refusals, each naming a distinct rule. */
    {
        uint8_t bad[64];
        for (i = 0u; i < 64u; ++i) {
            bad[i] = buf[i];
        }
        bad[0] = 'X';
        checks += 1u;
        if (adreno_mdt_header(bad, 64u, &phoff, &phentsize, &phnum) != kAdrenoMdtBadMagic) {
            fails += 1u;
        }
        bad[0] = 0x7fu;
        bad[4] = 2u; /* ELFCLASS64 */
        checks += 1u;
        if (adreno_mdt_header(bad, 64u, &phoff, &phentsize, &phnum) != kAdrenoMdtNotElf32) {
            fails += 1u;
        }
        bad[4] = 1u;
        bad[5] = 2u; /* big endian */
        checks += 1u;
        if (adreno_mdt_header(bad, 64u, &phoff, &phentsize, &phnum) != kAdrenoMdtNotLittleEndian) {
            fails += 1u;
        }
        bad[5] = 1u;
        bad[42] = 40u; /* wrong e_phentsize */
        checks += 1u;
        if (adreno_mdt_header(bad, 64u, &phoff, &phentsize, &phnum) != kAdrenoMdtBadPhdrSize) {
            fails += 1u;
        }
        bad[42] = 32u;
        bad[44] = 0u; /* no programs */
        checks += 1u;
        if (adreno_mdt_header(bad, 64u, &phoff, &phentsize, &phnum) != kAdrenoMdtNoPrograms) {
            fails += 1u;
        }
    }
    checks += 1u;
    if (adreno_mdt_header(buf, 51u, &phoff, &phentsize, &phnum) != kAdrenoMdtInvalid) {
        fails += 1u;
    }
    /* A table that runs past the buffer is TRUNCATED, not parsed. */
    checks += 1u;
    if (adreno_mdt_header(buf, 60u, &phoff, &phentsize, &phnum) != kAdrenoMdtTruncated) {
        fails += 1u;
    }

    /* Program headers. */
    checks += 1u;
    if (adreno_mdt_phdr(buf, sizeof(buf), 0u, &ph) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (ph.p_filesz != 64u) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_phdr(buf, sizeof(buf), 1u, &ph) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_phdr_is_hash(&ph) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_phdr_valid(&ph) != 0) {
        fails += 1u; /* a hash segment is never loadable */
    }
    checks += 1u;
    if (adreno_mdt_phdr(buf, sizeof(buf), 2u, &ph) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_phdr_valid(&ph) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_phdr_relocatable(&ph) != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_phdr(buf, sizeof(buf), 3u, &ph) != kAdrenoMdtIndexRange) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_phdr(buf, sizeof(buf), 0u, 0) != kAdrenoMdtInvalid) {
        fails += 1u;
    }

    /* The three acceptance rules are independent: each alone must refuse. */
    {
        struct adreno_mdt_phdr t;
        t.p_type = kAdrenoMdtPtLoad;
        t.p_paddr = 0x1000u;
        t.p_filesz = 8u;
        t.p_memsz = 16u;
        t.p_flags = 0u;
        t.p_offset = 0u;
        t.p_vaddr = 0u;
        t.p_align = 0u;
        checks += 1u;
        if (adreno_mdt_phdr_valid(&t) != 1) {
            fails += 1u;
        }
        t.p_type = 0u;
        checks += 1u;
        if (adreno_mdt_phdr_valid(&t) != 0) {
            fails += 1u; /* rule 1: not PT_LOAD */
        }
        t.p_type = kAdrenoMdtPtLoad;
        t.p_flags = kAdrenoMdtTypeHash;
        checks += 1u;
        if (adreno_mdt_phdr_valid(&t) != 0) {
            fails += 1u; /* rule 2: HASH */
        }
        t.p_flags = 0u;
        t.p_memsz = 0u;
        checks += 1u;
        if (adreno_mdt_phdr_valid(&t) != 0) {
            fails += 1u; /* rule 3: zero memsz */
        }
        t.p_memsz = 16u;
        checks += 1u;
        if (adreno_mdt_phdr_valid(0) != 0) {
            fails += 1u;
        }
    }

    /* Region size: only the ONE valid segment counts, ALIGNed up. */
    checks += 1u;
    if (adreno_mdt_get_size(buf, sizeof(buf), &region) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (region != 0x1000u) {
        fails += 1u; /* ALIGN(0x1000+0x1000) - 0x1000 = 0x1000 */
    }
    checks += 1u;
    if (region == 0x8800u) {
        fails += 1u; /* 0x8800 would be the p_filesz sum, which it must not be */
    }

    /* p_filesz sum is a DIFFERENT number -- the conflation trap. */
    checks += 1u;
    if (adreno_mdt_filesz_sum(buf, sizeof(buf), &sum) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (sum != 64u + 84u + 8u) {
        fails += 1u;
    }
    checks += 1u;
    if (sum == region) {
        fails += 1u; /* if these ever coincide the test stopped discriminating */
    }

    /* Metadata: the split branch, since 24 + 40 == 64 == sizeof(buf). */
    checks += 1u;
    if (adreno_mdt_metadata(buf, sizeof(buf), &meta, &split, &hash_seg) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (meta != 148u) {
        fails += 1u;
    }
    checks += 1u;
    if (split != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (hash_seg != 1u) {
        fails += 1u;
    }
    /* phdrs[0] being PT_LOAD makes the metadata rule refuse. */
    {
        uint8_t bad2[52 + 3 * 32];
        for (i = 0u; i < (uint32_t)sizeof(bad2); ++i) {
            bad2[i] = buf[i];
        }
        bad2[52 + 0] = 1u; /* phdr[0].p_type = PT_LOAD */
        checks += 1u;
        if (adreno_mdt_metadata(bad2, sizeof(bad2), &meta, &split, &hash_seg) !=
            kAdrenoMdtNoMetadata) {
            fails += 1u;
        }
    }

    /* Segment naming: the last THREE characters are replaced by bNN. */
    checks += 1u;
    if (adreno_mdt_segment_name("a630_zap.mdt", 12u, 1u, name, sizeof(name)) !=
        kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (name[0] != 'a' || name[9] != 'b' || name[10] != '0' ||
        name[11] != '1' || name[12] != '\0') {
        fails += 1u; /* a630_zap.b01 */
    }
    checks += 1u;
    if (adreno_mdt_segment_name("a630_zap.mdt", 12u, 2u, name, sizeof(name)) !=
        kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (name[10] != '0' || name[11] != '2') {
        fails += 1u; /* a630_zap.b02 */
    }
    checks += 1u;
    if (adreno_mdt_segment_name("a630_zap.mdt", 12u, 0u, name, sizeof(name)) !=
        kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (name[10] != '0' || name[11] != '0') {
        fails += 1u; /* a630_zap.b00 */
    }
    checks += 1u;
    if (adreno_mdt_segment_name("abc", 3u, 1u, name, sizeof(name)) !=
        kAdrenoMdtNameTooShort) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_segment_name("a630_zap.mdt", 12u, 1u, name, 4u) !=
        kAdrenoMdtInvalid) {
        fails += 1u;
    }

    /* Readiness is derived and is 0. */
    checks += 1u;
    if (adreno_mdt_loader_available() != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_ready() != 0) {
        fails += 1u;
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return fails == 0u ? 0 : 1;
}

/* ==========================================================================
 * ROUND 106: the LOAD PLAN (P1:293-392). Pure arithmetic; loads nothing.
 * ========================================================================== */

AdrenoMdtStatus adreno_mdt_bins_are_split(const uint8_t *data, size_t size,
                                          int *is_split)
{
    uint32_t phnum = 0u, phoff = 0u, phentsize = 0u;
    uint32_t i = 0u;
    struct adreno_mdt_phdr ph;
    AdrenoMdtStatus st;

    if (is_split == 0) {
        return kAdrenoMdtInvalid;
    }
    st = adreno_mdt_header(data, size, &phoff, &phentsize, &phnum);
    if (st != kAdrenoMdtOk) {
        return st;
    }
    *is_split = 0;
    for (i = 0u; i < phnum; ++i) {
        st = adreno_mdt_phdr(data, size, i, &ph);
        if (st != kAdrenoMdtOk) {
            return st;
        }
        /* P1:281 phdrs with a zero p_filesz are IGNORED -- they are padding at
         * the end of the file and must not decide the split question. */
        if (ph.p_filesz == 0u) {
            continue;
        }
        /* P1:284-287 seg_start/seg_end; either one past fw->size means the
         * payload lives in a separate .bNN file. Overflow-safe form: compare
         * against the remaining bytes rather than computing seg_end. */
        if ((size_t)ph.p_offset > size) {
            *is_split = 1;
            return kAdrenoMdtOk;
        }
        if ((size_t)ph.p_filesz > size - (size_t)ph.p_offset) {
            *is_split = 1;
            return kAdrenoMdtOk;
        }
    }
    return kAdrenoMdtOk;
}

AdrenoMdtStatus adreno_mdt_relocatable(const uint8_t *data, size_t size,
                                       int *is_relocatable)
{
    uint32_t phnum = 0u, phoff = 0u, phentsize = 0u;
    uint32_t i = 0u;
    struct adreno_mdt_phdr ph;
    AdrenoMdtStatus st;

    if (is_relocatable == 0) {
        return kAdrenoMdtInvalid;
    }
    st = adreno_mdt_header(data, size, &phoff, &phentsize, &phnum);
    if (st != kAdrenoMdtOk) {
        return st;
    }
    *is_relocatable = 0;
    for (i = 0u; i < phnum; ++i) {
        st = adreno_mdt_phdr(data, size, i, &ph);
        if (st != kAdrenoMdtOk) {
            return st;
        }
        /* P1:320-324 only VALID phdrs are consulted, and ONE is enough. */
        if (!adreno_mdt_phdr_valid(&ph)) {
            continue;
        }
        if ((ph.p_flags & kAdrenoMdtRelocatable) != 0u) {
            *is_relocatable = 1;
            return kAdrenoMdtOk;
        }
    }
    return kAdrenoMdtOk;
}

AdrenoMdtStatus adreno_mdt_min_paddr(const uint8_t *data, size_t size,
                                     uint32_t *min_paddr)
{
    uint32_t phnum = 0u, phoff = 0u, phentsize = 0u;
    uint32_t i = 0u;
    uint32_t found = 0u;
    uint32_t lowest = 0u;
    struct adreno_mdt_phdr ph;
    AdrenoMdtStatus st;

    if (min_paddr == 0) {
        return kAdrenoMdtInvalid;
    }
    st = adreno_mdt_header(data, size, &phoff, &phentsize, &phnum);
    if (st != kAdrenoMdtOk) {
        return st;
    }
    for (i = 0u; i < phnum; ++i) {
        st = adreno_mdt_phdr(data, size, i, &ph);
        if (st != kAdrenoMdtOk) {
            return st;
        }
        if (!adreno_mdt_phdr_valid(&ph)) {
            continue;
        }
        /* P1:326-327 `if (phdr->p_paddr < min_addr) min_addr = phdr->p_paddr`. */
        if (found == 0u || ph.p_paddr < lowest) {
            lowest = ph.p_paddr;
        }
        found += 1u;
    }
    if (found == 0u) {
        return kAdrenoMdtNoValidSegment;
    }
    *min_paddr = lowest;
    return kAdrenoMdtOk;
}

AdrenoMdtStatus adreno_mdt_plan(const uint8_t *data, size_t size,
                                uint32_t mem_phys, uint32_t mem_size,
                                struct adreno_mdt_plan *out,
                                struct adreno_mdt_segment *segments,
                                uint32_t capacity)
{
    uint32_t phnum = 0u, phoff = 0u, phentsize = 0u;
    uint32_t i = 0u;
    uint32_t min_addr = 0u;
    uint32_t max_addr = 0u;
    uint32_t found = 0u;
    uint32_t n = 0u;
    uint32_t copy_total = 0u;
    uint32_t zero_total = 0u;
    int relocate = 0;
    int split = 0;
    uint32_t mem_reloc = 0u;
    struct adreno_mdt_phdr ph;
    AdrenoMdtStatus st;

    if (out == 0) {
        return kAdrenoMdtInvalid;
    }
    /* A plan with nowhere to put its rows is still a legal query: the caller
     * gets the summary and the refusal codes. But capacity 0 with a non-NULL
     * pointer and a blob that needs rows is refused below, explicitly. */
    st = adreno_mdt_header(data, size, &phoff, &phentsize, &phnum);
    if (st != kAdrenoMdtOk) {
        return st;
    }

    /* ---- PASS 1 (P1:317-342): validity, relocatable, min/max ------------- */
    for (i = 0u; i < phnum; ++i) {
        st = adreno_mdt_phdr(data, size, i, &ph);
        if (st != kAdrenoMdtOk) {
            return st;
        }
        if (!adreno_mdt_phdr_valid(&ph)) {
            continue;
        }
        /* P1:323-324 one relocatable valid phdr sets the flag for the image. */
        if ((ph.p_flags & kAdrenoMdtRelocatable) != 0u) {
            relocate = 1;
        }
        /* P1:326-327 the lowest paddr, which becomes the reloc base. */
        if (found == 0u || ph.p_paddr < min_addr) {
            min_addr = ph.p_paddr;
        }
        {
            /* P1:232-233 the top edge, ALIGNed to SZ_4K, exactly as get_size. */
            uint32_t end = ph.p_paddr + ph.p_memsz;
            if (end < ph.p_paddr) {
                return kAdrenoMdtInvalid;
            }
            end = (end + kAdrenoMdtPageBytes - 1u) & ~(kAdrenoMdtPageBytes - 1u);
            if (end < ph.p_paddr) {
                return kAdrenoMdtInvalid;
            }
            if (found == 0u || end > max_addr) {
                max_addr = end;
            }
        }
        found += 1u;
    }
    if (found == 0u) {
        return kAdrenoMdtNoValidSegment;
    }
    if (max_addr < min_addr) {
        return kAdrenoMdtInvalid;
    }

    /* P1:330-342: a RELOCATABLE image is offset by its own lowest segment
     * address, so segment 0 always lands at region offset 0. A non-relocatable
     * one is offset by the region's physical base the caller allocated. */
    mem_reloc = (relocate != 0) ? min_addr : mem_phys;

    /* P1:313 split detection is a SEPARATE question from the metadata SPLIT at
     * P1:168, and it is computed before the loop because it changes how each
     * segment's payload is obtained. */
    st = adreno_mdt_bins_are_split(data, size, &split);
    if (st != kAdrenoMdtOk) {
        return st;
    }

    /* ---- PASS 2 (P1:344-386): per-segment arithmetic -------------------- */
    for (i = 0u; i < phnum; ++i) {
        uint32_t offset;
        uint32_t zero;

        st = adreno_mdt_phdr(data, size, i, &ph);
        if (st != kAdrenoMdtOk) {
            return st;
        }
        if (!adreno_mdt_phdr_valid(&ph)) {
            continue;
        }
        if (n >= kAdrenoMdtMaxSegments) {
            return kAdrenoMdtPlanTooManySegments;
        }

        /* P1:350 `offset = phdr->p_paddr - mem_reloc`. The subtraction is done
         * in unsigned arithmetic on the SAME rule the upstream signed compare
         * encodes: offset < 0 is exactly p_paddr < mem_reloc. */
        if (ph.p_paddr < mem_reloc) {
            return kAdrenoMdtPlanOutsideRegion; /* P1:351 offset < 0 */
        }
        offset = ph.p_paddr - mem_reloc;
        /* P1:351 `offset + ph.memsz > mem_size` -- overflow-safe. */
        if (ph.p_memsz > mem_size - (offset < mem_size ? offset : mem_size)) {
            return kAdrenoMdtPlanOutsideRegion;
        }
        if (offset > mem_size) {
            return kAdrenoMdtPlanOutsideRegion;
        }

        /* P1:357 `refusing to load segment with p_filesz > p_memsz`. */
        if (ph.p_filesz > ph.p_memsz) {
            return kAdrenoMdtPlanFileszGtMemsz;
        }

        /* P1:369/:376 a NON-SPLIT segment's payload must be inside this file,
         * or the copy the loader would perform reads out of bounds. Overhead-
         * safe: compare against the remainder instead of computing the sum. */
        if (split == 0 && ph.p_filesz != 0u) {
            if ((size_t)ph.p_offset > size) {
                return kAdrenoMdtPlanTruncatedSrc;
            }
            if ((size_t)ph.p_filesz > size - (size_t)ph.p_offset) {
                return kAdrenoMdtPlanTruncatedSrc;
            }
        }

        /* P1:384-385 the zero fill. p_filesz <= p_memsz was just enforced, so
         * this cannot underflow. */
        zero = ph.p_memsz - ph.p_filesz;

        segments[n].phdr_index = i;
        segments[n].dst_offset = offset;
        /* P1:376 src is meaningful only for a non-split load; for a split one
         * the payload comes from file .b<phdr_index> and src_offset is not a
         * byte offset into this buffer, so it is reported as 0. */
        segments[n].src_offset = (split != 0) ? 0u : ph.p_offset;
        segments[n].copy_bytes = ph.p_filesz;
        segments[n].zero_bytes = zero;
        segments[n].split_segment = i;

        if (copy_total + ph.p_filesz < copy_total) {
            return kAdrenoMdtInvalid;
        }
        if (zero_total + zero < zero_total) {
            return kAdrenoMdtInvalid;
        }
        copy_total += ph.p_filesz;
        zero_total += zero;
        n += 1u;
    }

    if (capacity < n) {
        return kAdrenoMdtPlanTooManySegments;
    }

    out->reloc_base = mem_reloc;      /* P1:388-389 *reloc_base = mem_reloc */
    out->region_bytes = max_addr - min_addr;
    out->segment_count = n;
    out->total_copy = copy_total;
    out->total_zero = zero_total;
    out->is_split = split;
    out->is_relocatable = relocate;
    return kAdrenoMdtOk;
}

int adreno_mdt_plan_verify(uint32_t *check_count)
{
    uint32_t fails = 0u;
    uint32_t checks = 0u;
    uint32_t i = 0u;
    uint32_t phoff = 0u, phentsize = 0u, phnum = 0u;
    uint32_t min_paddr = 0u, region = 0u;
    int split = -1;
    int reloc = -1;
    struct adreno_mdt_plan plan;
    struct adreno_mdt_segment seg[kAdrenoMdtMaxSegments];

    /* The same synthetic ELF32 the round-74 self-check builds, so the plan is
     * exercised with no file and no libc: one header slice, one HASH phdr, and
     * ONE valid relocatable PT_LOAD at paddr 0x1000 with filesz 8 / memsz 4096.
     * Those numbers make every rule individually checkable:
     *   reloc base = min paddr = 0x1000  -> the one segment sits at offset 0
     *   zero fill  = 4096 - 8 = 4088
     *   region     = ALIGN(0x1000+0x1000,4K) - 0x1000 = 0x1000                    */
    uint8_t buf[52 + 3 * 32];
    for (i = 0u; i < (uint32_t)sizeof(buf); ++i) {
        buf[i] = 0u;
    }
    buf[0] = 0x7fu; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 1u;
    buf[5] = 1u;
    buf[28] = 52u;
    buf[42] = 32u;
    buf[44] = 3u;
    buf[52 + 16] = 24u;                    /* ph[0].p_filesz  */
    buf[52 + 32 + 16] = 40u;               /* ph[1].p_filesz  */
    buf[52 + 32 + 27] = 0x02u;             /* ph[1].p_flags = 0x02000000 HASH */
    buf[52 + 64 + 0] = 1u;                 /* ph[2].p_type = PT_LOAD */
    /* Elf32_Phdr field offsets: p_offset 4..7, p_paddr 12..15, p_filesz 16..19,
     * p_memsz 20..23, p_flags 24..27. Writing the wrong byte of a field is the
     * easiest mistake here, so each store below names its field. */
    buf[52 + 64 + 5] = 0x10u;              /* ph[2].p_offset  = 0x1000 */
    buf[52 + 64 + 13] = 0x10u;             /* ph[2].p_paddr   = 0x1000 */
    buf[52 + 64 + 16] = 8u;                /* ph[2].p_filesz  = 8 */
    buf[52 + 64 + 21] = 0x10u;             /* ph[2].p_memsz   = 0x1000 */
    buf[52 + 64 + 27] = 0x08u;             /* relocatable BIT(27) */

    checks += 1u;
    if (adreno_mdt_header(buf, sizeof(buf), &phoff, &phentsize, &phnum) != kAdrenoMdtOk) {
        fails += 1u;
    }
    /* Confirm the fixture itself before asserting on it. */
    {
        struct adreno_mdt_phdr chk;
        checks += 1u;
        if (adreno_mdt_phdr(buf, sizeof(buf), 2u, &chk) != kAdrenoMdtOk) {
            fails += 1u;
        }
        checks += 1u;
        if (chk.p_offset != 0x1000u || chk.p_paddr != 0x1000u ||
            chk.p_filesz != 8u || chk.p_memsz != 0x1000u) {
            fails += 1u; /* the synthetic blob is what the rest assumes */
        }
        checks += 1u;
        if (adreno_mdt_phdr_valid(&chk) != 1) {
            fails += 1u;
        }
    }

    /* ---- Split detection is its OWN rule, not the metadata one --------- */
    checks += 1u;
    if (adreno_mdt_bins_are_split(buf, sizeof(buf), &split) != kAdrenoMdtOk) {
        fails += 1u;
    }
    /* ph[2] declares its payload at p_offset 4096 in a 148-byte container, so
     * the payload is NOT here: qcom_mdt_bins_are_split() must say SPLIT. */
    checks += 1u;
    if (split != 1) {
        fails += 1u;
    }
    /* THE SEPARABILITY POINT: on these SAME bytes the METADATA split test
     * (P1:168, ehdr + hash == file size) says NOT split, because 24 + 40 = 64
     * and the container is 148 bytes. Two rules, two answers, one blob -- which
     * is exactly why round 106 added bins_are_split() as a separate function
     * instead of reusing the metadata answer. */
    {
        uint32_t meta = 0u, hseg = 99u;
        int msplit = -1;
        checks += 1u;
        if (adreno_mdt_metadata(buf, sizeof(buf), &meta, &msplit, &hseg) !=
            kAdrenoMdtOk) {
            fails += 1u;
        }
        checks += 1u;
        if (msplit != 0) {
            fails += 1u;
        }
        checks += 1u;
        if (msplit == split) {
            fails += 1u; /* the two rules must be able to disagree */
        }
    }
    /* And the converse: a container whose ehdr+hash fills it exactly IS a
     * metadata-split image while its payload still fits (bins says NOT split).
     * 24 + 124 == 148, and ph[2]'s payload is moved inside the file. */
    {
        uint8_t exact[52 + 3 * 32];
        uint32_t meta = 0u, hseg = 99u;
        int msplit = -1;
        int bsplit = -1;
        for (i = 0u; i < (uint32_t)sizeof(exact); ++i) {
            exact[i] = buf[i];
        }
        exact[52 + 32 + 16] = 124u;   /* ph[1].p_filesz: 24 + 124 == 148 */
        exact[52 + 64 + 5] = 0u;      /* ph[2].p_offset = 0 -- inside the file */
        checks += 1u;
        if (adreno_mdt_metadata(exact, sizeof(exact), &meta, &msplit, &hseg) !=
            kAdrenoMdtOk) {
            fails += 1u;
        }
        checks += 1u;
        if (msplit != 1) {
            fails += 1u; /* ehdr + hash == the whole file */
        }
        checks += 1u;
        if (adreno_mdt_bins_are_split(exact, sizeof(exact), &bsplit) != kAdrenoMdtOk) {
            fails += 1u;
        }
        checks += 1u;
        if (bsplit != 0) {
            fails += 1u; /* the payload fits, so the BINS are not split */
        }
        checks += 1u;
        if (msplit == bsplit) {
            fails += 1u;
        }
    }

    /* ---- Relocatable and min paddr ------------------------------------ */
    checks += 1u;
    if (adreno_mdt_relocatable(buf, sizeof(buf), &reloc) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (reloc != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_min_paddr(buf, sizeof(buf), &min_paddr) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (min_paddr != 0x1000u) {
        fails += 1u; /* only ph[2] is valid, and its paddr is 0x1000 */
    }
    /* The min must match what get_size() subtracts. */
    checks += 1u;
    if (adreno_mdt_get_size(buf, sizeof(buf), &region) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (region != 0x1000u) {
        fails += 1u;
    }

    /* ---- The plan itself ---------------------------------------------- */
    checks += 1u;
    if (adreno_mdt_plan(buf, sizeof(buf), 0xDEAD0000u, 0x1000u, &plan, seg,
                        kAdrenoMdtMaxSegments) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (plan.segment_count != 1u) {
        fails += 1u; /* only ph[2] is valid */
    }
    /* P1:335 a RELOCATABLE image ignores mem_phys entirely. This is the check
     * that proves the reloc base is min_addr (0x1000), not the bogus
     * mem_phys (0xDEAD0000) the caller passed. */
    checks += 1u;
    if (plan.reloc_base != 0x1000u) {
        fails += 1u;
    }
    checks += 1u;
    if (plan.reloc_base == 0xDEAD0000u) {
        fails += 1u; /* it must NOT be mem_phys */
    }
    checks += 1u;
    if (plan.is_relocatable != 1) {
        fails += 1u;
    }
    checks += 1u;
    if (plan.is_split != 1) {
        fails += 1u; /* ph[2]'s payload lives beyond this container */
    }
    /* dst = p_paddr - mem_reloc = 0x1000 - 0x1000 = 0. */
    checks += 1u;
    if (seg[0].dst_offset != 0u) {
        fails += 1u;
    }
    checks += 1u;
    if (seg[0].phdr_index != 2u) {
        fails += 1u; /* the segment came from ph[2], not from index 0 */
    }
    checks += 1u;
    if (seg[0].copy_bytes != 8u) {
        fails += 1u;
    }
    /* P1:385 zero fill = p_memsz - p_filesz = 0x1000 - 8 = 4088. */
    checks += 1u;
    if (seg[0].zero_bytes != 4088u) {
        fails += 1u;
    }
    checks += 1u;
    if (plan.total_copy != 8u) {
        fails += 1u;
    }
    checks += 1u;
    if (plan.total_zero != 4088u) {
        fails += 1u;
    }
    /* copy + zero reconstructs the region EXACTLY. */
    checks += 1u;
    if (plan.total_copy + plan.total_zero != 0x1000u) {
        fails += 1u;
    }
    checks += 1u;
    if (plan.region_bytes != region) {
        fails += 1u; /* the plan's region size == get_size()'s answer */
    }
    /* A SPLIT segment has no in-buffer source offset. */
    checks += 1u;
    if (seg[0].src_offset != 0u) {
        fails += 1u;
    }
    /* The bNN number a loader would request for this segment. */
    checks += 1u;
    if (seg[0].split_segment != 2u) {
        fails += 1u;
    }
    {
        char name[32];
        checks += 1u;
        if (adreno_mdt_segment_name("a630_zap.mdt", 12u, seg[0].split_segment,
                                    name, sizeof(name)) != kAdrenoMdtOk) {
            fails += 1u;
        }
        checks += 1u;
        if (name[10] != '0' || name[11] != '2') {
            fails += 1u; /* a630_zap.b02 -- the real payload segment */
        }
    }

    /* ---- P1:351 the region-bounds refusal ----------------------------- */
    checks += 1u;
    if (adreno_mdt_plan(buf, sizeof(buf), 0x1000u, 0x1000u, &plan, seg,
                        kAdrenoMdtMaxSegments) != kAdrenoMdtOk) {
        fails += 1u; /* exactly fitting is LEGAL, not refused */
    }
    /* One byte short: offset 0 + memsz 0x1000 > 0xFFF -> refuse. */
    checks += 1u;
    if (adreno_mdt_plan(buf, sizeof(buf), 0x1000u, 0xFFFu, &plan, seg,
                        kAdrenoMdtMaxSegments) != kAdrenoMdtPlanOutsideRegion) {
        fails += 1u;
    }
    /* A reloc base ABOVE the segment paddr would make offset negative. */
    checks += 1u;
    if (adreno_mdt_plan(buf, sizeof(buf), 0x2000u, 0x1000u, &plan, seg,
                        kAdrenoMdtMaxSegments) != kAdrenoMdtOk) {
        fails += 1u; /* this blob IS relocatable, so mem_phys is IGNORED */
    }
    checks += 1u;
    if (plan.reloc_base != 0x1000u) {
        fails += 1u;
    }

    /* ---- P1:357 p_filesz > p_memsz is refused ------------------------- */
    {
        uint8_t bad[52 + 3 * 32];
        for (i = 0u; i < (uint32_t)sizeof(bad); ++i) {
            bad[i] = buf[i];
        }
        /* ph[2].p_filesz = 0x2000 while p_memsz stays 0x1000. */
        bad[52 + 64 + 16] = 0x00u;
        bad[52 + 64 + 17] = 0x20u;
        checks += 1u;
        if (adreno_mdt_plan(bad, sizeof(bad), 0x1000u, 0x2000u, &plan, seg,
                            kAdrenoMdtMaxSegments) != kAdrenoMdtPlanFileszGtMemsz) {
            fails += 1u;
        }
        /* The rule is INDEPENDENT of the region: even a huge region refuses. */
        checks += 1u;
        if (adreno_mdt_plan(bad, sizeof(bad), 0x1000u, 0xFFFFF000u, &plan, seg,
                            kAdrenoMdtMaxSegments) != kAdrenoMdtPlanFileszGtMemsz) {
            fails += 1u;
        }
    }

    /* ---- Refusals on short capacity and bad inputs -------------------- */
    checks += 1u;
    if (adreno_mdt_plan(buf, sizeof(buf), 0x1000u, 0x1000u, &plan, seg, 0u) !=
        kAdrenoMdtPlanTooManySegments) {
        fails += 1u; /* one segment needs one row */
    }
    checks += 1u;
    if (adreno_mdt_plan(buf, sizeof(buf), 0x1000u, 0x1000u, 0, seg,
                        kAdrenoMdtMaxSegments) != kAdrenoMdtInvalid) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_plan(0, 0u, 0x1000u, 0x1000u, &plan, seg,
                        kAdrenoMdtMaxSegments) != kAdrenoMdtInvalid) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_bins_are_split(buf, sizeof(buf), 0) != kAdrenoMdtInvalid) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_relocatable(buf, sizeof(buf), 0) != kAdrenoMdtInvalid) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_min_paddr(buf, sizeof(buf), 0) != kAdrenoMdtInvalid) {
        fails += 1u;
    }

    /* ---- NON-relocatable images use mem_phys as the base ------------- */
    /* Clearing BIT(27) flips the base rule: mem_reloc becomes mem_phys the
     * caller supplies, NOT the lowest p_paddr. Two fixtures prove it, and they
     * differ ONLY in p_paddr / mem_phys, never in the flag. */
    {
        uint8_t plain[52 + 3 * 32];
        for (i = 0u; i < (uint32_t)sizeof(plain); ++i) {
            plain[i] = buf[i];
        }
        plain[52 + 64 + 27] = 0u;      /* clear BIT(27): NOT relocatable */
        plain[52 + 64 + 13] = 0x20u;   /* ph[2].p_paddr = 0x2000 */
        checks += 1u;
        if (adreno_mdt_relocatable(plain, sizeof(plain), &reloc) != kAdrenoMdtOk) {
            fails += 1u;
        }
        checks += 1u;
        if (reloc != 0) {
            fails += 1u;
        }
        /* mem_phys 0x1000 < paddr 0x2000, so offset = 0x1000; the region must be
         * at least offset + memsz = 0x2000 bytes for that to be legal. */
        checks += 1u;
        if (adreno_mdt_plan(plain, sizeof(plain), 0x1000u, 0x2000u, &plan, seg,
                            kAdrenoMdtMaxSegments) != kAdrenoMdtOk) {
            fails += 1u;
        }
        checks += 1u;
        if (plan.is_relocatable != 0) {
            fails += 1u;
        }
        /* P1:341 non-relocatable -> mem_reloc = mem_phys, so this is 0x1000. */
        checks += 1u;
        if (plan.reloc_base != 0x1000u) {
            fails += 1u;
        }
        checks += 1u;
        if (plan.reloc_base == 0x2000u) {
            fails += 1u; /* it must NOT be min paddr for a plain image */
        }
        /* dst = p_paddr - mem_reloc = 0x2000 - 0x1000. */
        checks += 1u;
        if (seg[0].dst_offset != 0x1000u) {
            fails += 1u;
        }
        /* Same bytes, mem_phys ABOVE paddr -> offset would be negative: the
         * P1:351 refusal, and the exact trap a signed compare hides. */
        checks += 1u;
        if (adreno_mdt_plan(plain, sizeof(plain), 0x40000000u, 0x1000u, &plan, seg,
                            kAdrenoMdtMaxSegments) != kAdrenoMdtPlanOutsideRegion) {
            fails += 1u;
        }
    }
    /* The RELOCATABLE original, given the very same mem_phys, is NOT refused:
     * the base comes from the blob, so a silly mem_phys cannot hurt it. This
     * pair is the whole point of P1:330-342. */
    checks += 1u;
    if (adreno_mdt_plan(buf, sizeof(buf), 0x40000000u, 0x1000u, &plan, seg,
                        kAdrenoMdtMaxSegments) != kAdrenoMdtOk) {
        fails += 1u;
    }
    checks += 1u;
    if (plan.reloc_base != 0x1000u || seg[0].dst_offset != 0u) {
        fails += 1u;
    }

    /* ---- Readiness is unchanged: a plan is still not a loader --------- */
    checks += 1u;
    if (adreno_mdt_loader_available() != 0) {
        fails += 1u;
    }
    checks += 1u;
    if (adreno_mdt_ready() != 0) {
        fails += 1u;
    }

    if (check_count != 0) {
        *check_count = checks;
    }
    return fails == 0u ? 0 : 1;
}
