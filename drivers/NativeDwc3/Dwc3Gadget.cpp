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
// Included so the compiler checks these definitions against the declarations the callers
// see -- in particular that dwc3ConsoleWrite/dwc3ConsoleReady keep the exact signatures
// package/BootLog.c declares, since that file is C and the two would not otherwise meet.
#include "Dwc3Gadget.h"

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
// GUSB2PHYCFG(0) before and after the gadget rewrites it. If SUSPHY was set here and
// cleared there, the PHY was parked when we took over -- that is the whole diagnosis.
volatile uint32_t native_dwc3_gadget_phy_before = 0, native_dwc3_gadget_phy_after = 0;
// QSCRATCH (the wrapper) as it was and as the gadget left it. HS is the VBUS/session
// half, SS the lane-power half; hs_vbus/hs_sel keep the two HS bits individually so
// the log says which one the write actually landed on.
volatile uint32_t native_dwc3_gadget_qs_hs_before = 0, native_dwc3_gadget_qs_hs_after = 0;
volatile uint32_t native_dwc3_gadget_qs_ss_before = 0, native_dwc3_gadget_qs_ss_after = 0;
// Stage C: what endpoint 0 was asked and what it answered. last_setup packs the eight
// SETUP bytes as bRequestType<<24 | bRequest<<16 | wValue, so one word says which
// request the host got through with. ep0_events counts endpoint events seen (the
// drain loop only recorded them before), configured flips on SET_CONFIG.
volatile uint32_t native_dwc3_gadget_ep0_events = 0;
volatile uint32_t native_dwc3_gadget_last_setup = 0;
volatile uint32_t native_dwc3_gadget_answered = 0;
volatile uint32_t native_dwc3_gadget_configured = 0;
// Why an arm can stop before it issues anything. consoles 193 showed eleven resets with
// cmd frozen at 8: every re-arm bailed out silently. arm_ok counts the TRBs actually
// handed to the controller, arm_hwo the refusals, and ep0_ctl keeps what EP0-OUT's
// control TRB held at the last refusal -- bit0 is HWO, so a stuck 1 explains it.
volatile uint32_t native_dwc3_gadget_arm_ok = 0;
volatile uint32_t native_dwc3_gadget_arm_hwo = 0;
volatile uint32_t native_dwc3_gadget_ep0_ctl = 0;
// The CDC data endpoints. data_eps flips on SET_CONFIG, bulk_armed counts the receive
// TRBs handed to bulk OUT, bulk_stuck the ones that could not be, and bulk_in/bulk_out
// count completed transfers in each direction -- the first sign that a host is actually
// talking to the port rather than merely enumerating it.
volatile uint32_t native_dwc3_gadget_data_eps = 0;
volatile uint32_t native_dwc3_gadget_bulk_armed = 0;
volatile uint32_t native_dwc3_gadget_bulk_stuck = 0;
volatile uint32_t native_dwc3_gadget_bulk_in = 0;
volatile uint32_t native_dwc3_gadget_bulk_out = 0;
volatile uint32_t native_dwc3_gadget_line_coding = 0;
volatile uint32_t native_dwc3_gadget_dtr = 0;
// Stage E: the pipe itself. bulk_in/bulk_out above only counted transfers; these count the
// bytes that actually crossed, and tx_dropped the ones a full queue refused. A port that
// enumerates but moves no bytes and one that moves bytes look identical in every other row.
volatile uint32_t native_dwc3_gadget_tx_bytes = 0;
volatile uint32_t native_dwc3_gadget_rx_bytes = 0;
volatile uint32_t native_dwc3_gadget_tx_dropped = 0;
volatile uint32_t native_dwc3_gadget_tx_queued = 0;
// The last few bytes the host sent, exactly as they arrived. A console that echoes nothing
// back is indistinguishable from one that received nothing, and this row settles which.
volatile uint32_t native_dwc3_gadget_rx_last = 0;
volatile uint32_t native_dwc3_gadget_rx_events = 0;
// console199 queued 8447 bytes and the host took none: bulk IN was armed exactly once and
// never completed, so txInflight stayed non-zero and armBulkIn() returned early forever.
// These two say which half of that happened -- how often a bulk IN transfer was handed to
// the controller, and how often a bus reset threw an in-flight one away.
volatile uint32_t native_dwc3_gadget_bulk_in_armed = 0;
volatile uint32_t native_dwc3_gadget_tx_aborted = 0;
// console196 answered two SETUPs and then went deaf: the host stopped talking after
// SET_ADDRESS and just reset in a loop. Two explanations fit that shape and the counts so
// far cannot tell them apart -- either a stage never got a TRB armed, or the endpoint
// command itself came back with a status. cmd_last_fail packs the last failing command as
// ep<<24 | opcode<<8 | status, status_armed counts the zero-length status TRBs handed
// over, status_done the ones the host consumed, and stuck_setups counts SETUP completions
// that arrived while endpoint 0 was not in its setup state.
volatile uint32_t native_dwc3_gadget_cmd_last_fail = 0;
volatile uint32_t native_dwc3_gadget_arm_fail = 0;
volatile uint32_t native_dwc3_gadget_notready = 0;
volatile uint32_t native_dwc3_gadget_notready_last = 0;
volatile uint32_t native_dwc3_gadget_status_armed = 0;
volatile uint32_t native_dwc3_gadget_status_done = 0;
volatile uint32_t native_dwc3_gadget_stuck_setups = 0;
// Counts host-to-device data stages that completed. console201's COM5 could be seen by
// Windows and then timed out on Open(), and SET_LINE_CODING -- a seven byte OUT stage --
// was the last request in the log; this counter is what makes that stage visible.
volatile uint32_t native_dwc3_gadget_ep0_out_bytes = 0;
// The last eight requests, packed requestType<<24 | request<<16 | wValueLow:0 for a
// command-only write, and the same for the ones a status stage actually followed. Three
// runs have now ended on a class request and the log could only name the very last one, so
// which request went unanswered -- and whether it was answered here or by the controller
// opening the phase -- was a guess. A ring makes it a reading.
enum : unsigned { kSetupLogEntries = 8 };
volatile uint32_t native_dwc3_gadget_setup_log[kSetupLogEntries] = {};
volatile uint32_t native_dwc3_gadget_setup_log_next = 0;
// Status phases the controller announced on EP0-IN and on EP0-OUT separately. A STATUS2
// request (no data stage) and a STATUS3 one (after a data stage) differ only in which
// endpoint the host drives, so counting them together hid which of the two was stuck.
volatile uint32_t native_dwc3_gadget_status_nr_in = 0;
volatile uint32_t native_dwc3_gadget_status_nr_out = 0;
volatile uint32_t native_dwc3_gadget_status_nr_skipped = 0;
// The CDC-ACM SERIAL_STATE carrier notification. Linux's f_acm.c asserts DSR|DCD on
// the interrupt IN endpoint as soon as the tty opens (acm_connect), and usbser.sys waits
// for that carrier while opening the port -- without it CreateFile reports "The semaphore
// timeout period has expired." console204 enumerated, answered all 146 SETUPs and moved
// 290816 bytes, yet never armed one TRB on the notify endpoint. armed counts the ten-byte
// reports handed to the controller, done the ones the host actually polled.
volatile uint32_t native_dwc3_gadget_notify_armed = 0;
volatile uint32_t native_dwc3_gadget_notify_done = 0;
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
  kMaxRings         = 16,                              // DWC3_MAX_PHYSICAL_EP
  // Stage E's transmit queue, in bytes. It is deliberately small: the boot log produces
  // one 4 KiB slot every half second, so eight kilobytes is several rounds of slack, and
  // a queue that fills means the host is not reading rather than that we are behind.
  kTxQueueBytes     = 8192,
};

