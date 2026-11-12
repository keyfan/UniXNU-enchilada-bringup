# Stage 5 GPU — ARM SMMU v2 stage-1 context WORD MODEL (ROUND 75)

**Status: HOST-VERIFIED. WORD MODEL ONLY.** This is NOT a driver and NOT an MMIO
sequence. It computes the exact register words and the exact order for one SMMU
context bank, and then states plainly which of the four needed INPUTS are still
absent on enchilada — **all four**.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area **STAYS
`"host-verified"`**. No device run, no flash, `m39510` NOT exercised, no MMIO, no
mapping, no page table. It does **not** unblock stage 5.

## Why this round

The admission table has read `kgsl-smmu-stage1` as `modeled = 0` since round 69
with the reason *"round 53's work is a fail-closed READ-safety gate, not a
stage-1 context"*. Re-reading round 53 confirms that is exactly right:
`AdrenoKGSLSmmuGate` exposes permission predicates and a verdict enum, and its own
header says it *"merely DECIDES whether a future read would be safe; it never
performs one"*. **Nothing in the tree computed a TTBR0/TCR/SCTLR word.**

This round adds that middle layer, so the row can stop reading "not modelled"
without pretending a context has ever been programmed.

## The words, derived (3 sha256 pins)

| pin | file | bytes | sha256 |
| --- | --- | --- | --- |
| P1 | `drivers/iommu/arm/arm-smmu/arm-smmu.h` | 16630 | `ed859987…546b53` |
| P2 | `drivers/iommu/arm/arm-smmu/arm-smmu.c` | 67693 | `d5a22b62…ecb4fb4a` |
| P3 | `drivers/iommu/io-pgtable-arm.c` | 38934 | `b78adbf8…0abab278` |

**Context-bank byte offsets** (P1:145-198): SCTLR `0x00`, ACTLR `0x04`, TCR2
`0x10`, TTBR0 `0x20`, TTBR1 `0x28`, TCR `0x30`, CONTEXTIDR `0x34`, S1_MAIR0
`0x38`, S1_MAIR1 `0x3c`, FSR `0x58`.

**SCTLR bits** (P1:146-154): `S1_ASIDPNE` BIT(12), `HUPCF` BIT(8), `CFCFG`
BIT(7), `CFIE` BIT(6), `CFRE` BIT(5), `E` BIT(4), `AFE` BIT(2), `TRE` BIT(1),
`M` BIT(0).

**The default a stage-1 context bank gets** (`arm-smmu.c:641-651`) is
`CFIE | CFRE | AFE | TRE | M`, plus `S1_ASIDPNE` when stage-1, plus `E` **only**
for a big-endian CPU. Recomputed from those bits, not copied:

```
LE: 0x0040|0x0020|0x0004|0x0002|0x0001|0x1000 = 0x00001067
BE: the same | 0x0010                         = 0x00001077
```

The verifier rebuilds both from the pinned bit positions and asserts the module's
constants equal them, so a transposed bit cannot survive.

**S2CR geometry** (P1:105-120): address `0xc00 + (n << 2)`; `PRIVCFG`
GENMASK(25,24), `TYPE` GENMASK(17,16) with TRANS 0 / BYPASS 1 / FAULT 2,
`EXIDVALID` BIT(10), `CBNDX` GENMASK(7,0).

**The plan, six writes, ordered:** MAIR0 → MAIR1 → TTBR0 → TCR → CONTEXTIDR →
SCTLR. The load-bearing rule is the last one: **SCTLR carries `M`, so it is the
write that turns translation on, and it must come after the table and the format
exist.** The verifier asserts `SCTLR` is later in the plan than both MAIR0 and
TTBR0. TTBR0 is the only 64-bit write.

## The four inputs that are still absent — named, not assumed

| # | input | why it is absent |
| --- | --- | --- |
| I1 | page table | TTBR0 must point at a real LPAE table. **Nothing in this port allocates or walks one.** |
| I2 | ASID | CONTEXTIDR needs an owned ASID; there is **no ASID allocator**. |
| I3 | stream→context-bank binding | S2CR must be programmed for the target SID first, and round 53 **measured** the target streams still in the firmware BYPASS handoff (`Busy=10`, `id0=4c017e4c`) — they do not fault yet, so nothing reaches translation. |
| I4 | powered CX | every one of these writes needs GPUCC CX up, and round 35 **measured** CX DOWN (`cx=0x00005ff4`, BIT(31) clear). |

`adreno_smmu_stage1_missing()` returns `0xf` and
`adreno_smmu_stage1_ready()` is **derived** from it, so it is 0 and flips only
when all four genuinely exist. I3 is modelled as a TYPE choice rather than a
comment: `adreno_smmu_s2cr_type_for_state(faulting)` returns **BYPASS** for a
non-faulting stream and TRANS only for a faulting one — round 53's measurement
expressed as register semantics.

## The admission table

`kgsl-smmu-stage1` moves **modeled 0 → 1** (total modelled **7 → 8**).
`satisfied` STAYS **0**, verdict STAYS **BLOCKED**.

## Deliverables

| file | role |
| --- | --- |
| `drivers/NativeAdreno/AdrenoSmmuStage1V1.h` | 3 sha256 pins; 10 CB offsets; 9 SCTLR bits; S2CR fields; 12 APIs |
| `drivers/NativeAdreno/AdrenoSmmuStage1V1.c` | freestanding C11; the SCTLR default; S2CR composition; the six-write plan with SCTLR last; the four-input refusal |
| `drivers/NativeAdreno/tests/adreno-smmu-stage1-v1-test.cpp` | 6 test groups |
| `scripts/verify-enchilada-smmu-stage1.py` | cross-verifier, **117 checks**, SCTLR default recomputed from the pinned bits |
| `scripts/build-enchilada-adreno.py` | 27th archive unit |
| `scripts/check-enchilada-adreno-host.py` | 27th host suite |

## Verifications

1. `PASS: enchilada A630 SMMU stage-1 context word model (6 test groups, 0
   failures)` inside the 27-suite runner.
2. `SMMU_STAGE1_CROSS: PASS (117 checks, 0 failures)` — Domain A re-derives the
   offsets, the nine bits, the SCTLR default (both endiannesses), the S2CR
   geometry and the S2CR writer composition; Domain B asserts SCTLR is last, all
   four inputs are recorded missing, and readiness is derived.
3. Strict freestanding compile: arm64 Mach-O **3496 B**, COFF 3113 B,
   `R75_STRICT: PASS`.

## A defect this round found in itself

The first draft asserted `s2cr_value(..., privcfg=1, ...)` should equal the bare
mask `0x03000000`. That is wrong: `PRIVCFG` is GENMASK(25,24), so a value of 1
occupies `0x01000000` and only 3 fills the mask. The self-check caught it; the
assertion now tests both 1 → `0x01000000` and 3 → `0x03000000`, which is a
stronger statement than either alone.

## Honest boundary

Computing the words is not programming them. There is no MMIO, no mapping, no page
table, no ASID, no IOKit, no caller, and **all four inputs are absent**. The four
hard blockers of `docs/porting/gpu.md` §1 remain open; the A630 client GPU windows
stay all-zero; the `gpu` area **STAYS `"host-verified"`**; no device run, no flash,
`m39510` NOT exercised. It does **not** unblock stage 5.

Chain: round 74 closed at `4c0e5e096acb39f5776ddacf99fa11923d453042`.
