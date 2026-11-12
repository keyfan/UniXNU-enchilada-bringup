# Stage 5 GPU — an XNU-side MDT parser, validated against the phone's own a630_zap.mdt (ROUND 74)

**Status: HOST-VERIFIED. PARSER ONLY.** This is NOT a firmware loader and NOT a
GPU driver. It reimplements the four short Qualcomm MDT/ELF32 rules that the ZAP
handshake depends on — steps **S6/S7** of the round-73 PAS chain — and validates
them against the **real `a630_zap.mdt` pulled read-only from this phone in round
38**, not against a mock.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area **STAYS
`"host-verified"`**. No device run, no flash, `m39510` NOT exercised, no MMIO, no
SCM call, no allocation. It does **not** unblock stage 5.

## Why this round

Round 73 recorded S6/S7 as absent with the reason *"qcom_mdt_get_size /
qcom_mdt_load are Linux helpers and no XNU-side MDT parser has been written"*.

That was the most tractable remaining gap because the **input file is already on
hand**: round 38 pulled `a630_zap.mdt` (and `.b00/.b01/.b02`) read-only from the
phone, and the rules are four functions of pure struct arithmetic. So the work was
to write the parser and then check it against reality — which is where it got
interesting.

## The rules (2 sha256 pins)

| pin | file | bytes | sha256 |
| --- | --- | --- | --- |
| P1 | `drivers/soc/qcom/mdt_loader.c` | 12182 | `ea79424a…e441ed` |
| P2 | `include/linux/soc/qcom/mdt_loader.h` | 2179 | `12d1c1c8…b3a8be` |

Four rules, all encoded:

1. **`mdt_phdr_valid`** (`:21-33`) — a segment is loadable only if
   `p_type == PT_LOAD` **and** `(p_flags & TYPE_MASK) != TYPE_HASH` **and**
   `p_memsz != 0`. All three must hold; they are independent.
2. **`qcom_mdt_get_size`** (`:76-103`) — `ALIGN(max(p_paddr+p_memsz), 4K) - min(p_paddr)`
   over **valid phdrs only**, never over the file size and never over a sum.
3. **split naming** (`:35-68`) — the last three characters of the firmware name
   are **replaced** by `bNN`, so `a630_zap.mdt` → `a630_zap.b01`.
4. **metadata** (`:126-176`) — `e_phnum >= 2`, `phdrs[0]` must **not** be
   `PT_LOAD`, the hash segment is the first phdr whose TYPE bits are `HASH`, and
   the image is **split** iff `ehdr_size + hash_size == file size`.

Constants from P2: `TYPE_MASK = 7 << 24`, `TYPE_HASH = 2 << 24`,
`RELOCATABLE = BIT(27)`.

## What the real blob says (measured, not assumed)

Parsing the actual 6684-byte `a630_zap.mdt`:

```
e_ident    7f 45 4c 46, class 1 (ELF32), data 1 (LE)
e_machine  0xa4 = EM_HEXAGON 164, at byte 18   (NOT aarch64 — round 38's correction holds)
e_phoff 52, e_phentsize 32, e_phnum 3
ph[0] type=0  off=0      paddr=0x0000 filesz=148   memsz=0     INVALID
ph[1] type=0  off=4096   paddr=0x6000 filesz=6536  memsz=8192  INVALID
ph[2] type=1  off=12288  paddr=0x5000 filesz=1968  memsz=1968  VALID, relocatable
region size = 4096    p_filesz sum = 8652
```

Three cross-checks fall out, and all three hold:

1. **The three `p_filesz` values are exactly the device segment sizes.** 148, 6536
   and 1968 are the byte counts of `a630_zap.b00`, `.b01` and `.b02` recorded
   from the phone in round 38. The metadata layout and the segment layout are the
   same partition of 8652 bytes — which is *why* the split-chain load works at all.
   The verifier asserts this against `scripts/data/adreno-a630-device-firmware.json`
   rather than against a constant.