enum Stage : unsigned {
  kStageStart      = 1,
  kStageSmmu       = 2,
  kStageEventBuf   = 3,
  kStageRun        = 4,
  kStageEp0Config  = 5,
  kStageReady      = 6,
  // Added after console187: the core soft reset runs after the SMMU plan and before GCTL, so
  // it needs its own number or a timeout there would be reported as a stage 2 failure.
  kStageSoftReset  = 7,
  // Added after console190: the wrapper's VBUS override is the step between a controller
  // that runs and a link the host can see.
  kStageVbus       = 8,
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
//
// Both arrays are sized from the core's own ceiling rather than from what this board
// happened to report: GHWPARAMS3 allows up to sixteen device endpoints, and the ring
// setup loop runs over the count the probe read, so a smaller array would have been
// overrun the moment that count stopped being clamped to four.
struct Gadget {
  DmaRegion events;
  DmaRegion rings[16];
  // Endpoint 0's own DMA page: the eight SETUP bytes at 0 and the IN payload at 64. One
  // allocation keeps every buffer the controller sees inside a single physically
  // contiguous region, and keeps the SETUP packet out of the TRB ring.
  DmaRegion control;
  uintptr_t core = 0;
  uintptr_t smmu = 0;
  uintptr_t qscratch = 0;     // wrapper window; 0 = no VBUS override
  unsigned physicalEps = 0;
  unsigned eventPos = 0;      // byte offset read so far in the event buffer
  // Resource index handed back by STARTTRANSFER, per endpoint. ENDTRANSFER needs it as
  // its parameter, and 0 means "no transfer outstanding" (Dwc3Endpoint.c:348).
  uint32_t resourceIndex[16] = {};
  uint32_t ep0State = 0;
  bool ep0Armed = false;      // a CONTROL_SETUP TRB is waiting on EP0-OUT
  bool threeStage = false;    // the request being answered has a data stage
  bool dataIn = false;        // ...and it moves device -> host
  // Bytes the outstanding data-stage transfer was armed with. bytesTransferred() needs it
  // to turn the TRB's "remaining" count back into a byte count on completion.
  unsigned dataArmed = 0;
  bool running = false;
  // CDC-ACM state. The line coding the host sets is accepted and forgotten: there is no
  // separate UART behind this port to configure. dtr is recorded because a serial client
  // toggles it on open and close, and it makes a useful "a human plugged in" marker.
  bool lineCoding = false;
  bool dtr = false;
  // The seven bytes SET_LINE_CODING brought, handed back verbatim by GET_LINE_CODING. The
  // host sets a rate and then reads it back to confirm the device took it; answering with a
  // constant made every read-back disagree with what was just written, and usbser.sys
  // retried forever -- console204's ring shows the pair repeating (21200007 a1210007 ...)
  // with every stage completing, which is a rejected value, not a stuck endpoint.
  uint8_t lineCodingBytes[7] = {0x80, 0x25, 0x00, 0x00, 0x00, 0x00, 0x08};   // 9600 8N1 until told otherwise
  // The bulk OUT buffer the controller writes into. It lives in its own DMA region so a
  // host that writes before anything reads does not scribble over the control page.
  DmaRegion bulkOut;
  DmaRegion notify;
  bool dataEndpoints = false;
  // Stage E. The transmit queue is written by whoever has bytes to send (today the boot
  // log's writer thread) and drained by the gadget thread when bulk IN completes. One
  // producer and one consumer, so the ring needs no lock: only the producer moves txHead,
  // only the consumer moves txTail, and both are volatile with a barrier on hand-off.
  DmaRegion bulkIn;
  unsigned char txQueue[kTxQueueBytes];
  volatile unsigned txHead = 0;
  volatile unsigned txTail = 0;
  unsigned txInflight = 0;     // bytes handed to the controller and not yet completed
  // Bytes the current control transfer receives from the host. Only SET_LINE_CODING uses
  // this direction so far; an OUT data stage needs a receive TRB before the host sends,
  // which is why it cannot be answered like the IN ones.
  unsigned outBytes = 0;
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

// ---- The wrapper's VBUS override ------------------------------------------------

// Tell the wrapper the cable is present. On SDM845 the dwc3 core is one block of a
// USB wrapper, and the "session valid" the UTMI side samples is a bit that software
// sets: there is no VBUS pad on this board (the device tree points usb-phy at
// usb-nop-xceiv), so nothing else will ever set it.
//
// Without this the controller runs, endpoint 0 is programmed, DCTL.RunStop is set,
// and DSTS still reads HS_DISCONNECTED -- console186 through console190. Mu-Silicium
// gets its fastboot link by forcing the equivalent bits on its own USB registers
// (BoardInit.c:239-245), and mainline's dwc3-qcom.c calls
// dwc3_qcom_vbus_override_enable() in peripheral mode and again from its
// pre_run_stop notifier, with the comment that the next UDC write needs VBUS_VALID
// set to generate a connect-done event. Both halves are done here: once before
// RunStop, once immediately after, because the override has to be in place when the
// controller samples the line, and the firmware's own notifier re-applies it.
// `record` keeps the "before" words on the first call and leaves them alone on the
// second, so the log shows UEFI's own value rather than the first write's result.
void applyVbusOverride(uintptr_t qscratch, bool record) {
  if (!qscratch) {
    return;
  }
  // SS first, then HS: mainline's order in dwc3_qcom_vbus_override_enable().
  uint32_t ss = read32(qscratch, kDwc3QscratchSsPhyCtrl);
  if (record) {
    native_dwc3_gadget_qs_ss_before = ss;
  }
  write32(qscratch, kDwc3QscratchSsPhyCtrl, ss | kQscratchLane0PwrPresent);
  native_dwc3_gadget_qs_ss_after = read32(qscratch, kDwc3QscratchSsPhyCtrl);

  uint32_t hs = read32(qscratch, kDwc3QscratchHsPhyCtrl);
  if (record) {
    native_dwc3_gadget_qs_hs_before = hs;
  }
  write32(qscratch, kDwc3QscratchHsPhyCtrl, hs | kQscratchUtmiOtgVbusValid | kQscratchSwSessVldSel);
  native_dwc3_gadget_qs_hs_after = read32(qscratch, kDwc3QscratchHsPhyCtrl);
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
        // Which endpoint, which opcode, which status -- the three things needed to tell a
        // bad SETEPCONFIG from a STARTTRANSFER that never took.
        native_dwc3_gadget_cmd_last_fail =
            (ep << 24) | ((command & 0xffu) << 8) | dwc3DepCmdStatus(value);
        return false;
      }
      return true;
    }
    for (volatile unsigned pause = 0; pause < 200; pause = pause + 1) {
    }
  }
  native_dwc3_gadget_cmd_fail = native_dwc3_gadget_cmd_fail + 1;
  // 0xf in the status field is not a status the controller returns: it means CMDACT never
  // cleared, so the command hung instead of being rejected.
  native_dwc3_gadget_cmd_last_fail = (ep << 24) | ((command & 0xffu) << 8) | 0xfu;
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
//
// The two address halves go in as PAR0 = high, PAR1 = low. That is easy to get
// backwards because every other command here takes the low word first: the firmware
// passes (TrbPoolPhys >> 32) as Param0 and the low half as Param1
// (Dwc3Endpoint.c:318-323), and mainline's dwc3_gadget_start_transfer writes
// DWC3_DEPCMDPAR0 = upper_32_bits(trb_dma), PAR1 = lower_32_bits(trb_dma).
// STARTTRANSFER is handed the TRB ring's base address, not the buffer the TRB points
// at: the controller reads the queue head out of PAR0/PAR1 and the buffer/length out of
// the TRB itself. The firmware passes Endpoint->TrbPoolPhys
// (Dwc3Endpoint.c:318-320), and mainline does the same with trb_dma. Passing the data
// buffer instead -- which this driver did until console193 -- makes the controller walk
// whatever bytes live there as if they were TRBs, so nothing ever completes, HWO never
// clears, and endpoint 0 stops answering after its first arm. That is the "unrecognised
// USB device" the host reports.
bool startTransfer(unsigned ep, uint64_t bufferPhys, unsigned length, unsigned trbType) {
  Dwc3Trb *trb = ringFor(ep);
  const uint64_t ringPhys = gGadget.rings[ep].phys;
  // HWO is cleared by the controller when it finishes with the TRB, so the copy in the
  // cache is stale by definition here; read the one in memory.
  invalidateDma(trb, kTrbBytes);
  if (trb->control & kDwc3TrbCtrlHwo) {
    // Do not silently give up: on endpoint 0 a stuck HWO is the difference between a
    // device the host can enumerate and one it calls "unrecognised", and the boot log
    // has to say which happened.
    native_dwc3_gadget_arm_hwo = native_dwc3_gadget_arm_hwo + 1;
    if (ep == kDwc3Ep0Out) {
      native_dwc3_gadget_ep0_ctl = trb->control;
    }
    return false;
  }
  trb->bufferLow = uint32_t(bufferPhys & 0xffffffffu);
  trb->bufferHigh = uint32_t(bufferPhys >> 32);
  trb->size = dwc3TrbSizeLength(length);
  trb->control = dwc3TrbCtrlType(trbType) | kDwc3TrbCtrlHwo | kDwc3TrbCtrlIspImi | kDwc3TrbCtrlIoc | kDwc3TrbCtrlLst;
  cleanDma(trb, kTrbBytes);

  uint32_t result = 0;
  // PAR2 stays zero: STARTTRANSFER reads the buffer and length out of the TRB, and the
  // firmware passes 0 here too (Dwc3Endpoint.c:318-323). PAR0/PAR1 carry ringPhys -- the
  // ring, not bufferPhys -- which is the whole point of the note above.
  if (!endpointCommand(gGadget.core, ep, kDwc3DepCmdStartTransfer, uint32_t(ringPhys >> 32),
                       uint32_t(ringPhys & 0xffffffffu), 0, &result)) {
    gGadget.resourceIndex[ep] = 0;
    native_dwc3_gadget_arm_fail = native_dwc3_gadget_arm_fail + 1;
    return false;
  }
  gGadget.resourceIndex[ep] = dwc3DepCmdResourceIndex(result);
  native_dwc3_gadget_arm_ok = native_dwc3_gadget_arm_ok + 1;
  return true;
}

