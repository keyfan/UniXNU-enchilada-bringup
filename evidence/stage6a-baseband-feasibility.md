# Stage 6a — baseband (cellular) scope + feasibility audit

**Status: HOST-VERIFIED. Not device-verified. No CI build. No device access.**

Date: 2026-11-09 · Board: OnePlus 6 (enchilada, SDM845) · OS profile: ipados-17.7.11-21H461
Device repository revision this record describes: `8f4ca9bf2b386ab8414f7ca9de176d71b4f02ded`
(parent `ee2960eb081236d32fe6fc8fa96a5eeedbff1697`).

This document is the evidence payload for the `cellular` area in `porting.toml`.
It exists because the honest answer for this stage is **"in scope, and blocked"**, not
"not applicable" — and a claim like that has to be backed by source text, not by vibes.

---

## 1. Why this area is not `not-applicable`

The official page (https://unixnu.com/zh/porting/baseband) offers an escape hatch:

> Wi-Fi-only tablets without a cellular modem can skip this page. Set the cellular area of
> `porting.toml` to `not-applicable`.

That rule is scoped to **Wi-Fi-only tablets without a cellular modem**. enchilada is a phone.
The vendor device tree contains a real modem subsystem:

- `Mu-Silicium/Resources/DTBs/enchilada.dts:6444-6484` — `qcom,mss@4080000`,
  `compatible = "qcom,pil-q6v55-mss"`, `qcom,firmware-name = "modem"`,
  `qcom,pil-self-auth`, `qcom,qdsp6v65-1-0`, `qcom,mem-protect-id = <0x0f>`,
  `interrupts = <0x00 0x10a 0x01>` (GIC SPI 266), `mboxes = <&mss-pil>`.

Marking it `not-applicable` would therefore be a **factual error**, and the checker would also
force an empty `components` list, erasing the modem from the record entirely.

---

## 2. The cross-generation gap (why the official component cannot be dropped in)

| | SDM845 (enchilada) | SM8550 (sheng) | SC7180 (Coachz / QcomBaseband) |
|---|---|---|---|
| compatible | `qcom,pil-q6v55-mss` | `qcom,sm8550-mpss-pas` | PAS |
| boot flow | **self-auth** (MBA first, then MPSS) | PAS `auth_and_reset` | PAS |
| PAS id | MPSS 5, IPA 0x0f (15) | 4, plus `dtb_pas_id` 0x26 | — |
| IPA generation | **v3.5.1** | v5.5 | **v4.2** |
| transport | GLINK/QRTR over SMEM | GLINK/QRTR | GLINK/QRTR |

Three different generations, three different auth flows. The officially referenced component
(`QcomBaseband`, by HoshinoYumeka) targets Coachz/SC7180 and must be built with
`build-qcom-baseband.py --coachz`; it is IPA v4.2 + PAS, which is neither enchilada's IPA v3.5.1
nor enchilada's self-auth flow.

`sheng` itself has **no baseband driver at all**: no `QcomBaseband`, no IPA, no CommCenter. Its
MHI code is WCN7850 Wi-Fi; its GLINK/QRTR/SMEM/SMP2P/MDT/SCM code is the ADSP audio helper;
`package/tests/modem_fw_host.c` is only a host-side FAT/MDT firmware reader;
`make-sheng-adt.py:143` says "no SEP or baseband"; and sheng's own `porting.toml` has 13 areas,
none of which is cellular.

---

## 3. Corrections to the previous record

The `cellular` area previously claimed:

> SDM845 MSS PIL (mba.mbn + modem.mbn), IPA with qcom,gsid-loader=self (ipa_fws.mbn), QRTR/GLINK,
> WCN3990 is Wi-Fi/BT only; no eSIM on this board.

Four problems, all now corrected:

1. **`qcom,gsid-loader` does not exist.** The real property is `qcom,gsi-loader`
   (`drivers/net/ipa/ipa_main.c:735`, `Documentation/devicetree/bindings/net/qcom,ipa.yaml:135`).
   Neither the vendor DTB nor the mainline `sdm845.dtsi` sets it, and mainline leaves `&ipa`
   `status = "disabled"`, so the loader is the legacy `IPA_LOADER_SELF` — by absence, not by declaration.
2. **The defaults are split images, not single files.** `q6v5_mss.c:2007`
   `qproc->hexagon_mdt_image = "modem.mdt";` and `ipa_main.c:71`
   `#define IPA_FW_PATH_DEFAULT "ipa_fws.mdt"`. The `.mbn` files named in
   `sdm845-oneplus-common.dtsi:552-556` and `:604-610` are entry pieces of those split sets
   (`qcom/sdm845/OnePlus/enchilada/{ipa_fws,mba,modem}.mbn`).
3. **"No eSIM" was never verified.** Zero `esim`/`eSIM` hits in the vendor DTB. Dropped.
4. **"QRTR" was loose.** The DTB node is `qcom,ipc_router_glink_xprt`
   (`enchilada.dts:7504`); SMEM `:7323`, GLINK `:7381`, SMP2P `:14183`, IPA `:7793`, `ipa_fws` `:7852`.

---

## 4. What was actually built, and what it does NOT do

`drivers/NativeModem/` (`ModemPil.h`, `ModemPil.c`, `tests/modem-pil-test.cpp`) plus
`scripts/check-enchilada-modem-host.py`.

It encodes the three reference generations **as data** — boot flow, IPA generation, MSS base,
GIC SPI, PAS ids, mem-protect id, SMP2P pid, firmware role names — plus the split-image vs
single-file distinction and a `modem_pil_assess()` blocker gate.

It is not linked into any kernel. It maps no register, authenticates no image, loads no firmware,
and performs no modem access. There is no baseband `CONFIG_*=y`, no modem firmware in the payload,
and no `[cellular]` build section: `drivers/NativeModem` is deliberately absent from
`[kernel] archives` and from `CONFIG_XNU_EXTRA_LD_ARCHIVES`.

---

## 5. The two verifications

### Pass 1 — host runner

```
python scripts/check-enchilada-modem-host.py \
  --cc oneplus6-ios/tools/llvm-mingw/bin/clang.exe \
  --cxx oneplus6-ios/tools/llvm-mingw/bin/clang++.exe \
  --out work/tmp/modem-host-out
```

8 test groups, all PASS:

```
PASS: enchilada stage-6a modem-PIL feasibility study (8 test groups)
```

Groups: `testLookupSdm845` (row data matches the vendor DT), `testGenerationGap` (three flows
distinct), `testSplitImageNames` (`modem.mdt`/`ipa_fws.mdt` recognised as split; `.mbn` not),
`testFirmwareMask` (missing-firmware mask arithmetic), `testAssessToday` (the five real blockers),
`testAssessGenerationGate` (SDM845 keeps GENERATION without a matching backend even when every
other resource is forced available), `testNotApplicableIsWrong` (the board has a modem),
`testNames` (stable name strings).

### Pass 2 — different method: strict compile + independent re-derivation

```
clang -std=c11 -Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion -Wshadow \
      -O2 -ffreestanding -c drivers/NativeModem/ModemPil.c
```

exit 0. Then `work/tmp/verify_modem_record_2.py` re-derives **every constant** from the vendor DTB,
`q6v5_mss.c`, `ipa_main.c` and `dtsi` text rather than trusting the module's own tables, and
cross-checks that each value in `ModemPil.h`/`.c` appears in the source text at the expected place.

Result: `MODEM_VERIFY2_EXIT=0`, 13 header/impl facts verified, `porting.toml` no longer contains
the bogus `qcom,gsid-loader` string.

---

## 6. Blockers — four independent, each individually sufficient

| Blocker | Evidence |
|---|---|
| **FIRMWARE** | Zero modem images (`*.mbn`/`*.mdt`/`*.bNN`) anywhere in the workspace; the whole modem image set requires Qualcomm signing. |
| **BACKEND** | No SDM845 MSS-PIL / IPA-v3.5.1 generation backend exists locally or upstream (the official one is SC7180/PAS). |
| **OS_ABI** | `ABM`, `BBUpdater`, `CommCenter`, `AppleBaseband*` are closed-source; this host has no macOS/Xcode, so CI is the only ABI authority. |
| **GENERATION** | The vendor DT self-auth flow and the SC7180/SM8550 PAS flow are not interchangeable. |

Plus, independently: `qcom,pil-self-auth` with `qcom,mem-protect-id = <0x0f>` requires
TrustZone/XPU memory assignment (`qcom_scm_assign_mem`, RMB auth 0x3/0x4) with no iOS equivalent here.

The gate evaluates as:

```
modem_pil_assess(kModemSocSdm845, fw = 0, backend = 0, os_abi = 0, trustzone = 0) = 0x3e
  = FIRMWARE | BACKEND | OS_ABI | GENERATION | TRUSTZONE
```

which is exactly right: it states what would have to become **true** for this stage to move.

---

## 7. Honest limits of this record

- No device verification, no CI build, no firmware, no register touched, no data call attempted.
- The pass/fail facts come from source text and the vendor DTB. The one inferential step — that
  `QcomBaseband` targets SC7180 — is stated as that component's own build recipe (`--coachz`),
  not measured here.
- If baseband is ever put in scope, the first deliverable is **a signed-MBA authentication
  experiment**, not a data call.
