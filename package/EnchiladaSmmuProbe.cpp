// SPDX-License-Identifier: BSD-2-Clause-Patent
// Default-off, READ-ONLY appSMMU survey for enchilada (OnePlus 6, SDM845).
// The decision core lives in drivers/NativeSmmu/SmmuSurvey.c (freestanding C11,
// host-tested); this file is only the thin kernel shim that owns the MMIO
// mapping and publishes one verdict line. It mirrors the DEVICE-VERIFIED
// stage-3/4/7 pattern:
//   * package/BootProgress.c monitor() calls enchilada_smmu_poll() at 1 Hz;
//   * this file defines it STRONGLY inside the -force_load'ed
//     libEnchiladaSmmu.a, so no stub is needed in package/BootProgressStubs.c
//     (that file must stay byte-identical; a weak_import function with a strong
//     definition links fine -- see BootProgress.c:141-142 and
//     BootDiagExport.c:43 armcpu_map_physical, neither of which has a stub);
//   * IOLog("enchilada-smmu: ...") lands in msgbuf.txt (stage4welcome proved
//     the identical channel for "enchilada-touch: ...").
//
// REPLAY (why this re-emits instead of logging once): the kernel msgbuf is a
// 128 KB ring that keeps only the NEWEST bytes. Measured on device across three
// single-variable boots, it first saturated at 101.2 s / 87.7 s / 58.8 s, so a
// verdict logged once at t<8 s is always overwritten before the log is
// collected (work/tmp/GAUGE-DIAGNOSIS-3.md). The touch driver survives because
// it keeps WRITING. So the survey runs exactly once, the resulting text is
// cached, and that same text is re-emitted once per second. A replay never
// touches MMIO, so the single-observation property and the READ-ONLY contract
// both hold; replays are labelled "(replay t=<tick> attempt=<n>)".
//
// READ-ONLY CONTRACT ("先核验再上机"): this probe performs NO store of any kind
// to the SMMU window. The io callback it exposes has only read32 -- there is no
// write member to call -- and SmmuSurvey.c contains no store into the mapping.
// It does not program a context, does not enable a bank, does not touch SMR or
// S2CR, and cannot recover a synchronous bus abort. It is an observation, not a
// translation driver. In particular it does NOT claim the ArmSmmu4K kit is
// linked or that translation is owned by this port.
//
// Stage (enchilada-smmu=<n>, default off / absent = no-op):
//   1  read the SMMUv2 identification registers, derive the slot/bank/global
//      page geometry, scan every stream-match slot, report where the UFS (0x60)
//      and DWC3 (0x740) streams landed and whether they are bypass, translation
//      or fault. ZERO writes.
//
// Board facts (each with a source in the tree) are in the header of
// drivers/NativeSmmu/SmmuSurvey.h.
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <string.h>

extern "C" {
#include "SmmuSurvey.h"
#include "ArmSmmu4K.h"
#include "ArmSmmu4KProgram.h"
#include "ArmSmmu4KStreams.h"

// Device-proven cache-inhibited kernel mapping of a physical range
// (drivers/ArmCpu/ArmCpu.cpp:207, used the same way by
// package/EnchiladaGaugeProbe.cpp:68 and package/EnchiladaBacklightProbe.cpp:78).
// weak_import: if the ArmCpu archive is not linked its address is NULL and we
// wait for the platform instead of dereferencing it.
uint64_t armcpu_map_physical(uint64_t physical, uint64_t length) __attribute__((weak_import));
}

namespace {
// The mapping is hoisted to file scope and kept FOREVER. The DWC3 bring-up bug:
// an inspect()-local OSSharedPtr<IOMemoryMap> was destroyed on return and the
// later thread read garbage (cb=6f6f6c6c "lloo").
uintptr_t gSmmuVa = 0;
unsigned gAttempts = 0;
bool gDone = false;
bool gNotArmed = false;

// Replay state: the cached result text plus the attempt that produced it.
char gReplay[220];
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

uint32_t rd32(uintptr_t va, uint32_t off) {return *reinterpret_cast<volatile uint32_t *>(va + off);}

// The survey core only ever calls this: reads, never writes.
int smmuRead32(void *ctx, uint32_t offset, uint32_t *out) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(ctx);
    if (!base || !out) return 1;
    if (offset + 4u > SMMU_SURVEY_SDM845_SIZE || (offset & 3u)) return 1;
    *out = rd32(base, offset);
    // dsb sy: order the observation against later reads and against the log.
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}

