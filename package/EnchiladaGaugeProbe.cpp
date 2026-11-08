// SPDX-License-Identifier: BSD-2-Clause-Patent
// Default-off, READ-ONLY bq27xxx fuel-gauge probe for enchilada (OnePlus 6,
// SDM845). The pure decision core lives in drivers/NativeBattery/BqGaugeProbe.c
// (freestanding C11, host-tested); this file is only the thin kernel shim that
// owns MMIO and the GENI I2C transport and publishes ONE verdict line over the
// USB serial console. It mirrors the DEVICE-VERIFIED stage-3/4 pattern:
//   * package/BootProgress.c monitor() calls enchilada_gauge_poll() at 1 Hz;
//   * this file defines it STRONGLY, in the -force_load'ed libEnchiladaGauge.a
//     (BootProgressStubs.c/.symbols are deliberately left byte-identical: a
//     weak_import function with a strong definition needs no stub -- see
//     BootProgress.c:141-142 boot_progress_exit_fault/reason and
//     BootDiagExport.c:43 armcpu_map_physical, neither of which has a stub);
//   * IOLog("enchilada-gauge: ...") lands in msgbuf.txt (stage4welcome proved
//     the identical "enchilada-touch: ..." channel).
//
// REPLAY (why this file re-emits instead of logging once): the kernel's msgbuf
// is a 128 KB ring that keeps only the NEWEST bytes. Measured on device, three
// single-variable boots (enchilada-deadline=900/240/60) first saturated it at
// 101.2 s / 87.7 s / 58.8 s -- dominated by userland spew at 1500-3000 B/s --
// so a verdict logged once at t<8 s is ALWAYS overwritten before the log is
// collected, and the probe's output was structurally unobservable (three
// INCONCLUSIVE runs, work/tmp/GAUGE-DIAGNOSIS-3.md). The touch driver survives
// every boot precisely because it keeps WRITING. So this shim keeps the same
// single bus transaction and the same single hardware measurement, caches the
// resulting text, and re-emits that cached text once per second. The newest copy
// is therefore always inside the ring. A replay is labelled `(replay t=<tick>)`
// and never re-reads the gauge, so the READ-ONLY contract and the "one physical
// measurement" property are both preserved -- the log distinguishes the original
// attempt number from the re-emissions.
//
// READ-ONLY CONTRACT ("先核验再上机"): the ONLY bytes emitted to the fuel gauge
// on the I2C bus are the register-address bytes of standard-command READS --
// geni_i2c_write_read() sends [START][0x55+W][reg][Repeated START][0x55+R][2]
// exactly like Linux bq27xxx_battery_i2c_read(). There is NO gauge register
// write, no CONTROL/CONFIG/unseal, no data-flash update. Board bring-up only
// writes GCC clock votes (additive, never un-voted), the two TLMM pin muxes and
// the GENI master config -- the same device-verified sequence as the touch
// stage 2 enable() at enchilada-touch=2.
//
// Two-source MODEL CONFLICT (honestly unresolved): vendor DTB says
// ti,bq27541-battery (bq27541_regs), mainline says ti,bq27411 (bq27421_regs).
// The core READS BOTH maps and scores each field by physical plausibility; the
// device log empirically resolves which chip is actually soldered.
//
// Stages (enchilada-gauge=<n>, default off / absent = no-op):
//   1  survey only: read GCC vote + QUP wrapper1 S2/M/S_AHB branch + DFS + the
//      two TLMM pins, log GA1. ZERO writes of any kind, ZERO SE access.
//   2  survey, then vote the SE10 clocks (BIT20|21|24, NOT touch's BIT26 S4),
//      mux GPIO55/56 to qup10, GENI init at 100 kHz, and run the 12 read-only
//      standard-command reads; log GA2 + the BG verdict.
//
// Board facts (two independent sources each) are in EnchiladaGaugeBoard.h.
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <string.h>

extern "C" {
#include "GeniI2c.h"
#include "BqFuelGauge.h"
#include "BqGaugeProbe.h"
#include "EnchiladaGaugeBoard.h"

// Device-proven cache-inhibited kernel mapping of a physical range
// (drivers/ArmCpu/ArmCpu.cpp:207, used the same way by
// package/EnchiladaBacklightProbe.cpp:78 and package/BootDiagExport.c:43).
// weak_import: if the ArmCpu archive is not linked its address is NULL and we
// wait for the platform instead of dereferencing it.
uint64_t armcpu_map_physical(uint64_t physical, uint64_t length) __attribute__((weak_import));
}

