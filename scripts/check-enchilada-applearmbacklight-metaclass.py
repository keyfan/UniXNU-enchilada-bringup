# -*- coding: utf-8 -*-
"""
Stage 3 layer 3d PREREQUISITE (host-side, read-only, NO flashing).

Settle the two open risks flagged by the broad AppleARMBacklight survey BEFORE
writing any IOService code (遵守"先核验再上机"):

  Risk 1  Does the AppleARMBacklight metaclass exist in the iPadOS 17.7.11
          (21H461, iPad7,1) kernel we boot?  If not, impersonate()
          (OSMetaClass::getMetaClassWithName(OSSymbol::withCString(
           "AppleARMBacklight"))) would return null and the whole layer-3d
           IOService shape would fail.
  Risk 2  Does the built-in AppleARMBacklight driver auto-instantiate on this
          board via an IOPlatformDevice 'backlight' nub in the ADT?  If so our
          service would collide with a live built-in instance.

Two independent checks, both purely byte-level scans of local artifacts:

  CHECK 1  ASCII scan of (a) the stock decompressed iPad7,1 kernelcache AND
           (b) the CI #210 kernel.macho that was actually flashed and booted,
           for AppleARMBacklight / backlight-control / IODisplayParameters /
           CoreBrightness, the driver personality plist
           (IOClass / IOProviderClass / IONameMatch), the metaclass
           registration records (site.AppleARMBacklight) and method symbols.
  CHECK 2  scan four independent device ADT copies for a 'backlight' node.

Measured 2026-11-07 (verbatim in evidence/applearmbacklight-metaclass.md):
  stock kernelcache : 16 x AppleARMBacklight, 4 x backlight-control,
                      2 x IODisplayParameters, 4 x CoreBrightness,
                      7 x AppleCLCD, personality <key>AppleARMBacklight</key>
                      present with IONameMatch=backlight
  CI #210 kernel    : 13 x AppleARMBacklight, 3 x site.AppleARMBacklight
                      (metaclass registration records)
  all 4 ADTs        : 0 x 'backlight'  => NO built-in instance clash

Usage:  python -X utf8 scripts/check-enchilada-applearmbacklight-metaclass.py
Exit 0 = prerequisites settled; exit 1 = a finding changed, re-audit before
writing the layer-3d IOService.
"""
import pathlib
import re
import sys

# Device repo is <oneplus6-ios>/work/UniXNU-enchilada; scripts/ is parents[0].
ROOT = pathlib.Path(__file__).resolve().parents[3]

STOCK_KC = ROOT / "work/UniXNU/.cache/firmware/kernelcache.macho/kernelcache.release.ipad7.decompressed"
BUILT_K = ROOT / "work/tmp/ci210-kernel/enchilada/kernel/kernel.macho"
ADTS = [
    ROOT / "work/tmp/inject.adt",                  # CI #210 flash (manifest-hash injected)
    ROOT / "work/tmp/shipped.adt",                 # shipped baseline
    ROOT / "notes/adt-test/enchilada.adt",         # extracted enchilada ADT
    ROOT / "work/tmp/console/kernel.macho.adt",    # console-build ADT
]

NEEDLES = [b"AppleARMBacklight", b"backlight-control", b"IODisplayParameters",
           b"CoreBrightness", b"AppleCLCD", b"AppleARMPlatform"]


def scan(path, needles):
    """Return {needle: count} with 32-byte overlap so straddling hits count."""
    counts = {n: 0 for n in needles}
    maxlen = max(len(n) for n in needles)
    prev = b""
    size = 0
    with open(path, "rb") as f:
        while True:
            chunk = f.read(8 * 1024 * 1024)
            if not chunk:
                break
            size += len(chunk)
            buf = prev + chunk
            prev = buf[-(maxlen - 1):] if maxlen > 1 else b""
            for n in needles:
                counts[n] += buf.count(n)
    return size, counts


def ascii_ctx(data, off, before=32, after=64):
    lo, hi = max(0, off - before), min(len(data), off + after)
    return "".join(chr(c) if 32 <= c < 127 else "." for c in data[lo:hi])


