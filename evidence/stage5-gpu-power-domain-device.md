# Stage 5 GPU — first real-device A630 power-domain measurement (round 35)

**Status: DEVICE-VERIFIED for a narrow precondition only.** The `gpu` AREA stays
`host-verified`: this document proves that a READ-ONLY GPUCC GDSC status read is
safe on real hardware and reports CX down / GX off. It does NOT prove a GPU
driver, a Metal path, firmware load, GMU start, HFI exchange, or any command
submission. See "Honest boundary" below.

## Why this round exists

On 2026-11-10 the user asked (m33324) "什么时候可以上机验证" and then authorized
"开始做探针并准备上机" (the first real-device WRITE of this session: flash
`misc` + `system_b`, reboot, capture, decode). Before that, rounds 22-34 had
done only HOST-VERIFIED GPU work because the phone was offline (11-09 to
12:35) and because m12247 forbids flashing an old kernel with no new code.

The probe did NOT need writing: `package/EnchiladaAdrenoProbe.cpp` (195 lines,
added in round 28/29) is already a STRICTLY READ-ONLY, default-OFF,
boot-arg-gated (`enchilada-adreno=1`) kernel shim with five safety predicates
evaluated BEFORE `armcpu_map_physical()`. Its only dereference is
`uint32_t rd32(uintptr_t va, uint32_t off) { return *reinterpret_cast<volatile uint32_t *>(va + off); }`
and it reads exactly two GPUCC GDSC status registers: CX `0x05091540`
(BIT(31) power-on) and GX `0x05091010` (BIT(16) up / BIT(15) down), with
`dsb sy`, then replays the one observation once per second (1 Hz) because the
128 KiB `msgbuf` ring wraps at ~60-100 s on this board.

## Flash identity (single-variable design)

CI #219 (`42762559936`, `completed success`, device-repo head at dispatch
`9e52009ede99f83c4ab21fe497d650e64b615292`) built the kernel that actually ran:

* `kernel.macho` 126,017,536 B, sha256
  `f12a214e7b15a5a8e608f9371706966d8fac6bb581ce6416865d8dc4f45ad526`;
  the logs artifact says `PASS: libEnchiladaAdreno.a (7 objects, sha256
  859f426865f9da2a)` — the 7th object is round 33's `AdrenoFirmwareV1.c`.
* `payload-gpuprobe219.img` 150,994,944 B, sha256
  `bd44ed90764503f6cbddabf68e167dc7883f6e95680a38a5ccff250f9ba2c474`.
* Boot args: 49 items / **988 of 1000 bytes**, derived from the device-verified
  flash #8 (smmu3) arg line by REMOVING `enchilada-smmu=1` (budget) and ADDING
  the single new variable `enchilada-adreno=1`. Everything else is unchanged:
  `enchilada-wdog=3`, `enchilada-deadline=900`, `--no-reset` kernel patch,
  `misc` BCB armed for `boot-recovery`.
* Two independent PRE-FLASH verifications PASS:
  `preflash_check_gpuprobe219_1.py` (20 checks, trusts the built image) and
  `preflash_check_gpuprobe219_2.py` (re-derives args from the device-verified
  flash #8 payload, re-derives the READ-ONLY contract from source, checks CI
  #219 identity + kernel symbols + board.toml/defconfig wiring). One initial
  FAIL in #2 was a CHECKER false positive (a pointer *declaration*
  `const char *verdict = ...` matched a naive deref-store regex); two
  independent regex methods proved zero stores, and the checker was fixed to
  assert the real property instead of loosening it.

## The measurement

`python scripts/test-cycle.py gpuprobe219 --wait-return 1100` (2026-11-10
14:35-14:51 local): flashed `misc` (0.064 s), `system_b` payload (4.342 s),
`fastboot boot boot-108.img` (0.151 s), **auto-returned to recovery after
941 s**, pulled the 32 MiB persisted SHLOG window read-only, re-armed the BCB.
The COM5 CDC-ACM hostwatch window (720 s) ended before the 941 s boot completed
and never saw `VID_1209`, so the authoritative channel is the persisted SHLOG
window — the same channel every recent device-verified stage used.