// Stop whatever transfer the endpoint has outstanding. ENDTRANSFER takes the resource
// index from the STARTTRANSFER that is being cancelled, and a zero index means there is
// nothing to stop (Dwc3Endpoint.c:348-351) -- so this is safe to call unconditionally.
void endTransfer(unsigned ep) {
  uint32_t index = gGadget.resourceIndex[ep];
  if (index == 0) {
    return;
  }
  // ENDTRANSFER carries the resource index inside the command word itself, bits 22:16
  // (Dwc3Endpoint.c:353-356 with DWC3_DEPCMD_PARAM), not in a parameter register.
  // The opcode and the flags live in two different enumerations in Dwc3Registers.h, and
  // clang refuses to combine values from two anonymous enums with -Werror
  // (-Wdeprecated-anon-enum-enum-conversion), so the flags are widened here.
  const uint32_t command = uint32_t(kDwc3DepCmdEndTransfer) | uint32_t(kDwc3DepCmdHiPriForceRm) |
                           uint32_t(kDwc3DepCmdCmdIoc) | ((index & 0x7fu) << 16);
  endpointCommand(gGadget.core, ep, command, 0, 0, 0);
  gGadget.resourceIndex[ep] = 0;
  Dwc3Trb *trb = ringFor(ep);
  trb[0].control = 0;
  trb[1].control = 0;
  cleanDma(trb, 2 * kTrbBytes);
}

// Take the first two TRBs of an endpoint back unconditionally.
//
// Dwc3StartTransfer overwrites TrbPool[0] on every call and never inspects what was
// there before (Dwc3Endpoint.c:276-290). This driver instead refuses to reuse a TRB
// whose HWO is still set, which is the right instinct for an endpoint that is actively
// moving data but a deadlock for endpoint 0 after a bus reset: the controller flushes
// its transfer state, HWO stays set in memory, resourceIndex is already 0 so
// endTransfer() returns early, and every later armSetup() bails at the HWO check
// without issuing a command at all. console193 measured exactly that -- eleven resets
// and cmd still at 8. A reset means the controller has thrown the transfer away, so
// the TRB is ours to reclaim.
void clearTrb(unsigned ep) {
  Dwc3Trb *trb = ringFor(ep);
  trb[0].control = 0;
  trb[1].control = 0;
  cleanDma(trb, 2 * kTrbBytes);
  gGadget.resourceIndex[ep] = 0;
}

// ---- The CDC data endpoints ------------------------------------------------------
//
// Until SET_CONFIG these three endpoints do not exist as far as the host is concerned,
// so they are programmed on demand rather than with endpoint 0. The physical numbers
// follow DWC3's rule (2 * USB endpoint number + direction, IN being 1) and have to
// match the endpoint addresses in kConfigDescriptor: 0x81 -> 3, 0x82 -> 5, 0x02 -> 4.
enum : unsigned {
  kEpNotify         = 3,   // 0x81, interrupt IN: serial state notifications
  kEpBulkOut        = 4,   // 0x02, bulk OUT: host -> device
  kEpBulkIn         = 5,   // 0x82, bulk IN: device -> host
  kEpMaxPacketBulk  = 64,
  kEpMaxPacketNotify = 16,
  // One 256-byte receive buffer is enough for a console: a host that writes more than
  // that between two polls gets NAKs, not corruption.
  kEpBufferBytes    = 256,
  // Not an endpoint address: "every endpoint this device owns", for the reset path.
  kAllEndpoints     = 0xff,
};

// Re-arm the bulk OUT endpoint with an empty buffer. The host writes whenever it likes
// once the port is open, and a configured endpoint with no TRB is a NAK the driver has
// to retry -- correct, but it also means bytes only move when something is listening.
void armBulkOut() {
  clearTrb(kEpBulkOut);
  if (startTransfer(kEpBulkOut, gGadget.bulkOut.phys, gGadget.bulkOut.bytes, kDwc3TrbTypeNormal)) {
    native_dwc3_gadget_bulk_armed = native_dwc3_gadget_bulk_armed + 1;
  } else {
    native_dwc3_gadget_bulk_stuck = native_dwc3_gadget_bulk_stuck + 1;
  }
}

// CDC-ACM SERIAL_STATE: the ten-byte interrupt-IN report a serial class driver waits
// for while opening the port. Linux f_acm.c asserts DSR|DCD in acm_connect() and sends
// this on the notify endpoint (struct usb_cdc_notification + LE serial_state); without
// a TRB there the host polls forever and CreateFile fails with "The semaphore timeout
// period has expired." console204 answered all 146 SETUPs, completed every status stage
// and moved 290816 bytes, yet never armed this endpoint once -- so the control path was
// healthy and the only thing missing was the carrier. Layout (uapi/linux/usb/cdc.h):
//   A1 = IN|CLASS|INTERFACE, 20 = NOTIFY_SERIAL_STATE, wValue 0, wIndex 0 (ctrl_id),
//   wLength 2, then serial_state 0x0003 = DCD|DSR.
void armSerialState() {
  if (!gGadget.notify.virt) {
    return;
  }
  static const uint8_t carrier[10] = {
      0xa1, 0x20, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00};
  __builtin_memcpy(gGadget.notify.virt, carrier, sizeof carrier);
  cleanDma(gGadget.notify.virt, sizeof carrier);
  clearTrb(kEpNotify);
  if (startTransfer(kEpNotify, gGadget.notify.phys, sizeof carrier, kDwc3TrbTypeNormal)) {
    native_dwc3_gadget_notify_armed = native_dwc3_gadget_notify_armed + 1;
  }
}

// How many of the bytes a transfer was armed with actually crossed. The controller writes
// the count it did *not* use back into the TRB's size field, so the answer is the
// difference -- the same arithmetic the firmware does in Dwc3TrbBytesTransferred
// (Dwc3Endpoint.c:528-534). The cache copy is stale: the controller wrote that field.
unsigned bytesTransferred(unsigned ep, unsigned armed) {
  Dwc3Trb *trb = ringFor(ep);
  invalidateDma(trb, kTrbBytes);
  const unsigned remaining = dwc3TrbSizeLength(trb->size);
  return remaining > armed ? armed : armed - remaining;
}

// Hand the host whatever is queued. Only one bulk IN transfer may be in flight at a time:
// the TRB ring has a single usable entry for this direction (the memory the controller
// reads lives in the TRB itself), so arming a second before the first completes would
// overwrite the queue head the controller is still walking.
void armBulkIn() {
  if (!gGadget.dataEndpoints || gGadget.txInflight) {
    return;
  }
  const unsigned head = gGadget.txHead;
  unsigned tail = gGadget.txTail;
  if (tail == head) {
    return;
  }
  // One contiguous run, up to the end of the queue: the buffer has to be linear.
  unsigned run = head > tail ? head - tail : kTxQueueBytes - tail;
  if (run > gGadget.bulkIn.bytes) {
    run = gGadget.bulkIn.bytes;
  }
  __builtin_memcpy(gGadget.bulkIn.virt, gGadget.txQueue + tail, run);
  cleanDma(gGadget.bulkIn.virt, run);
  clearTrb(kEpBulkIn);
  if (!startTransfer(kEpBulkIn, gGadget.bulkIn.phys, run, kDwc3TrbTypeNormal)) {
    // Not counted as dropped: the queue head has not moved, so the next pass tries the
    // same bytes again. startTransfer has already counted the refused command in arm_fail,
    // which is the honest place for it -- a persistent failure shows up there once per
    // pass rather than as fake byte loss here.
    return;
  }
  gGadget.txInflight = run;
  native_dwc3_gadget_bulk_in_armed = native_dwc3_gadget_bulk_in_armed + 1;
  tail = (tail + run) % kTxQueueBytes;
  gGadget.txTail = tail;
}

}  // namespace

// The two entry points the boot log calls. They live outside the anonymous namespace so
// the linker can see them (Dwc3Gadget.h declares them); everything they touch -- gGadget,
// the queue arithmetic, armBulkIn -- is internal to this file and still reachable.
//
// extern "C" because the caller is C (package/BootLog.c); without it these would be
// mangled and the weak check in the log would quietly find nothing.

// Append bytes for the host. Called from the boot log's writer thread, which is the only
// producer; the gadget thread is the only consumer. Returns how many bytes were taken --
// a short count means the host is not reading and the caller may drop the rest.
extern "C" unsigned dwc3ConsoleWrite(const void *bytes, unsigned length) {
  if (!gGadget.dataEndpoints) {
    return 0;
  }
  const unsigned char *source = static_cast<const unsigned char *>(bytes);
  unsigned room = 0;
  {
    const unsigned head = gGadget.txHead;
    const unsigned tail = gGadget.txTail;
    // One byte is always left free so head == tail keeps meaning "empty".
    room = head >= tail ? kTxQueueBytes - 1 - (head - tail) : tail - head - 1;
  }
  if (length > room) {
    length = room;
  }
  if (!length) {
    native_dwc3_gadget_tx_dropped = native_dwc3_gadget_tx_dropped + 1;
    return 0;
  }
  unsigned head = gGadget.txHead;
  unsigned first = kTxQueueBytes - head;
  if (first > length) {
    first = length;
  }
  __builtin_memcpy(gGadget.txQueue + head, source, first);
  if (length > first) {
    __builtin_memcpy(gGadget.txQueue, source + first, length - first);
  }
  asm volatile("dmb oshst" ::: "memory");
  gGadget.txHead = (head + length) % kTxQueueBytes;
  native_dwc3_gadget_tx_queued = native_dwc3_gadget_tx_queued + length;
  // Deliberately not arming here: startTransfer touches the TRB ring and the endpoint
  // command registers, and both are the gadget thread's. Queuing is a plain memory write,
  // which is all this thread may do. The drain loop hands the run over on its next pass,
  // two milliseconds later at worst.
  return length;
}

