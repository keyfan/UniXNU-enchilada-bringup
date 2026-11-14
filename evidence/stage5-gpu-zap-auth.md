# Stage 5 GPU — the Qualcomm PAS / ZAP auth SHAPE, and the eight-step chain that is not there (ROUND 73)

**Status: HOST-VERIFIED. SHAPE + CHAIN + INVENTORY ONLY.** This is NOT a PAS
implementation and NOT a GPU driver. It encodes the exact SCM call the ZAP
handshake uses, the eight ordered preconditions around it, and — the point of the
round — **which of those eight actually exist on enchilada today: exactly one.**

No SCM call was made, no firmware was loaded, no `memremap` happened, no MMIO was
issued. `CONFIG_XNU_NATIVE_ADRENO` remains `n`, the `gpu` area STAYS
`"host-verified"`, and it does **not** unblock stage 5.

## Why this round

The admission table has recorded `zap-auth` as `modeled = 0` since round 69. The
reason given was honest but thin: "the A740/sheng tree contains NO PAS code at
all, so nothing can be inherited." That says what is absent; it does not say what
would have to be *written*. Round 71 confirmed `GPU_PAS_ID 13` from the device
tree. What was missing was the shape of the call and the exact set of things that
must exist around it.

Encoding that turned "PAS is unimplemented" into **eight named, individually
checkable absences**, which is a far more useful form for anyone who picks this
up next.

## The call shape (4 sha256 pins)

| pin | file | bytes | sha256 |
| --- | --- | --- | --- |
| P1 | `include/linux/firmware/qcom/qcom_scm.h` | 5203 | `47390ea0…c448a1` |
| P2 | `drivers/firmware/qcom/qcom_scm.c` | 53938 | `8f1fbf64…a5001` |
| P3 | `drivers/firmware/qcom/qcom_scm.h` (internal) | 4977 | `d9cc584a…2394881` |
| P4 | `drivers/gpu/drm/msm/adreno/adreno_gpu.c` | 29592 | `e6b5203f…6f00b7` |

The auth call is `qcom_scm_pas_auth_and_reset(peripheral)` and its body
(`qcom_scm.c:703-731`) builds exactly one SCM descriptor:

```
svc      = QCOM_SCM_SVC_PIL                0x02     (internal header :97)
cmd      = QCOM_SCM_PIL_PAS_AUTH_AND_RESET 0x05     (internal header :100)
arginfo  = QCOM_SCM_ARGS(1)                one argument
args[0]  = peripheral                      the PAS id
owner    = ARM_SMCCC_OWNER_SIP             SIP, not TZ/hyp
```

**A finding worth recording: the numeric SVC/command ids are NOT in the public
`qcom_scm.h`.** A port that pinned only the public header would have the function
signature and none of the wire values. The verifier asserts that absence in P1
and the presence in P3, so the distinction cannot quietly rot.

One more detail that is easy to get wrong and is now encoded
(`qcom_scm.c:729`):

```c
return ret ? : res.result[0];
```

A returned 0 from the SCM *transport* is **not** success. If the secure world
rejects the image, `res.result[0]` is non-zero and the call has FAILED. Getting
this backwards reports a rejected ZAP image as authorised — and since ZAP is the
gate on GPU access, that would be the worst possible lie for this port to tell.
`adreno_zap_result_semantics()` requires **both** to be zero.

## The eight-step chain, and what is actually present

From `adreno_gpu.c:30-174`, in source order:

| # | step | line | present | why |
| --- | --- | --- | --- | --- |
| S1 | `CONFIG_ARCH_QCOM` | :43 | **no** | XNU is not Linux: no such Kconfig exists. This step's real content is the SCM transport itself — it is **not even reachable** as written |
| S2 | `"zap-shader"` child node | :48 | **no** | live device tree has NO such child under `qcom,kgsl-3d0` |
| S3 | `memory-region` phandle | :54 | **no** | exists only on `qcom,kgsl-hyp`, a different node |
| S4 | `of_address_to_resource` | :61 | **no** | the region is declared; the phys translation was never done here |
| S5 | `firmware-name` | :82 | **YES** | round 71 read `"a630_zap"` live |
| S6 | `qcom_mdt_get_size` | :111 | **no** | the .mdt sizes are known (round 38) but no XNU-side MDT parser exists |
| S7 | `qcom_mdt_load` | :141/148 | **no** | needs S3, S4 and S6 first |
| S8 | `pas_auth_and_reset` | :156 | **no** | the shape is now encoded; the transport is absent |

**Exactly one of eight is present**, and it is `firmware-name`.
`adreno_zap_auth_ready()` is **derived** from that count rather than hard-coded,
so it flips only when all eight genuinely exist. It is 0.

Note S1's distinction: seven steps are *not-yet-built*, but S1 is
*not-reachable-as-written* — and the module models that difference explicitly
(`reachable = 0`) instead of flattening eight different absences into "no".

