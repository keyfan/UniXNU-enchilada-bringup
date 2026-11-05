# NativeDwc3

Native SDM845 DWC3 USB **device-mode** bring-up for the OnePlus 6 (enchilada), the
kernel half of the SSH-over-USB channel (see `../../NOTES-ssh.md`).

Current state: **stage A (read-only probe) and stage B (device-mode bring-up plus
endpoint 0)**. Default-off. Nothing is mapped or read unless the boot argument
`native-dwc3-probe=1` is present, and no register is written below
`native-dwc3-probe=3`.

| boot arg | what it does |
| --- | --- |
| (none) | the driver attaches to the root device and does nothing |
| `native-dwc3-probe=1` | map GCC + the dwc3 core window UEFI left up, read a register snapshot (GSNPSID, GCTL, DCFG, DSTS, DALEPENA, event buffer) and the QUSB2 PHY PLL-ready bit. No MMIO is written. |
| `native-dwc3-probe=2` | also compute the `apps_smmu` plan for the dwc3 stream `0x740` through `Dwc3SmmuPlan.h`. Still nothing is written. |
| `native-dwc3-probe=3` | also bring the controller up as a USB device (`Dwc3Gadget.cpp`): install or adopt that SMMU entry, allocate the event buffer and one TRB ring per endpoint, program GCTL/GUSB2PHYCFG/DCFG/DEVTEN, run `DEPSTARTCFG` + `DEPXFERCFG` and `DEPCFG` on endpoint 0, then set `DCTL.RunStop` and start the event thread. |

Everything the probe and the gadget see is published through `native_dwc3_*`
counters that `package/BootLog.c` writes to the UFS diagnostic log (rows `U` and
`G`), readable with the sheng log reader over USB (`sheng-log=1`).

Hardware facts it relies on (all read-only):

- dwc3 core `0x0a600000` (window `0xcd00`), wrapper `0x0a6f8800`
  (`qcom,sdm845-dwc3` / `snps,dwc3`, device tree + Mu-Silicium `USB30_PRIM`).
- QUSB2 v2 HS PHY `0x088e2000` (`qcom,sdm845-qusb2-phy`).
- GCC `0x00100000`; the `USB30_PRIM` GDSC/branch offsets are from the SDM845 GCC
  driver. The probe only reads them to decide whether UEFI left the controller
  clocked, exactly as NativeUfs gates its UFS read.
- `apps_smmu` `0x15000000` (size `0x80000`); the dwc3 master is
  `iommus = <&apps_smmu 0x740 0>`.

Register offsets and field positions in `Dwc3Registers.h` / `Dwc3Phy.h` are
architectural facts of the Synopsys DWC3 IP and the Qualcomm QUSB2 PHY (the same
numbers mainline Linux records); no driver code is copied.

The CDC-ACM/-ECM gadget's *function* (descriptors, SET_CONFIG, bulk endpoints,
`ifnet`) is **stage C** and is not built yet. Stage B stops after the controller
enumerates and endpoint 0 answers: `Dwc3Gadget.cpp` brings the device up, programs
endpoint 0 and runs the event loop, but it does not yet answer GET_DESCRIPTOR or
SET_CONFIG. `NOTES-ssh.md` has the plan.

## Stage B: why the bring-up order is what it is

Mu-Silicium's `UsbfnDwc3Dxe` drives this exact controller for fastboot, and its
`Dwc3CoreInit` (`Silicon/Samsung/SamsungPkg/Drivers/UsbfnDwc3Dxe/Dwc3Core.c:225`)
is the order the firmware itself used:

```
GUSB2PHYCFG(0) PHY soft reset -> DCTL.CsfTReset -> GCTL.PrtCapDir = device
-> GCTL scale-down/dis-scramble/hibernation off -> event buffer -> DCFG speed
and address -> DEVTEN -> DCTL.RunStop
```

`dwc3GadgetStart()` keeps that order, with one deliberate difference: it does not
reset the PHY. The firmware resets it because it brings the block up from cold,
while at XNU entry the QUSB2 PLL is already locked and the GDSC is on (stage A
reports `phy=1`, `gdsc=f8222000` on this board). Resetting a locked PHY here would
only risk dropping the link the phone is already presenting.

The event buffer is drained by a kernel thread, not an interrupt: `GEVNTSIZ.INTMASK`
stays set and the thread polls `GEVNTCOUNT`. It blocks with `assert_wait_timeout` +
`thread_block` rather than `IOSleep`, because on this platform `IOSleep` forwards to
`delay_for_interval`, which spins the CPU on the time base instead of yielding it
(`package/BootLog.c:905-913` records the same lesson).

## Tests

`tests/run.sh` builds and runs host unit tests (`c++ -std=c++17`), like
`../NativeUfs/tests`:

- `Dwc3Decode.cpp` — the register decode helpers.
- `Dwc3Smmu.cpp` — the `apps_smmu` plan for the dwc3 stream `0x740`, through
  `Dwc3SmmuPlan.h`.

## The SMMU plan is lenient on purpose

`../NativeUfs/SmmuTakeover.h` plans the *UFS* stream and refuses any matching
stream-match entry that does not sit on the bank it picked itself
(`"conflicting UFS stream mapping"`). For the UFS queue that is correct. For this
driver it is not: UEFI brings the controller up in device mode for fastboot and
leaves a bypass entry for stream `0x740` on a bank of its own, and inheriting that
entry is the point. The first hardware run of `native-dwc3-probe=2` reported

```
U st=5 rc=0 mode=2 id=5533300a ... gdsc=f8222000 smmu=-1/-1
U smmu: conflicting UFS stream mapping
```

— the strict rule rejecting the only correct answer. `Dwc3SmmuPlan.h` answers the
question this driver actually has: adopt a unique existing bypass entry wherever it
lives, and only plan a fresh bank when the stream is not mapped at all. It refuses
an ambiguous table, a bank with translation on, and a 32-bit bypass bank that
cannot carry the gadget's DMA range. The UFS planner is untouched, so NativeUfs
keeps its strict rules.

## Build

`scripts/build-enchilada-dwc3.py` compiles both translation units (`NativeDwc3.cpp`
and `Dwc3Gadget.cpp`) against this kernel's recorded IOKit ABI, audits each Mach-O,
and archives them into `build/enchilada-dwc3/libNativeDwc3.a`, which
`CONFIG_XNU_EXTRA_LD_ARCHIVES` force-loads into the kernel. The IOKit personality is
added by `patches/xnu/zzv-native-dwc3-personality.patch`.
