// SPDX-License-Identifier: BSD-2-Clause-Patent
// Native SDM845 DWC3 USB device-mode bring-up for the OnePlus 6 (enchilada).
//
// Default-off: no MMIO is touched unless native-dwc3-probe=1 is present. This is a
// read-only register probe, staged exactly like NativeUfs (drivers/NativeUfs):
//   native-dwc3-probe=1  map GCC + the dwc3 core window UEFI left up, read a
//                        snapshot (GSNPSID/GCTL/DCFG/DSTS/DALEPENA/event buffer)
//                        and the QUSB2 PHY PLL-ready bit. Nothing is written.
//   native-dwc3-probe=2  also compute (read-only) the apps_smmu plan for the dwc3
//                        stream 0x740, the way NativeUfs plans the UFS stream, to
//                        prove the DMA path before any gadget code runs.
// Every stage advances a counter that package/BootLog.c surfaces on the UFS log.
// The CDC-ACM gadget itself (TRB ring, DEPCMDs, enumeration) is stage B and is NOT
// built here; see NOTES-ssh.md. This driver never writes a device-mode register.
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSSharedPtr.h>
#include <pexpert/pexpert.h>
#include <kern/debug.h>
#include <kern/thread.h>
#include <sys/kernel.h>
#define NATIVE_DWC3_KERNEL 1
#include "Dwc3Registers.h"
#include "Dwc3Phy.h"
// The lenient planner for this driver's own stream. It pulls in the ARM SMMUv2 definitions from
// ../NativeUfs/SmmuTakeover.h, which has no include guard, so it must not be included directly.
#include "Dwc3SmmuPlan.h"
// Stage B (Dwc3Gadget.cpp): the device-mode bring-up and the endpoint-0 event loop.
#include "Dwc3Gadget.h"

extern "C" {
// Counters the boot log reads (package/BootLog.c), all weak so a board without the
// log still links. Mirrors NativeUfs's native_ufs_* counter block.
volatile uint32_t native_dwc3_probe_calls = 0;
volatile uint32_t native_dwc3_matched = 0;
volatile uint32_t native_dwc3_mode = 0;      // native-dwc3-probe value actually seen
volatile uint32_t native_dwc3_stage = 0;     // 1 gcc, 2 core snapshot, 3 phy, 4 smmu plan, 5 done
volatile int32_t  native_dwc3_status = -1;    // 0 ok; negative = stage that soft-failed
// Snapshot, for the log.
volatile uint32_t native_dwc3_gsnpsid = 0, native_dwc3_gctl = 0, native_dwc3_gsts = 0;
volatile uint32_t native_dwc3_dcfg = 0, native_dwc3_dctl = 0, native_dwc3_dsts = 0;
volatile uint32_t native_dwc3_dalepena = 0, native_dwc3_devten = 0;
volatile uint32_t native_dwc3_ghwparams0 = 0, native_dwc3_ghwparams1 = 0, native_dwc3_ghwparams3 = 0;
volatile uint32_t native_dwc3_gevntsiz0 = 0, native_dwc3_gevntcount0 = 0;
volatile uint32_t native_dwc3_qusb2_status = 0, native_dwc3_phy_ready = 0;
volatile uint32_t native_dwc3_gcc_gdscr = 0, native_dwc3_gcc_master = 0, native_dwc3_gcc_mock = 0;
volatile uint32_t native_dwc3_gcc_ahb2phy = 0, native_dwc3_gcc_noc_axi = 0, native_dwc3_gcc_aggre_axi = 0;
volatile uint32_t native_dwc3_prtcap = 0;    // GCTL current mode (2 = device)
volatile int32_t  native_dwc3_smmu_slot = -1, native_dwc3_smmu_bank = -1;
volatile uint32_t native_dwc3_smmu_reuse = 0;
volatile uint32_t native_dwc3_smmu_firmware = 0;   // 1 = UEFI's own entry for the stream, adopted as it stands
const char *volatile native_dwc3_smmu_error = nullptr;
const char *volatile native_dwc3_failure = nullptr;
}

