# Stage 5 GPU — A630 GMU teardown order derived from pinned upstream (round 59)

**Status: HOST-VERIFIED. The gpu area STAYS `host-verified`.**

No MMIO is performed. No domain is powered. No firmware is loaded. No CI run and
no flash. Nothing in this document is a device measurement. `CONFIG_XNU_NATIVE_ADRENO`
remains `n`.

## What was still only prose after round 58

Round 51 (`AdrenoPowerUpV1`) models the **power-UP** sequence from
`a6xx_gmu_resume()` (`a6xx_gmu.c:1033-1047`). Round 57 derived the GDSC step plans,
round 58 proved the guarded-bit/write-count composition — but the **DOWN**
direction was never modeled at all. `AdrenoPowerUpV1Bus.genpd_off()` existed for
exactly one purpose: letting `Execute()` roll back after a failed step, in
reverse *acquisition* order (`apuv1_rollback`, `AdrenoPowerUpV1.c:178-193`).

That is not a teardown. Upstream's real teardown is a **constrained ordering**,
and it is not the power-up plan run backwards.

## Pinned sources

All four are exact Linux v6.12 files, sha256-pinned in
`scripts/data/enchilada-gpu-power-down-inputs.json` and read from the on-disk
cache at `.cache/adreno-a630-source/`:

| file | bytes | sha256 |
|---|---|---|
| `drivers/gpu/drm/msm/adreno/a6xx_gmu.c` | 48981 | `c14fbb25…` |
| `drivers/clk/qcom/gdsc.c` | 15019 | `e1210770…` |
| `drivers/clk/qcom/gdsc.h` | 2863 | `ba487640…` |
| `drivers/gpu/drm/msm/adreno/a6xx_gpu.c` | 69087 | `c3347937…` |

## The teardown, verbatim from `a6xx_gmu_stop()` (`a6xx_gmu.c:1184-1217`)

```c
:1189-1190  if (!pm_runtime_active(gmu->dev))
                return 0;
:1196-1199  if (gmu->hung) a6xx_gmu_force_off(gmu);
            else           a6xx_gmu_shutdown(gmu);
:1202       dev_pm_opp_set_opp(&gpu->pdev->dev, NULL);
:1204-1210  /* Make sure the GX domain is off before turning off the GMU (CX)
             * domain. Usually the GMU does this but only if the shutdown
             * sequence was successful */
            if (!IS_ERR_OR_NULL(gmu->gxpd))
                pm_runtime_put_sync(gmu->gxpd);
:1212       clk_bulk_disable_unprepare(gmu->nr_clocks, gmu->clocks);
:1214       pm_runtime_put_sync(gmu->dev);
```

The CX-last rule is stated twice upstream — `gdsc.c:590` *"Hardware constraints
dictate that the GX be powered down before the CX"* and `a6xx_gmu.c:1205`.

## Derived order (the round-59 finding)

| | power-up (round 51) | teardown (round 59) |
|---|---|---|
| 0 | CX genpd on | **GX genpd off** (guarded) |
| 1 | GX genpd on (guarded) | **clocks disable** (4 GMU clocks) |
| 2 | set core rate 200 MHz | **CX genpd off** (last) |
| 3 | set hub rate 150 MHz | — |
| 4 | clocks enable | — |

**Teardown is NOT the reverse of power-up.** Power-up enables the clocks *last*;
teardown disables them *in the middle*, because CX must stay up until the clocks
are already gone. The plan is 3 ops against power-up's 5. Rates are never
restored — `a6xx_gmu_stop()` contains no `clk_set_rate` at all, so a module that
"restored the old rate" would be inventing behavior upstream does not have.

## Finding 1 — CX-off is not verifiable by the CPU

The CX domain's disable plan performs **no status poll**: upstream's VOTABLE
short-cut (`gdsc.c:153-161`) returns after a fixed `udelay(TIMEOUT_US)` without
reading the status register. The board DTB says the same thing in hardware terms
with `qcom,no-status-check-on-disable` on the CX node only.

