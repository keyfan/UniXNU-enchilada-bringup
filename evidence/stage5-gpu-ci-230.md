# Stage 5 GPU — CI #230: the 13th and 14th A630 archive units build and link (round 59)

**Status: CI-COMPILE-ONLY. The gpu area STAYS `host-verified`.**

No flash. No device run. `m39510`'s standing flash authorization was **not**
exercised this round, because nothing in this build needs hardware to be
interesting: the claim under test is "does the code compile and link inside a
real XNU kernel", and CI answers exactly that.

## Why this run

Rounds 56, 57 and 58 each added a pure-C module to the A630 backend
(`AdrenoGdscDecode`, `AdrenoPowerUpGdsc`, plus the round-57 sequence extension).
Round 59 added `AdrenoPowerDownV1`. **None of the round-56/57/58 additions had
ever been through an actual kernel build** — the host suite compiles them with
the local clang for aarch64-apple-darwin, which is a different toolchain, a
different SDK and a different link step from the XNU kernel.

CI #230 was dispatched on the **round-58 head** (`b4579fd5`), so it proves the
13th and 14th units. Round 59's 15th unit needs a later run and is **not**
covered here — the verifier asserts `AdrenoPowerDownV1` is *absent* from this
kernel precisely so the boundary cannot be overstated.

## Run

| field | value |
|---|---|
| run id | `43034488610` |
| event | `workflow_dispatch` |
| created | `2026-11-11T14:01:28Z` |
| updated | `2026-11-11T14:26:22Z` |
| status / conclusion | `completed` / `success` |
| job | `build`, `completed` / `success`, 27 steps |
| workflow head | `edb0593a9fd6d4568c1d4e9adbb152910dc10603` (unixnu-build) |
| device head consumed | `b4579fd5` via `device_ref=main` |

Inputs were the standard set: `device_repository=keyfan/UniXNU-enchilada-bringup`,
`defconfig=enchilada_defconfig`, `xcode=26.6`,
`data_seed_run_id=41552968815`, `skip_setup=true`,
`unixnu_ref=8ddccb1e42d0063a2e613f897155c83994f60b11`.

## Observed artifacts

```
PASS: libEnchiladaAdreno.a (14 objects, sha256 e676a6f1025eb1cc)
```

- **14 objects** — CI #229 reported 12 (`5a6bb21bcf3d7da5`). The count matches
  the two units added in rounds 57 and 58.
- archive sha256 prefix `e676a6f1025eb1cc` ≠ the CI #229 `5a6bb21bcf3d7da5`,
  so this is a genuine rebuild, not a cache hit.
- `kernel.macho` **126,042,112 bytes**, sha256
  `afcff91011a51a94c329860607a411f34cee3aa474bdc612fa18847a155dd2c0`,
  Mach-O 64 magic `cf fa ed fe`.
- **Honest note on size**: the image is *the same size* as CI #229 (which had 12
  objects) even though two more objects linked in. The sha256 differs, which
  proves a real relink; the equal size simply means both new objects fit inside
  the page the 12th object had already pushed the image into. This is exactly the
  kind of thing that would be invisible if only the size were checked — which is
  why the verifier asserts the sha256 **and** the per-symbol presence below.

### Symbols that must be in the linked image

`AdrenoPowerUpGdscContract`, `AdrenoPowerUpGdscVerify`,
`AdrenoPowerUpGdscGuardMatchesWrites`, `AdrenoPowerUpGdscDisableIsUnverified`,
`adreno_gdsc_plan_enable`, `adreno_gdsc_plan_disable`,
`adreno_gdsc_verify_gx_enable_writes_nothing`, `AdrenoKGSLSmmuGateAssess`,
`EnchiladaAdrenoProbe` — all present. `AdrenoPowerDownV1` — correctly absent.
`stage3=plan steps=%u` survives; `stage3=exec` is not compiled in, so the probe
remains observation-only.

### The two logs, and why only one of them may be clean

- `enchilada/logs/kernel.log` — 36,731,152 B, **zero** `Undefined symbols`,
  **zero** `Error 1`. This is the real build.
- `enchilada/logs/kernel-bootstrap.log` — 37,041,128 B, and it *does* contain
  `Undefined symbols for architecture arm64` plus `_native_verified_root_probe`.

That second file is the **intentionally failing baseline pass**, and its failure
is *evidence*: it shows the kernel cannot link without the extra archives, which
is the differential justification for force-loading them. A verifier that scans
"all logs" for `Undefined symbols` will therefore misread a correct build as a
regression — this bit the previous round, so both directions are now asserted.

## Two independent verifications

1. **Artifact domain** — `scripts/verify-ci-230-adreno.py` →
   `CI230_ADRENO_VERIFY: PASS (27 checks, 0 failures)`.
2. **API domain** — `_verify_ci230_dotnet.ps1` (.NET `Invoke-RestMethod`) →
   `DOTNET_VERIFY_CI230: PASS (10 checks, 0 failures)`: run completed/success,
   job `build` success with 27 steps, 3 artifacts, kernel artifact 78,840,808 B.

## Honest boundary

This proves the A630 modules *compile and link*. It does not prove they run, and
it does not touch hardware. The GPU still cannot be powered up: CX is measured
DOWN, the TrustZone PAS path for `GPU_PAS_ID 13` is unimplemented, and the kgsl
SMMU stage-1 context is deferred until CX is up. `CONFIG_XNU_NATIVE_ADRENO`
remains `n`. The gpu area stays `host-verified`.
