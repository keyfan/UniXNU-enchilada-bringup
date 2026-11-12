# Stage 5 GPU — ARM SMMU v2 stage-1 context WORDS (ROUND 83)

**Status: HOST-VERIFIED. WORD DERIVATION ONLY.** This is NOT a driver and NOT an
MMIO sequence. It derives the register words a stage-1 context bank is a function
of, and it **corrects a real defect in round 75** that seven rounds of passing
tests did not catch.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area **STAYS `"host-verified"`**.
No device run, no flash, `m39510` NOT exercised, no MMIO, no mapping, no page
table. It does **not** unblock stage 5, and it does **not** move the admission
table: round 75 already records `kgsl-smmu-stage1` as modelled, and computing one
more word satisfies none of its four inputs.

## The defect this round measured

Round 75's `AdrenoSmmuStage1V1` answers *in what order* the context-bank
registers are written. That answer is correct as far as it goes, and its one
documented rule — SCTLR last — is preserved here unchanged. But every VALUE it
writes is an **argument**: `adreno_smmu_stage1_plan(cbndx, asid, ttbr0, tcr,
mair0, mair1, ...)` takes `tcr`, `mair0` and `mair1` from the caller and never
computes one. Its own test therefore passes `tcr = 0x1234`, a number that appears
in no pinned source.

The larger problem is that the sequence is **incomplete**. Upstream
`arm-smmu.c:566-653` (`arm_smmu_write_context_bank`) writes, in order: CBA2R,
CBAR, TCR2, TCR, TTBR0, TTBR1, S1_MAIR0, S1_MAIR1, SCTLR — **nine** registers.
Round 75 emits **six**, and the missing four are not optional:

| register | pinned citation | why it cannot be omitted |
| --- | --- | --- |
| CBAR (GR1 `0x0 + n<<2`) | P2:594-612, P1:123 | carries the context TYPE; without it the stage-1 page-table format is never declared |
| CBA2R (GR1 `0x800 + n<<2`) | P2:581-592, P1:141 | VA64 selects the 64-bit table format |
| TCR2 (CB `0x10`) | P2:619-620, P1:161 | carries PASIZE and SEP, and upstream writes it BEFORE TCR because TCR decides how ASID[15:8] is read |
| TTBR1 (CB `0x28`) | P2:630-632, P1:168 | stage-1 writes BOTH TTBRs, and BOTH carry the ASID |

Round 75 also writes **TTBR0 with no ASID**, although P2:540-548 places
`FIELD_PREP(ARM_SMMU_TTBRn_ASID, cfg->asid)` into bits 63..48. So the plan cannot
be repaired by supplying better arguments — it is missing registers.

### And one register it writes that upstream does not

Round 75 writes **CONTEXTIDR unconditionally**. Upstream writes it in exactly one
place — `arm-smmu.c:625` — inside the `cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_S`
branch. The AArch64 branch (P2:628-633) writes **no CONTEXTIDR at all**; on that
path the ASID reaches the bank through `TTBRn_ASID`. Verified mechanically:
there is exactly one `ARM_SMMU_CB_CONTEXTIDR` write site in the pinned file.

### Net effect, and why both numbers are pinned

6 → 9 writes. **Four** registers added, **one** dropped, so the net delta is
**+3**, deliberately smaller than the register delta. Both are exposed as numbers
(`adreno_smmu_ctx_round75_writes()` = 6, `adreno_smmu_ctx_plan_writes()` = 9) and
asserted by the test, so the gap stays measurable instead of becoming folklore.

### Why the omission survived seven rounds

Because the order round 75 emits is a **subsequence** of the correct one, and
every test asserted the subsequence. A correct rule plus a correct subsequence is
not a correct sequence. This is the same class as the round-81 regression, where
`verify-enchilada-gmu-bus-v1.py` asserted a rule the source had deliberately
removed.

## The words, derived (4 sha256 pins)

| pin | file | bytes | sha256 |
| --- | --- | --- | --- |
| P1 | `drivers/iommu/arm/arm-smmu/arm-smmu.h` | 16630 | `ed85998794695d1a824dc1b86f680c8d124546688fd2665e00e3cbcfa9546b53` |
| P2 | `drivers/iommu/arm/arm-smmu/arm-smmu.c` | 67693 | `d5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a` |
| P3 | `drivers/iommu/io-pgtable-arm.c` | 38934 | `b78adbf87352e574556638fae1206d62bfbc06787a85125f75b9d5da0abab278` |
| P4 | `drivers/iommu/io-pgtable-arm.h` | 812 | `34520ecc0fa7f29c7bc03c38c125e67bd66229ddc5fc44be0c59408e0389a379` |