Decoded header: `SHLOG001 version 4`, **`boot_id 0x205050d2`** (decimal
542134482 — a NEW boot, distinct from the 12:42 TWRP baseline `0x20e344b3`),
`first_lba 14809083`, `window_blocks 8192`, `uptime 899 s`, `slots_written
1730`, `rounds 1730`, `write_errors 0`, `msg_bytes 131072`, `state 2`.

**300 `enchilada-adreno:` replay lines**, all byte-identical in content:

```
[  627.240844]: enchilada-adreno: rc=0 GPUCC=5090000 size=9000 cx=00005ff4 gx=00088400 cx_on=0 gx_on=0 gx_off=1 verdict=cx-DOWN steps_left=9 (replay t=600 attempt=1)
```

* `rc=0`, `GPUCC=0x5090000`, `size=0x9000`: the five-predicate safety gate
  PASSED (no `refused` line anywhere), the mapping succeeded, and the read did
  not hang — this is the empirical answer to the round-22/28 open question
  "whether a read of 0x05091540 returns rather than hangs on real hardware".
* `cx=00005ff4`, `gx=00088400` are STABLE, non-garbage values across all 300 s.
* `cx_on=0` (BIT(31) clear) → **gpu_cx domain is DOWN**; `gx_on=0`,
  `gx_off=1` (BIT(15) set) → **gpu_gx rail is OFF**, which is EXPECTED because
  `gdsc_gx_do_nothing_enable` means software cannot raise GX.
* `verdict=cx-DOWN`, `steps_left=9` → `AdrenoPowerGate.c` fail-closed state:
  `adreno_power_current_state()` is all-zero, so all 9 power-up steps remain.
* `attempt=1`, `t=600..899` monotonic: one physical measurement, then a
  read-only 1 Hz replay — NOT 300 independent hardware reads.

The SAME `msgbuf.txt` carries 300 `enchilada-gauge:` replays with a live
`a[soc=100 v=4315 i=24 t=204 ...]`, and `latest.txt` (final slot 1729 at
t=899 s) carries `LIVE: DEADLINE EXCEEDED`, `root=1 procs=228` and
`UFS requests=307742 ... failures=0` — the XNU userspace really booted, this
is not a probe stub.

## Two independent device verifications (m03546)

| # | Domain | Script | Checks | Result |
|---|--------|--------|--------|--------|
| 1 | RAW BINARY + flash log + CI sha chain | `scripts/verify-gpuprobe-device-1.py` | 25 | `GPUPROBE_DEVICE_VERIFY_1: PASS` |
| 2 | DECODED TEXT + header + SOURCE contract | `scripts/verify-gpuprobe-device-2.py` | 36 | `GPUPROBE_DEVICE_VERIFY_2: PASS` |

Verifier 1 reads the 32 MiB `window-gpuprobe219.bin` bytes directly
(window sha256 `dfbdabe70291fe8ec5e188c788beeea469b39a047b8bf77f185e1de3ba947d09`),
finds `SHLOG001` and the boot_id word in the raw header, counts the 300
`enchilada-adreno:` / `rc=0 GPUCC=5090000` markers in raw bytes, proves the
decoded `msgbuf.txt` equals `raw[4096:4096+131072]` byte-for-byte, and checks
the flashed kernel/payload sha256 against CI #219 / the builder output.

Verifier 2 parses `header.txt` as key=value, walks `msgbuf.txt` line by line
with a strict regex, and asserts across ALL 300 rows: `rc=0`, `GPUCC=5090000`,
`size=9000`, `cx=00005ff4`, `gx=00088400`, `cx_on=0`, `gx_on=0`, `gx_off=1`,
`verdict=cx-DOWN`, `steps_left=9`, `attempt=1`, monotonic `t=600..899`, plus
`~299 s` at 1 Hz. It also re-derives from `EnchiladaAdrenoProbe.cpp` that the
probe is boot-arg gated, contains exactly one volatile load, and has ZERO
stores through pointers.

