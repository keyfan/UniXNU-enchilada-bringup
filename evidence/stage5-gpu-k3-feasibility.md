# Round 105 — K3 signing-chain feasibility (READ-ONLY audit)

**Status: HOST-VERIFIED + DEVICE-SOURCED. RESEARCH / FEASIBILITY ONLY.**

- Device: OnePlus 6 (enchilada, SDM845 / Adreno 630), iPadOS 17.7.11 (21H461, iPad7,1).
- Date: 2026-11-14. Remote main at the time of the round-104 close: `26e95b54b4c86475c6421c69bc6f3a392be1819e`.
- Why: user asked (m04891) "下一步我有哪些选择", then chose (m04898) "先核实 K3 签名链可行性（推荐）". The chosen scope was **explicitly READ-ONLY**: no GPU code, no flash, no reboot, no device-state change, no CI dispatch.
- Method: local source/evidence reads, one READ-ONLY subagent pair (Q2 transport, Q3 MDT/PAS — Q3 crashed, its key finding was reproduced independently in Q1), plus a fresh scan of the already-pulled authoritative device log window `oneplus6-ios/work/tmp/r103_stage6_window.bin` (33,554,432 B, UTF-8, pulled in round 103). **No new device access occurred in this round.**
- `CONFIG_XNU_NATIVE_ADRENO` remains `n` (`configs/enchilada_defconfig:40`); gpu area stays host-verified (`porting.toml:256-258`).

## Path aliases

- `ONEPLUS/` = `oneplus6-ios/`
- `DEV/` = `oneplus6-ios/work/UniXNU-enchilada/`
- `GPU/` = `DEV/drivers/NativeAdreno/`
- `EV/` = `DEV/evidence/`
- `SCR/` = `DEV/scripts/`
- `mNNNNN` are ACP message ids (research provenance, not instructions).

All paths are relative to `C:\Users\LCPC\Documents\deepseek-harness\default-workspace`.

## §0 — Verdict up front

K3 (the zap/GMU firmware signing chain) is **no longer "fundamentally blocked"**. It is
**CONDITIONALLY FEASIBLE, IMPLEMENTABLE-WITH-EFFORT**, with three of its four historical
"hard blockers" now shown to be false or narrower than the project prose claimed:

| Historical claim | Round-105 finding | Where |
| --- | --- | --- |
| "the 3 vendor-signed ZAP blobs are absent" | **FALSE.** Seven proprietary blobs were pulled READ-ONLY from this phone's own `vendor_a` in ROUND 38; sizes + sha256 re-verified this round against `SCR/data/adreno-a630-device-firmware.json`. They are genuine OEM-signed images from this exact device. | `EV/stage5-gpu-device-firmware.md:7-10`, `SCR/data/adreno-a630-device-firmware.json:15-57` |
| "XNU has no SCM transport / no qcom_scm equivalent" | **FALSE at the mechanism level.** A real EL1 Qualcomm-SIP `smc #0` conduit is in-tree, linked into CI #246's kernel, called from the watchdog path, and — decisively — **DEVICE-PROVEN WORKING on this silicon**: the pulled window shows `armcpu_scm[]` with real non-`-99` return values in every one of 509 `boot_reset_probe()` snapshots. | `DEV/drivers/ArmCpu/ArmCpu.cpp:119-158`, `:607-616`; `DEV/package/BootReset.c:46-50`; `oneplus6-ios/work/tmp/r103_stage6_window.bin` |
| "end-to-end PAS auth is impossible without OEM signing" | **NARROWER.** The signed ZAP blobs are the device's own authentic ones (ROUND 38), so the "cannot forge a signature" objection does not apply to a load of these blobs. What remains unproven is whether the secure world will accept a PAS_AUTH call issued by *this* EL1 kernel on this boot; that is a device experiment, not a source-level impossibility. | `EV/stage5-gpu-a630-registers.md:174-179`, `EV/stage5-gpu-device-firmware.md:76-86` |
| "no MDT loader / no memremap / no SCM call" | **TRUE but a WIRING GAP, not a wall.** The MDT parser is real (`AdrenoMdtV1`, pure struct arithmetic, no I/O); `adreno_mdt_loader_available()` is deliberately `0`. There is no loader, no reserved-region mapping, no PAS driver. These are implementable. | `EV/stage5-gpu-mdt-parser.md:117-127`, `GPU/AdrenoMdtV1.h:7-11` |