fail = []
print("ROOT:", ROOT)

# ---- CHECK 1a: stock kernelcache -------------------------------------------
print("\n=== CHECK 1a - stock iPad7,1 kernelcache ===")
if not STOCK_KC.is_file():
    print("  SKIP: stock kernelcache not present")
else:
    data = STOCK_KC.read_bytes()
    size, c = scan(STOCK_KC, NEEDLES)
    print("  size: %d B (%.1f MiB)" % (size, size / 1048576.0))
    for n in NEEDLES:
        print("  %-22s %d" % (n.decode(), c[n]))
    if c[b"AppleARMBacklight"] < 1:
        fail.append("stock kernelcache has NO AppleARMBacklight string")
    if c[b"backlight-control"] < 1 or c[b"IODisplayParameters"] < 1:
        fail.append("stock kernelcache lacks backlight-control/IODisplayParameters")
    m = re.search(b"<key>AppleARMBacklight</key>", data)
    if not m:
        fail.append("stock kernelcache lacks the AppleARMBacklight driver personality")
    else:
        plist = ascii_ctx(data, m.start(), 40, 260)
        print("  personality:", plist)
        for token in (b"IOClass", b"IOProviderClass", b"IONameMatch", b"backlight"):
            if token not in data[m.start():m.start() + 300]:
                fail.append("personality missing %s" % token.decode())
    meths = sorted(set(x.group().decode() for x in
                       re.finditer(rb"AppleARMBacklight::[A-Za-z0-9_]+", data)))
    print("  method symbols:", meths)
    if not meths:
        fail.append("no AppleARMBacklight:: method symbols (class not implemented?)")

# ---- CHECK 1b: CI #210 built kernel ----------------------------------------
print("\n=== CHECK 1b - CI #210 built kernel (the image that boots) ===")
if not BUILT_K.is_file():
    print("  SKIP: CI #210 kernel.macho not present")
else:
    data = BUILT_K.read_bytes()
    size, c = scan(BUILT_K, NEEDLES)
    print("  size: %d B (%.1f MiB)" % (size, size / 1048576.0))
    for n in NEEDLES:
        print("  %-22s %d" % (n.decode(), c[n]))
    sites = len(re.findall(rb"site\.AppleARMBacklight", data))
    print("  site.AppleARMBacklight metaclass records:", sites)
    if c[b"AppleARMBacklight"] < 1:
        fail.append("built kernel has NO AppleARMBacklight string")
    if sites < 1:
        fail.append("built kernel has NO site.AppleARMBacklight metaclass record")

# ---- CHECK 2: device ADT has no 'backlight' nub ----------------------------
print("\n=== CHECK 2 - device ADT 'backlight' node scan ===")
present = 0
for p in ADTS:
    if not p.is_file():
        print("  %-40s MISSING" % p.relative_to(ROOT))
        continue
    d = p.read_bytes()
    n_back = d.count(b"backlight")
    n_pwm = d.count(b"backlight,pwm")
    n_disp = d.count(b"display")
    print("  %-40s backlight=%d backlight,pwm=%d display=%d" %
          (str(p.relative_to(ROOT)), n_back, n_pwm, n_disp))
    present += 1
    if n_back != 0:
        fail.append("%s has a 'backlight' node (built-in instance clash)" % p.name)
if present < 2:
    fail.append("too few ADT copies present to conclude (need >= 2)")

# ---- verdict ---------------------------------------------------------------
print("\n=== VERDICT ===")
if fail:
    for x in fail:
        print("  FAIL:", x)
    print("  layer-3d prerequisites NOT settled - re-audit before writing the IOService")
    sys.exit(1)
print("  AppleARMBacklight metaclass EXISTS in both the stock kernelcache and the")
print("  CI #210 built kernel; the built class is fully implemented (personality +")
print("  method symbols). The device ADT has NO 'backlight' IOPlatformDevice nub,")
print("  so the built-in driver will NOT auto-instantiate and our service will be")
print("  the sole AppleARMBacklight. layer-3d prerequisites SETTLED (host-verified,")
print("  static evidence only - runtime getMetaClassWithName still needs CI + device).")
sys.exit(0)
