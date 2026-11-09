# Stage 1 — appSMMU read-only survey: host verification record

OnePlus 6 / enchilada / SDM845. iPadOS 17.7.11 (21H461).
Strict unixnu.com 8-stage driver order, stage 1 = CPU / interrupt / timer / RAM / DMA.

**Status: HOST-VERIFIED. Not device-verified.** No CI kernel carrying
`libEnchiladaSmmu.a` has been built at the time of writing, and no device boot has
reported an `enchilada-smmu:` line. Nothing here may be promoted to
`device-verified` on the strength of these runs.

## What was built

| File | Purpose |
| --- | --- |
| `drivers/NativeSmmu/SmmuSurvey.h` | Public types and the derived geometry |
| `drivers/NativeSmmu/SmmuSurvey.c` | Freestanding C11: identification, geometry, stream scan, formatting |
| `drivers/NativeSmmu/tests/smmu-survey-test.cpp` | 13 host test groups over a fake register file |
| `drivers/NativeSmmu/README.md` | Register map, refusals, read-only contract |
| `package/EnchiladaSmmuProbe.cpp` | Thin kernel shim: one mapping, one survey, 1 Hz replay |
| `scripts/check-enchilada-smmu-host.py` | Host runner (compiles C11, asserts from C++17) |
| `scripts/build-enchilada-smmu.py` | Archive builder for `libEnchiladaSmmu.a` |

Wiring: `package/BootProgress.c` declares and calls `enchilada_smmu_poll()`,
`board.toml` registers the builder, and
`configs/enchilada_defconfig` force-loads the archive (six entries now).
`package/BootProgressStubs.c` and `.symbols` are deliberately byte-identical: a
`weak_import` function with a strong definition in a force-loaded archive needs
no stub, exactly as `BootProgress.c:141-142` and `BootDiagExport.c:43`
(`armcpu_map_physical`) already demonstrate.

## Register map, checked against three sources

Every offset below was read from the port's own working code before it was
written into `SmmuSurvey.c`, and all three sources agree:

| Offset | Register | Source |
| --- | --- | --- |
| `0x000` | `sCR0` | `NativeUfs.cpp:873` |
| `0x020` | `IDR0` (slots in bits 7:0, EXIDS in bit 8) | `NativeUfs.cpp:874` |
| `0x024` | `IDR1` (banks in bits 7:0, page shift in bits 30:28, 64K in bit 31) | `NativeUfs.cpp:884`, `SmmuTakeover.cpp:33` |
| `0x028` | `IDR2` | `Dwc3SmmuPlan.h:49` |
| `0x048` | `sGFSR` | `NativeUfs.cpp:883` |
| `0x800 + 4n` | `SMR(n)` — bit 31 valid, bits 30:16 mask | `SmmuTakeover.h:141` |
| `0xc00 + 4n` | `S2CR(n)` — `(s2cr >> 16) & 3` is the translation type | `SmmuTakeover.h:142` |

Geometry is derived, never assumed: `slots = idr0 & 0xff`, `banks = idr1 & 0xff`,
`pages = 1 << (((idr1 >> 28) & 7) + 1)`, matching `NativeUfs.cpp:915` and
`ArmSmmu4KProgram.c:62-80`.

The one geometry this code refuses outright is a 64 KiB SMMU. The port's existing
test states the rule as a comment:

```
reset(); r[0x24/4] |= 0x80000000; assert(plan().error); // Never guess 64K register geometry.
```

`SmmuSurvey.c` follows the same rule and returns `kSmmuSurveyUnsupported` when
`idr1` bit 31 is set, when any count is zero, when `slots > 128`, `banks > 128`
or `pages > 128`, or when `(pages + banks) * 4096` exceeds the `0x80000` window.

## Verification 1 — `scripts/check-enchilada-smmu-host.py`

```
PASS: enchilada apps_smmu read-only survey (13 test groups)
PASS: enchilada appSMMU read-only survey host tests (without sanitizers)
exit 0
```

