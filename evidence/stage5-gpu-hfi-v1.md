# Stage 5 GPU — A630 legacy GMU HFI v1 transport (Round 31, HOST-VERIFIED)

Date: 2026-11-10. Round: 31/50. Status: **HOST-VERIFIED** (source-anchored
code model + host tests + CI wiring). No device was present, nothing was
flashed, no MMIO was read, no firmware was loaded, no GMU was started.

## 1. Why this round

The `porting.toml` gpu-area blocker (b) states that stage 5 on this board needs
an "A6XX_GEN1 backend (legacy GMU HFI v1 / SPTPRAC)". The upstream UniXNU
transport at `oneplus6-ios/work/UniXNU/kits/AdrenoFirmware/AdrenoHfi.h:23`
declares:

> Modern (nonlegacy) GMU HFI only. Caller owns an exclusive, 16KiB extent
> mapped with four native4K pages, coherent for CPU and GMU, and a valid
> 32bit GMU IOVA.

That explicitly EXCLUDES the enchilada Adreno A630, which Linux v6.12 marks
`gmu->legacy = true` (`a6xx_gmu.c:1736-1738`, comment "HFI v1, has sptprac").
This is a genuine CODE component gap, not a stale record: there was no
freestanding legacy HFI v1 transport in the device tree. The device has been
offline since Round 22 (no COM port), so the round's concrete progress is a
host-verified, CI-buildable pure-C implementation of the A630 legacy HFI v1
wire protocol, grounded in sha-pinned upstream sources (per m12247
"先核验再上机" / "广域查找解决方案" and m03546 "每步两次验证").

## 2. Broad source lookup (pins, read verbatim before writing any code)

- `scripts/data/adreno-a630-inputs.json` pins 8 Linux v6.12 sources; the
  relevant one is `drivers/gpu/drm/msm/adreno/a6xx_hfi.c`, 22853 B, sha256
  `f62c802d25475b86ac05cc58df012e088230ed64a0e9319456eb20ebb40c9faa`, cached at
  `.cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_hfi.c` (855 lines).
- `a6xx_hfi.h` and `a6xx_gmu.h` obtained via
  `https://cdn.jsdelivr.net/gh/torvalds/linux@v6.12/...` (HTTP 200) for the
  queue-header / message struct layouts and the `bool legacy` field.
- Upstream MODERN transport for contrast:
  `oneplus6-ios/work/UniXNU/kits/AdrenoFirmware/AdrenoHfi.h` (60 lines) and
  `AdrenoHfi.c` (115 lines).

### The 7 load-bearing legacy-vs-modern deltas (all anchored to exact lines)

1. **2 queues, not 3.** `a6xx_hfi.c:834-844`: `table_size = sizeof(*table)`
   (24 B) `+ ARRAY_SIZE(gmu->queues)*sizeof(queue_header)` (2*48 B) = 120 B;
   `qhdr0_offset = sizeof(*table) >> 2` = 6; `qhdr_size = ... >> 2` = 12;
   `num_queues = active_queues = ARRAY_SIZE(gmu->queues)` = 2. Modern
   `AdrenoHfi.c:20` expects `m[1] == 168` (24+3*48, 3 queues).
2. **Response queue id 4, not 1.** `a6xx_hfi.c:853-854`
   `a6xx_hfi_queue_init(..., gmu->legacy ? 4 : 1)` and `:809`
   `header->type = 10 << 8 | id` → legacy response `type = 0xa04`, modern
   `0xa01`. Command queue is `0xa00` in both.
3. **NO 4-dword index alignment.** `a6xx_hfi.c:59-60`
   `if (!gmu->legacy) index = ALIGN(index, 4) % header->size;` — legacy skips
   it on READ. Modern `AdrenoHfi.c:26` poisons on `(*read&3) || (*write&3)`.
4. **NO 0xfafafafa write padding.** `a6xx_hfi.c:90-93`
   `if (!gmu->legacy) { ... queue->data[index] = 0xfafafafa; }` — legacy writes
   exactly `count` dwords. Modern `AdrenoHfi.c:60` pads to `(count+3)&~3`.
5. **Startup v1 is a different message sequence.** `a6xx_hfi.c:747-748`
   `if (gmu->legacy) return a6xx_hfi_start_v1(...)`, and `:708-741`
   `a6xx_hfi_start_v1` = INIT → FW_VERSION → PERF_TABLE_V1 → BW_TABLE → TEST
   ("no more HFI messages until next boot"). NO CORE_FW_START, NO START
   (those are modern `:743-772`).
