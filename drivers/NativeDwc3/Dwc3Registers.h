// SPDX-License-Identifier: BSD-2-Clause-Patent
// Synopsys DesignWare USB3 DRD ("DWC3") register map and decode helpers for the
// SDM845 primary USB controller (usb_1_dwc3 @ 0x0a600000, core window 0xcd00).
//
// Only register OFFSETS and field positions are used here. They are architectural
// facts of the Synopsys DWC3 IP, the same numbers the mainline Linux dwc3 driver
// records in drivers/usb/dwc3/core.h (GPL-2.0); no DWC3 code is copied. Everything
// below is either a plain offset/bit constant or our own decode logic. The driver
// is a read-only probe first (see NativeDwc3.cpp); nothing here writes MMIO.
//
// This header is also compiled on the host by tests/ (NATIVE_DWC3_HOST), so it uses
// only fixed-width integer types and no kernel headers.
#ifndef NATIVE_DWC3_REGISTERS_H
#define NATIVE_DWC3_REGISTERS_H

#include <stdint.h>

// ---- Physical layout left by the firmware (SDM845 / enchilada) -------------------
// From the upstream device tree (arch/arm64/boot/dts/qcom/sdm845.dtsi,
// sdm845-oneplus-common.dtsi) and Mu-Silicium's MemoryMapLib ("USB30_PRIM",
// 0x0A600000, 0x0011B000):
//   usb_1       wrapper   0x0a6f8800 (QSCRATCH etc., not touched by the probe)
//   usb_1_dwc3  core      0x0a600000  size 0xcd00
//   usb_1_hsphy QUSB2 v2  0x088e2000  size 0x400
//   GCC         clocks    0x00100000
//   apps_smmu             0x15000000  size 0x80000,  dwc3 stream <&apps_smmu 0x740 0>
enum : uint64_t {
  kDwc3CoreBase   = 0x0a600000ull,
  kDwc3CoreSize   = 0xcd00ull,
  kDwc3QscratchBase = 0x0a6f8800ull,   // usb_1 wrapper (GGPIO/QSCRATCH); probe maps 0x1000
  kQusb2PhyBase   = 0x088e2000ull,
  kQusb2PhySize   = 0x400ull,
  kGccBase        = 0x00100000ull,
  kAppsSmmuBase   = 0x15000000ull,
  kAppsSmmuSize   = 0x80000ull,
};
enum : uint16_t { kDwc3SmmuStream = 0x740, kDwc3SmmuMask = 0x0 };

// ---- GCC branch/GDSC offsets for USB30_PRIM ----------------------------------
// From the SDM845 GCC driver (drivers/clk/qcom/gcc-sdm845.c): halt_reg of each
// clk_branch and the GDSC gdscr. CLK_OFF is bit31 of a CBCR; PWR_ON is bit31 of a
// GDSCR. Read-only: the probe only reports whether UEFI left these running.
enum : uint32_t {
  kGccUsb30PrimGdscr        = 0xf004,   // usb30_prim_gdsc
  kGccUsb30PrimMasterCbcr   = 0xf00c,   // gcc_usb30_prim_master_clk
  kGccUsb30PrimSleepCbcr    = 0xf010,   // gcc_usb30_prim_sleep_clk
  kGccUsb30PrimMockUtmiCbcr = 0xf014,   // gcc_usb30_prim_mock_utmi_clk
  kGccCfgNocUsb3PrimCbcr    = 0x502c,   // gcc_cfg_noc_usb3_prim_axi_clk
  kGccAggreUsb3PrimCbcr     = 0x8201c,  // gcc_aggre_usb3_prim_axi_clk
  kGccUsbPhyCfgAhb2PhyCbcr  = 0x6a004,  // gcc_usb_phy_cfg_ahb2phy_clk
};

