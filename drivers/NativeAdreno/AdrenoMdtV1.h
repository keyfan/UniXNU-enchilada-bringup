// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoMdtV1 -- enchilada stage 5 (GPU) HOST-VERIFIED XNU-side replacement for
// the Qualcomm MDT loader rules that the ZAP handshake depends on
// (round 74, 2026-11-12).
//
// This is NOT a firmware loader. It PARSES the MDT/ELF32 container and answers
// the three questions the mainline ZAP path asks of it, so that steps S6
// (qcom_mdt_get_size) and S7 (qcom_mdt_load) of the round-73 PAS chain can be
// built on real logic instead of on "a parser does not exist yet". It performs
// no I/O, allocates nothing, calls no SCM, and touches no MMIO.
//
// WHY THIS ROUND (round-73 next_action steps (c) and the S6/S7 gap)
// ----------------------------------------------------------------
// Round 73 encoded the eight-step PAS chain and recorded S6/S7 as absent with
// the reason "qcom_mdt_get_size / qcom_mdt_load are Linux helpers and no
// XNU-side MDT parser has been written". That is the most concrete of the
// remaining gaps because the INPUT FILE IS ALREADY ON HAND: round 38 pulled
// a630_zap.mdt (.b00/.b01/.b02) read-only from this phone, and the rules are
// four short functions of pure struct arithmetic. So this round writes the
// parser and validates it against the real blob, not against a mock.
//
// PROVENANCE (3 sha256 pins re-checked by scripts/verify-enchilada-mdt.py)
// -----------------------------------------------------------------------
//  (P1) drivers/soc/qcom/mdt_loader.c  12182 B
//       ea79424add96b68c81ab9dfbde64ffac3ea5089a17259ba980c896f5d5e441ed
//       :21-33  mdt_phdr_valid(): p_type == PT_LOAD, (p_flags & TYPE_MASK) !=
//               TYPE_HASH, p_memsz != 0 -- all three must hold.
//       :76-103 qcom_mdt_get_size(): min p_paddr .. ALIGN(max p_paddr + p_memsz,
//               SZ_4K), over only the VALID phdrs.
//       :35-68  mdt_load_split_segment(): sprintf(seg_name + strlen - 3,
//               "b%02d", segment) -- the .mdt suffix is REPLACED by bNN.
//       :126-176 qcom_mdt_read_metadata(): e_phnum >= 2, phdrs[0] must NOT be
//               PT_LOAD, the hash segment is the first phdr whose TYPE bits are
//               HASH, ehdr_size = phdrs[0].p_filesz, hash_size =
//               phdrs[hash].p_filesz; SPLIT when ehdr_size + hash_size == size.
//       :265-291 qcom_mdt_bins_are_split(): split when ANY phdr with non-zero
//               p_filesz has p_offset or p_offset+p_filesz BEYOND fw->size.
//       :293-392 __qcom_mdt_load(): :323-327 relocatable test and min p_paddr;
//               :330-342 mem_reloc = min_addr when relocate else mem_phys;
//               :344-386 the per-segment loop this round 106 plan mirrors --
//               :350 offset = p_paddr - mem_reloc; :351 refuse when offset < 0
//               or offset + p_memsz > mem_size; :357 refuse p_filesz > p_memsz;
//               :365 dst = mem_region + offset; :369/:376 non-split bounds and
//               copy; :379 split via mdt_load_split_segment; :384-385 zero fill
//               p_memsz - p_filesz.
//  (P2) include/linux/soc/qcom/mdt_loader.h  2179 B
//       12d1c1c8b4e420ec28af0cd8255c9d0d37d56df7bf04714a61cee99479b3a8be
//       :7-9 QCOM_MDT_TYPE_MASK (7 << 24); QCOM_MDT_TYPE_HASH (2 << 24);
//       QCOM_MDT_RELOCATABLE BIT(27).
//  (P3) scripts/data/adreno-a630-device-firmware.json -- the round-38 read-only
//       DEVICE inventory. It is the authority for the seven file sizes below.
//
// THE THING THIS ROUND ESTABLISHES (measured, not assumed)
// --------------------------------------------------------
// Parsing the REAL a630_zap.mdt with the rules above gives:
//   e_ident    7f 45 4c 46, class 1 (ELF32), data 1 (LE)
//   e_machine  0xa4 = EM_HEXAGON 164 at byte 18 (NOT aarch64 -- round 38 corrected this)
//   e_phoff 52, e_phentsize 32, e_phnum 3
//   ph[0] type=0     off=0     paddr=0x0000 filesz=148   memsz=0     INVALID (not PT_LOAD, memsz 0)
//   ph[1] type=0     off=4096  paddr=0x6000 filesz=6536  memsz=8192  INVALID (not PT_LOAD)
//   ph[2] type=1     off=12288 paddr=0x5000 filesz=1968  memsz=1968  VALID (relocatable, BIT(27))
//   qcom_mdt_get_size equivalent = 4096 bytes
//   sum of p_filesz = 8652
// THREE independent cross-checks fall out of that, and all three hold:
//   1. The three p_filesz values (148, 6536, 1968) are EXACTLY the sizes of
//      a630_zap.b00, .b01 and .b02 recorded from the device in round 38. The
//      metadata layout and the segment layout are the same partition of 8652
//      bytes, which is why the split-chain load works at all.
//   2. ehdr_size (148) + hash_size (6536) = 6684 = the WHOLE FILE, so this MDT
//      takes the SPLIT branch at :168 and its PAS metadata blob is the entire
//      6684-byte .mdt -- not a 148-byte header slice.
//   3. Only ONE of three phdrs is loadable, so qcom_mdt_get_size answers 4096
//      from a 6684-byte container: the region needed is smaller than the file.
//      A parser that summed p_filesz would have said 8652 and over-allocated;
//      one that used the file size would have said 6684 and been wrong too.
//
// HONEST BOUNDARY
// ---------------
// HOST-VERIFIED, PARSER ONLY. It reads a buffer the caller already holds; it
// does not load a segment from disk, does not allocate the region, does not
// memremap, does not call SCM, and does not authorise anything. Steps S6/S7 are
// now modelled as RULES, not as a working loader, so `adreno_mdt_ready()`
// returns 0 and the zap-auth chain's S6/S7 stay present=0 until a caller wires
// this to real storage and to the secure-world call. CONFIG_XNU_NATIVE_ADRENO
// remains n; the gpu area STAYS "host-verified"; no device run, no flash,
// m39510 NOT exercised. It does NOT unblock stage 5.
// ---------------------------------------------------------------------------

