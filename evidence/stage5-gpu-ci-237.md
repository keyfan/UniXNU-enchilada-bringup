# Stage 5 GPU - CI #237 (ROUND 76, CI-COMPILE-ONLY)

**Status: HOST-VERIFIED (CI compile/link only).** This is NOT a GPU driver, NOT a
device measurement. No device run was performed, no flash was performed, the
standing flash authorization (`m39510`) was NOT exercised, no MMIO was issued, and
`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area of `porting.toml` STAYS
`"host-verified"`. It does **not** unblock stage 5.

## Why this run

Three archive units had been added since the last green build and **none had ever
been compiled by the kernel's own clang**:

| unit | round | what it is |
| --- | --- | --- |
| `AdrenoZapAuthV1` | 73 | the PAS/ZAP call shape and the eight-step chain inventory |
| `AdrenoMdtV1` | 74 | the XNU-side MDT/ELF32 parser |
| `AdrenoSmmuStage1V1` | 75 | the ARM SMMU v2 stage-1 context word model |

CI #236 proved 24 objects. Every host test since round 73 has been evidence about
the **host** toolchain only. This run is the ABI authority for those three units,
and the first time they have been through a real XNU build.

## Run (observed via the REST API)

| item | value |
| --- | --- |
| repository | `keyfan/unixnu-build` (workflow `kernel.yml`) |
| run | `43242359273` (#237), `workflow_dispatch` |
| created | `2026-11-12T14:56:05Z` |
| updated | `2026-11-12T15:13:47Z` |
| status / conclusion | `completed` / `success` |
| workflow head | `edb0593a9fd6d4568c1d4e9adbb152910dc10603` |
| device ref consumed | `main` @ `30b39fa1f8644bf9d1c07b8a0bcc7e857edf622a` (round-75 phase 4, before the dispatch) |
| job | `build`, 27 steps, 0 failed |

Inputs were the same hardcoded set used for rounds 59-75.

## Artifacts (derived, not guessed)

- `PASS: libEnchiladaAdreno.a (27 objects, sha256 a9b461e4f1378731)`
  (CI #236 was `24 objects, sha256 59e80bd40870c8d8`).
- `kernel.macho` **126,099,456 B**, sha256
  `40d6fe4a5fdde2cdeaf0062a3c016346df01bf3d620dff339e60cc76c450918e`, magic
  `cf fa ed fe` (Mach-O 64). Changed vs CI #236's
  `9bd2d93910192a9c5d24c9aa9f297ebec85ce934674d50e5c38eaff9719b609e` → a genuine
  relink. Delta vs CI #236: **+24,576 B** — three units this time, so the image
  did grow. Observed, not asserted: rounds 43/45/59 established that an extra
  object can link without growing the image, so the load-bearing proof is the
  object count, the changed sha256 and the symbol inventory.
- `enchilada/logs/kernel.log` is CLEAN (36,731,014 B): no `Undefined symbols`, no
  `Error 1`.
- `enchilada/logs/kernel-bootstrap.log` (37,041,126 B) is the INTENTIONALLY
  failing baseline — retained as differential evidence.
- the archive build log is `PASS: libEnchiladaAdreno.a (27 objects, sha256
  a9b461e4f1378731)` and nothing else, so the object COUNT is the proof.

## Symbol inventory (all present in the linked image)

35 exports across the three new units:

- **11 round-73 zap-auth**: `adreno_zap_scm_cmd`, `adreno_zap_scm_op_name`,
  `adreno_zap_pas_id`, `adreno_zap_chain`, `adreno_zap_present_count`,
  `adreno_zap_step_name`, `adreno_zap_step_anchor`, `adreno_zap_auth_ready`,
  `adreno_zap_result_semantics`, `adreno_zap_unsupported_is_quiet`,
  `adreno_zap_auth_verify`.
- **12 round-74 mdt**: `adreno_mdt_header`, `adreno_mdt_phdr`,
  `adreno_mdt_phdr_valid`, `adreno_mdt_phdr_relocatable`,
  `adreno_mdt_phdr_is_hash`, `adreno_mdt_get_size`, `adreno_mdt_segment_name`,
  `adreno_mdt_metadata`, `adreno_mdt_filesz_sum`, `adreno_mdt_ready`,
  `adreno_mdt_loader_available`, `adreno_mdt_verify`.
- **12 round-75 smmu-stage1**: `adreno_smmu_sctlr_stage1`,
  `adreno_smmu_sctlr_stage1_be`, `adreno_smmu_sctlr_enabled`,
  `adreno_smmu_s2cr_addr`, `adreno_smmu_s2cr_value`,
  `adreno_smmu_s2cr_type_for_state`, `adreno_smmu_stage1_plan`,
  `adreno_smmu_stage1_missing`, `adreno_smmu_stage1_input_name`,
  `adreno_smmu_stage1_input_anchor`, `adreno_smmu_stage1_ready`,
  `adreno_smmu_stage1_verify`.

Plus the 11 round-70 cp-ring, 19 round-67 publish, 8 round-66 darwin-kmd and 15
earlier unit names — no regression.

## Independent symbol-table check (third verification)

`llvm-nm --defined-only --format=just-symbols` over both linked images:
CI #236 = **70,214** defined symbols, CI #237 = **70,261** — exactly **+47**, and
`LOST_FROM_236 = 0`. The 47 new names are precisely the three units' exports plus
the round-72 additions that had also never been through this check (cp-ring's
`buffer_bytes`, `buffer_dwords`, `can_encode_packet`, `me_init_message`,
`me_init_message_matches`, `pack_rb_cntl`, `parity`, `rb_cntl_fields`,
`rb_cntl_value`, `type7_header`, `undefined_open`, `unpack_field`), i.e. every
public function added since #236 appears exactly once.

## A verifier defect this run exposed — and it is the interesting part

The verifier's boundary check was `'qcom_scm' not in text`, asserting that no PAS
plumbing reached the image. It **failed**, and the failure was correct to flag but
wrong in its premise: `qcom_scm` now appears at byte 1302126 of the image only
inside a **citation string** written by round 73 —
`"adreno_gpu.c:156 qcom_scm_pas_auth_and_reset(13); shape here, transport absent"`.
That is a *reference* to why PAS is unimplemented, not an *implementation* of it.

CI #236's image had no such string (it did not contain the zap-auth unit), so the
check had never been exercised against prose. The check now distinguishes a
citation from a symbol by requiring the callable forms `_qcom_scm_` (the Mach-O
symbol-table prefix) and `qcom_scm_call(` to be absent — which is the assertion
that was intended all along. Recording this because a boundary check that fires on
documentation text is exactly the kind that gets "fixed" by deleting the document.

## Two independent verifications

1. `scripts/verify-ci-237-adreno.py` — artifact-based, derives the archive sha16
   and kernel size/sha256 from the downloaded artifacts (125 checks, 0 failures):
   27 objects, sha16 changed, clean kernel.log, dirty bootstrap, Mach-O magic,
   changed sha256, all 35 new + 11 + 19 + 8 + 15 symbols, the corrected PAS/SAP
   boundary, and the stage-3 invariants.
2. The .NET `Invoke-RestMethod` audit over run/job/step-list/artifact metadata
   (independent network stack): `completed/success`, run 237, build job 27 steps
   0 failed, 3 artifacts all non-empty.

## Honest boundary

CI COMPILE/LINK ONLY. No device run, no flash, `m39510` NOT exercised,
`CONFIG_XNU_NATIVE_ADRENO` remains `n`, NOT a GPU driver, no Turnip dylib, the four
hard blockers of `docs/porting/gpu.md` section 1 remain open. The A630 client GPU
windows stay all-zero. The `gpu` area STAYS `"host-verified"`. It does **not**
unblock stage 5.

**The admission table does not move because of this run** — still modeled=8,
satisfied=0, verdict BLOCKED. Compiling three contract modules proves they are
kernel-clang clean and linkable; it satisfies nothing on the phone.

Chain: `4c0e5e09` (round 74 phase 4) -> `56d65299` (round 75 phase 3) ->
`30b39fa1` (round 75 phase 4) -> phase 3 (this evidence + verifier) -> phase 4
(porting.toml CI record).
