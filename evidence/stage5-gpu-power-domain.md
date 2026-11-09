# Stage 5 GPU — power-domain preconditions for ANY GPU/mmio read, HOST-VERIFIED

**Status: HOST-VERIFIED only.** A device tree and an upstream driver source are
*descriptions of how firmware is wired*, not a device measurement. This record is
derived from the locally SHA-pinned vendored DTB and from SHA-pinned upstream
Linux v6.12 sources. **No CI compile, no flash, no device register read, no
regulator or GDSC brought up, no clock enabled, no firmware load, no GMU/HFI
start.** It does not make stage 5 `device-verified`.

Round 22 / 2026-11-09. Goal: continue the strict unixnu 8-stage driver order on
OnePlus 6 (enchilada, SDM845), GPU mainline, under the hard constraints
「先核验再上机」and「每做一步之前进行两次验证」: verify first, only then touch
hardware. The device was **disconnected** this round (no COM port), so the only
honest deliverable is a host-side correction — never a device claim.

---

## What this corrects (load-bearing)

The Round-21 record, `porting.toml:320`, ended its next-step instruction with:

> "… Then read a safe register (RBBM_STATUS 0x210 / CP_ALWAYS_ON_COUNTER 0x980)
> on device - each step with CI + two independent device verifications."

**That instruction is unsafe as literally written, and is hereby superseded.**
`RBBM_STATUS` is read as the *first* thing after power-on in upstream
(`a6xx_gpu.c:29` inside `a6xx_idle()`, `:1220` inside `a6xx_hw_init()`), but
only *after* the whole GMU cold-start sequence has run. On this board the GPU,
the GMU and the dedicated kgsl SMMU all live in a **collapsible** power domain:
the `gpu_cx` / `gpu_gx` GDSCs are real switchable regulators, not always-on
infrastructure. A raw CPU bus read of `0x5000000`/`0x506a000`/`0x5040000`
while those rails are collapsed is a **hang risk** (the interconnect can stall
waiting for a clock that is gated), not a "safe probe".

