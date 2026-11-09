# Stage 1 memory — the ArmSmmu4K kit is now linked, and it says Busy

Device: OnePlus 6 (enchilada), SDM845, 8 GB.
OS profile: iPadOS 17.7.11 (21H461, iPad7,1).
Status of the claim below: **device-verified.** The read-only ArmSmmu4K
preflight ran on real hardware (boot_id 551765171, uptime 898 s);
this file records both what the build proves and what the device confirmed.

## What changed, and why it is not a driver

`porting.toml`'s memory area has said, accurately, that the ArmSmmu4K kit was
*listed as the intended component but was NOT yet linked*, and that the port
instead reuses the firmware's bypass slots. That was true: `grep -c ArmSmmu4K`
over the CI logs of runs #215 and earlier returned zero, so no compile unit of
the kit had ever been processed.

This build links it. **It does not program a stage-1 context, enable a bank,
write an SMR, or write an S2CR.** The kit is used for exactly one entry point:

```c
ArmSmmu4KStatus ArmSmmu4KCheckFaultedStreams(const ArmSmmu4KIo *,
    const ArmSmmu4KStream *, unsigned count, unsigned bank,
    ArmSmmu4KStreamResult *);
```

whose own header documents it as *"Read-only preflight of global fault policy,
target faulting slots and all conflicting live routes, including unrelated SIDs
using the selected bank."* The question it answers is the one the hand-rolled
survey in `drivers/NativeSmmu` was never able to answer: **could a stage-1
context legally be programmed right now, at all?**

## Why only four of the kit's units

`scripts/build-enchilada-smmu.py` builds `ArmSmmu4K.c`,
`ArmSmmu4KContext.c`, `ArmSmmu4KStreams.c` and `ArmSmmu4KProgram.c`. Compiled
locally with `-std=c11 -O2 -ffreestanding -fno-builtin`, the only undefined
`ArmSmmu4K` symbols those four leave are ones they define among themselves:

```
$ llvm-nm --defined-only *.o | grep ArmSmmu4K
ArmSmmu4KBuild              ArmSmmu4KContext.c →
ArmSmmu4KBuildUpper         ArmSmmu4KContext.c →
ArmSmmu4KEncodeContext
ArmSmmu4KValidateContextReadback
ArmSmmu4KCheckFaultedStreams    ArmSmmu4KStreams.c
ArmSmmu4KAttachStreams          ArmSmmu4KStreams.c
ArmSmmu4KObserveStreams         ArmSmmu4KStreams.c
ArmSmmu4KObserveOwnedStreams    ArmSmmu4KStreams.c
ArmSmmu4KFaultStreams           ArmSmmu4KStreams.c
ArmSmmu4KProgramDisabledContext ArmSmmu4KProgram.c
ArmSmmu4KObserveOwnedContext    ArmSmmu4KProgram.c
… (7 from Program)
```

`ArmSmmu4KLive.c`, `ArmSmmu4KAudit.c`, `ArmSmmu4KTableMemory.c` and
`ArmSmmu4KMmio.c` are deliberately excluded. None of them adds a check this
probe reports, and every extra unit widens the linked surface without producing
evidence. This is the same discipline the display and battery work used: link
what answers the question, not the whole kit.

## Design of the read-only enforcement

A comment saying "read-only" is not enforcement. The `ArmSmmu4KIo` is a table of
six callbacks, and the kit's `prepare()` only checks that each is non-NULL. So
the three mutating callbacks are implemented to be *observably* inert:

```c
unsigned gKitWriteCalls = 0;

int kitWrite32(void *, size_t, uint32_t) {++gKitWriteCalls; return 1;}
int kitWrite64(void *, size_t, uint64_t) {++gKitWriteCalls; return 1;}
```

They return failure **without touching the mapping**. The verdict line then
publishes that counter:

```
enchilada-smmu: rc=0 SS … | K pre=<ufs>/<usb> r=<rufs>/<rusb> w=<writes> …
```

So the device itself reports how many stores the kit attempted. If any future
kit entry point starts writing, `w=` becomes non-zero on the console rather than
silently corrupting a live SMMU.

## Verification 1 — the CI-equivalent C++20 syntax check

