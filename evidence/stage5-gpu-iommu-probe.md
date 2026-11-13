# Stage 5 GPU — the kernel-side ID2 probe contract (ROUND 86, HOST-VERIFIED)

**Status: HOST-VERIFIED. CONTRACT ONLY.** This is NOT a driver and issues no MMIO.
It is the specification the **kernel-side** SMMU caller must honour: which
register, at which offset, decoded exactly how, gated behind exactly which
preconditions.

`CONFIG_XNU_NATIVE_ADRENO` remains `n`. The `gpu` area **STAYS `"host-verified"`**.
No device run, no flash, no MMIO, `m39510` NOT exercised. It does **not** unblock
stage 5.

## Why this module exists, and why it is not "another derivation"

Round 85 measured a hard ceiling: this kernel is built with **`CONFIG_DEVMEM` not
set**, so `/dev/mem` does not exist and every read fails `ENXIO`; and the iommu
debugfs exposes **no address-size node at all** (the whole tree is
`io-pgtable` + `tests`, and `find -maxdepth 2` returns one file).

That result changes what the next artifact must be. `ias` — the SMMU input-address
size — is the input round 77 proved underivable from any document, and round 85
proved **unreadable from userspace**. So it is obtainable **only by code running in
the kernel**. The correct next deliverable is therefore not another derivation but
the **contract** that kernel caller must satisfy, written down precisely enough to
be checkable before any hardware is touched.

## What it pins (4 sha256 sources)

| pin | file | bytes | sha256 |
| --- | --- | --- | --- |
| P1 | `drivers/iommu/arm/arm-smmu/arm-smmu.h` | 16630 | `ed85998794695d1a824dc1b86f680c8d124546688fd2665e00e3cbcfa9546b53` |
| P2 | `drivers/iommu/arm/arm-smmu/arm-smmu.c` | 67693 | `d5a22b62915e52d2f01744fc68507a8bdf626b68fe090bef6a66a633ecb4fb4a` |
| P3 | `drivers/iommu/io-pgtable-arm.c` | 38934 | `b78adbf87352e574556638fae1206d62bfbc06787a85125f75b9d5da0abab278` |
| P4 | `drivers/iommu/io-pgtable-arm.h` | 812 | `34520ecc0fa7f29c7bc03c38c125e67bd66229ddc5fc44be0c59408e0389a379` |

### The ID2 decode, exactly as the driver does it

`GR0_ID2` is at `0x28` (P1:65). The fields (P1:66-72): `ID2_VMID16` BIT(15),
`ID2_PTFS_64K` BIT(14), `ID2_PTFS_16K` BIT(13), `ID2_PTFS_4K` BIT(12),
`ID2_UBS` GENMASK(11,8), `ID2_OAS` GENMASK(7,4), `ID2_IAS` GENMASK(3,0).

The size chain (P2:1879-1905) is the load-bearing part, and it has a **version
gate that is easy to get wrong**:

- `ipa_size` ← `arm_smmu_id_size_to_bits(ID2_IAS)` (P2:1880-1881)
- `pa_size`  ← `arm_smmu_id_size_to_bits(ID2_OAS)` (P2:1884-1885)
- for **v1**: `va_size = ipa_size` (P2:1899-1900), i.e. UBS is *not* consulted
- for **v2+**: `va_size = arm_smmu_id_size_to_bits(ID2_UBS)` (P2:1904-1905)

and `arm_smmu_id_size_to_bits()` (P2:1727-1744) has a **default arm** that collapses
value 5 *and every other value* to 48. Encoding it as a table without that default
would invent a width for an undefined IDR field — the same trap round 83 recorded
for `TG1_4K`.

Also decoded: the three granule-feature bits, and `TTBR1` selection for SID 0,
because the aperture geometry differs between them.

### The composition is a CALL, not a copy

The stage-1 word functions are **`adreno_smmu_ctx_tcr()`**,
**`adreno_smmu_ctx_tcr2()`** and **`adreno_smmu_ctx_ttbrn()`** from round 83's
`AdrenoSmmuCtxWordV1`. This module **calls** them; it does not re-implement them.
That is asserted by the module's undefined-symbol set and by the cross-verifier:
if someone copies the arithmetic here, the two derivations can silently diverge,
which is precisely the round-81 regression class.

## The gate is fail-closed and ordered

The probe is gated behind preconditions that must all hold, each with a pinned or
measured citation, and the step table places **every register read AFTER the CX
gate**. That ordering is not stylistic — round 35 measured CX DOWN
(`cx=0x00005ff4`, BIT(31) clear) and rounds 22/28 forbid reading the GPU/SMMU
windows while the domain is collapsed.

## What is still missing — derived, not assumed

Seven inputs are recorded absent and `ready()` is derived from that mask, so it is
**0** today:

| input | why absent |
| --- | --- |
| `ias` | needs the kernel-side ID2 read this contract specifies |
| `asid` | no ASID allocator exists in this port |
| page table | no populated LPAE table exists |
| CX powered | round 35 measured it DOWN |
| stream binding | round 53 measured the streams still in firmware BYPASS |
| kernel MMIO mapping | no XNU-side SMMU driver exists yet |
| probe caller | this module is the contract for it, not the caller |

## Post-Route-2: system_b now holds the payload

Independent of this module, the same round wrote the CI #239 payload volume to
`system_b` and verified the write (`OKAY`), because the ramdisk route is
impossible: measured, the ABL reserves only ~18 MiB for the initrd window while
the payload is 49.4 MiB, and a 32 MiB ramdisk takes the SoC to **EDL 9008**.
`system_b` is the failed slot (`slot-successful:b: no`) while `current-slot: a`,
and it is 2.79 GiB, so the write is reversible by re-flashing the stock system
image.

## Honest boundary

Host-verified contract. It reads no register, allocates nothing, programs nothing,
and does not prove the SMMU works. It cannot: the values it decodes are inputs a
kernel caller must supply. What it does provide is that when such a caller is
written, the register offset, the field positions, the version gate, the collapsing
default and the precondition order are all pinned and machine-checked rather than
recalled.

Verifier: `scripts/verify-enchilada-iommu-probe.py` — re-derives the ID2 field
positions from their GENMASK bounds, the `id_size_to_bits` collapse, the
ipa/pa/va chain including the v1/v2 gate, stage-1 `oas = ipa_size`, the 16K
clearing, TTBR1 for SID 0, and the probe order. **710 checks.**
