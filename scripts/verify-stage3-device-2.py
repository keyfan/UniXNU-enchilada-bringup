#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Round 52 stage-3 DEVICE verification - method 2 (DECODED TEXT domain).

Deliberately DIFFERENT from method 1: parses header.txt as key=value, walks
msgbuf.txt line by line extracting every stage1/stage2/stage3 replay row by
its canonical payload, asserts payload identity + strictly consecutive 1 Hz
ticks across ALL THREE rows (proves ONE cached verdict replayed, not
re-sampled), and verifies the ZERO-MECHANICAL-EFFECT property FROM SOURCE:
the stage-3 block calls ONLY pure helpers (InputsValid/CountsAreDistinct/
PlanSize/Plan/PreIsReady) and NEVER AdrenoPowerUpV1Execute / a Bus callback.
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
D = LOGDIR[-1]  # newest decoded log by mtime
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
    need(boot not in (0x205050d2, 0x20e344b3, 0x20c89316), "new boot_id", hex(boot))

    # ---- msgbuf line walk by canonical payload suffix (ring-wrap safe) ----
    lines = MSGBUF.read_text(encoding="utf-8", errors="replace").splitlines()
    S1 = "enchilada-adreno: rc=0 GPUCC=5090000 size=9000 cx=00005ff4 gx=00088400 cx_on=0 gx_on=0 gx_off=1 verdict=cx-DOWN steps_left=9"
    S2 = "stage2=cx-down-gmu-not-mapped"
    S3 = "stage3=plan steps=5 gxpd_guarded=1 core=200000000 hub=150000000 clocks=4 valid=1 distinct=1 size=5 plan_rc=0 ready=0 mcx=0 mclk=0"
    stage1 = [l for l in lines if S1 in l]
    stage2 = [l for l in lines if S2 in l]
    stage3 = [l for l in lines if S3 in l]
    need(len(stage1) >= 1, "stage-1 verdict rows >= 1", str(len(stage1)))
    need(len(stage2) >= 1, "stage-2 CX-down rows >= 1", str(len(stage2)))
    need(len(stage3) >= 1, "stage-3 PLAN rows >= 1", str(len(stage3)))
    need(not [l for l in lines if "stage2=gmu-read" in l], "no GMU-read branch (CX stayed down)")

    # statistical invariant: ONE cached verdict, 1 Hz, strictly consecutive ticks
    def payload(rows):
        out = []
        for l in rows:
            l = re.sub(r"^\[\s*[\d.]+\]:\s*", "", l)
            l = re.sub(r"\(replay t=\d+ attempt=\d+\)", "", l).strip()
            k = l.find("enchilada-adreno:")
            if k >= 0:
                l = l[k:]
            out.append(l)
        return out

    def ticks(rows):
        return [int(m.group(1)) for l in rows
                for m in [re.search(r"replay t=(\d+)", l)] if m]

    for name, rows in (("stage-1", stage1), ("stage-2", stage2), ("stage-3", stage3)):
        if not rows:
            continue
        p = payload(rows)
        need(len(set(p)) == 1, "%s rows identical payload (replay)" % name,
             str(len(set(p))))
        t = ticks(rows)
        need(bool(t) and all(t[i] == t[0] + i for i in range(len(t))),
             "%s ticks strictly consecutive (1 Hz)" % name,
             "%d..%d" % (t[0], t[-1]) if t else "no ticks")
    # all three rows replay the SAME tick sequence => one gDone replay branch
    if stage1 and stage2 and stage3:
        t1, t2, t3 = ticks(stage1), ticks(stage2), ticks(stage3)
        need(t2 == t1, "stage-2 ticks == stage-1 ticks (paired replay)")
        need(t3 == t1, "stage-3 ticks == stage-1 ticks (same replay branch)")

    # ---- PURE / zero-mechanical-effect property FROM SOURCE ----
    src = SRC.read_text(encoding="utf-8")
    # strip comments so documentation NAMES of forbidden functions don't collide
    code = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)
    a = code.find("if (stage >= 3u)")
    b = code.find("IOLog(\"enchilada-adreno: %s\\n\", planRow)", a)
    seg = code[a:b] if a >= 0 and b > a else ""
    need(bool(seg), "stage-3 code block isolated (comments stripped)", "%d chars" % len(seg))
    pure = ["AdrenoPowerUpV1InputsValid", "AdrenoPowerUpV1CountsAreDistinct",
            "AdrenoPowerUpV1PlanSize", "AdrenoPowerUpV1Plan",
            "AdrenoPowerUpV1PreIsReady", "AdrenoPowerUpV1BoardGmuClockCount",
            "AdrenoPowerUpV1BoardGpuClockCount", "AdrenoPowerUpV1BoardIsA740Family"]
    for fn in pure:
        need(re.search(r"\b" + fn + r"\b", seg) is not None,
             "stage-3 calls pure helper %s" % fn)
    forbidden = ["AdrenoPowerUpV1Execute", "genpd_on", "genpd_off", "set_rate",
                 "clocks_enable(", "wr32", "armcpu_map_physical"]
    for fn in forbidden:
        need(re.search(r"\b" + re.escape(fn).replace(r"\(", "") + r"\b", seg) is None,
             "stage-3 NEVER calls %s" % fn)
    need("AdrenoPowerUpV1Bus" not in seg, "stage-3 wires NO AdrenoPowerUpV1Bus")
    # the stage-3 block must sit AFTER the stage-2 CX-gate and BEFORE gDone
    need(src.find("if (stage >= 3u)") > src.find("if (stage >= 2u)"),
         "stage-3 block is after the stage-2 CX gate")
    # Use rfind for gDone: the file has early-return `gDone = true` assignments
    # (refused-window / map-failed / wait-platform) BEFORE the stage-3 block.
    # The completion we care about is the FINAL one at the end of poll().
    need(src.find("setPlanReplay(planRow)") < src.rfind("gDone = true"),
         "plan replay armed before gDone")

    print("STAGE3_DEVICE_VERIFY_2: %s (%d checks, %d failures)" %
          ("PASS" if not fails else "FAIL", checks, len(fails)))
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
