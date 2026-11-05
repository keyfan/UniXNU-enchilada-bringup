// SPDX-License-Identifier: BSD-2-Clause-Patent
// The entry points stage B exposes to the probe in NativeDwc3.cpp.
//
// Kept in its own header because NativeDwc3.cpp includes Dwc3SmmuPlan.h (and
// through it the guardless NativeUfs/SmmuTakeover.h) exactly once; putting these
// declarations in Dwc3SmmuPlan.h would tie two unrelated things together.
#ifndef NATIVE_DWC3_GADGET_H
#define NATIVE_DWC3_GADGET_H

#include <stdint.h>
#include "Dwc3SmmuPlan.h"

// Bring the controller up as a USB device and program endpoint 0. `plan` must be
// the same plan the probe computed for stream 0x740: this function installs it (or,
// for the firmware's own entry, only verifies it) before any DMA address is
// written. `qscratch` is the wrapper window (kDwc3QscratchBase); pass 0 to skip the
// VBUS override. Returns false and leaves native_dwc3_gadget_error set on failure.
bool dwc3GadgetStart(uintptr_t core, uintptr_t smmu, uintptr_t qscratch, const UfsSmmuPlan &plan,
                     unsigned physicalEps, unsigned maxPacket);

// Drain the event buffer. Safe to call from any thread; does nothing until
// dwc3GadgetStart succeeded.
void dwc3GadgetDrain();

#endif
