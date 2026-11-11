# Stage 5 GPU — power-up/GDSC composition contract (round 58)

Status: **HOST-VERIFIED**. No CI run, no flash, no device run, no MMIO.
GPU area remains `host-verified`.

## What was still unproven after round 57

Round 57 DERIVED what the CPU physically writes to turn a GPUCC domain on or off
(`gdsc_enable()` / `gdsc_disable()` → an ordered step plan). Round 51 built the
ordered 5-step genpd/clock power-up plan for A6XX_GEN1. But the two were joined
only by trust:

    AdrenoPowerUpV1Execute()  ->  bus->genpd_on(cookie, 0)   /* CPU claimed to do CX */
                                  bus->genpd_on(cookie, 1)   /* GX, guarded */

Nothing checked that step 0 actually *is* the derived CX write sequence, or that
step 1's `guarded` flag marks the correct domain. A bus implementation that
turned `genpd_on` into a silent no-op for the wrong domain would have passed
every test written so far.

Round 58 closes that gap with a derived invariant rather than a comment.

## The invariant

For every genpd step of the power-up plan,

    StepGuarded(step)  ==  (the CPU's GDSC register writes for that domain == 0)

and it is DERIVED, not asserted:

| domain | guarded | CPU enable register WRITES | why |
|---|---|---|---|
| CX | 0 | **1** | `gdsc.c:150` clears the collapse bit at `0x106c`; the status `0x1540` is then **read**, not written |
| GX | 1 | **0** | `gpu_gx_gdsc.pd.power_on` is `gdsc_gx_do_nothing_enable()` and the descriptor has neither `.supply` nor `.cxcs` |

So `guarded` is not a stylistic choice: it marks exactly the step the CPU
*cannot* act on. Upstream guards that same step (`a6xx_gmu.c:1040-1041`,
`!IS_ERR_OR_NULL(gmu->gxpd)`) for an independent reason — whether the GX genpd
exists — and both reasons land on the same domain.

## The first mistake this round caught

My initial claim was "CX performs 2 register writes". Host test
`adreno-power-up-gdsc-test` failed on `c.cx_enable_writes == 2u`. The derived
plan reports `register_writes == 1`: the collapse clear is the only write, and
`POLL_ON` is a **read**. The C module and its verification were right; my prose
was wrong. Both the constant and the comment were corrected to 1. This is
exactly the failure mode this module exists to make visible — conflating a write
path with a read path on a block whose power state is not confirmed.

## The second finding: CX power-off is UNVERIFIED by construction

CX disable writes the collapse bit, waits 500 µs, and **polls nothing**
(`gdsc.c:153-161`, the VOTABLE short-cut; the board DTB mirrors it as
`qcom,no-status-check-on-disable`). GX, which is not VOTABLE, does poll its
down-complete bit and does clamp IO afterwards.

That asymmetry is a safety boundary: after a successful CX power-off the CPU has
no confirmation that the domain actually collapsed. Given round 53 established
that touching a powered-down GPU-adjacent block (the kgsl SMMU at `0x5040000`)
may bus-abort, "CX-disable returned 0" must never be read as "CX is quiet".
The module names this as `AdrenoPowerUpGdscDisableIsUnverified(which)` instead
of leaving it as folklore.

## Deliverables

* `drivers/NativeAdreno/AdrenoPowerUpGdsc.h` / `.c` — the composition contract.
  `struct adreno_power_up_gdsc_contract` holds no stored constant: every field is
  computed per call from `AdrenoGdscDecode` and `AdrenoPowerUpV1`. There is not
  one hard-coded write count, poll offset or guarded bit in the file; the
  cross verifier asserts that mechanically.
  API: `AdrenoPowerUpGdscContract`, `AdrenoPowerUpGdscStepWrites`,
  `AdrenoPowerUpGdscGuardMatchesWrites`, `AdrenoPowerUpGdscDisableIsUnverified`,
  `AdrenoPowerUpGdscVerify`.
* `drivers/NativeAdreno/tests/adreno-power-up-gdsc-test.cpp` — 41 checks across
  6 groups (contract values, guard/writes invariant, disable verification,
  non-GDSC steps, aggregate, power-up plan intact).
* `scripts/verify-enchilada-power-up-gdsc.py` — 66 checks that re-derive the CX
  and GX write counts in PYTHON from the pinned upstream bytes and only then
  compare against the C module.
* Wiring: `scripts/build-enchilada-adreno.py` now has **14 archive objects**;
  `scripts/check-enchilada-adreno-host.py` now has **14 host tests**.
* `scripts/data/enchilada-gpu-gdsc-inputs.json` — `module.api_round58` plus the
  `round58_composition` block recording the derived counts, the invariant, and
  the two corrections above.

## Three verifications

1. Full host suite (`scripts/check-enchilada-adreno-host.py`, 14 suites):
   exit 0, every sub-group 0 failures, including
   `PASS: enchilada Adreno power-up/GDSC composition host tests (41 checks)`
   and the unchanged `GDSC derivation (108 checks)`,
   `power-up-v1 (84 checks)`, `gmu-caller-v1 (50 checks)`,
   `kgsl-smmu-gate (58 checks)`.
2. Strict freestanding compile,
   `--target=aarch64-apple-darwin -std=c11 -ffreestanding -fno-builtin
   -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Wconversion -Wsign-conversion
   -Wshadow -Wpedantic`: exit 0, object 3536 B, magic `cf fa ed fe`,
   `llvm-nm` exports 5 `_AdrenoPowerUpGdsc*` symbols.
3. Cross verifier `scripts/verify-enchilada-power-up-gdsc.py`:
   `POWER_UP_GDSC_CROSS: PASS (0 failures)`, 66 checks — including that both
   `*_enable_writes` assignments copy `register_writes` rather than a literal, so
   the C cannot drift away from the derivation.

Both verifier defects it found were mine: asserting `gdsc_poll_status(sc,
GDSC_OFF)` inside `gdsc_disable()` (the OFF poll actually lives in
`gdsc_toggle_logic`, `gdsc.c:177`) and expecting a `gx_disable_polls` field that
the struct did not yet have. The verifier was corrected in the first case and
the C was corrected in the second.

## Honest boundary

Everything above is a derivation from sha256-pinned Linux v6.12 sources plus the
vendored board DTB, cross-checked against the C source text. **No register is
read or written, no domain is powered, no firmware is loaded, no GMU is booted,
no CI ran, and no device was touched.** The device words cited in earlier rounds
(`cx=0x00005ff4`, `gx=0x00088400`) are *reused* as consistency inputs, not
re-measured here.

Consequently the GPU area stays `host-verified` and `CONFIG_XNU_NATIVE_ADRENO`
stays `n`. What this round buys is mechanical proof that the order we intend to
execute is upstream's order and that the step we marked "the CPU cannot touch"
is provably the one with zero CPU writes — not that executing it on enchilada
succeeds. Still open for stage 5: a real XNU genpd/clock backend, TrustZone PAS
authentication for `GPU_PAS_ID 13`, and a kgsl SMMU stage-1 context bank that
can only be programmed once CX is genuinely up.
