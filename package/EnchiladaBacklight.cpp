// SPDX-License-Identifier: BSD-2-Clause-Patent
// enchilada stage 3, layer 3d (beta): the OS-facing brightness service.
//
// Provenance / why this is a SEPARATE object:
//   sheng package/AppleARMBacklight.cpp + BacklightProbe.cpp (both the 384-line
//   non-ptrauth and the 406-line ptrauth variants) defines ShengBacklight : IOService
//   and impersonates the built-in AppleARMBacklight metaclass so CoreBrightness /
//   backboardd accepts the service (IOObjectConformsTo(service, "AppleARMBacklight")
//   is what starts libEDR). The class MUST NOT be named AppleARMBacklight: that
//   metaclass already exists in AppleARMPlatform and a duplicate name panics at
//   class registration (sheng int-229).
//
//   This enchilada port is deliberately THIN (smallest possible IOKit ABI surface):
//   ALL brightness logic is delegated to the pure-C, HOST-VERIFIED MsmDsiControl
//   (0..1023 clamp -- NOT sheng's 2047 -- 16.16 FadePeriod, linear fade) and to the
//   DEVICE-VERIFIED transmit hook enchilada_backlight_transmit() (mode 3, CI #210).
//   The only IOKit work here is: register one IOService, impersonate the metaclass,
//   publish IODisplayParameters.brightness {min=0,max=1023,value}, and accept
//   setProperties() from backboardd WITHOUT ever blocking it.
//
//   Local machine has no macOS/Xcode: cached raw XNU headers are not a compilable
//   SDK (verified by two experiments), so CI (macOS 26.6 + Xcode 26.6 + iPhoneOS
//   SDK) is the ONLY ABI authority. This file therefore carries BOTH the ptrauth
//   (arm64e, manual metaclass split) and non-ptrauth (arm64, vtable-copy)
//   impersonation branches, verbatim from the upstream 406-line template; the
//   preprocessor + real SDK headers decide which one compiles (21H461 / iPad7,1
//   is arm64, so the non-ptrauth branch is expected).
//
// Entry point: package/BootProgress.c monitor() calls enchilada_backlight_poll() at
// 1 Hz; with enchilada-backlight=4 that shim calls enchilada_backlight_service_probe()
// once (preModLoad -> static_init -> postModLoad -> OSTypeAlloc -> registerService)
// and enchilada_backlight_service_tick(now_ms) on every later poll. The class's C++
// static initializer is at __GLOBAL__sub_I_EnchiladaBacklight.cpp, staged by
// scripts/build-enchilada-msm-dsi.py's late-objcopy step (--remove-section
// __DATA,__mod_init_func --globalize-symbol ...) so it runs at a controlled time.
//
// Diagnostics: everything is published into native_backlight_stats[10] (strong def
// in package/EnchiladaBacklightProbe.cpp, read by package/BootLog.c:749 L row) via
// msm_dsi_control_publish(). stage 6 = init, 7 = published, -1 = no metaclass;
// stats[1] = setProperties() requests = the CoreBrightness acceptance signal.

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSDictionary.h>
#include <pexpert/pexpert.h>
#include <mach/kmod.h>
#include <stdint.h>

#include "MsmDsiControl.h"   /* pure-C, host-verified control plane (0..1023, 16.16) */

extern kmod_info_t g_kernel_kmod_info;   /* KeyStoreProbe.cpp:11 (resolved in kernel) */
extern "C" void enchilada_backlight_static_init(void)
    __asm__("__GLOBAL__sub_I_EnchiladaBacklight.cpp");

/* Strong definition lives in package/EnchiladaBacklightProbe.cpp (same -force_load
 * archive); BootLog.c:174 reads it as volatile int32_t[10]. Referenced, NOT defined. */
extern "C" volatile int32_t native_backlight_stats[10];

/* The device-verified DSI transmit (mode 3, CI #210). weak_import: it is defined in
 * package/EnchiladaBacklightProbe.cpp (same force-loaded archive); guarded so this
 * object still links if that shim is ever built without it. */
extern "C" int enchilada_backlight_transmit(uint16_t level) __attribute__((weak_import));

