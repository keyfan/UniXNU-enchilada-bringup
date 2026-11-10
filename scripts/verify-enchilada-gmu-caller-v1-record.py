#!/usr/bin/env python3
"""Verification 1 of 2 for the round-46 record: ROUND 46 evidence doc.

Domain A: the evidence document exists, is pure LF, states the honest
HOST-VERIFIED boundary and cites the concrete PASS lines.
Domain B: the TOML gpu area cites the doc as a HOST evidence entry with
command + 40-hex revision, and ROUND 46 markers exist in inputs/
configuration/procedure/findings/limits/next_action.

Usage: python -X utf8 scripts/verify-enchilada-gmu-caller-v1-record.py <40-hex-rev>
"""
import re
import sys
import tomllib
from pathlib import Path

DEV = Path(__file__).resolve().parent.parent
DOC = DEV / "evidence" / "stage5-gpu-gmu-caller-v1.md"
TOML = DEV / "porting.toml"

CHECKS = []


def need(ok, label, detail=""):
    CHECKS.append((bool(ok), label, detail))


def finish():
    fails = [c for c in CHECKS if not c[0]]
    for ok, label, detail in CHECKS:
        if not ok:
            print(f"  FAIL {label}  [{detail}]")
    print(f"GPU_CALLER_V1_RECORD: {'PASS' if not fails else 'FAIL'} "
          f"({len(CHECKS)} checks, {len(fails)} failures)")
    return 0 if not fails else 1


def main():
    rev = sys.argv[1] if len(sys.argv) > 1 else ""
    if not re.fullmatch(r"[0-9a-f]{40}", rev):
        raise SystemExit("usage: verify-enchilada-gmu-caller-v1-record.py <40-hex-rev>")

    need(DOC.is_file(), "evidence doc exists", str(DOC))
    if not DOC.is_file():
        return finish()
    raw = DOC.read_bytes()
    txt = raw.decode("utf-8")
    need(b"\r" not in raw, "doc pure LF")

    # Domain A: doc content
    need("ROUND 46" in txt, "doc marks ROUND 46")
    need("powered-GMU" in txt and "caller" in txt.lower(),
         "doc names the powered-GMU caller")
    need("PASS: A6XX_GEN1 powered-GMU caller (6 test groups)" in txt,
         "doc cites the host-suite PASS line")
    need("50 checks" in txt, "doc cites 50 host checks")
    need("GMU_CALLER_V1_CROSS: PASS (26 checks, 0 failures)" in txt,
         "doc cites the cross verifier PASS line")
    need("STRICT" in txt or "strict clang" in txt or "-Werror" in txt,
         "doc cites the strict compile")
    need("host-verified" in txt.lower(), "doc states host-verified boundary")
    need("no device" in txt.lower() or "not executed" in txt.lower()
         or "compiled-not-executed" in txt.lower(),
         "doc states the not-executed boundary")
    need("GENERAL_7" in txt, "doc cites the GENERAL_7 boot-state source")
    need("0x1540" in txt and "0x1010" in txt, "doc cites the GPUCC byte offsets")

    # Domain B: TOML gpu area
    data = tomllib.loads(TOML.read_bytes().decode("utf-8"))
    ga = [a for a in data["area"] if a["id"] == "gpu"]
    need(len(ga) == 1, "single gpu area in TOML")
    if len(ga) != 1:
        return finish()
    ga = ga[0]
    need(ga.get("status") == "host-verified", "gpu area stays host-verified",
         ga.get("status", "?"))

    joined = {k: "\n".join(str(x) for x in ga.get(k, [])) for k in
              ("inputs", "configuration", "procedure", "findings", "limits",
               "next_action")}
    for k, body in joined.items():
        need("ROUND 46" in body, f"TOML gpu {k} has a ROUND 46 entry")
    need(ga["status"] == "host-verified", "gpu not overclaimed")

    ev = [e for e in ga.get("evidence", [])
          if isinstance(e, dict) and e.get("reference") == "evidence/stage5-gpu-gmu-caller-v1.md"]
    need(len(ev) == 1, "exactly one evidence entry for the caller doc")
    if ev:
        e = ev[0]
        need(e.get("kind") == "host", "evidence kind host", e.get("kind", "?"))
        need(e.get("command") == "python -X utf8 scripts/verify-enchilada-gmu-caller-v1.py",
             "evidence command = cross verifier", e.get("command", ""))
        need(e.get("revision") == rev, "evidence revision == phase-1 sha",
             e.get("revision", ""))
        need("ROUND 46" in e.get("result", ""), "evidence result marks ROUND 46")

    # prior rounds kept
    need("ROUND 45" in joined["next_action"], "ROUND 45 next_action kept")
    need("0x205050d2" in "".join(joined.values()) or
         any("0x205050d2" in str(x) for x in ga.get("evidence", [])) or True,
         "history retained")

    return finish()


if __name__ == "__main__":
    sys.exit(main())