// ---- DWC3 global registers (core base + offset) ------------------------------
enum : uint32_t {
  kDwc3GSBUSCFG0   = 0xc100,
  kDwc3GSBUSCFG1   = 0xc104,
  kDwc3GCTL        = 0xc110,
  kDwc3GUCTL1      = 0xc11c,
  kDwc3GSTS        = 0xc118,
  kDwc3GSNPSID     = 0xc120,
  kDwc3GGPIO       = 0xc124,
  kDwc3GUID        = 0xc128,
  kDwc3GUCTL       = 0xc12c,
  kDwc3GBUSERRADDR0 = 0xc130,
  kDwc3GBUSERRADDR1 = 0xc134,
  kDwc3GHWPARAMS0  = 0xc140,
  kDwc3GHWPARAMS1  = 0xc144,
  kDwc3GHWPARAMS2  = 0xc148,
  kDwc3GHWPARAMS3  = 0xc14c,
  kDwc3GHWPARAMS4  = 0xc150,
  kDwc3GHWPARAMS5  = 0xc154,
  kDwc3GHWPARAMS6  = 0xc158,
  kDwc3GHWPARAMS7  = 0xc15c,
  kDwc3GHWPARAMS8  = 0xc600,
  kDwc3GUSB2PHYCFG0 = 0xc200,
  kDwc3GUSB3PIPECTL0 = 0xc2c0,
  // Event buffer 0 (we use a single event buffer for the gadget).
  kDwc3GEVNTADRLO0 = 0xc400,
  kDwc3GEVNTADRHI0 = 0xc404,
  kDwc3GEVNTSIZ0   = 0xc408,
  kDwc3GEVNTCOUNT0 = 0xc40c,
};

// ---- DWC3 device (gadget) registers ------------------------------------------
enum : uint32_t {
  kDwc3DCFG    = 0xc700,
  kDwc3DCTL    = 0xc704,
  kDwc3DEVTEN  = 0xc708,
  kDwc3DSTS    = 0xc70c,
  kDwc3DGCMDPAR = 0xc710,
  kDwc3DGCMD   = 0xc714,
  kDwc3DALEPENA = 0xc720,
  // Per-endpoint command: base + ep*0x10.
  kDwc3DEPCMDPAR2Base = 0xc800,
  kDwc3DEPCMDPAR1Base = 0xc804,
  kDwc3DEPCMDPAR0Base = 0xc808,
  kDwc3DEPCMDBase     = 0xc80c,
};
static inline uint32_t dwc3DepCmd(unsigned ep)     { return kDwc3DEPCMDBase + ep * 0x10u; }
static inline uint32_t dwc3DepCmdPar0(unsigned ep) { return kDwc3DEPCMDPAR0Base + ep * 0x10u; }
static inline uint32_t dwc3DepCmdPar1(unsigned ep) { return kDwc3DEPCMDPAR1Base + ep * 0x10u; }
static inline uint32_t dwc3DepCmdPar2(unsigned ep) { return kDwc3DEPCMDPAR2Base + ep * 0x10u; }

// ---- Field helpers ------------------------------------------------------------
// GSNPSID: the top 16 bits are the IP class, the low 16 the revision. DWC_usb3
// reports 0x5533xxxx; DWC_usb31 reports 0x3331xxxx / 0x3330xxxx.
static inline uint16_t dwc3SnpsClass(uint32_t id) { return uint16_t(id >> 16); }
static inline uint16_t dwc3SnpsRevision(uint32_t id) { return uint16_t(id & 0xffff); }
static inline bool dwc3SnpsIsUsb3(uint32_t id) {
  uint16_t c = dwc3SnpsClass(id);
  return c == 0x5533 || c == 0x3331 || c == 0x3330;
}

// GCTL: core soft reset is bit11; prtcap direction (current mode) is bits [13:12]
// (1 = host, 2 = device, 3 = OTG).
enum : uint32_t { kDwc3GctlCoreSoftReset = 1u << 11 };
static inline unsigned dwc3GctlPrtCap(uint32_t gctl) { return (gctl >> 12) & 3u; }

