// SPDX-License-Identifier: BSD-2-Clause-Patent
// Stage B of the SDM845 DWC3 USB device-mode work: bring the controller up as a
// USB device and answer endpoint 0, so a host sees the phone enumerate.
//
// Default-off, like the probe: nothing here runs unless native-dwc3-probe=3 (or
// more) is on the command line. Stage A (native-dwc3-probe=1/2) only reads; this
// file is the first one that writes a device-mode register.
//
// Why the sequence below is what it is. Mu-Silicium's UsbfnDwc3Dxe drives this
// exact controller for fastboot, and its Dwc3CoreInit (Silicon/Samsung/SamsungPkg/
// Drivers/UsbfnDwc3Dxe/Dwc3Core.c:225) is the order the firmware itself used:
//   GUSB2PHYCFG(0) PHY soft reset, DCTL.CsfTReset, GCTL.PrtCapDir = device,
//   GCTL scale-down/dis-scramble/hibernation off, event buffer, DCFG speed and
//   address, DEVTEN, DCTL.RunStop.
// We keep that order. The one deliberate difference is the PHY: the firmware
// resets it because it brings the whole block up from cold, while at XNU entry the
// QUSB2 PLL is already locked and the GDSC is on (stage A reports phy=1 and
// gdsc bit31 set on this board). Resetting a locked PHY here would only risk
// dropping the link the phone is already presenting, so the PHY is left alone
// unless the snapshot says it is not ready.
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSSharedPtr.h>
#include <pexpert/pexpert.h>
#include <kern/debug.h>
#include <kern/thread.h>
#include "Dwc3Registers.h"
#include "Dwc3SmmuPlan.h"

// Counters the boot log reads (package/BootLog.c), all weak so a board without the
// log still links.
extern "C" {
volatile uint32_t native_dwc3_gadget_stage = 0;      // 1..N, see the enum below
volatile int32_t  native_dwc3_gadget_status = 0;
volatile uint32_t native_dwc3_gadget_events = 0;     // events drained
volatile uint32_t native_dwc3_gadget_resets = 0;     // USB reset events seen
volatile uint32_t native_dwc3_gadget_setups = 0;     // SETUP packets received
volatile uint32_t native_dwc3_gadget_cmds = 0;       // endpoint commands issued
volatile uint32_t native_dwc3_gadget_cmd_fail = 0;   // endpoint commands that did not complete
volatile uint32_t native_dwc3_gadget_ep0_state = 0;  // last endpoint-0 state word
volatile uint32_t native_dwc3_gadget_last_event = 0; // last raw event word
volatile uint32_t native_dwc3_gadget_dcfg = 0, native_dwc3_gadget_dctl = 0, native_dwc3_gadget_dsts = 0;
volatile uint32_t native_dwc3_gadget_speed = 0;      // connected link speed from DSTS
volatile uint32_t native_dwc3_gadget_address = 0;    // device address the host set
// What the planner chose and what the SMMU actually holds for that slot, so a refusal can be
// attributed to one comparison instead of the shared error string (SmmuTakeover.h, firmware branch).
volatile uint32_t native_dwc3_gadget_smmu_slot = 0, native_dwc3_gadget_smmu_bank = 0;
volatile uint32_t native_dwc3_gadget_smmu_smr = 0, native_dwc3_gadget_smmu_s2cr = 0;
volatile uint32_t native_dwc3_gadget_smmu_smr_now = 0, native_dwc3_gadget_smmu_s2cr_now = 0;
volatile uint32_t native_dwc3_gadget_smmu_cb_now = 0;
// Why ev can stay at 0: either the loop never ran, or it ran and the controller posted
// nothing. polls counts invocations, gevent_raw keeps the last GEVNTCOUNT0 as read
// (bit31 EHB included), so the two explanations can be told apart from the log alone.
volatile uint32_t native_dwc3_gadget_polls = 0;
volatile uint32_t native_dwc3_gadget_gevent_raw = 0;
const char *volatile native_dwc3_gadget_error = nullptr;
}