#ifndef ADRENO_MDT_V1_H
#define ADRENO_MDT_V1_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ELF32 constants this parser needs (P1/P2) ------------------------ */
#define kAdrenoMdtElf32EhdrBytes   52u  /* sizeof(Elf32_Ehdr)          */
#define kAdrenoMdtElf32PhdrBytes   32u  /* sizeof(Elf32_Phdr)          */
#define kAdrenoMdtElfClass32       1u   /* EI_CLASS                    */
#define kAdrenoMdtElfDataLsb       1u   /* EI_DATA                     */
#define kAdrenoMdtElfMachineHexagon 0xa4u /* EM_HEXAGON 164 (P3/round 38) */
#define kAdrenoMdtPtLoad           1u   /* PT_LOAD                     */
#define kAdrenoMdtTypeMask         0x07000000u /* (7 << 24), P2:7     */
#define kAdrenoMdtTypeHash         0x02000000u /* (2 << 24), P2:8     */
#define kAdrenoMdtRelocatable      0x08000000u /* BIT(27),   P2:9     */
#define kAdrenoMdtPageBytes        0x1000u     /* SZ_4K align         */

/* The four-byte ELF magic, as bytes. */
#define kAdrenoMdtElfMagic0 0x7fu
#define kAdrenoMdtElfMagic1 'E'
#define kAdrenoMdtElfMagic2 'L'
#define kAdrenoMdtElfMagic3 'F'

/* ---- One parsed program header ---------------------------------------- */
struct adreno_mdt_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

/* Status codes. Each refusal names a rule that failed, not just "bad". */
typedef enum {
    kAdrenoMdtOk = 0,
    kAdrenoMdtInvalid = 1,     /* NULL or undersized buffer            */
    kAdrenoMdtBadMagic = 2,    /* not \x7fELF                          */
    kAdrenoMdtNotElf32 = 3,    /* EI_CLASS != 1                        */
    kAdrenoMdtNotLittleEndian = 4,
    kAdrenoMdtBadPhdrSize = 5, /* e_phentsize != 32                    */
    kAdrenoMdtNoPrograms = 6,  /* e_phnum == 0                         */
    kAdrenoMdtTruncated = 7,   /* phdr table runs past the buffer      */
    kAdrenoMdtIndexRange = 8,  /* phdr index out of range              */
    kAdrenoMdtNoValidSegment = 9, /* no PT_LOAD segment -> size undefined */
    kAdrenoMdtNoMetadata = 10, /* P1:140-156 metadata preconditions    */
    kAdrenoMdtNameTooShort = 11 /* P1:43-44 strlen(fw_name) < 4        */
} AdrenoMdtStatus;

