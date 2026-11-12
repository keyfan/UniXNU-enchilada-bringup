# Round 63 — AdrenoGpuccWindowV1: the bounded window the executor writes through

**Area:** gpu · **Status:** host-verified · **Device evidence:** none this round

## Why this round exists

Round 62 built an executor that turns derived plans into MMIO. It takes an
`AdrenoPowerUpBackendV1Mmio` triple — a cookie and three function pointers — and
**nothing in the tree produced one.** The intended caller was going to have to
write three thunks by hand, and the arithmetic "byte offset → `volatile
uint32_t *`" would have lived in kernel code with no test and no bound.

That is the same shape of hole round 62 closed: a layer that exists but is never
connected, so every test passes while the thing itself is unexercised. This
round supplies the missing piece in the form sheng's A740 driver uses for it —
one bounded MMIO adapter per window, owning no policy.

## What this module is, and is not

It owns exactly one thing: **the bounds arithmetic for a single GPUCC window.**

- It does not map. The caller passes `base`; mapping is the caller's decision.
- It does not enable clocks, vote regulators, or touch the GDSC.
- It does not know about GPU power sequencing at all — it converts an offset
  into a checked pointer and nothing more.

`Bind()` and `SetDelay()` are deliberately separate calls. A caller that only
needs reads (a status probe) should not have to invent a delay function just to
satisfy a triple it will never use.

## The three bounds, and the one that is easy to miss

| # | rule | why it exists |
|---|---|---|
| 1 | `offset % 4 == 0` | every GPUCC register is 32-bit and word-aligned |
| 2 | `offset + 4 <= bytes` | stays inside the mapped window |
| 3 | `offset <= max_register` | **the regmap limit, which is smaller than the window** |

Bound 3 is the finding. The device tree node for `gpucc@5090000` claims
`reg = <0x0 0x9000>`, and the round-61 model carried that size. But
`gpucc-sdm845.c` registers its regmap with `max_register = 0x8008`, so the last
~4 KiB of the node is not addressable by the driver at all. A window that trusts
the DTB alone is 4 KiB too generous, and bound 2 alone would never catch it — the
offending offsets are inside the mapping.

So the usable size is derived, not read from the DTB:

```
usable = min(bytes, max_register + 4) = min(0x9000, 0x8008 + 4) = 0x800c
```

`0x800c` is *not* the DTB size, and it is not hardcoded: the C computes it, and
`Verify()` asserts both that `0x8008` is reachable and that `0x800c` is refused.

The constants are derived from round 61 rather than retyped, so the two modules
cannot drift apart:

```c
#define ADRENO_GPUCC_WINDOW_V1_MAX_REGISTER ADRENO_CLK_CTL_V1_GPUCC_MAX_REG
#define ADRENO_GPUCC_WINDOW_V1_DTB_BYTES    ADRENO_CLK_CTL_V1_GPUCC_SIZE
```

## The offset convention, which had to be stated explicitly

`AdrenoGmuBusV1Resolve()` (round 44) takes a **word** offset and shifts it:

```c
uint64_t byte_off = (uint64_t)word_offset << 2;   /* AdrenoGmuBusV1.c:27 */
```

The round-61 clock plans take **byte** offsets — `0x1120` means `GPUCC + 0x1120`,
not `GPUCC + 0x4480`. Two adapters in the same directory with opposite
conventions is exactly how a silent 4× offset error gets shipped, so this module
takes byte offsets, never shifts, and says so in the header.

## The integration test, and why it does not use `Mmio()`

`testExecutorThroughWindow()` builds the real round-62 executor on top of this
window, runs the real five-op plan, and then inspects the fake register file for
the writes the derivation predicts.

It deliberately does **not** obtain its triple from `AdrenoGpuccWindowV1Mmio()`.
A plain memory window cannot emulate `0x1120`: `CMD_UPDATE` is write-1-to-trigger
and self-clears, and `CMD_ROOT_OFF` is a read-only status bit. Round 62 spent
real time on exactly this — a simulator that treats `0x1120` as ordinary storage
certifies an executor that would poll forever on the device.

The honest options were:

1. add a read-side hook to the production window so tests can intercept reads;
2. keep the hardware emulation in the test and let it serve its own reads.

Option 1 puts test emulation logic into kernel code — a worse trade than the
test doing more work. So the test owns the emulation, but **every access still
passes through the real `AdrenoGpuccWindowV1Resolve()` first**, which is what is
actually under test here. `Mmio()` is exercised separately in `testMmioTriple()`.

What the integration test proves that the unit test cannot: if the adapter and
the executor ever disagree about bytes vs words, the writes land at 4× their
intended offsets and the test fails. It asserts the parked CFG word `0x0602`
arrives at `0x1124` (CMD + 4), not at `0x1120` and not at `0x4490`.

## One wrong assertion I wrote, and what it taught

My first integration assertion was:

```cpp
checkEq(pre.gx_genpd_on, 0u, "integration: GX not powered by CPU path");
```

It failed with `got 0x1`. The module was right and I was wrong. The GX GDSC step
is *guarded* — `AdrenoPowerUpV1StepGuarded(GX) == 1` — because the GMU firmware
powers GX, not CPU writes; the executor performs **zero** GX writes. But
`gx_genpd_on` records that the GX *step completed*, which is a different fact
from "the CPU wrote GX registers". Conflating them would have meant asserting
that the GX step never ran, which is not what the chain does.

The corrected assertion is `== 1` with the distinction written into the test.

## Verification

- Host suite: **18 suites, 0 failures**. New suite: **91 checks**,
  `PASS: enchilada GPUCC window host tests (91 checks)`; the module's own
  `Verify()` contributes 44 of them (`rc = 0`).
- Independent cross-check: `GPUCC_WINDOW_V1_CROSS: PASS (30 checks, 0
  failures)`. It re-derives `min(0x9000, 0x8008 + 4)` in Python from the pinned
  board facts, asserts the two source constants agree with each other, asserts
  the production code contains no `0x800c` literal, and confirms every real
  offset the executor touches (`0x1010`, `0x106c`, `0x1098`, `0x109c`, `0x1120`,
  `0x1124`, `0x1540` — highest `0x1540`) fits inside `0x800c`.
- Strict freestanding arm64 Mach-O:
  `clang -target arm64-apple-macos11 -std=c11 -ffreestanding -fno-builtin -O2
  -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wpedantic
  -Wundef -Wcast-qual -Wstrict-prototypes -mgeneral-regs-only` → `exit 0`,
  1368-byte object, magic `0xfeedfacf`, cputype `0x100000c`, subtype 0, filetype
  1, 6 exported symbols.
- `check-enchilada-probe.py`: 18/18 PASS (the round-29 shim contract still holds).
- `check-enchilada-archives-linked.py`: PASS, 7 force-loaded archives.

The cross-check needed three corrections to *itself* before it passed — all
three were wrong assumptions in the checker, not defects in the module:

1. it forbade the literal `0x800c` anywhere in the C, but `Verify()` must assert
   the derived value; the scan now stops before `Verify()`;
2. it looked for `word_offset) << 2` on one line, but that expression in
   `AdrenoGmuBusV1.c:27` wraps across a line break — now a regex;
3. it failed on the word `regulator` in the file header *comment* ("no mapping,
   clock, regulator or power action here") — now it matches call syntax
   (`regulator_[a-z_]+\s*\(`) rather than the bare word.

## Archive build: same pre-existing gap, plus one real fix

`scripts/build-enchilada-adreno.py` still cannot run here: it needs recorded XNU
compile flags from `build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/...cpo.json`, and
that tree does not exist in this workspace (`MISSING: build\xnu\ios-obj`).

While adding this round's module to the object list, I found that
**`AdrenoPowerUpBackendV1` was missing from the `source_sha256` set** even though
round 62 recorded it as added. Both it and `AdrenoGpuccWindowV1` are now in the
object list and in `source_sha256`. That is a correction to a previous round's
record, not a new claim: the archive was not rebuilt and is not claimed built.

## Honest boundary

HOST-VERIFIED ADAPTER. It bounds one window's offsets. It does not map memory,
holds no clock or power policy, and performs no MMIO of its own beyond the
callbacks it hands out.

It does not make the round-22/28 prohibition any safer. A correctly bounded
window pointed at a collapsed domain will still hang the bus — bounds checking
prevents *wrong offsets*, not *wrong timing*. The gate that must run first
remains `AdrenoGmuCallerV1Precheck`.

The gpu area stays `host-verified`. Last device evidence remains round 52. This
round had no device evidence and did not look for any: `adb` shows the phone in
TWRP recovery with no `/dev/mem`, no `/dev/kmem`, an empty `/sys/kernel/debug`,
no regmap debugfs, no `/proc/config.gz`, and `kptr_restrict=2`. Under those
conditions there is no safe way to read a GPUCC register from userspace, so the
honest thing is to say so rather than to attempt a read that could hang the CPU.
