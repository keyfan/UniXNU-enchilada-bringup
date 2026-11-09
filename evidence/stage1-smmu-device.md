# Stage 1 memory: appSMMU read-only survey, device-verified

**Verdict line (verbatim, repeated once per second by the replay):**

```
enchilada-smmu: rc=0 SS slots=76 banks=45 pages=64 exids=0 valid=8 t0=8 t1=0 t2=0 scr0=002a0406 gfsr=00000000 ufs=5/2/bypass usb=6/4/bypass (replay t=899 attempt=1)
```

- Device: OnePlus 6 (enchilada, SDM845), OS profile `ipados-17.7.11-21H461`.
- Boot: `boot_id 553412253`, uptime 899 s, payload
  `payload-smmu2.img` sha256
  `5e3c7146b268239b33c50ad3a6c400bf18d461ab75452925ff16a8af2f5edc01`.
- Kernel: CI run `42544313568` (#215), `kernel_sha256`
  `64f40cc1054820e803c18b9b47332aedf110000d9285ec3377721be65b23b7ef`.
- Record commit: `0d496779f92c5b62cba448956a52e59b19a95d1d`.

## What was measured, and why measuring was the only option

The apps_smmu window is `0x15000000` size `0x80000` (stated twice in the tree:
`drivers/NativeDwc3/Dwc3Registers.h:35-36` and `drivers/NativeUfs/NativeUfs.cpp:432`).
The survey walks every SMR slot and reads S2CR only for slots whose SMR valid bit
is set, so the read count is itself an assertion.

| Field | This boot | Meaning |
| --- | --- | --- |
| `slots` | 76 | `sIDR0[7:0]` — populated stream-table entries |
| `banks` | 45 | `sIDR1[7:0]` — context banks |
| `pages` | 64 | `1 << (sIDR1[30:28] + 1)` with `[30:28] = 5`, the identical expression as `NativeUfs.cpp:915` |
| `exids` | 0 | no extended stream IDs |
| `valid` | 8 | eight entries with the SMR valid bit set |
| `t0 / t1 / t2` | 8 / 0 / 0 | all eight are type 0 = **bypass**; none translate, none fault |
| `scr0` | `002a0406` | SMMU global control: `[10:8] = 4` (client/coherent) with translation disabled |
| `gfsr` | `00000000` | **no global fault** recorded |
| `ufs` | `5/2/bypass` | the UFS stream lives in slot 5, context bank 2, bypassing translation |
| `usb` | `6/4/bypass` | the DWC3 stream in slot 6, bank 4, bypassing translation |

## The two halves are independent, and one is cross-validated

`usb=6/4/bypass` matches, exactly, the line the **DWC3 takeover planner** printed
on an earlier boot:

```
G smmu slot=6 bank=4 plan=80000740/00000004
```

That planner (`drivers/NativeDwc3/Dwc3SmmuPlan.h`) and this survey share no code
and read the table through different abstractions, so the agreement is a genuine
cross-check rather than a tautology. It also means the survey's window mapping
and offset arithmetic are right, independently of the UFS result.

## A correction this run forced on the driver

The previous boot printed `ufs=none`. That was **this driver's defect, not a
hardware fact**: the survey core had sheng's default stream `0x60` hardcoded
(`drivers/NativeUfs/NativeUfs.cpp:192`), while this board boots with
`native-ufs-smmu-stream=0x100 native-ufs-smmu-mask=0xf` (the vendor DTB says
`iommus = <&apps_smmu 0x100 0xf>`, and `NativeUfs.cpp:201-202` parses exactly
those two boot args).

The fix has two halves, so the two drivers *cannot* disagree:

1. the pair is a caller-supplied `struct smmu_survey_targets`, defaulting to the
   board values `0x100 / 0xf`;
2. `package/EnchiladaSmmuProbe.cpp` re-reads the same two boot args before
   scanning.

`testDefaultTargetsAreTheBoardPair()` pins both directions: the board pair
matches an SMR of `0x80000100`, and the sheng pair does **not** — the exact
regression that produced the false `ufs=none`.

## Read-only contract

`struct smmu_survey_io` has only `read32`; there is no write member to call, and
`SmmuSurvey.c` contains no store into the mapping. The kernel shim maps the
window once, read-only (`kIOMapInhibitCache` equivalent path via
`armcpu_map_physical`), and every load is followed by `dsb sy`. Nothing in this
change writes a stream table entry, a context descriptor, or `sCR0` — the
translation hardware is observed, never reconfigured.

That boundary is deliberate. Taking the SMMU over requires writing a stage-1
context and enabling a bank; a single wrong descriptor there is an unrecoverable
bus abort. The survey was built first precisely so the write-side change can be
made later against measured geometry instead of assumptions.

## Honest boundaries

- This verifies the **read path and the firmware handoff state**, not translation.
  All eight attached streams are in bypass, which is consistent with a
  bootloader-handoff SMMU and is what the Linux `arm-smmu` driver also expects at
  probe time.
- The `ArmSmmu4K` kit is still **not** linked. It is the write-side driver; that is
  a separate and much riskier change.
- `t1 = t2 = 0` is a fact about this boot, not a claim that translation can never
  be enabled.
- The replay exists because the kernel msgbuf is a 128 KB ring that saturated at
  101.2 s / 87.7 s / 58.8 s across three measured boots, so a verdict logged once
  at t<8 s is always overwritten before collection. Replay performs no MMIO: one
  physical measurement, 289 identical re-emissions (`distinct_verdicts == 1`).

## Reproduce

```sh
python scripts/check-enchilada-smmu-host.py --cc <clang> --cxx <clang++> --out <dir>
python work/tmp/verify_smmu_survey_2.py
python work/tmp/preflash_check_smmu2.py
python work/tmp/verify_smmu2_prebuild_2.py
python work/tmp/collect_smmu2.py
python work/tmp/analyze_smmu2_device_logs.py
python work/tmp/analyze_smmu2_serial.py
```
