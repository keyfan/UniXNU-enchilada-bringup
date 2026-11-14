# Stage 5 (GPU) dependency map: "from here to Adreno actually renders" (到 Adreno 出图 完整依赖图)

**Status: HOST-VERIFIED. RESEARCH / INVENTORY ONLY — no GPU code was written, no
flash, no reboot, no device run, no CI dispatch, no firmware load, no MMIO write.**
`CONFIG_XNU_NATIVE_ADRENO` remains `n` (`configs/enchilada_defconfig:40`); the
`gpu` area of `porting.toml` STAYS `"host-verified"` (`porting.toml:256-258`).
This document does **not** unblock stage 5. It is a map, not a step.

- Date: 2026-11-14 (Asia/Shanghai)
- Device: OnePlus 6 (enchilada, SDM845 / Adreno 630), iPadOS 17.7.11 (21H461, iPad7,1)
- Remote main at time of writing: `c27d7fd85dbed35c8555ba1b11ebbaa394a493d9`
  (ROUND 103 DEVICE-VERIFIED, the stage-6 ID0/ID1 probe).
- Why: the user asked (m04481) "下一步做什么" and confirmed (m04487) the choice
  to pin the **complete dependency graph to "Adreno really participates in
  rendering"** BEFORE opening another three-layer verifier on the next field.
  Rounds 87-90 are the cautionary precedent: without a global map the port
  regressed into UEFI/payload-volume work until the user pointed out (m01539)
  "我之前已经进入 ios 桌面". Rounds 82-103 then produced ~20 rounds of
  individually-correct host/CI/device proof while the admission table never
  moved off `modeled = 9 / satisfied = 0 / BLOCKED`. This map exists so the next
  round works on the *critical path*, not the nearest decomposable field.

Method: local source reads only, each assertion carrying a workspace-relative
`file:line`. Two READ-ONLY research subagents cross-checked the kernel chain and
the userland/interface face; their findings are folded in below. Nothing here is
inferred from a desired outcome.

Path aliases used throughout (all workspace-relative, from
`C:\Users\LCPC\Documents\deepseek-harness\default-workspace`):

- `ONEPLUS/` = `oneplus6-ios/`
- `DEV/` = `oneplus6-ios/work/UniXNU-enchilada/`
- `GPU/` = `DEV/drivers/NativeAdreno/`
- `EV/` = `DEV/evidence/`
- `SCR/` = `DEV/scripts/`

`mNNNNN` refs are this conversation's ACP message ids; they are research
provenance, not live instructions.

---

## 0. What the goal actually is — two DIFFERENT targets, do not conflate

The user's objective (m01874, restated m03375) is literally **"Adreno 真的参与
渲染"**. That is target **(B)**. There is a cheaper target **(A)** that keeps
being mistaken for it:

| | (A) "UI 不崩 / 画面能出来" | (B) "Adreno 真的参与渲染" (the goal) |
| --- | --- | --- |
| Mechanism | CPU software compositing, or an EAGL shim backed by Mesa OSMesa/softpipe | a real A6XX_GEN1 kernel GPU backend driving the Adreno 630 hardware |
| Adreno involved? | **NO** | **YES** |
| Needs SMMU stage-1 / GMU / ZAP / CP ring? | **NO** | **YES, all of it** |
| Precedent | bergamota `bglrender`, HackiOS `libredwood-gl.dylib`, and THIS PORT's current CPU desktop | sheng A740 Metal stack only; NO A630 precedent exists |
| Status today | (partly) already working — the desktop was reached (`notes/home-screen-coreimage-wall.md:5-9`) | BLOCKED, GPU has never rendered (`evidence/stage5-gpu-kgsl-sysfs.md:85,111`) |

**Honest re-framing:** the wall is NOT "cannot reach the desktop". The desktop
was reached. `NOTES-userland.md:24` states it plainly — "NativeAdreno, adreno
kits, accelerator | sheng's Adreno 740 | **n/a (enchilada renders on the CPU)**";
`board.toml:384-395` arms `offline-legacy-cpu-present=1` / `offline-legacy-
transfer=1` / `sheng-vsync=60`, i.e. CPU copy of each IOSurface into the GOP
framebuffer with a software vsync timer. The gap is that the **GPU is absent
from the entire composition path**, and any on-demand rasterisation that wants a
GL/Metal context has none (`notes/home-screen-coreimage-wall.md:66-67,75-79`).

