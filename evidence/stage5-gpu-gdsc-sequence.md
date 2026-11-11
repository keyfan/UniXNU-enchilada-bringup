# Stage 5 GPU — the GDSC enable/disable SEQUENCE is derived, not asserted (round 57)

Status: **HOST-VERIFIED**. No device run, no flash, no CI, no MMIO.
Area: `gpu` stays `host-verified`.

## What was still asserted after round 56

Round 56 turned the GDSC *status register and its masks* into a derivation:
`0x05091540` / BIT(31) for CX and `0x05091010` / BIT(16)+BIT(15) for GX now
follow mechanically from the pinned `gdsc.c` + `gpucc-sdm845.c`.

What it did **not** cover is the thing the kernel actually *does* when it powers
CX up: the ordered write sequence. Until this round, the statement
"CX power-up means clear the collapse bit at `0x106c`, wait, then poll `0x1540`
for BIT(31)" was prose in `AdrenoPowerUpV1.h` — not something a test could
contradict. Editing the module to poll the wrong register would still have
passed every check.

Round 57 closes that gap the same way round 56 closed the previous one: the
sequence is extracted from `gdsc_enable()` / `gdsc_disable()` and re-expressed
as an ordered, fixed-size step plan that a Python cross-check rebuilds from the
pinned bytes independently of the C code.

## The upstream rule (sha-pinned Linux v6.12)

Pinned in `scripts/data/enchilada-gpu-gdsc-inputs.json`, reproducible with
`scripts/fetch-enchilada-gpu-gdsc-sources.py`:

| file | bytes | sha256 |
| --- | --- | --- |
| `drivers/clk/qcom/gdsc.c` | 15019 | `e1210770ad3e4406…` |
| `drivers/clk/qcom/gdsc.h` | 2863 | `ba487640dc1a0acb…` |
| `drivers/clk/qcom/gpucc-sdm845.c` | 5176 | `80c2fbef5c65df98…` |

`gdsc_enable()` (`gdsc.c:259-315`) order, verbatim:

```
pwrsts == PWRSTS_ON          -> gdsc_deassert_reset(sc); return
SW_RESET                     -> assert_reset; udelay(1); deassert_reset
CLAMP_IO                     -> (AON_RESET ? assert_reset_aon : -) ; deassert_clamp_io
                             -> gdsc_toggle_logic(sc, GDSC_ON, false)
pwrsts & PWRSTS_OFF          -> gdsc_force_mem_on(sc)
                             -> udelay(1)
HW_CTRL                      -> gdsc_hwctrl(sc, true); udelay(1)
RETAIN_FF_ENABLE             -> gdsc_retain_ff_on(sc)
```

`gdsc_disable()` (`gdsc.c:317-364`):

```
PWRSTS_ON                    -> gdsc_assert_reset(sc)
HW_CTRL                      -> gdsc_hwctrl(sc, false); udelay(1); gdsc_poll_status(GDSC_ON)
pwrsts & PWRSTS_OFF          -> gdsc_clear_mem_on(sc)
PWRSTS_RET_ON                -> return 0
                             -> gdsc_toggle_logic(sc, GDSC_OFF, domain->synced_poweroff)
CLAMP_IO                     -> gdsc_assert_clamp_io(sc)
```

And the short-cut that explains the whole CX/GX asymmetry
(`gdsc.c:153-161`, inside `gdsc_toggle_logic()`):

```c
if ((sc->flags & VOTABLE) && status == GDSC_OFF && !wait) {
        udelay(TIMEOUT_US);   /* TIMEOUT_US == 500 */
        return 0;             /* no polling */
}
```

## The two SDM845 descriptors, and what each yields

From `gpucc-sdm845.c:119-139` (unchanged since round 56, re-read for this round):

* `gpu_cx_gdsc` — `gdscr 0x106c`, `gds_hw_ctrl 0x1540`, `clk_dis_wait_val 0x8`,
  `pwrsts PWRSTS_OFF_ON`, `flags VOTABLE` **only**. No `.supply`, no `.cxcs`,
  no `.clamp_io_ctrl`.
* `gpu_gx_gdsc` — `gdscr 0x100c`, `clamp_io_ctrl 0x1508`,
  `.pd.power_on = gdsc_gx_do_nothing_enable`, `pwrsts PWRSTS_OFF_ON`,
  `flags CLAMP_IO | AON_RESET | POLL_CFG_GDSCR`. No `.supply`, no `.cxcs`.

Feeding those flags through the two functions gives, mechanically:

