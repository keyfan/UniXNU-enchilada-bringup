# Layer 3d prerequisite: AppleARMBacklight metaclass exists, no built-in instance clash

Date: 2026-11-07. Stage 3 layer 3d (OS-facing `EnchiladaBacklight : IOService`) prerequisite,
settled **before writing any IOKit C++ class** (遵守「先核验再上机」). This is a
host-side, read-only, byte-level audit of local artifacts — no flashing, no
register writes, no DMA. Reproducible with
`python -X utf8 scripts/check-enchilada-applearmbacklight-metaclass.py`.

## The two risks being settled

The broad `AppleARMBacklight` survey (stage 3 layer-3 research) flagged two UNCERTAIN
assumptions that gate whether the sheng `impersonate()` shape can work on this board:

1. **Does the `AppleARMBacklight` metaclass exist in the iPadOS 17.7.11 (21H461,
   iPad7,1) kernel we boot?** `ShengBacklight::impersonate()` calls
   `OSMetaClass::getMetaClassWithName(OSSymbol::withCString("AppleARMBacklight"))`;
   if the class is not linked into this kernelcache the call returns `nullptr` and the
   whole OS-facing shape collapses.
2. **Does the built-in `AppleARMBacklight` driver auto-instantiate on enchilada?**
   Its IOKit personality matches an `IOPlatformDevice` nub named `backlight`; if the
   device ADT had such a nub, our service would collide with a live built-in instance.

## Two independent verifications

### CHECK 1 — byte scan of two independent kernels (metaclass present)

ASCII scan with 32-byte overlap (so straddling hits count). Verbatim counts:

| needle | stock iPad7,1 kernelcache (44.3 MiB) | CI #210 built kernel (120.1 MiB) |
|---|---|---|
| `AppleARMBacklight` | 16 | 13 |
| `backlight-control` | 4 | 2 |
| `IODisplayParameters` | 2 | 1 |
| `CoreBrightness` | 4 | 0 (userspace daemon, not in this image) |
| `AppleCLCD` | 7 | 3 |
| `AppleARMPlatform` | 123 | 16 |

- Sources: `work/UniXNU/.cache/firmware/kernelcache.macho/kernelcache.release.ipad7.decompressed`
  (the device's stock kernelcache), and `work/tmp/ci210-kernel/enchilada/kernel/kernel.macho`
  (CI #210, sha256 `5022f801045973055c422bfd23b93c78a05d7b51791034b37871dd3d3482c1b3`,
  the image that was actually flashed and booted for backlight210mode3).
- **Driver personality** in the stock kernelcache (verbatim excerpt, offset ~0x029333d8):
  `<key>AppleARMBacklight</key><dict><key>IOClass</key><string>AppleARMBacklight</string><key>IOProviderClass</key><string>IOPlatformDevice</string><key>IONameMatch</key><string>backlight</string><key>backlight-control</key><true/></dict>`
  — i.e. the built-in class is fully described, with `IONameMatch=backlight` and the
  `backlight-control` property.
- **Method symbols** in the stock kernelcache (proves it is a real, implemented class, not a
  leftover string): `AppleARMBacklight::handleMessageGated`, `AppleARMBacklight::isDarkBoot`,
  `AppleARMBacklight::setBacklightEnableGated`.
- **Metaclass registration records**: `site.AppleARMBacklight` appears 3 times in the CI #210
  built kernel — the `OSMetaClass` name/registration records that
  `getMetaClassWithName()` looks up. So the lookup will succeed on the booted kernel.
- A CSTRING context at stock offset `0x0009aab6` shows the ObjC type encoding
  `AppleARMBacklight.B16@?0^{OSObject=^^?i}8` (a real class/method signature).

### CHECK 2 — byte scan of four independent device ADT copies (no `backlight` nub)

| ADT copy | `backlight` | `backlight,pwm` | `display` |
|---|---|---|---|
| `work/tmp/inject.adt` (CI #210 flash, manifest-hash injected) | 0 | 0 | 1 |
| `work/tmp/shipped.adt` (shipped baseline) | 0 | 0 | 1 |
| `notes/adt-test/enchilada.adt` (extracted enchilada ADT) | 0 | 0 | 1 |
| `work/tmp/console/kernel.macho.adt` (console build ADT) | 0 | 0 | 1 |

The built-in driver's `IONameMatch=backlight` therefore has **no matching
IOPlatformDevice nub** on this board, so it will not auto-instantiate. Our
`EnchiladaBacklight` service will be the sole `AppleARMBacklight`-conforming instance.
(The board has no backlight node at all — the firmware/UEFI GOP already owns the panel;
this is consistent with the layer 2.5/mode-2/3b findings that the DSI0 controller is
firmware-configured and XNU only injects DCS brightness packets.)

## Conclusion (load-bearing for layer 3d)

- **RESOLVED (static):** the `AppleARMBacklight` metaclass exists in both the stock
  kernelcache and the CI #210 booted kernel, and the built-in class is fully implemented.
  `OSMetaClass::getMetaClassWithName("AppleARMBacklight")` has a valid target, so the
  sheng slot-7 `impersonate()` approach is viable here.
- **RESOLVED (static):** no `backlight` nub in the ADT → no built-in instance clash.
- **Scale mismatch to handle in code:** sheng publishes `IODisplayParameters.brightness`
  `max = 2047` (11-bit KTZ8866); enchilada DCS `SET_DISPLAY_BRIGHTNESS` is `0..1023`.
  Layer 3d must publish `max = 1023` and clamp/scale, never feed 2047 to
  `dsi_dcs_set_brightness_large()` (which clamps to 1023). The transmit side is the
  already DEVICE-VERIFIED `msm_dsi_brightness_send()` path (backlight210mode3, `st=5`).

## Proves / Does NOT prove

- **Proves:** the two static preconditions for the layer-3d IOService shape. This closes
  the "is AppleARMBacklight's metaclass loaded on enchilada" risk that the survey left
  UNCERTAIN.
- **Does NOT prove:** that `getMetaClassWithName()` returns non-null at runtime on this
  specific boot (needs a real CI-compiled driver + device log), nor that
  `IOObjectConformsTo()` / CoreBrightness / backboardd accept our impersonated service
  (all closed-source userspace behaviour, still UNCERTAIN and requiring device
  verification in layer 3d), nor any brightness change (no panel readback in this harness).
  These remain the subject of the layer-3d CI + device verification, each with two
  independent verifiers and its own evidence, before any claim of completion.