// ---- ArmSmmu4K read-only bridge -------------------------------------------
// The kit takes five callbacks and its prepare() only checks that each is
// non-NULL; whether a given entry point uses one is up to that entry point.
// ArmSmmu4KCheckFaultedStreams is documented read-only ("Read-only preflight of
// global fault policy, target faulting slots and all conflicting live routes")
// and the local replay recorded writes=0, so the three mutating callbacks below
// exist only to satisfy the shape. If the kit ever did call one, it returns
// failure WITHOUT touching MMIO -- the probe can be wrong, but it cannot write
// to the SMMU. That is the "先核验再上机" contract in code, not in a comment.
int kitRead32(void *cookie, size_t at, uint32_t *out) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(cookie);
    if (!base || !out) return 1;
    if (at + 4u > SMMU_SURVEY_SDM845_SIZE || (at & 3u)) return 1;
    *out = rd32(base, static_cast<uint32_t>(at));
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}

int kitRead64(void *cookie, size_t at, uint64_t *out) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(cookie);
    if (!base || !out) return 1;
    if (at + 8u > SMMU_SURVEY_SDM845_SIZE || (at & 7u)) return 1;
    const uint32_t lo = rd32(base, static_cast<uint32_t>(at));
    const uint32_t hi = rd32(base, static_cast<uint32_t>(at) + 4u);
    *out = (static_cast<uint64_t>(hi) << 32) | lo;
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}

// Counted by these two functions alone: any non-zero value published in the
// verdict's w= field is the kit actually reaching for a store.
unsigned gKitWriteCalls = 0;

int kitWrite32(void *, size_t, uint32_t) {++gKitWriteCalls; return 1;}   // counts, never touches MMIO
int kitWrite64(void *, size_t, uint64_t) {++gKitWriteCalls; return 1;}   // counts, never touches MMIO
int kitBarrier(void *) {
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}

ArmSmmu4KIo gKitIo = {nullptr, SMMU_SURVEY_SDM845_SIZE, kitRead32, kitRead64,
                      kitWrite32, kitWrite64, kitBarrier};

}  // namespace