/* ---- API (freestanding; no libc, no I/O, no allocation) -------------- */

/* Validate the ELF32 header. On success the three output pointers are
 * filled with e_phoff, e_phentsize and e_phnum. */
AdrenoMdtStatus adreno_mdt_header(const uint8_t *data, size_t size,
                                  uint32_t *phoff, uint32_t *phentsize,
                                  uint32_t *phnum);
/* Parse one program header by index. */
AdrenoMdtStatus adreno_mdt_phdr(const uint8_t *data, size_t size,
                                uint32_t index, struct adreno_mdt_phdr *out);
/* P1:21-33 the three acceptance rules, in the source's order. */
int adreno_mdt_phdr_valid(const struct adreno_mdt_phdr *phdr);
/* 1 when the phdr is relocatable (BIT(27) in p_flags), else 0. */
int adreno_mdt_phdr_relocatable(const struct adreno_mdt_phdr *phdr);
/* 1 when the phdr carries the HASH type bits, else 0. */
int adreno_mdt_phdr_is_hash(const struct adreno_mdt_phdr *phdr);

/* P1:76-103 qcom_mdt_get_size equivalent: ALIGN(max(p_paddr+p_memsz), 4K) -
 * min(p_paddr), over VALID phdrs only. Refuses when none is valid. */
AdrenoMdtStatus adreno_mdt_get_size(const uint8_t *data, size_t size,
                                    uint32_t *region_bytes);

/* P1:35-68 mdt_load_split_segment naming: the last three characters of the
 * firmware name are REPLACED by "bNN" (so a630_zap.mdt -> a630_zap.b01).
 * out must hold strlen(name)+1 bytes; capacity is checked. */
AdrenoMdtStatus adreno_mdt_segment_name(const char *name, uint32_t name_len,
                                        uint32_t segment, char *out,
                                        uint32_t capacity);

/* P1:126-176 qcom_mdt_read_metadata equivalent. On success *meta_len is the
 * number of bytes that form the PAS metadata blob, beginning at data[0].
 * *is_split is 1 when the firmware is a split image (P1:168). */
AdrenoMdtStatus adreno_mdt_metadata(const uint8_t *data, size_t size,
                                    uint32_t *meta_len, int *is_split,
                                    uint32_t *hash_segment);

/* Sum of p_filesz over ALL phdrs -- deliberately NOT the region size; provided
 * so a caller that conflates them can be caught by a test. */
AdrenoMdtStatus adreno_mdt_filesz_sum(const uint8_t *data, size_t size,
                                      uint32_t *sum);

/* ==========================================================================
 * ROUND 106: the LOAD PLAN -- pure arithmetic, still loads nothing
 * ==========================================================================
 * Rounds 74-105 answered "how big is the region" (get_size) and "what is the
 * metadata blob" (metadata). A loader needs one more thing: WHERE each segment
 * goes inside that region and HOW MUCH of it is real data versus zero fill.
 * That is what this section computes, following P1:344-386 exactly.
 *
 * THE ARITHMETIC (P1:344-386, verbatim structure)
 *   :335/:341  mem_reloc = min_addr when relocatable, else mem_phys
 *   :350       offset    = phdr->p_paddr - mem_reloc
 *   :351       REFUSE when offset < 0 or offset + p_memsz > mem_size
 *   :357       REFUSE when p_filesz > p_memsz
 *   :365       dst       = mem_region + offset
 *   :376       copy p_filesz bytes from fw->data + p_offset (non-split)
 *   :379       else mdt_load_split_segment (split: .bNN files)
 *   :384-385   zero fill p_memsz - p_filesz at dst + p_filesz
 *
 * WHY THE RELOC BASE MATTERS: :335 offsets every segment by the LOWEST valid
 * p_paddr when the image is relocatable, so the region's segment 0 sits at
 * offset 0 regardless of the paddr the linker chose. On the real a630_zap.mdt
 * min_addr == 0x5000 and the single valid segment lands at offset 0.
 *
 * SPLIT DETECTION is P1:265-291 qcom_mdt_bins_are_split(): a blob is split when
 * ANY phdr with a non-zero p_filesz has p_offset or p_offset+p_filesz beyond
 * fw->size. Note that this is a DIFFERENT test from the metadata SPLIT test at
 * P1:168 -- the two answer different questions and can disagree, which is why
 * they are separate functions here and a test asserts they are separable.
 *
 * HONEST BOUNDARY: this still LOADS NOTHING. No storage access, no memcpy, no
 * memset, no region allocation, no memremap. It returns numbers a loader would
 * use. `adreno_mdt_ready()` stays 0 and is derived from
 * adreno_mdt_loader_available(), which stays 0 because nothing reads a .bNN.
 * -------------------------------------------------------------------------- */