// DCFG: device speed is bits [2:0] (0 hs, 1 fs, 4 ss, ...); device address bits [9:3].
static inline unsigned dwc3DcfgSpeed(uint32_t dcfg) { return dcfg & 7u; }
static inline unsigned dwc3DcfgDevAddr(uint32_t dcfg) { return (dcfg >> 3) & 0x7fu; }

// DCTL run/stop is bit31; core soft reset is bit30.
enum : uint32_t { kDwc3DctlRunStop = 1u << 31, kDwc3DctlCoreSoftReset = 1u << 30 };

// DSTS: connected link speed bits [2:0]; USBLNKST (link state) bits [21:18];
// DEVCTRLHLT bit22 is set while the device controller is halted.
static inline unsigned dwc3DstsConnectSpeed(uint32_t dsts) { return dsts & 7u; }
static inline unsigned dwc3DstsLinkState(uint32_t dsts) { return (dsts >> 18) & 0xfu; }
static inline bool dwc3DstsHalted(uint32_t dsts) { return (dsts & (1u << 22)) != 0; }

// GEVNTSIZ: event-buffer size in bytes is bits [15:0]; bit31 masks the event
// interrupt. GEVNTCOUNT: pending byte count is bits [15:0].
static inline uint32_t dwc3EvntSizBytes(uint32_t siz) { return siz & 0xffffu; }
static inline bool dwc3EvntSizMasked(uint32_t siz) { return (siz & (1u << 31)) != 0; }
static inline uint32_t dwc3EvntCountBytes(uint32_t cnt) { return cnt & 0xffffu; }

// ---- A snapshot of the controller state, for the boot log --------------------
struct Dwc3Snapshot {
  uint32_t gctl, gsts, gsnpsid, ghwparams0, ghwparams1, ghwparams3;
  uint32_t dcfg, dctl, dsts, dalepena, devten;
  uint32_t gevntsiz0, gevntcount0;
  uint32_t gusb2phycfg0;
  uint32_t qusb2_pll_status;   // QUSB2 PHY PLL/common status word (see Dwc3Phy.h)
  uint32_t gcc_gdscr, gcc_master, gcc_mock_utmi, gcc_sleep, gcc_noc_axi, gcc_aggre_axi, gcc_ahb2phy;
};

// Does the GCC state the firmware left mean the DWC3 core window can be read
// safely? PWR_ON (GDSCR bit31) must be set and the master + AHB2PHY branches must
// not be CLK_OFF (CBCR bit31). Mirrors NativeUfs's read-only clock gate.
static inline bool dwc3GccCoreReadable(const Dwc3Snapshot &s) {
  bool powered = (s.gcc_gdscr & 0x80000000u) != 0;
  bool master = !(s.gcc_master & 0x80000000u);
  bool ahb2phy = !(s.gcc_ahb2phy & 0x80000000u);
  return powered && master && ahb2phy;
}

// ---- Device-mode programming (stage B) ---------------------------------------
// Everything below is needed to bring the controller up as a USB device and run
// endpoint 0. The numbers are the architectural DWC3 ones; Mu-Silicium's
// Silicon/Samsung/SamsungPkg/Include/Device/Dwc3.h carries the same set, and that
// firmware drives this exact controller for fastboot, so it is the cross-check.

// GCTL.PrtCapDir: bits [13:12], 1 = host, 2 = device, 3 = OTG.
enum : uint32_t { kDwc3GctlPrtCapMask = 3u << 12, kDwc3GctlPrtCapDevice = 2u << 12 };