This is a different situation from the earlier, legitimate read-only survey of
`apps_smmu @ 0x15000000` (flash #8): that node carries `qcom,skip-init` and has
**no** supply and **no** clocks in the DTB, i.e. firmware leaves it up, which is
precisely why reading it cold was safe.

### Evidence for the claim, verbatim from the pinned DTB

`qcom,kgsl-3d0@5000000` (`notes/mu-silicium/enchilada.dts:23632-23671`):

```
:23646 			qcom,no-nap;
:23656 			clocks = <0x1c 0x03 0xa7 0x0a 0x22 0x1a 0x22 0x29 0xa7 0x04 0x87 0x10>;
:23657 			clock-names = "core_clk\0rbbmtimer_clk\0mem_clk\0mem_iface_clk\0gmu_clk\0l3_vote";
:23666 			regulator-names = "vddcx\0vdd";
:23667 			vddcx-supply = <0x1a4>;
:23668 			vdd-supply = <0x2a9>;
```

`arm,smmu-kgsl@5040000` (`:13957-13973`) — note it shares the GPU's `gpu_cx`
rail and has its own memnoc clock:

```
:13967 			vdd-supply = <0x1a4>;
:13969 			clock-names = "gcc_gpu_memnoc_gfx_clk";
:13970 			clocks = <0x22 0x29>;
```

The two GDSCs those phandles resolve to (`:593-621`):

```
:595 			regulator-name = "gpu_cx_gdsc";
:604 			phandle = <0x1a4>;
:609 			regulator-name = "gpu_gx_gdsc";
:620 			phandle = <0x2a9>;
```

Contrast `apps-smmu@0x15000000` (`:13975-13996`), which has **neither**:

```
:13980 			qcom,skip-init;
```

So `vddcx-supply -> 0x1a4` (GPU) **and** `vdd-supply -> 0x1a4` (kgsl SMMU) are
the *same rail*, and `vdd-supply -> 0x2a9` is the second one. Both are
`qcom,gdsc` switchable-power-collapse controllers under
`/soc/qcom,gdsc@0x509106c` and `/soc/qcom,gdsc@0x509100c`
(aliases `:25405-25406`).

### Evidence that upstream orders the same way

SHA-pinned `a6xx_gmu.c` (`c14fbb25…`, see `scripts/data/adreno-a630-inputs.json`):

| line | code | meaning |
|---|---|---|
| `:1737-1738` | `/* HFI v1, has sptprac */ gmu->legacy = true;` | A630 is legacy GMU |
| `:1622` | `gmu->cxpd = dev_pm_domain_attach_by_name(gmu->dev, "cx");` | CX genpd handle |
| `:1638` | `gmu->gxpd = dev_pm_domain_attach_by_name(gmu->dev, "gx");` | GX genpd handle |
| `:1033` | `pm_runtime_get_sync(gmu->dev);` | CX rail ON first |
| `:1041` | `pm_runtime_get_sync(gmu->gxpd);` | GX rail ON |
| `:1047` | `clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks);` | 6 clocks ON |
| `:811` | `ret = a6xx_rpmh_start(gmu);` | RPMh/PDC |
| `:815` | `ret = a6xx_gmu_fw_load(gmu);` | GMU firmware (signed ZAP chain) |
| `:877` | `ret = a6xx_gmu_start(gmu);` | GMU boot |
| `:882` | `ret = a6xx_gmu_gfx_rail_on(gmu);` | legacy-only GX rail |
| `:889` | `ret = a6xx_sptprac_enable(gmu);` | `0x778000` (`:373`) |
| `:894` | `ret = a6xx_gmu_hfi_start(gmu);` | HFI v1 |

and `a6xx_gpu.c` (`c3347937…`): `:1291 pm_runtime_get_sync(&gpu->pdev->dev)` →
`:1296 msm_gpu_hw_init(gpu)` → `:1204 a6xx_hw_init()` → first RBBM writes.
**Every** GPU register access is downstream of that sequence. `0x210`
(`REG_A6XX_RBBM_STATUS`) is not an exception and is not "safe" when cold.

---

## What was built (pure host, no MMIO)

| file | role |
|---|---|
| `drivers/NativeAdreno/AdrenoPowerGate.h` | board constants + `enum adreno_power_step` (9 ordered steps) + `struct adreno_power_state` (7 preconditions) + 6 declarations |
| `drivers/NativeAdreno/AdrenoPowerGate.c` | `adreno_power_mmio_permitted()` requires **all seven** flags; `adreno_power_current_state()` is all-zero (today: no backend); `adreno_power_gpu_windows_are_gated()`, `adreno_power_apps_smmu_probe_permitted()`, `adreno_power_is_legacy_gmu()`, `adreno_power_steps_remaining()` |
| `drivers/NativeAdreno/tests/adreno-power-gate-test.cpp` | 5 host test groups, incl. the load-bearing `adreno_power_mmio_permitted(&cold) == 0` |
| `scripts/data/enchilada-gpu-power.json` | SHA-pinned DTB + node slices + facts + upstream required markers |
| `scripts/extract-enchilada-gpu-power.py` | Verification 1 (extract from the pinned DTB) |
| `scripts/verify-enchilada-gpu-power.py` | Verification 2 (cross-domain: DTB ↔ upstream source ↔ header) |

The gate is deliberately conservative and **cannot be satisfied today**:
`CONFIG_XNU_NATIVE_ADRENO=n` and `adreno_power_current_state()` returns all
zeroes, so `adreno_power_mmio_permitted()` returns 0. Its purpose is to make the
preconditions *checkable* rather than a comment someone skips at 3 a.m.

### The 9 ordered steps

1. `kAdrenoPowerStepCxGenpdOn` — `gpu_cx_gdsc` (`0x509106c`) on, i.e.
   `pm_runtime_get_sync(gmu->dev)`
2. `kAdrenoPowerStepGxGenpdOn` — `gpu_gx_gdsc` (`0x509100c`) on,
   `pm_runtime_get_sync(gmu->gxpd)`
3. `kAdrenoPowerStepClocksEnable` — 6 GPU clocks + `gcc_gpu_memnoc_gfx_clk`
4. `kAdrenoPowerStepRpmhStart` — `a6xx_rpmh_start` (PDC `0x30080`)
5. `kAdrenoPowerStepGmuFwLoad` — `a6xx_gmu_fw_load` (signed ZAP chain)
6. `kAdrenoPowerStepGmuStart` — `a6xx_gmu_start` + `a6xx_gmu_gfx_rail_on`
7. `kAdrenoPowerStepSptPracEnable` — `a6xx_sptprac_enable` (`0x778000`)
8. `kAdrenoPowerStepHfiStart` — `a6xx_gmu_hfi_start` (HFI v1)
9. `kAdrenoPowerStepHwInit` — `msm_gpu_hw_init` → `a6xx_hw_init` → first RBBM
   writes. **Only now** is `RBBM_STATUS 0x210` a legal read.

---

## Verification (two independent methods)

**Method 1 — extract (DTB is the only domain):**

```
python -X utf8 scripts/extract-enchilada-gpu-power.py
ENCHILADA_GPU_POWER_EXTRACT: PASS (23 checks, DTB sha e590ee327349...)
```

Re-pins the full DTB SHA-256 across all 4 byte-identical copies, re-parses the
GDSC names/regs/phandles, `vddcx-supply -> 0x1a4`, `vdd-supply -> 0x2a9`, the
6 clock names, `qcom,no-nap`, the kgsl SMMU sharing `0x1a4` plus its memnoc
clock, and the `apps_smmu` contrast (`qcom,skip-init`, no supply, no clocks,
`#iommu-cells = 2`), plus provenance: the `bl_ctrl_dcs` node at `:2661` — the
stage-3 **device-verified** backlight fact — is in this same DTB.

**Method 2 — cross-domain (three independent domains must agree):**

```
python -X utf8 scripts/verify-enchilada-gpu-power.py
ENCHILADA_GPU_POWER_CROSS: PASS (45 checks, three independent domains agree)
```

1. DTB slices re-parsed by pinned line numbers: `gpu_vddcx == cx_phandle ==
   kgsl_smmu_vdd == [0x1a4]` and `gpu_vdd == gx_phandle == [0x2a9]` — the shared
   rail is proven from two different nodes, not asserted.
2. Every `required_markers` string in `a6xx_gmu.c` / `a6xx_gpu.c` verified
   present in the SHA-pinned cached files, with the file SHA-256 re-checked
   against `scripts/data/adreno-a630-inputs.json`.
3. The `AdrenoPowerGate.h` constants parsed back out and compared against the
   DTB and against the manifest; the gate body asserted to test all seven flags;
   and the module asserted to contain **no** `armcpu_map_physical` / `mmio_read`
   call anywhere — the honest "no MMIO, no device" boundary is machine-checked,
   not just promised.

**Host runner (extended):**

```
python -X utf8 scripts/check-enchilada-adreno-host.py --cc clang.exe --cxx clang++.exe --out work/tmp/adreno-host-out
PASS: enchilada A630 GPU power-domain safety gate host tests (5 groups)
PASS: enchilada A630 catalog/hwinit/board-input/power-gate host tests (without sanitizers)
```

Runner now covers 10 catalog + 3 hwinit + 5 board-input + 5 power-gate = 23
groups, compiled with the same strict flags (warnings are errors).

---

## Honest boundaries

- **No device touched.** No COM port existed this round. No regulator, GDSC,
  clock, GMU or register was accessed, and none is claimed to be.
- **The DTB is a description, not a measurement.** It proves the *intent* of the
  firmware description — that these rails are switchable and shared. It does not
  prove the voltage values, the GDSC switch latency, or that the sequence above
  completes on real silicon.
- **`adreno_power_current_state()` is all-zero on purpose.** It will stay that
  way until an actual backend exists. The gate therefore fails closed.
- **This does not unblock the Metal path.** The blockers recorded in Round 21
  stand unchanged: (a) signed ZAP `.mdt`/`.b00`/`.b01` chain + TrustZone PAS auth
  (`GPU_PAS_ID 13`) are vendor-only; (b) a new A6XX_GEN1 backend is required
  (legacy HFI v1, SPTPRAC, RPMh/PDC, A6XX CP/IRQ/ring). What this round adds is
  a *third* prerequisite that was previously implicit: the power-up sequence
  must come first, and it is itself unimplemented.
- **A device read remains the only way to promote this.** The correct next device
  step is not "read `RBBM_STATUS`" but "bring up the rails and clocks, prove the
  GDSCs actually collapse and restore, and *then* read `RBBM_STATUS`" — and that
  requires (a)+(b) anyway.