extern "C" void enchilada_smmu_poll(void) {
    gTick++;
    if (gNotArmed) return;
    if (gDone) {
        if (gHaveReplay)
            IOLog("enchilada-smmu: %s (replay t=%u attempt=%u)\n",
                  gReplay, gTick, gReplayAttempt);
        return;             // finished: no further MMIO access, ever
    }
    unsigned stage = 0;
    if (!PE_parse_boot_argn("enchilada-smmu", &stage, sizeof stage) || stage < 1u || stage > 1u) {
        gNotArmed = true;   // absent (or an unsupported stage) -> never run
        return;
    }
    if (!gSmmuVa) {
        if (!&armcpu_map_physical) {
            if (!gAttempts) IOLog("enchilada-smmu: wait-platform\n");
            return;         // retry next 1 Hz tick, no attempt consumed
        }
        const uint64_t va = armcpu_map_physical(SMMU_SURVEY_SDM845_BASE, SMMU_SURVEY_SDM845_SIZE);
        if (!va) {
            if (!gAttempts) {
                IOLog("enchilada-smmu: map-failed %llx+%llx\n",
                      (unsigned long long)SMMU_SURVEY_SDM845_BASE,
                      (unsigned long long)SMMU_SURVEY_SDM845_SIZE);
                setReplay("map-failed");
            }
            if (++gAttempts >= 8u) gDone = true;
            return;
        }
        gSmmuVa = static_cast<uintptr_t>(va);
    }
    gAttempts++;
    struct smmu_survey_io io = {reinterpret_cast<void *>(gSmmuVa), smmuRead32};
    struct smmu_survey_geometry geometry = {};
    struct smmu_survey_result result = {};
    // The UFS stream is a BOARD property, not a SoC constant: NativeUfs.cpp:201-202
    // parses exactly these two boot args, and a survey that assumes a different
    // pair reports ufs=none on a board where the mapping is present (that is what
    // the first device run of this file did with sheng's 0x60). Read the same args
    // so the two drivers cannot disagree.
    struct smmu_survey_targets targets = smmu_survey_default_targets();
    {
        uint32_t value = 0;
        if (PE_parse_boot_argn("native-ufs-smmu-stream", &value, sizeof value))
            targets.ufs.sid = static_cast<uint16_t>(value & 0x7fffu);
        if (PE_parse_boot_argn("native-ufs-smmu-mask", &value, sizeof value))
            targets.ufs.mask = static_cast<uint16_t>(value & 0x7fffu);
    }
    const int geo = smmu_survey_geometry_read(&io, &geometry);
    if (geo != kSmmuSurveyOk) {
        char row[160];
        snprintf(row, sizeof row, "GEOM rc=%d id0=%08x id1=%08x id2=%08x scr0=%08x",
                 geo, geometry.id0, geometry.id1, geometry.id2, geometry.scr0);
        IOLog("enchilada-smmu: %s\n", row);
        setReplay(row);
        if (geo == kSmmuSurveyUnsupported || gAttempts >= 8u) gDone = true;
        return;
    }
    const int scan = smmu_survey_scan(&io, &geometry, &targets, &result);
    if (scan != kSmmuSurveyOk) {
        char row[160];
        snprintf(row, sizeof row, "SCAN rc=%d slots=%u valid=%u reads=%u",
                 scan, geometry.slots, result.valid, result.reads);
        IOLog("enchilada-smmu: %s\n", row);
        setReplay(row);
        if (gAttempts >= 8u) gDone = true;
        return;
    }
    // One buffer holds the whole verdict, so the IOLog and every replay carry
    // the same machine-parsable text that work/tmp/analyze_smmu*_device_logs.py
    // parses (`SS slots=...` prefix).
    char line[260];
    const size_t n = smmu_survey_format(&geometry, &result, line, sizeof line);

    // ---- ArmSmmu4K preflight ------------------------------------------------
    // Until this build the kit was never linked, so the port could only state
    // hand-rolled facts. This asks the upstream translator the question the
    // hand-rolled scan cannot answer: "are these streams in a state where a
    // stage-1 context could legally be programmed at all?" Its documented
    // precondition is that the target S2CRs already FAULT; a board running
    // entirely on firmware bypass must answer ArmSmmu4KBusy, which is a real
    // gate on the next step rather than a failure of this one.
    //
    // `w` counts the mutating callbacks the kit actually invoked. It must stay
    // 0: that number, published on device, is the evidence for the READ-ONLY
    // claim instead of the claim asserting itself.
    unsigned kitWrites = 0;
    struct kit_preflight {
        int ufs, usb;
        unsigned ufs_reads, usb_reads, ufs_verified, usb_verified;
        unsigned ufs_faulted, usb_faulted;
        uint32_t kit_id0;
    } pre = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (gSmmuVa) {
        gKitIo.cookie = reinterpret_cast<void *>(gSmmuVa);
        if (result.ufs.found) {
            ArmSmmu4KStream s = {result.ufs.slot, result.ufs.sid};
            ArmSmmu4KStreamResult r = {};
            pre.ufs = static_cast<int>(ArmSmmu4KCheckFaultedStreams(&gKitIo, &s, 1u, result.ufs.bank, &r));
            pre.ufs_reads = r.reads;
            pre.ufs_verified = r.streams_verified;
            pre.ufs_faulted = r.streams_faulted;
            pre.kit_id0 = r.id0;
        }
        if (result.usb.found) {
            ArmSmmu4KStream s = {result.usb.slot, result.usb.sid};
            ArmSmmu4KStreamResult r = {};
            pre.usb = static_cast<int>(ArmSmmu4KCheckFaultedStreams(&gKitIo, &s, 1u, result.usb.bank, &r));
            pre.usb_reads = r.reads;
            pre.usb_verified = r.streams_verified;
            pre.usb_faulted = r.streams_faulted;
            if (!pre.kit_id0) pre.kit_id0 = r.id0;
        }
        // Counted by the bridge callbacks themselves (gKitWriteCalls), not
        // asserted here: a non-zero w= in the verdict is a fact, not a claim.
        kitWrites = gKitWriteCalls;
    }

    char row[420];
    snprintf(row, sizeof row,
             "rc=0 %s | K pre=%d/%d r=%u/%u w=%u v=%u/%u f=%u/%u id0=%08x",
             n ? line : "format-failed",
             pre.ufs, pre.usb,
             pre.ufs_reads, pre.usb_reads, kitWrites,
             pre.ufs_verified, pre.usb_verified,
             pre.ufs_faulted, pre.usb_faulted,
             pre.kit_id0);
    IOLog("enchilada-smmu: %s\n", row);
    setReplay(row);
    // A completed read is a finished observation: stop issuing requests.
    gDone = true;
}
