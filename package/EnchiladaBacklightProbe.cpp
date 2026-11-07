// SPDX-License-Identifier: BSD-2-Clause-Patent
// enchilada stage-3 layer 2.5: READ-ONLY runtime probe for the MSM DSI0 command channel.
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
// Sending the brightness packet (stage 3 / AppleARMBacklight) still requires settling the
// CMD-DMA address question (physical vs the MDSS SMMU IOVA Linux 6G uses) and issuing the
// `dc civac` + `dsb sy` cache maintenance that Dwc3Gadget.cpp:306-313 uses for its rings.
//
// This file is deliberately a THIN wiring shim: zero IOKit C++ classes (no IOService
// subclass, no OSMetaClass), only extern "C" kernel primitives. All logic lives in the
// pure-C, host-tested drivers/NativeMsmDsi/MsmDsiSession.c + MsmDsiSmmu.c, compiled into
// the same libEnchiladaMsmDsi.a archive (already -force_load'ed by configs/enchilada_defconfig).
// The C++ ABI surface is therefore limited to a plain function, exactly like the
// established EnchiladaCpuFreq.cpp / EnchiladaTouchProbe.cpp probe entries.
#include <pexpert/pexpert.h>
#include <stdint.h>

// These headers carry their own extern "C" guards (MsmDsiCmd.h / MsmDsiSession.h /
// MsmDsiSmmu.h / DsiDcs.h), so include them at file scope rather than nesting C
// standard headers inside an extern "C" block.
#include "MsmDsiCmd.h"        // MSM_DSI0_CTRL_PHYS, MSM_DSI0_REG_SHIFT, struct msm_dsi_io
#include "MsmDsiSession.h"    // snapshot/publish + stage codes
#include "MsmDsiSmmu.h"       // MSM_SMMU_BASE/WINDOW, read-only display-bank survey + pack

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
}  // namespace

// Called once a second by package/BootProgress.c monitor(). Idempotent: after the first
// successful map each mode only re-surveys, so a missing mapping stays at the weak stub's
// zeros (stage 0) rather than spamming the log. Both modes are READ-ONLY by construction.
extern "C" void enchilada_backlight_poll(void) {
    static bool absent = false;
    if (absent) return;

    unsigned mode = 0;
    if (!PE_parse_boot_argn("enchilada-backlight", &mode, sizeof mode)) {
        absent = true;
        return;   // default off: leave native_backlight_stats at the weak zeros
    }
    if (mode != 1 && mode != 2) {
        absent = true;
        return;   // any other value is "off" too
    }
    // Wait until the ArmCpu archive that owns armcpu_map_physical is linked (mirrors
    // BootDiagExport.c:50). The 1 Hz poll simply retries until then.
    if (!&armcpu_map_physical) return;

    if (mode == 1) pollMode1();
    else pollMode2();
}
