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
LOGDIR = sorted((W / "work" / "log").glob("stage2probe/untagged-*"),
                key=lambda p: p.stat().st_mtime)
if not LOGDIR:
    sys.exit("no stage2probe log dir yet")
D = LOGDIR[-1]  # newest decoded log by mtime (flash #11 tag 20c89316 sorts before #10's 20df20d6 lexicographically)
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
    # Ring-wrap interleaving: userspace IOLog lines can be GLUED onto the front of a
    # replay row (e.g. "accessoryupdaterd[364] ==> temporary-sandboxenchilada-adreno: rc=0
    # ..."). Extract each row by its CANONICAL payload suffix, not by line membership.
    lines = MSGBUF.read_text(encoding="utf-8", errors="replace").splitlines()
    S1 = "enchilada-adreno: rc=0 GPUCC=5090000 size=9000 cx=00005ff4 gx=00088400 cx_on=0 gx_on=0 gx_off=1 verdict=cx-DOWN steps_left=9"
    stage1 = [l for l in lines if S1 in l]
    stage2_down = [l for l in lines if "stage2=cx-down-gmu-not-mapped" in l]
    stage2_up = [l for l in lines if "stage2=gmu-read" in l]
    need(len(stage1) >= 1, "stage-1 verdict rows >= 1", str(len(stage1)))
    need(len(stage2_down) + len(stage2_up) >= 1, "stage-2 rows >= 1",
         "down=%d up=%d" % (len(stage2_down), len(stage2_up)))
    need(not (stage2_down and stage2_up), "exactly one stage-2 branch",
         "down=%d up=%d" % (len(stage2_down), len(stage2_up)))

    # statistical invariant: every replay row identical (1 Hz replay of one verdict).
    # Rows embed a monotonically increasing replay tick "replay t=N attempt=1" and an
    # IOLog timestamp, so identity is asserted on the PAYLOAD (row minus tick/time),
    # with ticks strictly consecutive (proves a single cached verdict replayed at 1 Hz,
    # not re-sampled values).
    def payload(rows):
        out = []
        for l in rows:
            l = re.sub(r"^\[\s*[\d.]+\]:\s*", "", l)
            l = re.sub(r"\(replay t=\d+ attempt=\d+\)", "", l).strip()
            # strip any glued userspace prefix: keep from the replay row's own start
            k = l.find("enchilada-adreno:")
            if k >= 0:
                l = l[k:]
            out.append(l)
        return out

    def ticks(rows):
        ts = [int(m.group(1)) for l in rows for m in [re.search(r"replay t=(\d+)", l)] if m]
        return ts

    p1 = payload(stage1)
    if stage1:
        need(len(set(p1)) == 1, "all stage-1 rows identical payload (replay)", str(len(set(p1))))
        t1 = ticks(stage1)
        need(all(t1[i] == t1[0] + i for i in range(len(t1))), "stage-1 ticks consecutive",
             "%d..%d" % (t1[0], t1[-1]) if t1 else "")
        m = re.search(r"verdict=([\w-]+)", stage1[0])
        need(m and m.group(1) == "cx-DOWN", "verdict=cx-DOWN", m.group(1) if m else "?")
    if stage2_down:
        p2 = payload(stage2_down)
        need(len(set(p2)) == 1, "all stage-2 rows identical payload (replay)", str(len(set(p2))))
        t2 = ticks(stage2_down)
        need(all(t2[i] == t2[0] + i for i in range(len(t2))), "stage-2 ticks consecutive",
             "%d..%d" % (t2[0], t2[-1]) if t2 else "")
        if stage1:
            need(t2 == t1, "stage-2 ticks == stage-1 ticks (paired replay)")

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