The 13 groups are: SDM845 geometry, 64K refusal, empty/degenerate geometry
refusal, NULL and I/O error propagation, finding both the UFS (0x60) and DWC3
(0x740) streams, reporting absent streams as not-found, classifying translation
and fault entries, honouring SMR mask bits, determinism, bounded and
NUL-terminated formatting, type names, not reading an S2CR for a free slot, and
propagating a mid-scan I/O failure.

## Verification 2 — `work/tmp/verify_smmu_survey_2.py` (different method)

```
SMMU_VERIFY2: PASS (constants re-derived from other files, API shape proves read-only,
freestanding -fno-builtin at -O0/-O2, independent classification harness)
```

This one is deliberately not a rerun of method 1:

* it re-derives the window (`0x15000000`/`0x80000`) and both stream ids from
  `drivers/NativeDwc3/Dwc3Registers.h:35-36`, `drivers/NativeUfs/NativeUfs.cpp:432`
  and `drivers/NativeDwc3/Dwc3SmmuPlan.h`, rather than trusting the constants
  under test;
* it re-derives every register offset from `NativeUfs.cpp:862-944` and
  `Dwc3SmmuPlan.h:47`;
* it proves the read-only contract from the **API shape** — `struct
  smmu_survey_io` has a `read32` and no write member at all — and from the
  absence of stores in `SmmuSurvey.c`;
* it compiles with `-ffreestanding -fno-builtin` at `-O0` and `-O2` to prove the
  core needs no libc;
* it links its own harness with the **opposite** fixture (UFS translation into
  bank 9, DWC3 fault in bank 11) and asserts the formatted text:
  `SS slots=128 banks=64 pages=64 exids=0 valid=2 t0=0 t1=1 t2=1 scr0=00000406
  gfsr=00000000 ufs=12/9/translation usb=40/11/fault`;
* it asserts exactly four exported symbols, all in the `smmu_survey_` namespace.

Both scripts reported a passing run only after fixing real defects in the
*verifiers* (an over-strict `#define` regex that ignored column alignment; a
symbol filter that read the wrong `llvm-nm` field and therefore passed
vacuously; and a missing `PATH` that made the harness fail to start with
`0xC0000135`). The vacuous-pass bug is worth naming: the first version asserted
"every exported symbol is namespaced" over an **empty** set. It now also asserts
that the set is non-empty and has exactly four members.

## Why a survey and not a translation driver

`porting.toml` memory `limits[0]` still stands: the `ArmSmmu4K` kit is not
linked, and translation remains the firmware/UEFI bypass slots. Seizing the SMMU
would require programming a stage-1 context and enabling a bank — writes that a
mistake turns into an unrecoverable bus abort. Observing first has three
properties that make it the correct first step under "先核验再上机":

1. every access is a read of an APB register in a window this port already maps
   for UFS and DWC3, so a mistake cannot brick the boot;
2. the output is directly comparable with what both existing arms already assert
   (`SmmuTakeover.h:141-148`, `Dwc3SmmuPlan.h:44-129`);
3. the decision logic is host-testable against a fake register file, so it is
   verified before it ever touches hardware.

## Read-only contract (stated, not implied)

`struct smmu_survey_io` carries **only** `read32`. There is no write member to
call and `SmmuSurvey.c` contains no store into the mapping. The survey does not
program a context, does not enable a bank, does not write SMR or S2CR, loads no
firmware, and cannot recover a synchronous bus abort. A free slot's S2CR is never
read — the SMR-valid test gates it — which makes the read count an assertion
(128 SMR reads plus one S2CR read per valid entry).

## Honest boundaries

* This proves the *observation* is correct, not that SMMU translation works.
* `device-verified` for this area still requires a CI kernel with
  `libEnchiladaSmmu.a` and a device boot whose log carries
  `enchilada-smmu: rc=0 SS ...`.
* The 1 Hz replay exists because the kernel msgbuf is a 128 KB ring that keeps
  only the newest bytes; measured on device it saturated at 101.2 s / 87.7 s /
  58.8 s across three single-variable boots (`work/tmp/GAUGE-DIAGNOSIS-3.md`).
  Replay performs no MMIO, so the measurement still happens exactly once.
