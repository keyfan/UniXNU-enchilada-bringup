# Round 101 — `ias` is MEASURED: the input the port never had

**Status: DEVICE-VERIFIED. The single missing input of the SMMU stage-1 row now has a
measured value.**

```
[   42.087556] enchilada-adreno: stage5=run rc=0 up=0 up_fail=5 precheck=0 ops=32 w=9 r=14
                                  cx_before=00005ff4 cx_after=80001fe0 cx_on_after=1
[   42.087823] enchilada-adreno: stage4=id2 id2=00007511 ver=2 ias_ok=1 ias=48 ipa=36 pa=36
                                  oas2=36 ptfs4k=1 vmid16=0 words_ok=1 plan_ok=1
                                  tcr=a0100080 tcr2=00038011
```

This is the first time in the project that `ID2` has been read on the phone, and the
first time `ias` has been anything other than an argument.

## The word, decoded independently

`ID2 = 0x00007511`, decoded by hand against the pinned bit positions (P1:66-72) and
compared with the probe's own output:

| field | bits | raw | meaning |
| --- | --- | --- | --- |
| `IAS` | `[3:0]` | `1` | `arm_smmu_id_size_to_bits(1)` = **36-bit** input-address (IPA) size |
| `OAS` | `[7:4]` | `1` | 36-bit output-address (PA) size |
| `UBS` | `[11:8]` | `5` | **collapses to 48** via the default arm of `id_size_to_bits()` |
| `PTFS_4K` | `BIT(12)` | 1 | 4 KiB granule supported |
| `PTFS_16K` | `BIT(13)` | 1 | 16 KiB granule supported |
| `PTFS_64K` | `BIT(14)` | 1 | 64 KiB granule supported |
| `VMID16` | `BIT(15)` | 0 | 8-bit VMIDs only |

**`ias = 48`**, and it comes from `UBS = 5` — which is exactly the value the default
arm of `arm_smmu_id_size_to_bits()` (P2:1727-1744) collapses to 48. Round 83 recorded
that collapsing default as a trap; it is the branch this device actually lands in.
Note `IAS = 1 → ipa = 36` while `UBS = 5 → va_size = 48`: for a v2 SMMU the VA size
comes from UBS and the IPA size from IAS, so the two fields genuinely disagree here.

## Why this value matters and why it is not a guess

Round 77 proved the chain `ias = smmu->va_size <- id_size_to_bits(ID2.UBS) <- a
probe-time register read`, so no document or DTS could supply it. Round 85 measured
that *userspace* cannot read it either (`CONFIG_DEVMEM` is not set; the iommu debugfs
exposes no address-size node). That is why the read had to move into the kernel, and
why this number could only appear now.

The downstream composition also succeeded in the same row: `words_ok=1` and
`plan_ok=1` mean `adreno_iommu_stage1_words()` accepted the decoded word and produced
round 83's context words with **a real `ias` for the first time** —
`tcr=0xa0100080`, `tcr2=0x00038011`.

Sanity check on those: `tcr2=0x00038011` is SEP(`0x7`<<15) | PASIZE(1 = 36-bit) |
AS(BIT4) — the same constant round 83 derived for oas 36 with an AArch64 context.

`tcr=0xa0100080` is the **TTBR1 geometry**, and that is assertable rather than
assumed: EPD0 is SET, EPD1 is CLEAR, the granule field `TG1` sits at bits [31:30]
(the TTBR1 branch shifts the low fields up by 16) and reads 2 = 4 KiB, and the real
`T1SZ` is in the high half: `0xa010` & `0x3f` = 16 ⇒ `64 − 16 = 48` = ias. Reading
`T0SZ` from the low half in this geometry returns 0 and would look like an ias of 64,
which is why the two halves must be identified before either is believed.

Re-deriving round 83's own function for `(ias=48, coherent_walk=0, ttbr1=1)` yields
**`0xa0100080` exactly**, byte for byte. **The measured word and the composed word
agree**, which is the cross-check that makes this a measurement rather than two
coincidences.

## How the device got here

Three consecutive device rounds each removed one wrong precondition from the read
gate, and each was caught by the device rather than by review:

| round | verdict | what was wrong |
| --- | --- | --- |
| 98 | `3(refused-cx-down)` | the gate used the CX word sampled **before** stage 5 raised the rail |
| 99 | `4(refused-clocks-off)` | the probe hard-coded `clocks_enabled = 0` although the executor had enabled them |
| 100 | `5(refused-unmapped)` | the window was mapped **inside** the permitted branch, but the gate checks mapped-ness ⇒ deadlock |
| 100 | `7(refused-no-stream)` | `stream_faults` was folded into the READ gate although upstream reads ID2 at probe with nothing bound |
| **101** | **`stage4=id2`** | — read succeeded |

The last one was resolved by checking upstream rather than reasoning: a mechanical
count over `arm_smmu_device_cfg_probe()` between its start and the ID2 read finds
**zero** occurrences of `fault`, `BYPASS`, `BUSY` or `wait`. The stream condition is
real but belongs to *programming* a context, so it now lives in the separate
`adreno_iommu_admit_program()` gate.

## Honest boundary

This proves **one thing**: `ID2` reads as `0x00007511` on this phone with CX up, and
it decodes to `ias = 48`. It does **not** prove that a stage-1 context works — that
still needs an ASID allocator and a populated LPAE table, neither of which exists.
Both `words_ok` and `plan_ok` are about *computing* words, not writing them.

The admission table is unchanged at **modeled = 9 / satisfied = 0 / BLOCKED**: the
`kgsl-smmu-stage1` row needs one of its four inputs, and this round obtained one.
Progress, not completion.

## Safety and recovery

The device ran with the reboot guarantee established first
(`scripts/check-reboot-to-twrp.py` → PASS) and was re-armed immediately after
returning (`--arm-bcb-only`), because the misc BCB is one-shot. Measured this round:
`fastboot reboot recovery` returns to TWRP **even with a zeroed BCB**, which is why it
is now the default reboot path in `scripts/reboot-to-twrp.py`; the BCB is kept as the
second line for an XNU-initiated reset, and the byte-identical `boot_a` backup
(md5 `ca8ddd7a5af345636cad8c5bf645fee2`) is the third.