// Whether a host has configured us and there is somewhere to put bytes. Deliberately not
// gated on DTR: the endpoint being programmed is the only thing that has to be true, and
// requiring DTR would mean a host that opens the port without raising it (or a plain
// sniffer) sees a silent line. When nothing is reading, the queue fills and the drop
// counter says so, which is better evidence than an empty log.
// Returns int, not bool, because the declaration the C caller sees (package/BootLog.c)
// says int: the two translation units have to agree, and C has no bool without <stdbool.h>.
extern "C" int dwc3ConsoleReady(void) {
  return gGadget.dataEndpoints ? 1 : 0;
}

namespace {

bool programDataEndpoints() {
  if (gGadget.dataEndpoints) {
    return true;
  }
  // GHWPARAMS3 says how many physical endpoints this core implements, and the rings were
  // sized from it. A core that reports fewer than six has nowhere to put a CDC pair.
  if (gGadget.physicalEps <= kEpBulkIn) {
    native_dwc3_gadget_error = "cdc needs six endpoints";
    return false;
  }
  if (!gGadget.bulkOut.allocate(kEpBufferBytes) || !gGadget.notify.allocate(kEpBufferBytes) ||
      !gGadget.bulkIn.allocate(kEpBufferBytes)) {
    native_dwc3_gadget_error = "cdc buffers";
    return false;
  }
  if (!configureEndpoint(gGadget.core, kEpBulkOut, kDwc3EpTypeBulk, kEpMaxPacketBulk) ||
      !configureEndpoint(gGadget.core, kEpBulkIn, kDwc3EpTypeBulk, kEpMaxPacketBulk) ||
      !configureEndpoint(gGadget.core, kEpNotify, kDwc3EpTypeInterrupt, kEpMaxPacketNotify)) {
    native_dwc3_gadget_error = "cdc endpoint config";
    return false;
  }
  linkRing(kEpNotify);
  linkRing(kEpBulkIn);
  linkRing(kEpBulkOut);
  gGadget.dataEndpoints = true;
  native_dwc3_gadget_data_eps = 1;
  armBulkOut();
  // As soon as the config is live, present the carrier. A serial host opens the port
  // by waiting for DCD/DSR on the interrupt endpoint, so it must be armed before the
  // first IN token arrives -- not after some later event that may never come.
  armSerialState();
  return true;
}

void unprogramDataEndpoints() {
  if (!gGadget.dataEndpoints) {
    return;
  }
  endTransfer(kEpNotify);
  endTransfer(kEpBulkIn);
  endTransfer(kEpBulkOut);
  uint32_t enable = read32(gGadget.core, kDwc3DALEPENA);
  enable &= ~(dwc3DalepEna(kEpNotify) | dwc3DalepEna(kEpBulkIn) | dwc3DalepEna(kEpBulkOut));
  write32(gGadget.core, kDwc3DALEPENA, enable);
  gGadget.dataEndpoints = false;
  native_dwc3_gadget_data_eps = 0;
  // Anything still queued belongs to a configuration the host has torn down; the buffer
  // it would have gone out through is unprogrammed from here on.
  gGadget.txHead = 0;
  gGadget.txTail = 0;
  gGadget.txInflight = 0;
}

// One event on a data endpoint. A completed bulk OUT transfer means the host's bytes are
// sitting in bulkOut; they are recorded and the receive TRB is handed back. A completed
// bulk IN transfer means the host took what was queued, so the next run goes out.
void handleDataEvent(unsigned ep, unsigned type) {
  if (type != kDwc3EpEventXferComplete) {
    return;
  }
  // A completion means the controller is done with that resource; keeping the index
  // would make a later endTransfer() issue ENDTRANSFER for a transfer that no longer
  // exists (the firmware guards this with TransferStarted, Dwc3Endpoint.c:341-352).
  gGadget.resourceIndex[ep] = 0;
  if (ep == kEpBulkOut) {
    invalidateDma(gGadget.bulkOut.virt, gGadget.bulkOut.bytes);
    const unsigned got = bytesTransferred(ep, gGadget.bulkOut.bytes);
    native_dwc3_gadget_bulk_in += 1;
    if (got) {
      native_dwc3_gadget_rx_events = native_dwc3_gadget_rx_events + 1;
      native_dwc3_gadget_rx_bytes = native_dwc3_gadget_rx_bytes + got;
      // The last four bytes of the most recent write, little-endian in one word, so the
      // log can show that a human typed something without a driver in between.
      uint32_t tail = 0;
      const unsigned char *data = static_cast<const unsigned char *>(gGadget.bulkOut.virt);
      for (unsigned i = got > 4 ? got - 4 : 0; i < got; ++i) {
        tail = (tail >> 8) | (uint32_t(data[i]) << 24);
      }
      native_dwc3_gadget_rx_last = tail;
    }
    armBulkOut();
  } else if (ep == kEpBulkIn) {
    const unsigned sent = gGadget.txInflight;
    native_dwc3_gadget_bulk_out += 1;
    native_dwc3_gadget_tx_bytes = native_dwc3_gadget_tx_bytes + bytesTransferred(ep, sent);
    gGadget.txInflight = 0;
    armBulkIn();   // there may be another run queued already
  } else if (ep == kEpNotify) {
    // The host took the SERIAL_STATE report. Keep one armed: the class driver may poll
    // again, and a NAK on the interrupt endpoint is indistinguishable from no carrier.
    native_dwc3_gadget_notify_done = native_dwc3_gadget_notify_done + 1;
    armSerialState();
  }
}

// A bus reset throws away every transfer the controller had in flight, for the data
// endpoints as much as for endpoint 0. The firmware walks them all on
// DWC3_DEVICE_EVENT_RESET (Dwc3Endpoint.c:721-727: Dwc3EndTransfer plus
// TransferBuffer = NULL for every enabled endpoint above EP0_IN) and so must this driver.
// console199 is what happens otherwise: 8447 bytes queued, bulk IN armed exactly once,
// then eleven resets -- and because txInflight never went back to zero, armBulkIn()
// returned early on every one of its 264786 passes and not a byte left the device. The
// queue kept growing while the row that should have shown the problem (tx) stayed at 0.
//
// The endpoints stay programmed, which is what the firmware does too: the host
// re-enumerates over them rather than sending a new SET_CONFIG, so unprogramming here
// would only race the enumeration.
// A stalled endpoint stays stalled until something asks for it to stop stalling. This
// driver stalls endpoint 0 whenever a SETUP asks for something it does not implement, which
// is what the USB spec wants -- but the reset path has to take it back, because the firmware
// does exactly that (Dwc3Endpoint.c:700-703: Dwc3SetHalt(FALSE) for every enabled stalled
// endpoint) and because a host that meets a stall, issues CLEAR_FEATURE, and meets the same
// stall again concludes the device is broken and resets it. That is a plausible reading of
// console199's eleven resets.
//
// `target` is the endpoint address from a CLEAR_FEATURE (0x81, 0x02, ...), or kAllEndpoints
// when the whole device is being re-armed after a bus reset.
void clearStalls(unsigned target) {
  if (target == kAllEndpoints) {
    endpointCommand(gGadget.core, kDwc3Ep0Out, kDwc3DepCmdClearStall, 0, 0, 0);
    endpointCommand(gGadget.core, kDwc3Ep0In, kDwc3DepCmdClearStall, 0, 0, 0);
    if (gGadget.dataEndpoints) {
      endpointCommand(gGadget.core, kEpBulkOut, kDwc3DepCmdClearStall, 0, 0, 0);
      endpointCommand(gGadget.core, kEpBulkIn, kDwc3DepCmdClearStall, 0, 0, 0);
      endpointCommand(gGadget.core, kEpNotify, kDwc3DepCmdClearStall, 0, 0, 0);
    }
    return;
  }
  // Endpoint address to physical endpoint: number * 2 + direction, with IN as the odd number.
  const unsigned number = target & 0x0fu;
  const unsigned ep = number * 2u + ((target & 0x80u) ? 1u : 0u);
  if (ep > kEpBulkIn || (!gGadget.dataEndpoints && ep > kDwc3Ep0In)) {
    return;
  }
  endpointCommand(gGadget.core, ep, kDwc3DepCmdClearStall, 0, 0, 0);
}

void restartDataEndpoints() {
  if (!gGadget.dataEndpoints) {
    return;
  }
  if (gGadget.txInflight) {
    native_dwc3_gadget_tx_aborted = native_dwc3_gadget_tx_aborted + 1;
  }
  endTransfer(kEpBulkIn);
  endTransfer(kEpBulkOut);
  endTransfer(kEpNotify);
  // The bytes never crossed, so the run that was in flight is still at the queue tail and
  // the next armBulkIn() hands the same bytes over again.
  gGadget.txInflight = 0;
  armBulkOut();
  // A reset discarded the notification too; the host re-opens the port and waits for
  // carrier again, so it has to be presented once more.
  armSerialState();
}

}  // namespace

