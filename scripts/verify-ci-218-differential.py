"""Round 32: SECOND, independent verification of the CI #218 GPU evidence.

Unlike verify-ci-218-hfi-v1.py (which proves the new module was BUILT and
LINKED from two artifact domains of ONE run), THIS verifier proves the evidence
is DISCRIMINATING by a differential comparison against the known-negative
baseline CI #217 (42726356348, round 30, FIVE objects, no AdrenoHfiV1.c):

  * CI #217 MUST say 5 objects and contain ZERO _AdrenoHfiV1* symbols.
  * CI #218 MUST say 6 objects and contain ALL NINE _AdrenoHfiV1* symbols.
  * If the same checks passed on both runs, the 'evidence' would be vacuous.

Both runs' artifacts are fetched through the GitHub Actions API (with the
Azure-redirect auth-strip handler) and cached under work/tmp/. Exit non-zero
on any failed check.

Usage: python -X utf8 scripts/verify-ci-218-differential.py [old_run] [new_run]
Defaults: old_run=42726356348 (#217), new_run=42743495901 (#218).
"""
import io
import json
import re
import sys
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
WORK = DEV.parent
OLD = sys.argv[1] if len(sys.argv) > 1 else "42726356348"
NEW = sys.argv[2] if len(sys.argv) > 2 else "42743495901"
REPO = "keyfan/unixnu-build"
TOKEN = (WORK / "github-token.txt").read_text().strip()
BASE = "https://api.github.com/repos/" + REPO + "/actions"
CACHE = WORK / "tmp"
CACHE.mkdir(parents=True, exist_ok=True)


def api(path):
    req = urllib.request.Request(
        BASE + "/" + path,
        headers={"Authorization": "Bearer " + TOKEN,
                 "User-Agent": "dsh",
                 "Accept": "application/vnd.github+json"},
    )
    return json.loads(urllib.request.urlopen(req, timeout=120).read())


class DropAuthOnRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None and "api.github.com" not in (urllib.parse.urlparse(newurl).netloc or ""):
            for k in list(new.headers):
                if k.lower() == "authorization":
                    del new.headers[k]
            new.add_unredirected_header("User-Agent", "dsh")
        return new


def artifacts(run, tag):
    """Return (logs_path, kernel_path), fetching via API into stable cache names."""
    logs = CACHE / ("ci" + tag + "-logs.zip")
    kern = CACHE / ("ci" + tag + "-kernel.zip")
    arts = api("runs/" + run + "/artifacts")["artifacts"]
    by_name = {a["name"]: a for a in arts}
    want = {"logs": "unixnu-enchilada_defconfig-logs",
            "kernel": "unixnu-enchilada_defconfig-kernel"}
    out = {}
    for kind, path in [("logs", logs), ("kernel", kern)]:
        if path.exists() and path.stat().st_size > 100000:
            out[kind] = path
            continue
        aid = by_name[want[kind]]["id"]
        req = urllib.request.Request(
            BASE + "/artifacts/" + str(aid) + "/zip",
            headers={"Authorization": "Bearer " + TOKEN,
                     "User-Agent": "dsh",
                     "Accept": "application/vnd.github+json"},
        )
        opener = urllib.request.build_opener(DropAuthOnRedirect())
        with opener.open(req, timeout=900) as r, open(path, "wb") as f:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
        out[kind] = path
    return out["logs"], out["kernel"]


def adreno_log(zpath):
    with zipfile.ZipFile(io.BytesIO(zpath.read_bytes())) as f:
        names = [n for n in f.namelist() if re.search(r"archive-\d+-build-enchilada-adreno\.log$", n)]
        return f.read(names[0]).decode("utf-8", "replace").strip()


def kern_bytes(zpath):
    with zipfile.ZipFile(io.BytesIO(zpath.read_bytes())) as f:
        n = [x for x in f.namelist() if x.endswith("kernel.macho")][0]
        return f.read(n)


