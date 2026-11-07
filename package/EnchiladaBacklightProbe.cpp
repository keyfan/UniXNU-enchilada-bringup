// SPDX-License-Identifier: BSD-2-Clause-Patent
// enchilada stage-3 layers 2.5 / 3b: MSM DSI0 read-only probes + first brightness WRITE.
//
// `enchilada-backlight=1` (default off: nothing is mapped or read without it) makes the
// boot progress monitor's 1 Hz poll map the DSI0 controller at its 6G-shifted base
// 0xae94004, read seven live config/status registers, and publish their values into
// native_backlight_stats[10] so the UFS/serial `L st=...` row becomes non-zero on device.
// This mode is DEVICE-VERIFIED (backlight208 / CI #208, 510 rows all `st=3`) and its
// behavior is kept byte-for-byte unchanged here.
//
// `enchilada-backlight=2` is the layer-3 PREREQUISITE (still READ-ONLY): it maps the
// apps_smmu@0x15000000 window (0x80000, the DTS-declared size also used by the UFS
// driver) and runs the pure-C read-only display-bank survey MsmDsiSmmu.c, publishing
// the verdict (Bypass / Translate / NotFound / Geometry) + matched SMR/S2CR/SCTLR into
// the SAME native_backlight_stats[10] with stage code 4 (via msm_smmu_survey_pack).
// It answers whether the display's CMD-DMA base is a PHYSICAL address (bypass /
// translation off, SCTLR.M==0) or an SMMU IOVA, which must be settled BEFORE the first
// DCS brightness packet is ever sent. NO register is written and NO DMA is triggered.
//
// WHY READ-ONLY / "先核验再上机": the panel is already lit by the UEFI GOP framebuffer
// and the firmware owns DSI0 (command mode, clocks, lanes, PHY, software DMA trigger all
// configured by the bootloader). Before any DCS packet is sent we must VERIFY on the real
// device that XNU can map and read the controller. A read-only snapshot cannot disturb the
// scanout or hang the DSI bus. This mirrors the device-proven staged probes on this board:
//   * EnchiladaTouchProbe.cpp  stage 1 = "read only"
//   * EnchiladaCpuFreq.cpp     mode 1  = "read only"
// Mode 2 (backlight209mode2, CI #209) has since DEVICE-CONFIRMED the answer: the display
// SMMU bank is in BYPASS (SCTLR.M=0), so a CPU PHYSICAL address passes through unchanged;
// the "physical vs MDSS IOVA" question is settled. What remains is the 32-bit
// REG_DSI_DMA_BASE: dsi.xml declares it 32 bits, and whether a fresh kernel allocation
// lands below 4 GiB on this board is UNCERTAIN (NativeUfs.cpp:1197 warns of high
// allocations on the sibling SoC; enchilada's carveouts are all below 4 GiB in
// enchilada-facts/parsed.txt). Mode 3 resolves that ON THE DEVICE.
//
// `enchilada-backlight=3` is the FIRST WRITE (stage 3, layer 3b). It allocates ONE
// physically-contiguous DMA buffer with the device-proven IOBufferMemoryDescriptor
// pattern (Dwc3Gadget.cpp:204-218), maps DSI0 at 0xae94004, and calls the pure-C,
// host-verified MsmDsiBrightness.c orchestrator ONCE. That orchestrator REFUSES (before
// touching any register) when (phys >> 32) != 0 and publishes the verdict into the L row:
// st=5 (transmit OK) / st=-2 (AddrTooHigh -> need the stage-1 SMMU fallback) / st=-4
// (DMA busy timeout). Cache maintenance is `dc civac` per 64-byte line + `dsb sy`, exactly
// the Dwc3Gadget.cpp:306-313 cleanDma / NativeUfs.cpp:1150-1155 pattern. Only ONE packet
// is sent; later 1 Hz polls just republish the stored result so the L row stays stable.
//
// This file is a THIN wiring shim. Modes 1/2 define NO IOKit C++ classes; mode 3 USES the
// existing IOBufferMemoryDescriptor class but still defines no IOService subclass and no
// OSMetaClass, so it needs NO late-object objcopy. All logic lives in the pure-C,
// host-tested drivers/NativeMsmDsi/*.c, compiled into the same libEnchiladaMsmDsi.a
// archive (already -force_load'ed by configs/enchilada_defconfig). The C++ ABI surface
// stays limited to a plain function, like EnchiladaCpuFreq.cpp / EnchiladaTouchProbe.cpp.
#include <pexpert/pexpert.h>
#include <stdint.h>
#include <kern/task.h>                       // kernel_task (Dwc3Gadget.cpp:205 allocation pattern)
#include <IOKit/IOMemoryDescriptor.h>         // IOOptionBits / kIOMemory* (mode 3)
#include <IOKit/IOBufferMemoryDescriptor.h>  // the device-proven physically-contiguous DMA buffer

