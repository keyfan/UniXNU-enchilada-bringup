# ROUND 53 · Date: 2026-11-11 · enchilada kgsl-SMMU read SAFETY study — HOST-VERIFIED (NO device run)

**What was built.** `drivers/NativeAdreno/AdrenoKGSLSmmuGate.{h,c}` — a
freestanding C11 FAIL-CLOSED decision layer that answers ONE question:
*may a future kernel probe READ the enchilada GPU's DEDICATED kgsl SMMU
register window at `0x05040000`?* It performs no read itself. It has NO MMIO,
NO IOKit, NO firmware, NO kernel caller, NO device run and NO flash. gpu area
STAYS `host-verified`; this is a SAFETY STUDY, not a driver and not a new device
measurement.

**Why this question matters.** The GPU does NOT use apps_smmu @ `0x15000000`
(phandle `0x29`, `#iommu-cells=2`, SID+mask). All four GPU iommus use
`arm,smmu-kgsl@5040000` (phandle `0x1ac`, `#iommu-cells=1`, unmasked SIDs):
`gfx3d_user`=0, `gfx3d_secure`=2/1, `gmu_user`=4, `gmu_kernel`=5
(`enchilada.dts:23848-23925`, sha `e590ee3273499...`). Flash #8 (rounds 20/21)
surveyed apps_smmu ONLY; the kgsl SMMU has NEVER been read on the device. A
naive "just read it" probe is UNSAFE while the GPU power domain is collapsed.

**The load-bearing upstream rule (Linux v6.12, sha256-pinned).**
`oneplus6-ios/work/tmp/r53-lookup/manifest.json` pins
`drivers/iommu/arm/arm-smmu/arm-smmu.c` (67693 B,
`d5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a`) and
`.../arm-smmu-qcom.c` (20423 B, `75760784bbf68942a4055acdc0886c3559c4993fd49e39e0a3abc20f5f52be44`),
fetched from `https://cdn.jsdelivr.net/gh/torvalds/linux@v6.12/`.
The exact mainline ORDER is:
- `arm-smmu.c:2196 err = clk_bulk_prepare_enable(smmu->num_clks, smmu->clks);`
- `arm-smmu.c:2200 err = arm_smmu_device_cfg_probe(smmu);`
- `arm-smmu.c:2255 arm_smmu_device_reset(smmu);`
- `arm-smmu.c:2264-2266 pm_runtime_set_active(dev); pm_runtime_enable(dev);`
- `arm-smmu.c:71-77 arm_smmu_rpm_get() -> pm_runtime_resume_and_get(smmu->dev)`
- `arm-smmu.c:1973 { .compatible = "qcom,smmu-v2", .data = &qcom_smmuv2 }`
- `a6xx_gmu.c:1033 pm_runtime_get_sync(gmu->dev)` (CX genpd),
  `:1041 pm_runtime_get_sync(gmu->gxpd)` (GX genpd),
  `:1047 clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks)`,
  `:1277 msm_iommu_new(gmu->dev, 0)` — the IOMMU is created AFTER power.

Mainline ALWAYS enables the SMMU clocks AND resumes the power domain BEFORE it
touches any SMMU register. Touching a powered-down SMMU register can bus-abort.

**The board fact that makes a read unsafe TODAY.** `enchilada.dts:13967`
`vdd-supply = <0x1a4>` on the kgsl SMMU; `:595/:604` show `0x1a4` IS
`gpu_cx_gdsc` (`regulator-name = "gpu_cx_gdsc"`, `phandle = <0x1a4>`).
`:23667` (kgsl-3d0) and `:23887` (gmu) also use `vddcx-supply = <0x1a4>`.
Device logs from rounds 35/49/50/52 measured `verdict=cx-DOWN`
**1912 times** with words `cx=00005ff4 gx=00088400` (CX bit31 clear). The
kgsl SMMU therefore sits on a COLLAPSED rail. A read may bus-abort.

**Honest nuance (why GPUCC does not rescue this).**
`AdrenoDomainState.h:17-22` and `AdrenoDomainState.c:58-77` note GPUCC *status*
registers ARE readable with the domain collapsed because GPUCC rides always-on
PMIC rails, and carve GPUCC out of the forbidden-window list. That exception
does NOT transfer: the kgsl SMMU rides `gpu_cx` (`0x1a4`), a DIFFERENT power
domain. `AdrenoKGSLSmmuGateGpuccExceptionDoesNotApply()` encodes this by
asserting the kgsl base is NOT a GPUCC-window address and IS in the
collapsible-domain forbidden list (`AdrenoDomainState.c:24` already lists
`{ 0x05040000ull, 0x10000ull }`).

**The fail-closed decision (check ORDER is load-bearing).**
`AdrenoKGSLSmmuGateAssess()` refuses in this order, returning a typed verdict:
1. `RefusedInvalid` — NULL / zero / overflowing window;
2. `RefusedWrongSmmu` — caller targeted apps_smmu (`0x29`), not `0x1ac`;
3. `RefusedWindow` — window is not EXACTLY `0x05040000/0x10000`;
4. `RefusedCxDown` — device-measured `gpu_cx` collapsed (THE current state);
5. `RefusedClocksOff` — `gcc_gpu_memnoc_gfx_clk` not enabled (upstream :2196);
6. `RefusedDomainOff` — gpu_cx GDSC not resumed (upstream :2264);
7. only if ALL pass: `Permitted` (a hypothetical powered GMU, NOT today).