def main():
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))
        print(("PASS " if cond else "FAIL ") + name + (" | " + detail if detail else ""))

    print("OLD_RUN", OLD, "NEW_RUN", NEW)
    # Cheap identity sanity: both runs concluded success on main.
    for tag, run in [("old", OLD), ("new", NEW)]:
        r = api("runs/" + run)
        ck(tag + " run concluded success",
           r["status"] == "completed" and r["conclusion"] == "success",
           "%s %s" % (r["status"], r["conclusion"]))

    old_l, old_k = artifacts(OLD, "217")          # reuses work/tmp/ci217-{logs,kernel}.zip
    new_l, new_k = artifacts(NEW, NEW)            # reuses work/tmp/ci<run>-{logs,kernel}.zip
    print("OLD_LOGS", old_l.name, old_l.stat().st_size, "OLD_KERN", old_k.name, old_k.stat().st_size)
    print("NEW_LOGS", new_l.name, new_l.stat().st_size, "NEW_KERN", new_k.name, new_k.stat().st_size)

    ol, nl = adreno_log(old_l), adreno_log(new_l)
    print("OLD_ADRENO_LOG", repr(ol))
    print("NEW_ADRENO_LOG", repr(nl))

    print("--- differential: 5 vs 6 objects ---")
    ck("CI #217 baseline says 5 objects", "5 objects" in ol and "6 objects" not in ol, repr(ol))
    ck("CI #218 says 6 objects", "6 objects" in nl and "5 objects" not in nl, repr(nl))
    m5 = re.fullmatch(r"PASS: libEnchiladaAdreno\.a \(5 objects, sha256 ([0-9a-f]{16})\)", ol)
    m6 = re.fullmatch(r"PASS: libEnchiladaAdreno\.a \(6 objects, sha256 ([0-9a-f]{16})\)", nl)
    ck("both PASS lines well-formed", m5 is not None and m6 is not None)
    if m5 and m6:
        print("  OLD_ARCHIVE_SHA", m5.group(1), "NEW_ARCHIVE_SHA", m6.group(1))
        ck("archive sha CHANGED between runs", m5.group(1) != m6.group(1))

    print("--- differential: linked HFI v1 symbols absent in old, present in new ---")
    ob, nb = kern_bytes(old_k), kern_bytes(new_k)
    print("  OLD_MACHO", len(ob), "NEW_MACHO", len(nb))
    ck("both Mach-O magic 0xfeedfacf",
       int.from_bytes(ob[:4], "little") == 0xfeedfacf and int.from_bytes(nb[:4], "little") == 0xfeedfacf)
    syms = [b"_AdrenoHfiV1InitializeStopped", b"_AdrenoHfiV1MakeHeader", b"_AdrenoHfiV1Send",
            b"_AdrenoHfiV1Poll", b"_AdrenoHfiV1Exchange", b"_AdrenoHfiV1StartupTable",
            b"_AdrenoHfiV1StartupStepCount", b"_AdrenoHfiV1StepName", b"_AdrenoHfiV1IsLegacyStart"]
    for s in syms:
        co, cn = ob.count(s), nb.count(s)
        ck("symbol " + s.decode() + ": old=0 new>=1", co == 0 and cn >= 1, "old=%d new=%d" % (co, cn))
    for s, label in [(b"PERF_TABLE_V1", "legacy step name"), (b"FW_VERSION", "legacy FW_VERSION")]:
        co, cn = ob.count(s), nb.count(s)
        ck(label + ": old=0 new>=1", co == 0 and cn >= 1, "old=%d new=%d" % (co, cn))

    print("--- non-regression: older symbols present in BOTH ---")
    for s in [b"_adreno_domain_", b"_adreno_power_", b"_adreno_board_", b"_adreno_catalog_",
              b"_enchilada_adreno_poll"]:
        ck("older prefix " + s.decode() + " in both", ob.count(s) >= 1 and nb.count(s) >= 1,
           "old=%d new=%d" % (ob.count(s), nb.count(s)))

    ck("linked image grew (new object added)", len(nb) > len(ob), "%d > %d" % (len(nb), len(ob)))

    failed = [n for n, ok, _ in checks if not ok]
    print("CHECKS", len(checks), "PASS", len(checks) - len(failed), "FAIL", len(failed))
    if failed:
        print("FAILED", failed)
        raise SystemExit("CI218_DIFFERENTIAL_VERIFY: FAIL")
    print("CI218_DIFFERENTIAL_VERIFY: PASS (%d checks, new evidence discriminates against the CI #217 baseline)" % len(checks))


if __name__ == "__main__":
    main()