// ---- Stage C: endpoint 0 ---------------------------------------------------------
//
// Everything above brings the controller up and lets the host see a link. It does not
// answer: console191 measured twenty device events, five resets, DSTS reporting
// HS_SUSPEND, and not one SETUP. A host that resets a device and gets no descriptor
// back resets it again -- which is exactly the five resets in that log.
//
// The state machine below is the firmware's (UsbfnDwc3Dxe/Dwc3Endpoint.c:388-612),
// reduced to what a device that only has to enumerate needs:
//
//   arm a CONTROL_SETUP TRB on EP0-OUT   (Dwc3Ep0OutStart, :388)
//   XferNotReady / Complete on EP0-OUT   -> the eight SETUP bytes are in the buffer
//   answer in one of three shapes:
//     no data stage      -> zero-length IN  on EP0-IN   (CONTROL_STATUS2)
//     data stage, IN     -> payload on EP0-IN           (CONTROL_DATA)
//                           then zero-length OUT on EP0-OUT (CONTROL_STATUS3)
//     data stage, OUT    -> zero-length OUT on EP0-OUT  (CONTROL_STATUS3)
//   and re-arm.
//
// Two things here were easy to get wrong and are worth the words:
//  * ENDTRANSFER carries its resource index in the command word (bits 22:16), not in a
//    parameter register, and index 0 means "nothing outstanding" (Dwc3Endpoint.c:348).
//  * STARTTRANSFER takes PAR0 = high half of the TRB address, PAR1 = low half -- the
//    opposite of every other command's parameter order.