**This audit does NOT unblock stage 5.** `modeled=9 / satisfied=0 / BLOCKED` is unchanged
(`GPU/AdrenoGen1BackendV1.c:542-543`); the GPU has never rendered
(`EV/stage5-gpu-kgsl-sysfs.md:85,110-111`). K3 is one of twelve prerequisites.

## §1 — Q① Are the firmware blobs actually obtainable? YES (ROUND 38)

`oneplus6-ios/work/tmp/a630-device-fw/` holds 7 files + `manifest.json`, all sha256
re-verified this round. Durable record `SCR/data/adreno-a630-device-firmware.json`:

- `a630_gmu.bin` 32768 B `b07f9029…c1621f` (legacy flat ITCM, exactly `0x8000`, passes `a6xx_gmu.c:739-750` gate)
- `a630_sqe.fw` 31980 B `34edacb6…ae541e`
- `a630_zap.b00` 148 B / `.b01` 6536 B / `.b02` 1968 B (PIL segments ph[0..2])
- `a630_zap.elf` 14256 B `c9751419…0993`
- `a630_zap.mdt` 6684 B `e0dffc7e…595e` (the file `a6xx_catalog.c:897 .zapfw = "a630_zap.mdt"` names)

Extraction was strictly reversible: one `mount -t ext4 -o ro`, `adb pull`, `umount in
finally`, `VENDOR_MOUNTED_BEFORE=False` / `STATE_RESTORED=True`, no flash / no reboot /
no `dd of=` / no rw remount (`EV/stage5-gpu-device-firmware.md:16-25`). The `.mdt`/`.elf`
are ELF32 `e_machine=0xa4 EM_HEXAGON` (not AARCH64); `p_filesz=[148,6536,1968]` matches
the `.bNN` sizes exactly (`:65-72`, `SCR/data/adreno-a630-device-firmware.json:59-71`).

**Scope correction (must be carried forward).** `EV/stage5-gpu-feasibility.md:35` (2026-11-08,
ROUND 38-era predecessor) says "all 7 A630 firmware names … absent" — that meant
*workspace tree / public mirror*, and `porting.toml:475` `[ROUND 38] [SUPERSEDES the '3
vendor-signed ZAP blobs absent' premise]` already supersedes it. `EV/stage5-gpu-a630-registers.md:174-179`
refines it further: the two public microcode blobs + a public `a630_zap.mbn` exist, but the
**signed split MDT/b00/b01/b02 chain is device-vendor-only** — and this device has it.
`porting.toml:764` still carries the stale historical result string (traceability, not a live claim).

## §2 — Q② Is there an SCM / SMCCC transport in XNU? YES, and it is DEVICE-PROVEN

### 2.1 The conduit is real code, not a citation

`DEV/drivers/ArmCpu/ArmCpu.cpp:119-158`:

- `:119` comment "Qualcomm SCM over SMC (SMCCC SIP, the calls Linux qcom_scm makes from EL1 on this firmware)".
- `:126-137` `static ScmResult scm(uint64_t fn, uint64_t info, uint64_t a2, uint64_t a3)` with `:134 __asm__ volatile ("smc #0" : "+r"(x0), ... )`.
- `:138-142` `scmAvailable()` uses `0xC2000601 QCOM_SCM_INFO_IS_CALL_AVAIL`.
- `:143-158` `scmDisableCrashdump()` issues real SMC32-SIP calls `0xC2000109` (SDI), `0xC2000110` (dload), `0xC2000501` (TCSR read), `0xC2000502` (secure write fallback), storing 8 results in `extern volatile int64_t armcpu_scm[8]` (`:124`, initialised to `-99`).
- `:607-616`: with `enchilada-wdog=3` and a mapped watchdog, `scmDisableCrashdump()` is **actually called** at `:616`.
- `DEV/package/BootReset.c:46-50`: `boot_reset_probe()` prints those 8 values into the boot log as `scm avail sdi=%lld dl=%lld wr=%lld sdi=%lld tcsr=%llx dl=%lld wr=%lld tcsr=%llx`.

Upstream `oneplus6-ios/work/UniXNU/drivers/ArmCpu/ArmCpu.cpp:480-492` has the same raw
`smc #0` PSCI primitive; `…/ArmDTPlatform/ArmDTPlatform.cpp:119` uses `smc #0` for
PSCI SYSTEM_RESET.

### 2.2 It is in the linked CI kernel