**TCR** (P1:393-412 + P3:944-999), from `(ias, coherent_walk, ttbr1)`: TG0/TG1,
SH0, ORGN0, IRGN0, T0SZ = 64 − ias, and the **exclusive** EPD1/EPD0 pair. At
ias 40: coherent/TTBR0 `0x00803518`, non-coherent/TTBR0 `0x00802018`,
coherent/TTBR1 `0xB5180080`.

**TCR2** (P1:414-418 + P2:525-527): PASIZE from `oas`, SEP always `0x7`, plus
TCR2_AS only for AArch64. At oas 36: `0x00038011` with AS, `0x00038001` without.

**CBAR** (P2:594-612): TYPE=1 (`S1_TRANS_S2_BYPASS`, P2:733), S1_MEMATTR=WB,
S1_BPSHCFG=NSH → `0x0001F300`, plus an 8-bit VMID at 7..0 **only** when 16-bit
VMIDs are absent.

**CBA2R** (P2:581-592): VA64 for AArch64; VMID16 at bits 31..16 when the IDR
supports it.

**MAIR** (P3:123-130, :1002-1011, split by P2:560-561): `0xF404FF44`, so MAIR0 is
`0xF404FF44` and MAIR1 is `0` — every attribute lives in the low half on this
board, which the verifier asserts rather than assumes.

**TTBRn** (P1:169 + P2:540-548): `(asid << 48) | table`, for BOTH registers. For
asid `0x12` and a table at `0x877fe000`, `0x00120000877FE000`; the ASID lands in
the **high word** at bit 16, i.e. `0x00120000` — not `0x0012`.

### A trap this round recorded

`ARM_LPAE_TCR_TG1_4K` is **2**, not 3; `TG1_64K` is 3 (P4:10-11). Likewise
`arm_smmu_id_size_to_bits()` has a **default arm** that collapses value 5 *and
every other value* to 48 (P2:1727-1744), so encoding it as a table without that
default would invent a width for an undefined IDR field. Both are asserted.

## What is still missing

Three inputs, derived and reported as a mask; `adreno_smmu_ctx_ready()` derives
from it and is **0**:

| input | anchor |
| --- | --- |
| `ias` | P2:1904-1905 → P1:70 → P2:1879: `ID2.UBS` is a **probe-time register read** (round 77) |
| `asid` | P2:795 `cfg->asid = cfg->cbndx`; this port has no context-bank allocator (round 75 I2, unchanged) |
| page table | P2:1023 `TTBR = virt_to_phys(pgd)` of an allocated, populated table; this port allocates none (round 75 I1, unchanged) |

Round 75's I3 (stream binding) and I4 (CX powered) are **not** repeated here: they
gate whether a context may be **programmed**, not which word to write. That
distinction is stated in the module rather than blurred.

## Verification

- Host suite: **10 groups, 0 failures**, 82-check self-verifier whose count is
  pinned so a deleted assertion cannot pass silently.
- Cross-verifier `scripts/verify-enchilada-smmu-ctx-word.py`:
  **SMMU_CTX_WORD_CROSS PASS (264 checks)**. Domain A re-derives every word from
  the four pinned sources, recomputing TCR at 7 ias × 2 coherency × 2 geometries,
  the `id_size_to_bits` collapse, MAIR, TTBRn_ASID and the single CONTEXTIDR
  site. Domain B asserts the module mirrors it and that round 75 was left
  **byte-identical** (`need = 6u` still present) — this round CORRECTS, it does
  not rewrite history. Domain C checks the wiring and the unit bookkeeping.
- Full host runner: **28 suites, 0 failures** (26th→28th unit/suite wiring).
- All **58** runnable verifiers green; the 59th
  (`verify-enchilada-gmu-caller-v1-record.py`) requires a 40-hex revision argument
  by design and PASSes with `ead5834e` (28 checks) — the round-81 finding.
- Strict freestanding `arm64-apple-darwin` compile: exit 0, `mach-o arm64`.
- Sanitizers are **unavailable** in this environment (llvm-mingw ships no ASan
  runtime), so the suite runs `without sanitizers`, as every prior round records.

### Bookkeeping corrected (the round-78 drift class)

Adding the 28th unit made **six** existing verifiers assert a stale count. All six
were moved from 27 to 28, and two PASS enumerations were corrected from an exact
suffix match to a cumulative-prefix match:

`verify-enchilada-cp-ring.py`, `verify-enchilada-gen1-backend.py`,
`verify-enchilada-mdt.py`, `verify-enchilada-smmu-stage1.py`,
`verify-enchilada-va-map.py`, `verify-enchilada-zap-auth.py`.

## Honest boundary

Host-verified word derivation. No device run, no flash, no MMIO, no page table,
no ASID, no mapping, no caller. `CONFIG_XNU_NATIVE_ADRENO` stays `n`; the `gpu`
area stays `"host-verified"`. The first honest device action is unchanged from
round 82: CX up first, then the SMMU probe and stage-1 programming, then
zap-auth, and only then the round-28 gated GPUCC status read.
