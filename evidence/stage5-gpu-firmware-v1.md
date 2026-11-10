# Stage 5 GPU — A630 legacy GMU firmware-load contract (Round 33, HOST-VERIFIED)

2026-11-10. Device offline (3 independent methods: CIM serial-ports = 0,
.NET SerialPort.GetPortNames() = 0, PnP Ports present=False; registry
SERIALCOMM empty). NO flash, NO device register read, NO device claim. gpu area
stays `host-verified`.

## Why this round (the genuine generation gap)

Round 31 closed the A630 **legacy HFI v1 transport** gap. Round 33's broad
lookup (m12247) found the PARALLEL gap in **firmware loading**: upstream sheng
`work/UniXNU/kits/AdrenoFirmware/AdrenoFirmware.c` `AdrenoFirmwarePlanGmu()`
models the MODERN segmented GMU image — a 16-byte `block_header`
`{ addr, size, type, value }` iterated into ITCM/DTCM/icache/dcache/dummy
regions, with region validation across two passes. Its README says "GMU cold
start and HFI work on sheng hardware." It does NOT cover the A630.

The sha256-pinned Linux v6.12 `a6xx_gmu.c:739-750` shows A630 (`gmu->legacy`)
does something **completely different**:

```c
if (gmu->legacy) {
    /* Sanity check the size of the firmware that was loaded */
    if (fw_image->size > 0x8000) {
        DRM_DEV_ERROR(gmu->dev, "GMU firmware is bigger than the available region\n");
        return -EINVAL;
    }
    gmu_write_bulk(gmu, REG_A6XX_GMU_CM3_ITCM_START,
                   (u32*) fw_image->data, fw_image->size);
    return 0;
}
```

ONE flat ITCM bulk write, NO block_header parse, NO region validation.
`a6xx_gmu.c:733-734` `itcm_base=0x00000000`, `dtcm_base=0x00040000`;
`a6xx_gmu.xml:45-46` `GMU_CM3_ITCM_START 0xc00` / `GMU_CM3_DTCM_START 0x1c00`.
Cold-boot register writes `a6xx_gmu.c:821-826`: `CM3_FW_INIT_RESULT=0`,
`CM3_BOOT_CONFIG=0x02`, `HFI_QTBL_ADDR=gmu->hfi.iova`, `HFI_QTBL_INFO=1`.

No legacy firmware-load model existed in the enchilada tree before this round.

## Source grounding (sha-pinned, two independent reads)

- `scripts/data/adreno-a630-inputs.json` → `.cache/adreno-a630-source/`:
  `a6xx_gmu.c` (48981 B, `c14fbb25…`), `a6xx_gpu.c` (69087 B, `c334db82…`),
  `a6xx_catalog.c` (51692 B, `6d9061ee…`), `a6xx_gmu.xml` (11830 B,
  `462b8960…`) — all verified byte-for-byte in
  `scripts/verify-enchilada-firmware-v1.py`.
- `a6xx_gpu.c:17` `#define GPU_PAS_ID 13`; `:720-740` A630 SQE whereami
  predicate `(((buf[0]&0xf)==0xa && (buf[2]&0xf)>=1) || (buf[0]&0xfff)>=0x190)`,
  reading the **MAPPED BO** via `msm_gem_get_vaddr(obj)` (`:700`).
- `a6xx_catalog.c:897` `.zapfw="a630_zap.mdt"` (signed split chain) vs
  `:1047-1049` `MODULE_FIRMWARE("qcom/a630_zap.mbn")` (public single blob).
- `a6xx_gpu.c:819` `adreno_zap_shader_load(gpu, GPU_PAS_ID)`.

## Three public A630 blobs (OBSERVATIONS, not canonical)

`scripts/data/adreno-a630-firmware.json` pins sizes + sha256 observed from ONE
reachable mirror (`endlessm/linux-firmware@master`), cross-checked by TWO
independent network paths (jsdelivr CDN + GitHub API base64, both agree):

| blob | bytes | sha256 (first 16) | role |
|---|---|---|---|
| `qcom/a630_sqe.fw` | 34188 | `1c21b527d9183487` | SQE microcode |
| `qcom/a630_gmu.bin` | 32768 | `da8d9b1b1f5c1a0b` | GMU firmware (exactly 0x8000) |
| `qcom/sdm845/a630_zap.mbn` | 14256 | `4aef4aff842b7db4` | ZAP (public single) |

**Honest caveat (load-bearing)**: official/alternate mirrors were UNREACHABLE in
this environment (`git.kernel.org` SSL handshake timeout,
`DroidX-Projects` 404, `raw.githubusercontent.com` RemoteDisconnected, GitHub
API `torvalds/linux` 404). Only ONE mirror could be read → these hashes are
OBSERVATIONS, NOT asserted canonical/official. The raw `a630_sqe.fw`
`dword[0]==0` finding is likewise NOT a verdict: `a6xx_ucode_check_version()`
reads the MAPPED BO after `adreno_fw_create_bo()` may relocate/parse the image.
The new module therefore implements the whereami **predicate** only; a future
caller applies it to the real mapped image.

