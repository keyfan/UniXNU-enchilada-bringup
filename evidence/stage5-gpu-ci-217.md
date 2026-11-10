# Stage 5 GPU — CI run #217 proves the A630 archive is built AND linked

**Status: CI-VERIFIED (build/link), 2026-11-10, Round 30.** This is a
build-artifact observation, NOT a device measurement and NOT a device
verification. No COM port was present, nothing was flashed, no register was
read, no boot-arg was consumed. The `gpu` area of `porting.toml` therefore
stays `host-verified` (the record's ceiling for non-device evidence).

## What was observed

`oneplus6-ios/scripts/dispatch_ci.py` was run with the standard inputs
(`defconfig=enchilada_defconfig`, `device_repository=keyfan/UniXNU-enchilada-bringup`,
`device_ref=main`, `xcode=26.6`, `skip_setup=true`,
`unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11`) and produced:

- run id **42726356348** (`#217`), URL
  https://github.com/keyfan/unixnu-build/actions/runs/42726356348
- `status=completed`, `conclusion=success`, `run_attempt=1`
- created `2026-11-10T00:11:18Z`, updated `2026-11-10T00:31:32Z`
- job `129532875905 build`: 23 steps, all `success`; step 15 "Build kernel"
  ran 00:20:43Z → 00:30:14Z
- the job log confirms the device repository checkout was
  **`adb15ad5c3043cd9a6c0b72fef0bba151518c575`** — i.e. the Round 29 phase-2
  commit that actually contains `scripts/build-enchilada-adreno.py`, the
  7th `-force_load` and the `boot` monitor wiring. (The run's own
  `head_sha edb0593a…` belongs to the `unixnu-build` workflow repository, not
  to the device repository.)

Three artifacts were uploaded:
`unixnu-enchilada_defconfig-logs` (3,773,732 B),
`unixnu-enchilada_defconfig-kernel` (78,824,561 B),
`unixnu-enchilada_defconfig-payload` (83,019,362 B).

## Two independent verification domains

`scripts/verify-ci-217-adreno.py [run_id]` re-derives both from the uploaded
artifacts (cached under `work/tmp/ci<run>-{logs,kernel}.zip`). Final result:

```
CHECKS 27 PASS 27 FAIL 0
CI217_ADRENO_VERIFY: PASS (27 checks, two independent artifacts agree)
```

### Domain A — the `-logs` artifact (the macOS runner's build stdout)

- 23 `archive-NN-build-*.log` entries; 9 are `build-enchilada-*` (7
  force-loaded + the 2 EXEMPT `storage`/`verified-root` builders, matching
  `scripts/check-enchilada-archives-linked.py`).
- `enchilada/logs/archive-23-build-enchilada-adreno.log` is exactly one line:

  ```
  PASS: libEnchiladaAdreno.a (5 objects, sha256 24b9b6eec7f70413)
  ```

- every one of the 9 `build-enchilada-` logs reports success (`PASS:` or
  `passed`); no `Undefined symbols`, `Traceback (most recent`, `error:` or
  `FAILED` in any of the 23 archive logs.
- `enchilada/logs/kernel.log` is 36,731,151 chars with no
  `Undefined symbols`.

### Domain B — the `-kernel` artifact (the LINKED Mach-O image)

`enchilada/kernel/kernel.macho`, 126,013,440 B, magic `0xfeedfacf`
(Mach-O 64). Byte-level searches:

| needle | count | meaning |
| --- | --- | --- |
| `_enchilada_adreno_poll` | 1 | the weak_import is now DEFINED in the image symbol table |
| `enchilada_adreno_poll` | 2 | symbol + monitor-loop reference |
| `_enchilada_smmu_poll` | 1 | sibling probe still wired, loop intact |
| `enchilada-adreno` | 11 | boot-arg + replay/refusal format strings |
| `enchilada-adreno: %s (replay t=%u attempt=%u)` | 1 | 1 Hz replay (msgbuf-wrap workaround) |
| `an unsafe window is a port bug` | 1 | Round 28 safety-gate refusal string |
| `GPUCC` | 1 | verdict label |
| `cx_on` | 1 | CX domain-state label |
| `libEnchiladaAdreno` | 5 | archive name embedded in the image |
| `adreno_domain_` / `adreno_power_` / `adreno_board_` / `adreno_catalog_` | 6 / 6 / 4 / 7 | the four freestanding C modules are linked |

Symbol-table context (NUL shown as `|`):

```
_encap_detach|_encap_getarg|_encap_register_m_tag|_encaptab|_enchilada_adreno_poll|_enchilada_backlight_poll|_enchilada_backlight_service_probe|_e...
```

The presence of `_enchilada_adreno_poll` in the **linked image** is the
decisive proof that `-Wl,-force_load,{tree}/build/enchilada-adreno/libEnchiladaAdreno.a`
took effect. A merely-present builder with no `force_load` would leave no
such symbol.

## What this DOES and DOES NOT change

**Closes** the Round 29 boundary `limits[3]`, which said "the archive is
WIRED into CI, but no CI run was dispatched … the kernel actually compiling
and linking libEnchiladaAdreno.a is NOT yet observed". It is now observed,
twice: the runner printed the archive PASS line, and the shipped kernel image
contains the linked symbol. The Round 29 `next_action[4]` ("dispatch the
kernel CI … and confirm libEnchiladaAdreno.a builds and links") is done.

**Does not change** any of these, all still true:

- `status` stays `host-verified`. Nothing ran on the phone.
- `CONFIG_XNU_NATIVE_ADRENO=n` — there is still no A6XX_GEN1 backend, no CP
  ring, no GMU/HFI start, no firmware load, no command submission, no Metal
  frontend, no rendered frame. Zero of the six sheng GPU components exist.
- The 3 proprietary signed blobs (`a630_zap.mdt/.b00/.b01`, `GPU_PAS_ID 13`)
  are still absent; the public `a630_sqe.fw` / `a630_gmu.bin` / `a630_zap.mbn`
  are not sufficient for a full Metal path.
- The probe is **default OFF**: `PE_parse_boot_argn("enchilada-adreno", …)`
  with `stage == 1u`. A successful link does not mean the probe ran.
- Round 22/28 power-domain prohibitions are unchanged: before ANY read of
  `0x5000000` / `0x506a000` / `0x5040000`, the vddcx/vdd regulators,
  gpu_cx/gpu_gx GDSCs, 6 clocks, `gcc_gpu_memnoc_gfx_clk` and the GMU must be
  up. The first device action remains the single gated read of the two GPUCC
  GDSC status registers (`0x05091540` CX, `0x05091010` GX).
- `force_load` + `weak_import` means the kernel will not fail if the probe
  never runs; that safety property is intact.

## Honest notes on the verification itself

Three of the initially-written checks were wrong and were corrected before
any conclusion was drawn; they are recorded so the negative results are not
mistaken for build failures:

1. Expecting `build-enchilada-adreno.py` / `AdrenoDomainState.c` /
   `EnchiladaAdrenoProbe.cpp` literals in the logs — the per-archive builder
   prints only its one-line PASS summary, not the command list.
2. Expecting the `-kernel` artifact to contain text build metadata — it
   contains exactly one binary, `enchilada/kernel/kernel.macho`.
3. Expecting all 23 archive logs to start with `PASS:` — corecrypto prints
   `529 kernel C sources; 0 failures`, dwc3 prints `... passed`, and there
   are 9 (not 7) `build-enchilada-` logs because 2 builders are exempt from
   `-force_load`.

Also: the GitHub artifact `/zip` endpoint 302-redirects to an Azure signed
URL; forwarding the `Bearer` token there returns `401 Server failed to
authenticate the request`. `DropAuthOnRedirect` strips `Authorization` when
the redirect leaves `api.github.com`.

## Next (unchanged, strict order)

1. With a device attached, boot with `enchilada-adreno=1` and capture the
   1 Hz replay; the safety gate must pass and the two GPUCC GDSC status words
   must be read (never a cold GPU register read).
2. Only after (1) is device-verified may a real stage-1 kgsl SMMU context be
   programmed (streams must first be made to fault — `ArmSmmu4KBusy=10`).
3. A6XX_GEN1 backend + vendor-signed ZAP chain remain the hard blockers for
   any `device-verified` claim on stage 5.

See also `evidence/stage5-gpu-ci-archive.md` (Round 29 recipe),
`evidence/stage5-gpu-domain-state.md` (Round 28 safe-read design),
`evidence/stage5-gpu-power-domain.md` (Round 22 power gating).
