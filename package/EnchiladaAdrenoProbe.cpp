// SPDX-License-Identifier: BSD-2-Clause-Patent
// Default-off, READ-ONLY GPUCC domain-state probe for enchilada (OnePlus 6, SDM845).
//
// WHAT THIS ANSWERS, AND WHY IT IS THE ONLY SAFE GPU QUESTION ON THIS BOARD
// ---------------------------------------------------------------------------
// The GPU register window (kgsl-3d0 0x05000000), the GMU (0x0506a000) and the
// dedicated kgsl SMMU (0x05040000) all live in the collapsible gpu_cx/gpu_gx
// power domain (notes/mu-silicium/enchilada.dts:23666-23668, :13967, :593-621).
// Reading any of them before that domain is powered is a bus read to a block
// with no clock: it can hang the CPU, and on this port a hang in early boot is
// indistinguishable from a broken kernel. So "is the GPU up?" cannot be asked by
// reading the GPU.
//
// It CAN be asked of the GPUCC GDSC status registers, and that is all this probe
// reads:
//   CX power state: 0x05091540 (GPUCC 0x05090000 + gds_hw_ctrl 0x1540), BIT(31)
//   GX power state: 0x05091010 (GPUCC 0x05090000 + gdscr 0x100c + 0x4),  BIT(16) up / BIT(15) down
// Both facts come from the upstream clk driver, not from the DTB: gpucc-sdm845.c
// registers gpu_cx_gdsc with .gds_hw_ctrl = 0x1540 and gpu_gx_gdsc with
// .gdscr = 0x100c + POLL_CFG_GDSCR, and gdsc.c:66-71 selects between those two
// fields. The DTB's reg = <0x509106c 0x04> is the CONTROL register, which is why
// reading it as "the status" would be wrong (drivers/NativeAdreno/AdrenoDomainState.h
// records the correction with line numbers).
//
// WHY THESE REGISTERS SURVIVE A COLLAPSED DOMAIN: the GDSC framework polls its
// status register WHILE the domain is transitioning (gdsc.c gdsc_poll_status,
// STATUS_POLL_TIMEOUT_US) -- the register cannot die with the thing it switches.
// GPUCC itself is on the always-on PMIC rails vdd_cx/vdd_mx/vdd_gfx
// (enchilada.dts:6227-6228, :6239), not on the GDSC it controls.
//
// SAFETY IS ENFORCED IN CODE, NOT ASSERTED IN A COMMENT
// -----------------------------------------------------
// Before mapping anything, the probe asks the round-28 predicates whether the
// window it intends to touch is (a) the GPUCC probe window and (b) not one of
// the six forbidden GPU/GMU/SMMU windows. If either answer is wrong the probe
// logs and returns WITHOUT mapping. The mapping is cache-inhibited, hoisted to
// file scope and kept forever (the DWC3 lesson: an inspect()-local
// OSSharedPtr<IOMemoryMap> was destroyed on return and the gadget thread then
// read garbage). There is no write of any kind to any register in this file:
// the only dereference is a volatile uint32_t load.
//
// Stage (enchilada-adreno=<n>, default off / absent = no-op):
//   1  map the GPUCC window, read the two status registers once, decode them
//      with the host-tested predicates, publish one verdict line, stop.
//
// REPLAY: the kernel msgbuf is a 128 KB ring that keeps only the newest bytes
// (measured: it first saturated at 101.2 s / 87.7 s / 58.8 s on three
// single-variable boots), so a verdict logged once at t<8 s is gone before the
// log is collected. The read happens exactly once; the TEXT is cached and
// re-emitted once per second, labelled "(replay t=<tick> attempt=<n>)". A replay
// touches no MMIO, so the single-observation property and the read-only contract
// both hold.
//
// Board facts and their sources are in drivers/NativeAdreno/AdrenoDomainState.h.
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <string.h>

extern "C" {
#include "AdrenoDomainState.h"
#include "AdrenoPowerGate.h"

// Device-proven cache-inhibited kernel mapping of a physical range
// (drivers/ArmCpu/ArmCpu.cpp:207, same use as package/EnchiladaSmmuProbe.cpp:57).
// weak_import: if the ArmCpu archive is not linked this is NULL and we wait.
uint64_t armcpu_map_physical(uint64_t physical, uint64_t length) __attribute__((weak_import));
}

