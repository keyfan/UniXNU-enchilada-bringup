# Round 62 — AdrenoPowerUpBackendV1: the executor that finally writes

**Area:** gpu · **Status:** host-verified · **Device evidence:** none this round

## Why this round exists

Rounds 42, 44, 46, 51, 56, 57, 58, 59 and 61 each ended in a plan. That is nine
consecutive rounds whose output is a data structure describing what *should* be
written to the GPUCC register file. Not one of them wrote anything, and — this
is the part that matters — **nothing in the tree would have noticed if the bus
callbacks did nothing at all.**

`AdrenoPowerUpV1Bus` had four callbacks and no implementation anywhere. A
backend could be wired with four `return 0;` stubs and every existing test,
including all 76 checks of round 61, would still pass. The `set_rate` and
`clocks_enable` callbacks were the exact hole: round 61 derived *what* they
should do, but the derivation was never connected to an executor.

This round closes that. `AdrenoPowerUpBackendV1` supplies a real
`AdrenoPowerUpV1Bus` whose four callbacks turn the derived plans into MMIO.

## What the executor does, per leg

| leg | derived source | writes | notes |
|---|---|---|---|
| `genpd_on(0)` CX | round 57 `adreno_gdsc_plan_cpu_enable` | **1** | collapse clear at `0x106c`, then poll `0x1540` BIT(31) |
| `genpd_on(1)` GX | round 57 (gx = do-nothing) | **0** | `gdsc_gx_do_nothing_enable`; the GMU powers GX up |
| `set_rate(0)` core | round 61 `PlanSetCoreRate` | **0** | RCG disabled at resume → parked, word `0x0602` cached |
| `set_rate(1)` hub | round 61 finding 1 | **0** | no `hub` clock exists on SDM845 |
| `clocks_enable` | round 61 `PlanRcgEnable` + `PlanBranchEnable` | **8** | 4 RCG + 2 per branch clock × 2 |
| **total** | | **9** | |

The eight-write split is derived, not asserted: `clk_rcg2_shared_enable()` is
force-enable + CFG + UPDATE + clear (4), and `clk_enable_regmap()` is
wake/sleep recommendation + CLOCK_ENABLE (2) for each of the two GPUCC branch
clocks.

## The bug this round found in its own test

The first run failed with 11 failures, all in `clocks_enable` and the full-chain
test. The cause was **the simulator, not the module**: it modelled the RCG CMD
register at `0x1120` as plain storage, so

- `CMD_UPDATE` (BIT 0) is write-1-to-trigger and self-clears in the register
  file — the poll after it could never end against a register that remembered
  the write;
- `CMD_ROOT_OFF` (BIT 31) is a read-only status bit, not a latched value.

Modelling it correctly (mask both bits out on read) turned all 11 green. This is
worth recording because it is the failure mode the whole round is about: a
simulated register file that does not behave like hardware will happily certify
an executor that would hang on the device.

## Fail-closed rules

- `Make()` refuses `armed != 1` — an unarmed backend is not constructed at all,
  rather than constructed inert, so a constructed backend is one the caller
  meant to run.
- Every disarmed MMIO attempt increments `stats.refused` and fails; it must stay
  zero in any real run.
- `clocks_enable` refuses before a parked CFG word exists (nothing was derived).
- `clocks_enable` refuses a bulk count that is not the derived 4 — claiming 6
  would claim the two GCC clocks this window does not own.
- A GDSC step kind the executor does not implement fails closed rather than
  being skipped.
- A regulator step fails closed: this module owns no regulator handle, and the
  two GPUCC domains have no `.supply`, so such a step should never appear.

## Verification

- Host suite: 17 suites, 0 failures. New suite: **51 checks**,
  `PASS: enchilada power-up backend host tests (51 checks)`.
- Independent cross-check: `POWER_UP_BACKEND_V1_CROSS: PASS` — re-derives the
  9-write total in Python from the plan shapes and asserts the executor contains
  no write-count literal of its own.
- Strict freestanding arm64: `STRICT_EXIT=0`, 4968-byte object.
- `check-enchilada-archives-linked.py`: PASS, 7 force-loaded archives.

## What was NOT verified, and why

`scripts/build-enchilada-adreno.py` **could not be run in this workspace**: it
compiles against recorded XNU compile flags read from
`build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/...cpo.json`, and that tree does
not exist here (`MISSING: build\xnu\ios-obj`; `build/` contains only
`enchilada-adreno`). `AdrenoPowerUpBackendV1.c` **is** added to the archive's
object list and to its `source_sha256` set, but the archive itself was not
rebuilt. This is a pre-existing workspace condition, not a regression from this
round — and it is why the round-61 evidence file recorded only "16th archive
object" rather than an archive build result.

The strict freestanding compile above is the substitute check: it proves the
module builds for arm64-apple with no libc and no host headers, which is the
property the archive build would have exercised.

## Honest boundary

HOST-VERIFIED EXECUTOR. It performs MMIO *through the caller's callbacks* and
does nothing else: it maps nothing, owns no window, enables no regulator, runs
no TrustZone PAS, and has no IOKit caller.

The round-22/28 prohibition is **unchanged and not enforced here**: arming this
module against a collapsed domain will still hang the bus. The gate that must
run first is `AdrenoGmuCallerV1Precheck` — and the reason `Up()` returns an
`AdrenoGmuCallerV1Pre` is precisely so it can be fed to exactly that.

The gpu area stays `host-verified`. Last device evidence remains round 52.