namespace {
struct msm_dsi_control gControl;   /* pure-C control-plane state (zero-init) */
uint64_t gNowMs = 0;                /* ms clock injected by the 1 Hz tick */
IOService *gService = nullptr;      /* retained singleton; also gates the tick */
const OSMetaClass *gAppleClass = nullptr;

#if __has_feature(ptrauth_calls)
/* arm64e: vtable pointers/entries are signed, so a copied vtable would not
 * authenticate. The manual metaclass split below answers getMetaClass() with
 * gAppleClass after impersonation (upstream 406-line variant). */
bool gImpersonating = false;
#else
/* Non-ptrauth (arm64): installed into vtable slot 7 by impersonate(). */
static const OSMetaClass *appleMetaClass(const OSMetaClassBase *) { return gAppleClass; }
#endif
}  // namespace

class EnchiladaBacklight : public IOService {
    OSDeclareDefaultStructors(EnchiladaBacklight)
public:
    bool impersonate(void);
    void publish(uint32_t value);
    void publishFirst(void);
    IOReturn setProperties(OSObject *properties) override;   /* IORegistryEntry.h:661 */
};

bool EnchiladaBacklight::impersonate(void) {
    const OSSymbol *name = OSSymbol::withCString("AppleARMBacklight");
    gAppleClass = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
    OSSafeReleaseNULL(name);
    if (!gAppleClass) return false;
#if __has_feature(ptrauth_calls)
    __atomic_store_n(&gImpersonating, true, __ATOMIC_RELEASE);
    return true;
#else
    enum { kSlots = 320, kGetMetaClass = 7 };   /* iOS APPLE_KEXT_VTABLE_PADDING = 0 */
    uintptr_t *copy = static_cast<uintptr_t *>(IOMalloc(kSlots * sizeof(uintptr_t)));
    if (!copy) return false;
    uintptr_t *vptr = *reinterpret_cast<uintptr_t **>(this);
    for (unsigned i = 0; i < (unsigned)kSlots; ++i) copy[i] = vptr[i];
    copy[kGetMetaClass] = reinterpret_cast<uintptr_t>(&appleMetaClass);
    __asm__ volatile("dmb ish" ::: "memory");
    *reinterpret_cast<uintptr_t **>(this) = copy;
    return true;
#endif
}

void EnchiladaBacklight::publish(uint32_t value) {
    /* Mirror sheng publish() (AppleARMBacklight.cpp:258-267) with max = 1023, the
     * sofef00 DCS bl-max (0x3ff), NOT sheng's KTZ8866 2047. */
    OSDictionary *brightness = OSDictionary::withCapacity(3);
    OSDictionary *parameters = OSDictionary::withCapacity(1);
    if (brightness && parameters) {
        OSNumber *min = OSNumber::withNumber(0ULL, 32);
        OSNumber *max = OSNumber::withNumber((unsigned long long)kMsmDsiControlMaxLevel, 32);
        OSNumber *current = OSNumber::withNumber((unsigned long long)value, 32);
        if (min && max && current) {
            brightness->setObject("min", min);
            brightness->setObject("max", max);
            brightness->setObject("value", current);
            parameters->setObject("brightness", brightness);
            setProperty("IODisplayParameters", parameters);
        }
        OSSafeReleaseNULL(min);
        OSSafeReleaseNULL(max);
        OSSafeReleaseNULL(current);
    }
    OSSafeReleaseNULL(brightness);
    OSSafeReleaseNULL(parameters);
}

void EnchiladaBacklight::publishFirst(void) {
    unsigned control = 1;
    PE_parse_boot_argn("enchilada-backlight-control", &control, sizeof control);
    bool impersonated = false;
    if (control == 1) impersonated = impersonate();
    if (impersonated) {
        /* The enchilada kernel carries the sheng sandbox-grant patches, so
         * backboardd is allowed to set FadePeriod on this owned service. */
        setProperty("NativeSandboxGrantProcess", "backboardd");
        setProperty("backlight-control", true);
    }
    publish(gControl.level);
    registerService();
    gControl.impersonated = impersonated ? 1 : 0;
    gControl.stage = gAppleClass ? kMsmDsiControlStagePublished
                                 : kMsmDsiControlStageNoMeta;
    msm_dsi_control_publish(&gControl, native_backlight_stats);
}

