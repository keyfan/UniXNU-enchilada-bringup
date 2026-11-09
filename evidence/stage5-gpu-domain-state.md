# Stage 5 GPU — domain-state safe-read design (round 28)

**Status: HOST-VERIFIED only. No register has been read on this device.**

Round 22 established a *prohibition*: the GPU (`0x5000000`), GMU (`0x506a000`) and
dedicated kgsl SMMU (`0x5040000`) all live inside the collapsible `gpu_cx` /
`gpu_gx` power domain, so a cold bus read can hang the machine. That is honest
but not testable — it produces no measurement.

This round asks the narrower question: **is there any GPU-adjacent register that
can be read with the domain collapsed, to learn whether the domain is up?**
Answer: yes, the GDSC status registers inside the GPUCC register file.

## 1. Why such a register must exist

The mainline GDSC framework polls the GDSC status register *while* a domain
transitions out of or into collapse — `drivers/clk/qcom/gdsc.c:103-117`
(`gdsc_poll_status()`, `STATUS_POLL_TIMEOUT_US = 1500`). A block cannot poll a
register that dies with the thing it is switching, so the GDSC status register is
readable with its own domain collapsed. It lives in the GPUCC register file, and
the board DTB puts GPUCC on always-on PMIC rails, not on the GDSC it controls.

## 2. Where the registers actually are — and the correction

The DTB's `gpu_cx_gdsc` node advertises `reg = <0x509106c 0x04>`, which reads like
"the CX status register is `0x0509106c`". It is not. That is the **control**
register. `gdsc.c:66-71` chooses the status register like this:

```c
if (sc->flags & POLL_CFG_GDSCR)  reg = sc->gdscr + CFG_GDSCR_OFFSET;  /* +0x4 */
else if (sc->gds_hw_ctrl)        reg = sc->gds_hw_ctrl;
else                             reg = sc->gdscr;
```

and `gpucc-sdm845.c:119-139` gives the two GDSCs different flags:

| | `gpu_cx_gdsc` | `gpu_gx_gdsc` |
|---|---|---|
| `.gdscr` | `0x106c` | `0x100c` |
| extra | `.gds_hw_ctrl = 0x1540` | `.clamp_io_ctrl = 0x1508` |
| flags | `VOTABLE` | `CLAMP_IO \| AON_RESET \| POLL_CFG_GDSCR` |
| `.power_on` | (default) | `gdsc_gx_do_nothing_enable` |
| **status register** | **`0x05091540`** | **`0x05091010`** |
| **on test** | `val & BIT(31)` | `val & BIT(16)` |
| off test | — | `val & BIT(15)` |

So the CX status register is `GPUCC + 0x1540`, **not** the DTB's `0x509106c`. The
DTB's `hw-ctrl-addr = <0x1a>` on that node is a *vendor register index into a
table*, not a byte address; treating it as an address would be a bug.

## 3. Board provenance

`notes/mu-silicium/enchilada.dts`, sha256
`e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275`, 784857 bytes:

| line | content |
|---|---|
| `:6223` | `qcom,gpucc@5090000 {` |
| `:6225` | `reg = <0x5090000 0x9000>;` |
| `:6227` | `vdd_cx-supply = <0x1b>;` → `pm8998_s9_level` (`:10864`) |
| `:6228` | `vdd_mx-supply = <0x8d>;` → `pm8998_s6_level` (`:10817`) |
| `:6235` | `qcom,gfxcc@5090000 {` |
| `:6239` | `vdd_gfx-supply = <0x1d>;` → `pm8005_s1_level` (`:11505`) |
| `:593/:596` | `qcom,gdsc@0x509106c` / `reg = <0x509106c 0x04>;` |
| `:607/:610` | `qcom,gdsc@0x509100c` / `reg = <0x509100c 0x04>;` |
| `:611` | `qcom,poll-cfg-gdscr;` (GX only) |

## 4. A subtlety the tests caught

The DTB's GMU node declares `reg = <0x506a000 0x30000>`, i.e.
`0x0506a000..0x0509a000`, which **numerically swallows the GPUCC window**
`0x05090000..0x05099000`. A naive "does this read overlap a forbidden window"
check therefore rejects the very registers that are safe. They are physically
distinct blocks; the GMU node simply over-declares its size. The module encodes an
explicit GPUCC carve-out, and `testGpuccCarveOut` fails if it is removed.

## 5. Deliverables

| path | purpose |
|---|---|
| `drivers/NativeAdreno/AdrenoDomainState.h` | register design + pure predicates (147 lines) |
| `drivers/NativeAdreno/AdrenoDomainState.c` | predicate implementation + forbidden-window table |
| `drivers/NativeAdreno/tests/adreno-domain-state-test.cpp` | 56 host checks, 7 groups |
| `scripts/data/enchilada-gpu-domain-state.json` | pinned facts + honest limits |
| `scripts/extract-enchilada-gpu-domain-state.py` | verification 1 (EXTRACT, 73 checks) |
| `scripts/verify-enchilada-gpu-domain-state.py` | verification 2 (CROSS, 117 checks) |

Pinned upstream inputs (all sha256-verified against `adreno-a630-inputs.json` or
this manifest): `drivers/clk/qcom/gpucc-sdm845.c` 5176 B
`80c2fbef5c65df98…`, `drivers/clk/qcom/gdsc.c` 15019 B `e1210770ad3e4406…`,
`drivers/clk/qcom/gdsc.h` 2863 B `ba487640dc1a0acb…`.

## 6. Verification

```
python -X utf8 scripts/check-enchilada-adreno-host.py \
    --cc tools/llvm-mingw/bin/clang.exe --cxx tools/llvm-mingw/bin/clang++.exe
  -> PASS: enchilada GPU domain-state probe design host tests (56 checks)
  -> PASS: enchilada A630 catalog/hwinit/board-input/power-gate/domain-state host tests

python -X utf8 scripts/extract-enchilada-gpu-domain-state.py
  -> ENCHILADA_GPU_DOMAIN_STATE_EXTRACT: PASS (73 checks, DTB sha e590ee327349...)

python -X utf8 scripts/verify-enchilada-gpu-domain-state.py
  -> ENCHILADA_GPU_DOMAIN_STATE_CROSS: PASS (117 checks,
     DTB <-> C header <-> upstream gdsc source agree)
```

Strict compile, C11 and C++17, `-Wconversion -Wsign-conversion -Wshadow
-Wpedantic -Wundef`: 0 warnings.

## 7. Honest boundaries

- **Nothing here is measured.** Every value is derived from the vendor DTB plus
  sha-pinned Linux v6.12 source. A DTB describes intent; it is not silicon.
- **GX cannot be powered up in software.** `power_on = gdsc_gx_do_nothing_enable`
  means a GX-OFF reading is the *expected* steady state, not evidence of a fault.
  Only a GX-ON reading would be informative.
- **Whether XNU early boot leaves the GPUCC PMIC rails voted on is unknown.** A
  real probe must be gated behind an explicit boot-arg and a watchdog; a hang
  there would be unrecoverable.
- This design does **not** make stage 5 device-verified. It makes the *next*
  device step structured and bounded instead of a blind cold read.