6. **FW version 1.10.** `a6xx_hfi.c:210-211`
   `msg.supported_version = (1 << 28) | (1 << 19) | (1 << 17);` = `0x100A0000`.
7. **Wire header + ACK matching.** `a6xx_hfi.c:181`
   `(seqnum << 20) | (HFI_MSG_CMD << 16) | (dwords << 8) | id` (HFI_MSG_CMD=0),
   `seqnum % 0xfff` at `:178`; ACK matched on `HFI_HEADER_SEQNUM(resp.ret_header)`
   (`:148-153`), F2H error id 100 skipped (`:139-146`), `resp.error != 0`
   → `-EINVAL` (`:155-160`). `a6xx_hfi.h`: `struct a6xx_hfi_msg_error
   { u32 header; u32 code; u32 payload[2]; }` — code at dword 1.

## 3. Deliverables (all freestanding pure C / C++17, pure LF)

- `drivers/NativeAdreno/AdrenoHfiV1.h` (185 lines) — 20+ constants
  (`ADRENO_HFI_V1_QUEUE_COUNT 2`, `RESP_QUEUE_ID 4`, `QUEUE_TYPE 0xa00|id`,
  `SUPPORTED_VERSION 0x100A0000`, `SHARED_BYTES 12288`, header macros,
  `AdrenoHfiV1Status`, `AdrenoHfiV1Step`, `AdrenoHfiV1StepDesc`,
  `struct AdrenoHfiV1`) and 9 function declarations.
- `drivers/NativeAdreno/AdrenoHfiV1.c` (390 lines) — 2-queue/120-B table
  init, `validate()` (bounds only, NO `&3` check), `MakeHeader`, `Send`
  (exact `write_index += count`, no padding), `Poll` (exact `read_index +=
  count`, ACK by `ret_header` seqnum, F2H 100 handled with dword-1 code,
  payload from dword 3), `Exchange` (bounded wait), the 5-step legacy startup
  table, and `AdrenoHfiV1IsLegacyStart() == 1`. NO MMIO, NO firmware, NO
  boot-arg, NO kernel caller.
- `drivers/NativeAdreno/tests/adreno-hfi-v1-test.cpp` (7 groups / 94 checks) —
  table geometry, exact wire header bits, no-padding send, poll/ACK/error/
  seqnum-mismatch, bounded exchange + timeout, legacy startup table, and
  refusal of a forged modern 3-queue table.
- `scripts/verify-enchilada-hfi-v1.py` — INDEPENDENT domain 2, never runs the
  module: asserts the 7 deltas against the pinned `a6xx_hfi.c` raw text, the
  MODERN contrast against upstream `AdrenoHfi.h/.c`, and 14 `#define`s plus
  "no modern-only construct leaked into code" against the new module.
- `scripts/check-enchilada-adreno-host.py` — 6th TEST tuple + 5th SOURCES
  entry (`AdrenoHfiV1`), final print updated.
- `scripts/build-enchilada-adreno.py` — `AdrenoHfiV1.c` added as the 6th
  kernel-CI unit (`-mgeneral-regs-only`) so `libEnchiladaAdreno.a` will compile
  it under the kernel's own clang.

## 4. Verification (two independent domains + pre-CI check, all PASS)

- **Domain 1 — host execution**: `scripts/check-enchilada-adreno-host.py
  --cc <clang> --cxx <clang++> --out <tmp>` → `RUNNER_EXIT=0`; 10 catalog +
  3 hwinit + 5 board-input + 5 power-gate + 7 domain-state (56 checks) + 7 HFI
  v1 (94 checks) groups; final `PASS: enchilada A630
  catalog/hwinit/board-input/power-gate/domain-state/hfi-v1 host tests`.
- **Domain 2 — source cross-check**: `scripts/verify-enchilada-hfi-v1.py` →
  `CROSS_EXIT=0`, `HFI_V1_CROSS: PASS (46 checks, pinned upstream + modern
  contrast + derived module agree)`.
- **Linkage guard**: `scripts/check-enchilada-archives-linked.py` →
  `ARCH_EXIT=0`, `PASS: 7 force-loaded archives, all {tree}-relative; 7
  enchilada runtime builders covered`.