namespace {

// Event buffer: one 4 KiB page is the size Mu-Silicium uses and the granularity
// the controller documents. TRB rings are one page each, with the last entry a
// link TRB pointing back at the start.
enum : unsigned {
  kEventBufferBytes = 4096,
  kTrbBytes         = 16,
  kTrbsPerRing      = kEventBufferBytes / kTrbBytes,   // 256
};

enum Stage : unsigned {
  kStageStart      = 1,
  kStageSmmu       = 2,
  kStageEventBuf   = 3,
  kStageRun        = 4,
  kStageEp0Config  = 5,
  kStageReady      = 6,
};

// A DMA-visible region: one physically contiguous allocation, its bus address and
// the mapping the CPU writes through. Same shape NativeUfs uses for its request
// list, so the cache maintenance below is the same `dc civac` it relies on.
struct DmaRegion {
  IOBufferMemoryDescriptor *buffer = nullptr;
  uint8_t *virt = nullptr;
  uint64_t phys = 0;
  unsigned bytes = 0;

  bool allocate(unsigned size) {
    bytes = size;
    buffer = IOBufferMemoryDescriptor::inTaskWithOptions(
        kernel_task,
        static_cast<IOOptionBits>(kIODirectionInOut) | static_cast<IOOptionBits>(kIOMemoryPhysicallyContiguous) |
            static_cast<IOOptionBits>(kIOMemoryMapperNone),
        size, 4096);
    if (!buffer) {
      return false;
    }
    virt = static_cast<uint8_t *>(buffer->getBytesNoCopy());
    IOByteCount contiguous = 0;
    phys = buffer->getPhysicalSegment(0, &contiguous, kIOMemoryMapperNone);
    if (!virt || !phys || (phys & 4095) || contiguous < size) {
      return false;
    }
    bzero(virt, size);
    return true;
  }
};

// The gadget's whole DMA surface. One event buffer, and one TRB ring per physical
// endpoint (endpoint 0's two directions included).
struct Gadget {
  DmaRegion events;
  DmaRegion rings[8];
  uintptr_t core = 0;
  uintptr_t smmu = 0;
  unsigned physicalEps = 0;
  unsigned eventPos = 0;      // byte offset read so far in the event buffer
  uint32_t ep0State = 0;
  bool running = false;
};

Gadget gGadget;

// ---- MMIO ---------------------------------------------------------------------

uint32_t read32(uintptr_t base, unsigned offset) {
  return *reinterpret_cast<volatile uint32_t *>(base + offset);
}

void write32(uintptr_t base, unsigned offset, uint32_t value) {
  *reinterpret_cast<volatile uint32_t *>(base + offset) = value;
  // A posted write to the controller must be visible before the next command is
  // issued, or DEPCMD can be observed with stale parameters. A read of the same
  // window is the barrier NativeUfs uses for the UFS doorbells.
  (void)read32(base, offset);
}

// The controller reads the rings through the SMMU, the CPU writes them cached:
// clean before handing a TRB over and invalidate before reading one back.
void cleanDma(const void *address, unsigned bytes) {
  const uintptr_t start = reinterpret_cast<uintptr_t>(address) & ~uintptr_t(63);
  const uintptr_t end = (reinterpret_cast<uintptr_t>(address) + bytes + 63) & ~uintptr_t(63);
  for (uintptr_t line = start; line < end; line += 64) {
    asm volatile("dc civac, %0" ::"r"(line) : "memory");
  }
  asm volatile("dsb sy" ::: "memory");
}

void invalidateDma(const void *address, unsigned bytes) {
  const uintptr_t start = reinterpret_cast<uintptr_t>(address) & ~uintptr_t(63);
  const uintptr_t end = (reinterpret_cast<uintptr_t>(address) + bytes + 63) & ~uintptr_t(63);
  for (uintptr_t line = start; line < end; line += 64) {
    asm volatile("dc ivac, %0" ::"r"(line) : "memory");
  }
  asm volatile("dsb sy" ::: "memory");
}

// ---- Endpoint commands ---------------------------------------------------------

// Issue one endpoint command and wait for CMDACT to clear. Mirrors
// Dwc3SendEpCommand in Mu-Silicium's Dwc3Endpoint.c:78, including the timeout and
// the status field, so a failure here names the same thing the firmware would.
bool endpointCommand(uintptr_t core, unsigned ep, uint32_t command, uint32_t par0, uint32_t par1, uint32_t par2,
                     uint32_t *result = nullptr) {
  const unsigned base = ep * 0x10u;
  write32(core, kDwc3DEPCMDPAR0Base + base, par0);
  write32(core, kDwc3DEPCMDPAR1Base + base, par1);
  write32(core, kDwc3DEPCMDPAR2Base + base, par2);
  write32(core, kDwc3DEPCMDBase + base, command | kDwc3DepCmdCmdAct);

  native_dwc3_gadget_cmds = native_dwc3_gadget_cmds + 1;
  // 1 ms per poll, 1000 polls: the firmware's DWC3_CMD_TIMEOUT_US budget.
  for (unsigned spin = 0; spin < 100000; ++spin) {
    const uint32_t value = read32(core, kDwc3DEPCMDBase + base);
    if ((value & kDwc3DepCmdCmdAct) == 0) {
      if (result) {
        *result = value;
      }
      if (dwc3DepCmdStatus(value) != 0) {
        native_dwc3_gadget_cmd_fail = native_dwc3_gadget_cmd_fail + 1;
        return false;
      }
      return true;
    }
    for (volatile unsigned pause = 0; pause < 200; pause = pause + 1) {
    }
  }
  native_dwc3_gadget_cmd_fail = native_dwc3_gadget_cmd_fail + 1;
  return false;
}

// DEPSTARTCFG on endpoint 0 OUT, then one transfer resource per endpoint. This is
// the sequence the firmware runs before the first DEPCFG (Dwc3StartConfig,
// Dwc3Endpoint.c:126); without it SETEPCONFIG completes with a bad status.
bool startConfig(uintptr_t core, unsigned physicalEps) {
  if (!endpointCommand(core, kDwc3Ep0Out, kDwc3DepCmdStartConfig, 0, 0, 0)) {
    return false;
  }
  for (unsigned ep = 0; ep < physicalEps; ++ep) {
    if (!endpointCommand(core, ep, kDwc3DepCmdSetXferResource, dwc3DepXferCfgNumXferRes(1), 0, 0)) {
      return false;
    }
  }
  return true;
}

// Program one endpoint. The two parameter words are built exactly as
// Dwc3EnableEndpoint does (Dwc3Endpoint.c:187-200): type and packet size in
// parameter 0 with the FIFO number for IN endpoints, the endpoint number and the
// transfer-event enables in parameter 1.
bool configureEndpoint(uintptr_t core, unsigned ep, unsigned type, unsigned maxPacket) {
  uint32_t par0 = dwc3DepCfgEpTypeBits(type) | dwc3DepCfgMaxPacket(maxPacket) | kDwc3DepCfgActionInit;
  if (ep & 1u) {
    par0 |= dwc3DepCfgFifoNumber(ep >> 1);
  }
  uint32_t par1 = dwc3DepCfgEpNumber(ep);
  if (type == kDwc3EpTypeControl) {
    par1 |= kDwc3DepCfgXferCompleteEn | kDwc3DepCfgXferNotReadyEn;
  } else {
    par1 |= kDwc3DepCfgXferCompleteEn | kDwc3DepCfgXferInProgressEn;
  }
  if (!endpointCommand(core, ep, kDwc3DepCmdSetEpConfig, par0, par1, 0)) {
    return false;
  }
  write32(core, kDwc3DALEPENA, read32(core, kDwc3DALEPENA) | dwc3DalepEna(ep));
  return true;
}

// ---- TRB rings -----------------------------------------------------------------

Dwc3Trb *ringFor(unsigned ep) {
  return reinterpret_cast<Dwc3Trb *>(gGadget.rings[ep].virt);
}

// Point the last TRB of a ring back at the first one, so the controller can keep
// walking it. Mu-Silicium does the same in Dwc3InitTrbRing (Dwc3Endpoint.c:148).
void linkRing(unsigned ep) {
  Dwc3Trb *ring = ringFor(ep);
  Dwc3Trb *link = &ring[kTrbsPerRing - 1];
  const uint64_t phys = gGadget.rings[ep].phys;
  link->bufferLow = uint32_t(phys & 0xffffffffu);
  link->bufferHigh = uint32_t(phys >> 32);
  link->size = 0;
  link->control = dwc3TrbCtrlType(kDwc3TrbTypeLink) | kDwc3TrbCtrlHwo;
  cleanDma(link, kTrbBytes);
}

// Hand one buffer to an endpoint. Returns false when the ring's first TRB is still
// owned by the controller, i.e. the previous transfer has not finished.
// Stage B programs endpoint 0 but does not transfer on it yet -- that is stage C, which
// answers GET_DESCRIPTOR from a SETUP stage through this function. It is written and
// reviewed here so stage C only adds the descriptor tables, but nothing calls it yet.
__attribute__((unused)) bool startTransfer(unsigned ep, uint64_t bufferPhys, unsigned length, unsigned trbType) {
  Dwc3Trb *trb = ringFor(ep);
  if (trb->control & kDwc3TrbCtrlHwo) {
    return false;
  }
  trb->bufferLow = uint32_t(bufferPhys & 0xffffffffu);
  trb->bufferHigh = uint32_t(bufferPhys >> 32);
  trb->size = dwc3TrbSizeLength(length);
  trb->control = dwc3TrbCtrlType(trbType) | kDwc3TrbCtrlHwo | kDwc3TrbCtrlIspImi | kDwc3TrbCtrlIoc | kDwc3TrbCtrlLst;
  cleanDma(trb, kTrbBytes);

  uint32_t result = 0;
  if (!endpointCommand(gGadget.core, ep, kDwc3DepCmdStartTransfer, uint32_t(bufferPhys & 0xffffffffu),
                       uint32_t(bufferPhys >> 32), length, &result)) {
    return false;
  }
  (void)result;
  return true;
}

}  // namespace