**Five initial FAILs were all classified as CHECKER false positives by two
independent methods each, then fixed in the CHECKERS only:**
1-4) `_flash_gpuprobe219.txt` is UTF-16LE (BOM `fffe`); both UTF-16 decode and
UTF-8+NUL-strip confirmed all four marker strings present → BOM-aware decode.
5) `latest.txt` is the final diagnostic slot, NOT the IOLog ring — it contains
no `enchilada-adreno:` by design; checking that it is the final slot with
`root=1 procs=`, `UFS requests=... failures=0` is stronger boot evidence.

## Honest boundary (load-bearing)

* This is a **real device measurement** of a **narrow precondition**: "on
  enchilada, with the GPU domains collapsed, a gated read of the two GPUCC
  GDSC status registers returns stable, non-garbage values and reports CX
  down / GX off; the read-only probe does not hang." It closes the round-22/28
  open question about whether that read is safe on real hardware.
* The `gpu` AREA status stays **`host-verified`**. The official completion
  criteria ("GPU submissions complete, a known image renders, the Metal
  client presents it") are NOT met. Zero of the six sheng GPU components are
  implemented; `CONFIG_XNU_NATIVE_ADRENO=n` is unchanged; there is still no
  A6XX_GEN1 kernel backend, no kernel caller for `AdrenoHfiV1`/`AdrenoFirmwareV1`,
  no GMU power-up, no firmware load, no HFI exchange, no CP ring, no kgsl
  SMMU stage-1 context.
* The three vendor-signed ZAP blobs (`a630_zap.mdt/.b00/.b01`,
  `GPU_PAS_ID 13`, TrustZone PAS auth) remain ABSENT. The three public blob
  sizes/sha256 in the round-33 record are OBSERVATIONS from ONE reachable
  mirror, NOT canonical/official.
* The round-22/28 prohibitions are UNCHANGED: never cold-read `0x5000000`,
  `0x506a000` or `0x5040000` before the full CX/GX genpd + 6 clocks +
  RPMh/PDC + GMU power-up sequence. This probe read `0x05091540` /
  `0x05091010` (GPUCC GDSC status), which the round-28 carve-out explicitly
  permits.
* One boot, one unit, one power state (CX down). No claim about silicon
  behaviour under power-up, GDSC switch latency, or any other boot path.
* Nothing here is a GPU driver, Metal, display acceleration, or a fix for
  battery/UI. Device was returned to TWRP recovery with the BCB re-armed.

## Next (strict 8-stage order)

1. CI + two independent device verifications are DONE for the gated probe.
2. Implement the A6XX_GEN1 kernel backend that INSTANTIATES `AdrenoHfiV1` +
   `AdrenoFirmwareV1` on a POWERED GMU (CX/GX genpd, 6 clocks, RPMh/PDC,
   flat 0x8000 ITCM load, SPTPRAC 0x778000, legacy HFI v1 startup, CP ring),
   all behind the power-up sequence this measurement just confirmed is needed.
3. Only then a real device stage-1 kgsl SMMU context @ `0x5040000` (target
   streams must first be made to fault; `ArmSmmu4KBusy=10` on the bypass
   handoff), then re-read the GDSC status AFTER power-up to prove the domains
   actually collapse and restore.
4. The signed ZAP chain remains a hard blocker for a full Metal path;
   network/audio/cellular device verification also still open.

## Reproduce

```
python -X utf8 scripts/verify-gpuprobe-device-1.py
python -X utf8 scripts/verify-gpuprobe-device-2.py
python -X utf8 scripts/verify-ci-218-hfi-v1.py 42743495901   # kernel lineage
python -X utf8 work/UniXNU/scripts/porting-record.py check --device work/UniXNU-enchilada
```
Both verifiers require the session-local artifacts
`oneplus6-ios/work/tmp/window-gpuprobe219.bin`,
`oneplus6-ios/work/tmp/_flash_gpuprobe219.txt` and
`oneplus6-ios/work/log/gpuprobe219/untagged-205050d2/` (SHLOG decode). They
perform NO device writes and NO network access.