namespace {

enum : unsigned {
  kControlPageBytes = 4096,
  kSetupOffset      = 0,      // eight SETUP bytes land here, and an OUT payload follows
  kDataOffset       = 64,     // the IN payload is staged here
  // SET_LINE_CODING is a seven byte host-to-device stage; the host expects those same
  // seven bytes back from GET_LINE_CODING.
  kEpLineCodingBytes = 7,
  // Two of the descriptor tables in the kernel image are 18 bytes and the strings are
  // shorter still; 256 bytes leaves room for the CDC-ACM configuration stage D adds
  // without touching this page.
  kDataBytes        = 256,
};

enum Ep0State : unsigned {
  kEp0Idle    = 0,
  kEp0Setup   = 1,   // a CONTROL_SETUP TRB is armed
  kEp0Data    = 2,   // the data stage is being moved
  kEp0Status  = 3,   // the status stage is being moved
};

// vid/pid from pid.codes (0x1209 is the open-source test VID), so nothing here can be
// mistaken for a real product. bDeviceClass 0x02 marks a single CDC function at device
// level, which is what lets the host bind a serial driver from the device descriptor
// alone; the interface descriptors repeat it for hosts that look there instead.
const uint8_t kDeviceDescriptor[18] = {
    18, 0x01,             // bLength, DEVICE
    0x00, 0x02,           // bcdUSB 2.00
    0x02, 0x00, 0x00,     // class: CDC, subclass / protocol per interface
    64,                   // bMaxPacketSize0
    0x09, 0x12,           // idVendor  0x1209
    0x01, 0x00,           // idProduct 0x0001
    0x00, 0x01,           // bcdDevice 1.00
    1, 2, 3,              // iManufacturer / iProduct / iSerialNumber
    1,                    // bNumConfigurations
};

// CDC-ACM, the shape Windows binds usbser.sys to, which is what turns this device into a
// COM port instead of an entry under "other devices". Two interfaces: a communication
// interface with one interrupt IN endpoint for notifications, and a data interface with a
// bulk pair. The class-specific descriptors between them are mandatory -- a host rejects
// a CDC interface whose functional descriptors are missing.
//
// The endpoints are notification 0x81 and bulk 0x82 / 0x02: USB endpoint 1 IN and USB
// endpoint 2 in both directions. DWC3 numbers a physical endpoint as 2 * number +
// direction with direction 1 being IN, so those are physical 3, 5 and 4.
const uint8_t kConfigDescriptor[] = {
    9, 0x02,              // bLength, CONFIGURATION
    67, 0x00,             // wTotalLength: 9 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 7
    2,                    // bNumInterfaces
    1,                    // bConfigurationValue
    0,                    // iConfiguration
    0x80,                 // bmAttributes: bus powered, no remote wakeup
    50,                   // bMaxPower: 100 mA

    9, 0x04,              // bLength, INTERFACE -- communication
    0, 0,                 // bInterfaceNumber, bAlternateSetting
    1,                    // bNumEndpoints
    0x02, 0x02, 0x01,     // CDC / ACM
    0,                    // iInterface

    5, 0x24, 0x00,        // HEADER functional descriptor
    0x10, 0x01,           // bcdCDC 1.10
    5, 0x24, 0x01,        // CALL MANAGEMENT functional descriptor
    0x00,                 // bmCapabilities: no call management
    1,                    // bDataInterface
    4, 0x24, 0x02,        // ACM functional descriptor
    0x02,                 // bmCapabilities: line coding and serial state
    5, 0x24, 0x06,        // UNION functional descriptor
    0, 1,                 // control interface, subordinate data interface

    7, 0x05, 0x81,        // bLength, ENDPOINT -- IN, interrupt
    0x03,                 // bmAttributes: interrupt
    16, 0x00,             // wMaxPacketSize 16
    16,                   // bInterval: 16 frames

    9, 0x04,              // bLength, INTERFACE -- data
    1, 0,                 // bInterfaceNumber, bAlternateSetting
    2,                    // bNumEndpoints
    0x0a, 0x00, 0x00,     // CDC data
    0,                    // iInterface

    7, 0x05, 0x82,        // bLength, ENDPOINT -- IN, bulk
    0x02,                 // bmAttributes: bulk
    64, 0x00,             // wMaxPacketSize 64
    0,                    // bInterval

    7, 0x05, 0x02,        // bLength, ENDPOINT -- OUT, bulk
    0x02,                 // bmAttributes: bulk
    64, 0x00,             // wMaxPacketSize 64
    0,                    // bInterval
};

const uint8_t kStringLangId[4] = {4, 0x03, 0x09, 0x04};   // en-US

// USB strings are UTF-16LE. These are ASCII, so the high byte is always zero. The first
// header byte is the whole descriptor's length: two bytes of header plus two per
// character, so it has to be written out per string rather than computed.
struct Utf16String {
  uint8_t header[2];
  char text[16];
};

const Utf16String kStrings[] = {
    {{2 + 2 * 6, 0x03}, "UniXNU"},           // iManufacturer = 1, six characters
    {{2 + 2 * 14, 0x03}, "Enchilada DWC3"},  // iProduct = 2, fourteen
    {{2 + 2 * 4, 0x03}, "0001"},             // iSerialNumber = 3, four
};

// Build one of the three strings above into wire format at `out`, never more than
// `limit` bytes. Returns the length written.
unsigned buildString(unsigned index, uint8_t *out, unsigned limit) {
  if (index == 0) {
    const unsigned n = limit < sizeof kStringLangId ? limit : unsigned(sizeof kStringLangId);
    __builtin_memcpy(out, kStringLangId, n);
    return n;
  }
  if (index > (sizeof kStrings / sizeof kStrings[0])) {
    return 0;
  }
  const Utf16String &source = kStrings[index - 1];
  const unsigned length = source.header[0];
  const unsigned n = length < limit ? length : limit;
  out[0] = uint8_t(length);
  out[1] = 0x03;
  unsigned i = 2;
  while (i + 1 < n) {
    out[i] = uint8_t(source.text[(i - 2) / 2]);
    out[i + 1] = 0;
    i += 2;
  }
  if (i < n) {
    out[i] = 0;
    ++i;
  }
  return n;
}

uintptr_t controlPhys(unsigned offset) {
  return gGadget.control.phys + offset;
}

// Every state change goes through here so the log's ep0 field always names the state
// the machine is actually in, not the last event word that happened to arrive.
void setEp0State(unsigned state) {
  gGadget.ep0State = state;
  native_dwc3_gadget_ep0_state = state;
}

// Re-arm: stop whatever endpoint 0 has outstanding and hand EP0-OUT an empty
// eight-byte buffer for the next SETUP. Dwc3Ep0OutStart (:388) does the same, and it
// is the only state in which the controller accepts a new setup packet.
//
// Both endpoints' TRBs are reclaimed rather than only ended: Dwc3Ep0OutStart calls
// Dwc3EndTransfer on both, and Dwc3EndTransfer sends ENDTRANSFER when a resource index
// is outstanding... but it also clears TrbPool[0] and TrbPool[1] on the way out
// (:358-362), which is what makes the next STARTTRANSFER legal. Without that half, a
// stale HWO with no resource index left to end is unrecoverable -- console193.
void armSetup() {
  endTransfer(kDwc3Ep0In);
  endTransfer(kDwc3Ep0Out);
  clearTrb(kDwc3Ep0In);
  clearTrb(kDwc3Ep0Out);
  gGadget.threeStage = false;
  gGadget.dataIn = false;
  if (startTransfer(kDwc3Ep0Out, controlPhys(kSetupOffset), 8, kDwc3TrbTypeControlSetup)) {
    gGadget.ep0Armed = true;
    setEp0State(kEp0Setup);
  } else {
    gGadget.ep0Armed = false;
  }
}

// Answer one standard request. Returns the number of bytes staged for a data stage, or
// 0 when the request has no data stage (the status stage follows immediately).
unsigned handleSetup(const uint8_t *setup, bool *supported) {
  const uint8_t requestType = setup[0];
  const uint8_t request = setup[1];
  const unsigned value = unsigned(setup[2]) | (unsigned(setup[3]) << 8);
  const unsigned length = unsigned(setup[6]) | (unsigned(setup[7]) << 8);
  uint8_t *data = gGadget.control.virt + kDataOffset;

  *supported = true;
  native_dwc3_gadget_last_setup = (uint32_t(requestType) << 24) | (uint32_t(request) << 16) | (value & 0xffffu);

  // Standard, device-to-host requests are 0x80 / 0x8x.
  if (requestType == 0x80 && request == 0x06) {   // GET_DESCRIPTOR
    const unsigned type = (value >> 8) & 0xffu;
    const unsigned index = value & 0xffu;
    unsigned available = 0;
    if (type == 0x01) {                            // DEVICE
      available = sizeof kDeviceDescriptor;
      __builtin_memcpy(data, kDeviceDescriptor, available);
    } else if (type == 0x02) {                     // CONFIGURATION
      available = sizeof kConfigDescriptor;
      __builtin_memcpy(data, kConfigDescriptor, available);
    } else if (type == 0x03) {                     // STRING
      available = buildString(index, data, kDataBytes);
      if (available == 0) {
        *supported = false;
        return 0;
      }
    } else {
      *supported = false;
      return 0;
    }
    // The controller reads the payload out of DMA, so whatever was just written has to
    // reach memory before the TRB is handed over. startTransfer() cleans the TRB only.
    if (available) {
      cleanDma(data, available);
    }
    return available < length ? available : length;
  }

  // GET_STATUS: device (0x80), interface (0x81) and endpoint (0x82) all answer two bytes.
  // Windows asks this as soon as it binds a driver, and a stall here costs it a whole
  // enumeration retry: console199 collected eleven of those. Nothing in this device is
  // self-powered, asks for remote wakeup or stays halted after the reset path clears it,
  // so the honest answer is zero -- but it has to be an answer.
  if ((requestType == 0x80 || requestType == 0x81 || requestType == 0x82) && request == 0x00) {
    data[0] = 0;
    data[1] = 0;
    cleanDma(data, 2);
    return length < 2 ? length : 2;
  }

  if (requestType == 0x80 && request == 0x08) {    // GET_CONFIGURATION: one byte
    data[0] = uint8_t(native_dwc3_gadget_configured);
    cleanDma(data, 1);
    return length < 1 ? length : 1;
  }

  // The CDC data interface has no alternate settings, so its only answer is zero.
  if (requestType == 0x81 && request == 0x0a) {    // GET_INTERFACE: one byte
    data[0] = 0;
    cleanDma(data, 1);
    return length < 1 ? length : 1;
  }

  if (requestType == 0x01 && request == 0x0b) {    // SET_INTERFACE: no data stage
    return 0;
  }

  // SET_FEATURE / CLEAR_FEATURE. For an endpoint, wValue selects the feature (0 =
  // ENDPOINT_HALT) and wIndex carries the endpoint address, so the clear has to look there.
  // A stall that is never lifted is exactly what makes a host give up on a device, so both
  // requests are answered and the clear actually clears. An interface has no halt of its
  // own here, so the request is acknowledged and nothing else.
  if ((requestType == 0x01 || requestType == 0x02) && (request == 0x03 || request == 0x01)) {
    if (request == 0x01 && (value & 0xffffu) == 0) {
      const unsigned index = unsigned(setup[4]) | (unsigned(setup[5]) << 8);
      if (requestType == 0x02) {
        clearStalls(index & 0xffu);
      }
    }
    return 0;
  }

  if (requestType == 0x00 && request == 0x05) {    // SET_ADDRESS: no data stage
    uint32_t dcfg = read32(gGadget.core, kDwc3DCFG);
    dcfg &= ~kDwc3DcfgDevAddrMask;
    dcfg |= dwc3DcfgDevAddrBits(value & 0x7fu);
    write32(gGadget.core, kDwc3DCFG, dcfg);
    native_dwc3_gadget_address = value & 0x7fu;
    return 0;
  }

  if (requestType == 0x00 && request == 0x09) {    // SET_CONFIG
    native_dwc3_gadget_configured = value & 0xffu;
    if (value) {
      // Nothing below endpoint 0 exists until this arrives. Programming the CDC
      // endpoints here is what turns an enumerated device into a COM port.
      if (!programDataEndpoints()) {
        *supported = false;
        return 0;
      }
    } else {
      unprogramDataEndpoints();
    }
    return 0;
  }

  if (requestType == 0x21 && request == 0x20) {    // SET_LINE_CODING: 7 bytes OUT
    // A serial driver sends this before it opens the port. This is an OUT data stage, so
    // the receive TRB has to be handed over before the host starts sending -- the caller
    // stages it through startDataStage() by leaving the length non-zero. The seven bytes
    // are only readable once that stage completes, so they are captured in the completion
    // path in handleEp0Event() rather than here.
    return length;
  }

  if (requestType == 0xa1 && request == 0x21) {    // GET_LINE_CODING: 7 bytes IN
    // Hand back exactly what the host last set. A constant here is what kept usbser.sys
    // retrying: it writes a rate, reads it back, and treats a mismatch as a refusal.
    __builtin_memcpy(data, gGadget.lineCodingBytes, kEpLineCodingBytes);
    cleanDma(data, kEpLineCodingBytes);
    return length < kEpLineCodingBytes ? length : unsigned(kEpLineCodingBytes);
  }

  // SET_CONTROL_LINE_STATE: Class, interface recipient, host to device -- 0x21, not 0xa1.
  // console201 sent exactly this (last=21220000) and got a stall, because the direction bit
  // made it look like a request the device does not implement. usbser.sys raises DTR here
  // as part of Open(), so the stall is what Windows reported as "The semaphore timeout
  // period has expired": the driver was waiting for an acknowledgement that never came.
  if (requestType == 0x21 && request == 0x22) {    // SET_CONTROL_LINE_STATE: no data stage
    gGadget.dtr = (value & 0x1u) != 0;
    native_dwc3_gadget_dtr = gGadget.dtr ? 1 : 0;
    return 0;
  }

  if (requestType == 0x82 && request == 0x06) {    // GET_DESCRIPTOR (to the interface)
    // A CDC driver asks for the class-specific functional descriptors this way: wValue is
    // the descriptor type in the high byte and its index in the low one. They are already
    // inside kConfigDescriptor, so walk that table and hand back the nth one of that type.
    const unsigned type = (value >> 8) & 0xffu;
    const unsigned wanted = value & 0xffu;
    if (type == 0x21 || type == 0x24) {
      const unsigned whole = sizeof kConfigDescriptor;
      unsigned offset = 0, seen = 0;
      while (offset + 2 <= whole) {
        const unsigned size = kConfigDescriptor[offset];
        if (size < 2 || offset + size > whole) {
          break;
        }
        if (kConfigDescriptor[offset + 1] == type && seen++ == wanted) {
          __builtin_memcpy(data, kConfigDescriptor + offset, size);
          cleanDma(data, size);
          return size < length ? size : length;
        }
        offset += size;
      }
      *supported = false;
      return 0;
    }
  }

  // Anything else -- including GET_STATUS, GET_CONFIGURATION and the class requests a
  // driver would send after SET_CONFIG -- is refused, which tells the host plainly
  // that this device is not ready for it yet.
  *supported = false;
  return 0;
}

// The data stage has been decided; hand it to the endpoint the host will use.
void startDataStage(unsigned length) {
  gGadget.dataArmed = length;
  if (length == 0) {
    // A control-only request (wLength 0) still owes the host a status stage, but the
    // controller is the one that opens it: it posts XFERNOTREADY(CONTROL_STATUS) and
    // Dwc3Ep0StartStatus answers that (Dwc3Endpoint.c:503-524, 550-610). Arming ahead of
    // it -- which run 201 did, and then run 202 did again for the zero-length case --
    // leaves a TRB whose HWO never clears and the next SETUP baulks at the HWO check.
    setEp0State(kEp0Status);
    return;
  }
  if (gGadget.dataIn) {
    if (!startTransfer(kDwc3Ep0In, controlPhys(kDataOffset), length, kDwc3TrbTypeControlData)) {
      setEp0State(kEp0Status);
      return;
    }
  } else {
    // Host-to-device data stage: the TRB has to be armed on EP0-OUT before the host
    // sends, and the same buffer the SETUP landed in is reused for the payload.
    if (!startTransfer(kDwc3Ep0Out, controlPhys(kSetupOffset), length, kDwc3TrbTypeControlData)) {
      setEp0State(kEp0Status);
      return;
    }
  }
  setEp0State(kEp0Data);
}

// Zero-length status stage. STATUS3 when there was a data stage, STATUS2 when the
// request was control-only (Dwc3Ep0StartStatus, :503).
//
// The controller posts XFERNOTREADY for the status phase and keeps posting it until a
// TRB is there, so this has to be idempotent: if a transfer is already outstanding on
// this endpoint the phase is being served and restarting it would cancel a status the
// host is waiting for.
bool startStatusStage(unsigned ep) {
  if (gGadget.resourceIndex[ep] != 0) {
    return true;
  }
  const unsigned type = gGadget.threeStage ? kDwc3TrbTypeControlStatus3 : kDwc3TrbTypeControlStatus2;
  endTransfer(ep);
  if (!startTransfer(ep, controlPhys(kDataOffset), 0, type)) {
    return false;
  }
  native_dwc3_gadget_status_armed = native_dwc3_gadget_status_armed + 1;
  setEp0State(kEp0Status);
  return true;
}

// One endpoint event from the drain loop. Event/status are the decoded fields; the
// endpoint number is physical, so EP0-OUT is 0 and EP0-IN is 1.
void handleEp0Event(unsigned ep, unsigned type, unsigned status) {
  native_dwc3_gadget_ep0_events = native_dwc3_gadget_ep0_events + 1;

  if (type == kDwc3EpEventXferComplete) {
    // The transfer the controller just finished holds the endpoint's resource no longer.
    // Leaving the index behind makes startStatusStage()'s idempotence check think a stage
    // is still outstanding and skip arming the one the host is waiting for -- on an IN data
    // stage the status stage goes out on EP0-OUT, whose index is the SETUP transfer's.
    gGadget.resourceIndex[ep] = 0;
    if (gGadget.ep0State == kEp0Setup) {
      // The eight SETUP bytes were written by the controller, not the CPU, so the cache
      // lines covering them have to be dropped before they are read.
      invalidateDma(gGadget.control.virt + kSetupOffset, 64);
      const uint8_t *setup = gGadget.control.virt + kSetupOffset;
      native_dwc3_gadget_setups = native_dwc3_gadget_setups + 1;
      {
        // requestType<<24 | request<<16 | wLength, not wValue: the requests that have hung
        // this device are class requests whose wLength is what decides whether a data stage
        // follows, and that is the field the log needs to show.
        const unsigned slot = native_dwc3_gadget_setup_log_next % kSetupLogEntries;
        native_dwc3_gadget_setup_log[slot] = (uint32_t(setup[0]) << 24) | (uint32_t(setup[1]) << 16) |
                                             uint32_t(setup[6] | (setup[7] << 8));
        native_dwc3_gadget_setup_log_next = native_dwc3_gadget_setup_log_next + 1;
      }
      gGadget.ep0Armed = false;

      const bool threeStage = (setup[6] | setup[7]) != 0;
      const bool dataIn = (setup[0] & 0x80u) != 0;
      // Only an IN data stage is answered here; an OUT data stage would need a receive
      // buffer this device has no request for yet. handleSetup() decides by returning 0.
      bool supported = false;
      const unsigned length = handleSetup(setup, &supported);
      if (!supported) {
        // Refuse it: a stall is what makes the host move on instead of retrying.
        endpointCommand(gGadget.core, ep, kDwc3DepCmdSetStall, 0, 0, 0);
        armSetup();
        return;
      }
      gGadget.threeStage = threeStage;
      gGadget.dataIn = dataIn;
      native_dwc3_gadget_answered = native_dwc3_gadget_answered + 1;
      startDataStage(length);
      return;
    }

    if (gGadget.ep0State == kEp0Data) {
      // The data stage is over; record it and wait for the controller to name the status
      // stage. Arming it here was tried in run 201 and made things much worse: a STATUS3
      // TRB handed to EP0-OUT ahead of the controller's own XFERNOTREADY(CONTROL_STATUS)
      // kept its HWO set forever, and the next SETUP then baulked at the HWO check. That
      // run reached 9 SETUPs where the previous one reached 151, and never got as far as
      // SET_CONFIG. The controller is the one that knows which endpoint and which phase
      // the host is about to use, so it gets to say when (Dwc3HandleEp0Event,
      // Dwc3Endpoint.c:550-610).
      if (!gGadget.dataIn) {
        const unsigned got = bytesTransferred(ep, gGadget.dataArmed);
        gGadget.outBytes = gGadget.outBytes + got;
        native_dwc3_gadget_ep0_out_bytes = native_dwc3_gadget_ep0_out_bytes + got;
        // SET_LINE_CODING is the only control request on this device with a host-to-device
        // data stage, and the controller wrote those bytes straight into DMA. Keep them so
        // GET_LINE_CODING can hand the same seven bytes back: console204 repeated the
        // SET/GET pair forty times because the answer was a constant 9600, which read as
        // "the device refused the rate" and kept usbser.sys in its open loop.
        if (got >= kEpLineCodingBytes) {
          invalidateDma(gGadget.control.virt + kSetupOffset, kEpLineCodingBytes);
          __builtin_memcpy(gGadget.lineCodingBytes, gGadget.control.virt + kSetupOffset, kEpLineCodingBytes);
          gGadget.lineCoding = true;
          native_dwc3_gadget_line_coding = 1;
        }
      }
      setEp0State(kEp0Status);
      return;
    }

    if (gGadget.ep0State == kEp0Status) {
      native_dwc3_gadget_status_done = native_dwc3_gadget_status_done + 1;
      armSetup();
      return;
    }
    // A completion with no stage outstanding means a transfer finished after the state
    // machine had already moved on; counting it is how a re-armed SETUP that arrived too
    // late shows up in the log.
    native_dwc3_gadget_stuck_setups = native_dwc3_gadget_stuck_setups + 1;
    return;
  }

  if (type == kDwc3EpEventXferNotReady) {
    // The host started a stage endpoint 0 has no TRB for. That is the controller's way
    // of saying "your turn" for both the data and the status stage.
    native_dwc3_gadget_notready = native_dwc3_gadget_notready + 1;
    native_dwc3_gadget_notready_last = (ep << 8) | (status & 0xffu);
    const unsigned phase = status & 0x3u;
    // phase 1 is CONTROL_DATA. The data stage is driven by the SETUP handler deciding
    // how many bytes to move, so a not-ready there means the host is waiting on a TRB
    // that startDataStage() has not handed over yet -- nothing to do from this side.
    if (phase == 1) {                       // CONTROL_DATA
      return;
    }
    if (phase == 2) {                       // CONTROL_STATUS
      if (ep == kDwc3Ep0In) {
        native_dwc3_gadget_status_nr_in = native_dwc3_gadget_status_nr_in + 1;
      } else {
        native_dwc3_gadget_status_nr_out = native_dwc3_gadget_status_nr_out + 1;
      }
      if (gGadget.ep0State == kEp0Setup) {
        // A status phase announced while the state machine still believes it is collecting
        // a SETUP means the data stage's completion was never seen, or was seen after this.
        // Counting it rather than silently dropping it is what tells the two apart.
        native_dwc3_gadget_status_nr_skipped = native_dwc3_gadget_status_nr_skipped + 1;
        return;
      }
      startStatusStage(ep);
      return;
    }
    return;
  }
}

}  // namespace