namespace {
// All mapped VAs are hoisted to file scope and kept FOREVER. The DWC3 bring-up
// bug: an inspect()-local OSSharedPtr<IOMemoryMap> was destroyed on return and
// the later thread read garbage (cb=6f6f6c6c "lloo"). armcpu_map_physical is
// intentionally never released.
uintptr_t gGccVoteVa = 0, gGccBranchVa = 0, gWrapperVa = 0, gSeVa = 0;
uintptr_t gPinSdaVa = 0, gPinSclVa = 0;
struct geni_i2c gI2c = {};
unsigned gAttempts = 0;
bool gDone = false;
bool gNotArmed = false;

// Replay state: the cached result text plus the attempt that produced it. The
// same text is re-emitted every 1 Hz tick so the ring always holds a recent copy
// (see the REPLAY note above). Zero extra bus traffic: nothing here touches MMIO.
char gReplay[200];
bool gHaveReplay = false;
unsigned gReplayAttempt = 0;
unsigned gTick = 0;

// Copies 'text' into the replay buffer (bounded, always NUL-terminated).
void setReplay(const char *text) {
    if (!text) return;
    unsigned i = 0;
    for (; text[i] && i + 1 < sizeof gReplay; ++i) gReplay[i] = text[i];
    gReplay[i] = 0;
    gHaveReplay = i != 0;
    gReplayAttempt = gAttempts;
}

uint32_t rd32(uintptr_t va, uint32_t off) {return *reinterpret_cast<volatile uint32_t *>(va + off);}
void wr32(uintptr_t va, uint32_t off, uint32_t v) {
    *reinterpret_cast<volatile uint32_t *>(va + off) = v;
    __asm__ volatile("dsb sy" ::: "memory");
}
// Architectural-counter spin: NO XNU IODelay/WFE (RootCommands.h:22 records a
// WFE-based poll that never returned on enchilada). Used for the GENI 1 us
// polls and the bounded clock-halt wait.
void spinUs(unsigned us) {
    uint64_t frequency = 0, start = 0, now = 0;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(start));
    const uint64_t ticks = (frequency ? frequency : 19200000ULL) * us / 1000000ULL + 1ULL;
    do {__asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(now));} while (now - start < ticks);
}
uint32_t ioRd(void *ctx, uint32_t reg) {return rd32(reinterpret_cast<uintptr_t>(ctx), reg);}
void ioWr(void *ctx, uint32_t reg, uint32_t v) {wr32(reinterpret_cast<uintptr_t>(ctx), reg, v);}
void ioDelayUs(void *, unsigned us) {spinUs(us);}

uint64_t mapOnce(uintptr_t *slot, uint64_t phys, uint64_t len) {
    if (*slot) return *slot;
    if (!&armcpu_map_physical) return 0;
    uint64_t va = armcpu_map_physical(phys, len);
    if (va) *slot = static_cast<uintptr_t>(va);
    return va;
}

// The ONE bus operation of the whole probe: a register-address byte, a repeated
// START, and a 2-byte read. Never a data write to the gauge.
int gaugeRead(void *ctx, uint8_t addr, uint8_t reg, uint8_t *out, size_t len) {
    struct geni_i2c *g = reinterpret_cast<struct geni_i2c *>(ctx);
    return geni_i2c_write_read(g, addr, &reg, 1, out, len);
}

