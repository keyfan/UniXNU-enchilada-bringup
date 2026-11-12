# Stage 5 GPU — WHY the GPU VA map cannot be derived from a document (ROUND 77)

**Status: HOST-VERIFIED. DERIVATION MODEL ONLY.** This is NOT a board VA map and
NOT a driver. It encodes the exact mainline chain that produces the GPU virtual
address aperture, and proves — rather than asserts — that the chain **terminates
in a runtime SMMU register read** on this hardware.

That is the load-bearing result: the GPU VA range is not a board constant, so the
round-67 "window-zero gate" is waiting for something **no static source can
supply**.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area **STAYS
`"host-verified"`**. No device run, no flash, `m39510` NOT exercised, no MMIO, no
page table, no mapping. It does **not** unblock stage 5.

## Why this round

The admission table has read `board-va-map` as `modeled = 0` since round 69 with
the reason *"the map itself is the missing input"*, and earlier rounds said
variants of *"a board VA map is absent"*. **None of them said why it cannot simply
be derived** the way the catalog, the clock lists and the register offsets were
derived from pinned sources.

This round answers that, and the answer changes what the remaining work is:
deriving it needs a **powered SMMU and a page-table walker**, not another
document.

## The chain, and where it stops (3 sha256 pins)

| pin | file | bytes | sha256 |
| --- | --- | --- | --- |
| P1 | `adreno/adreno_gpu.c` | 29592 | `e6b5203f…f6f00b7` |
| P2 | `arm/arm-smmu/arm-smmu.c` | 67693 | `d5a22b62…ecb4fb4a` |
| P3 | `adreno/a6xx_gmu.c` | 48981 | `c14fbb25…522f78d4` |

```
GPU aperture
  <- max(16 MiB, aperture_start) .. aperture_end     P1:224-225
  <- (1 << ias) - 1                                  P2:828
  <- ias = smmu->va_size                             P2:735
  <- arm_smmu_id_size_to_bits(ID2.UBS)               P2:1904-1905
  <- ARM_SMMU_GR0_ID2 READ AT PROBE                  P2:1879   <-- END
```

Line 5 is the end of what **any pinned source can tell you**. The value depends on
the silicon's `ID2.UBS` field. A document cannot record it, a DTS cannot declare
it, and this port cannot read it without a powered SMMU — the same blocker the
SMMU row already records.

Two shapes exist (`P2:824-828`): the plain TTBR0 geometry
`end = (1 << ias) - 1`, and the TTBR1 variant where `start = ~0 << ias` and
`end = ~0`. Both are encoded; a caller cannot pick one by accident.

## The trap this round makes explicit

`a6xx_gmu.c:1283` creates the **GMU's own** address space as
`msm_gem_address_space_create(mmu, "gmu", 0x0, 0x80000000)`, and `:1245` sets
`range_end = 0x80000000` for allocations. **That 2 GiB is not the GPU's aperture.**
The GPU space is created separately at `adreno_gpu.c:227` and named `"gpu"`.

The module exposes this as a first-class predicate,
`adreno_va_gmu_end_is_gpu_end(gpu_end)`, so a caller cannot substitute one for the
other silently. The verifier additionally recomputes the TTBR0 aperture for
`ias ∈ {32, 36, 40, 48}` and asserts that **none** of them equals `0x80000000` —
i.e. the two numbers are genuinely different on this board, not coincidentally
similar.

## Three blockers, named and derived

| # | blocker | why |
| --- | --- | --- |
| B1 | `ID2.UBS` unread | `arm-smmu.c:1904-1905` derives `va_size` from an ID2 field; `:1879` reads ID2 from the SMMU |
| B2 | no powered SMMU | reading ID2 needs it powered; round 35 **measured** `cx=0x00005ff4` with BIT(31) clear (CX DOWN) |
| B3 | no page-table walker | an address space needs one; round 75 recorded that this port allocates and walks no LPAE table |

`adreno_va_missing()` returns `0x7` and `adreno_va_map_ready()` is **derived** from
it, so it is 0 and flips only when all three genuinely exist.

Note the dependency structure this exposes: **B1 is blocked on B2, which is the
SMMU row's own first input.** So `board-va-map` is not merely "another missing
input" — it is *downstream* of `kgsl-smmu-stage1`. Round 75's ordering note said
"CX must come first of the four"; this round shows it must come first of **five**.

## The admission table

`board-va-map` moves **modeled 0 → 1** (total modelled **8 → 9**).
`satisfied` STAYS **0**, verdict STAYS **BLOCKED**. The *derivation* is modelled;
the *value* is not obtainable.

## Deliverables

| file | role |
| --- | --- |
| `drivers/NativeAdreno/AdrenoVaMapV1.h` | 3 sha256 pins; the floor, the GMU end, two shapes, three blockers; 10 APIs |
| `drivers/NativeAdreno/AdrenoVaMapV1.c` | freestanding C11; the clamp, both geometries, the aperture composer, the GMU/GPU predicate, derived readiness |
| `drivers/NativeAdreno/tests/adreno-va-map-v1-test.cpp` | 7 test groups |
| `scripts/verify-enchilada-va-map.py` | cross-verifier, **98 checks**, aperture recomputed for four `ias` values |
| `scripts/build-enchilada-adreno.py` | 28th archive unit |
| `scripts/check-enchilada-adreno-host.py` | 28th host suite |

## Verifications

1. `PASS: enchilada A630 GPU VA aperture derivation model (7 test groups, 0
   failures)` inside the 28-suite runner.
2. `VA_MAP_CROSS: PASS (98 checks, 0 failures)` — Domain A re-derives the chain,
   both shapes, the runtime read and the GMU/GPU distinction, and recomputes the
   aperture for `ias ∈ {32,36,40,48}` including monotonicity; Domain B asserts the
   clamp and both shapes are implemented rather than described.
3. Strict freestanding compile: arm64 Mach-O **2680 B**, COFF 2373 B,
   `R77_STRICT: PASS`.

## A defect this round found in itself

The test file was labelled 6 groups and asserted 6; it actually contains **7**
(clamp, ias, geometry, aperture, GMU trap, blockers, self-check). Corrected
upward, and the verifier now reports the actual function names when the count
mismatches so the next such error is diagnosable in one line.

## Honest boundary

A derivation model is not a map. There is no MMIO, no page table, no VA
allocation, no mapping, and **three blockers** stand in the way, the first of
which is downstream of the SMMU row's own CX requirement. The four hard blockers
of `docs/porting/gpu.md` §1 remain open; the A630 client GPU windows stay all-zero;
the `gpu` area **STAYS `"host-verified"`**; no device run, no flash, `m39510` NOT
exercised. It does **not** unblock stage 5.

Chain: round 76 closed at `0d9a34303af2b1bb8169baa048cb79f32f3d5139`.
