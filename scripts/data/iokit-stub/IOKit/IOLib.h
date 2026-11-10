// Host-only IOKit stubs so package/EnchiladaAdrenoProbe.cpp can be type-checked
// WITHOUT an XNU tree. This file is NEVER compiled into the kernel: it exists
// only for the local shim-compile check (scripts/check-enchilada-adreno-probe.py).
// It must stay minimal and honest -- if the shim needs something that is not
// here, the check fails loudly instead of silently widening.
#ifndef ENCHILADA_IOKIT_STUB_H
#define ENCHILADA_IOKIT_STUB_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

// IOLib.h surface used by the probes.
#ifdef __cplusplus
extern "C" {
#endif
void IOLog(const char *format, ...) __attribute__((format(printf, 1, 2)));
// The kernel shim formats its single verdict line with snprintf, exactly as
// package/EnchiladaSmmuProbe.cpp:194 already does; <stdio.h> is not pulled in by
// the other probes, so it is declared here for the host type-check only.
int snprintf(char *buffer, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
#ifdef __cplusplus
}
#endif

// pexpert surface used by the probes.
#ifdef __cplusplus
extern "C" {
#endif
int PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_len);
#ifdef __cplusplus
}
#endif

#endif  // ENCHILADA_IOKIT_STUB_H