`oneplus6-ios/work/tmp/r105_check_kernel_smc.py` scanned CI #246's
`r103_ci246_kernel.macho` (126,128,128 B): `armcpu_scm` = 1, `_armcpu_scm` = 1,
`qcom_scm_pas_auth_and_reset` = 1 (a *citation string*), while
`_qcom_scm_call` / `adreno_zap_shader_load` / `scmDisableCrashdump` / `arm_smccc` = 0.
`SCR/verify-ci-246-adreno.py:203-208` forbids the six leading-underscore **linked
symbols** (`_writel(`, `_readl(`, `_IOServiceOpen`, `_IOConnectCallMethod`,
`_qcom_scm_call`, `adreno_zap_shader_load`); the leading `_` targets Mach-O mangled
symbols, not string literals (ROUND 76 fix). So the CI image has **no callable
qcom_scm/PAS transport** — but it does have the working raw SMC mechanism.

### 2.3 DEVICE-PROVEN on this exact silicon (new this round)

`r105_scm_live.py` / `r105_scm_distinct.py` scanned the authoritative round-103 pulled
window (the same window that carries the `stage6=id1` row):

```
509 × "scm avail sdi=1 dl=0 wr=1 sdi=0 tcsr=10 dl=-1 wr=0 tcsr=0"
distinct tuples = 1 (509/509)
```

- `armcpu_scm[0]=1` (`scmAvailable(0xC2000109)` → SDI call available, `a1=1`)
- `armcpu_scm[1]=0` (`scmAvailable(0xC2000110)` → dload-disable call NOT available)
- `armcpu_scm[2]=1` (`scmAvailable(0xC2000502)` → secure-io-write call available)
- `armcpu_scm[3]=0` (`:148` SDI-off call returned `a0=0` = success)
- `armcpu_scm[4]=0x10` (`:150` TCSR_BOOT_MISC_DETECT read back = `0x10`, download-mode bit set, read succeeded)
- `armcpu_scm[5]=-1` (`:151` dload-disable call returned `a0=-1`, consistent with `[1]=0` "not available")
- `armcpu_scm[6]=0` (`:154` secure-write fallback returned `a0=0` = success; it is what cleared the bit)
- `armcpu_scm[7]=0` (`:157` TCSR after = 0 → the `0x10` download-mode bit WAS cleared)

All eight slots hold real values produced by a sequence of EL1 SMC calls (three
`scmAvailable()` probes plus the register read/write calls at `:148-:156`), not the
`-99` "never executed" sentinel, recorded 509 times across two boot heartbeats.
**The `smc #0` → TrustZone SIP path is proven working on this device**, not merely
compiled. This directly corrects
`EV/stage5-gpu-zap-auth.md:134` ("no SCM transport in XNU") and `porting.toml:688`
("XNU has no qcom_scm equivalent") at the mechanism level — while leaving their
*driver-level* conclusion intact (no exported `qcom_scm` API; `scm()`/`psciCall()` are
`static` file-local).

### 2.4 What is still absent at the API/driver layer

- No generic kext-callable SMC primitive: `scm()` / `psciCall()` are `static`; no `arm_smccc_smc` (NOT FOUND).
- No Qualcomm SCM/PAS driver (NOT FOUND); `CONFIG_XNU_NATIVE_ADRENO=n`.
- `memremap` / `of_address_to_resource` real code NOT FOUND (only `AdrenoZapAuthV1.c` comments/anchors); `ml_io_map` exists but only for the panic region.
- `SCR/verify-ci-246-adreno.py:204-206` forbids the PAS symbols in the image on purpose.

**Verdict Q②** = IMPLEMENTABLE-WITH-EFFORT at the transport layer (proven working),
ABSENT as a today-usable exported API. The Q2 subagent (`c6f7040b`) reached the same
layered conclusion and flagged the same prose correction.

## §3 — Q③ Can an MDT loader / PAS auth land? WIRING GAP, not a wall

`GPU/AdrenoMdtV1.h:7-11` "This is NOT a firmware loader. It PARSES the MDT/ELF32
container … performs no I/O, allocates nothing, calls no SCM, and touches no MMIO";
`:18-21` "the INPUT FILE IS ALREADY ON HAND: round 38 pulled a630_zap.mdt (.b00/.b01/.b02)
read-only from this phone". The parser already decodes the real blob: `e_phoff 52`,
`e_phentsize 32`, `e_phnum 3`, `ph[2]` VALID relocatable, `qcom_mdt_get_size
equivalent = 4096`, `sum of p_filesz = 8652` (`:46-66`).