// Read-only survey: GCC vote/branch pages + TLMM pins are always-safe APB regs.
// Returns the 19.2 MHz DFS level (or -1) in *xoLevel and publishes GA1.
bool survey(int *xoLevel, uint32_t *vote) {
    if (!mapOnce(&gGccVoteVa, GB_GCC_BASE + 0x52000ULL, 0x1000ULL) ||
        !mapOnce(&gGccBranchVa, GB_GCC_BASE + 0x18000ULL, 0x1000ULL) ||
        !mapOnce(&gPinSdaVa, GB_TLMM_BASE + gb_pin_offset(GB_GPIO_SDA), 0x1000ULL) ||
        !mapOnce(&gPinSclVa, GB_TLMM_BASE + gb_pin_offset(GB_GPIO_SCL), 0x1000ULL)) {
        IOLog("enchilada-gauge: GA1 map-failed\n");
        setReplay("GA1 map-failed");
        return false;
    }
    const uint32_t v = rd32(gGccVoteVa, GB_GCC_VOTE & 0xfffu);
    if (vote) *vote = v;
    const uint32_t haltM = rd32(gGccBranchVa, GB_HALT_M_AHB & 0xfffu);
    const uint32_t haltS = rd32(gGccBranchVa, GB_HALT_S_AHB & 0xfffu);
    const uint32_t haltS2 = rd32(gGccBranchVa, GB_HALT_S2 & 0xfffu);
    uint32_t dfsr = rd32(gGccBranchVa, (GB_S2_RCG + GB_RCG_DFSR) & 0xfffu);
    uint32_t cfgNow = rd32(gGccBranchVa, (GB_S2_RCG + GB_RCG_CFG) & 0xfffu);
    uint32_t perfCfg[GB_DFS_LEVELS], perfM[GB_DFS_LEVELS], perfN[GB_DFS_LEVELS];
    for (unsigned l = 0; l < GB_DFS_LEVELS; l++) {
        perfCfg[l] = rd32(gGccBranchVa, (GB_S2_RCG + GB_RCG_PERF_CFG(l)) & 0xfffu);
        perfM[l] = rd32(gGccBranchVa, (GB_S2_RCG + GB_RCG_PERF_M(l)) & 0xfffu);
        perfN[l] = rd32(gGccBranchVa, (GB_S2_RCG + GB_RCG_PERF_N(l)) & 0xfffu);
    }
    int xoL = gb_xo_level(dfsr, perfCfg, perfM, perfN, GB_DFS_LEVELS, cfgNow);
    if (xoLevel) *xoLevel = xoL;
    const uint32_t ctlSda = rd32(gPinSdaVa, 0), ctlScl = rd32(gPinSclVa, 0);
    // Formatted once into a buffer so the IOLog text and the 1 Hz replay are the
    // SAME bytes (snprintf is already used by the device-verified
    // package/EnchiladaCpuFreq.cpp with these exact headers).
    char row[200];
    snprintf(row, sizeof row,
             "GA1 vote=%08x m=%08x s=%08x s2=%08x dfs=%08x cfg=%08x xoL=%d sda=%03x scl=%03x",
             v, haltM, haltS, haltS2, dfsr, cfgNow, xoL, ctlSda & 0x3ffu, ctlScl & 0x3ffu);
    IOLog("enchilada-gauge: %s\n", row);
    setReplay(row);
    return true;
}

// Stage 2: enable SE10 exactly as the device-verified touch stage 2 does, but
// with the S2 (BIT24) branch and qup10 pins. No gauge register writes.
bool enable(int xoLevel, uint32_t vote) {
    // Shared, additive vote; nothing is ever un-voted (touch enable() :349).
    wr32(gGccVoteVa, GB_GCC_VOTE & 0xfffu, rd32(gGccVoteVa, GB_GCC_VOTE & 0xfffu) | GB_VOTE_ALL);
    bool on = false;
    unsigned waitedUs = 0;
    for (; waitedUs < 20000u && !on; waitedUs += 10u) {
        on = gb_branch_running(rd32(gGccBranchVa, GB_HALT_M_AHB & 0xfffu)) &&
             gb_branch_running(rd32(gGccBranchVa, GB_HALT_S_AHB & 0xfffu)) &&
             !(rd32(gGccBranchVa, GB_HALT_S2 & 0xfffu) & GB_CBCR_CLK_OFF);
        if (!on) spinUs(10);
    }
    if (!on) {
        char row[120];
        snprintf(row, sizeof row, "GA2 CLOCKS OFF vote=%08x %uus", vote, waitedUs);
        IOLog("enchilada-gauge: %s\n", row);
        setReplay(row);
        return false;
    }
    // GPIO55/56 -> qup10 function 1, 2 mA, no bias (qup_i2c10_default).
    const uint32_t cfg = gb_pin_cfg(GB_FUNC_QUP10, 2, GB_PULL_NONE, 0);
    wr32(gPinSdaVa, 0, cfg);
    wr32(gPinSclVa, 0, cfg);
    if (!mapOnce(&gWrapperVa, GB_QUP_WRAPPER_BASE, 0x1000ULL) ||
        !mapOnce(&gSeVa, GB_SE_BASE, GB_SE_SIZE)) {
        IOLog("enchilada-gauge: GA2 se-map-failed\n");
        setReplay("GA2 se-map-failed");
        return false;
    }
    gI2c.se = geni_i2c_io{reinterpret_cast<void *>(gSeVa), ioRd, ioWr, ioDelayUs};
    gI2c.wrapper = geni_i2c_io{reinterpret_cast<void *>(gWrapperVa), ioRd, ioWr, ioDelayUs};
    gI2c.poll_limit = 20000;
    int r = geni_i2c_init(&gI2c);                       // clears ready
    if (r) {
        char row[96];
        snprintf(row, sizeof row, "GA2 init=%d", r);
        IOLog("enchilada-gauge: %s\n", row);
        setReplay(row);
        return false;
    }
    // geni_i2c_init() sets ready=0; set_clock() must follow to set ready=1.
    const unsigned level = xoLevel >= 0 ? static_cast<unsigned>(xoLevel) : 0u;
    r = geni_i2c_set_clock(&gI2c, level, GB_TCXO_HZ, GB_I2C_HZ);
    if (r) {
        char row[96];
        snprintf(row, sizeof row, "GA2 clk=%d xoL=%d", r, xoLevel);
        IOLog("enchilada-gauge: %s\n", row);
        setReplay(row);
        return false;
    }
    char row[120];
    snprintf(row, sizeof row, "GA2 init=0 clk=0 xoL=%d fw=%04x hz=%u",
             xoLevel, gI2c.info.fw_revision & 0xffffu, GB_I2C_HZ);
    IOLog("enchilada-gauge: %s\n", row);
    setReplay(row);     // last good row: a later read failure still replays this
    return true;
}
}  // namespace

