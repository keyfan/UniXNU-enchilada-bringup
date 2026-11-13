# Stage 6 on the phone: ID0/ID1/ASID were MEASURED, and ID0.CTTW corrected the TCR

**Round 103, 2026-11-13 — DEVICE-VERIFIED.**
Build: CI #246 (`run 43438540052`), device head `b04783074d995cf168f7b00ecfa2090c342bae69`
for the code, pushed through `dc9a1505463d52a0d664ae8554f81cf3a7c957da` for the CI
evidence. Payload `enchilada/payload/payload.img` = 150,994,944 B, sha256
`adb6f89e8bcb56b9d94bff3644fdac190d9a798ee6843cad7e97f38d3d911c44`, boot arg
`enchilada-adreno=6`. Device `27f4f45d`, TWRP 3.4.0-3.

This is the first time the stage-6 probe actually ran on the silicon. It closed the
two *device-sourced* register inputs that round 102's ASID allocator and round 86's
ID0/coherent-walk contract were missing, and it produced one genuinely new fact that
changes the geometry of any later work: **this SMMU is coherent**.

## What was missing before this run

| Input | Contract | Before | After |
| --- | --- | --- | --- |
| `ID1.NUMCB` / `NUMS2CB` | `kAdrenoAsidInputId1` (`AdrenoSmmuAsidV1.h`) | never read on this phone | `NUMCB=5`, `NUMS2CB=0` |
| `ID0.CTTW` | `kAdrenoIommuMissingId0Word` (`AdrenoIommuProbeV1.h`) | never read on this phone | `CTTW=1` |

`ready()` is *derived* from a missing-inputs mask and is still 0. Reading the two
registers does **not** flip that derived bit: no context bank was written, no real
owner allocated a bank, no LPAE page table was built, no stream was taken out of
BYPASS. See the HONEST BOUNDARY below.

## The measured rows (verbatim, re-read from the live log window)

The window is `userdata = /dev/block/sda17`, last `8192 x 4096 = 32 MiB`,
`skip = 105727448/8 - 8192 = 13207739`, dumped to `/tmp/w.bin` on the phone and
pulled with `adb pull` (never through a PowerShell `dd | adb shell` pipe, which
would re-encode to UTF-16 and truncate). The pulled file is plain UTF-8. Five unique
`enchilada-adreno` records, in order:

```
[ 47.113695]: enchilada-adreno: rc=0 GPUCC=5090000 size=9000 cx=00005ff4 gx=00088400 cx_on=0 gx_on=0 gx_off=1 verdict=cx-DOWN steps_left=9
[ 47.390496]: enchilada-adreno: stage5=run rc=0 up=0 up_fail=5 precheck=0 ops=32 w=9 r=14 cx_before=00005ff4 cx_after=80001fe0 cx_on_after=1
[ 47.390886]: enchilada-adreno: stage6=id1 id0=48016605 id1=20008005 cttw=1 numcb=5 nums2cb=0 cap=5 init=0 alloc=0 cbndx=0 ttbr=0 ctxidr=00000000 s2cr=00000000 cbar=00000000 words_ok=1 tcr=b5100080 tcr2=00038011
[ 47.390906]: enchilada-adreno: stage4=id2 id2=00007511 ver=2 ias_ok=1 ias=48 ipa=36 pa=36 oas2=36 ptfs4k=1 vmid16=0 words_ok=1 plan_ok=1 tcr=a0100080 tcr2=00038011
[ 47.390921]: enchilada-adreno: rc=0 GPUCC=5090000 size=9000 cx=00005ff4 gx=00088400 cx_on=0 gx_on=0 gx_off=1 verdict=cx-DOWN steps_left=9
```

`stage6=id1`, `stage4=id2`, `stage5=run` each occur exactly once in the raw bytes.
The stage-6 row is a standalone `\n`-terminated record; the bytes that follow are
ordinary XNU startup (SMP cpu1-7 online, APFS root `DawnUpdate21H461...`
mount-complete, `NativeUfs: OS volume ... ready`), which proves this boot really
reached XNU and flushed the log to the window — it is not a scratch buffer.