`EV/stage5-gpu-mdt-parser.md:117-127`: "**A parser is not a loader.** It reads a buffer
the caller already holds: it does not read `.bNN` from storage, does not allocate or
`memremap` the region, does not call SCM, and authorises nothing.
`adreno_mdt_loader_available()` is **0**". `adreno_mdt_ready()` is derived from it.

`EV/stage5-gpu-zap-auth.md:63-84` eight ordered preconditions (Linux
`adreno_gpu.c:30-174`): exactly ONE present today (`firmware-name` = `"a630_zap"`,
S5). S1 `CONFIG_ARCH_QCOM` "not even reachable as written", S2 `zap-shader` child,
S3 `memory-region`, S4 `of_address_to_resource`, S6 `qcom_mdt_get_size`,
S7 `qcom_mdt_load`, S8 `pas_auth_and_reset` all absent. `adreno_zap_auth_ready()=0`.

The PAS call shape is pinned from Linux source, not compiled: `svc=QCOM_SCM_SVC_PIL
0x02`, `cmd=QCOM_SCM_PIL_PAS_AUTH_AND_RESET 0x05`, `arginfo=QCOM_SCM_ARGS(1)`,
`owner=ARM_SMCCC_OWNER_SIP`, `GPU_PAS_ID 13`, and `qcom_scm.c:729 return ret ? :
res.result[0];` — so a zero transport return is NOT success; the secure-world
`result[0]` must also be zero (`EV/stage5-gpu-zap-auth.md:34-61`).

**What remains genuinely open (a device experiment, not a source proof):** whether the
secure world accepts `PAS_AUTH_AND_RESET(13)` issued by this EL1 kernel with these
device-authentic blobs. ROUND 105 did NOT run it (read-only scope). Unlocked-bootloader
notes exist (`oneplus6-ios/notes/device-live/getprop-unlocked.txt`) but say nothing about
PAS enforcement. This is the single honest unknown; it is not the same as "impossible".

**Verdict Q③** = IMPLEMENTABLE-WITH-EFFORT: MDT parse is done, blobs are on hand, the
SMC pipe works; a loader + reserved-region mapping + PAS driver + `result[0]` check
are new code, plus one bounded device experiment to settle secure-world acceptance.

## §4 — The honest boundary (unchanged)

- READ-ONLY round: no GPU code written, no MMIO read/write, no flash, no reboot, no
  device-state change, no CI dispatch, no `PAS_AUTH` executed.
- `modeled=9 / satisfied=0 / BLOCKED` (`GPU/AdrenoGen1BackendV1.c:542-543`);
  9 gates `:211-244`, 3 unmodeled `:245-250`, prereq #10 (`CONFIG_XNU_NATIVE_ADRENO=y`)
  needs all 12.
- `CONFIG_XNU_NATIVE_ADRENO=n`; the GPU has never rendered anything.
- This round **re-classifies K3 from "BLOCKED / hardest single piece" to
  "CONDITIONALLY FEASIBLE"**; it does not satisfy any gate. The dependency map's
  §3 K3 row and §2 ② verdict (`EV/stage5-gpu-render-dependency-map.md:108,198`) are
  now historically superseded by this document and must be read with ROUND 105's
  correction.
- Do NOT restart from UEFI / menu / payload volume — those were solved long ago.

## §5 — Next minimal action (for the user to choose)

Two candidate next rounds, both still bounded:

1. **(i) Continue the SMMU chain** — I1 LPAE page table using the measured
   `coherent_walk=1 → tcr=b5100080` (the `a0100080` in stage 4 is a placeholder),
   I2 real kernel caller owning a context bank, I3 faulted stream out of BYPASS.
   Known not to shorten the distance to (B) on its own (1 of 12).
2. **(ii) Advance K3 from "feasible" to "wired"** — design (not yet run) a
   fail-closed PAS experiment behind the existing stage-6 gate: reuse the proven
   `smc #0` shape, load the device-authentic `.mdt/.b00/.b01/.b02` into a reserved
   region, call `PAS_AUTH_AND_RESET(13)`, report `result[0]` separately, and keep
   `CONFIG_XNU_NATIVE_ADRENO=n` until it is DEVICE-VERIFIED. This is the first K3
   action that could turn a modeled gate into a satisfied one — and it is the one
   this audit's finding newly makes worth considering.

**ROUND 103 technical debt stays registered:** any later LPAE/TCR programming must use
`coherent_walk=1 → b5100080`, never stage 4's `a0100080`.