`AdrenoKGSLSmmuGateRequiredSteps()` returns `CLOCKS|DOMAIN` (0x3), i.e. BOTH are
mandatory before ANY register access; no step is skippable.
`AdrenoKGSLSmmuGateContextSids()` enumerates the five 1-cell unmasked SIDs
`{0,2,1,4,5}` for a future stage-1 context (NOT programmed here).

**Reproducible pins (committed, m12247).**
`scripts/data/enchilada-kgsl-smmu-inputs.json` records the two Linux v6.12
sha256 pins, the upstream ORDER line numbers, the 15 DTB line facts, the
gate's `RefusedCxDown` verdict today, and the reused `cx-DOWN` measurement.
`scripts/fetch-enchilada-kgsl-smmu-sources.py` re-fetches and verifies the exact
bytes into the gitignored `.cache/` (pin-before-derive, the same method as
`scripts/fetch-adreno-a630-sources.py`); run gives
`PASS: 2 exact Linux v6.12 SMMU source inputs, 88116 bytes; upstream ORDER pins only, no device claim`.
`work/tmp/_r53_pin.py` verified the pin file, the committed cache bytes and the
original lookup manifest all agree (`R53_PIN_VERIFY: PASS (0 failures)`).

**Three independent HOST verifications (m03546), all PASS.**
1. `scripts/check-enchilada-adreno-host.py` (full 12-suite suite, compiled):
   `adreno-kgsl-smmu-gate: 58 checks, 0 failures`,
   `PASS: enchilada kgsl SMMU read SAFETY gate (11 test groups, fail-closed)`,
   `HOST_EXIT=0`. The other 11 suites stayed green (no regression).
2. `scripts/verify-enchilada-kgsl-smmu-gate.py` (source-text cross, NEVER
   compiles/runs the C module): `KGSL_SMMU_GATE_CROSS: PASS (0 failures)` —
   reads the COMMITTED pin file + cache bytes, asserts the upstream lines/ORDER,
   the 15 DTB line checks, the module constants and Assess() check ORDER, the
   build+runner 12th-object/12th-test wiring, and the 1912 previously measured
   `cx-DOWN` rows.
3. `work/tmp/strict_compile_r53.ps1`: strict freestanding
   `--target=aarch64-apple-darwin -std=c11 -ffreestanding -fno-builtin
   -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Wconversion
   -Wsign-conversion -Wshadow -Wpedantic` → `STRICT_ALL_OK=True`
   (gate 2680 B, board-inputs 592 B, domain-state 880 B objects, all exit 0).

**Integration (CI compile authority, pending).** `scripts/build-enchilada-adreno.py`
adds the 12th archive object `AdrenoKGSLSmmuGate.c` and the 12th source stem;
`scripts/check-enchilada-adreno-host.py` adds the 12th host test. CI #229 is
the compile/link authority for the 12-object `libEnchiladaAdreno.a` and will be
recorded separately. A HOST-VERIFIED study does NOT require a device run.

**Honest boundary (load-bearing).**
- This evidence item is HOST-VERIFIED ONLY. NO flash, NO device run, NO new
  device measurement, NO SMMU register was read this round. The `cx-DOWN` fact
  is REUSED from rounds 35/49/50/52, not re-measured.
- gpu area STAYS `host-verified`. The gate is a DECISION layer, not a driver;
  `CONFIG_XNU_NATIVE_ADRENO` remains `n`.
- User m39510 gave a standing "如有需要可以随时上机" authorization. It was
  NOT exercised this round: m12247 "先核验再上机" is satisfied precisely by
  establishing, BEFORE any device action, that a powered-down read is unsafe.
- Remaining strict-order work is UNCHANGED: (a) a real XNU kernel backend that
  wires the `AdrenoPowerUpV1` genpd/clock callbacks to hardware and
  INSTANTIATES the chain on a POWERED GMU; (b) THEN a kgsl SMMU stage-1 context
  @ `0x5040000` (streams must first be made to fault — flash #8 `pre=10` =
  `ArmSmmu4KBusy`), only AFTER the gate returns Permitted; (c) TrustZone PAS
  auth (`GPU_PAS_ID 13`); (d) network/audio/cellular device verification;
  (e) controlled adversarial SMP stress test, CPU hotplug, filesystem-level
  persistence, `IOPMPowerSource`/PMI8998, ShengPreflight link.

**Verification chain (this round).** `fb28eed5199e75b9d4bfe796eb76d70debed8d55`
(round-52 p2) → this ROUND 53 record (two-phase: module+host tests+cross
verifier+evidence doc first, then `porting.toml` gpu-area record citing the
phase-1 sha). All prior round-49/50/51/52 device-verified history is retained.
