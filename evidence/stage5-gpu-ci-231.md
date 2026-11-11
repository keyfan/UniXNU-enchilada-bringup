# Stage 5 GPU — CI #231: the round-59 teardown object compiles and links

**ROUND 60 · CI-COMPILE-ONLY · gpu area STAYS `host-verified`**

## Why this run existed

Rounds 56, 57 and 58 each added a pure-C archive unit, and round 59 added a
fifteenth (`AdrenoPowerDownV1.c`). The host suite is a *different* toolchain,
SDK and link step from the real XNU build, so it cannot prove that an object
survives `xcrun ar` into `libEnchiladaAdreno.a` and then through `-force_load`
into `kernel.macho`. Round 59's object had **never** been through a real
kernel build. This run closes that gap.

## Run

| field | value |
|---|---|
| run | `43043000108` (#231), `keyfan/unixnu-build` |
| event | `workflow_dispatch` |
| created | `2026-11-11T14:54:20Z` |
| updated | `2026-11-11T15:17:58Z` |
| conclusion | **success** |
| job | `build`, completed/success, **27 steps**, 0 failed |
| workflow head | `edb0593a9fd6d4568c1d4e9adbb152910dc10603` |
| device repo head consumed | `0e11f1b3e2b469d8efa177300e76879ba1332e5e` (round 59) |
| artifacts | kernel `13866779735` 78,841,773 B · logs `13867193041` 3,724,704 B · payload `13867108119` 82,978,492 B |

## Observed

```
PASS: libEnchiladaAdreno.a (15 objects, sha256 f0030d424fa6fc81)
```

CI #230 was 14 objects `e676a6f1025eb1cc`; the archive grew by exactly the one
round-59 unit.

```
kernel.macho  126,046,208 B
sha256        9de56c6ac592dd53f07bae866bf8379ede11703a617439bcca5f68c10938bb8b
magic         cf fa ed fe   (Mach-O 64)
```

**The kernel is 4,096 B larger than CI #230** (126,042,112 → 126,046,208). That
is the opposite of what happened between #229 and #230, where two objects linked
yet the size did not move because they fit inside the page the twelfth object
had already reached. Here the fifteenth unit needed one more page. The size
delta is therefore *corroborating* evidence this time, but it is still not the
primary proof — a dropped object can also change the size by shifting the link
layout, so the symbol assertions below carry the real weight.

Both logs are present and behave as designed:

- `enchilada/logs/kernel.log` — 36,731,151 B, no `Undefined symbols`, no `Error 1`
- `enchilada/logs/kernel-bootstrap.log` — 37,040,895 B, still contains
  `Undefined symbols for architecture arm64` and `_native_verified_root_probe`

The bootstrap log's undefined-symbol block is the **intentionally failing
baseline** that proves the extra archives are required; it is differential
evidence, not a build failure. Reading it as a regression was a mistake I made
in round 53, so both directions are asserted explicitly now.

## Symbols that must be in the image

All nine exports of the round-59 teardown module are present:

`AdrenoPowerDownV1ShouldRun`, `…InputsValid`, `…StepGuarded`, `…StepStatus`,
`…PlanSize`, `…Plan`, `…Execute`, `…CxOffIsUnverified`, `…Verify`

Plus the round-58 units must not have regressed out:
`AdrenoPowerUpGdscContract`, `AdrenoPowerUpGdscVerify`, `adreno_gdsc_plan_enable`,
`AdrenoKGSLSmmuGateAssess`, `EnchiladaAdrenoProbe`.

The probe is still the only executable stage path: `stage3=plan steps=%u` is
present and `stage3=exec` is absent.

## Two independent verifications

1. `scripts/verify-ci-231-adreno.py` — parses the downloaded artifacts:
   **`CI231_ADRENO_VERIFY: PASS (32 checks, 0 failures)`**.
2. A .NET `Invoke-RestMethod` pass that re-reads the run, job, step list and
   artifact metadata through a different network stack and a different API
   surface: **`DOTNET_VERIFY_CI231: PASS (13 checks, 0 failures)`**.

## Honest boundary

**CI-COMPILE-ONLY.** This run proves the code compiles and links into a real
XNU kernel; it proves nothing about runtime behaviour. No flash was performed,
the standing flash authorization (`m39510`) was **not** exercised, no MMIO was
issued, and `CONFIG_XNU_NATIVE_ADRENO` remains `n`. The gpu area **STAYS
`host-verified`**.

What stage 5 still needs: a real XNU genpd/clock backend, TrustZone PAS
authentication for `GPU_PAS_ID 13`, and the kgsl SMMU stage-1 context — which
round 53 proved must not be attempted until the CX domain is actually powered.

## Chain

```
0e11f1b3  round 59 CI #230 record (device repo head consumed by this run)
<phase 3> this evidence doc + scripts/verify-ci-231-adreno.py
<phase 4> porting.toml gpu area cites the CI #231 record
```
