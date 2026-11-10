# Stage 5 (GPU) — the A630 modules become a CI-built, force-loaded archive

**Status: HOST-VERIFIED + CI WIRED (2026-11-10, round 29). No flash, no device read.
The four `drivers/NativeAdreno/*.c` modules are now a `-force_load`'ed kernel archive,
`libEnchiladaAdreno.a`, built by kernel CI exactly like the other six enchilada archives.**

## The gap this closes

Rounds 20–28 produced four freestanding C modules plus a generated register header:

| file | bytes | what it encodes |
|---|---|---|
| `drivers/NativeAdreno/AdrenoCatalog.c` | 3703 | A630 vs A740 identity, firmware set, `adreno_catalog_assess()` |
| `drivers/NativeAdreno/AdrenoBoardInputs.c` | 3336 | dedicated kgsl SMMU 0x5040000, single-cell SIDs 0/2,1/4/5 |
| `drivers/NativeAdreno/AdrenoPowerGate.c` | 2679 | the 9-step power-up order, fail-closed `adreno_power_mmio_permitted()` |
| `drivers/NativeAdreno/AdrenoDomainState.c` | 3515 | GPUCC GDSC status registers, forbidden-window predicate + carve-out |
| `drivers/NativeAdreno/NativeAdrenoA630HwInit.h` | 9055 | 50 offsets, 105 hwcg entries, 32 protect writes |

Every one of them was verified **only** by `scripts/check-enchilada-adreno-host.py`,
i.e. by the host's `clang` against host stubs. The kernel CI never compiled any of them:
`configs/enchilada_defconfig:36` force-loaded exactly six archives and
`CONFIG_XNU_NATIVE_ADRENO=n`, while `board.toml [kernel] archives` had no
`build-enchilada-adreno*.py` entry.

That is a real risk, not a bookkeeping detail. A header that the kernel's clang rejects,
an object that does not link, or a shim that references a symbol the kernel does not
export would all have surfaced **only after a flash**, on a device that was already
costing a boot cycle per attempt. The CI is the only ABI authority here (no macOS/Xcode
locally, and the cached XNU tree is not compilable).

## What was added

| file | role |
|---|---|
| `scripts/build-enchilada-adreno.py` | builds 5 objects into `build/enchilada-adreno/libEnchiladaAdreno.a` using the XNU `.cpo.json` / `.o.json` compile records, `xcrun ar rcs`, sha256-stable like the other six builders |
| `package/EnchiladaAdrenoProbe.cpp` | the kernel shim defining `enchilada_adreno_poll()`; reads exactly two GPUCC words, default OFF |
| `scripts/data/iokit-stub/IOKit/IOLib.h`, `.../pexpert/pexpert.h` | host-only declarations of `IOLog`, `snprintf`, `PE_parse_boot_argn` so the shim can be type-checked without the kernel tree |
| `scripts/check-enchilada-adreno-probe.py` | 18 host checks: arm64 Mach-O for all 5 objects, `enchilada_adreno_poll` defined, no unexpected undefined symbols, every cast `volatile`-qualified, and **no store through a pointer anywhere** |
| `configs/enchilada_defconfig:36` | 7th `-Wl,-force_load` → `build/enchilada-adreno/libEnchiladaAdreno.a` |
| `board.toml [kernel] archives` | 23rd entry `"build-enchilada-adreno.py"` |
| `package/BootProgress.c` | `extern void enchilada_adreno_poll(void) __attribute__((weak_import));` + a call in the 1 Hz monitor loop |

`package/BootProgressStubs.c` is **untouched**: a `weak_import` declaration plus a strong
definition in a force-loaded archive needs no stub. `CONFIG_XNU_NATIVE_ADRENO=n` is also
unchanged — the probe is a read-only observability shim, not the A6XX_GEN1 backend.
No firmware is loaded, no GMU or HFI is started, no command is submitted.

## The safety gate (why this probe cannot become the Round-22 mistake)

Round 22 established that the GPU `0x5000000`, the GMU `0x506a000` and the kgsl SMMU
`0x5040000` sit in the collapsible `gpu_cx`/`gpu_gx` domain, so a cold read can hang.
Round 28 then found the one GPU-adjacent block that stays readable while collapsed: the
GPUCC GDSC status registers (`0x05091540` CX, `0x05091010` GX).

The shim refuses to map **anything** unless five independent predicates all hold first:

1. `adreno_domain_addr_is_probe_window()` accepts both ends of the GPUCC window, and the
   size is a non-zero multiple of the 4-byte register stride.
2. both derived offsets (`0x1540`, `0x1010`) are within
   `ADRENO_DOMAIN_GPUCC_MAX_REGISTER` and stride-aligned;
3. neither status register is inside a forbidden window;
4. `adreno_domain_window_is_forbidden(0x05090000, 0x9000) == 0` — **the GPUCC carve-out**.
   The GMU DT node `reg = <0x506a000 0x30000>` numerically swallows the GPUCC window, so a
   naive range test rejects the only legal register. If that carve-out is ever lost, this
   assertion turns the probe into a refusal instead of a hang;
5. `adreno_power_gpu_windows_are_gated()` — the board facts that make the gating real.

A refusal logs `refused base=... size=... (an unsafe window is a port bug, not a device
state)` and is cached for 1 Hz replay. The result is cached and replayed too, because the
128 KiB `msgbuf` ring overwrites one-shot early logs — the stage-7 gauge lesson.

## Verification

| check | result |
|---|---|
| `scripts/check-enchilada-adreno-probe.py` | `PASS` — 18/18 checks, arm64 Mach-O ×5, no unexpected undefined symbol, no pointer store |
| `scripts/check-enchilada-archives-linked.py` | `PASS: 7 force-loaded archives, all {tree}-relative; 7 enchilada runtime builders covered` (was 6/6) |
| `scripts/check-enchilada-adreno-host.py` | `PASS` — catalog 10 + hwinit 3 + board-input 5 + power-gate 5 + domain-state 56 checks |
| delta vs remote (phase 1) | `SAME=287 ADDED=5 CHANGED=3 REMOVED=0`, exactly the intended files |
| phase-1 audit (`.NET`, independent stack) | `AUDIT_GPU_CI_P1 PASS` — 17 checks incl. the CRLF stub unchanged |
| second phase (record) | see `evidence/stage5-gpu-domain-state.md` for the round-28 record pattern |

## Honest boundaries

- **No device ran.** The COM port is absent (three independent checks), so nothing here is
  device-verified, and no register has ever been read on this hardware.
- **The archive is wired into CI, and its host type-check passes, but this round did not
  dispatch a CI run.** The wiring is verified statically: `check-enchilada-archives-linked.py`
  proves every `board.toml` builder has a matching `-force_load`, and the host check proves
  the five objects are arm64 Mach-O with the expected symbol table. A green CI run would
  prove the kernel's own clang compiles them; that is the next step, and it is the first
  thing in this area that does not need the device.
- **The GPU backend is still absent.** `CONFIG_XNU_NATIVE_ADRENO=n`, no CP ring, no GMU
  start, no firmware load. Being force-loaded only guarantees the code is *linked*, not
  that it runs: the shim exits immediately unless the boot-arg `enchilada-adreno=1` is
  present.
- `CONFIG_XNU_NATIVE_ADRENO=n` will not change until the signed ZAP chain and the
  A6XX_GEN1 backend exist (the Round 20/21/22 blockers, unchanged).
