#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Round 52 stage-3 DEVICE verification - method 1 (RAW BINARY + line-scan domain).

Two-domain plan (m03546): method 1 scans the raw msgbuf binary for the three
1 Hz replay rows (stage-1 verdict / stage-2 CX-down / stage-3 ordered PLAN) and
the SHLOG001 header; method 2 (verify-stage3-device-2.py) parses the decoded
text with a DIFFERENT method and asserts statistical invariants + the source
zero-write contract.

Expected single-boot outcome (round-35 CX-DOWN steady state, no GPU clock):
  rc=0 GPUCC=5090000 ... verdict=cx-DOWN steps_left=9
  stage2=cx-down-gmu-not-mapped
  stage3=plan steps=5 gxpd_guarded=1 core=200000000 hub=150000000 clocks=4
         valid=1 distinct=1 size=5 plan_rc=0 ready=0 mcx=0 mclk=0
The stage-3 row proves the round-51 AdrenoPowerUpV1 model is LINKED INTO the
kernel and its pure Plan() emits the exact 5-op a6xx_gmu.c:1033-1047 order.
"""
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
W = DEV.parents[1]
LOGDIR = sorted((W / "work" / "log").glob("stage3probe/untagged-*"),
                key=lambda p: p.stat().st_mtime)
if not LOGDIR:
    sys.exit("no stage3probe log dir yet")
D = LOGDIR[-1]  # newest decoded log by mtime (lexicographic order is wrong)
MSGBUF = D / "msgbuf.txt"
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

    # ---- stage 1 GPUCC line (round-35 baseline) ----
    m1 = re.search(r"rc=(\d+) GPUCC=([0-9a-f]+) size=([0-9a-f]+) cx=([0-9a-f]+) gx=([0-9a-f]+) "
                   r"cx_on=(\d) gx_on=(\d) gx_off=(\d) verdict=([\w-]+) steps_left=(\d+)", txt)
    need(m1 is not None, "stage-1 GPUCC verdict line present")
    if m1:
        need(m1.group(1) == "0", "stage-1 rc=0", m1.group(1))
        need(m1.group(2) == "5090000", "GPUCC base 0x5090000", m1.group(2))
        need(m1.group(9) == "cx-DOWN", "verdict=cx-DOWN", m1.group(9))
        need(m1.group(10) == "9", "steps_left=9", m1.group(10))

    # ---- stage 2: CX down => no GMU mapping ----
    m2 = re.search(r"stage2=cx-down-gmu-not-mapped", txt)
    m2b = re.search(r"stage2=gmu-read gmu=506a000 size=30000 general7=([0-9a-f]+) boot_state=(\d)\((warm|cold)\)", txt)
    need(m2 is not None or m2b is not None, "stage-2 line present (either branch)")
    if m2:
        need(m2b is None, "cx-down branch only (no GMU read)", "mutually exclusive")
    if m2b:
        need(m2 is None, "cx-up branch only", "unexpected: CX came up this boot")

    # ---- stage 3: ordered power-up PLAN (the NEW evidence) ----
    m3 = re.search(
        r"stage3=plan steps=(\d+) gxpd_guarded=(\d) core=(\d+) hub=(\d+) clocks=(\d+) "
        r"valid=(\d) distinct=(\d) size=(\d+) plan_rc=(\d+) ready=(\d) mcx=(\d) mclk=0", txt)
    need(m3 is not None, "stage-3 PLAN row present")
    if m3:
        want = ["5", "1", "200000000", "150000000", "4", "1", "1", "5", "0", "0", "0"]
        labels = ["steps=5", "gxpd_guarded=1", "core=200000000", "hub=150000000 (A630 non-A740)",
                  "clocks=4 (GMU node)", "valid=1", "distinct=1 (4 != 6)", "size=5",
                  "plan_rc=0 (Ok)", "ready=0 (PreIsReady refuses)", "mcx=0 (CX DOWN)"]
        for i, (got, exp, lab) in enumerate(zip(m3.groups(), want, labels), 1):
            need(got == exp, "stage-3 %s" % lab, "got %s want %s" % (got, exp))

    # ---- zero-write property: NO Execute / NO bus writes in the log ----
    need(b"stage3=exec" not in raw, "no stage3=exec line (Execute never called)")
    need(b"genpd_on=" not in raw and b"clocks_enable=" not in raw,
         "no genpd/clock callback lines (pure Plan only)")

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
        need(boot not in (0x205050d2, 0x20e344b3, 0x20c89316),
             "NEW boot_id (not r35 / r38 / flash #11)", hex(boot))

    print("STAGE3_DEVICE_VERIFY_1: %s (%d checks, %d failures)" %
          ("PASS" if not fails else "FAIL", checks, len(fails)))
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
