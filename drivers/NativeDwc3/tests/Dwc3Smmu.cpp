// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test: the read-only apps_smmu plan the DWC3 probe (native-dwc3-probe=2) computes for the
// dwc3 stream. This test pins the DWC3 stream id (0x740, mask 0) and window (apps_smmu 0x80000) so
// a regression in either the planner or the DWC3 constants is caught offline.
//
// The planner is deliberately more lenient than the UFS one (drivers/NativeUfs/SmmuTakeover.h):
// UEFI brings the controller up in device mode for fastboot and leaves a bypass entry for this
// stream on a context bank of its own, and inheriting that entry is the whole point. On hardware
// the strict UFS rule reported "conflicting UFS stream mapping" and lost the mapping the gadget
// was supposed to use. The cases below pin the lenient answers, including the two that must still
// be refused: an ambiguous table, and a bank with translation turned on.
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <array>
#include "../Dwc3SmmuPlan.h"
#include "../Dwc3Registers.h"

int main()
{
  // A self-consistent apps_smmu register image for the planner: IDR0 (0x20) 128 stream-match
  // groups, IDR1 (0x24) 64 context banks and a 64-page global address space (so (pages + banks) *
  // 4KiB == the 0x80000 window), IDR2 (0x28) 36-bit addressing, AArch64 stage-1 supported. These
  // are a representative image chosen to fit the real 0x80000 apps_smmu window, not SDM845's
  // measured IDR words; the point is to exercise the planner against the DWC3 stream id and
  // window.
  std::array<uint32_t, 0x80000 / 4> r{};
  auto reset = [&] { r.fill(0); r[0x20 / 4] = 0x40000080; r[0x24 / 4] = 0x50000040; r[0x28 / 4] = 0x5111; };
  auto plan = [&] {
    return planDwc3Smmu(kDwc3SmmuStream, kDwc3SmmuMask, kAppsSmmuSize, 0x80000000ull, 0x10000ull,
                        [&](unsigned at) { return r.at(at / 4); });
  };
  // VA64 on for every bank, the way UEFI leaves a bypass bank it expects the OS to keep using.
  auto va64All = [&] { for (unsigned b = 0; b < 64; ++b) r[(0x1800 + 4 * b) / 4] = 1; };

  // A clean SMMU: nothing maps the stream, so a fresh bypass bank is planned from the top down.
  reset();
  va64All();
  UfsSmmuPlan p = plan();
  assert(!p.error && !p.reuse && !p.firmware && p.slot == 127 && p.bank == 63);

  // The SMR we would program must carry the exact stream id with no mask.
  assert(ufsSmmuSmr(0x740, 0) == 0x80000740u);

  // UEFI's own entry for our stream, on UEFI's own bank (bank 1, not the top one): adopted as it
  // stands. This is the case the strict UFS planner refused on hardware.
  reset();
  va64All();
  r[(0x800 + 4 * 110) / 4] = ufsSmmuSmr(kDwc3SmmuStream, kDwc3SmmuMask);   // 0x80000740
  r[(0xc00 + 4 * 110) / 4] = 1;                                            // bypass, bank 1
  p = plan();
  assert(!p.error && p.reuse && p.firmware && p.slot == 110 && p.bank == 1);

  // Our stream on the top bank: still adopted, just at the bank it is on.
  reset();
  va64All();
  r[(0x800 + 4 * 5) / 4] = ufsSmmuSmr(kDwc3SmmuStream, kDwc3SmmuMask);
  r[(0xc00 + 4 * 5) / 4] = 63;
  p = plan();
  assert(!p.error && p.reuse && p.firmware && p.slot == 5 && p.bank == 63);

  // Two entries claiming the stream: which one the hardware picks is not ours to guess.
  reset();
  va64All();
  r[(0x800 + 4 * 5) / 4] = ufsSmmuSmr(kDwc3SmmuStream, kDwc3SmmuMask);
  r[(0xc00 + 4 * 5) / 4] = 1;
  r[(0x800 + 4 * 6) / 4] = ufsSmmuSmr(kDwc3SmmuStream, kDwc3SmmuMask);
  r[(0xc00 + 4 * 6) / 4] = 2;
  assert(plan().error);

  // Our stream on a bank with translation turned on: the gadget would take faults, not bypass.
  reset();
  va64All();
  r[(0x800 + 4 * 5) / 4] = ufsSmmuSmr(kDwc3SmmuStream, kDwc3SmmuMask);
  r[(0xc00 + 4 * 5) / 4] = 1 | (1u << 16);   // bank 1, S2CR.TYPE = translate
  assert(plan().error);

  // A bypass bank without VA64 cannot pass a DMA range above 4 GiB; refuse rather than adopt.
  reset();
  r[(0x800 + 4 * 5) / 4] = ufsSmmuSmr(kDwc3SmmuStream, kDwc3SmmuMask);
  r[(0xc00 + 4 * 5) / 4] = 1;                // bank 1, VA64 left at 0
  {
    UfsSmmuPlan big = planDwc3Smmu(kDwc3SmmuStream, kDwc3SmmuMask, kAppsSmmuSize, 0x100000000ull, 0x1000ull,
                                   [&](unsigned at) { return r.at(at / 4); });
    assert(big.error);
  }

  // UEFI's entry carries mask bits beyond the ones we would write. Hardware 2026-11-05 (console178)
  // hit exactly this: the plan adopted the entry, and configureUfsSmmu then refused it with
  // "firmware UFS stream mapping changed" because it compared the register against a recomputed
  // ufsSmmuSmr() instead of the value the planner had read. Adoption must survive that.
  reset();
  va64All();
  r[(0x800 + 4 * 47) / 4] = 0x80000740u | (0x18u << 16);   // mask bits set that kDwc3SmmuMask (0) lacks
  r[(0xc00 + 4 * 47) / 4] = 3;                             // bypass, bank 3
  p = plan();
  assert(!p.error && p.reuse && p.firmware && p.slot == 47 && p.bank == 3);
  assert(p.smr == (0x80000740u | (0x18u << 16)));           // the observed value, not a recomputation
  {
    unsigned writes = 0;
    auto wr = [&](unsigned at, uint32_t v) { ++writes; r.at(at / 4) = v; };
    auto rd = [&](unsigned at) { return r.at(at / 4); };
    assert(!configureUfsSmmu(p, kDwc3SmmuStream, kDwc3SmmuMask, rd, wr) && writes == 0);
  }

  // Another stream squatting on the top bank: a fresh plan falls to the next bank down.
  reset();
  va64All();
  r[0x800 / 4] = 0x80000061; r[0xc00 / 4] = 63;
  p = plan();
  assert(!p.error && !p.reuse && p.bank == 62 && p.slot == 127);

  // No free group at all: refused, not silently given a group that maps another stream.
  reset();
  va64All();
  for (unsigned i = 0; i < 128; ++i) { r[(0x800 + 4 * i) / 4] = 0x80000000u | i; r[(0xc00 + 4 * i) / 4] = i & 63; }
  assert(plan().error);

  std::printf("Dwc3Smmu: stream 0x740 SMMU plan assertions passed\n");
  return 0;
}