## ID0 — the important finding: the SMMU is coherent

`ID0 = 0x48016605`. `ARM_SMMU_ID0_CTTW = BIT(14)` (`arm-smmu.h:54`), and
`(ID0 >> 14) & 1 == 1`, so `cttw_reg` is set. `sdm845.dtsi` has **no** `dma-coherent`
on `adreno_smmu: iommu@5040000` (`:4903`), so `fw_says_coherent = 0`. The upstream
rule `arm-smmu.c:1799-1805` (`if (cttw_fw || cttw_reg)`) therefore makes
`coherent_walk = 1` purely from the hardware bit. The device row agrees: `cttw=1`.

This matters because round 101's stage-4 row computed its TCR with
`adreno_iommu_stage1_words(id2, ver, adreno_iommu_coherent_walk(0u, 0u), &w)` —
ID0 had not been read yet, so it fed `id0_raw = 0` and, with fw=0, assumed
**non-coherent**. That is why the stage-4 row says `tcr=a0100080`. Stage 6 re-reads
ID0 and recomputes with the real `cttw = 1`, giving `tcr=b5100080`.

`AdrenoSmmuCtxWordV1.c:106-121` warns that the two branches are **not** free
variations — "picking one without IDR0.CTTW would lie about the silicon":
`coherent_walk != 0` selects `sh = SH_IS(3)` / `rgn = RGN_WBWA(1)`;
`coherent_walk == 0` selects `sh = SH_OS(2)` / `rgn = RGN_NC(0)`. A scratch C
program compiled against the real `AdrenoSmmuCtxWordV1.c` confirmed:

```
coh ttbr1 ias48 = b5100080      (real silicon, stage 6)
non ttbr1 ias48 = a0100080      (round-101 placeholder, stage 4)
coh ttbr0 ias48 = 00803510
```

**`a0100080` is a placeholder, not this part's configuration. Any later LPAE page
table, TCR, or context-bank programming must use `coherent_walk = 1` /
`b5100080`, not the stage-4 value.** `tcr2=00038011` is unchanged between the two
rows (PASIZE=1 / 36-bit, SEP=0x7, AS=1).

## ID1 — context-bank geometry

`ID1 = 0x20008005`, decoded against `arm-smmu.h:60-63`:

- `NUMCB = ID1 & GENMASK(7,0) = 5` → `num_context_banks = 5` (`arm-smmu.c:1866`)
- `NUMS2CB = FIELD_GET(GENMASK(23,16), ID1) = 0` → `num_s2_context_banks = 0`
  (`arm-smmu.c:1865`). No stage-2 reserve, so the stage-1 first-fit `start` is 0.
- `NUMPAGENDXB = FIELD_GET(GENMASK(30,28), ID1) = 2` → `numpage = 1 << (2+1) = 8`
  (`arm-smmu.c:1857`)
- `PAGESIZE = BIT(31) = 0` → `pgshift = 12`, i.e. 4 KiB (`arm-smmu.c:1854`)

`NUMCB=5` is far below both ceilings (`ID1.NUMCB` <= 256, `ARM_SMMU_MAX_CBS = 128`,
`arm-smmu.h:253`), so the 16-bit `TTBRn_ASID` field is never a bottleneck here.

## The ASID tuple, re-derived and re-confirmed

`AdrenoSmmuAsidV1` was compiled with the real `clang` and run against the measured
`ID1 = 0x20008005`; every field matches the device row:

```
s2cb_count_from_id1 = 0      capacity_from_id1   = 5
init_from_id1       = 0      b.capacity          = 5    b.start = 0
alloc               = 0      cbndx               = 0
ttbr_asid_field     = 0      contextidr          = 00000000
s2cr_cbndx          = 00000000                  cbar_offset = 00000000
```