static bool gProbeEnabled = false;
static unsigned gProbeMode = 0;

static uint32_t dwc3Read32(uintptr_t base, unsigned offset)
{
  return *reinterpret_cast<volatile uint32_t *>(base + offset);
}

static uintptr_t dwc3MapPhys(uint64_t phys, uint64_t length, OSSharedPtr<IOMemoryMap> &keep)
{
  IODeviceMemory *memory = IODeviceMemory::withRange(phys, length);
  if (!memory) {
    return 0;
  }
  keep = OSSharedPtr<IOMemoryMap>(memory->map(kIOMapAnywhere | kIOMapInhibitCache), OSNoRetain);
  memory->release();
  return keep ? static_cast<uintptr_t>(keep->getVirtualAddress()) : 0;
}

static int gEventWaitEvent;

// What the gadget thread needs from the probe, handed over before it starts.
static uintptr_t gGadgetCore = 0;
static uintptr_t gGadgetSmmu = 0;
static unsigned gGadgetPhysicalEps = 0;
static UfsSmmuPlan gGadgetPlan;

class NativeDwc3 : public IOService {
  OSDeclareDefaultStructors(NativeDwc3)
  void inspect();
public:
  IOService *probe(IOService *provider, SInt32 *score) override;
  bool start(IOService *provider) override;
  // Stage B runs on its own thread: bring the controller up, then poll the event buffer
  // (the controller's interrupt stays masked, so nothing else drains it).
  static void gadgetThread(void *self, wait_result_t);
};

OSDefineMetaClassAndStructors(NativeDwc3, IOService)

IOService *NativeDwc3::probe(IOService *provider, SInt32 *score)
{
  native_dwc3_probe_calls = native_dwc3_probe_calls + 1;
  // Attach to the root device, like NativeUfs's second personality: it starts
  // before the platform expert publishes IOResources, so the probe runs early and
  // before the original AMFI. The probe is a no-op unless the boot arg opts in.
  if (OSDynamicCast(IOPlatformExpertDevice, provider)) {
    native_dwc3_matched = 1;
    if (score) {
      *score = 500;
    }
    return this;
  }
  return nullptr;
}

bool NativeDwc3::start(IOService *provider)
{
  if (!IOService::start(provider)) {
    return false;
  }
  uint32_t value = 0;
  if (PE_parse_boot_argn("native-dwc3-probe", &value, sizeof(value))) {
    gProbeMode = value;
    gProbeEnabled = value != 0;
  }
  native_dwc3_mode = gProbeMode;
  if (!gProbeEnabled) {
    return true;   // default off: nothing is mapped or read
  }
  inspect();
  return true;
}

