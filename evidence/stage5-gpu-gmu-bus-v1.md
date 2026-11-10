# ROUND 44 (2026-11-10) — A6XX_GEN1 GMU bus adapter — HOST-VERIFIED

## What was built

`drivers/NativeAdreno/AdrenoGmuBusV1.h` / `AdrenoGmuBusV1.c` — a freestanding
pure-C bus adapter that translates the `AdrenoGmuBootV1` ordered-op model
(domains Gmu/Rscc/PdcSeq/Pdc, word-offset operations) to real MMIO windows.

- Domains are 1:1 with `AdrenoGmuBootV1` (Gmu=0, Rscc=1, PdcSeq=2, Pdc=3).
- `struct AdrenoGmuBusV1Windows { gmu_mmio, gmu_bytes, pdc_seq_mmio,
  pdc_seq_bytes, pdc_mmio, pdc_bytes }` — caller-provided windows only.
- Word-offset addressing: every access is `(word_offset << 2) + 4 <= bytes`
  (`a6xx_gmu.h:104-152` io contract, sha-pinned Linux v6.12).
- The RSCC window is DERIVED, not mapped: base `gmu_mmio + 0x23000`
  (`a6xx_gmu.c:1771`), guard `gmu_bytes - 0x23000` clamped ≥ 0.
- `AdrenoGmuBusV1` is a typedef of `AdrenoGmuBootV1Bus` — the same three
  callbacks the boot model already uses.
- Fail-closed: unknown domain, NULL window, OOB, NULL/empty bulk values and
  oversize bulk spans are all refused. NO mapping/clock/regulator/power action.

## Why this step exists (strict order context)

Round 42 encoded the A630 legacy GMU cold-boot bring-up sequence as an
ordered-op plan over abstract bus callbacks. Round 44 supplies the concrete
translation to MMIO so a future kernel caller can power the GMU, run
`PlanRpmhInit` once, then `PlanCold` with real callbacks. The adapter itself
performs no power action; the caller must have CX/GX genpd + 6 clocks +
RPMh/PDC sequence done first.

## Key test findings (corrected against upstream during the two-verification loop)

1. `PDC_SEQ_START_ADDR 0x1148` is a WORD offset → byte 0x4520.
2. `PDC_ENABLE_PDC 0x1140` → byte 0x4500; `PDC_TCS3_CMD0_DATA 0x15db` →
   byte 0x576c. Simulated PDC windows must be ≥ 8192 words (32 KiB).
3. Upstream `setup_pdc` (a6xx_gmu.c:641-643) writes
   `REG_A6XX_PDC_GPU_SEQ_START_ADDR = 0` FIRST, then
   `REG_A6XX_PDC_GPU_ENABLE_PDC = 0x80000001` — the end-to-end test asserts
   exactly this pair.
4. The `0x4520` SEQ_START_VALUE belongs to the RSCC `PDC_SEQ_START_ADDR`
   (word offset 0x0008) write at `a6xx_gmu.c:573`, NOT the PDC window.
5. RSC ucode goes through `gmu_write_rscc(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0, …)`
   (:590-594); PDC ucode through `pdc_write(seqptr, REG_A6XX_PDC_GPU_SEQ_MEM_0, …)`
   (:601-605) — two different windows, two different domains.

All four initial test failures were TEST bugs (missing BootV1 object in the
link list, the 0x4520 mis-targeting, undersized simulated windows, unwired
pdc_seq window); the adapter itself never failed a check.

## Two independent verifications (m03546 discipline)

1. Host runner `scripts/check-enchilada-adreno-host.py` — 9 suites:
   `PASS: A6XX_GEN1 GMU bus adapter (7 test groups)` plus the sanitizer pass.
2. Cross verifier `scripts/verify-enchilada-gmu-bus-v1.py`:
   `GMU_BUS_V1_CROSS: PASS (40 checks, 0 failures)` — pinned `a6xx_gmu.c`
   sha integrity + io-contract slices, quard `a6xx.xml.h` PDC offsets
   0x1140/0x1148/0x15db, adapter translation checks, and absence of any
   power/mapping token.

Strict compile: `clang -std=c11 -Wall -Wextra -Werror -pedantic
-Wconversion -Wsign-conversion -Wshadow` on `AdrenoGmuBusV1.c` → exit 0.

## CI inclusion (compiled-not-executed)

`scripts/build-enchilada-adreno.py` units gained `AdrenoGmuBootV1.c` and
`AdrenoGmuBusV1.c` (both `-mgeneral-regs-only`), so the kernel CI archive
grows 7 → 9 objects and the kernel relinks (+8192 B). This supersedes the
round-42 note that BootV1 was "not a CI object". The objects are compiled by
CI but have no kernel caller and never execute against hardware.

## Honest boundary

HOST-VERIFIED only. No device run, no MMIO mapped on hardware, no clock or
regulator touched, no GMU start, no firmware load. `CONFIG_XNU_NATIVE_ADRENO=n`
unchanged. gpu area stays `host-verified`. Remaining stage-5 blockers are
unchanged: powered-GMU kernel caller, CX/GX genpd + 6 clocks power-up,
TrustZone PAS auth (GPU_PAS_ID 13), device kgsl SMMU stage-1 context.