`cap = min(5, 128) = 5`; `init_from_id1` delegates to `adreno_asid_init(b, 5, 0)`,
which accepts because `num_s2 (0) < num_cb (5)` (the `arm-smmu.c:1867-1870`
"impossible number of S2 context banks!" refusal is not triggered); first-fit
allocates `cbndx = start = 0`; `cfg->asid = cfg->cbndx` (`arm-smmu.c:795`) so the
ASID field is 0. The device row (`cap=5 init=0 alloc=0 cbndx=0 ttbr=0
ctxidr=00000000 s2cr=00000000 cbar=00000000 words_ok=1`) agrees field-for-field.
The verifier `verify-enchilada-stage6-device.py` re-derives all of this in Python
independently of the module, so a change in either place is caught.

## Safety preconditions of the run

Recorded because the phone cannot boot Android (userdata is the APFS root image) and
a fall into Android wedges it until a manual key-combo back to TWRP:

- `oneplus6-ios/scripts/check-reboot-to-twrp.py` passed **before** any device write:
  `REBOOT_TO_TWRP_GATE: PASS (3 checks, 0 failures)` — BCB == `boot-recovery`,
  `/sbin/twrp` + `/twres` present, `boot_a` md5 == `ca8ddd7a5af345636cad8c5bf645fee2`.
- `fastboot flash system_b <payload.img>` then `fastboot boot work/out/boot-r16.img`
  (one `fastboot boot` per session). Booted to the UniXNU menu / magenta bar, then
  came back to TWRP automatically.
- `oneplus6-ios/scripts/reboot-to-twrp.py --arm-bcb-only` re-armed the BCB after
  the run (`PASS: misc command = 'boot-recovery'`). The BCB is ONE-SHOT; it must be
  re-armed before the next reboot. `fastboot reboot recovery` is the reliable
  runtime instruction that does not depend on a persistent marker.
- `boot_a` TWRP backup `oneplus6-ios/work/tmp/r86-backup/boot_a.twrp.img` remains
  byte-identical (md5 `ca8ddd7a5af345636cad8c5bf645fee2`).

## Verification

| Gate | Result |
| --- | --- |
| `check-enchilada-adreno-probe.py` (strict C++17 arm64) | 18/18 |
| `check-enchilada-adreno-host.py` | 31/31 suites |
| `verify-enchilada-probe-stage3/4/5/6.py` | 40 / 49 / 40 / 86 |
| `verify-enchilada-smmu-asid.py` | 99/99 |
| `verify-ci-246-adreno.py` (archive 31 objects `901a60db440b9f72`, kernel.macho sha256 `ec629ef2...`) | 90/90 |
| compiled `AdrenoSmmuAsidV1` + `AdrenoSmmuCtxWordV1` scratch runs | tuple + TCR confirmed |
| `verify-enchilada-stage6-device.py` (this round) | live-window re-read + independent re-derivation |

## HONEST BOUNDARY

This run **only read three GR0 registers** (`ID0 @ 0x20`, `ID1 @ 0x24`,
`ID2 @ 0x28`) behind the same fail-closed stage-4 gate, and decoded them. It does
**not** prove that a stage-1 context works. `ready()` is still derived from the
missing-inputs mask and is still 0: `kAdrenoIommuMissingId0Word` /
`kAdrenoAsidInputId1` were *measured*, but no context bank was programmed, no real
owner took a bank, no LPAE page table was filled, and no stream was moved out of
BYPASS (round 53 `Busy=10` still BYPASS). The admission table is unchanged:
**modeled = 9 / satisfied = 0 / BLOCKED**. `CONFIG_XNU_NATIVE_ADRENO` remains `n`,
the gpu area is host+CI+device-verified for *reads only*, and this does **not**
unblock stage 5 / rendering.

NEXT, in order: (I1) an LPAE page table using the measured geometry
(`ias=48`, 4 KiB granule, TTBR1, and crucially `coherent_walk=1` -> `b5100080`),
(I2) a real kernel caller that owns a context bank, (I3) a faulted stream bound to
that bank. Do **not** restart from UEFI / the menu / the payload volume — those were
already working (this very boot reached the APFS root) and are a known dead end.
