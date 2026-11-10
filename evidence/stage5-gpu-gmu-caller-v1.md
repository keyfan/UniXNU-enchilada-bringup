# Round 46 — A6XX_GEN1 powered-GMU caller chain (`AdrenoGmuCallerV1`) — HOST-VERIFIED

**Date:** 2026-11-10 · **Area:** gpu (stage 5) · **Status: HOST-VERIFIED.**
No device run, no MMIO mapping, no domain power-up, no firmware load: every
value in this module is a *specification* transcribed from pinned sources and
the round-35 device-verified GPUCC window; the module is compiled-not-executed
in CI (`hardware_execution: False`).

## What was built

`drivers/NativeAdreno/AdrenoGmuCallerV1.h/.c` — freestanding pure-C caller
chain that turns the round-42 ordered-op model (`AdrenoGmuBootV1`) and the
round-44 MMIO bus adapter (`AdrenoGmuBusV1`) into the fail-closed boot
sequence a real backend would drive:

1. **Precheck** (`AdrenoGmuCallerV1Precheck`) — caller-asserted state FIRST
   (`cx_genpd_on`, `gx_genpd_on`, `clocks_enabled` from upstream
   `a6xx_gmu.c:1033-1047`), then a GPUCC cross-check via the reader callback:
   CX status BIT(31) must be set, and if `require_gx`, GX status BIT(16).
   Any refusal happens BEFORE any GMU-window access.
2. **BootState** (`AdrenoGmuCallerV1BootState`) — reads `GENERAL_7` (0x51cc)
   through the wired bus and decodes WARM/COLD exactly like upstream
   `gmu_start :1062-1068` (`1 → WARM`, else COLD).
3. **ColdBoot** (`AdrenoGmuCallerV1ColdBoot`) — optional one-shot
   `PlanRpmhInit` (RSC/PDC ucode + TCS cmds, upstream probe-time semantics),
   warm refusal (`kAdrenoGmuCallerV1WarmUnsupported` — warm resume is
   upstream `a6xx_gmu_resume` work, deliberately out of scope), then
   `PlanCold` + `AdrenoGmuBootV1Execute` with per-Poll retries bounded by
   `poll_retries`. Status enum
   `Ok/Invalid/NotReady/CxOff/GxOff/WarmUnsupported/BusError/PollTimeout`;
   `first_fail_op` is a **diagnostic scan** (first Poll op index) — the
   status code is authoritative.

GPUCC byte offsets come from the round-35 device-verified window
(`GPUCC=5090000`): CX status byte offset `0x1540` (= 0x5091540), GX status
byte offset `0x1010` (= 0x5091010). GDSC bit decode is the round-22/28
pinned semantics (`gdsc.c:66-71`, `gpucc-sdm845.c:119-139`) via
`AdrenoDomainState.h:102-105` (CX_ON 31 / GX_ON 16 / GX_OFF 15).

## Cross-round defect found and fixed (m03546 discipline)

The round-44 bus adapter refused `write_bulk(values == NULL)`, but the
round-42 model contract legitimately sends NULL to mean "the cookie-resolved
ITCM image". The caller tests exposed it: cold-plan op idx 1 (ITCM bulk)
always returned BusError. Fixed in `AdrenoGmuBusV1`: `bulk_image` /
`bulk_words` registration (`NULL` = unchanged refuse rule), with extended
bus-v1 tests (registered-image NULL-bulk roundtrip, too-small image refuses,
unregistered NULL still refuses).

## Verification (two independent domains + two more)

1. **Host suite** — `scripts/check-enchilada-adreno-host.py` (10 suites):
   `PASS: A6XX_GEN1 powered-GMU caller (6 test groups)`,
   `gmu-caller-v1: 50 checks, 0 failures` (log `work/tmp/r46k.log`).
   Full line: `PASS: enchilada A630 catalog/hwinit/board-input/power-gate/
   domain-state/hfi-v1/firmware-v1/gmu-boot-v1/gmu-bus-v1/gmu-caller-v1
   host tests (without sanitizers)`.
2. **Strict compile** — `clang -std=c11 -Wall -Wextra -Werror -Wconversion
   -Wsign-conversion -Wshadow -pedantic` on `AdrenoGmuCallerV1.c` → exit 0.
3. **Cross verifier** — `scripts/verify-enchilada-gmu-caller-v1.py` against
   the sha-pinned Linux v6.12 sources:
   `GMU_CALLER_V1_CROSS: PASS (26 checks, 0 failures)`.
4. **CI-object consumer probe** — `tests/adreno-gmu-caller-v1-ci-check.cpp`
   (all-NULL wiring, strict clang++) → exit 0; the archive build gains the
   10th object (`scripts/build-enchilada-adreno.py` units + `source_sha256`
   stems). CI compile of the object is the second half of that verification.

## Honest boundary

- HOST-VERIFIED ONLY: no device, no MMIO window, no power domain, no
  firmware load, no GMU/HFI start was performed or observed.
- Warm resume (`GENERAL_7 == 1`) is REFUSED, not implemented.
- PAS/ZAP authentication (GPU_PAS_ID 13) is outside this module.
- gpu area stays `host-verified`.