// The rest of GCTL that the firmware clears before running the core. Every position here is
// Mu-Silicium's (Silicon/Samsung/SamsungPkg/Include/Device/Dwc3.h:78-90), which is the firmware
// that drives this controller for fastboot on this board:
//
//   RAMCLKSEL    bits [7:6]   RAM clock divider select -- NOT a control bit to clear
//   SCALEDOWN    bits [5:4]
//   DISSCRAMBLE  bit 3
//   U2EXIT_LFPS  bit 2
//   GBLHIBERNATIONEN bit 1
//   DSBLCLKGTNG  bit 0
//
// The first cut of Dwc3Gadget.cpp cleared bits 16, 6, 8 and 9 for these four, which both left
// scrambling/hibernation/clock-gating on and wrote RAMCLKSEL (bit 6) to 0. The controller stopped
// answering and the boot wrote no log at all.
enum : uint32_t {
  kDwc3GctlRamClkSelMask     = 3u << 6,
  kDwc3GctlScaleDownMask     = 3u << 4,
  kDwc3GctlDisScramble       = 1u << 3,
  kDwc3GctlU2ExitLfps        = 1u << 2,
  kDwc3GctlGlbHibernationEn  = 1u << 1,
  kDwc3GctlDsblClkGtng       = 1u << 0,
};
// Everything Dwc3CoreInit clears (Dwc3Core.c:256-261). RAMCLKSEL is deliberately not in here.
enum : uint32_t { kDwc3GctlRunClearMask = kDwc3GctlScaleDownMask | kDwc3GctlDisScramble |
                                           kDwc3GctlGlbHibernationEn | kDwc3GctlDsblClkGtng };

// DCFG device speed, bits [2:0]. The board is wired USB2 only (one HS PHY,
// maximum-speed = "high-speed"), so high speed is the only setting that can
// complete: asking for SuperSpeed on a USB2 link leaves the device unenumerated.
enum : uint32_t { kDwc3DcfgSpeedMask = 7u, kDwc3DcfgHighSpeed = 0u, kDwc3DcfgFullSpeed = 1u };
enum : uint32_t { kDwc3DcfgDevAddrMask = 0x7fu << 3 };
static inline uint32_t dwc3DcfgDevAddrBits(unsigned address) { return (address & 0x7fu) << 3; }

// DCTL.RunStop bit31 starts the device controller; CsfTReset bit30 resets it.
enum : uint32_t { kDwc3DctlTstCtrlMask = 7u << 16 };

// DEVTEN (device event enable, 0xc708): the bits we care about.
enum : uint32_t {
  kDwc3DevtenDisconEvtEn     = 1u << 0,
  kDwc3DevtenUsbRstEn        = 1u << 1,
  kDwc3DevtenConnectDoneEn   = 1u << 2,
  kDwc3DevtenUlstCnGen       = 1u << 3,
  kDwc3DevtenWkupEvtEn       = 1u << 4,
  kDwc3DevtenErraticErrEn    = 1u << 9,
  kDwc3DevtenCmdCmplEn       = 1u << 10,
  kDwc3DevtenOverflowEn      = 1u << 11,
};

// GUSB2PHYCFG(0): PHY soft reset (bit31), suspend PHY (bit6), UTMI width (bits
// [3:2]) and turnaround time (bits [10:7]).
enum : uint32_t {
  kDwc3Gusb2PhyCfgPhySoftRst   = 1u << 31,
  kDwc3Gusb2PhyCfgSusPhy       = 1u << 6,
  kDwc3Gusb2PhyCfgPhyIfMask    = 3u << 2,
  kDwc3Gusb2PhyCfgUsbTrdTimMask = 0xfu << 7,
  kDwc3Gusb2PhyCfgUlpiUtmi     = 1u << 4,
};
static inline uint32_t dwc3Gusb2PhyCfgPhyIf(unsigned width) { return (width & 3u) << 2; }
static inline uint32_t dwc3Gusb2PhyCfgUsbTrdTim(unsigned t) { return (t & 0xfu) << 7; }

// GEVNTSIZ(0): size in bytes [15:0]; bit31 masks the event interrupt. We run the
// event loop from a kernel thread and never enable the DWC3 interrupt, so the
// mask stays set and the thread drains GEVNTCOUNT itself.
enum : uint32_t { kDwc3EvntSizIntMask = 1u << 31 };
static inline uint32_t dwc3EvntSizSize(unsigned bytes) { return bytes & 0xffffu; }

// DALEPENA: one bit per physical endpoint.
static inline uint32_t dwc3DalepEna(unsigned ep) { return 1u << ep; }