namespace {
uintptr_t gGpuccVa = 0;
unsigned gAttempts = 0;
bool gDone = false;
bool gNotArmed = false;

char gReplay[200];
bool gHaveReplay = false;
unsigned gReplayAttempt = 0;
unsigned gTick = 0;

void setReplay(const char *text) {
    if (!text) return;
    unsigned i = 0;
    for (; text[i] && i + 1 < sizeof gReplay; ++i) gReplay[i] = text[i];
    gReplay[i] = 0;
    gHaveReplay = i != 0;
    gReplayAttempt = gAttempts;
}

// In-domain register read: GPUCC base plus a byte offset the host-tested header
// already bounds against GPUCC_SIZE / REG_STRIDE.
uint32_t rd32(uintptr_t va, uint32_t off) {return *reinterpret_cast<volatile uint32_t *>(va + off);}

// Byte offsets of the two status registers inside GPUCC. Derived, never typed,
// so they cannot drift from the absolute addresses in the header.
const uint32_t kCxOffset = static_cast<uint32_t>(ADRENO_DOMAIN_CX_STATUS - ADRENO_DOMAIN_GPUCC_BASE);  // 0x1540
const uint32_t kGxOffset = static_cast<uint32_t>(ADRENO_DOMAIN_GX_STATUS - ADRENO_DOMAIN_GPUCC_BASE);  // 0x1010

// The single safety gate. Returns 0 only when the exact window we are about to
// map is the GPUCC window and is not one of the forbidden GPU/GMU/SMMU windows.
int gpucc_window_is_safe(void) {
    // (a) the window must be the GPUCC probe window, with the driver's granularity
    if (!adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_GPUCC_BASE)) return 0;
    if (!adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_GPUCC_BASE + ADRENO_DOMAIN_GPUCC_SIZE - 4u)) return 0;
    if (ADRENO_DOMAIN_GPUCC_SIZE % ADRENO_DOMAIN_GPUCC_REG_STRIDE) return 0;
    if (ADRENO_DOMAIN_GPUCC_SIZE < ADRENO_DOMAIN_GPUCC_REG_STRIDE) return 0;
    // (b) both offsets must lie inside the clk driver's own regmap window. The DT
    // node says 0x9000 bytes but gpucc-sdm845.c registers max_register 0x8008, so
    // the last ~4 KiB of the DT window is NOT addressable through the driver --
    // and our two registers (0x1540 / 0x1010) are comfortably inside it.
    if (kCxOffset > ADRENO_DOMAIN_GPUCC_MAX_REGISTER) return 0;
    if (kGxOffset > ADRENO_DOMAIN_GPUCC_MAX_REGISTER) return 0;
    if (kCxOffset % ADRENO_DOMAIN_GPUCC_REG_STRIDE) return 0;
    if (kGxOffset % ADRENO_DOMAIN_GPUCC_REG_STRIDE) return 0;
    // (c) neither register may be an address that dies with the domain
    if (adreno_domain_window_is_forbidden(ADRENO_DOMAIN_CX_STATUS, 4u)) return 0;
    if (adreno_domain_window_is_forbidden(ADRENO_DOMAIN_GX_STATUS, 4u)) return 0;
    // (d) the GPUCC carve-out must be active. This is the load-bearing round-28
    // fix: the GMU DT node declares reg = <0x506a000 0x30000>, which numerically
    // swallows GPUCC 0x5090000-0x5099000, so a naive overlap test rejects the
    // only safe registers on the board. If this ever returns 1 the carve-out has
    // been lost and the probe must not read anything.
    if (adreno_domain_window_is_forbidden(ADRENO_DOMAIN_GPUCC_BASE, ADRENO_DOMAIN_GPUCC_SIZE)) return 0;
    // (e) ... and the board description must still describe a gated GPU at all
    if (!adreno_power_gpu_windows_are_gated()) return 0;
    return 1;
}
}  // namespace