The shim must pass the same standard CI holds it to. Locally the shim
(`-Wdeprecated-volatile` **with** `-Werror`, CI #207's lesson) compiles clean:

```
clang++ -std=c++20 -target arm64-apple-darwin -fsyntax-only -Wall -Wextra
        -Werror -Wdeprecated-volatile … package/EnchiladaSmmuProbe.cpp
exit 0
```

This caught a real defect on the way in: the write counter was used before it
was declared. The first run of this check exited 0 only because a PowerShell
`| Select-Object -First 40` pipeline masked the exit code — re-running it with
the status written to a file gave `exit 1` and named the two undeclared
identifiers.

## Verification 2 — the four-unit closed set and the live-state replay

`work/tmp/verify_smmu_kit_2.py` takes a different route: it compiles each kit
unit with the flags of the kit's **own** checker
(`scripts/check-smmu4k-streams.py:60`: `-Wall -Wextra -Werror -ffreestanding
-mgeneral-regs-only` — deliberately not this repo's stricter
`-Wconversion`/`-Wsign-conversion`, which hold our sources, not unmodified
upstream code), verifies nothing in the set is left undefined, then links them
against a host harness that reproduces the shim's bridge and replays the live
register state recorded from flash #7.

```
status=10 writes=0 must_retain=1 reads=15 verified=0 faulted=0 id0=4800164c
changed=0
```

Four facts, each load-bearing:

- `status=10` is `ArmSmmu4KBusy`.
- `writes=0`: the kit invoked no mutating callback.
- `must_retain=1`: the kit explicitly says resources must be retained, i.e. it
  **did not take ownership** of anything.
- `changed=0`: `memcmp` over the whole 512 KiB register image is identical before
  and after the call. This is stronger than the counter — a bug that wrote
  around the callbacks would show up here even if `w=` stayed 0.

## What the Busy verdict means

`ArmSmmu4KStreams.c` returns `ArmSmmu4KBusy` from `scan()` when an un-attached
owned slot's S2CR is not in the FAULT state:

```c
} else if((s2cr&~valid_s2cr(r->id0))!=FAULT ||
    (live && smr!=(valid_smr(r->id0)|s[own].sid))) return ArmSmmu4KBusy;
```

The flash #7 record shows every one of the eight live streams in **bypass**
(`valid=8 t0=8 t1=0 t2=0`, S2CR carrying only a bank). A board running the
firmware's bypass handoff is therefore exactly the state the kit calls Busy.

**That is not a failure of this probe. It is the gate on the next step**: to own
translation a driver must first make the target streams fault. Recording a Busy
verdict turns "we have not written a stage-1 context yet" into "we know
precisely what must happen before one can be written."

## Honest boundaries

- The kit is **linked and exercised**, not **trusted to translate**. Nothing here
  programs tables, enables a context, or moves a single device's translation.
- The Busy verdict was first a prediction from a local replay of recorded
  registers, then confirmed on device (see Device run below): pre=10/10,
  w=0, id0=4c017e4c. The memory area's limits text is now updated.
- The replay's `ID0` SID-width field is an assumption (11 bits), because the
  hand-rolled survey never logged ID0. It is a *lower bound*: the DWC3 stream
  `0x740` needs bit 10 to be matchable at all. On device the assumption
  disappears — `ArmSmmu4KCheckFaultedStreams` reads `0x20` itself and reports the
  real ID0 back in `r.id0`, which the verdict publishes as `id0=`.
- `ArmSmmu4KCheckFaultedStreams` was called for the UFS and DWC3 streams
  separately, not for all eight: those are the two the port actually drives, and
  each call is a bounded preflight rather than a survey.
- Fixtures passing is not hardware proof (the kit's own `AGENTS.md` requires
  hardware observations to be reported separately from build results — which is
  why this file says host-verified).


## Device run (flash #8, CI #216 kernel)

Payload `work/out/payload-smmu3.img` (sha256
`c6d4fc03135b0391f49cad10d1b9b4bd55439452896b81da3facf86d9118b87f`), built from the
CI #216 kernel (`kernel_sha256` `8c6849cb195ee348…`). The args were byte-identical
to flash #7 (49 tokens, 986/1000 B) -- the linked kit is the ONLY variable.

The verdict arrived on all three channels used for verification (decoded logs,
live USB serial, raw 32 MB window), and all three agree on the same boot:

```
enchilada-smmu: rc=0 SS … | K pre=10/10 r=13/17 w=0 v=0/0 f=0/0 id0=4c017e4c
 (replay t=N attempt=1)
```

- `pre=10/10` = `ArmSmmu4KBusy` (status 10) on BOTH the UFS and DWC3
  streams. This is the precondition gate on programming a stage-1 context: the
  board is still in the firmware's bypass handoff, so the target streams must be
  made to fault before any takeover is legal.
- `w=0`: the kit attempted ZERO stores. The 32/64-bit write bridges counted
  their calls and returned failure without touching the mapping; the counter is
  published on the console, so a future writing kit entry point cannot hide.
- `r=13/17` > 0: `ArmSmmu4KCheckFaultedStreams` actually read the
  live registers on both streams.
- `id0=4c017e4c`: the real SMMU identification register, read by the kit itself --
  the host replay's 11-bit SID-width assumption disappears on device.
- `v=0/0 f=0/0`: the kit took ownership of nothing and faulted nothing.

Two independent analysers (`work/tmp/analyze_smmu3_device_logs.py`, method A =
decoded text; `work/tmp/analyze_smmu3_serial.py`, method B = live serial + raw
window byte scan) plus the structural post-flash self-check
(`work/tmp/verify_smmu3_postflash.py`) were consolidated by
`work/tmp/_smmu3_verdict.py` into `work/tmp/smmu3_verdict.json`: DEVICE-VERIFIED.

Honest boundary: this verifies the kit is LINKED, EXERCISED, and READ-ONLY on
hardware, and that it correctly reports Busy. It does NOT implement a stage-1
context -- that remains the next step, now with a precisely stated precondition.

## Reproduce

```
# 1. the archive builds the four-unit closed set and nothing else
python -X utf8 work/tmp/verify_smmu_kit_2.py

# 2. the shim passes CI-equivalent C++20
clang++ -std=c++20 -target arm64-apple-darwin -fsyntax-only -Wall -Wextra \
  -Werror -Wdeprecated-volatile -I work/tmp/gauge-shim-stubs \
  -I drivers/NativeSmmu -I ../UniXNU/kits/ArmSmmu4K -I kits/enchilada \
  package/EnchiladaSmmuProbe.cpp

# 3. the host test suite still passes
python -X utf8 scripts/check-enchilada-smmu-host.py \
  --cc ../tools/llvm-mingw/bin/clang.exe \
  --cxx ../tools/llvm-mingw/bin/clang++.exe --out ../work/tmp/smmu-host-out

# 4. the wiring guard still sees six force-loaded archives
python -X utf8 scripts/check-enchilada-archives-linked.py

# 5. on device, after flashing a payload built from the CI kernel:
#    the verdict carries K pre=… w=0 and, on a bypass-only board, pre=10
grep -o 'K pre=[^ ]* r=[^ ]* w=[0-9]*' work/log/<run>/*/latest.txt
```