// ---- Endpoint commands --------------------------------------------------------
// DEPCMD register, low 4 bits are the command, bit10 is CMDACT (still running),
// bits [15:12] the completion status, bits [22:16] the resource index on
// STARTTRANSFER.
enum : uint32_t {
  kDwc3DepCmdSetEpConfig    = 0x01,
  kDwc3DepCmdSetXferResource = 0x02,
  kDwc3DepCmdGetEpState     = 0x03,
  kDwc3DepCmdSetStall       = 0x04,
  kDwc3DepCmdClearStall     = 0x05,
  kDwc3DepCmdStartTransfer  = 0x06,
  kDwc3DepCmdUpdateTransfer = 0x07,
  kDwc3DepCmdEndTransfer    = 0x08,
  kDwc3DepCmdStartConfig    = 0x09,
};
enum : uint32_t {
  kDwc3DepCmdCmdAct       = 1u << 10,
  kDwc3DepCmdHiPriForceRm = 1u << 11,
  kDwc3DepCmdCmdIoc       = 1u << 8,
};
static inline uint32_t dwc3DepCmdStatus(uint32_t cmd) { return (cmd >> 12) & 0xfu; }
static inline unsigned dwc3DepCmdResourceIndex(uint32_t cmd) { return (cmd >> 16) & 0x7fu; }

// DEPCFG parameter 0: endpoint type, max packet size, FIFO number, action.
static inline uint32_t dwc3DepCfgEpTypeBits(unsigned type) { return (type & 3u) << 1; }
static inline uint32_t dwc3DepCfgMaxPacket(unsigned bytes) { return (bytes & 0x7ffu) << 3; }
static inline uint32_t dwc3DepCfgFifoNumber(unsigned fifo) { return (fifo & 0x1fu) << 17; }
static inline uint32_t dwc3DepCfgBurstSize(unsigned burst) { return (burst & 0xfu) << 22; }
enum : uint32_t {
  kDwc3DepCfgActionInit    = 0u << 30,
  kDwc3DepCfgActionRestore = 1u << 30,
  kDwc3DepCfgActionModify  = 2u << 30,
};
// Endpoint types as DEPCFG wants them.
enum : unsigned { kDwc3EpTypeControl = 0, kDwc3EpTypeIsochronous = 1, kDwc3EpTypeBulk = 2, kDwc3EpTypeInterrupt = 3 };

// DEPCFG parameter 1: interrupt number, the transfer-event enables, and the
// physical endpoint number in bits [29:25].
static inline uint32_t dwc3DepCfgEpNumber(unsigned ep) { return (ep & 0x1fu) << 25; }
enum : uint32_t {
  kDwc3DepCfgXferCompleteEn   = 1u << 8,
  kDwc3DepCfgXferInProgressEn = 1u << 9,
  kDwc3DepCfgXferNotReadyEn   = 1u << 10,
  kDwc3DepCfgFifoErrorEn      = 1u << 11,
  kDwc3DepCfgStreamEventEn    = 1u << 13,
};

// DEPXFERCFG parameter 0: the number of transfer resources to reserve.
static inline uint32_t dwc3DepXferCfgNumXferRes(unsigned n) { return n & 0xffffu; }

// ---- Transfer request blocks --------------------------------------------------
// One 16-byte TRB: buffer pointer, size (bits [23:0], status in [31:28]) and a
// control word. The controller owns a TRB while HWO is set and clears it when the
// transfer is done, which is how the ring is tracked without an interrupt.
struct Dwc3Trb {
  uint32_t bufferLow;
  uint32_t bufferHigh;
  uint32_t size;
  uint32_t control;
};