// Read-only inspection. Each stage sets native_dwc3_stage before it touches MMIO,
// so a fault leaves the last safe stage recorded in the log. Nothing is written.
void NativeDwc3::inspect()
{
  OSSharedPtr<IOMemoryMap> gccMap, coreMap, phyMap, smmuMap;
  Dwc3Snapshot snap;
  bzero(&snap, sizeof(snap));

  native_dwc3_stage = 1;
  uintptr_t gcc = dwc3MapPhys(kGccBase, 0x100000, gccMap);
  if (!gcc) {
    native_dwc3_failure = "gcc map";
    native_dwc3_status = -1;
    return;
  }
  snap.gcc_gdscr    = dwc3Read32(gcc, kGccUsb30PrimGdscr);
  snap.gcc_master   = dwc3Read32(gcc, kGccUsb30PrimMasterCbcr);
  snap.gcc_mock_utmi = dwc3Read32(gcc, kGccUsb30PrimMockUtmiCbcr);
  snap.gcc_sleep    = dwc3Read32(gcc, kGccUsb30PrimSleepCbcr);
  snap.gcc_noc_axi  = dwc3Read32(gcc, kGccCfgNocUsb3PrimCbcr);
  snap.gcc_aggre_axi = dwc3Read32(gcc, kGccAggreUsb3PrimCbcr);
  snap.gcc_ahb2phy  = dwc3Read32(gcc, kGccUsbPhyCfgAhb2PhyCbcr);
  native_dwc3_gcc_gdscr = snap.gcc_gdscr;
  native_dwc3_gcc_master = snap.gcc_master;
  native_dwc3_gcc_mock = snap.gcc_mock_utmi;
  native_dwc3_gcc_ahb2phy = snap.gcc_ahb2phy;
  native_dwc3_gcc_noc_axi = snap.gcc_noc_axi;
  native_dwc3_gcc_aggre_axi = snap.gcc_aggre_axi;

  if (!dwc3GccCoreReadable(snap)) {
    // UEFI did not leave the controller clocked: report and stop, do not guess.
    native_dwc3_failure = "usb30 clocks off after hand-off";
    native_dwc3_status = -1;
    return;
  }

  native_dwc3_stage = 2;
  uintptr_t core = dwc3MapPhys(kDwc3CoreBase, kDwc3CoreSize, coreMap);
  if (!core) {
    native_dwc3_failure = "core map";
    native_dwc3_status = -2;
    return;
  }
  snap.gsnpsid    = dwc3Read32(core, kDwc3GSNPSID);
  snap.gctl       = dwc3Read32(core, kDwc3GCTL);
  snap.gsts       = dwc3Read32(core, kDwc3GSTS);
  snap.ghwparams0 = dwc3Read32(core, kDwc3GHWPARAMS0);
  snap.ghwparams1 = dwc3Read32(core, kDwc3GHWPARAMS1);
  snap.ghwparams3 = dwc3Read32(core, kDwc3GHWPARAMS3);
  snap.dcfg       = dwc3Read32(core, kDwc3DCFG);
  snap.dctl       = dwc3Read32(core, kDwc3DCTL);
  snap.dsts       = dwc3Read32(core, kDwc3DSTS);
  snap.dalepena   = dwc3Read32(core, kDwc3DALEPENA);
  snap.devten     = dwc3Read32(core, kDwc3DEVTEN);
  snap.gevntsiz0  = dwc3Read32(core, kDwc3GEVNTSIZ0);
  snap.gevntcount0 = dwc3Read32(core, kDwc3GEVNTCOUNT0);
  snap.gusb2phycfg0 = dwc3Read32(core, kDwc3GUSB2PHYCFG0);
  native_dwc3_gsnpsid = snap.gsnpsid;
  native_dwc3_gctl = snap.gctl;
  native_dwc3_gsts = snap.gsts;
  native_dwc3_ghwparams0 = snap.ghwparams0;
  native_dwc3_ghwparams1 = snap.ghwparams1;
  native_dwc3_ghwparams3 = snap.ghwparams3;
  native_dwc3_dcfg = snap.dcfg;
  native_dwc3_dctl = snap.dctl;
  native_dwc3_dsts = snap.dsts;
  native_dwc3_dalepena = snap.dalepena;
  native_dwc3_devten = snap.devten;
  native_dwc3_gevntsiz0 = snap.gevntsiz0;
  native_dwc3_gevntcount0 = snap.gevntcount0;
  native_dwc3_prtcap = dwc3GctlPrtCap(snap.gctl);
  if (!dwc3SnpsIsUsb3(snap.gsnpsid)) {
    native_dwc3_failure = "unexpected GSNPSID (not a DWC3 core)";
    native_dwc3_status = -2;
    return;
  }

  native_dwc3_stage = 3;
  uintptr_t phy = dwc3MapPhys(kQusb2PhyBase, kQusb2PhySize, phyMap);
  if (phy) {
    snap.qusb2_pll_status = dwc3Read32(phy, kQusb2PllCommonStatusOne);
    native_dwc3_qusb2_status = snap.qusb2_pll_status;
    native_dwc3_phy_ready = qusb2PhyReady(snap.qusb2_pll_status) ? 1 : 0;
  } else {
    native_dwc3_qusb2_status = 0;
    native_dwc3_phy_ready = 0;
  }

  if (gProbeMode >= 2) {
    native_dwc3_stage = 4;
    // Read-only SMMU plan for the dwc3 stream. The UFS planner cannot answer this question: it
    // insists that a matching stream entry sits on the bank it picked itself, which is right for
    // the UFS queue and wrong here, where the whole point is to inherit the entry UEFI left. See
    // Dwc3SmmuPlan.h. Nothing is configured either way; a representative 64 KiB DMA window at a
    // plausible address only exercises the address-width checks, and the real gadget would pass
    // its own event-buffer/TRB allocation here.
    uintptr_t smmu = dwc3MapPhys(kAppsSmmuBase, kAppsSmmuSize, smmuMap);
    if (!smmu) {
      native_dwc3_failure = "smmu map";
      native_dwc3_status = -4;
      return;
    }
    const uint64_t probeDma = 0x80000000ull, probeBytes = 0x10000ull;
    UfsSmmuPlan plan = planDwc3Smmu(kDwc3SmmuStream, kDwc3SmmuMask, kAppsSmmuSize, probeDma, probeBytes,
                                    [smmu](unsigned off) { return dwc3Read32(smmu, off); });
    native_dwc3_smmu_slot = plan.error ? -1 : int32_t(plan.slot);
    native_dwc3_smmu_bank = plan.error ? -1 : int32_t(plan.bank);
    native_dwc3_smmu_reuse = plan.reuse ? 1 : 0;
    native_dwc3_smmu_firmware = plan.firmware ? 1 : 0;
    native_dwc3_smmu_error = plan.error;

    // Stage B: only when the plan is usable, and only at probe mode 3 or more. The
    // probe has already established that the controller is in device mode with the
    // PHY locked and the GDSC on, so there is nothing to bring up first.
    //
    // It runs on its own thread rather than inline here. The probe's attach happens before
    // the log thread has written anything, so a stage-B hang inline cost the whole boot its
    // log (the first console175 run came back after 70 s with the previous boot's window
    // still in place, which said nothing about where it stopped). On a thread, the probe's
    // U row is already on disk before the first MMIO write, and the G row names the stage
    // even when the bring-up never returns.
    if (gProbeMode >= 3 && !plan.error) {
      native_dwc3_stage = 6;
      gGadgetPhysicalEps = dwc3HwParams3NumEps(snap.ghwparams3);
      if (!gGadgetPhysicalEps || gGadgetPhysicalEps > 8) {
        gGadgetPhysicalEps = 4;   // SDM845's dwc3 reports 4; a sane default if the field reads oddly
      }
      gGadgetCore = core;
      gGadgetSmmu = smmu;
      gGadgetPlan = plan;
      thread_t thread = nullptr;
      if (kernel_thread_start(&NativeDwc3::gadgetThread, this, &thread) == KERN_SUCCESS) {
        thread_deallocate(thread);
      }
    }
  }

  native_dwc3_stage = 5;
  native_dwc3_status = 0;
  native_dwc3_failure = nullptr;
}

static int gEventWaitEvent;

void NativeDwc3::gadgetThread(void *self, wait_result_t)
{
  (void)self;
  thread_set_thread_name(current_thread(), "dwc3_gadget");
  // The probe's U row is already written by now, so a hang in here is visible: the G row
  // keeps whatever stage this reached, and the log keeps being written by the log thread.
  if (!dwc3GadgetStart(gGadgetCore, gGadgetSmmu, gGadgetPlan, gGadgetPhysicalEps, kDwc3Ep0MaxPacketHighSpeed)) {
    // native_dwc3_gadget_error already names the stage that failed.
    native_dwc3_stage = 7;
    return;
  }
  for (;;) {
    dwc3GadgetDrain();
    assert_wait_timeout((event_t)&gEventWaitEvent, THREAD_UNINT, 2, NSEC_PER_MSEC);
    thread_block(THREAD_CONTINUE_NULL);
  }
}
