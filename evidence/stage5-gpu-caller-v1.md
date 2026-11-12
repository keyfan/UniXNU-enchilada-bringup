# Stage 5 GPU — round 64: the ordering layer (AdrenoCallerV1)

**Status: HOST-VERIFIED.** No device run. The phone was reachable in TWRP
recovery during this round (`27f4f45d`, `uname -r 4.9.179-perf+`, `id -u` = 0)
but TWRP exposes no `/dev/mem`, no `/dev/kmem`, no regmap debugfs, an empty
`/sys/kernel/debug`, no `/proc/config.gz`, and `kptr_restrict=2`. Under those
conditions there is no safe userspace way to read the GPUCC register file, and
the round-22/28 prohibition against cold reads of the GPU windows while
`gpu_cx` is collapsed is unchanged and remains in force.

## Why this round exists

Round 63 ended with the sentence "an `IOService` that owns the mapping, calls
`Precheck`, then `Bind`, then `Make`/`Up`". Read as an execution order, that is
impossible, and finding out *why* is the substance of this round.

`AdrenoGmuCallerV1Precheck()` refuses unless

```
pre->cx_genpd_on != 0 && pre->gx_genpd_on != 0 && pre->clocks_enabled != 0
```

and those three fields are produced by `AdrenoPowerUpBackendV1Up()`. A gate
that consumes the power-up's output cannot run before the power-up. `Precheck`
therefore runs **last**, after the MMIO has already happened.

That is not a defect in either module — round 62's header even says the point
of returning a `Pre` from `Up()` is to feed the gate — but it has a consequence
worth stating plainly:

> **The first GPUCC access of any real bring-up is a WRITE.** The CX enable
> plan's collapse-clear is a store to `0x106c`.

Every device-verified result this port has (rounds 35, 49, 52) began with a
**read** of `0x1540`/`0x1010` and decided from the answer. The executor begins
by writing, and nothing ahead of it observes anything.

## What was done instead of faking it

Making `Precheck` a pre-gate would require manufacturing those three bits
before the power-up that produces them — the "assert what you want, then verify
it" move this port has spent sixty rounds avoiding. So the module inserts the
missing observation around the existing order:

```
OBSERVE   read 0x1540 + 0x1010, decode, record     <- first traffic of the run
POWER     AdrenoPowerUpBackendV1Up()               <- the writes
OBSERVE   read 0x1540 + 0x1010 again
VERDICT   AdrenoGmuCallerV1Precheck(pre, gpucc)
```

Two properties follow, both enforced in code and both tested:

1. **The first MMIO attempt is a read, and an unreadable window costs zero
   writes.** `Verify()` and the host suite both drive this against a window
   whose status reads fail: the run stops at the observation, `stats.writes`
   is 0, and `first_op_was_read` is 1.
2. **A successful `Up()` and an accepted `Precheck` are different results.**
   `Up()` succeeding means the five ops executed. `Precheck` then re-reads the
   very CX word the executor's own poll accepted and asks whether it says CX is
   on. Round 35 measured `cx = 0x00005ff4`, BIT(31) clear; nothing here proves
   that clearing the collapse bit raises it. So the status enum keeps
   `kAdrenoCallerV1UpFailed` and `kAdrenoCallerV1PrecheckRefused` apart, and the
   report carries `up_status` and `precheck_status` as separate fields.

## The veto that is deliberately absent

An observation of CX-down **does not** suppress the following write. Writing
`gpu_cx_gdsc`'s own control register is how a collapsed domain is raised, and
that register lives in always-on GPUCC (rounds 28/35). A veto on the
pre-observation would be a rule with no hardware behind it, so the module
records the observation and proceeds. The test `cx down is recorded, not
vetoed` pins this: with a settled clear BIT(31) the run fails, but it fails
*inside the plan's CX poll*, not at the gate.

## One choke point

The backend is not wired to the caller's triple directly. `Make()` installs a
`wrapped` triple whose three members are the counting thunks, so the executor's
accesses and this layer's observations pass through the same place. Without
that, "the first access was a read" would be a claim about one of two code
paths. `AdrenoCallerV1Verify()` asserts that construction alone touches nothing
(`ops == 0`, `saw_op == 0`).

## The assertion I got wrong

Full-run check `rep.ops` vs the window's write count: I first asserted
`stats.writes + 2 <= gHw.writes`, expecting the caller to add writes of its
own. It failed. The caller adds **none** — it only reads, and the executor
behind it writes — so the correct relation is equality, and it is now asserted
as `stats.writes == gHw.writes == 9`, the nine executor writes derived in
round 62.

## Verification

| check | result |
|---|---|
| host suite | **19 suites, 0 failures**; new suite **77 checks** |
| `AdrenoCallerV1Verify` | 35 checks, rc=0 |
| cross-verifier (`verify-enchilada-caller-v1.py`) | **CALLER_V1_CROSS: PASS (48 checks, 0 failures)** |
| strict freestanding arm64 | exit 0, **3072 B**, magic `0xfeedfacf`, 3 `T` exports (`Make`/`Run`/`Verify`) |
| probe shim | 18/18 PASS |
| archives-linked | PASS, 7 force-loaded |
| `check_hardware_claims.py` | 21 == baseline, zero new |

### The cross-verifier failed twice before passing — both were its assumptions

1. It demanded `pre->cx_genpd_on` appear inline in `Precheck`'s body. The gate
   is factored into `caller_v1_pre_ok()`; following the helper is the correct
   check. It also anchored "the first status read" on the word `read`, which
   matches the NULL check `gpucc->read == 0` before the gate. Now anchored on
   the `ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE` macro.
2. It searched for `->write32(` — but `AdrenoPowerUpBackendV1Mmio` is a struct
   **value**, so the forwarder is `.write32(`. Found 0, reported a failure
   against correct code. Then the replacement rule rejected the `NULL` that
   `Verify()` legitimately installs to prove `Make()` rejects an incomplete
   triple.

Both are the round-63 lesson repeating: a verifier's false positive says more
about the verifier than the module, and the fix is to read the source, not to
loosen the assertion.

## What is still missing

- `libEnchiladaAdreno.a` is **still not built.** `build/xnu/ios-obj` does not
  exist in this workspace, so no `IOService` `.cpp` here can be compiled
  locally either. `AdrenoCallerV1.c` is registered in the archive object list
  and in the `source_sha256` set, and the strict freestanding build is the
  substitute check — it is not an archive build, and it is not claimed as one.
- **The IOKit `IOService` itself is not written.** This round is the ordering
  layer the service will call, not the service. What remains for that: own the
  `OSSharedPtr<IOMemoryMap>` at file scope (round-63 note: a local map dies at
  return), parse a boot-arg, and call `Make` → `Run`.
- The boundary this module enforces is **whether an offset is legal**, not
  **whether the moment is safe**. A perfectly in-bounds adapter pointed at a
  collapsed domain window can still hang the bus. `AdrenoGmuCallerV1Precheck`
  is the gate for that, and by this round's own finding it can only run *after*
  the writes it would guard.