// ---- Bring-up ------------------------------------------------------------------

// Everything stage B does, in the firmware's order, with the SMMU planned first so
// a refusal costs nothing. Returns true when the controller is running and
// endpoint 0 is programmed.
bool dwc3GadgetStart(uintptr_t core, uintptr_t smmu, uintptr_t qscratch, const UfsSmmuPlan &plan,
                     unsigned physicalEps, unsigned maxPacket) {
  gGadget.core = core;
  gGadget.smmu = smmu;
  gGadget.qscratch = qscratch;
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
  for (unsigned ep = 0; ep < physicalEps && ep < kMaxRings; ++ep) {
    if (!gGadget.rings[ep].allocate(kEventBufferBytes)) {
      native_dwc3_gadget_error = "trb ring";
      native_dwc3_gadget_status = -int32_t(kStageEventBuf);
      return false;
    }
    linkRing(ep);
  }

  // Endpoint 0's buffer page: the eight SETUP bytes at 0 and the IN payload at 64. The
  // descriptors themselves stay in the kernel image and are copied into this page per
  // request, so there is exactly one copy of each table to keep correct.
  if (!gGadget.control.allocate(kControlPageBytes)) {
    native_dwc3_gadget_error = "control page";
    native_dwc3_gadget_status = -int32_t(kStageEventBuf);
    return false;
  }

  // ---- Core soft reset, and why it has to come before the PHY is set up -------------
  //
  // Dwc3CoreInit (Dwc3Core.c:242-254) selects device mode, stalls 100 us, calls
  // Dwc3SoftReset, and only *then* calls Dwc3PhyInterfaceSetup. That order is not
  // cosmetic: DCTL.CsfTReset returns the block's registers to their reset values, so
  // anything written to GUSB2PHYCFG before it is erased by it. The first cuts of this
  // file instead adopted whatever UEFI left behind, and the controller sat in device
  // mode with endpoint 0 programmed, RunStop set, and a link that never left
  // "disconnected" -- console186 and console187 both measured exactly that.
  //
  // Dwc3SoftReset also pulses PHYSOFTRST on the UTMI PHY itself (Dwc3Core.c:70-84), which
  // is a different thing from the register reset below: it drops the PHY's own state
  // machine, which the core reset does not reach.
  //
  // Both waits are timebase spins. IODelay parks the calling CPU in WFE on this SoC and
  // the poll never returns (RootCommands.h:22, NativeUfs.cpp:337).
  native_dwc3_gadget_stage = kStageSoftReset;

  // GCTL: device mode first, as the firmware does at :242-245.
  uint32_t gctl_before_reset = read32(core, kDwc3GCTL);
  gctl_before_reset &= ~kDwc3GctlPrtCapMask;
  gctl_before_reset |= kDwc3GctlPrtCapDevice;
  write32(core, kDwc3GCTL, gctl_before_reset);
  for (unsigned spin = 0; spin < 100; ++spin) {
    for (volatile unsigned pause = 0; pause < 50; pause = pause + 1) {
    }
  }

  // PHYSOFTRST pulse: assert, ~100 us, release.
  uint32_t phy_reset = read32(core, kDwc3GUSB2PHYCFG0);
  write32(core, kDwc3GUSB2PHYCFG0, phy_reset | kDwc3Gusb2PhyCfgPhySoftRst);
  for (unsigned spin = 0; spin < 100; ++spin) {
    for (volatile unsigned pause = 0; pause < 50; pause = pause + 1) {
    }
  }
  write32(core, kDwc3GUSB2PHYCFG0, read32(core, kDwc3GUSB2PHYCFG0) & ~kDwc3Gusb2PhyCfgPhySoftRst);
  for (unsigned spin = 0; spin < 100; ++spin) {
    for (volatile unsigned pause = 0; pause < 50; pause = pause + 1) {
    }
  }

  // Core reset proper, then wait for the controller to drop CsfTReset on its own.
  uint32_t dctl_reset = read32(core, kDwc3DCTL);
  write32(core, kDwc3DCTL, dctl_reset | kDwc3DctlCoreSoftReset);
  bool reset_done = false;
  for (unsigned spin = 0; spin < 200000; ++spin) {
    if (!(read32(core, kDwc3DCTL) & kDwc3DctlCoreSoftReset)) {
      reset_done = true;
      break;
    }
    for (volatile unsigned pause = 0; pause < 50; pause = pause + 1) {
    }
  }
  native_dwc3_gadget_resets = native_dwc3_gadget_resets + 1;
  if (!reset_done) {
    native_dwc3_gadget_error = "core soft reset timed out";
    native_dwc3_gadget_status = -int32_t(kStageSoftReset);
    return false;
  }

  // ---- PHY interface, now that the reset has cleared the register --------------------
  //
  // Dwc3PhyInterfaceSetup (Dwc3Core.c:109-145): clear PHYIF/USBTRDTIM/ULPI_UTMI, program
  // the width and turnaround this board's PHY wants, then clear SUSPHY and ENBLSLPM.
  // Writing these before the reset would have thrown them away.
  uint32_t phy_before = read32(core, kDwc3GUSB2PHYCFG0);
  uint32_t phy = phy_before;
  phy &= ~(kDwc3Gusb2PhyCfgSusPhy | kDwc3Gusb2PhyCfgEnblSlpm);
  phy &= ~(kDwc3Gusb2PhyCfgPhyIfMask | kDwc3Gusb2PhyCfgUsbTrdTimMask);
  phy &= ~kDwc3Gusb2PhyCfgUlpiUtmi;
  phy |= dwc3Gusb2PhyCfgPhyIf(kDwc3UtmiPhyIf8Bit);
  phy |= dwc3Gusb2PhyCfgUsbTrdTim(kDwc3UsbTrdTimUtmi8Bit);
  write32(core, kDwc3GUSB2PHYCFG0, phy);
  native_dwc3_gadget_phy_before = phy_before;
  native_dwc3_gadget_phy_after = read32(core, kDwc3GUSB2PHYCFG0);

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

  // GCTL prtcap again, exactly as the firmware does after the event buffer
  // (Dwc3Core.c:278-281): programming GEVNT* can drop the block back to its reset
  // direction, and a controller that is not in device mode never asserts a pull-up.
  uint32_t gctl_after_events = read32(core, kDwc3GCTL);
  gctl_after_events &= ~kDwc3GctlPrtCapMask;
  gctl_after_events |= kDwc3GctlPrtCapDevice;
  write32(core, kDwc3GCTL, gctl_after_events);

  // DCFG: high speed (the board is USB2 only) and address 0. A stale address from
  // the firmware's own session would make every control transfer go unanswered.
  uint32_t dcfg = read32(core, kDwc3DCFG);
  dcfg &= ~kDwc3DcfgSpeedMask;
  dcfg |= kDwc3DcfgHighSpeed;
  dcfg &= ~kDwc3DcfgDevAddrMask;
  write32(core, kDwc3DCFG, dcfg);

  // DEVTEN: the same five the firmware enables (Dwc3Core.c:305-310). SOF is deliberately
  // left off so a running link does not fill the buffer one event per frame. The first cut
  // added ErraticErr/CmdCmpl/Overflow and dropped Wakeup; the delta is small but there is no
  // reading of this controller yet in which deviating from the known-good order has paid off.
  write32(core, kDwc3DEVTEN, kDwc3DevtenDisconEvtEn | kDwc3DevtenUsbRstEn | kDwc3DevtenConnectDoneEn |
                                 kDwc3DevtenUlstCnGen | kDwc3DevtenWkupEvtEn);

  // DALEPENA cleared before any endpoint is programmed (Dwc3Core.c:312): the two DEPCFGs
  // below arm endpoints from scratch rather than adding to whatever UEFI left enabled.
  write32(core, kDwc3DALEPENA, 0);

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

  // Tell the wrapper the cable is in, before the controller is told to run. See
  // applyVbusOverride(): mainline calls this from probe in peripheral mode and again from
  // pre_run_stop, so the override is in place on both sides of the RunStop write.
  native_dwc3_gadget_stage = kStageVbus;
  applyVbusOverride(qscratch, true);

  // DCTL.RunStop last, exactly as the firmware does it (Dwc3Core.c:191-201).
  native_dwc3_gadget_stage = kStageRun;
  uint32_t dctl = read32(core, kDwc3DCTL);
  dctl &= ~kDwc3DctlTstCtrlMask;
  write32(core, kDwc3DCTL, dctl | kDwc3DctlRunStop);

  // ...and once more right after, because this is the write that makes the controller
  // sample the session lines. The read-back in applyVbusOverride records both words.
  applyVbusOverride(qscratch, false);

  gGadget.running = true;

  // Endpoint 0 waits for the host's first SETUP, which means an empty eight-byte
  // CONTROL_SETUP TRB has to be outstanding before the host resets the device. The
  // firmware arms it from the connect-done event (Dwc3Endpoint.c:766); arming it here
  // as well costs one command and covers the case where the link came up before the
  // event thread started.
  native_dwc3_gadget_stage = kStageEp0Config;
  armSetup();

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
      const unsigned ep = dwc3EpEventEpNumber(event);
      const unsigned type = dwc3EpEventType(event);
      if (type == kDwc3EpEventCmdComplete) {
        // A command completion carries the command's resource index, not a state this
        // side tracks; the C row wants our own state, so leave ep0_state alone here.
        continue;
      }
      if (ep <= kDwc3Ep0In) {
        handleEp0Event(ep, type, dwc3EpEventStatus(event));
      } else {
        handleDataEvent(ep, type);
      }
      continue;
    }
    switch (dwc3DevEventType(event)) {
      case kDwc3DevEventReset: {
        // The host reset the link: address 0 again and endpoint 0 back to waiting for a
        // SETUP. The firmware does the same on every reset (Dwc3Endpoint.c:696-724 ends
        // with Dwc3Ep0ResetState), and a device that does not re-arm after a reset is a
        // device the host gives up on.
        native_dwc3_gadget_resets = native_dwc3_gadget_resets + 1;
        uint32_t dcfg = read32(core, kDwc3DCFG);
        dcfg &= ~kDwc3DcfgDevAddrMask;
        write32(core, kDwc3DCFG, dcfg);
        native_dwc3_gadget_address = 0;
        // The data endpoints lost their in-flight transfers with the reset; hand them back
        // and lift any stall before endpoint 0 starts answering again.
        clearStalls(kAllEndpoints);
        restartDataEndpoints();
        armSetup();
        break;
      }
      case kDwc3DevEventConnectDone: {
        // The link finished its handshake. Endpoint 0 stays as programmed: its packet
        // size is 64 for both high and full speed, which are the only two this board can
        // reach with a USB2-only PHY, so there is nothing the speed changes. The firmware
        // reprograms here (Dwc3Endpoint.c:744-766) because it also serves SuperSpeed,
        // where the size is 512.
        native_dwc3_gadget_speed = dwc3DstsConnectSpeed(read32(core, kDwc3DSTS));
        armSetup();
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
  // Stage E: hand the host whatever the writer queued since the last pass. This is the
  // only place a bulk IN transfer is armed from, which keeps every TRB write on this
  // thread -- the writer in package/BootLog.c only ever touches the byte queue.
  armBulkIn();
}