2. **This MDT takes the SPLIT branch.** `ehdr_size (148) + hash_size (6536) = 6684`
   = the whole file, so its PAS metadata blob is the entire `.mdt`, not a 148-byte
   header slice.
3. **Only one of three segments is loadable**, so `get_size` answers **4096** from
   a **6684**-byte container. A parser that summed `p_filesz` would say 8652 and
   over-allocate; one that used the file size would say 6684 and also be wrong.
   The module exposes `adreno_mdt_filesz_sum()` specifically so a test can show
   the two numbers differ — on real data, not synthetic.

## Deliverables

| file | role |
| --- | --- |
| `drivers/NativeAdreno/AdrenoMdtV1.h` | 2 sha256 pins; the ELF32/PT_LOAD/TYPE constants; 12 APIs |
| `drivers/NativeAdreno/AdrenoMdtV1.c` | freestanding C11; bounds-checked byte-wise little-endian readers (no unaligned 32-bit loads); the four rules; `ready()` derived and 0 |
| `drivers/NativeAdreno/tests/adreno-mdt-v1-test.cpp` | 6 test groups; group 6 parses the REAL blob and **SKIPs loudly** if absent |
| `scripts/verify-enchilada-mdt.py` | cross-verifier, **113 checks**, including an independent Python reparse of the real blob |
| `scripts/build-enchilada-adreno.py` | 26th archive unit |
| `scripts/check-enchilada-adreno-host.py` | 26th host suite; passes the real blob path to the test |

## Verifications

1. `PASS: enchilada A630 MDT parser contract (6 test groups, 0 failures)` with
   `REAL_BLOB bytes=6684` — group 6 ran against the phone's own file.
2. `MDT_CROSS: PASS (113 checks, 0 failures)` — Domain A re-derives the rules from
   both pins **and** reparses the blob in an independent Python implementation,
   then confronts the result with the round-38 device inventory; Domain B asserts
   the module mirrors the rules, bounds-checks every read, and stays a parser.
3. Strict freestanding compile: arm64 Mach-O **4880 B**, COFF 4703 B,
   `R74_STRICT: PASS` (run **with** the real blob as argv[1]).

## Defects this round found in its own first draft

Six, all caught by the assertions rather than shipped:

- A `/*` inside a block comment (caught by `-Werror`).
- An unused variable in `get_size` (`-Werror` again).
- The synthetic container's `p_flags` HASH byte written at index 26 instead of 27,
  producing `0x00020000` — not a HASH type at all.
- `e_machine` read at offset **24** in the test; the real offset is **18**
  (`Elf32_Ehdr` = `e_ident[16]`, `e_type@16`, `e_machine@18`). The parser itself
  never read it, so only the assertion was wrong — but that is exactly the kind of
  wrongness that would have propagated into a ZAP loader.
- The segment-suffix index assumed 8; `"a630_zap.mdt"` is 12 chars so the suffix
  starts at **9**.
- A test count inflated to 8 when the file had 6 — corrected downward rather than
  adding filler.

## Honest boundary

A parser is not a loader. It reads a buffer the caller already holds: it does not
read `.bNN` from storage, does not allocate or `memremap` the region, does not call
SCM, and authorises nothing. `adreno_mdt_loader_available()` is **0** and
`adreno_mdt_ready()` is derived from it, so it stays 0 until real wiring exists.
S6/S7 therefore remain `present = 0` in the zap-auth chain: their **rules are now
modelled**, their execution is not. The four hard blockers of
`docs/porting/gpu.md` §1 remain open; the A630 client GPU windows stay all-zero;
the `gpu` area **STAYS `"host-verified"`**; no device run, no flash, `m39510` NOT
exercised. It does **not** unblock stage 5.

Chain: round 73 closed at `085b4f1110221a34e67eb6641ee71ad0811d59e1`.