[VERDICT: the goal is (B); (A) is a useful bypass but is NOT the objective.]

---

## 1. HONEST CORRECTION — the "CoreImage GLContext crash" is NOT a verified anchor

The planning option (m04486) cited `CI::GLContext::GLContext` →
`EXC_BAD_ACCESS at 0x0` / `usedImages[312] {"size":0,...}` as "the real crash
point". **That must not be carried into the map as fact.**

- `evidence/stage5-gpu-feasibility.md:38-47` records the correction verbatim:
  the porting.toml gpu area's earlier finding "said SpringBoard crashed inside
  `CI::GLContext::GLContext` ... **This has no literal evidence in 246 decoded
  logs** — `CoreImage`/`GLContext`/`mach_vm_allocate_kernel`/`vm_map_enter` are
  all zero. The actual `CRASH` rows are `PerfPowerServices sig6` (SIGABRT). We
  keep that entry marked `[HISTORICAL, unverified external inference]`."
- `evidence/stage5-gpu-feasibility.md:36`: GPU-driver literals all zero across
  246 decoded logs (`IOGPU`/`IOAccelerator`/`AGX`/`Metal`/`[gpu]`/`CoreImage`/
  `mach_vm_allocate_kernel`/`Adreno`/`kgsl`/`GMU`/`SPTPRAC`).
- `evidence/stage4-welcome.md` + `porting.toml:253`: "the CoreImage GLContext
  crash has no literal log evidence (244 logs scanned, real CRASH rows are
  PerfPowerServices sig6)".
- The `usedImages[312]` backtrace in `notes/home-screen-coreimage-wall.md:44-64`
  is explicitly from "older `diag2-k158` export", not a current-boot literal log.