## Deliverables (all freestanding, pure, HOST-VERIFIED)

- `drivers/NativeAdreno/AdrenoFirmwareV1.h/.c` — the A630 legacy GMU
  firmware-load contract: `adreno_fw_v1_plan_gmu_legacy()` (flat ITCM ≤0x8000
  single bulk write, refuses TooLarge/Unaligned/Empty/Invalid and zeroes *out),
  `adreno_fw_v1_sqe_whereami()` (exact upstream predicate), a 3-blob table with
  observed sizes/hashes, `adreno_fw_v1_zap_requires_signed_chain()` (PAS 13),
  and a **fail-closed** `adreno_fw_v1_ready()` 6-way AND gate (power, GMU plan,
  blobs, signed ZAP, SQE whereami, kernel caller) that returns 0 today.
- `drivers/NativeAdreno/tests/adreno-firmware-v1-test.cpp` — 8 groups / 70 checks.
- `scripts/data/adreno-a630-firmware.json` — observed blob manifest + contract.
- `scripts/verify-enchilada-firmware-v1.py` — independent cross-verifier.
- `scripts/check-enchilada-adreno-host.py` / `scripts/build-enchilada-adreno.py`
  — wired the 7th test and 7th archive object.

## Verification (m03546 — two independent domains)

1. **Host runtime** `check-enchilada-adreno-host.py` → `PASS: ... firmware-v1
   host tests`; new module `70 checks` across 8 groups. Strict `-Wconversion
   -Wsign-conversion -Wshadow -Wpedantic` C11 syntax = exit 0; strict C++17 test
   syntax = exit 0; arm64 Mach-O `-target arm64-apple-macos11` object =
   `0xfeedfacf / 0x100000c / 0 / 1` (2120 B). `check-enchilada-archives-linked.py`
   = `PASS: 7 force-loaded archives ... 7 enchilada runtime builders covered`.
2. **Independent source cross-check** `verify-enchilada-firmware-v1.py` →
   `FIRMWARE_V1_CROSS: PASS (47 checks, pinned upstream + modern contrast +
   derived module agree)`. It never runs the module: it re-verifies the 4 pinned
   source hashes, asserts the exact upstream legacy/modern lines, the modern
   sheng planner contrast, all 15 derived `#define`s, the 3 sha256 pins, and
   that no modern-only construct (block_header parse, region validation) leaked
   into the legacy model.

Three initial cross-check FAILs were proven **checker false positives** by two
independent reads (grep + exact-line read) and fixed in the checker, NOT in the
content: (a) helper name `adreno_fw_v1_modern_block_header_bytes()` only
RETURNS the contrast constant, it does not parse; (b) the gate uses the macro
`ADRENO_FW_V1_GMU_MAX_BYTES` (single source of truth) not the raw `0x8000`;
(c) the SQE "NOT a verdict" caveat lives in a `//` comment — comment-stripping
must not be applied to that check (same lesson as rounds 31/32).

## Honest boundary (unchanged)

- HOST-VERIFIED only. gpu is NOT device-verified, NOT a driver. A CI green
  build+link (when dispatched) is a BUILD ARTIFACT, not a device measurement.
- No kernel caller, no coherent GMU firmware extent, no GMU IOVA/notify/HFI
  exchange, `CONFIG_XNU_NATIVE_ADRENO=n`.
- Round 22/28 power-up gate unchanged: GPU `0x5000000`, GMU `0x506a000`,
  kgsl SMMU `0x5040000` sit in the collapsible `gpu_cx`/`gpu_gx` domain; NO cold
  MMIO. First legal device action is a GATED read of `0x05091540` / `0x05091010`
  after the full power sequence.
- Vendor-signed ZAP `.mdt/.b00/.b01` chain (GPU_PAS_ID 13) ABSENT →
  `adreno_fw_v1_ready()` is fail-closed = 0.
- Remaining strict-order work: A6XX_GEN1 backend caller instantiating
  `AdrenoHfiV1` + this plan on a powered GMU, SPTPRAC 0x778000 + RPMh/PDC +
  kgsl SMMU v2 stage-1 context + CP/PM4, then a single gated device read.

## Reproduce

```
python -X utf8 scripts/check-enchilada-adreno-host.py \
    --cc <llvm-mingw>/clang.exe --cxx <llvm-mingw>/clang++.exe --out <dir>
python -X utf8 scripts/verify-enchilada-firmware-v1.py
python -X utf8 scripts/check-enchilada-archives-linked.py
```
