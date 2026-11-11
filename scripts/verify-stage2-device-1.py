#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Round 49 stage-2 DEVICE verification — method 1 (raw binary + line-scan domain).

Two-domain plan (m03546): method 1 scans the raw msgbuf binary for the stage-2
IOLog lines and the stage-1 GPUCC line; method 2 (verify-stage2-device-2.py)
parses the decoded text with a different method and asserts statistical
invariants. Expected outcome per the round-35 steady state:
  stage1 rc=0 GPUCC=5090000 ... verdict=cx-DOWN ...
  stage2=cx-down-gmu-not-mapped
"""
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
W = DEV.parents[1]
LOGDIR = sorted((W / "work" / "log").glob("stage2probe/untagged-*"))
if not LOGDIR:
    sys.exit("no stage2probe log dir yet")
D = LOGDIR[-1]
MSGBUF = D / "msgbuf.txt"
LATEST = D / "latest.txt"
HDR = D / "header.txt"

checks = 0
fails = []


def ck(ok, label, detail=""):
    global checks
    checks += 1
    if not ok:
        fails.append((label, detail))
        print("FAIL: %s | %s" % (label, detail))
    else:
        print("PASS: %s | %s" % (label, detail))


def need(cond, label, detail=""):
    ck(bool(cond), label, detail)


def main():
    need(MSGBUF.is_file(), "msgbuf.txt exists", str(MSGBUF))
    raw = MSGBUF.read_bytes()
    txt = raw.decode("utf-8", errors="replace")

    # ---- stage 1 GPUCC line (round-35 baseline behaviour) ----
    m1 = re.search(r"rc=(\d+) GPUCC=([0-9a-f]+) size=([0-9a-f]+) cx=([0-9a-f]+) gx=([0-9a-f]+) "
                   r"cx_on=(\d) gx_on=(\d) gx_off=(\d) verdict=([\w-]+) steps_left=(\d+)", txt)
    need(m1 is not None, "stage-1 GPUCC verdict line present")
    if m1:
        need(m1.group(1) == "0", "stage-1 rc=0", m1.group(1))
        need(m1.group(2) == "5090000", "GPUCC base 0x5090000", m1.group(2))
        need(m1.group(9) == "cx-DOWN", "verdict=cx-DOWN (round-35 steady state)", m1.group(9))
        need(m1.group(10) == "9", "steps_left=9", m1.group(10))

    # ---- stage 2 line: CX down => no mapping ----
    m2 = re.search(r"stage2=cx-down-gmu-not-mapped", txt)
    m2b = re.search(r"stage2=gmu-read gmu=506a000 size=30000 general7=([0-9a-f]+) boot_state=(\d)\((warm|cold)\)", txt)
    need(m2 is not None or m2b is not None, "stage-2 line present (either branch)")
    if m2:
        need(m2b is None, "cx-down branch only (no gmu read)", "mutually exclusive")
    if m2b:
        need(m2 is None, "cx-up branch only", "unexpected: CX came up this boot")

    # ---- header ----
    if HDR.is_file():
        kv = {}
        for line in HDR.read_text(encoding="utf-8", errors="replace").splitlines():
            mm = re.match(r"\s*([A-Za-z_]+)\s*=\s*(\S+)", line)
            if mm:
                kv[mm.group(1)] = mm.group(2)
        need(kv.get("magic") == "SHLOG001", "header magic=SHLOG001", repr(kv.get("magic")))
        need(kv.get("write_errors") == "0", "header write_errors=0", repr(kv.get("write_errors")))
        boot = int(kv.get("boot_id", "0"))
        need(boot != 0x205050d2 and boot != 0x20e344b3, "new boot_id (not a replay of r35/r38)", hex(boot))

    # ---- zero-write property: no GMU write lines, no ITCM traffic in log ----
    need(b"stage2=gmu-write" not in raw, "no GMU write line (zero-write contract)")

    print("STAGE2_DEVICE_VERIFY_1: %s (%d checks, %d failures)" % ("PASS" if not fails else "FAIL", checks, len(fails)))
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
