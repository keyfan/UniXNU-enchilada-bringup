# Stage 5 GPU — CI #228 observed green build/link (ROUND 52, 2026-11-11)

HOST-VERIFIED / CI-COMPILE-ONLY. No device run in this evidence item; the
companion device result is `evidence/stage5-gpu-powerup-plan-device.md`.
The gpu AREA status STAYS `host-verified`.

## What ran

Kernel CI run `42976742096` (#228) on the round-52 phase-1 tree
(`device_ref=main`, head `56d22a3a54df5d0448a62ebfcd32f02186c93bab`),
macOS Xcode 26.6, `defconfig=enchilada_defconfig`,
`unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11`,
`data_seed_run_id=41552968815`, `skip_setup=true`.

## Result (two independent verification stacks, m03546)

1. Raw artifacts (`scripts/verify-ci-228-adreno.py`, 15 checks, 0 failures,
   `CI228_ADRENO_VERIFY: PASS`):
   - `archive-23-build-enchilada-adreno.log` is exactly
     `PASS: libEnchiladaAdreno.a (11 objects, sha256 d2f35797698a7ba2)` —
     the SAME 11 objects as CI #227 but a NEW sha (round-43 `xcrun ar rcs`
     member-metadata nondeterminism; no source change), with NO 10/12-object
     regression;
   - `kernel.macho` 126,038,016 B, Mach-O magic `cffaedfe`,
     sha256 `3a361e3b54170e3ac8fec6cf653f6675380088a52b1866e9be7f9abc03032976`,
     no `Undefined symbols` in kernel.log;
   - 5 kernel byte symbols present: `_enchilada_adreno_poll`,
     `stage3=plan steps=%u`, `AdrenoPowerUpV1Plan`,
     `AdrenoPowerUpV1PreIsReady`, `AdrenoPowerUpV1InputsValid`;
   - no `stage3=exec`.
2. `.NET Invoke-RestMethod` third stack (`work/tmp/_verify_ci228_dotnet.ps1`,
   `DOTNET_VERIFY_CI228: PASS`): `RUN_STATUS=completed`,
   `RUN_CONCLUSION=success`, `HEAD_SHA=edb0593a9fd6d4568c1d4e9adbb152910dc10603`
   (workflow repo, not the device repo), job `build completed/success
   steps=27`, three artifacts `-kernel 78834271 B`, `-logs 3775610 B`,
   `-payload 83029321 B`. The poller `CI228_FINAL: 0 (conclusion=success)`
   independently agrees.

## Honest boundary

An observed CI build + link is NOT a device measurement. `libEnchiladaAdreno.a`
is force-loaded, so the 11 objects and the 5 symbols are present in the shipped
kernel image, but CI executes no GMU code on hardware. The stage-3 probe is
default OFF; it runs only with `enchilada-adreno=3` (see the companion device
evidence, flash #12). `CONFIG_XNU_NATIVE_ADRENO=n` remains; no GMU power-up,
no firmware load, no HFI exchange, no command submission, no rendered image.

Verification chain (remote): `70ce497a` (round 51 p2) -> `56d22a3a`
(round 52 phase 1) -> this record.