- **Pre-CI arm64 Mach-O**: `clang -target arm64-apple-macos11 -std=c11
  -ffreestanding -O2 -Wall -Wextra -Werror -Wconversion -Wsign-conversion
  -Wshadow -Wpedantic -Wundef -Wcast-qual -Wstrict-prototypes
  -mgeneral-regs-only -c AdrenoHfiV1.c` → exit 0, `magic 0xfeedfacf cputype
  0x100000c subtype 0 filetype 1`, 3640 B.

### 5 self-caught defects before any conclusion (m03546 working as intended)

1. Two embedded `/* ... */` sequences inside the header's block comment
   (`AdrenoHfiV1.h:11`, `:38`) closed it early → strict `-Werror` compile
   failed with `'/*' within block comment [-Werror,-Wcomment]`. Fixed to plain
   text. LESSON: never put C comments inside a C block comment.
2. Unused `uint32_t payload[2]` in `testLegacyPollAndAck` →
   `-Wunused-variable`. Removed.
3. **NULL-pointer init (decisive)**: standalone run crashed
   `0xC0000005` with no output; unbuffered stdout (`setvbuf`) traced it to
   `AdrenoHfiV1InitializeStopped` dereferencing `h->shared` in the qhdr loop
   BEFORE assigning `h->shared = shared`. Fixed by wiring the whole handle
   first. (ASAN runtime is absent in this llvm-mingw —
   `libclang_rt.asan_dynamic.dll.a` missing — so the runner's
   `sanitizers()` probe correctly falls back to "without sanitizers".)
4. **Error-packet layout**: the module's F2H id-100 read of dword 1 was
   CORRECT (`struct a6xx_hfi_msg_error { header; code; payload[2]; }`); the
   test helper `push_response()` wrongly built it in the RESPONSE layout.
   Added `push_error()` (code at dword 1, payload from dword 2) → 94/94 PASS.
5. Cross-verifier false positives (4): upstream `table_size` is a two-statement
   form, modern uses `bytes!=16384` with no spaces, and `ALIGN`/`& 3` appear
   in the new `.c` ONLY in comments. Fixed the matcher + `strip_comments()`.

## 5. Honest boundary (load-bearing)

This is a **CODE MODEL + HOST-VERIFIED build wiring**, NOT a running GPU
driver, and NOT a device measurement. Therefore the gpu area `status` STAYS
`host-verified` — it is NOT promoted to `device-verified`.

- No device present (COM port absent since Round 22), nothing flashed, no
  register read, no boot-arg consumed, no GMU/HFI exchange with real firmware.
- **The module has NO caller in the kernel yet.** `libEnchiladaAdreno.a` will
  now compile `AdrenoHfiV1.c`, but no IOService/driver instantiates it, maps a
  coherent 3-page shared extent, or owns a GMU IOVA. `CONFIG_XNU_NATIVE_ADRENO=n`
  is unchanged; there is still no A6XX_GEN1 backend, no CP ring, no GMU
  firmware load, no ZAP shader, no Metal frontend, no rendered frame.
- Round 22/28 power-domain prohibitions are UNCHANGED: before ANY GPU MMIO the
  vddcx/vdd regulators, gpu_cx/gpu_gx GDSCs, 6 clocks, gcc_gpu_memnoc_gfx_clk
  and GMU must be brought up. The first legal device action remains the single
  gated read of `0x05091540` (CX) / `0x05091010` (GX).
- The 3 vendor-signed ZAP blobs (`a630_zap.mdt/.b00/.b01`, `GPU_PAS_ID 13`)
  are still absent; the signed firmware chain remains a hard blocker for a
  full Metal path.

## 6. Next (strict order, still open)

1. When a device is present: perform the gated GPUCC GDSC domain-state read
   (CI + two independent device verifications), per m12247/m03546.
2. Implement the A6XX_GEN1 kernel backend that INSTANTIATES this transport —
   coherent 3-page HFI extent, GMU IOVA, legacy HFI v1 startup sequence, RPMh/
   SPTPRAC, CP ring — behind the full power-up sequence; then a real device
   stage-1 kgsl SMMU context.
3. Obtain the vendor-signed ZAP firmware chain; only then can gpu progress
   beyond host-verified. network/audio/cellular device verification also still
   open.
