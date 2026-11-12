# Stage 5 GPU — A630 CP ring init / doorbell contract (ROUND 70)

**Status: HOST-VERIFIED. ORDER + PAYLOAD + REFUSAL ONLY.** This is NOT a GPU
driver. It encodes the exact CP-ring bring-up an A6XX_GEN1 backend must perform,
the exact `CP_ME_INIT` payload, and the doorbell ordering rule — and, with equal
care, the **five constants that no pinned source defines**, because those five
are what stand between this model and an executable ring.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area **STAYS
`"host-verified"`**. No device run, no flash, `m39510` NOT exercised, no MMIO.
It does **not** unblock stage 5.

## Why this round

Round 69's admission table named `cp-ring` as one of seven unmodelled
prerequisites, with the reason "only the eight word offsets are known". That was
accurate and it was the cheapest gap to close: `a6xx_gpu.c` pins the full init
order, the full `CP_ME_INIT` payload and both doorbell strategies.

**The table moves one row: modeled 5 → 6. satisfied STAYS 0, verdict STAYS
BLOCKED.** That is deliberate: a model that knows the order but cannot encode a
packet header cannot issue a single packet, so calling it "satisfied" would be
the dishonest way to move the table.

## What is derived (3 sha256 pins)

| pin | file | § |
| --- | --- | --- |
| P1 | `a6xx_gpu.c` 69087 B `c3347937…f943a2` | `:37-54` idle + rptr/wptr diagnostic; `:56-67` WHERE_AM_I shadow; `:69-90` doorbell; `:612-639` A630 ME_INIT; `:641-688` A7xx contrast; `:1118-1153` ring setup order |
| P2 | `a6xx.xml` 307513 B `7f432c5e…bcd602` | `:2281-2286`, `:2294` the CP register word offsets |
| P3 | `a6xx_gpu.h` 3251 B `8909f9d7…46033e` | `:59-69` the `CP_PROTECT` encoding |

**The ordered bring-up (6 steps, P1:1118-1153):** `CP_SQE_INSTR_BASE := sqe_iova`
→ `CP_RB_BASE := rb[0]->iova` → `CP_RB_CNTL` (bare default under extended APRIV;
`| AXXX_CP_RB_CNTL_NO_UPDATE` otherwise) → **conditionally** `CP_RB_RPTR_ADDR :=
shadowptr()` only when a shadow BO exists → `CP_SQE_CNTL := 1` → `CP_ME_INIT`.
The RPTR_ADDR step is the only conditional one, and the module models the
condition rather than always emitting six steps.

**The payload (P1:616-635), 8 dwords:** `0x0000002f`, `0x00000003` (multiple
hardware contexts), `0x20000000` (error detection), then five zeros (header dump
off, no workarounds, pad). The A7xx shape at `:641-688` — `CP_THREAD_CONTROL`
first and `CP_ME_INIT` with count **7** — is encoded as a refusal, not a variant.

**The doorbell (P1:82-89):** `wptr = get_wptr(ring)` latched under the lock,
`mb()` **then** a single `CP_RB_WPTR` store. The barrier-before-store order is
the content of the function; there is no path that writes first.

**The shadow strategy (P1:61-66, :1123-1131):** extended-APRIV targets keep the
hardware RPTR shadow; everyone else sets `NO_UPDATE` and drives the shadow via
`CP_WHERE_AM_I(2)`. enchilada is the second case.

## The five constants that are NOT derivable (refused, not guessed)

A repo-wide scan of every pinned `.c/.h/.xml` finds these **names** and never
their **definitions**:

| # | constant | why it is open |
| --- | --- | --- |
| U1 | `CP_ME_INIT` numeric opcode | the pinned XML register databases contain **zero** occurrences of `CP_ME_INIT`; it is an opcode, not a register |
| U2 | type-7 packet header encoding | `OUT_PKT7` is used at `:616` but defined in an unpinned header |
| U3 | `MSM_GPU_RB_CNTL_DEFAULT` value | name only |
| U4 | `AXXX_CP_RB_CNTL_NO_UPDATE` bit | name only |
| U5 | ring size (`MSM_GPU_RINGBUFFER_SZ`) | absent from **every** pinned file |

