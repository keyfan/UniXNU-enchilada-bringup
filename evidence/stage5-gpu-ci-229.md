# Stage 5 GPU - CI #229 compile + link of the 12-object A630 archive (round 53)

Status: **HOST-VERIFIED (CI compile/link only). NO device run, NO flash.**

This document records that the round-53 module `AdrenoKGSLSmmuGate.c` compiles
and links into the Enchilada XNU kernel in CI. It is a *build* fact, not a
hardware measurement, and it does not change the `gpu` area status, which
stays `host-verified`.

## What ran

CI run `43004328339` (`keyfan/unixnu-build` #229), dispatched from device repo
`main` at `6a3d59bd` (round-53 phase 2 record).

- created `2026-11-11T10:31:29Z`, completed `2026-11-11T10:47:17Z`
- conclusion **success**, job `build` completed with 27 steps
- macOS runner with Xcode 26.6 (the only ABI authority available for this port)

## Observed results

Archive (from `archive-23-build-enchilada-adreno.log`):

```
PASS: libEnchiladaAdreno.a (12 objects, sha256 5a6bb21bcf3d7da5)
```

CI #228 produced 11 objects (`d2f35797698a7ba2`), so the round-53
`AdrenoKGSLSmmuGate.c` is genuinely the 12th archive member.

Kernel:

- `kernel.macho` **126,042,112 B**
- sha256 `b446e81d2b524d326c3968603754ae6ce9c5c6b707e019e9639279bca8ff534e`
- Mach-O magic `cffaedfe`

The size grew by **4096 B (one page)** against CI #228 (126,038,016 B). This is
the first kernel size change since CI #221 and is the honest evidence that the
12th object is linked rather than dropped: an object that fails to link would
leave the image size unchanged, the way rounds 43-52 did for their own
additions.

Byte strings present in the linked image:

- `_enchilada_adreno_poll`
- `AdrenoKGSLSmmuGateAssess`
- `AdrenoKGSLSmmuGateReadPermitted`
- `AdrenoPowerUpV1Plan`
- `stage3=plan steps=%u`

`stage3=exec` is **absent**, confirming the round-53 stage-3 probe remains a
read-only plan and never executes the write path.

## The bootstrap pass must fail

`kernel-bootstrap.log` contains one `Undefined symbols for architecture arm64`
block citing `_mockfs_mountroot`, `_native_legacy_display_probe`,
`_native_verified_root_probe`, `_sheng_pmgr_run` and friends.

That pass is the **intentionally failing baseline**: it links the same XNU
objects *without* the extra archives, so it must report those symbols as
undefined. It is differential evidence that the archive pass is required, not a
build failure. The real build log, `kernel.log` (36,731,049 B), has zero
`Undefined symbols` and zero `Error 1`.

An earlier revision of `scripts/verify-ci-229-adreno.py` scanned every log and
therefore misread this baseline as a regression. The verifier now asserts the
opposite in both directions: `kernel.log` clean, `kernel-bootstrap.log`
failing on exactly those symbols.

## Verification

1. `scripts/verify-ci-229-adreno.py` - **21 checks, 0 failures**. Asserts the
   exact 12-object archive line and sha16, no 10/11-object regression, kernel
   size/sha/magic, the five byte strings, absence of `stage3=exec`, a clean
   `kernel.log`, and the expected bootstrap failure.
2. Independent `.NET` stack (`Invoke-RestMethod`, `DefaultWebProxy = $null`,
   TLS 1.2) - run/job/artifact metadata: `completed`, `success`, job `build`
   with 27 steps, 3 artifacts. This path shares no code with the urllib
   poller.

## Honest boundary

- This is **CI compile/link only**. Nothing here was observed on the hardware.
- No flash was performed and none was needed: the module is a pure decision
  layer with no kernel caller, so booting it would produce no observable
  effect.
- The standing flash authorization from the user (`m39510`, "if needed you can
  go on the device anytime") was therefore **NOT exercised**.
- `gpu` stays `host-verified`. `CONFIG_XNU_NATIVE_ADRENO` remains `n`.
- The device-measured `verdict=cx-DOWN` state is unchanged and still the only
  GPU power fact measured on real hardware.

## What this does not prove

Compiling and linking the gate says nothing about whether reading the kgsl SMMU
at `0x5040000` is safe. The round-53 safety study concluded it is **not**
established safe while `gpu_cx` is collapsed, because upstream enables clocks
and the power domain before touching SMMU registers
(`arm-smmu.c:2196` before `:2200`/`:2255`). That conclusion stands; this CI run
only proves the reasoning is implemented in code that builds.

Remaining strict-order work: a real XNU genpd/clock backend on a **powered**
GMU, the kgsl SMMU stage-1 context at `0x5040000` (only after CX power-up),
TrustZone PAS auth (`GPU_PAS_ID 13`), network/audio/cellular device
verification, an adversarial SMP stress test, CPU hotplug, filesystem-level
persistence, `IOPMPowerSource`/PMI8998, and the ShengPreflight link.

## Verification chain

`fb28eed5` (round 52 phase 2) -> `01fc892b` -> `25cd3100` -> `6a3d59bd`
(round 53 record) -> this CI #229 record.