// These headers carry their own extern "C" guards (MsmDsiCmd.h / MsmDsiSession.h /
// MsmDsiSmmu.h / MsmDsiBrightness.h / DsiDcs.h), so include them at file scope rather
// than nesting C standard headers inside an extern "C" block.
#include "MsmDsiCmd.h"         // MSM_DSI0_CTRL_PHYS, MSM_DSI0_REG_SHIFT, struct msm_dsi_io
#include "MsmDsiSession.h"     // snapshot/publish + stage codes (mode 1)
#include "MsmDsiSmmu.h"        // MSM_SMMU_BASE/WINDOW, read-only display-bank survey + pack (mode 2)
#include "MsmDsiBrightness.h"  // the 32-bit-gated, cache-cleaned brightness transmit (mode 3)

extern "C" {
// Device-proven cache-inhibited kernel mapping of a physical range (drivers/ArmCpu/
// ArmCpu.cpp:207, used the same way by package/BootDiagExport.c:43-51). Kept forever:
// never released, so the register reads stay valid for the whole boot.
uint64_t armcpu_map_physical(uint64_t physical, uint64_t length) __attribute__((weak_import));

// Strong definition that takes precedence over the weak char[4096] stand-in in
// package/BootProgressStubs.c:65. The type matches package/BootLog.c:174 exactly
// (C linkage does not type-check across TUs); the L row reads stats[0..9].
volatile int32_t native_backlight_stats[10];
}