`adreno_cp_ring_is_executable()` returns **0**, and the reason is U1 **and** U2
together: without a header encoding there is no packet, and without an opcode
there is nothing to put in it. The module reports this set as a first-class
value (`adreno_cp_ring_undefined()` == `0x1f`) rather than presenting the order
as executable.

## Deliverables

| File | Role |
| --- | --- |
| `drivers/NativeAdreno/AdrenoCpRingV1.h` | 3 sha256 pins; 7 CP offsets; the 8-dword payload constants; the 6-step enum; the 5-bit undefined enum; 11 APIs |
| `drivers/NativeAdreno/AdrenoCpRingV1.c` | freestanding C11, no libc/MMIO/IOKit; conditional shadow step; payload builder + strict matcher; doorbell order; the refusal set |
| `drivers/NativeAdreno/tests/adreno-cp-ring-v1-test.cpp` | 7 test groups, `CHECK()` accumulates failures |
| `scripts/verify-enchilada-cp-ring.py` | method-2 cross-verifier, **139 checks** (Domain A = 3 pinned sources + an exhaustive define-scan; Domain B = deliverable) |
| `scripts/build-enchilada-adreno.py` | 24th archive unit |
| `scripts/check-enchilada-adreno-host.py` | 24th host suite |
| `drivers/NativeAdreno/AdrenoGen1BackendV1.c` | admission row moved: `cp-ring` modeled 0 → 1, `modeled_count` 5 → 6 |

## A defect this round found and fixed (in its own first draft)

The first draft's `adreno_cp_ring_plan()` refused any capacity below 6 **even
when no shadow BO was present and only 5 steps were emitted**, and the self-check
then re-used a 5-slot buffer for a 6-slot allowance. The self-check caught it
(2 failures); the fix makes the required capacity the number of steps actually
emitted, and the verification now asserts both directions: 5 slots must succeed
without a shadow BO, and 6 slots must still emit only 5. This is recorded because
the bug class — "capacity guard stricter than the payload" — is exactly the kind
that would ship as a silent off-by-one in a real ring allocator.

## Three independent verifications (all PASS)

1. **Host suite #24**: `PASS: enchilada A630 CP ring init/doorbell contract
   (7 test groups, 0 failures)`; the full runner reports 24 suites ending
   `… gen1-backend-v1/cp-ring-v1 host tests (without sanitizers)`.
2. **Cross-verifier (method 2, never compiles the module)**:
   `CP_RING_CROSS: PASS (139 checks, 0 failures)` — Domain A re-derives the seven
   offsets, the six-step order, the eight payload dwords, the barrier-before-store
   order, the WHERE_AM_I strategy, the A7xx contrast, the protect encoding, and
   proves the five undefined macros by exhaustive scan; Domain B asserts the
   deliverable mirrors all of it and that `is_executable()` is 0.
3. **Strict freestanding compile** `work/tmp/_r70_strict.py`: `clang -std=c11
   -ffreestanding -fno-builtin -O1 -Wall -Wextra -Werror -Wconversion
   -Wsign-conversion -Wshadow -pedantic -target aarch64-apple-darwin` → arm64
   Mach-O **4048 B**, magic `cf fa ed fe`; native COFF **3703 B**; C++17 host
   link/run prints the PASS line. `R70_STRICT: PASS`.

Plus the record checker: `enchilada: 15 structured areas checked`, and the
round-69 cross-verifier re-run green at 230 checks with the new row.

## Honest boundary

ORDER + PAYLOAD + REFUSAL. No BO, no IOVA, no mapping, no doorbell write, no
fence, no interrupt, no submit, no firmware load, no PAS authentication. NOT a
GPU driver. `CONFIG_XNU_NATIVE_ADRENO` stays `n`; the four hard blockers of
`docs/porting/gpu.md` §1 remain open; the A630 client GPU windows stay all-zero.
The admission table is **6 modeled / 0 satisfied / BLOCKED** — one row moved, the
verdict did not. The `gpu` area **STAYS `"host-verified"`**; no device run, no
flash, `m39510` NOT exercised.

Chain: round 69 closed at `abbfb13484768cce4c345cd781e68d79d4c23139`.
