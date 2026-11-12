# ROUND 61 · Date: 2026-11-12 · A6XX_GEN1 GPUCC clock-control derivation — HOST-VERIFIED

**No MMIO. Nothing mapped. No domain powered. No clock enabled. No kernel caller.
No CI run. No device run. No flash.** The gpu area **stays `host-verified`** and
`CONFIG_XNU_NATIVE_ADRENO` remains `n`.

## Why this round existed

Round 51 (`AdrenoPowerUpV1`) encoded the **order** of `a6xx_gmu_resume()`
(`a6xx_gmu.c:1033-1047`) as five ops behind caller-wired
`AdrenoPowerUpV1Bus` callbacks. Round 58 (`AdrenoPowerUpGdsc`) bound the two
**genpd** steps to derived GDSC register writes.

The three **clock** steps — `SET_CORE_RATE`, `SET_HUB_RATE`, `CLOCKS_ENABLE` —
stayed opaque. A `bus->set_rate()` or `bus->clocks_enable()` callback could do
anything at all and every existing test would still pass. That is the same
"trust, not contract" gap round 58 closed for the domains, still open for the
clocks, and it is the half a backend actually has to get right.

This round derives what those callbacks must write inside GPUCC, from the
sha256-pinned Linux v6.12 clock sources, so the clock half becomes checkable.

## Finding 1 (load-bearing) — there is no "hub" clock on this board

`a6xx_gmu.c:1045-1046` unconditionally does:

```c
clk_set_rate(gmu->hub_clk, adreno_is_a740_family(adreno_gpu) ?
	     200000000 : 150000000);
```

`gmu->hub_clk` comes from `a6xx_gmu.c:1491-1493`:

```c
gmu->hub_clk = msm_clk_bulk_get_clock(gmu->clocks, gmu->nr_clocks, "hub");
```

and `msm_clk_bulk_get_clock()` (`msm_io_utils.c:17-32`) matches a bulk id equal
to `"hub"` **or** `"hub_clk"`, skipping `snprintf("%s_clk")` — then returns
`NULL` when nothing matches.

The SDM845 GMU node names four clocks (`sdm845.dtsi:4941`, and the vendored
vendor DTB agrees verbatim at `enchilada.dts:23890`):

```
clock-names = "gmu", "cxo", "axi", "memnoc";
```

No `hub`. So the handle is `NULL`, and `clk_set_rate()` on a NULL handle is a
**documented no-op that returns success** (`clk.c`):

```c
int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (!clk)
		return 0;
	...
```

**Therefore the 150 MHz hub-rate step is dead code on enchilada.** It costs
nothing, writes nothing, and cannot fail.

### The A740 contrast is a different *shape*, not a different rate

The SM8550 GMU node lists **seven** clocks and includes one literally named
`hub`:

```
clock-names = "ahb", "gmu", "cxo", "axi", "memnoc", "hub", "demet";
```

So the *same upstream line* really programs an RCG on A740 and is a no-op on
A630. `AdrenoPowerUpV1HubRateHz()` is still correct as a **value** (the ternary
is exact), but a backend that implemented `SET_HUB_RATE` as a GPUCC register
write on this board would be encoding behaviour the hardware does not have —
and a test that expected a write there would be testing an invention.

## Finding 2 (the core rate is a PARKED write, not a write)

`clk_set_rate(gmu->core_clk, 200000000)` is `:1044`; `clk_bulk_prepare_enable`
is `:1047`. **The rate is set while the RCG is still disabled.**

`gpu->core_clk` resolves to `gpu_cc_cx_gmu_clk` (`gpucc-sdm845.c:88-104`, a
branch on `0x1098`) whose parent is `gpu_cc_gmu_clk_src`
(`gpucc-sdm845.c:74-86`, an RCG at `cmd_rcgr 0x1120`, `.mnd_width = 0`,
`.hid_width = 5`, ops `clk_rcg2_shared_ops`). So the call runs
`clk_rcg2_shared_set_rate()`, and:

1. `qcom_find_freq()` picks the **first** row with `rate <= row.freq` →
   `F(200000000, P_GPLL0_OUT_MAIN_DIV, 1.5, 0, 0)`.
2. The `F()` macro (`clk-rcg.h:10`) stores `2*h-1`, so **pre_div is 2, not
   1.5 and not 1**. `parent_map_0` maps `P_GPLL0_OUT_MAIN_DIV` → **cfg 6**.
   With `mnd_width == 0` there are **no M/N/D writes and no `CFG_MODE_DUAL_EDGE`
   bit** — the entire CFG word is:

   ```
   (2 << 0) | (6 << 8) == 0x0602
   ```

3. Because the RCG is **not enabled**, the shared ops take the parked branch
   (`clk-rcg2.c:1198-1205`):

   ```c
   if (!clk_hw_is_enabled(hw))
           return __clk_rcg2_configure(rcg, f, &rcg->parked_cfg);
   ```

   → **zero register writes**.

The real CFG write happens later, inside `clk_rcg2_shared_enable()`
(`clk-rcg2.c:1216-1239`), which writes `parked_cfg` back. So the derived plan
for the resume path is **one `PARK_CFG` step with `register_writes == 0`**, and
the first actual GPUCC CFG write is part of the *enable* leg. Deriving the two
plans separately would have hidden this; they have to be derived together.

## Finding 3 (only 2 of the 4 clocks are even in this window)

The bulk prepare/enable covers four clocks, but only two are GPUCC registers:

