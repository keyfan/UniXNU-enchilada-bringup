// SPDX-License-Identifier: BSD-2-Clause-Patent
// A lenient apps_smmu planner for the DWC3 gadget's stream.
//
// drivers/NativeUfs/SmmuTakeover.h plans the *UFS* stream and is deliberately strict: it refuses
// any matching stream-match entry that does not sit on the context bank it picked itself, with
// "conflicting UFS stream mapping" (SmmuTakeover.h:83). For UFS that is right -- a second mapping
// of the same stream means the queue would run through a bank the driver is not programming.
//
// The DWC3 stream is the opposite situation. UEFI brings the controller up in device mode for
// fastboot and leaves one bypass entry for stream 0x740 behind, on UEFI's own context bank; that
// entry is the mapping the gadget is meant to keep using. Applying the UFS rule to it rejects the
// only correct answer, which is exactly what native-dwc3-probe=2 reported on hardware:
//
//   U st=5 rc=0 mode=2 id=5533300a gctl=00102000 dcfg=00080804 dsts=00d3298c alep=0 cap=2 phy=1
//     gdsc=f8222000 smmu=-1/-1
//   U smmu: conflicting UFS stream mapping
//
// So this planner answers a different question: "what does the SMMU already say about stream
// 0x740, and is that usable as it stands?" It adopts a unique bypass entry wherever it lives, and
// only falls back to planning a fresh bank when the stream is not mapped at all. It never writes
// MMIO; the write path stays configureUfsSmmu(), which already knows how to keep a firmware entry
// untouched (p.firmware).
//
// The UFS planner is not modified: NativeUfs keeps its strict rules, and the two callers each get
// the question they actually need answered.
#ifndef NATIVE_DWC3_SMMU_PLAN_H
#define NATIVE_DWC3_SMMU_PLAN_H
#include <stdint.h>
// SmmuTakeover.h has no include guard and defines static helpers, so it must be reached exactly
// once per translation unit: everything that needs it goes through this header.
#include "../NativeUfs/SmmuTakeover.h"

// Plan the DWC3 stream's context bank from a read-only view of the apps_smmu register window.
//
//   stream, mask  the DWC3 stream id (SDM845: 0x740, mask 0) -- mask 0 is an exact match
//   window        size of the mapped SMMU window; the context banks must fit inside it
//   dma, bytes    the DMA range the gadget will use, for the address-width checks
//   read          offset -> register value, no writes
//
// On success the plan carries reuse=true. firmware=true means an entry UEFI left behind is being
// adopted as it stands; firmware=false with reuse=false means a fresh bank was planned and the
// caller must program it. error is set on every refusal, and is a static string.
template <class Read>
static UfsSmmuPlan planDwc3Smmu(uint16_t stream, uint16_t mask, uint64_t window, uint64_t dma, uint64_t bytes, Read read)
{
  UfsSmmuPlan p;
  uint32_t idr0 = read(0x20), idr2 = read(0x28);
  unsigned count = idr0 & 255;
  if (!(idr0 & (1U << 30)) || !(idr2 & (1U << 12))) {
    p.error = "SMMU AArch64 stage 1 unsupported"; return p;
  }
  for (unsigned shift = 0; shift <= 8; shift += 4) {
    unsigned bits = ufsSmmuAddressBits((idr2 >> shift) & 15);
    if (!bits || !bytes || dma >= (1ULL << bits) || bytes > (1ULL << bits) - dma) {
      p.error = "DMA exceeds SMMU address width"; return p;
    }
  }
  uint32_t idr1 = read(0x24);
  unsigned banks = idr1 & 255;
  p.pages = 1U << (((idr1 >> 28) & 7) + 1);
  if ((idr1 >> 31) || !count || p.pages > 128 || banks < 2 || banks > 128 ||
      (p.pages + banks) * 4096ULL > window) {
    p.error = "unsupported SMMU geometry"; return p;
  }
  if (count > 128) count = 128;

  // Pass 1: is the stream already mapped? The gadget inherits whatever UEFI left, so a unique
  // bypass entry anywhere in the table is the answer, not a conflict.
  unsigned matches = 0, matchSlot = 0, matchBank = 0;
  uint32_t matchSmr = 0, matchS2cr = 0;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t smr = read(0x800 + 4 * i);
    if (!(smr & 0x80000000U)) continue;
    uint32_t smrMask = (smr >> 16) & 0x7fff;
    if (((smr ^ stream) & 0xffff & ~(smrMask | mask)) != 0) continue;
    ++matches;
    matchSlot = i;
    matchSmr = smr;
    matchS2cr = read(0xc00 + 4 * i);
    matchBank = matchS2cr & 255;
  }
  if (matches > 1) {
    // Two entries claim this stream: which one the hardware would pick is not ours to guess.
    p.slot = matchSlot; p.smr = matchSmr; p.s2cr = matchS2cr;
    p.error = "ambiguous dwc3 stream mapping"; return p;
  }
  if (matches == 1) {
    p.slot = matchSlot; p.smr = matchSmr; p.s2cr = matchS2cr; p.bank = matchBank;
    if (matchBank >= banks) { p.error = "dwc3 stream bank out of range"; return p; }
    if (((matchS2cr >> 16) & 3) != 0) { p.error = "dwc3 stream mapped with translation on"; return p; }
    unsigned cb = (p.pages + matchBank) * 4096;
    if (read(cb) & 1) { p.error = "dwc3 stream bank has translation enabled"; return p; }
    // A 32-bit bypass bank cannot pass a DMA range above 4 GiB; the real gadget allocation must
    // respect that, so report it instead of adopting a mapping that would fault on first use.
    if (!(read(0x1800 + 4 * matchBank) & 1) && (dma >= (1ULL << 32) || bytes > (1ULL << 32) - dma)) {
      p.error = "dwc3 bypass bank is 32-bit and DMA is above 4 GiB"; return p;
    }
    p.reuse = true;
    p.firmware = true;
    return p;
  }

  // Pass 2: nothing maps this stream. Plan a fresh bypass bank from the top down, with the same
  // rules UFS applies to a bank it allocates: no other valid stream on it, translation off. The
  // group is the last free one, matching the UFS planner's choice. CBAR/CBA2R are not checked here
  // because a fresh bank gets them written; only the translation state has to be clean first.
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    if (attempt >= banks - 1) break;
    unsigned bank = banks - 1 - attempt;
    unsigned slot = 0xffffffffU;
    bool usable = true;
    for (unsigned i = 0; i < count; ++i) {
      uint32_t smr = read(0x800 + 4 * i);
      if (!(smr & 0x80000000U)) {
        slot = i;
        continue;
      }
      if ((read(0xc00 + 4 * i) & 255) == bank) { usable = false; break; }
    }
    if (!usable) continue;
    if (slot == 0xffffffffU) { p.error = "no free stream-match group"; return p; }
    if (read((p.pages + bank) * 4096) & 1) continue;
    p.bank = bank; p.slot = slot; p.reuse = false; p.firmware = false;
    p.smr = ufsSmmuSmr(stream, mask);   // what the caller programs into SMR[slot]
    return p;
  }
  p.error = "no free context bank";
  return p;
}

#endif  // NATIVE_DWC3_SMMU_PLAN_H
