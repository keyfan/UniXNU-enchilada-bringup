// Host-only stub of <pexpert/pexpert.h>, same purpose and same rules as
// scripts/data/iokit-stub/IOKit/IOLib.h -- type-check the probe shims without
// an XNU tree. Never compiled into the kernel.
#ifndef ENCHILADA_PEXPERT_STUB_H
#define ENCHILADA_PEXPERT_STUB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Declared once, in IOLib.h (which every probe includes first); repeated here so
// the pexpert stub is usable on its own too. The two declarations are identical.
int PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_len);
#ifdef __cplusplus
}
#endif

#endif  // ENCHILADA_PEXPERT_STUB_H