// ---- Bring-up ------------------------------------------------------------------

// Everything stage B does, in the firmware's order, with the SMMU planned first so
// a refusal costs nothing. Returns true when the controller is running and
// endpoint 0 is programmed.
bool dwc3GadgetStart(uintptr_t core, uintptr_t smmu, const UfsSmmuPlan &plan, unsigned physicalEps,
                     unsigned maxPacket) {
  gGadget.core = core;
  gGadget.smmu = smmu;
  gGadget.physicalEps = physicalEps;

  native_dwc3_gadget_stage = kStageStart;

  // The event buffer and every ring are DMA: install (or adopt) the stream mapping
  // before the controller is told where they are. configureUfsSmmu writes nothing
  // for the firmware's own entry, which is what the probe found on this board.
  native_dwc3_gadget_stage = kStageSmmu;
  // Record what the plan chose and what the SMMU holds right now, before configureUfsSmmu gets a
  // say. The firmware branch compares three things and returns one string for all of them, so
  // without these the boot log can only say "something changed".
  native_dwc3_gadget_smmu_slot = plan.slot;
  native_dwc3_gadget_smmu_bank = plan.bank;
  native_dwc3_gadget_smmu_smr = plan.smr;
  native_dwc3_gadget_smmu_s2cr = plan.s2cr;
  if (plan.slot != 0xffffffffU) {
    native_dwc3_gadget_smmu_smr_now = read32(smmu, 0x800 + 4 * plan.slot);
    native_dwc3_gadget_smmu_s2cr_now = read32(smmu, 0xc00 + 4 * plan.slot);
    native_dwc3_gadget_smmu_cb_now = read32(smmu, (plan.pages + plan.bank) * 4096);
  }
  if (const char *error = configureUfsSmmu(plan, kDwc3SmmuStream, kDwc3SmmuMask,
                                          [smmu](unsigned off) { return read32(smmu, off); },
                                          [smmu](unsigned off, uint32_t value) { write32(smmu, off, value); })) {
    native_dwc3_gadget_error = error;
    native_dwc3_gadget_status = -int32_t(kStageSmmu);
    return false;
  }

  native_dwc3_gadget_stage = kStageEventBuf;
  if (!gGadget.events.allocate(kEventBufferBytes)) {
    native_dwc3_gadget_error = "event buffer";
    native_dwc3_gadget_status = -int32_t(kStageEventBuf);
    return false;
  }
  for (unsigned ep = 0; ep < physicalEps; ++ep) {
    if (!gGadget.rings[ep].allocate(kEventBufferBytes)) {
      native_dwc3_gadget_error = "trb ring";
      native_dwc3_gadget_status = -int32_t(kStageEventBuf);
      return false;
    }
    linkRing(ep);
  }

  // GCTL: device mode, and clear exactly what Dwc3CoreInit clears (Dwc3Core.c:256-261).
  // RAMCLKSEL (bits 7:6) is not one of them -- writing it to 0 stops the controller's RAM
  // clock, which is what the first cut of this function did by clearing bit 6.
  uint32_t gctl = read32(core, kDwc3GCTL);
  gctl &= ~kDwc3GctlPrtCapMask;
  gctl |= kDwc3GctlPrtCapDevice;
  gctl &= ~kDwc3GctlRunClearMask;
  write32(core, kDwc3GCTL, gctl);

  // Event buffer address and size. INTMASK stays set: this driver polls, it does
  // not take the controller's interrupt.
  write32(core, kDwc3GEVNTADRLO0, uint32_t(gGadget.events.phys & 0xffffffffu));
  write32(core, kDwc3GEVNTADRHI0, uint32_t(gGadget.events.phys >> 32));
  write32(core, kDwc3GEVNTSIZ0, dwc3EvntSizSize(kEventBufferBytes) | kDwc3EvntSizIntMask);
  // Clear the pending count but leave EHB (bit31) alone; the firmware does exactly
  // this (Dwc3Core.c:171). Writing a flat 0 over a set EHB is the kind of thing that
  // leaves the controller believing the buffer is still full.
  write32(core, kDwc3GEVNTCOUNT0, read32(core, kDwc3GEVNTCOUNT0) & 0xfffcu);

  // DCFG: high speed (the board is USB2 only) and address 0. A stale address from
  // the firmware's own session would make every control transfer go unanswered.
  uint32_t dcfg = read32(core, kDwc3DCFG);
  dcfg &= ~kDwc3DcfgSpeedMask;
  dcfg |= kDwc3DcfgHighSpeed;
  dcfg &= ~kDwc3DcfgDevAddrMask;
  write32(core, kDwc3DCFG, dcfg);

  // DEVTEN: the events this driver acts on. SOF is deliberately not enabled, so a
  // running link does not fill the buffer with one event per frame.
  write32(core, kDwc3DEVTEN, kDwc3DevtenDisconEvtEn | kDwc3DevtenUsbRstEn | kDwc3DevtenConnectDoneEn |
                                 kDwc3DevtenUlstCnGen | kDwc3DevtenErraticErrEn | kDwc3DevtenCmdCmplEn |
                                 kDwc3DevtenOverflowEn);

  // Endpoint 0 first: DEPSTARTCFG plus one transfer resource per endpoint, then the
  // two control endpoints. Only after that is the controller allowed to run, so it
  // never has to answer a SETUP before endpoint 0 exists.
  native_dwc3_gadget_stage = kStageEp0Config;
  if (!startConfig(core, physicalEps)) {
    native_dwc3_gadget_error = "DEPSTARTCFG";
    native_dwc3_gadget_status = -int32_t(kStageEp0Config);
    return false;
  }
  if (!configureEndpoint(core, kDwc3Ep0Out, kDwc3EpTypeControl, maxPacket) ||
      !configureEndpoint(core, kDwc3Ep0In, kDwc3EpTypeControl, maxPacket)) {
    native_dwc3_gadget_error = "DEPCFG endpoint 0";
    native_dwc3_gadget_status = -int32_t(kStageEp0Config);
    return false;
  }

  // DCTL.RunStop last, exactly as the firmware does it (Dwc3Core.c:191-201).
  native_dwc3_gadget_stage = kStageRun;
  uint32_t dctl = read32(core, kDwc3DCTL);
  dctl &= ~kDwc3DctlTstCtrlMask;
  write32(core, kDwc3DCTL, dctl | kDwc3DctlRunStop);

  gGadget.running = true;
  native_dwc3_gadget_dcfg = read32(core, kDwc3DCFG);
  native_dwc3_gadget_dctl = read32(core, kDwc3DCTL);
  native_dwc3_gadget_dsts = read32(core, kDwc3DSTS);
  native_dwc3_gadget_stage = kStageReady;
  native_dwc3_gadget_status = 0;
  native_dwc3_gadget_error = nullptr;
  return true;
}