## A live-device confirmation (read-only)

The `zap-shader` question was settled against the phone rather than assumed:

```
adb shell ls /proc/device-tree/soc/qcom,kgsl-3d0@5000000/   -> no zap-shader
adb shell ls /proc/device-tree/soc/qcom,kgsl-hyp/           -> memory-region,
                                                               qcom,firmware-name,
                                                               qcom,pas-id
```

So the vendor firmware genuinely uses a *different node shape*, which is why S5 is
present while S2/S3/S4 are not. `scripts/verify-enchilada-device-tree.py` now
re-reads this on every run — **PASS (58 checks)**.

## The admission table

`zap-auth` moves **modeled 0 → 1** (total modeled **6 → 7**). `satisfied` STAYS
**0**, verdict STAYS **BLOCKED**. The shape is known; the transport and seven of
its eight preconditions are not. That is recorded as modelled-only, exactly as the
`cp-ring` row was before round 70 closed it.

## Deliverables

| file | role |
| --- | --- |
| `drivers/NativeAdreno/AdrenoZapAuthV1.h` | 4 sha256 pins; SVC/CMD/arity/owner; the 8-step enum; 11 APIs |
| `drivers/NativeAdreno/AdrenoZapAuthV1.c` | freestanding C11; the call shape; the chain with per-step `present`/`reachable`; the result-semantics trap; the -EOPNOTSUPP quiet fallback |
| `drivers/NativeAdreno/tests/adreno-zap-auth-v1-test.cpp` | 7 test groups |
| `scripts/verify-enchilada-zap-auth.py` | cross-verifier, **114 checks** |
| `scripts/build-enchilada-adreno.py` | 25th archive unit |
| `scripts/check-enchilada-adreno-host.py` | 25th host suite |

## Verifications

1. `PASS: enchilada A630 ZAP/PAS auth shape contract (7 test groups, 0 failures)`
   inside the 25-suite runner.
2. `ZAP_AUTH_CROSS: PASS (114 checks, 0 failures)` — Domain A re-derives the SVC,
   command, arity, owner, the `ret ?: res.result[0]` trap, the eight anchors and
   that they are in **strictly increasing source order**, plus the public-header
   absence; Domain B asserts exactly one step present and `auth_ready()` derived.
3. Strict freestanding compile: arm64 Mach-O **6336 B**, COFF 5398 B,
   `R73_STRICT: PASS`.
4. `DEVICE_TREE_VERIFY: PASS (58 checks)` with the new live ZAP-node checks.

## Honest boundary

The shape of a call is not the call. Nothing here can authorise anything: there is
no SCM transport in XNU, no MDT parser, no `memremap` equivalent, and seven of the
eight preconditions do not exist. `satisfied` stays 0, the four hard blockers of
`docs/porting/gpu.md` §1 remain open, `CONFIG_XNU_NATIVE_ADRENO` stays `n`, the A630
client GPU windows stay all-zero, and the `gpu` area STAYS `"host-verified"`. No
device run, no flash, `m39510` NOT exercised. It does **not** unblock stage 5.

Chain: round 72 closed at `4099a40649942d1dea3428bf9025a5a3b2cfc701`.

---

## ROUND 106 addendum (2026-11-14) -- SUPERSEDES the counts above, not the round-73 findings

Everything in this record about the round-73 shape, the eight-step chain and the
"exactly one of eight present" result still holds verbatim. Three things above
are now STALE and are corrected here rather than edited in place, because this
file is the frozen round-73 record.

1. **`7 test groups` -> 9.** Round 106 added `testPasStages` and
   `testSmcBaseAndChannels`. The banner now reads
   `PASS: enchilada A630 ZAP/PAS auth shape contract (9 test groups, 0 failures)`.
2. **`ZAP_AUTH_CROSS: 114 checks` -> 224.** The verifier gained Domain A11
   (the three-stage ORDER re-derived from `mdt_loader.c:226-252`, the three
   arginfo values recomputed from `qcom_scm.h:29-42`, and the SMC function-id
   arithmetic from `:69`) and Domain B12/B13 for the new contract.
3. **`owner` was recorded as `1` -- it is `2`.** The header line quoted above
   said `ARM_SMCCC_OWNER_SIP = 1u` from round 73 through round 105. That was
   WRONG. `ARM_SMCCC_OWNER_SIP` is 2, and it is now DERIVED rather than
   asserted: `(0xC2000000 >> 24) & 0x3F == 2`, and the module recovers the owner
   from the base in `adreno_zap_smc_base_owner()` so the verifier can prove it.

The round-73 honest boundary is UNCHANGED: no SCM call, no transport, seven of
eight preconditions absent, `adreno_zap_auth_ready()` still 0 and still derived
from `adreno_zap_present_count()`, `CONFIG_XNU_NATIVE_ADRENO` still `n`, the
`gpu` area still `"host-verified"`, no device run, no flash. Round 106 models the
call SEQUENCE; it does not execute one.