So after `pm_runtime_put_sync(gmu->dev)` the CPU knows the put was *issued*, not
that CX reached off-complete. GX, by contrast, **does** poll its derived
down-complete bit (`0x1010` `BIT(15)`), so the two domains are genuinely
asymmetric. Recorded as `AdrenoPowerDownV1CxOffIsUnverified()` and asserted from
both sides.

## Finding 2 — the two step enums are separate namespaces that collide

`ADRENO_POWER_UP_V1_STEP_CX_GENPD_ON` and `ADRENO_POWER_DOWN_V1_STEP_GX_GENPD_OFF`
are **both `0`**. My first host test asserted
`ops[0].step != pops[0].step` to argue "teardown op 0 differs from power-up op 0"
— a **category error** (it compares an up-step against a down-step) that failed on
the first run. Both the test and `AdrenoPowerDownV1Verify()` now assert each
plan's own order position by position plus the differing op counts. The header
records the hazard so a future reader does not reintroduce the comparison.

## Delivered

| file | purpose |
|---|---|
| `drivers/NativeAdreno/AdrenoPowerDownV1.h` | step enum, plan/bus/Execute contract, both findings, the no-rollback rationale |
| `drivers/NativeAdreno/AdrenoPowerDownV1.c` | `ShouldRun`/`InputsValid`/`Plan`/`Execute`/`CxOffIsUnverified`/`Verify` |
| `drivers/NativeAdreno/tests/adreno-power-down-v1-test.cpp` | 8 groups, 72 checks |
| `scripts/verify-enchilada-power-down-v1.py` | 51 checks, re-derives the order in Python from the pinned bytes |
| `scripts/check-enchilada-power-down-boundary.py` | 31 checks, boundary in both directions |
| `scripts/data/enchilada-gpu-power-down-inputs.json` | 4 sha256 pins + derived order + findings + honest boundary |
| `scripts/build-enchilada-adreno.py` | 15th archive unit |
| `scripts/check-enchilada-adreno-host.py` | 15th host test, PASS line updated |

`AdrenoPowerDownV1Execute()` deliberately **does not roll back**: re-acquiring CX
to redo a partial teardown would invent behavior, and upstream ignores every
return value and proceeds. Instead it stops at the failing op, leaves `*pre`
**cleared** so the round-46 caller refuses, and reports the failing step. The bus
struct has no `genpd_on`/`clocks_enable`/`set_rate` member at all, so a rollback is
structurally impossible rather than merely avoided.

## Three verifications

1. **Host test** — `PASS: enchilada Adreno power-down host tests (72 checks)`, part
   of the full 15-suite run `HOST_EXIT=0` (all earlier suites unchanged:
   `power-up-gdsc 41`, `gdsc-decode 108`, `power-up-v1 84`, `gmu-caller-v1 50`,
   `kgsl-smmu-gate 58`).
2. **Cross verifier** — `POWER_DOWN_V1_CROSS: PASS (51 checks, 0 failures)`:
   re-reads the pinned bytes, walks `a6xx_gmu_stop()`, and asserts
   `gate < gx < clocks < cx` by byte offset, the CX-last comment, the absence of
   `clk_set_rate`, the header's step numbering, the `.c`'s emission order, and
   the wiring.
3. **Strict freestanding compile** —
   `--target=aarch64-apple-darwin -std=c11 -ffreestanding -fno-builtin -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wpedantic`
   → exit 0, object **2336 B**, magic `cf fa ed fe`, **9 exported
   `_AdrenoPowerDownV1*` symbols**. Plus the boundary audit
   `R59_BOUNDARY: PASS (31 checks)`.

## Honest boundary

This module derives an **order** from pinned upstream text. It does not prove the
hardware obeys it, and it cannot power CX — the domain is still measured DOWN on
the device (`cx-DOWN`, reused from rounds 35/49/50/52; no new device run here).
The gpu area therefore stays `host-verified`. Remaining work is unchanged: a real
XNU genpd/clock backend, TrustZone PAS for `GPU_PAS_ID 13`, and the kgsl SMMU
stage-1 context — and that last one is only safe *after* CX power-up.
