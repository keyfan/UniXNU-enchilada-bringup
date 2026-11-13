# Stage 5 GPU — CI #239 (ROUND 84, CI-COMPILE-ONLY)

**Status: HOST-VERIFIED (CI compile/link only).** This is NOT a GPU driver and NOT
a device measurement. No device run, no flash, no MMIO. The standing flash
authorization (`m39510`) was NOT exercised. `CONFIG_XNU_NATIVE_ADRENO` remains `n`.
The `gpu` area of `porting.toml` STAYS `"host-verified"`. It does **not** unblock
stage 5.

## Why this run

CI #238 (round 78) was the last compile proof, and it certified **28** archive
objects. Rounds 79–82 produced only device-sourced read-only evidence and prose.
Round 83 added real code — `AdrenoSmmuCtxWordV1.c`, the stage-1 context WORD
derivation — which had passed a 10-group host suite and a 264-check cross-verifier
but had **never been through the kernel's own clang**.

That gap is exactly the distinction this port keeps insisting on: *host
verification of code is not a CI compile proof.* A file can be correct freestanding
C11 on this host and still fail under XNU's headers, its `-mgeneral-regs-only`
regime, or the archive link. This run closes that gap.

## Run (observed via the REST API)

| item | value |
| --- | --- |
| repository | `keyfan/unixnu-build` (workflow `kernel.yml`) |
| run | `43335691372` (#239), `workflow_dispatch` |
| created | `2026-11-13T00:16:06Z` |
| updated | `2026-11-13T00:32:07Z` |
| status / conclusion | `completed` / `success` |
| workflow head | `edb0593a9fd6d4568c1d4e9adbb152910dc10603` |
| device ref consumed | `main` @ `ab99958fbd7ca20fd32c9e7cba753bb9180318c6` (round-83 phase 4) |
| job | `build`, 27 steps, **0 failed** |

## Artifacts (derived, not guessed)

- **`PASS: libEnchiladaAdreno.a (29 objects, sha256 d140c2aa89bc89aa)`**
  — CI #238 was `28 objects, sha256 f90f1a3267b71762`. The **object count rose by
  one and the sha16 changed**, which together are structural proof that the
  round-83 unit compiled *and* joined the link. A cached artifact cannot do this.
- `kernel.macho` **126,103,552 B**, sha256
  `0be1d77ace20673f4980a0ea149965fb53c855c978fcc6903c18c0398929596a`; CI #238 was
  126,099,456 B (`bdfdc5a05881b663b0450d283bbb015c6f8a590eb6e8aa2fe4ffd287d0d90cc5`),
  so **+4,096 B** —
  the 29th object needed an extra page. Note this is the *opposite* of the
  rounds 43/45/59/78 case, where a new object linked without growing the image.
  Because the delta is observed in both directions across this project's history,
  **size is never the proof**; the object count, the changed archive sha16, the
  kernel sha256 and the symbol inventory carry the weight.
- `prelink.log`: `Experimental candidate: 126103552 bytes; executable bytes
  unchanged; runtime unqualified` — the payload stage accepted the larger image.
- `enchilada/logs/kernel.log` (36,731,153 B) is **CLEAN**: no `Undefined symbols`,
  no `Error 1`.
- `enchilada/logs/kernel-bootstrap.log` (37,040,468 B) is the **intentionally
  failing** baseline (contains both `Undefined symbols for architecture arm64` and
  `Error 1`, and references `_native_verified_root_probe`). It is retained as
  differential evidence: the green log means something only next to the red one.

### What this CI does NOT upload, stated plainly

This workflow uploads **one log per archive** (`archive-23-build-enchilada-adreno.log`),
**not one per object**. There is therefore **no `AdrenoSmmuCtxWordV1.log`**. The
verifier asserts that absence explicitly rather than inventing a per-unit log, and
the proof that the round-83 unit compiled is carried by the object count, the
changed sha16 and the eighteen exports below — not by a file that does not exist.

## What the eighteen exports prove

All 18 `adreno_smmu_ctx_*` functions are asserted present in the linked image:
`id_size_to_bits`, `mair`, `mair0`, `mair1`, `tcr`, `tcr2`, `cbar`, `cba2r`,
`ttbrn`, `ttbrn_checked`, `plan`, `plan_writes`, `round75_writes`, `missing`,
`input_name`, `input_anchor`, `ready`, `verify`.

The same image still carries every earlier unit: the round-77 va-map exports, the
round-75 smmu-stage1 exports (**whose source this round left byte-identical**), the
round-70 cp-ring, round-73 zap-auth, round-74 mdt, round-69 gen1-backend,
round-67 publish, round-66 darwin-kmd and the round-65-and-earlier symbols.

## The boundary that matters

Round 83's module is **derivation only**, and the linked image must show that:

- no `_writel(`, `_readl(`, `_IOServiceOpen`, `_IOConnectCallMethod` symbol,
- no `_qcom_scm_call`, no `adreno_zap_shader_load`,
- `stage3=plan steps=%u` still present, `stage3=exec` still **absent**.

A citation string may *name* a register; a symbol may not. That distinction is
asserted directly, because it is the difference between a contract module and a
driver.

## Honest statement

This run proves the round-83 module is **kernel-clang clean and linkable in a real
XNU image**. It proves **nothing** about the phone: no context was programmed, no
page table allocated, no ASID assigned, no ID2 read, no CX power-up. The admission
table is unchanged at **modeled = 9 / satisfied = 0 / BLOCKED** — a compile proof
of a word-derivation contract cannot satisfy a single prerequisite.

Verifier: `scripts/verify-ci-239-adreno.py` —
`CI239_ADRENO_VERIFY` (see run log for the check count).