**CX enable** — `UPDATE_COLLAPSE(0x106c, value 0)` → `UDELAY(1)` (because
`gds_hw_ctrl` is set) → `POLL_ON(0x1540, BIT(31))` → `UDELAY(1)`. Two register
writes. No clamp step (no `CLAMP_IO`), no AON reset (no `AON_RESET`), no
hardware-trigger step (no `HW_CTRL`), and no retention write (no `.cxcs`, so
`gdsc_force_mem_on()` has nothing to iterate over — the module says "not
modelled" rather than pretending to do a zero-byte write).

**CX disable** — `UPDATE_COLLAPSE(0x106c, value 1)` → `UDELAY(500)` → **return
without polling**. This is the VOTABLE short-cut, and it is why CX can be
disabled safely even though its status register is the hardware-controlled
`0x1540`: upstream never waits for it.

**GX enable, generic path** — `ASSERT_RESET_AON(0x1508, BIT(4))` →
`DEASSERT_CLAMP_IO(0x1508, BIT(0)=0)` → `UPDATE_COLLAPSE(0x100c, value 0)` →
`POLL_ON(0x1010, BIT(16))`.

**GX enable, CPU path** — **zero steps, zero register writes.**

That last line is the round's load-bearing finding, so state it plainly:
`gpu_gx_gdsc.pd.power_on` is `gdsc_gx_do_nothing_enable()`, and GX has neither
a `.supply` nor a `.cxcs`, so the function body reduces to nothing at all. The
CPU's attempt to power GX on cannot write a single GPUCC register. This is not
an observation, an inference, or a claim about the hardware — it is the
composition of the pinned source with the pinned descriptor, and
`adreno_gdsc_verify_gx_enable_writes_nothing()` fails if either input changes.

It also explains, without invoking anything unmeasured, the device words
already on record: `gx=0x00088400` sits permanently in the down-complete state
across rounds 35/49/50/52 because **nothing on the CPU side ever turns GX on**.
Powering GX is the GMU's job. The GX disable path is real and does write
(`POLL_OFF` on BIT(15), then `ASSERT_CLAMP_IO` afterwards) precisely because
the CPU must be able to force GX down if the GMU wedges — which is the stated
rationale at `gdsc.c:585-603`.

## The board DTB says the same thing independently

`notes/mu-silicium/enchilada.dts` (sha256 `e590ee32…`, 784857 B):

* `qcom,gdsc@0x509106c` (`gpu_cx_gdsc`) carries `qcom,no-status-check-on-disable`
  and `qcom,clk-dis-wait-val = <0x08>` — the vendor's own spelling of
  "VOTABLE, do not poll on the way down", and the same `clk_dis_wait_val 0x8`
  that is in the descriptor.
* `qcom,gdsc@0x509100c` (`gpu_gx_gdsc`) carries `qcom,poll-cfg-gdscr` and
  `qcom,reset-aon-logic` — `POLL_CFG_GDSCR` and `AON_RESET`, the two flags the
  enable plan above consumes.

So the three independent artifacts — upstream `gdsc.c`, the `gpucc-sdm845.c`
descriptors, and the shipped DTB — agree on which of the two domains polls and
which does not. The cross verifier asserts all three independently rather than
letting one of them vouch for the others.

## Deliverables

* `drivers/NativeAdreno/AdrenoGdscDecode.{h,c}` extended with
  `struct adreno_gdsc_step`, `struct adreno_gdsc_plan` (fixed
  `ADRENO_GDSC_MAX_STEPS` array, no allocation), `adreno_gdsc_plan_enable()`,
  `adreno_gdsc_plan_disable()`, `adreno_gdsc_plan_cpu_enable()`,
  `adreno_gdsc_plan_has()`, `adreno_gdsc_plan_index_of()` (returns `-1` when a
  step is absent, so "absent" can never be confused with "step 0"), and
  `adreno_gdsc_verify_gx_enable_writes_nothing()`.
* `drivers/NativeAdreno/tests/adreno-gdsc-decode-test.cpp` — four new groups
  (`testCxEnablePlan`, `testGxEnableIsNoop`, `testCxDisableDoesNotPoll`,
  `testPlanHelpers`); 61 → **108 checks**.
* `scripts/verify-enchilada-gdsc-decode.py` — three new sections that re-derive
  the sequence in Python from the pinned bytes and only then compare with the C
  module; 69 → **98 checks**.
* `scripts/data/enchilada-gpu-gdsc-inputs.json` — `upstream_rule.gdsc_c` gains
  the enable/disable/helper line numbers, and a new
  `round57_sequence_derivation` block records both plans and the zero-write
  property.
* Nothing was added to the archive: round 57 extends the existing 13th object
  and the existing 13th host test, so the CI object count stays 13.

## Three verifications (all host-side)

1. **Host test** — `PASS: enchilada GPU GDSC derivation host tests (108 checks)`.
   Built by compiling the two `.c` with `clang -std=c11` and linking with
   `clang++ -std=c++17`; asking `clang++` to compile a `.c` directly fails with
   `treating 'c' input as 'c++' when in C++ mode`.
2. **Full 13-test suite** — `check-enchilada-adreno-host.py` exits 0, with
   `gmu-caller-v1: 50`, `adreno-power-up-v1: 84`, `adreno-kgsl-smmu-gate: 58`,
   `gmu-boot-v1: 98` and the new `gdsc-decode: 108` all at 0 failures.
3. **Cross verifier** — `GDSC_DECODE_CROSS: PASS (0 failures)`, 98 checks.
   Plus a strict freestanding aarch64 compile of `AdrenoGdscDecode.c`
   (`--target=aarch64-apple-darwin -ffreestanding -fno-builtin
   -mgeneral-regs-only -O2 -Wconversion -Wsign-conversion -Wshadow -Wpedantic
   -Werror`), exit 0, 2976 B object, 15 exported `_adreno_gdsc_*` symbols.

One defect was caught by verification 3 while writing it: the check
`"ADRENO_GDSC_STEP_UDELAY" in ctxt and "500u" in ctxt` failed because the C
module names the constant `ADRENO_GDSC_TIMEOUT_US` instead of inlining `500u`.
The C was right and the checker was wrong; the check now asserts the named
constant in `.c` and `500u` at its `#define` in `.h`. That is the kind of thing
a second, independently written verification is for.

## Honest boundary

Everything above is derived from source text and a board DTB. **No register was
read, no register was written, no firmware was loaded, no kernel caller exists,
no CI run was made, and the device was not touched.** The previously measured
device words (`cx=0x00005ff4`, `gx=0x00088400`) are *reused* here as a
consistency check, not re-measured.

The `gpu` area therefore **stays `host-verified`**. What is now mechanically
guaranteed is that the sequence this project *would* execute is the sequence
upstream specifies — not that executing it on enchilada succeeds. Bringing CX
up for real, then the GMU, then a kgsl SMMU stage-1 context, remains open, and
`CONFIG_XNU_NATIVE_ADRENO` stays `n`.