IOReturn EnchiladaBacklight::setProperties(OSObject *properties) {
    /* Mirror sheng setProperties() (AppleARMBacklight.cpp:339-359): parse
     * {brightness, FadePeriod(16.16)} and hand a clamped goal to the control plane.
     * NON-BLOCKING by construction: no runAction, no register writes, no DMA here
     * (sheng's explicit warning: doing work here blocks backboardd's brightness
     * thread). The actual DCS transmit happens later on the 1 Hz tick. */
    OSDictionary *dictionary = OSDynamicCast(OSDictionary, properties);
    if (!dictionary) return kIOReturnSuccess;
    OSNumber *brightness = OSDynamicCast(OSNumber, dictionary->getObject("brightness"));
    if (!brightness) return kIOReturnSuccess;
    OSNumber *fadePeriod = OSDynamicCast(OSNumber, dictionary->getObject("FadePeriod"));
    const int32_t requested = (int32_t)brightness->unsigned32BitValue();
    const uint32_t fp = fadePeriod ? fadePeriod->unsigned32BitValue() : 0u;
    msm_dsi_control_request(&gControl, requested, fp, gNowMs);
    publish(msm_dsi_control_clamp(requested));
    msm_dsi_control_publish(&gControl, native_backlight_stats);
    return kIOReturnSuccess;
}

#if __has_feature(ptrauth_calls)
/* Manual metaclass split (upstream 406-line, renamed). getMetaClass() answers the
 * AppleARMBacklight metaclass once impersonation is armed. */
EnchiladaBacklight::MetaClass EnchiladaBacklight::gMetaClass;
const OSMetaClass * const EnchiladaBacklight::metaClass = &EnchiladaBacklight::gMetaClass;
const OSMetaClass * const EnchiladaBacklight::superClass = &IOService::gMetaClass;
EnchiladaBacklight::EnchiladaBacklight(const OSMetaClass *meta) : IOService(meta) {}
EnchiladaBacklight::~EnchiladaBacklight() {}
const OSMetaClass *EnchiladaBacklight::getMetaClass() const {
    return __atomic_load_n(&gImpersonating, __ATOMIC_ACQUIRE) && gAppleClass
               ? gAppleClass : &gMetaClass;
}
OSMetaClassConstructorInit(EnchiladaBacklight, IOService, )
OSDefineDefaultStructors(EnchiladaBacklight, IOService)
#else
OSDefineMetaClassAndStructors(EnchiladaBacklight, IOService)
#endif

/* One-time registration. Called from the 1 Hz mode-4 poll (BootProgress.c monitor). */
extern "C" void enchilada_backlight_service_probe(void) {
    static bool done = false;
    if (done) return;
    done = true;

    msm_dsi_control_init(&gControl, kMsmDsiControlDefaultLevel);   /* stage 6, level 512 */
    msm_dsi_control_publish(&gControl, native_backlight_stats);

    /* sheng BacklightProbe.cpp / KeyStoreProbe.cpp: bracket the manual static
     * initializer with preModLoad/postModLoad so OSMetaClass registers this class
     * at a controlled time (the __mod_init_func entry was removed by late-objcopy). */
    void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if (!handle) {
        gControl.stage = kMsmDsiControlStageNoMeta;
        msm_dsi_control_publish(&gControl, native_backlight_stats);
        return;
    }
    enchilada_backlight_static_init();
    int status = OSMetaClass::postModLoad(handle);
    if (status) {
        gControl.stage = kMsmDsiControlStageNoMeta;
        msm_dsi_control_publish(&gControl, native_backlight_stats);
        return;
    }

    EnchiladaBacklight *svc = OSTypeAlloc(EnchiladaBacklight);
    if (!svc || !svc->init() || !svc->attach(IOService::getPlatform())) {
        if (svc) svc->release();
        gControl.stage = kMsmDsiControlStageNoMeta;
        msm_dsi_control_publish(&gControl, native_backlight_stats);
        return;
    }
    svc->publishFirst();
    gService = svc;   /* never released: a permanent, registered singleton */
}

/* 1 Hz tick: advance the pure-C fade and (when a new level appears) transmit it
 * through the DEVICE-VERIFIED mode-3 hook. now_ms is a monotonic millisecond clock
 * (the mode-4 poll passes polls*1000); setProperties() uses the last tick's value. */
extern "C" void enchilada_backlight_service_tick(uint64_t now_ms) {
    if (!gService) return;   /* probe did not register; nothing to tick */
    gNowMs = now_ms;
    uint32_t level = 0;
    if (msm_dsi_control_step(&gControl, now_ms, &level)) {
        if (&enchilada_backlight_transmit)
            gControl.last_rc = enchilada_backlight_transmit((uint16_t)level);
    }
    msm_dsi_control_publish(&gControl, native_backlight_stats);
}