// Called once a second by package/BootProgress.c monitor(). Idempotent and
// bounded (<= 8 attempts): the first ticks may precede the ArmCpu mapping or
// the IOKit platform. Default-off; enchilada-gauge=1 survey, =2 survey+read.
//
// After a result is latched (gDone) the cached text is RE-EMITTED each tick with
// an explicit `(replay t=<tick>)` marker: the msgbuf ring is only 128 KB and
// always wraps within ~60-100 s, so a single early line is unobservable. Replay
// performs no hardware access -- the measurement happened exactly once.
extern "C" void enchilada_gauge_poll(void) {
    gTick++;
    if (gNotArmed) return;
    if (gDone) {
        if (gHaveReplay)
            IOLog("enchilada-gauge: %s (replay t=%u attempt=%u)\n",
                  gReplay, gTick, gReplayAttempt);
        return;             // finished: no further bus traffic, ever
    }
    unsigned stage = 0;
    if (!PE_parse_boot_argn("enchilada-gauge", &stage, sizeof stage) || stage < 1u || stage > 2u) {
        gNotArmed = true;   // absent (or invalid) -> never run this boot
        return;
    }
    if (!&armcpu_map_physical) {
        if (!gAttempts) IOLog("enchilada-gauge: wait-platform\n");
        return;             // retry next 1 Hz tick, no attempt consumed
    }
    gAttempts++;
    int xoLevel = -1;
    uint32_t vote = 0;
    if (!survey(&xoLevel, &vote)) {
        if (gAttempts >= 8u) gDone = true;
        return;
    }
    if (stage == 1u) {gDone = true; return;}   // survey-only boot, zero writes

    if (!enable(xoLevel, vote)) {
        if (gAttempts >= 8u) gDone = true;
        return;
    }
    struct bq_probe_io io = {&gI2c, gaugeRead};
    struct bq_probe_result res = {};
    const int rc = bq_probe_run(&io, &res);
    char line[200];
    (void)bq_probe_format(&res, line, sizeof line);
    // One buffer holds the full verdict INCLUDING rc, so the IOLog and every
    // replay carry the same machine-parsable `rc=...` prefix that
    // work/tmp/analyze_gauge*_device_logs.py expects.
    char row[240];
    snprintf(row, sizeof row, "rc=%d %s", rc, line);
    IOLog("enchilada-gauge: %s\n", row);
    setReplay(row);
    // A clean read (any ACK) is a finished verification; a total no-ACK is
    // retried a few 1 Hz ticks in case the gauge was still starting.
    if (rc == 0 || gAttempts >= 8u) gDone = true;
}
