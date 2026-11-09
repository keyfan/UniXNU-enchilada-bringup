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
    char row[300];
    snprintf(row, sizeof row, "rc=0 %s", n ? line : "format-failed");
    IOLog("enchilada-smmu: %s\n", row);
    setReplay(row);
    // A completed read is a finished observation: stop issuing requests.
    gDone = true;
}
