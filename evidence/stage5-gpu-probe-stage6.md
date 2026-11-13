# Round 103 — stage 6: ID0/ID1 probe behind the same gate

**Status: HOST-VERIFIED + CI-VERIFIED (CI #246) + ARMED, NOT YET RUN ON HARDWARE.**
No flash, no reboot, no device register read by stage 6. Everything below is a
compile proof (now including a real XNU kernel link under the kernel own clang)
and static contract checks. The `stage6=id1 ...` row is UNKNOWN until a boot with
`enchilada-adreno=6`.

## What was wrong before this round

Round 94 read `GR0_ID2` (byte `0x28`) behind the round-86 fail-closed gate and
measured `ID2 = 0x00007511` -> `ias = 48`. Round 102 built the context-bank /
ASID allocator (`AdrenoSmmuAsidV1`) but correctly reported `ready() == 0`:
its honest missing-input mask still had two bits set.

| Missing input | Bit | Why it was missing |
|---|---|---|
| `kAdrenoAsidInputId1` | `1u` | `ID1.NUMCB` / `ID1.NUMS2CB` had NEVER been read on this phone, so the allocator had no capacity and no stage-2 reserve. |
| `kAdrenoAsidInputCaller` | `2u` | No kernel caller / domain owner takes a bank. |
| `kAdrenoIommuMissingId0Word` (round-86 contract) | `2u` | `ID0.CTTW` had NEVER been read, so stage-1 words were composed with `coherent_walk(0u, 0u)` — a GUESS that the walk is non-coherent. |

Upstream `arm_smmu_device_cfg_probe()` reads **ID0 (P2:1758) -> ID1 (P2:1853)
-> ID2 (P2:1879)**. Stage 4 joined that sequence at the *last* word. Stage 6
reads the two words that *precede* it, inside the SAME permitted branch, behind
the SAME `adreno_iommu_assess(&gi)` gate, using the SAME mapped kgsl SMMU
window at `0x05040000`. It is the prefix of an already device-proven sequence,
not a new hazard. Counted upstream: there are ZERO `fault`/`BYPASS`/`BUSY`/`wait`
operations between the start of probe and the ID reads (this is why round 100
moved `stream_faults` out of the READ gate and into `admit_program()`).

## The exact edit

`package/EnchiladaAdrenoProbe.cpp` (704 -> 766 lines, pure LF):

- `#include "AdrenoSmmuAsidV1.h"` added inside `extern "C" {`.
- New `char gId1Replay[300];` + `bool gHaveId1Replay` + `setId1Replay()` +
  one `if (gHaveId1Replay)` line in the `gDone` replay branch.
- Boot-arg mask `(stage < 1u || stage > 5u)` -> `(stage < 1u || stage > 6u)`.
- `if (stage >= 6u) { ... }` inserted in the stage-4 **permitted** branch,
  after the ID2 decode, before the stage-4 row is cached:

```cpp
const uint32_t s6id0 = rd32(gSmmuVa, 0x20u);   // GR0_ID0
const uint32_t s6id1 = rd32(gSmmuVa, 0x24u);   // GR0_ID1
__asm__ volatile("dsb sy" ::: "memory");
const uint32_t s6cttw  = adreno_iommu_coherent_walk(s6id0, 0u);
const uint32_t s6numcb   = adreno_asid_capacity_from_id1(s6id1);
const uint32_t s6nums2cb = adreno_asid_s2cb_count_from_id1(s6id1);
struct adreno_asid_bitmap s6bmp = {};
const int s6init = adreno_asid_init_from_id1(&s6bmp, s6id1);
uint32_t s6cbndx = 0xffffffffu;
const int s6alloc = adreno_asid_alloc(&s6bmp, &s6cbndx);
const uint64_t s6ttbr   = adreno_asid_ttbr_asid_field(s6cbndx);
const uint32_t s6ctxidr = adreno_asid_contextidr(s6cbndx);
const uint32_t s6s2cr   = adreno_asid_s2cr_cbndx(s6cbndx);
const uint32_t s6cbar   = adreno_asid_cbar_offset(s6cbndx);
struct adreno_iommu_stage1_words s6w = {};
const int s6wok = adreno_iommu_stage1_words(id2, ver, s6cttw, &s6w) == 0;
// one labelled stage6=id1 row -> snprintf -> IOLog -> setId1Replay(s6Row)
```

Exactly three `rd32(gSmmuVa, ...)` dereferences now exist: ID2 `0x28` (stage 4,
once), ID0 `0x20` (stage 6, once), ID1 `0x24` (stage 6, once). No second
mapping, no second gate, no store through a pointer, no libc helper, every cast
still `volatile`. All new locals carry the `s6` prefix to survive `-Wshadow`.

### `fw_says_coherent = 0` is a fact, not a guess

`arch/arm64/boot/dts/qcom/sdm845.dtsi` (pinned Linux v6.12) defines
`adreno_smmu: iommu@5040000` at `:4903` with `compatible = "qcom,sdm845-smmu-v2",
"qcom,adreno-smmu", "qcom,smmu-v2";`, `reg = <0 0x05040000 0 0x10000>;`,
and **no `dma-coherent` property anywhere in the file**. The round-86 contract
`adreno_iommu_coherent_walk()` is
`reg = !!(id0_raw & ID0_CTTW); if (fw_says_coherent) return 1; return reg;`
(P2:1799-1805), so with firmware flag 0 the measured `ID0.CTTW` bit alone
decides. Stage 4's `coherent_walk(0u, 0u)` was therefore a placeholder; stage 6
re-composes the real TCR/TCR2 with `adreno_iommu_stage1_words(id2, ver, s6cttw, &s6w)`.

### The ASID contract extension (no new C unit)

`AdrenoSmmuAsidV1.h/.c` gained (rather than a 31st archive object, which would
have cascaded `== 29/30/31` counter drift through 8+ verifiers):

- `#define kAdrenoAsidId1Nums2cbMask 0x00ff0000u` and
  `kAdrenoAsidId1Nums2cbShift 16u` — pinned to
  `arm-smmu.h:62 ARM_SMMU_ID1_NUMS2CB GENMASK(23,16)`.
- `uint32_t adreno_asid_s2cb_count_from_id1(uint32_t id1_raw)` — the
  FIELD_GET-equivalent `(id1_raw & mask) >> shift`.
- `int adreno_asid_init_from_id1(struct adreno_asid_bitmap *b, uint32_t id1_raw)`
  — decodes BOTH fields of the one ID1 word and **delegates** to
  `adreno_asid_init(b, numcb, nums2cb)`, so the upstream
  `arm-smmu.c:1867-1870` "impossible number of S2 context banks!" refusal
  (`num_s2 >= num_cb`) lives in exactly one place.
- Six new `adreno_asid_verify()` self-checks (52 -> **58**): for
  `id1 = 0x00020008u` (NUMS2CB=2, NUMCB=8) assert s2cb=2, cap=8,
  `init_from_id1`=0, `b.capacity==8 && b.start==2`, first-fit `alloc` yields
  `cbndx==2`, and `init_from_id1(&b, 0x00090008u)` is REFUSED (S2=9 > NUMCB=8).
- `missing_inputs()`, `enum kAdrenoAsidInputAll = 3u`, `ready()` and the EIGHT
  C++ test group names are deliberately UNCHANGED. A register read does not
  create a bank owner, so **ready() stays 0**.

## Verification (all host, reproducible)

| Check | Result |
|---|---|
| `check-enchilada-adreno-probe.py --cc ...clang.exe --cxx ...clang++.exe --out work/tmp/r103_probe_out` | **18/18** — `EnchiladaAdrenoProbe.cpp strict C++17 arm64 (0)`, `unexpected=[]`, casts `['volatile uint32_t *','volatile uint8_t *']`, no store |
| `verify-enchilada-probe-stage3.py` | **40/40** — mask now `1..6` |
| `verify-enchilada-probe-stage4.py` | **49/49** — `rd32(gSmmuVa` count `== 3`, ID0/ID1/ID2 each once |
| `verify-enchilada-probe-stage5.py` | **40/40** — ordering `i_s5_block < i_s4_block` preserved |
| `verify-enchilada-smmu-asid.py` | **99/99** — two new #defines, two new APIs, 8 test groups, 30 archive units (unchanged) |
| `verify-enchilada-probe-stage6.py` (new) | 86 checks — mask, three reads, same-gate/same-map, DTS `dma-coherent` absence, coherent_walk, ASID tuple, replay, READ-ONLY, honest boundary |
| `check-enchilada-adreno-host.py --cc ... --cxx ...` | **31/31 suites** (the `adreno-smmu-asid-v1-test` suite with `count == 58u` passes; ASan/UBSan unavailable on llvm-mingw, script degrades to `without sanitizers`) |

`board.toml:375` now arms `enchilada-adreno=6`; the argument is the same 18
bytes, so the joined command line stays **970/1000**. `porting.toml` carries the
ROUND 103 finding. `build-enchilada-adreno.py` gained the missing
`'AdrenoSmmuAsidV1'` stem in the `source_sha256` provenance tuple (a round-102
gap; no verifier asserted it).

Lessons captured: the probe insertion used a Python line-anchor script, not a
whitespace-fragile `edit` (three `old_string was not found` failures from 29-vs-30
leading spaces). `check-enchilada-adreno-host.py` needs explicit `--cc/--cxx`
on this host (`shutil.which('clang++')` returns nothing). The stage-6 verifier's
first "no second mapping" check wrongly counted the whole stage-4 block (which
legitimately has TWO pre-existing maps: pre-gate + round-100 fallback) — it was
narrowed to the stage-6 block itself.

## CI #246 — compile/link proof (run 43438540052, success)

Round 102 pushed the ASID unit to remote main but **never** sent it through CI;
round 103 then extended that same unit. Host `-Werror` is not a compile proof
under XNU's own clang / `-mgeneral-regs-only` archive link, so the change was
dispatched (`dispatch_ci.py`, device_ref=main, unixnu_ref
`8ddccb1e...`, xcode=26.6, data_seed_run_id=41552968815, skip_setup=true).

* Run `43438540052` (#246), `workflow_dispatch`, **conclusion success**, device
  head `b04783074d995cf168f7b00ecfa2090c342bae69` (the push that carried all 13
  staged round-103 files).
* `archive-23-build-enchilada-adreno.log`:
  `PASS: libEnchiladaAdreno.a (31 objects, sha256 901a60db440b9f72)`.
  CI #245 was `30 objects, sha256 97b3e86684d81c02`. The **+1 object and changed
  sha16** are structural proof the ASID unit rebuilt and joined the link — a
  cached artifact cannot produce that pair. Round 103 itself added **no** new C
  unit, so `30 -> 31` is entirely round 102, exactly as the no-new-unit design
  intended.
* `kernel.macho` = 126,128,128 B, Mach-O 64 magic `cf fa ed fe`, sha256
  `ec629ef24876b6e9e93fb7f2197dccc09325a9e444ecc71afe1abc1f96884318`.
* `kernel.log` (36,731,089 B) is clean: `Undefined symbols` = 0, `Error 1` = 0.
  The 16 `error:` / 8 `FAILED` / 11 `ld:` hits are upstream XNU warning context
  and string literals (`"kcdata error: %d"`, `NETEM_LOG("| error: ...")`,
  `ld: warning: -new_linker is obsolete`), not build errors.
  `kernel-bootstrap.log` remains the intentionally failing baseline.
* `scripts/verify-ci-246-adreno.py` (new, 90/90) asserts from the downloaded
  artifacts: 17 `adreno_asid_*` exports (including the two NEW ones), 18
  round-83 ctx-word exports, 24 retained ID2/probe/power-up **CamelCase**
  exports, the `stage6=id1 id0=%08x id1=%08x cttw=%u` / `stage4=id2` / `stage5=run`
  row formats IN the image, no `writel`/`readl`/`IOServiceOpen`/
  `IOConnectCallMethod`/`qcom_scm_call`/zap-shader executable symbol, and no
  `stage6=exec` / `stage3=exec` path.

Two tooling fixes were needed on this host and are NOT part of the device repo:
`push_via_api.py` gained a transient-422 retry (a 513-file tree timed out once),
and `dispatch_ci.py` gained SSL/URLError retry (the watch job died on
`ssl.SSLEOFError`). The 78.9 MB kernel artifact was fetched with the existing
8-connection ranged downloader (`r99_parallel_dl.py` pattern, ~200 KB/s).

Full sweep after CI: **68/68 verifiers** (67 pass in `r103_runall.py` plus the
by-design `verify-enchilada-gmu-caller-v1-record.py` with its correct 40-hex
argument `ead5834edce79b4b3ceb5108d6dca6e57496ee90` -> 28/28), and the 31/31
host C++ suites still pass. `porting.toml` carries both the HOST-VERIFIED and
the CI-VERIFIED ROUND 103 findings.

## Honest boundary

* NO device run, NO flash, NO reboot, NO register read by stage 6. The code is
  ARMED; the row is UNKNOWN until a boot with `enchilada-adreno=6`.
* `kAdrenoAsidInputId1` is **not** cleared by reading ID1: it is an honest
  derived mask whose `kAdrenoAsidInputCaller` bit is still set — no domain owner
  takes the bank. `ready()` remains 0. A register word is not a domain.
* Still absent: (I1) a populated 4 KiB-granule LPAE page table for the measured
  TTBR1 geometry / `ias=48`; (I2) a real kernel caller owning the bank; (I3)
  streams made to fault (round 53 `Busy=10`, firmware BYPASS handoff). Nothing
  in stage 6 programs a context bank or writes a single SMMU register.
* Admission table UNCHANGED: `modeled=9 / satisfied=0 / BLOCKED`.
  `CONFIG_XNU_NATIVE_ADRENO=n`; A630 client GPU windows stay all-zero; the gpu
  area STAYS host-verified; this does NOT unblock stage 5 and does NOT make
  Adreno participate in rendering.
* Next device action: read the `stage6=id1` row with the three-layer
  reboot-to-TWRP guarantee verified first and re-armed immediately after.
  Next code artifact: the LPAE page table, then a real bank owner.