// ---- The event loop -------------------------------------------------------------

// Drain whatever the controller has posted. Called from the probe's worker thread;
// nothing here blocks longer than one command.
void dwc3GadgetDrain() {
  if (!gGadget.running) {
    return;
  }
  uintptr_t core = gGadget.core;
  native_dwc3_gadget_polls = native_dwc3_gadget_polls + 1;
  uint32_t raw = read32(core, kDwc3GEVNTCOUNT0);
  native_dwc3_gadget_gevent_raw = raw;
  uint32_t pending = raw & 0xffffu;
  while (pending >= 4) {
    invalidateDma(gGadget.events.virt + gGadget.eventPos, 4);
    uint32_t event = 0;
    __builtin_memcpy(&event, gGadget.events.virt + gGadget.eventPos, 4);
    gGadget.eventPos = (gGadget.eventPos + 4) % kEventBufferBytes;
    pending -= 4;
    // Handing the bytes back is what frees the slot; without it the buffer fills
    // and the controller stops posting. The low 16 bits are the count to give back;
    // EHB (bit31) is the controller's own "empty" flag and must not be written as 0
    // (Dwc3Core.c:171 masks it the same way).
    write32(core, kDwc3GEVNTCOUNT0, 4);

    native_dwc3_gadget_events = native_dwc3_gadget_events + 1;
    native_dwc3_gadget_last_event = event;

    if (dwc3EventIsEndpoint(event)) {
      // Endpoint events are what stage C needs (transfer completion on the bulk
      // endpoints). Stage B only records them.
      if (dwc3EpEventType(event) == kDwc3EpEventCmdComplete) {
        native_dwc3_gadget_ep0_state = event;
      }
      continue;
    }
    switch (dwc3DevEventType(event)) {
      case kDwc3DevEventReset: {
        // The host reset the link: address 0 again and endpoint 0 re-armed. The
        // firmware does the same on every reset.
        native_dwc3_gadget_resets = native_dwc3_gadget_resets + 1;
        uint32_t dcfg = read32(core, kDwc3DCFG);
        dcfg &= ~kDwc3DcfgDevAddrMask;
        write32(core, kDwc3DCFG, dcfg);
        native_dwc3_gadget_address = 0;
        break;
      }
      case kDwc3DevEventConnectDone: {
        native_dwc3_gadget_speed = dwc3DstsConnectSpeed(read32(core, kDwc3DSTS));
        break;
      }
      case kDwc3DevEventLinkStatus: {
        native_dwc3_gadget_dsts = read32(core, kDwc3DSTS);
        break;
      }
      default:
        break;
    }
  }
  native_dwc3_gadget_dcfg = read32(core, kDwc3DCFG);
  native_dwc3_gadget_dctl = read32(core, kDwc3DCTL);
  native_dwc3_gadget_dsts = read32(core, kDwc3DSTS);
}