/* One entry of the plan: everything a memcpy/memset pair would need. */
struct adreno_mdt_segment {
    uint32_t phdr_index;    /* which program header this came from          */
    uint32_t dst_offset;    /* offset INSIDE the region (P1:350)            */
    uint32_t src_offset;    /* p_offset -- valid only when !is_split (P1:376)*/
    uint32_t copy_bytes;    /* p_filesz (P1:376)                            */
    uint32_t zero_bytes;    /* p_memsz - p_filesz, the fill (P1:385)        */
    uint32_t split_segment; /* the bNN number to request when is_split      */
};

/* The plan for one blob. `segment_count` counts only the VALID loadable phdrs. */
struct adreno_mdt_plan {
    uint32_t reloc_base;    /* P1:335/:341 mem_reloc                        */
    uint32_t region_bytes;  /* P1:76-103, the same answer as get_size()     */
    uint32_t segment_count; /* valid PT_LOAD phdrs                          */
    uint32_t total_copy;    /* sum of copy_bytes                            */
    uint32_t total_zero;    /* sum of zero_bytes                            */
    int is_split;           /* P1:265-291 qcom_mdt_bins_are_split()         */
    int is_relocatable;     /* any valid phdr with QCOM_MDT_RELOCATABLE     */
};

/* More reasons a plan can be refused, so a caller never sees a bare failure. */
#define kAdrenoMdtMaxSegments          8u   /* plan capacity; real blob has 1 */
#define kAdrenoMdtPlanTooManySegments  12
#define kAdrenoMdtPlanOutsideRegion    13   /* P1:351 offset/size check       */
#define kAdrenoMdtPlanFileszGtMemsz    14   /* P1:357                         */
/* P1:369 DEFENSIVE, and PROVABLY UNREACHABLE by construction: this plan runs
 * adreno_mdt_bins_are_split() first, and that rule already sets is_split when
 * ANY phdr with non-zero p_filesz overruns the buffer. So a valid segment that
 * would overrun is only ever seen with is_split == 1, where the in-buffer copy
 * branch is not taken. The refusal is retained because the upstream check
 * exists at P1:369 and dropping it would make the mirror incomplete -- NOT
 * because it can fire. Do not write a test that claims to trigger it. */
#define kAdrenoMdtPlanTruncatedSrc     15

/* P1:265-291 qcom_mdt_bins_are_split(). 1 = split, 0 = not. */
AdrenoMdtStatus adreno_mdt_bins_are_split(const uint8_t *data, size_t size,
                                          int *is_split);
/* 1 when any VALID phdr carries QCOM_MDT_RELOCATABLE (P1:323-324). */
AdrenoMdtStatus adreno_mdt_relocatable(const uint8_t *data, size_t size,
                                       int *is_relocatable);
/* The lowest p_paddr over VALID phdrs (P1:326-327) -- the reloc base for a
 * relocatable image. This is the SAME minimum get_size() subtracts. */
AdrenoMdtStatus adreno_mdt_min_paddr(const uint8_t *data, size_t size,
                                     uint32_t *min_paddr);

/* Build the plan. `mem_phys`/`mem_size` describe the region a caller WOULD
 * allocate; nothing is allocated here. `out` receives the summary, and
 * `segments`/`capacity` receive the per-segment arithmetic in phdr order.
 * Returns kAdrenoMdtOk, or a named refusal. Refuses on EVERY rule at
 * P1:351/:357/:369 -- the same three that make the upstream loop break. */
AdrenoMdtStatus adreno_mdt_plan(const uint8_t *data, size_t size,
                                uint32_t mem_phys, uint32_t mem_size,
                                struct adreno_mdt_plan *out,
                                struct adreno_mdt_segment *segments,
                                uint32_t capacity);

/* ROUND 106 self-check for the load plan. Returns 0 when every invariant
 * holds, else 1; *check_count receives the number evaluated. Separate from
 * adreno_mdt_verify() so the round-74 checks keep their original meaning. */
int adreno_mdt_plan_verify(uint32_t *check_count);

/* 1 only when a caller could load segments from storage and pass the metadata
 * to the secure world. 0 today: this is a parser, not a loader. Derived from
 * adreno_mdt_loader_available() so it cannot silently drift. */
int adreno_mdt_ready(void);
/* 0: there is no storage loader wired to this parser yet. */
int adreno_mdt_loader_available(void);

/* Self-check. Returns 0 when every invariant holds, else 1; *check_count
 * receives the number evaluated. */
int adreno_mdt_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_MDT_V1_H */
