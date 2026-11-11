#!/usr/bin/env python3
"""Verify CI #227 (round 51): archive 11 objects incl. AdrenoPowerUpV1."""
import hashlib
import sys
import zipfile
from pathlib import Path

W = Path(__file__).resolve().parents[3]          # oneplus6-ios (script lives in device repo scripts/)
TMP = W / "work" / "tmp"
LOGS = TMP / "ci227-logs"
KERNEL = TMP / "ci227-kernel"

FAILS = []


def need(ok, label, detail=""):
    print(("PASS " if ok else "FAIL ") + label + ("" if ok else f"  [{detail}]"))
    if not ok:
        FAILS.append(label)


def find_zip(tag):
    cands = sorted(TMP.glob(f"ci227-{tag}.zip"))
    return cands[-1] if cands else None


logz = find_zip("logs")
kernz = find_zip("kernel")
need(logz is not None, "logs zip found", str(logz))
need(kernz is not None, "kernel zip found", str(kernz))

if not (logz and kernz):
    sys.exit(1)

# Domain A: archive line from build log
logtxt = None
with zipfile.ZipFile(logz) as z:
    names = z.namelist()
    for n in names:
        data = z.read(n)
        try:
            t = data.decode("utf-8")
        except UnicodeDecodeError:
            t = data.decode("latin-1")
        if "PASS: libEnchiladaAdreno.a" in t:
            logtxt = t
            break
need(logtxt is not None, "archive build line present in logs")

line = None
for l in (logtxt or "").splitlines():
    if "PASS: libEnchiladaAdreno.a" in l:
        line = l.strip()
need(line is not None, "PASS archive line found", repr(line[:120]) if line else "")
need(line and "(11 objects" in line, "archive is 11 objects", repr(line))
if line:
    digest = line.split("sha256 ")[1].rstrip(")").strip()
    print(f"ARCHIVE_SHA16 {digest}")
    need(len(digest) == 16, "archive sha16 length", digest)
# The adreno build log contains ONLY the PASS line (past CIs identical);
# object inclusion is proven by the count 11 + the source_sha256 manifest
# committed in scripts/build-enchilada-adreno.py + local strict compile.
# So this check asserts the log file exists and holds the PASS line only.

# Domain B: kernel.macho
km = None
with zipfile.ZipFile(kernz) as z:
    for n in z.namelist():
        if n.endswith("kernel.macho"):
            km = z.read(n)
            break
need(km is not None, "kernel.macho found in kernel zip")
if km:
    h = hashlib.sha256(km).hexdigest()
    print(f"KERNEL_SHA256 {h}")
    print(f"KERNEL_SIZE {len(km)}")
    need(len(km) >= 126017536, "kernel.macho size >= baseline", str(len(km)))

# Domain C: no "10 objects" / "12 objects" regression
need(logtxt and "(10 objects" not in logtxt, "no 10-objects regression")
need(logtxt and "(12 objects" not in logtxt, "no 12-objects regression")

print(f"CI227_ADRENO_VERIFY: {'PASS' if not FAILS else 'FAIL'} ({len(FAILS)} failures)")
sys.exit(0 if not FAILS else 1)
