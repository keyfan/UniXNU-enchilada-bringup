// SPDX-License-Identifier: BSD-2-Clause-Patent
// Read-only apps_smmu (SMMUv2) survey core for enchilada (OnePlus 6, SDM845).
//
// WHY THIS EXISTS. porting.toml lists ArmSmmu4K as the memory component but the
// kit was never linked, so the memory area could not advance past "implemented".
// The first honest step is NOT to create a stage-1 context (that writes MMIO and
// enables a bank); it is to link the kit and let it OBSERVE the live SMMU that
// UEFI handed over. This file is the host-testable decision layer for that
// observation: it owns no MMIO and performs no writes, it only interprets
// register values that a caller supplies through smmu_survey_io.
//
// The register geometry and the stream-match rule are taken from two
// device-proven sources in this tree, not from memory:
//   drivers/NativeUfs/NativeUfs.cpp:862-901  (inspectSmmu, native-ufs-probe=7)
//       sCR0 0x0, IDR0 0x20, IDR1 0x24, sGFSR 0x48, SMR(n) 0x800+4n, S2CR(n) 0xc00+4n
//   drivers/NativeUfs/NativeUfs.cpp:903-944  (inspectContextBanks, probe=8)
//       banks = IDR1[7:0], globalPages = 1 << (IDR1[30:28] + 1)
//   drivers/NativeDwc3/Dwc3SmmuPlan.h:44-129 (lenient planner)
//       slots = IDR0[7:0]; IDR1[31] => 64K register geometry (refused, never guessed)
//
// A 64K-stride SMMU is refused (SMMU_SURVEY_UNSUPPORTED) because
// drivers/NativeUfs/tests/SmmuTakeover.cpp:33 pins exactly that rule:
//   r[0x24/4]|=0x80000000; assert(plan().error); // Never guess 64K register geometry.
//
// Nothing here allocates, maps, writes, or depends on libc: the freestanding
// build is checked with -ffreestanding and no <stdio.h>.
#ifndef ENCHILADA_SMMU_SURVEY_H
#define ENCHILADA_SMMU_SURVEY_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// apps_smmu@15000000, size 0x80000. Three independent sources in this tree:
//   drivers/NativeDwc3/Dwc3Registers.h:35-36 kAppsSmmuBase/kAppsSmmuSize
//   drivers/NativeUfs/NativeUfs.cpp:432,869,909  mapPhys(0x15000000ULL, ...)
//   drivers/NativeUfs/RootCommands.h:20,131,136  read32(gHost.smmu, 0x48)
#define SMMU_SURVEY_SDM845_BASE 0x15000000ull
#define SMMU_SURVEY_SDM845_SIZE 0x80000ull

// Stream-match window the firmware already programmed, per the device-verified
// console206 line "G smmu slot=6 bank=4 plan=80000740/00000004" (DWC3) and the
// UFS default in NativeUfs.cpp:863 (stream 0x60).
#define SMMU_SURVEY_UFS_STREAM 0x0060u
#define SMMU_SURVEY_USB_STREAM 0x0740u

#define SMMU_SURVEY_MAX_SLOTS 128u

// SMMUv2 S2CR[17:16] memory-access type.
enum smmu_survey_type {
    kSmmuSurveyBypass = 0,       // S2CR type 0: address passes through untranslated
    kSmmuSurveyTranslation = 1,  // stage-2 translation
    kSmmuSurveyFault = 2,        // all accesses fault (the state a lease expects)
    kSmmuSurveyReserved = 3
};

// Status codes. 0 is success; every negative value is a distinct refusal so the
// kernel log distinguishes "the bus did not answer" from "the geometry is one we
// refuse to interpret".
enum smmu_survey_status {
    kSmmuSurveyOk = 0,
    kSmmuSurveyNull = -1,        // a NULL argument
    kSmmuSurveyIo = -2,          // the caller's read callback failed
    kSmmuSurveyUnsupported = -3  // geometry this survey refuses to guess
};

// Read one 32-bit SMMU register. The callback must return 0 on success; any
// nonzero value is reported as kSmmuSurveyIo.
struct smmu_survey_io {
    void *ctx;
    int (*read32)(void *ctx, uint32_t offset, uint32_t *out);
};

struct smmu_survey_geometry {
    uint32_t id0, id1, id2, scr0, gfsr;
    unsigned slots;   // IDR0[7:0]  -- number of stream-match registers
    unsigned banks;   // IDR1[7:0]  -- number of context banks
    unsigned pages;   // 1 << (IDR1[30:28] + 1) -- global address space pages
    unsigned stride;  // 4096 always: a 64K geometry is refused, never guessed
    unsigned exids;   // IDR0[8]
};

struct smmu_survey_stream {
    uint16_t sid;
    int found;        // 1 when a valid SMR matches sid
    unsigned slot;    // 0 when not found
    unsigned bank;    // S2CR[7:0]
    uint32_t smr, s2cr;
    unsigned type;    // S2CR[17:16], see smmu_survey_type
    unsigned translated; // 1 when S2CR[17:16] != 0
};

struct smmu_survey_result {
    unsigned valid;                 // slots whose SMR[31] is set
    unsigned type_count[4];         // histogram over valid slots
    unsigned first_free;            // first SMR[31]==0 slot, or 0xffffu
    unsigned reads;                 // register reads performed
    struct smmu_survey_stream ufs;  // SMMU_SURVEY_UFS_STREAM
    struct smmu_survey_stream usb;  // SMMU_SURVEY_USB_STREAM
};

// Read IDR0/IDR1/IDR2/sCR0/sGFSR and derive the register geometry.
// Returns kSmmuSurveyUnsupported when IDR1[31] is set (64K stride),
// when either the slot or bank count is zero, or when the banks would not fit
// in SMMU_SURVEY_SDM845_SIZE at the derived geometry.
int smmu_survey_geometry_read(const struct smmu_survey_io *io,
                              struct smmu_survey_geometry *out);

// Scan every stream-match register once, classify the valid ones, and locate the
// UFS and USB streams. Strictly read-only. `out` is fully initialised on every
// return, including refusals, so a caller may always format it.
int smmu_survey_scan(const struct smmu_survey_io *io,
                     const struct smmu_survey_geometry *geometry,
                     struct smmu_survey_result *out);

// One machine-parsable line, NUL-terminated, never longer than cap-1.
// Returns the number of characters written, excluding the NUL.
size_t smmu_survey_format(const struct smmu_survey_geometry *geometry,
                          const struct smmu_survey_result *result,
                          char *buffer, size_t cap);

// "bypass" / "translation" / "fault" / "reserved".
const char *smmu_survey_type_name(unsigned type);

#ifdef __cplusplus
}
#endif
#endif  // ENCHILADA_SMMU_SURVEY_H