| bulk id | real clock | register | in GPUCC? |
|---|---|---|---|
| `gmu` | `GPU_CC_CX_GMU_CLK` | `0x1098` | **yes** |
| `cxo` | `GPU_CC_CXO_CLK` | `0x109c` | **yes** |
| `axi` | `GCC_DDRSS_GPU_AXI_CLK` | GCC | no |
| `memnoc` | `GCC_GPU_MEMNOC_GFX_CLK` | GCC | no |

Any count this module reports is a **GPUCC-subset count (2)**, never the bulk
count (4). Keeping them distinct is the same anti-conflation rule
`AdrenoPowerUpV1CountsAreDistinct()` already applies to the 4-vs-6 GMU/kgsl
clock counts.

## Finding 4 (the branch halt check is a disjunction)

`clk_branch2_check_halt()` (`clk-branch.c:43-60`) when enabling accepts
**either** path:

```c
if (enabling) {
	val &= mask;
	return (val & CBCR_CLK_OFF) == (invert ? CBCR_CLK_OFF : 0) ||
		FIELD_GET(CBCR_NOC_FSM_STATUS, val) == FSM_STATUS_ON;
}
```

Modelling this as a single-mask poll would silently drop the NOC-FSM acceptance
path and could report a healthy clock as stalled. The module gives it its own
step kind, `ADRENO_CLK_CTL_STEP_POLL_BRANCH2_ON`.

## Derived plans

| plan | steps | register writes | polls |
|---|---|---|---|
| `PlanSetCoreRate(200 MHz, rcg_enabled = 0)` — **the resume path** | 1 | **0** | 0 |
| `PlanSetCoreRate(200 MHz, rcg_enabled = 1)` | 6 | 4 | 2 |
| `PlanRcgEnable(0x0602)` | 6 | 4 | 2 |
| `PlanBranchEnable(CX_GMU 0x1098)` | 3 | 2 | 1 (branch2) |
| `SET_HUB_RATE` | — | **0** (finding 1) | 0 |

A backend performing the resume-time clock work therefore issues exactly
**six** GPUCC writes: none for the core rate (parked), none for the hub rate
(absent), four for the RCG enable leg, and two for the CX_GMU branch.

## Correction of an implicit assumption (rounds 45-60)

Rounds 45-60 treated `SET_HUB_RATE` as a real clock-rate step and reported the
150 MHz figure as data the backend would program. The **value** was and remains
correct (`a6xx_gmu.c:1045-1046` is exact); only the implied **effect** is
corrected here. No prior boundary is retracted, and nothing in this round
changes any earlier claim about power domains, firmware, PAS or SMMU.

## Two independent verifications

1. `scripts/check-enchilada-adreno-host.py` — compiles and runs the C module as
   C++17 with the address/undefined-behaviour sanitizers when available.
   **16 suites, 0 failures**; the new suite is **76 checks**.
2. `scripts/verify-enchilada-clk-ctl-v1.py` — never compiles or runs the C
   module; re-derives every fact in Python straight from the pinned bytes plus
   the vendored DTB, then checks the module agrees. **`CLK_CTL_V1_CROSS: PASS
   (72 checks, 0 failures)`**.

A strict freestanding compile also passes and produces a real arm64 Mach-O
object:

```
clang -target arm64-apple-darwin -std=c11 -ffreestanding -fno-builtin \
      -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Wconversion \
      -Wsign-conversion -Wshadow -Wpedantic -Wundef -Wcast-qual \
      -Wstrict-prototypes -c drivers/NativeAdreno/AdrenoClkCtlV1.c
→ STRICT_C_EXIT=0, 2368 bytes, magic 0xfeedfacf cpu 0x100000c, 12 exports
```

Both verifiers independently confirm each other's arithmetic; the Python side
does not read the C table for its expected values, it recomputes them from
`gpucc-sdm845.c`.

## Proves / does NOT prove

- **Proves:** the three clock steps of the A630 power-up plan are a function of
  the pinned clock tree; the hub step is a provable no-op on this board and a
  real write on A740; the resume-time core-rate set writes nothing and the CFG
  word arrives during enable; only 2 of the 4 bulk clocks are GPUCC registers;
  the branch halt check is a disjunction. All of it is asserted mechanically in
  two independent directions.
- **Does NOT prove:** that any GPUCC register is readable or writable on this
  device right now (round 22/28 prohibitions are unchanged, and reading GPUCC
  still requires the full precondition chain), that the GMU comes up, that the
  clocks actually settle, or that any of this executes in the kernel. Nothing
  here is a device measurement, and `CONFIG_XNU_NATIVE_ADRENO` stays `n`.

## What stage 5 still needs

Unchanged in shape, now with the clock half narrowed to a contract:

1. a real XNU genpd/clock backend that **wires these callbacks to hardware**
   (`AdrenoPowerUpV1Bus` already has the right shape: `genpd_on`, `genpd_off`,
   `set_rate`, `clocks_enable`);
2. TrustZone PAS authentication for `GPU_PAS_ID 13`;
3. the kgsl SMMU stage-1 context at `0x5040000`, only **after** CX is genuinely
   up (round 53: a powered-down read may bus-abort).

## Chain

```
<phase 1> drivers/NativeAdreno/AdrenoClkCtlV1.{h,c} + tests/adreno-clk-ctl-v1-test.cpp
<phase 2> scripts/data/enchilada-gpu-clk-inputs.json + scripts/fetch-enchilada-gpu-clk-sources.py
          + scripts/verify-enchilada-clk-ctl-v1.py + this document
<phase 3> scripts/build-enchilada-adreno.py 16th archive object + host runner 16th suite
<phase 4> porting.toml gpu area records round 61
```
