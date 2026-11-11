# Stage 5 GPU (round 56): the GDSC status register is DERIVED, not asserted

Date: 2026-11-11. Round 56. Board: OnePlus 6 (enchilada), SDM845, Adreno A630.

## Status

**HOST-VERIFIED.** No device run, no flash, no MMIO. The gpu area STAYS
`host-verified`. `CONFIG_XNU_NATIVE_ADRENO` remains `n`.

## What was wrong before this round

`drivers/NativeAdreno/AdrenoDomainState.h` has carried five constants since
round 28:

| constant | value |
| --- | --- |
| `ADRENO_DOMAIN_CX_STATUS` | `0x05091540` |
| `ADRENO_DOMAIN_GX_STATUS` | `0x05091010` |
| `ADRENO_DOMAIN_CX_ON_BIT` | `31` |
| `ADRENO_DOMAIN_GX_ON_BIT` | `16` |
| `ADRENO_DOMAIN_GX_OFF_BIT` | `15` |

They were correct, and a comment explained where they came from — but they were
**asserted**. Nothing mechanical connected them to the upstream source. If a
careful edit moved `ADRENO_DOMAIN_CX_STATUS`, the existing test suite would
still have passed, because the test re-asserted the same literals.

That is exactly the failure mode `m03546` ("verify twice before every step")
exists to prevent: two verifications of the same assertion are worth less than
one verification of the derivation.

## What upstream actually says

All three sources are now SHA-pinned in
`scripts/data/enchilada-gpu-gdsc-inputs.json` and reproduced by
`scripts/fetch-enchilada-gpu-gdsc-sources.py`:

| file | bytes | sha256 |
| --- | --- | --- |
| `drivers/clk/qcom/gdsc.c` | 15019 | `e1210770ad3e4406adaca4eb0d6d103823d73a65068cf1ff48183bb1a347d32d` |
| `drivers/clk/qcom/gdsc.h` | 2863 | `ba487640dc1a0acbecb3605417b2bd34418c6914d24c64a54a145565d1a15f86` |
| `drivers/clk/qcom/gpucc-sdm845.c` | 5176 | `80c2fbef5c65df986f4c391bbeedbf53e8744e5dd326f0e05dc6b4021f542a54` |

The status register is a **function of the descriptor**, not a constant.
`gdsc.c:66-71`:

```c
if (sc->flags & POLL_CFG_GDSCR)
        reg = sc->gdscr + CFG_GDSCR_OFFSET;
else if (sc->gds_hw_ctrl)
        reg = sc->gds_hw_ctrl;
else
        reg = sc->gdscr;
```

and the masks follow the same branch, `gdsc.c:77-91`: with `POLL_CFG_GDSCR` the
up/down bits are `GDSC_POWER_UP_COMPLETE` `BIT(16)` and
`GDSC_POWER_DOWN_COMPLETE` `BIT(15)`; without it, "up" is `PWR_ON_MASK`
`BIT(31)` and "down" is simply the inverse of that bit — there is no dedicated
down bit at all.

`gpucc-sdm845.c:119-139` supplies the two SDM845 descriptors:

```c
static struct gdsc gpu_cx_gdsc = {
        .gdscr = 0x106c,
        .gds_hw_ctrl = 0x1540,
        .clk_dis_wait_val = 0x8,
        .pwrsts = PWRSTS_OFF_ON,
        .flags = VOTABLE,
};

static struct gdsc gpu_gx_gdsc = {
        .gdscr = 0x100c,
        .clamp_io_ctrl = 0x1508,
        .pd = { .name = "gpu_gx_gdsc",
                .power_on = gdsc_gx_do_nothing_enable },
        .pwrsts = PWRSTS_OFF_ON,
        .flags = CLAMP_IO | AON_RESET | POLL_CFG_GDSCR,
};
```

## The derivation

**CX** — `VOTABLE` only, so no `POLL_CFG_GDSCR`; `gds_hw_ctrl = 0x1540` is
non-zero and wins over `gdscr`:

- status = `0x05090000 + 0x1540` = **`0x05091540`** ✓
- on mask = `BIT(31)` ✓
- off mask = none (inverse of the on bit) ✓

**GX** — `POLL_CFG_GDSCR` set, so `gdscr + 0x4`:

- status = `0x05090000 + 0x100c + 0x4` = **`0x05091010`** ✓
- on mask = `BIT(16)` ✓
- off mask = `BIT(15)` ✓

