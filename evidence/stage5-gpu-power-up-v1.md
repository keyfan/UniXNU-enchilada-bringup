# ROUND 51 · Date: 2026-11-11 · A6XX_GEN1 ordered CX/GX genpd + clock POWER-UP model — HOST-VERIFIED

**What was built.** `drivers/NativeAdreno/AdrenoPowerUpV1.{h,c}` — a freestanding C11
model of the upstream `a6xx_gmu_resume()` power-up ORDER, closing the gap left by the
round-46 `AdrenoGmuCallerV1` (which only ASSERTS the genpd/clock preconditions and
"maps nothing, enables no clock and no regulator by itself"). The module emits a
deterministic 5-step plan and executes it through caller-supplied function pointers
(no MMIO, no clock/regulator access inside the module):

1. `CX genpd` on — `pm_runtime_get_sync(gmu->dev)` (a6xx_gmu.c:1033).
2. `GX genpd` on — `pm_runtime_get_sync(gmu->gxpd)` (a6xx_gmu.c:1041), GUARDED by
   `if (!IS_ERR_OR_NULL(gmu->gxpd))` (:1040). On enchilada GX software power-on is
   `gdsc_gx_do_nothing_enable` ⇒ GX-OFF is the expected steady state, so `strict_gx`
   is recommended 0 and the guarded op is skipped when `gxpd_present == 0`.
3. `core rate` = 200000000 Hz (a6xx_gmu.c:1044).
4. `hub rate` = 150000000 Hz for A630 (non-A740-family), vs 200000000 for A740
   (a6xx_gmu.c:1045-1046 `adreno_is_a740_family(...) ? 200000000 : 150000000`).
5. `clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks)` (a6xx_gmu.c:1047).

On ANY failed step the module rolls back the acquired genpds in REVERSE order
(GX off then CX off, a6xx_gmu.c:1049-1050) and returns a typed status
(Ok/Invalid/CxFailed/GxFailed/CoreRateFailed/HubRateFailed/ClocksFailed/RollbackFailed).
`Execute` produces a `struct AdrenoGmuCallerV1Pre` for composition with the round-46
caller; `AdrenoPowerUpV1PreIsReady` documents the composition limit (if GX genpd is
absent, `gx_genpd_on = 0` ⇒ the round-46 caller returns NotReady).

**Two DISTINCT clock counts (load-bearing, do not conflate).**
- `qcom,gmu` node (`enchilada.dts:23889-23890`): FOUR clocks
  `gmu_clk\0cxo_clk\0axi_clk\0memnoc_clk` = `gmu->nr_clocks` from
  `devm_clk_bulk_get_all(gmu->dev, ...)` (a6xx_gmu.c:1481-1486). This is the count
  used by `clk_bulk_prepare_enable`.
- `qcom,kgsl-3d0` node (`enchilada.dts:23657`): SIX clocks
  `core_clk\0rbbmtimer_clk\0mem_clk\0mem_iface_clk\0gmu_clk\0l3_vote`.
`ADRENO_POWER_UP_V1_GMU_CLOCKS 4u` and `ADRENO_POWER_UP_V1_GPU_CLOCKS 6u` are kept
separate; `AdrenoPowerUpV1CountsAreDistinct()` / `AdrenoPowerUpV1InputsValid()`
refuse inputs that conflate them. Upstream `clk_set_rate(..., "gmu"/"hub")` names
are mainline sdm845.dtsi names, NOT vendor DTB names — recorded, not reused.

**Source provenance (sha-pinned, host-readable).** `a6xx_gmu.c` bytes 48981 sha
`c14fbb253e602bfd2585d757da871dde08ea63b811ff3c6808f657f4522f78d4`
(`scripts/data/adreno-a630-inputs.json`, Linux v6.12); vendored DTB sha
`e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275`
(`scripts/data/enchilada-gpu-domain-state.json`). No live network fetch this round
(live mainline dtsi fetches were dead ends in earlier rounds).

**Three independent verifications (m03546).**
1. Method 1 — host test suite `scripts/check-enchilada-adreno-host.py` (now 11 tests):
   `adreno-power-up-v1: 84 checks, 0 failures`, `PASS: A6XX_GEN1 ordered genpd +
   clock power-up (7 test groups)`. Groups: board_constants / inputs_valid / plan /
   execute_success / execute_guarded_skip / failures / composition (the last drives
   the REAL `AdrenoGmuCallerV1Precheck` with the device-measured round-35 words
   `cx=0x00005ff4 gx=0x00088400`). `HOST_EXIT=0`.
2. Method 2 — `scripts/verify-enchilada-power-up-v1.py` (never compiles/runs the C
   module; only re-derives from the sha-pinned upstream source + DTB + module text):
   `POWER_UP_V1_CROSS: PASS (52 checks, 0 failures)` `CROSS_EXIT=0`. Asserts the
   exact upstream line anchors (:1033/:1040/:1041/:1044/:1045/:1046/:1047/:1049/
   :1050/:1481/:1486), the two DTB clock counts, header macros, plan order, guarded
   GX, reverse rollback, and build/runner wiring.
3. Method 3 — strict freestanding compile `work/tmp/strict_compile_r51.ps1`:
   clang `--target=aarch64-apple-darwin -std=c11 -ffreestanding -fno-builtin
   -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Wconversion -Wsign-conversion
   -Wshadow -Wpedantic` → `STRICT_C_EXIT=0`, `STRICT_OBJ_BYTES=2072`.

**Integration.** `scripts/build-enchilada-adreno.py` now lists `AdrenoPowerUpV1.c`
as the 11th archive object (was 10) and `scripts/check-enchilada-adreno-host.py`
lists the 11th host test. CI #227 (in flight) is the compile authority for the
11-object `libEnchiladaAdreno.a` under macOS/Xcode; the local strict compile is a
pre-CI host gate only.

**Honest boundary.** HOST-VERIFIED only. NO device run, NO flash, NO kernel caller:
this is a freestanding pure-C MODEL of the power-up order, not a runtime backend.
`CONFIG_XNU_NATIVE_ADRENO=n`; the powered-GMU boot chain remains compiled-not-
executed. The gpu area STAYS `host-verified` (the round 49/50 stage-2 probe remains
the device-verified narrow sub-result). User m39510「如有需要可以随时上机」gave a
standing flash authorization, but it was NOT exercised: without a kernel caller
that wires the genpd/clock function pointers to real hardware, a flash would have
NO observable effect. Remaining work: a real XNU kernel backend implementing the
CX/GX genpd + 6-clock enable path, then a kgsl SMMU stage-1 context (target streams
must first be made to fault, per flash #8 `pre=10` = `ArmSmmu4KBusy`), then the
GMU boot chain on hardware.
