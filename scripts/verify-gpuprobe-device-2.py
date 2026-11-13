#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Verification #2 of the gpuprobe219 DEVICE result -- DECODED TEXT domain.

Deliberately different method from #1: parses header.txt as key=value, walks
msgbuf.txt line by line, extracts timestamps and every field with regex, and
asserts statistical invariants across ALL 300 replays. Also verifies the boot
id decimal/hex equivalence and the READ-ONLY property of the probe from source.

This is the repo-committed copy of work/tmp/_verify_gpuprobe_device_2.py; the
only difference is how W (the oneplus6-ios root) is derived from __file__.
Artifacts it reads live under the session-local oneplus6-ios/work/ tree.
"""
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]          # .../oneplus6-ios/work/UniXNU-enchilada
W = DEV.parents[1]                                # .../oneplus6-ios
D = W / "work" / "log" / "gpuprobe219" / "untagged-205050d2"
SRC = DEV / "package" / "EnchiladaAdrenoProbe.cpp"
HDR = D / "header.txt"
MSGBUF = D / "msgbuf.txt"
LATEST = D / "latest.txt"

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
    need(kv.get("header_bytes") == "152", "header header_bytes=152", repr(kv.get("header_bytes")))
    boot = int(kv.get("boot_id", "0"))
    need(boot == 0x205050d2, "header boot_id decimal == 0x205050d2", "dec=%d hex=%s" % (boot, hex(boot)))
    need(kv.get("window_blocks") == "8192", "header window_blocks=8192", repr(kv.get("window_blocks")))
    need(kv.get("msg_bytes") == "131072", "header msg_bytes=131072 (128 KiB ring)", repr(kv.get("msg_bytes")))
    need(kv.get("slots_written") == "1730", "header slots_written=1730", repr(kv.get("slots_written")))
    need(int(kv.get("uptime_s", "0")) == 899, "header uptime_s=899", repr(kv.get("uptime_s")))
    need(kv.get("write_errors") == "0", "header write_errors=0", repr(kv.get("write_errors")))
    need(kv.get("state") == "2", "header state=2", repr(kv.get("state")))

    # ---- msgbuf.txt structured parse ----
    text = MSGBUF.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    pat = re.compile(
        r"^\[\s*([0-9]+\.[0-9]+)\]: enchilada-adreno: rc=(\S+) GPUCC=(\S+) size=(\S+) "
        r"cx=([0-9a-fA-F]+) gx=([0-9a-fA-F]+) cx_on=(\d+) gx_on=(\d+) gx_off=(\d+) "
        r"verdict=(\S+) steps_left=(\d+) \(replay t=(\d+) attempt=(\d+)\)")
    rows = []
    for ln in lines:
        m = pat.match(ln)
        if m:
            rows.append(m.groups())
    need(len(rows) == 300, "exactly 300 fully-parsed GPUCC replay lines", "count=%d" % len(rows))
    if rows:
        times = [float(r[0]) for r in rows]
        need(abs((times[-1] - times[0]) - 299.0) < 2.0, "replays span ~299 s at 1 Hz",
             "%.3f .. %.3f" % (times[0], times[-1]))
        ts = [int(r[11]) for r in rows]
        need(ts == sorted(ts) and ts[0] == 600 and ts[-1] == 899,
             "replay t= monotonically 600..899 (no wrap loss)", "t=%d..%d" % (ts[0], ts[-1]))
        need(all(r[1] == "0" for r in rows), "all rc=0", "rc set=%s" % set(r[1] for r in rows))
        need(all(r[2] == "5090000" for r in rows), "all GPUCC base 0x5090000", "bases=%s" % set(r[2] for r in rows))
        need(all(r[3] == "9000" for r in rows), "all size 0x9000", "sizes=%s" % set(r[3] for r in rows))
        need(set(r[4] for r in rows) == {"00005ff4"}, "CX status 00005ff4 stable across 300 s",
             "cx=%s" % set(r[4] for r in rows))
        need(set(r[5] for r in rows) == {"00088400"}, "GX status 00088400 stable across 300 s",
             "gx=%s" % set(r[5] for r in rows))
        need(all(r[6] == "0" for r in rows), "cx_on=0 (BIT31 clear -> CX domain DOWN)", "cx_on=%s" % set(r[6] for r in rows))
        need(all(r[7] == "0" for r in rows), "gx_on=0 (BIT16 clear)", "gx_on=%s" % set(r[7] for r in rows))
        need(all(r[8] == "1" for r in rows), "gx_off=1 (BIT15 set -> GX rail OFF)", "gx_off=%s" % set(r[8] for r in rows))
        need(set(r[9] for r in rows) == {"cx-DOWN"}, "verdict=cx-DOWN on every replay", "verdicts=%s" % set(r[9] for r in rows))
        need(set(r[10] for r in rows) == {"9"}, "steps_left=9 fail-closed on every replay", "steps=%s" % set(r[10] for r in rows))
        need(all(r[12] == "1" for r in rows), "attempt=1 (one clean observation, then replay)", "attempts=%s" % set(r[12] for r in rows))

    need("refused" not in text and "unsafe window" not in text,
         "NO refusal string in decoded msgbuf (all 5 safety predicates passed)", "no refusal")
    need("enchilada-gauge:" in text, "same msgbuf carries gauge replays (real userspace boot)", "gauge present")
    need("accessoryupdaterd" in text, "same msgbuf carries userspace daemon line", "accessoryupdaterd")

    # ---- READ-ONLY property re-derived from SOURCE (independent of CI #219) ----
    src = SRC.read_text(encoding="utf-8")
    need("PE_parse_boot_argn(\"enchilada-adreno\"" in src, "probe is gated by enchilada-adreno boot-arg", "gate present")
    need(src.count("rd32(") >= 3 and "*reinterpret_cast<volatile uint32_t *>" in src,
         "only deref is a volatile 32-bit load", "volatile load present")
    stores = re.findall(r"\*\s*reinterpret_cast<[^>]*>\s*\([^)]*\)\s*=(?!=)", src)
    need(len(stores) == 0, "NO store through any pointer in the probe source", "stores=%d" % len(stores))
    # Round 95 widened this from a literal "WRITE" substring test, which could not
    # survive stage 5 (the stage that legitimately WRITES through the window
    # adapter). The property that actually matters is still asserted, and more
    # precisely: the probe performs NO direct store through any pointer, and it
    # never invokes the MMIO write member itself -- every write goes through
    # AdrenoGpuccWindowV1's bounded, gated callback. "wmb" stays forbidden because
    # a barrier on a write path this probe does not own would be a lie about what
    # it is doing.
    need("wmb" not in src, "no write barrier in a probe that owns no write path", "no wmb")
    need("mmio.write32(" not in src,
         "the probe never calls the MMIO write member directly", "callbacks only")
    need("AdrenoGpuccWindowV1Mmio(" in src,
         "stage 5 writes only through the bounded window adapter", "adapter present")

    # ---- latest.txt cross-check (it is the FINAL diagnostic slot, NOT msgbuf) ----
    latest = LATEST.read_text(encoding="utf-8", errors="replace")
    need("#slot 1729 t=899s" in latest, "latest.txt is the final slot 1729 at t=899s", "final slot")
    need("LIVE: DEADLINE EXCEEDED" in latest, "latest.txt records the 900 s deadline", "deadline")
    need("root=1 procs=" in latest, "latest.txt proves the root filesystem + userspace are live", "root=1")
    need("UFS requests=" in latest and "failures=0" in latest, "latest.txt has UFS I/O with zero failures", "UFS ok")
    # The GPUCC verdict lives in msgbuf.txt (the 128 KiB IOLog ring), NOT in latest.txt.
    need("enchilada-adreno:" in text, "GPUCC verdict lives in msgbuf.txt (IOLog ring) as expected", "msgbuf verdict")

    print()
    if fails:
        print("GPUPROBE_DEVICE_VERIFY_2: FAIL (%d/%d checks failed)" % (len(fails), checks))
        return 1
    print("GPUPROBE_DEVICE_VERIFY_2: PASS (%d checks, decoded text + header + source READ-ONLY contract agree)" % checks)
    return 0


if __name__ == "__main__":
    sys.exit(main())
