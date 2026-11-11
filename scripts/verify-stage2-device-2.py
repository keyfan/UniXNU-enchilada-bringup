#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Round 49 stage-2 DEVICE verification — method 2 (DECODED TEXT domain).

Deliberately different from method 1: parses header.txt as key=value, walks
msgbuf.txt line by line extracting every stage1/stage2 row with regex, asserts
statistical invariants across ALL 1 Hz replays (identical content), and
verifies the zero-write + CX-gate property FROM SOURCE.
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
SRC = DEV / "package" / "EnchiladaAdrenoProbe.cpp"
HDR = D / "header.txt"
MSGBUF = D / "msgbuf.txt"

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
    # ---- header.txt as key=value ----
    kv = {}
    for line in HDR.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"\s*([A-Za-z_]+)\s*=\s*(\S+)", line)
        if m:
            kv[m.group(1)] = m.group(2)
    need(kv.get("magic") == "SHLOG001", "header magic=SHLOG001", repr(kv.get("magic")))
    need(kv.get("version") == "4", "header version=4", repr(kv.get("version")))
    need(kv.get("write_errors") == "0", "header write_errors=0", repr(kv.get("write_errors")))
    boot = int(kv.get("boot_id", "0"))
    need(boot not in (0x205050d2, 0x20e344b3, 0x20e344b3), "new boot_id", hex(boot))

    # ---- msgbuf line walk ----
    lines = MSGBUF.read_text(encoding="utf-8", errors="replace").splitlines()
    stage1 = [l for l in lines if "GPUCC=" in l and "verdict=" in l]
    stage2_down = [l for l in lines if "stage2=cx-down-gmu-not-mapped" in l]
    stage2_up = [l for l in lines if "stage2=gmu-read" in l]
    need(len(stage1) >= 1, "stage-1 verdict rows >= 1", str(len(stage1)))
    need(len(stage2_down) + len(stage2_up) >= 1, "stage-2 rows >= 1",
         "down=%d up=%d" % (len(stage2_down), len(stage2_up)))
    need(not (stage2_down and stage2_up), "exactly one stage-2 branch",
         "down=%d up=%d" % (len(stage2_down), len(stage2_up)))

    # statistical invariant: every replay row identical (1 Hz replay of one verdict)
    if stage1:
        need(len(set(stage1)) == 1, "all stage-1 rows identical (replay)", str(len(set(stage1))))
        m = re.search(r"verdict=([\w-]+)", stage1[0])
        need(m and m.group(1) == "cx-DOWN", "verdict=cx-DOWN", m.group(1) if m else "?")
    if stage2_down:
        need(len(set(stage2_down)) == 1, "all stage-2 rows identical (replay)", str(len(set(stage2_down))))

    # ---- zero-write property FROM SOURCE ----
    src = SRC.read_text(encoding="utf-8")
    need("stage2=cx-down-gmu-not-mapped" in src, "source has the cx-down branch")
    need("stage2=gmu-read" in src, "source has the gmu-read branch")
    # the stage-2 code path must contain NO wr32 calls between the gate and the read
    seg = src[src.find("adreno_board_window_is_gmu"):src.find("stage2=gmu-read")]
    need(seg and "wr32" not in seg, "no wr32 in the stage-2 gate->read segment",
         "segment %d chars" % len(seg) if seg else "segment missing")

    print("STAGE2_DEVICE_VERIFY_2: %s (%d checks, %d failures)" % ("PASS" if not fails else "FAIL", checks, len(fails)))
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
