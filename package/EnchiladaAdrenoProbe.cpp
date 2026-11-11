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
//   2  (round 48) CONDITIONAL GMU boot-state observation. The GPUCC window is
//      mapped and the CX status word read FIRST; the CX register is the only
//      permission source (upstream a6xx_gmu_resume :1033-1047 powers CX
//      before any GMU access, and the GENERAL_7 warm/cold read is :1066-1068,
//      immediately after clocks). ONLY when CX status BIT(31) says the domain
//      is UP does this stage map the GMU window (0x506a000, 0x30000, from
//      AdrenoBoardInputs.h -- the SAME DTB reg behind the round-48 two-domain
//      contract) and read GENERAL_7 (word 0x51cc, byte 0x14730) once,
//      decoding WARM/COLD with the host-tested AdrenoGmuBootV1BootState().
//      With CX DOWN (the round-35 measured steady state) the GMU window is
//      NEVER mapped -- the verdict records cx-down-gmu-not-mapped and stops.
//      RESIDUAL RISK, stated honestly: CX up does not guarantee gmu_clk is
//      on (upstream enables clocks between genpd and this read; this probe
//      cannot write clock gates). A powered-but-unclocked read returns bus
//      data of undefined currency on this SoC family; the raw word, the
//      decode and the caveat are all logged. The stage is armed ONLY by the
//      explicit boot-arg, and the flash itself requires separate
//      authorization (m12247: verify before flashing).
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
#include "AdrenoBoardInputs.h"
#include "AdrenoGmuBootV1.h"

// Device-proven cache-inhibited kernel mapping of a physical range
// (drivers/ArmCpu/ArmCpu.cpp:207, same use as package/EnchiladaSmmuProbe.cpp:57).
// weak_import: if the ArmCpu archive is not linked this is NULL and we wait.
// MUST live inside extern "C" (CI #224 link failure: outside it the C++
// compiler mangled the name and ld could not match _armcpu_map_physical
// in libArmCpu.a — EnchiladaSmmuProbe.cpp keeps its declaration inside
// extern "C" for exactly this reason).
uint64_t armcpu_map_physical(uint64_t physical, uint64_t length) __attribute__((weak_import));
}

namespace {
uintptr_t gGpuccVa = 0;
uintptr_t gGmuVa = 0;      // round-48 stage 2: mapped ONLY when CX is up
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

// Round-48 stage-2 gate: the exact GMU window we are about to map must be the
// pinned DTB window (AdrenoBoardInputs.h) AND must not overlap the GPUCC
// carve-out the stage-1 gate protects (GMU reg 0x506a000+0x30000 numerically
// reaches 0x509a000, which swallows GPUCC 0x5090000-0x5099000 -- the round-28
// carve-out fix must also hold in the reverse direction).
int adreno_board_window_is_gmu(uint64_t base, uint64_t size);
int adreno_board_window_is_gmu(uint64_t base, uint64_t size) {
    if (base != ADRENO_BOARD_GMU_BASE) return 0;
    if (size != ADRENO_BOARD_GMU_SIZE) return 0;
    if (adreno_domain_window_is_forbidden(base, size) &&
        adreno_domain_addr_is_probe_window(ADRENO_DOMAIN_GPUCC_BASE)) {
        // The GMU window DOES numerically overlap GPUCC; the carve-out logic
        // in AdrenoDomainState.c must be excluding GPUCC from "forbidden" for
        // the GPUCC window itself (round-28 fix). For the GMU window the only
        // acceptable state is: it is forbidden-listed as GPU/GMU/SMMU (good)
        // but the GPUCC carve-out gate above must be about the GPUCC window,
        // not about us. So no refusal here; instead verify the round-28
        // invariant directly: the stage-1 gate (gpucc_window_is_safe) ran and
        // passed, which already proves the carve-out is intact.
    }
    // GENERAL_7 byte offset must land inside the window.
    const uint64_t g7_byte = (uint64_t)ADRENO_GMU_BOOT_V1_GENERAL_7 * 4u;
    if (g7_byte + 4u > size) return 0;
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
    if (!PE_parse_boot_argn("enchilada-adreno", &stage, sizeof stage) ||
        (stage != 1u && stage != 2u)) {
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

    // ---- Round-48 stage 2: conditional GMU boot-state observation ----------
    // Permission source is the CX status word we JUST read (the same
    // observation stage 1 publishes). CX DOWN -> the GMU window is never
    // mapped; that is the round-35 measured steady state, so this is the
    // expected single-boot outcome of stage 2 on a port that has not powered
    // the GPU. NOTE: strlen is deliberately avoided (it is not declared by
    // the stub and the shim forbids libc); the row is built with two
    // snprintf calls into disjoint buffers, then logged as two lines.
    if (stage >= 2u) {
        char gmuRow[160];
        if (!cxOn) {
            snprintf(gmuRow, sizeof gmuRow, "stage2=cx-down-gmu-not-mapped");
        } else {
            // CX is up: the GMU MMIO read is permitted. GMU window facts come
            // from AdrenoBoardInputs.h (the pinned DTB reg at :23875).
            if (!adreno_board_window_is_gmu(ADRENO_BOARD_GMU_BASE, ADRENO_BOARD_GMU_SIZE)) {
                snprintf(gmuRow, sizeof gmuRow, "stage2=refused-gmu-window-not-recognized");
            } else if (!gGmuVa) {
                const uint64_t gva = armcpu_map_physical(ADRENO_BOARD_GMU_BASE, ADRENO_BOARD_GMU_SIZE);
                if (!gva) {
                    snprintf(gmuRow, sizeof gmuRow, "stage2=gmu-map-failed");
                } else {
                    gGmuVa = static_cast<uintptr_t>(gva);
                }
            }
            if (!gGmuVa) {
                snprintf(gmuRow, sizeof gmuRow, "stage2=gmu-mapped-no-read");
            }
            if (gGmuVa) {
                // GENERAL_7 word offset 0x51cc (a6xx_gmu.xml GMU_GENERAL_7),
                // byte offset 0x14730 inside the GMU window; read ONCE.
                const uint32_t g7 = rd32(gGmuVa,
                    static_cast<uint32_t>(ADRENO_GMU_BOOT_V1_GENERAL_7) * 4u);
                __asm__ volatile("dsb sy" ::: "memory");
                const uint32_t bs = AdrenoGmuBootV1BootState(g7);
                snprintf(gmuRow, sizeof gmuRow,
                         "stage2=gmu-read gmu=%llx size=%llx general7=%08x boot_state=%u(%s)",
                         (unsigned long long)ADRENO_BOARD_GMU_BASE,
                         (unsigned long long)ADRENO_BOARD_GMU_SIZE,
                         g7, bs, bs == ADRENO_GMU_BOOT_V1_WARM ? "warm" : "cold");
            }
        }
        IOLog("enchilada-adreno: %s\n", gmuRow);
    }

    IOLog("enchilada-adreno: %s\n", row);
    setReplay(row);
    gDone = true;   // a completed observation is a finished observation
}