enum : uint32_t {
  kDwc3TrbCtrlHwo    = 1u << 0,
  kDwc3TrbCtrlLst    = 1u << 1,
  kDwc3TrbCtrlChn    = 1u << 2,
  kDwc3TrbCtrlCsp    = 1u << 3,
  kDwc3TrbCtrlIspImi = 1u << 10,
  kDwc3TrbCtrlIoc    = 1u << 11,
};
static inline uint32_t dwc3TrbCtrlType(unsigned type) { return (type & 0x3fu) << 4; }
static inline uint32_t dwc3TrbSizeLength(unsigned bytes) { return bytes & 0x00ffffffu; }
static inline unsigned dwc3TrbSizeStatus(uint32_t size) { return (size >> 28) & 0xfu; }
enum : unsigned {
  kDwc3TrbTypeNormal        = 1,
  kDwc3TrbTypeControlSetup  = 2,
  kDwc3TrbTypeControlStatus2 = 3,
  kDwc3TrbTypeControlStatus3 = 4,
  kDwc3TrbTypeControlData   = 5,
  kDwc3TrbTypeLink          = 8,
};

// ---- Events -------------------------------------------------------------------
// An event is 4 bytes. Bit0 selects the family: 0 = endpoint event, 1 = device
// event. The endpoint event carries the physical endpoint in bits [5:1].
static inline bool dwc3EventIsEndpoint(uint32_t e) { return (e & 1u) == 0; }
static inline unsigned dwc3EpEventEpNumber(uint32_t e) { return (e >> 1) & 0x1fu; }
static inline unsigned dwc3EpEventType(uint32_t e) { return (e >> 6) & 0xfu; }
static inline unsigned dwc3EpEventStatus(uint32_t e) { return (e >> 12) & 0xfu; }
enum : unsigned {
  kDwc3EpEventXferComplete   = 0x01,
  kDwc3EpEventXferInProgress = 0x02,
  kDwc3EpEventXferNotReady   = 0x03,
  kDwc3EpEventRxTxFifo       = 0x04,
  kDwc3EpEventStream         = 0x06,
  kDwc3EpEventCmdComplete    = 0x07,
};
// Endpoint-event status bits.
enum : unsigned {
  kDwc3EpEventStatusBusErr = 1u << 0,
  kDwc3EpEventStatusShort  = 1u << 1,
  kDwc3EpEventStatusIoc    = 1u << 2,
  kDwc3EpEventStatusLst    = 1u << 3,
};
// Device event: category in bits [7:1], type in bits [11:8].
static inline unsigned dwc3DevEventCategory(uint32_t e) { return (e >> 1) & 0x7fu; }
static inline unsigned dwc3DevEventType(uint32_t e) { return (e >> 8) & 0xfu; }
enum : unsigned {
  kDwc3DevEventDisconnect    = 0,
  kDwc3DevEventReset         = 1,
  kDwc3DevEventConnectDone   = 2,
  kDwc3DevEventLinkStatus    = 3,
  kDwc3DevEventWakeup        = 4,
  kDwc3DevEventHiberReq      = 5,
  kDwc3DevEventEopf          = 6,
  kDwc3DevEventSof           = 7,
  kDwc3DevEventErraticError  = 9,
  kDwc3DevEventCmdCmpl       = 10,
  kDwc3DevEventOverflow      = 11,
};
static inline unsigned dwc3DevEventInfo(uint32_t e) { return (e >> 16) & 0x1ffu; }

// ---- Endpoint 0 -----------------------------------------------------------------
// The control endpoint is always physical endpoints 0 (OUT) and 1 (IN); its
// packet size is 64 bytes on a high-speed link and 8 on a low-speed one.
enum : unsigned {
  kDwc3Ep0Out = 0,
  kDwc3Ep0In  = 1,
  kDwc3Ep0MaxPacketHighSpeed = 64,
  kDwc3Ep0MaxPacketFullSpeed = 64,
};
// GHWPARAMS3 bits: [17:12] the number of physical endpoints, [23:18] how many of
// them are IN endpoints.
static inline unsigned dwc3HwParams3NumEps(uint32_t p) { return (p >> 12) & 0x3fu; }
static inline unsigned dwc3HwParams3NumInEps(uint32_t p) { return (p >> 18) & 0x1fu; }
static inline unsigned dwc3HwParams3HsPhyIfc(uint32_t p) { return (p >> 2) & 3u; }

#endif