All five constants follow. The DTB agrees independently: it declares
`regulator-name = "gpu_cx_gdsc"` at `qcom,gdsc@0x509106c`,
`regulator-name = "gpu_gx_gdsc"` at `qcom,gdsc@0x509100c`, and — the part that
matches the flag analysis — the GX node itself carries the
`qcom,poll-cfg-gdscr` property, while the CX node does not.

## The second finding: why `gx_off=1` is permanently correct

`gdsc_gx_do_nothing_enable()` (`gdsc.c:604`) is the GX power-on callback: it
enables the parent supply and then, to quote the upstream rationale at
`gdsc.c:596-602`, does nothing with the GDSC itself so that during power up
nothing happens from the CPU and the GMU powers GX up normally.

So the device words measured in rounds 35/49/50/52 —

- CX `0x00005ff4` → `BIT(31)` clear → **CX down**
- GX `0x00088400` → `BIT(16)` clear, `BIT(15)` set → **GX down-complete**

— are not a fault and not a bug in our decode. GX is down *because the CPU
never powers it up*; that is the GMU's job. This retires a standing worry in
the record: `gx_off=1` is the expected steady state.

## Deliverables

- `drivers/NativeAdreno/AdrenoGdscDecode.h` / `.c` — freestanding pure-C
  derivation: `adreno_gdsc_cx_descriptor()`, `adreno_gdsc_gx_descriptor()`,
  `adreno_gdsc_status_offset()`, `adreno_gdsc_derive()`,
  `adreno_gdsc_polls_cfg_gdscr()`, `adreno_gdsc_power_on_is_noop()`,
  `adreno_gdsc_is_on()`, `adreno_gdsc_is_off()`, and
  `adreno_gdsc_verify_domain_state_constants()` — the mechanical link that
  returns 1 only when both descriptors re-derive the five header constants.
- `drivers/NativeAdreno/tests/adreno-gdsc-decode-test.cpp` — 6 groups.
- `scripts/verify-enchilada-gdsc-decode.py` — cross verifier that re-derives
  the answer in Python from the pinned bytes, independently of the C module.
- `scripts/data/enchilada-gpu-gdsc-inputs.json` —
  `scripts/fetch-enchilada-gpu-gdsc-sources.py` — the pin set.
- Wiring: 13th archive object in `scripts/build-enchilada-adreno.py`, 13th
  host test in `scripts/check-enchilada-adreno-host.py`.

## Verifications

1. **Host suite** — 13 test groups, all green; the new group reports
   `enchilada GPU GDSC derivation host tests (61 checks)` with 0 failures.
2. **Cross verifier** — `GDSC_DECODE_CROSS: PASS (0 failures)`, 69 checks,
   re-deriving from the pinned bytes rather than from the module.
3. **Strict freestanding compile** — `--target=aarch64-apple-darwin -std=c11
   -ffreestanding -fno-builtin -mgeneral-regs-only -O2 -Wall -Wextra -Werror
   -Wconversion -Wsign-conversion -Wshadow -Wpedantic` → exit 0, 1040-byte
   `cf fa ed fe 0c 00 00 01` arm64 Mach-O, nine `_adreno_gdsc_*` symbols
   exported.

Two real defects were caught by those verifications before anything was
recorded, and both were mine:

- a nested `/* ... */` inside the header's block comment (`-Werror,-Wcomment`);
- taking the address of a temporary in the test (`&adreno_gdsc_cx_descriptor()`),
  which the existing suite never exercised because it had no accessor-returning
  API before this round.

## Honest boundary

This is a **derivation**, not a measurement. It proves that our constants match
what Linux v6.12 computes, and that the CPU cannot power GX up by design. It
does **not**:

- prove the registers are readable on this device (round 53 showed the SMMU on
  the same `gpu_cx` rail is not safe to touch while CX is down; GPUCC status is a
  narrow exception, already exercised in round 35, but this round adds no new
  device evidence);
- power anything up — CX and GX both remain down;
- unblock stage 5. The remaining blockers are unchanged: a real XNU genpd/clock
  backend, TrustZone PAS authentication (`GPU_PAS_ID 13`), the kgsl SMMU
  stage-1 context (only after CX power-up), and the vendor-signed ZAP chain.

No CI run and no flash were needed for this round's claim, and none was claimed.
