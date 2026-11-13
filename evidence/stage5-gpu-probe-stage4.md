# Round 94 — stage 4: the kernel-side SMMU ID2 read (the `ias` the port never had)

**Status: KERNEL PROBE, HOST-VERIFIED + FAIL-CLOSED. NOT YET RUN ON HARDWARE.**

This adds stage 4 to `package/EnchiladaAdrenoProbe.cpp`: the **first kernel call
path that can read the SMMU `ID2` register**, which is the only place the GPU VA
size (`ias`) comes from. It does not power anything, does not program a context,
and on the currently-measured device state it is expected to **refuse**.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area **STAYS `"host-verified"`**.
It does **not** unblock stage 5.

## Why this shape, and not "just read the register"

Three earlier rounds each closed one door, and together they force the read into
the kernel:

| round | measurement | consequence |
| --- | --- | --- |
| 77 | `ias = smmu->va_size` ← `arm_smmu_id_size_to_bits(ID2.UBS)` ← **a probe-time read** (P2:1879, :1904-1905) | no document or DTS can supply it |
| 85 | `CONFIG_DEVMEM is not set`; a `mknod`-ed `/dev/mem` fails `ENXIO` | no userspace MMIO route exists |
| 85 | the iommu debugfs tree is `io-pgtable` + `tests` only, `find -maxdepth 2` returns one file | no debugfs route either |

So the word can only be obtained by code running **in the kernel**. That is what
this stage is.

## What stage 4 does, in order

1. **Reads the GPUCC CX status word** (already done by stage 1/2 in the same poll,
   at `0x05091540`, BIT(31)).
2. **Asks the round-86 contract's gate** with a `struct adreno_iommu_gate_inputs`
   whose `cx_status_word` is *that measured word* — not a constant, not an
   assumption. `adreno_iommu_assess()` returns one of eight named verdicts.
3. **If not permitted**, it logs a row that **names the verdict** and stops. No
   mapping, no read. This is deliberate: a refusal and "a read that returned
   `0x00000000`" mean completely different things, and conflating them is how a
   port invents a fact.
4. **If permitted**, it maps the kgsl SMMU window (`ADRENO_BOARD_KGSL_SMMU_BASE`,
   `0x05040000`) **for the first time**, reads `GR0_ID2` at byte `0x28` exactly
   once, issues a `dsb sy`, and hands the word to the contract:
   - `adreno_iommu_id2_decode()` — field positions, version gate, collapsing default
   - `adreno_iommu_va_size(id2, version, &ok)` — `ias`, refused if implausible
   - `adreno_iommu_stage1_words(...)` — mixes that real `ias` into round 83's
     TCR/TCR2 so the chain closes with a measured input for the first time

Nothing in the probe re-derives an ID2 bit position. The verifier asserts that.

## The expected outcome on the current device, stated up front

Round 35 measured **CX DOWN** (`cx=0x00005ff4`, BIT(31) clear) and nothing since has
powered the GPU domain. So the expected single-boot row is:

```
enchilada-adreno: stage4=refused verdict=N(cx-down or clocks-off) cx=00005ff4 cx_on=0 clocks=0
```

That is a **success of the probe's design, not a failure of the read**: it proves
the gate is fail-closed. Seeing a `stage4=id2 ...` row requires CX to be up, which
requires the genpd/clock backend that does not exist yet.

## What this does and does not prove

**Does:** the stage compiles as strict C++17 for arm64 against the real header
stubs; exactly one register is read, through exactly one volatile dereference;
no store of any kind exists in the file; the gate is consulted before the mapping;
the decode is the contract's, and the boot argument fits the command-line budget.

**Does not:** it has **not been flashed or run**. It reads no register today. Even
when it runs, a CX-DOWN board will refuse — and that refusal tells us nothing about
`ID2`, only about CX. The word itself is still unknown.

## Wiring

- `board.toml`: `enchilada-adreno=4` added (18 bytes + separator, against the
  measured 49 bytes of headroom; the joined line is now **970/1000**).
- The stage is **default-off**: absent argument = no-op, and the mask accepts only
  1/2/3/4.

## Verifiers

- `scripts/verify-enchilada-probe-stage4.py` — **45 checks**: stage arming and
  non-regression of 1/2/3, the gate struct and the measured CX word (including a
  negative assertion that it is never hard-coded), the map being inside the
  permitted branch only, the single `rd32(gSmmuVa, 0x28u)` and `dsb sy`, the
  contract-only decode, no re-derived field positions, no store through any
  pointer, no libc, pure LF, and the boot-arg budget.
- `scripts/verify-enchilada-probe-stage3.py` — updated to the new mask
  (40/40 checks): a new stage must not silently widen what the boot arg accepts.
- `scripts/check-enchilada-adreno-probe.py` — **18/18**, including "the only
  dereference is a read, not a store" and "no unexpected undefined symbols".
- Full tree: **62 of 63** verifiers green; the 63rd
  (`verify-enchilada-gmu-caller-v1-record.py`) requires a 40-hex revision argument
  by design and PASSes with one.

## Honest boundary

This is the smallest honest step toward "Adreno actually renders": it makes the
missing input *obtainable* the only way it can be, and it makes the attempt safe.
It does not power CX, does not program a context bank, does not allocate a page
table or an ASID, and does not bring `Mesa`/`freedreno` any closer by itself. The
admission table is unchanged at **modeled = 9 / satisfied = 0 / BLOCKED**.