extern "C" void enchilada_adreno_poll(void) {
    gTick++;
    if (gNotArmed) return;
    if (gDone) {
        if (gHaveReplay)
            IOLog("enchilada-adreno: %s (replay t=%u attempt=%u)\n", gReplay, gTick, gReplayAttempt);
        return;             // finished: no further MMIO access, ever
    }
    unsigned stage = 0;
    if (!PE_parse_boot_argn("enchilada-adreno", &stage, sizeof stage) || stage != 1u) {
        gNotArmed = true;   // absent (or an unsupported stage) -> never run
        return;
    }
    // Refuse BEFORE the mapping: an unsafe window is a port bug, not a device state.
    if (!gpucc_window_is_safe()) {
        IOLog("enchilada-adreno: refused base=%llx size=%llx (an unsafe window is a port bug, not a device state)\n",
              (unsigned long long)ADRENO_DOMAIN_GPUCC_BASE, (unsigned long long)ADRENO_DOMAIN_GPUCC_SIZE);
        setReplay("refused-unsafe-window");
        gDone = true;
        return;
    }
    if (!gGpuccVa) {
        if (!&armcpu_map_physical) {
            if (!gAttempts) IOLog("enchilada-adreno: wait-platform\n");
            return;         // retry next 1 Hz tick, no attempt consumed
        }
        const uint64_t va = armcpu_map_physical(ADRENO_DOMAIN_GPUCC_BASE, ADRENO_DOMAIN_GPUCC_SIZE);
        if (!va) {
            if (!gAttempts) {
                IOLog("enchilada-adreno: map-failed %llx+%llx\n",
                      (unsigned long long)ADRENO_DOMAIN_GPUCC_BASE, (unsigned long long)ADRENO_DOMAIN_GPUCC_SIZE);
                setReplay("map-failed");
            }
            if (++gAttempts >= 8u) gDone = true;
            return;
        }
        gGpuccVa = static_cast<uintptr_t>(va);
    }
    gAttempts++;
    // The two observations. Offsets are derived from the absolute addresses in the
    // header so the two cannot drift apart.
    const uint32_t cx = rd32(gGpuccVa, static_cast<uint32_t>(ADRENO_DOMAIN_CX_STATUS - ADRENO_DOMAIN_GPUCC_BASE));
    const uint32_t gx = rd32(gGpuccVa, static_cast<uint32_t>(ADRENO_DOMAIN_GX_STATUS - ADRENO_DOMAIN_GPUCC_BASE));
    // dsb sy: order the observation against the log that reports it.
    __asm__ volatile("dsb sy" ::: "memory");

    const int cxOn = adreno_domain_cx_is_on(cx);
    const int gxOn = adreno_domain_gx_is_on(gx);
    const int gxOff = adreno_domain_gx_is_off(gx);
    // Honest label: GX is only ever powered by the GMU firmware, and gpu_gx_gdsc's
    // power_on is gdsc_gx_do_nothing_enable, so a GX-OFF reading is the EXPECTED
    // steady state of a port that has not booted the GPU -- it is not a fault.
    const char *verdict = (cxOn && gxOn) ? "domain-UP-both"
                        : (cxOn && gxOff) ? "cx-up-gx-down-expected"
                        : (cxOn ? "cx-up-gx-indeterminate" : "cx-DOWN");

    char row[240];
    const struct adreno_power_state live = {};   // nothing powered by this port: steps_left is the whole list
    snprintf(row, sizeof row,
             "rc=0 GPUCC=%llx size=%llx cx=%08x gx=%08x cx_on=%d gx_on=%d gx_off=%d verdict=%s steps_left=%u",
             (unsigned long long)ADRENO_DOMAIN_GPUCC_BASE, (unsigned long long)ADRENO_DOMAIN_GPUCC_SIZE,
             cx, gx, cxOn, gxOn, gxOff, verdict,
             adreno_power_steps_remaining(&live));
    IOLog("enchilada-adreno: %s\n", row);
    setReplay(row);
    gDone = true;   // a completed observation is a finished observation
}
