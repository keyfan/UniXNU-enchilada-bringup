# Stage 5 GPU — CP-ring constants CLOSED by running the kernel's own generator (ROUND 72)

**Status: HOST-VERIFIED. All five round-70 unknowns are now DEFINED.**
`adreno_cp_ring_is_executable()` returns **1** for the first time.

This is still NOT a GPU driver: no ring was allocated, no IOVA mapped, no
`CP_RB_CNTL` written, no doorbell rung, no MMIO of any kind. `CONFIG_XNU_NATIVE_ADRENO`
remains `n`, the `gpu` area STAYS `"host-verified"`, and it does **not** unblock
stage 5. What changed is narrower but real: **the port can now compute every word
a CP ring must be programmed with.**

## Why this round

Round 71 closed three of the five constants and named the next step precisely:

> the ONLY remaining source step is the GENERATED `a6xx.xml.h` (CP_RB_CNTL bitfield
> accessors) — it is a BUILD ARTIFACT of the registers XML, so it can be regenerated
> with the kernel OWN generator rather than fetched from a mirror.

That is what this round did, and two of round 71's own assumptions turned out to be
wrong in interesting ways.

## Correction 1: CP_RB_CNTL's fields are NOT in a6xx.xml

Round 71 recorded that `a6xx.xml:2282` declares `CP_RB_CNTL` as a bare `reg32` with
no bitfields, and concluded the accessors must come from a generated header that
would have to be obtained somehow. The first half is correct (verified again here).
The second half was a wrong turn: **the fields live in `adreno_common.xml:116-123`**,
which `a6xx.xml` imports at line 7. Round 71 simply had not searched the imported
file.

| field | bits | value here |
| --- | --- | --- |
| `BUFSZ` | 0..5 | `ilog2(0x8000/8)` = **12** |
| `BLKSZ` | 8..13 | `ilog2(32/8)` = **2** |
| `BUF_SWAP` | 16..17 | 0 |
| `POLL_EN` | 20 | 0 |
| `NO_UPDATE` | 27 | **1** on this board |
| `RPTR_WR_EN` | 31 | 0 |

## Correction 2: generating the header proves it, and it agrees

Rather than hand-computing the register from the XML (which round 71 would have
had to do), the kernel's own `gen_header.py` was pinned and **actually run**:

```
gen_header.py --rnn a6xx --xml adreno/a6xx.xml        c-defines -> a6xx.xml.h
gen_header.py --rnn a6xx --xml adreno/adreno_common.xml c-defines -> adreno_common.xml.h
```

| artifact | bytes | sha256 |
| --- | --- | --- |
| `a6xx.xml.h` (generated) | 454595 | `adcd25caadf90d28ce480562047d0ff38f3d5b00bc6b9ba2dd67eae826512829` |
| `adreno_common.xml.h` (generated) | 17781 | `373a5c88c303d5a0a1ec8313cd24b6e531e4eba425b485356e587a0f702601a8` |

The generated accessors are exact:

```
#define REG_AXXX_CP_RB_CNTL             0x000001c1
#define AXXX_CP_RB_CNTL_BUFSZ__MASK     0x0000003f     __SHIFT 0
#define AXXX_CP_RB_CNTL_BLKSZ__MASK     0x00003f00     __SHIFT 8
#define AXXX_CP_RB_CNTL_NO_UPDATE       0x08000000     <- bit 27
#define AXXX_CP_RB_CNTL_RPTR_WR_EN      0x80000000     <- bit 31
#define REG_A6XX_CP_RB_CNTL             0x00000802
```

Feeding `BUFSZ(12) | BLKSZ(2)` through those masks yields **0x0000020C**, and ORing
the generated `NO_UPDATE` yields **0x0800020C** — byte-for-byte what the module
computed by hand. The generation therefore does two things at once: it *closes*
U3/U4 and it *independently checks* the hand derivation instead of restating it.

Two facts are now anchored that round 71 could only assert: the generated
`REG_A6XX_CP_RB_CNTL = 0x00000802` confirms the **word offset** the port has used
since round 70, and the generated `NO_UPDATE` confirms **bit 27**.

## What the module now computes

| constant | value | source |
| --- | --- | --- |
| `CP_ME_INIT` header | `0x70C80008` | round 71 (pm4.xml:248 + adreno_gpu.h:659-665) |
| `CP_ME_INIT` message | 9 dwords, header + `0x2F 0x3 0x20000000 0 0 0 0 0` | round 71 |
| ring size | `0x8000` (8192 dwords) | round 71 |
| `CP_RB_CNTL` default | **`0x0000020C`** | round 72 |
| `CP_RB_CNTL` A630 (no APRIV) | **`0x0800020C`** | round 72 |

`adreno_cp_ring_undefined_open()` is now **0** and `adreno_cp_ring_is_executable()`
is now **1** — derived from the open set rather than hard-coded, so it flips only
when the constants genuinely all exist.

## Verifications

1. Host suite #24, cp-ring now **11 groups** (added `testRbCntl`: field placement,
   pack/unpack round-trip, the two branches differing *exactly* in bit 27, and
   refusals) — `PASS: ... (11 test groups, 0 failures)`.
2. `CP_RING_CROSS: PASS (251 checks, 0 failures)` over **9** sha256-pinned sources,
   including a re-derivation of the register from the XML layout and the run of the
   generator. Domain A9 still checks the round-70 claim *as history*.
3. Strict freestanding compile: arm64 Mach-O **5472 B**, COFF 5125 B, `R72_STRICT: PASS`.

## A defect this round found in itself

The module's round-71 self-check still asserted `is_executable() == 0`. When round 72
made it 1, that stale assertion fired — the self-check caught its own superseded
expectation, which is exactly why the self-check is asserted rather than trusted.

## Honest boundary

Every register word is now COMPUTABLE. None has been WRITTEN. `is_executable() == 1`
means "the port knows what to put in each register", **not** "a ring works": the
power sequence, the PAS handshake and the SMMU context are all still absent, and the
four hard blockers of `docs/porting/gpu.md` §1 remain open. No device run, no flash,
`m39510` NOT exercised, `CONFIG_XNU_NATIVE_ADRENO` stays `n`, the A630 client GPU
windows stay all-zero, the `gpu` area STAYS `"host-verified"`, and this does **not**
unblock stage 5.

Chain: round 71 closed at `1e6a84988beb879abde372e1021b874e048ce030`.
