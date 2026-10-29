// Weak stand-in for the button sink sheng's HID kit publishes.
//
// NativeHidInput.cpp declares native_buttons_set_sink() __attribute__((weak_import)) and calls it only
// when the address is non-null, so an absent provider is meant to be harmless.  ld64 does not agree for a
// static kernel link: it fails with "Undefined symbols for architecture arm64: _native_buttons_set_sink"
// instead of resolving the reference to 0, exactly as it rejected the weak_import diagnostics of
// package/BootLog.c until weak definitions were linked (see package/BootProgressStubs.c).
//
// This board has no button kit -- the touch snapshot arrives through libEnchiladaTouch.a, which defines
// native_touch_set_sink -- so the definition is a no-op, and weak keeps a real provider able to replace it.
#include <stdint.h>

__attribute__((weak)) void native_buttons_set_sink(void (*sink)(void *, uint32_t), void *context)
{
	(void)sink;
	(void)context;
}