namespace {
// The mapped, cache-inhibited virtual address of the dsi_ctrl window. Hoisted to file
// scope and never released (the DWC3 bring-up bug: an inspect()-local OSSharedPtr was
// destroyed on return and the gadget thread then read garbage, cb=6f6f6c6c "lloo").
uintptr_t gDsiVa = 0;
// apps_smmu@0x15000000 window, mapped only for enchilada-backlight=2 (mode 2).
uintptr_t gSmmuVa = 0;
volatile uint32_t gPolls = 0;

// Mode 3 (FIRST WRITE) state, all hoisted to file scope and kept forever -- the same
// lifetime rule as gDsiVa: an inspect()-local OSSharedPtr<IOMemoryMap> destroyed on return
// makes a later kernel thread read garbage (DWC3 cb=6f6f6c6c "lloo"). One DMA buffer is
// allocated once; the result/counters are republished every 1 Hz poll.
static constexpr unsigned kMode3DmaBytes = 64u;   // one 64-byte cache line, 4096-aligned
static constexpr uint16_t kMode3Level = 512;      // mid-range DCS level (1..1023), one shot
IOBufferMemoryDescriptor *gDma = nullptr;
uint8_t *gDmaVirt = nullptr;
uint64_t gDmaPhys = 0;
struct msm_dsi_brightness_result gResult = {};
struct msm_dsi_brightness_counters gCounters = {};
bool gTransmitDone = false;

uint32_t ioRd(void *ctx, uint32_t reg) {
    uintptr_t va = reinterpret_cast<uintptr_t>(ctx) + reg;
    return *reinterpret_cast<volatile uint32_t *>(va);
}
void ioWr(void *ctx, uint32_t reg, uint32_t value) {
    uintptr_t va = reinterpret_cast<uintptr_t>(ctx) + reg;
    *reinterpret_cast<volatile uint32_t *>(va) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
void ioWmb(void *) { __asm__ volatile("dsb sy" ::: "memory"); }
void ioDelayUs(void *, unsigned) { /* unused in the read-only stage */ }

// Mode 3 only: a CPU spin, NOT an XNU IODelay/WFE wait (RootCommands.h:22 records a
// WFE-based poll never returned on enchilada). msm_dsi_cmd_wait_idle() calls this between
// STATUS0.CMD_DMA_BUSY polls; a small bounded busy loop, never a blocking kernel delay.
void ioDelayUsSpin(void *, unsigned us) {
    for (unsigned i = 0; i < us * 64u; ++i)
        __asm__ volatile("" ::: "memory");   // keep the busy loop; C++20-safe counter
}

// Mode 3 only: cache maintenance for the CMD DMA buffer. The CPU fills the buffer through
// its cached kernel mapping; the DSI controller reads it through the bypass SMMU. Match
// Dwc3Gadget.cpp:306-313 cleanDma() and NativeUfs.cpp:1150-1155 verbatim: `dc civac` over
// every 64-byte line covering [va, va+len), then `dsb sy`. Invoked by
// msm_dsi_brightness_send() AFTER the buffer is filled and BEFORE the TRIG_DMA write.
void cleanDma(void *, const void *va, size_t len) {
    const uintptr_t start = reinterpret_cast<uintptr_t>(va) & ~uintptr_t(63);
    const uintptr_t end = (reinterpret_cast<uintptr_t>(va) + len + 63) & ~uintptr_t(63);
    for (uintptr_t line = start; line < end; line += 64)
        __asm__ volatile("dc civac, %0" ::"r"(line) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

// Mode 1 (device-verified, backlight208 / CI #208): unchanged behavior. Maps DSI0 at
// 0xae94004 and publishes the seven live registers with stage code 3.
void pollMode1(void) {
    if (!gDsiVa) {
        uint64_t base = armcpu_map_physical(MSM_DSI0_CTRL_PHYS, 0x400u);
        if (!base) { native_backlight_stats[0] = kMsmDsiSessionStageMapFailed; return; }
        // dsi.xml offsets are relative to the 6G-shifted base (base + REG_SHIFT).
        gDsiVa = static_cast<uintptr_t>(base) + MSM_DSI0_REG_SHIFT;
        native_backlight_stats[0] = kMsmDsiSessionStageWaitPlatform;
    }

    struct msm_dsi_io io = {};
    io.ctx = reinterpret_cast<void *>(gDsiVa);
    io.rd = ioRd;
    io.wr = ioWr;
    io.wmb = ioWmb;
    io.delay_us = ioDelayUs;

    struct msm_dsi_snapshot snap = {};
    if (msm_dsi_session_snapshot(&io, &snap) == 0) {
        // C++20 deprecates ++ on a volatile-qualified object (-Werror,-Wdeprecated-volatile
        // in the CI kernel build); use an explicit read + assignment instead.
        gPolls = gPolls + 1;
        msm_dsi_session_publish(&snap, gPolls, native_backlight_stats);
    }
}

// Mode 2 (layer-3 prerequisite, READ-ONLY): maps the apps_smmu window and runs the
// pure-C read-only display-bank survey, publishing the verdict with stage code 4.
// NO register is written and NO DMA is triggered; this never touches DSI0.
void pollMode2(void) {
    if (!gSmmuVa) {
        uint64_t base = armcpu_map_physical(MSM_SMMU_BASE, MSM_SMMU_WINDOW);
        if (!base) { native_backlight_stats[0] = kMsmSmmuStageMapFailed; return; }
        // Unlike DSI0, SMMUv2 global offsets have NO +4 shift: the XML/dump offsets
        // (SmmuTakeover.h, NativeUfs.cpp:862-944) are relative to the map base.
        gSmmuVa = static_cast<uintptr_t>(base);
        native_backlight_stats[0] = kMsmSmmuStageWaitPlatform;
    }

    struct msm_smmu_io io = {};
    io.ctx = reinterpret_cast<void *>(gSmmuVa);
    io.rd = ioRd;   // read-only: struct msm_smmu_io has no write callback at all

    struct msm_smmu_survey survey = {};
    if (msm_smmu_survey_display(&io, MSM_SMMU_WINDOW, &survey) == 0) {
        gPolls = gPolls + 1;
        msm_smmu_survey_publish(&survey, gPolls, native_backlight_stats);
    }
}

// Mode 3 (stage 3, layer 3b): the FIRST WRITE. Allocate ONE low, physically-contiguous
// DMA buffer, map DSI0, and call the host-verified brightness orchestrator ONCE. The
// orchestrator's 32-bit REG_DSI_DMA_BASE gate is the "先核验再上机" check: a high buffer
// address is REFUSED (st=-2) before any register is touched, so a truncated address can
// never make the controller DMA the wrong bytes. Only one packet is sent; later polls
// republish the stored result (polls keeps climbing, sent stays 1) so the L row is stable.
void pollMode3(void) {
    gPolls = gPolls + 1;
    gCounters.polls = gPolls;

    if (!gTransmitDone) {
        // Lazy, one-time map + allocate: the 1 Hz poll simply retries until
        // armcpu_map_physical and the IOKit allocator are available (monitor() runs early).
        if (!gDsiVa) {
            uint64_t base = armcpu_map_physical(MSM_DSI0_CTRL_PHYS, 0x400u);
            if (base) gDsiVa = static_cast<uintptr_t>(base) + MSM_DSI0_REG_SHIFT;
        }
        if (gDsiVa && !gDmaVirt) {
            if (!gDma) {
                gDma = IOBufferMemoryDescriptor::inTaskWithOptions(
                    kernel_task,
                    static_cast<IOOptionBits>(kIODirectionInOut) |
                        static_cast<IOOptionBits>(kIOMemoryPhysicallyContiguous) |
                        static_cast<IOOptionBits>(kIOMemoryMapperNone),
                    kMode3DmaBytes, 4096);
            }
            if (gDma) {
                uint8_t *v = static_cast<uint8_t *>(gDma->getBytesNoCopy());
                IOByteCount contiguous = 0;
                uint64_t p = gDma->getPhysicalSegment(0, &contiguous, kIOMemoryMapperNone);
                if (v && p && !(p & 4095) && contiguous >= kMode3DmaBytes) {
                    gDmaVirt = v;
                    gDmaPhys = p;
                }
            }
        }
        if (gDsiVa && gDmaVirt && gDmaPhys) {
            struct msm_dsi_io io = {};
            io.ctx = reinterpret_cast<void *>(gDsiVa);
            io.rd = ioRd;
            io.wr = ioWr;
            io.wmb = ioWmb;
            io.delay_us = ioDelayUsSpin;   // CPU spin, NOT WFE (RootCommands.h:22)
            struct msm_dsi_brightness_result r = {};
            msm_dsi_brightness_send(&io, gDmaVirt, kMode3DmaBytes, gDmaPhys,
                                    cleanDma, nullptr, kMode3Level, &r);
            gResult = r;
            if (r.stage == kMsmDsiBrightnessStageTx)
                gCounters.sent = gCounters.sent + 1;
            else if (r.stage == kMsmDsiBrightnessStageAddrTooHigh)
                gCounters.addr_high = gCounters.addr_high + 1;
            else if (r.stage == kMsmDsiBrightnessStageDma)
                gCounters.dma_err = gCounters.dma_err + 1;
            gTransmitDone = true;
        } else {
            // Platform/allocator not ready yet: publish a distinct "waiting" stage so the
            // L row is not mistaken for the default-off weak stub zeros (st=0).
            gResult.stage = kMsmDsiBrightnessStageNoIo;
        }
    }
    msm_dsi_brightness_publish(&gResult, &gCounters, native_backlight_stats);
}
}  // namespace

// Called once a second by package/BootProgress.c monitor(). Idempotent: after the first
// successful map each mode only re-publishes, so a missing mapping stays at the weak
// stub's zeros (stage 0) rather than spamming the log. Modes 1/2 are READ-ONLY by
// construction; mode 3 is the first WRITE and sends exactly ONE packet (then republishes).
extern "C" void enchilada_backlight_poll(void) {
    static bool absent = false;
    if (absent) return;

    unsigned mode = 0;
    if (!PE_parse_boot_argn("enchilada-backlight", &mode, sizeof mode)) {
        absent = true;
        return;   // default off: leave native_backlight_stats at the weak zeros
    }
    if (mode != 1 && mode != 2 && mode != 3) {
        absent = true;
        return;   // any other value is "off" too
    }
    // Wait until the ArmCpu archive that owns armcpu_map_physical is linked (mirrors
    // BootDiagExport.c:50). The 1 Hz poll simply retries until then.
    if (!&armcpu_map_physical) return;

    if (mode == 1) pollMode1();
    else if (mode == 2) pollMode2();
    else pollMode3();
}