**Consequence for the map:** the userland end is anchored to the *measured* state
(`[gpu clients]` empty, `[bo pool] 0 BOs`, `gpu perf 0 bw 0` —
`notes/home-screen-coreimage-wall.md:75-79`; `evidence/stage5-gpu-kgsl-globals.md:36`
"17 entries" whose every IOVA is "exactly `0x0-0x0` with a degenerate range"
(`:132-134`), and `evidence/stage5-gpu-kgsl-sysfs.md:98` "**bound and idle, never
driven**"), NOT to an unverified crash signature. The three cheap candidates still
open in `notes/home-screen-coreimage-wall.md:113-118` (`text_fixup` returning a
software `CIContext`, a `CIContext`-level stub, or enabling the upstream
Adreno740+AdrenoMetal "expensive path") belong to target (A) and are registered
in §5, not to the critical path.

---

## 2. The three facts that had to be investigated (verdicts)

| Q | Question | Investigated result | Verdict |
| --- | --- | --- | --- |
| ① | Can CI's `macos-26 + Xcode 26.6` build Mesa/freedreno/OSMesa/Turnip shim? | **The OS/toolchain is present but the workflow does not do it.** `notes/enchilada/ci-kernel.yml:41` `runs-on: macos-26`, `:44` `DEVELOPER_DIR: /Applications/Xcode_26.6.app/Contents/Developer`, `:71-77` `brew install llvm lld mtools dtc` (+unifdef). But the whole build is `:79-110` `./unixnu defconfig/fetch/build kernel` + `device/scripts/build-darwinboot.sh` + `./unixnu build payload` + `mcopy`; there is **NO** `meson` / `build-turnip-darwin.py` / `xcrun --sdk iphoneos` / `codesign` / `hdiutil` / `add-gpu-plugin` / `make-gpu-addon` step. `evidence/stage5-gpu-metal-publish.md:31-38` pins this against the workflow sha256 `6629ee5180434b7ba8d638f50542ecd83b1f373240a0efcf4c28f7483fe7bb0b`. Upstream `work/UniXNU/docs/porting/gpu.md:100-115` shows the *separate* macOS-only commands that exist but are never invoked. A cheaper route exists: `bergamota-README.md:136-145,155-162` publishes **prebuilt** `berga-tools.zip` (EAGL shim + `libOSMesa` + `libdrm` stand-in) fetchable by `renegade fetch-tools` with "only Python 3, on macOS and on Linux, no gh". | **PIVOT**: do not make CI compile Mesa. Either add a dedicated macOS frontend workflow step, or use the prebuilt payload — but note (§5) that this serves target (A), not (B). |
| ② | Is the zap/GMU firmware signing chain obtainable on XNU? | **BLOCKED / ABSENT.** `evidence/stage5-gpu-zap-auth.md:63-80`: of the eight ordered preconditions in `adreno_gpu.c:30-174`, **exactly ONE is present** (`firmware-name` = `"a630_zap"`, S5); S1 `CONFIG_ARCH_QCOM` is "not even reachable as written" (XNU is not Linux), S2 `zap-shader` child node, S3 `memory-region`, S4 `of_address_to_resource`, S6 `qcom_mdt_get_size`, S7 `qcom_mdt_load`, S8 `pas_auth_and_reset` all absent. `:133-135` "there is no SCM transport in XNU, no MDT parser, no memremap equivalent". The call shape (`qcom_scm_pas_auth_and_reset`, svc `0x02`, cmd `0x05`, `GPU_PAS_ID 13`) is **CITED only** from pinned Linux source, never compiled: `SCR/verify-ci-246-adreno.py:203-206` *forbids* `_qcom_scm_call` / `adreno_zap_shader_load` in the image; `evidence/stage5-gpu-mdt-parser.md:119-122` "a parser is not a loader ... authorises nothing". `evidence/stage5-gpu-feasibility.md:35`: all 7 A630 firmware names absent from the workspace. | **BLOCKED**: hardest single piece. Needs a fresh XNU SMC conduit to the SIP owner, an MDT loader, a storage path, the S2/S3/S4 node-shape decision, and signed blobs. Not advanceable from a host-only round. |
| ③ | What is the userland → GPU interface face? | **NO GPU-accelerator implementing module** (display-side IOKit classes exist but are CPU-path only — see the precision note below). The kernel/user-client `NativeAdrenoABI.h` is "Versioned kernel/user-client wire ABI ... **NOT** the Metal, IOKit or Turnip ABI" (`evidence/stage5-gpu-metal-seam.md:72-81`); `AdrenoMetalSeamV1.c` "is not reachable from any IOKit user client" (`:186`), `frontend_allowed()==0` (`:64`). `evidence/stage5-gpu-caller-v1.md:130` "The IOKit `IOService` itself is not written." `evidence/stage5-gpu-darwin-kmd-abi.md:3-4` "no MTLDevice, no IOKit user client, no Turnip dylib build". CI verifiers forbid `_IOServiceOpen`/`_IOConnectCallMethod` (`evidence/stage5-gpu-ci-239.md:83`). Display is CPU: `drivers/NativeMobileDisplay/README.md:5,22` "CPU and GPU scanout paths work ... The GPU hand-off (`sheng-fb-gpu=1`) is not used for a logical display"; `configs/enchilada_defconfig:42` `CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y`, `:41` `CONFIG_XNU_NATIVE_MOBILE_DISPLAY=n`. `NativeGpuTrace.h:11-16` Submit/Retire/Display/Turnip* events are a **trace ABI schema** (`native_gpu_trace_enabled/record/snapshot`, `:36-38`), not produced GPU events on this board. | **BLOCKED**: no **GPU-accelerator** IOKit user client, no accelerator service, and no path from backboardd/CoreAnimation to the GPU (display-side `IOService`/`IOUserClient` classes DO exist but are CPU-path only — see the precision note below). The GPU face still has to be authored. |

**Precision note (independent second read, ROUND 104):** the phrases "no IOKit user client" and "NO implementing module" above mean **no GPU-accelerator client** — the display driver DOES contain real IOKit classes, they just are not wired to the Adreno: `drivers/NativeMobileDisplay/NativeGOPPresenter.cpp:33` "class NativeGOPPresenter : public IOService", `:74` "class NativeGOPClient : public IOUserClient", `:295` `newUserClient`, `:347-353` `externalMethod`, `:355` `clientMemoryForType`; `NativeLegacyDisplay.cpp:974` `newUserClient`; `NativeMobileDisplay.cpp:6` "Prototype only. No registry publication or display operation is enabled." The ONLY GPU hand-off is a **written-but-unarmed** seam: `NativeGOPWire.h:11-14` "GPU hand-off (boot-arg sheng-fb-gpu=1): the display driver offers a finished frame instead of copying it ... Nothing here copies pixels.", wired at `NativeGOPPresenter.cpp:107` (`PE_parse_boot_argn("sheng-fb-gpu", ...)`), yet `board.toml` contains ZERO `sheng-fb-gpu` and `scripts/build-native-legacy-display.py:23` "the presenter only acts with sheng-fb-gpu=1, which enchilada does not set". Composition is explicitly absent: `NativeLegacyDisplay.cpp:575-577` "copied through the IOSurfaceAccelerator scaler, which does not exist here (backboardd aborts: "Failed to create IOSurfaceAccelerator"). This display scans out one layer." `NativeGpuTrace.h:7-8` "Diagnostic ABI, independent of rendering policy" — its only definitions are weak no-op stubs (`package/BootProgressStubs.c:91-92`), so `Submit`/`Retire`/`Turnip*` events have ZERO producers. So U1/U3 "NO implementing module" is about the **GPU accelerator** face, not a claim that no IOKit C++ class exists anywhere in the tree. None of this changes the (B) verdict: the CPU display path works and the GPU path is still unauthored.

---

## 3. The main chain — kernel side (K1..K8)

`modeled` = a host-verified freestanding model exists in this tree; `satisfied` =
a device-capable backend HOLDS it. Authoritative: `drivers/NativeAdreno/
AdrenoGen1BackendV1.c:192-265` (`enchilada_gen1_backend_admit`), 12 prereqs,
9 modeled / 0 satisfied / BLOCKED (`:542-543`). 31 C units in `GPU/`.

| ID | Segment | 现状 (state) | 先例 (precedent) | CI 能否编 | 需上机 | 硬阻塞 | 结论 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| K1 | CX/GX power + GDSC | **CX raised on phone** (`evidence/stage5-gpu-cx-raised-device.md:38` `cx_after=80001fe0` BIT31=1); **GX still down** (`:34` `gx_off=1`). Sequence host-verified. | sheng A740 (cross-gen, not reusable) | YES (#246 31 obj) | was done | GX domain not brought up | **CONTINUE** (narrow: GX) |
| K2 | GPUCC/RPMh clocks | HOST-VERIFIED models only (`evidence/stage5-gpu-clk-ctl-v1.md:1`, `gpucc-window-v1.md:3`); `porting.toml:675` "only then does the round-28 gated GPUCC READ become the first honest device action" — not done. `evidence/stage5-gpu-devmem-ceiling.md:3` `/dev/mem` ENXIO. | sheng | YES | YES | no real clk backend | **CONTINUE** |
| K3 | GMU firmware + ZAP/MDT/SCM signing | SHAPE/PARSER/INVENTORY ONLY; 1/8 preconditions; no SCM transport (`evidence/stage5-gpu-zap-auth.md:78,133-135`; mdt-parser `:119-122`). 7 firmware names absent. | sheng (different PAS, `GPU_PAS_ID 13` is A630) | n/a (needs TrustZone) | YES | **HARD BLOCKER #2** | **BLOCKED** |
| K4 | HFI v1 / GMU cold boot | HOST-VERIFIED (`hfi-v1.md:1`, `gmu-boot-v1.md:1`, `gmu-bus-v1.md:1`, `gmu-caller-v1.md:1`); one narrow device bootstate (`bootstate-device.md:1`). A630 = **legacy GMU, HFI v1 + SPTPRAC** (`evidence/stage5-gpu-feasibility.md:32`). | sheng GEN2 GMU (different) | YES | YES | depends K3 | **BLOCKED** (by K3) |
| K5 | kgsl SMMU stage-1 context | Host word/derivation model + **device-measured inputs**: `ID2=0x00007511 → ias=48` (`ias-measured-device.md:3`), `ID0=0x48016605 → CTTW=1 → tcr=b5100080`, `ID1=0x20008005 → NUMCB=5/NUMS2CB=0/4 KiB` (`stage6-device.md:3`). But `evidence/stage5-gpu-smmu-stage1.md:115-116` "There is no MMIO, no mapping, no page table, no ASID, no IOKit, no caller, and **all four inputs are absent**"; `ready()=0`, `missing()=0xf` (`:71`). `porting.toml:758` "the PORT has no page table and no ASID allocator; the DEVICE has both inside a driver the port cannot use". | sheng ArmSmmu4K (A740) | YES | YES | I1 LPAE table / I2 owner / I3 faulted stream | **CONTINUE** (closest decomposable field — but see §6) |
| K6 | CP ring / IB1/IB2 submission | HOST-VERIFIED order+payload+header (`cp-ring.md:3`, `cp-ring-closed.md:3` "all five round-70 unknowns DEFINED"). `:106-114` "Every register word is now COMPUTABLE. None has been WRITTEN ... the power sequence, the PAS handshake and the SMMU context are all still absent". | sheng AdrenoPM4 | YES | YES | depends K1-K5 | **BLOCKED** (by upstream deps) |
| K7 | BO / IOVA / interrupts / fences | **NO implementing C module.** Only wire-ABI layout (`NativeAdrenoABI.h:96` `NativeAdrenoAbiBoImport`, `MaxBuffers=128`, `MaxBoBytesShift=28`) and IRQ constants (`AdrenoBoardInputs.h:81-84` 300/304/305). `evidence/stage5-gpu-kgsl-globals.md:36` 17 globals, `:132-134` "every IOVA is exactly `0x0-0x0`", `:28` `active_cnt = 0`. `cp-ring.md:185-193` "no BO, no IOVA, no fence, no interrupt, no submit". | sheng NativeAdrenoVm/Client | YES (ABI only) | YES | **HARD BLOCKER #1** (GEN1 backend) | **BLOCKED** |
| K8 | actual render / known image | **NONE.** `evidence/stage5-gpu-a630-registers.md:7` "submits no command, and renders nothing", `:239` "A6XX GEN1 kernel backend is NOT implemented"; `kgsl-sysfs.md:85,111` `gpu_busy_percentage 0 %`, `reset_count = 0` "GPU has never actually rendered anything". | sheng (A740) | n/a | YES | all of K1-K7 | **BLOCKED** |

**Upstream ordering rule (load-bearing):** `work/UniXNU/docs/porting/gpu.md:22-28`
"**For another GPU generation, implement the matching kernel backend and
compiler support before selecting its frontend**"; `:57-73` memory/submission,
`:100-115` "## 4. Build and stage Metal" is a macOS step that comes *after*. The
four hard blockers (`evidence/stage5-gpu-metal-publish.md:126-135`): (1) A6XX_GEN1
kernel backend, (2) signed ZAP / TrustZone PAS `GPU_PAS_ID 13`, (3) kgsl SMMU
stage-1 context `arm,smmu-kgsl@5040000` SIDs 0/2,1/4/5 "must first be made to
fault", (4) macOS frontend + selected OS Metal/IOSurface ABI.

---

## 4. The userland / interface face (U1..U4)

| ID | Segment | 现状 | 先例 | CI 能否编 | 需上机 | 硬阻塞 | 结论 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| U1 | IOKit user client / `IOService` / `IOConnectCall` / accelerator | **NO GPU-accelerator implementing module** (display-side `IOService`/`IOUserClient` classes exist but are CPU-path only — §2 precision note); only pure-C wire-ABI mirrors `AdrenoMetalSeamV1.c`, `AdrenoDarwinKmdAbiV1.c`, `AdrenoMetalPublishV1.c`. `frontend_allowed()==0`, "not reachable from any IOKit user client" (`metal-seam.md:64,186`); `_IOServiceOpen`/`_IOConnectCallMethod` forbidden in image (`ci-239.md:83`). | sheng `NativeOfflineAccelerator.cpp` / `IOAcceleratorES->MetalPluginName->initWithAcceleratorPort` discovery (`metal-publish.md:20`) | n/a (needs macOS SDK + OS ABI) | YES | **HARD BLOCKER #4** | **BLOCKED** |
| U2 | Mesa/Turnip/metal2vulkan frontend + plugin | Pinned Mesa commit `85fda3b36405bcaea8bffc64aa4f2651dc90770d` **already has A630** `GPUId(630)/CHIP.A6XX/a6xx_gen1` (`evidence/stage5-gpu-darwin-kmd-abi.md:22-32`) — compiler is NOT the hard blocker. `-Dtu-darwin-abi-dir` is a ONE-HEADER directory contract (`:59-66`). But no frontend is built: `ci-kernel.yml` has no such step (§2①), `metal-seam.md:190-193` "Windows workspace lacks the recorded XNU ios-obj tree. Only the macOS CI can link it". | sheng A740 Turnip dylib | macOS CI *could* but workflow doesn't | needs macOS host | #4 | **PIVOT** (add a workflow step, or use prebuilt) |
| U3 | Display / presenter / composition | ACTIVE = CPU: `README.md:5,22` + `defconfig:42` + `board.toml:384-395`. `sheng-fb-gpu=1` GPU hand-off "not used for a logical display" (`README.md:22`) is **written-but-unarmed dead code**: `NativeGOPWire.h:11-14` wires it at `NativeGOPPresenter.cpp:107` but `board.toml` has ZERO `sheng-fb-gpu` and `build-native-legacy-display.py:23` "the presenter only acts with sheng-fb-gpu=1, which enchilada does not set". `NativeLegacyDisplay.cpp:575-577` "which does not exist here (backboardd aborts: \"Failed to create IOSurfaceAccelerator\")". `NativeGpuTrace.h` events are a trace ABI with **zero producers** (only weak no-op stubs `BootProgressStubs.c:91-92`). | sheng GPU scanout (A740) | YES (driver already there) | YES | needs K8 + a presenter | **BLOCKED** |
| U4 | CoreImage / EAGL context availability | Measured emptiness (`home-screen-coreimage-wall.md:75-79` `[gpu clients]` empty, `[bo pool] 0 BOs`, `gpu perf 0 bw 0`); the `CI::GLContext` crash is **unverified** (§1). Cheap software-context candidates exist (`:113-118`). | bergamota `bglrender`, HackiOS `libredwood-gl.dylib` | macOS (prebuilt available) | YES | none for (A); all for (B) | **PIVOT** — belongs to target (A), see §5 |

---

## 5. Branch A — the cheap bypass that does NOT satisfy the goal (registered, not recommended as the goal)

Two real precedents show how to get "a rendered UI without the Adreno":

- **bergamota** (same SDM845 / A630): `notes/refs/bergamota-README.md:63`
  "Working on | GPU acceleration on the Adreno 630 (OpenGL ES over Mesa
  freedreno, **then Metal**)" — i.e. GPU acceleration is explicitly NOT working;
  Metal is further out. `:136` `berga-tools.zip` = "the EAGL shim, Mesa's
  `libOSMesa`, our `libdrm` stand-in", prebuilt for the phone; `:139`
  `renegade fetch-tools` "needs only Python 3, on macOS and on Linux, no gh";
  `:155` "Mesa 25.0.7 is cross-built for iOS (about 1.6 GB scratch, `brew install
  meson bison`), then the shim and the launcher are compiled and signed".
- **HackiOS / scion** (`notes/scion/GPU.md:45-61`): replaces backboardd's
  `EAGLContext` with a `gl*` table (`libredwood-gl.dylib`); "CoreAnimation's
  render server tries Metal, then OpenGL ES, then its own software rasterizer";
  behind it "Mesa 25.0.7 through OSMesa ... On the Poco X5 Pro that's freedreno
  on the Adreno 642L, through redwood-drm ... **Without a GPU it's softpipe**";
  ":58-61 "iOS 16.5's CoreAnimation renders its whole UI through OpenGL ES if
  offered one ... on real hardware". AVF effort estimated `:176-180` **~3-5
  person-months** to SpringBoard-on-GPU, +6-12 PM for Metal apps; status
  `:344-357` **paused**.

**Why this is not the answer to (m01874):** `libOSMesa`/softpipe is CPU software
rendering — the Adreno is idle. `freedreno` over `redwood-drm` DOES touch the
GPU, but only because scion wrote its **own kernel DRM backend** (`redwood-drm`,
"a libdrm stand-in that talks to the kernel's RedwoodGPU"); on enchilada the
equivalent is exactly K3-K8, i.e. the hard path. So Branch A either (a) fixes
the null EAGL context with software and never involves the Adreno, or (b) is
the same hard path under a different name. It does, however, explain why the
`text_fixup` / `CIContext` software-context candidates in
`home-screen-coreimage-wall.md:113-118` are the cheapest way to remove the UI
rasterisation wall — that is a legitimate **parallel** objective, not the goal.

[VERDICT: PIVOT a *separate* work-item for Branch A if UI stability is wanted;
keep the critical path on (B).]

---

## 6. Per-segment verdict + the single smallest next action

| Critical-path order | Verdict | Minimal next action |
| --- | --- | --- |
| K1 GX domain | CONTINUE | extend the stage-5 power-up probe to bring GX up and re-read `gx_on` (narrow, device-gated) |
| K3 signing chain | **BLOCKED** | no host-only action exists; needs a design decision on an XNU SMC conduit + signed-blob provenance before any code |
| K5 SMMU stage-1 | CONTINUE | I1 LPAE table (ias=48 / 4 KiB / TTBR1 / **coherent_walk=1 → b5100080**), I2 real bank owner, I3 faulted stream |
| K6/K7/K8 | BLOCKED | gated by K1-K5 |
| U1/U2/U3 | BLOCKED / PIVOT | gated by the kernel backend; U2 additionally needs a macOS frontend workflow step |

**The load-bearing observation of this whole map:** every kernel-side segment is
a HOST-VERIFIED model (with a handful of narrow DEVICE-VERIFIED read-only
register reads); NOTHING is wired into a running driver. `modeled=9 /
satisfied=0 / BLOCKED`; `CONFIG_XNU_NATIVE_ADRENO=n`; the GPU has never
rendered. The signing chain, BO, interrupts, GPU-accelerator IOKit user client and render
segments have **no implementing module at all** (display-side IOKit classes
exist but are CPU-path only, §2 precision note). Adding another field-level
verifier (e.g. I1 LPAE) is real progress but moves `satisfied` from 0 to at
most 1 of 12 — it does not, by itself, shorten the distance to (B). The next
round should therefore pick EITHER (i) continue I1 as an explicitly-scoped
"1-of-12" increment with this map attached, OR (ii) escalate K3 (the signing
chain) as the true long-pole item. This document deliberately does not choose
for the user; it makes the choice informed.

---

## 7. Technical debt registered from ROUND 103 (must not be lost)

`evidence/stage5-gpu-stage6-device.md` + `porting.toml:560`: the phone measured
`ID0=0x48016605` with `CTTW = BIT(14)` SET and `sdm845.dtsi` has **NO
`dma-coherent`** on `adreno_smmu` (`:4903-4918`), so `fw_says_coherent=0` and
upstream `arm-smmu.c:1799-1805` (`cttw_fw OR cttw_reg`) makes
**`coherent_walk = 1` — this silicon is coherent**. Stage 6 recomposes stage-1
words to `tcr=b5100080`; the round-101 `stage4=id2` row `tcr=a0100080` was a
PLACEHOLDER computed with `coherent_walk(0u,0u)` BEFORE ID0 was available.
`AdrenoSmmuCtxWordV1.c:106-121` warns "the two branches are NOT free variations
-- picking one without IDR0.CTTW would lie about the silicon" (difference
`0x15000000` = TCR.SH/IRGN/ORGN).

**ANY later LPAE page table, TCR, or context-bank programming MUST use
`coherent_walk = 1 / b5100080`, NEVER the stage-4 `a0100080`.** This is the one
device-measured fact that a future I1 round must inherit.

---

## 8. Admission table + HONEST BOUNDARY

- `drivers/NativeAdreno/AdrenoGen1BackendV1.c:542-543`: "Admission: 9 modelled
  (round 77 added BOARD_VA_MAP's derivation), 0 satisfied, BLOCKED." The 9
  modeled gates (`:211-244`): `power-path`, `gpucc-windows`, `gmu-cold-boot`,
  `hfi-v1-transport`, `firmware-blobs`, `zap-auth`, `kgsl-smmu-stage1`,
  `cp-ring`, `board-va-map` — all `satisfied=0`. 3 unmodeled (`:245-250`):
  `device-proof`, `config-on`, `macos-frontend`.
- `configs/enchilada_defconfig:40-42`: `CONFIG_XNU_NATIVE_ADRENO=n`,
  `CONFIG_XNU_NATIVE_MOBILE_DISPLAY=n`, `CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y`.
  `=y` requires prereq #10 `config-on`, gated on all 12 (`:261-263`).
- `porting.toml:675` (ROUND 84) strict order: CX up → SMMU probe/program →
  zap-auth (largest single piece) → only then the first honest GPUCC read; the
  three unmodeled prerequisites "still need a running driver or a macOS host and
  cannot be advanced from this workspace at all".

**HONEST BOUNDARY:** this is a research/inventory map. No device run, no flash,
no reboot, no CI dispatch, no firmware load, no MMIO, no code. It does not
change any verdict, does not move `satisfied`, does not unblock stage 5. The
`gpu` area STAYS `"host-verified"`. The "CoreImage crash" is explicitly marked
unverified (§1). The two targets (A)/(B) are explicitly separated (§0) so a
software-render fix is never reported as "Adreno renders".

---

## 9. Evidence index (every assertion above is greppable)

| Topic | Source |
| --- | --- |
| Goal / direction | m01874, m03375, m04481, m04487; `ONEPLUS/HANDOFF.md:152` |
| Desktop reached (CPU) | `ONEPLUS/notes/home-screen-coreimage-wall.md:5-9,66-67,75-79,113-118`; `DEV/NOTES-userland.md:24` |
| CoreImage crash correction | `DEV/evidence/stage5-gpu-feasibility.md:35-47`; `DEV/evidence/stage4-welcome.md`; `DEV/porting.toml:253` |
| CI workflow | `ONEPLUS/notes/enchilada/ci-kernel.yml:41,44,71-110,112-142`; `DEV/evidence/stage5-gpu-metal-publish.md:31-38` |
| Upstream generation-first rule | `ONEPLUS/work/UniXNU/docs/porting/gpu.md:22-28,57-73,100-115` |
| 4 hard blockers / 9 gates | `DEV/evidence/stage5-gpu-metal-publish.md:126-135`; `DEV/drivers/NativeAdreno/AdrenoGen1BackendV1.c:192-265,542-543` |
| Signing chain (1/8) | `DEV/evidence/stage5-gpu-zap-auth.md:63-80,133-135`; `DEV/evidence/stage5-gpu-mdt-parser.md:119-122`; `DEV/scripts/verify-ci-246-adreno.py:203-206` |
| SMMU / ASID / IAS device reads | `DEV/evidence/stage5-gpu-ias-measured-device.md:3`; `DEV/evidence/stage5-gpu-stage6-device.md:3`; `DEV/evidence/stage5-gpu-smmu-stage1.md:71,113-119`; `DEV/porting.toml:560` |
| User-client / wire ABI | `DEV/evidence/stage5-gpu-metal-seam.md:64,72-81,181-193`; `DEV/evidence/stage5-gpu-metal-publish.md:20`; `DEV/evidence/stage5-gpu-darwin-kmd-abi.md:3-4,22-32,59-66`; `DEV/evidence/stage5-gpu-caller-v1.md:130`; `DEV/evidence/stage5-gpu-ci-239.md:83` |
| Display / presenter | `DEV/drivers/NativeMobileDisplay/README.md:5,22`; `DEV/drivers/NativeMobileDisplay/NativeGOPWire.h:11-14`; `DEV/drivers/NativeMobileDisplay/NativeGOPPresenter.cpp:33,74,107,295,347,355`; `DEV/drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp:575-577,974`; `DEV/drivers/NativeMobileDisplay/NativeMobileDisplay.cpp:6-7`; `DEV/drivers/NativeMobileDisplay/NativeGpuTrace.h:7-16,36-38`; `DEV/scripts/build-native-legacy-display.py:23`; `DEV/board.toml:384-395` (NO `sheng-fb-gpu`); `DEV/configs/enchilada_defconfig:40-42` |
| bergamota / HackiOS precedents | `ONEPLUS/notes/refs/bergamota-README.md:63,123,136-145,155-162`; `ONEPLUS/notes/scion/GPU.md:45-61,121-127,176-180,291-294,344-357` |
| "never rendered" | `DEV/evidence/stage5-gpu-kgsl-sysfs.md:85,111`; `DEV/evidence/stage5-gpu-kgsl-globals.md:3`; `DEV/evidence/stage5-gpu-a630-registers.md:7,239` |
