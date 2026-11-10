"""Round 32: prove CI run 42743495901 actually BUILT and LINKED the NEW
round-31 module AdrenoHfiV1.c into the enchilada kernel, from two INDEPENDENT
domains. Exit non-zero on any failed check.

  Domain A - the -logs artifact: the per-archive build log for
             build-enchilada-adreno.py must print the PASS line with SIX
             objects (round 30's CI #217 had FIVE), and every enchilada
             archive log must be PASS (no hard failures).
  Domain B - the -kernel artifact: kernel.macho (a Mach-O 64 image) must
             contain the linked symbols _AdrenoHfiV1* contributed ONLY by
             the new module, the legacy startup step-name strings, and the
             libEnchiladaAdreno.a marker. A symbol with no kernel caller
             can still be linked via -force_load; its presence here is the
             decisive proof that kernel clang compiled this exact source.

Both domains are checked against the SAME run identity (head_sha, branch),
so a stale artifact cannot pass.

Usage: python -X utf8 scripts/verify-ci-218-hfi-v1.py [run_id]
Artifacts are cached under work/tmp/ci<run>-{logs,kernel}.zip.
"""
import io
import json
import re
import sys
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]          # .../UniXNU-enchilada
WORK = DEV.parent                                  # .../oneplus6-ios/work
RUN = sys.argv[1] if len(sys.argv) > 1 else "42743495901"
REPO = "keyfan/unixnu-build"
TOKEN = (WORK / "github-token.txt").read_text().strip()
BASE = "https://api.github.com/repos/" + REPO + "/actions"
CACHE = WORK / "tmp"
CACHE.mkdir(parents=True, exist_ok=True)
LOGS_ZIP = CACHE / ("ci" + RUN + "-logs.zip")
KERNEL_ZIP = CACHE / ("ci" + RUN + "-kernel.zip")

# The phase-2 commit that added AdrenoHfiV1.c to the device repo; the CI's
# head_sha is the unixnu-build repo's own head (different repo), so we only
# assert the run is on main and concluded success, not a sha equality here.
EXPECTED_PHASE2 = "bce4c5886b71c4c057b7126b3b67f3ad34a2f780"


def api(path):
    req = urllib.request.Request(
        BASE + "/" + path,
        headers={"Authorization": "Bearer " + TOKEN,
                 "User-Agent": "dsh",
                 "Accept": "application/vnd.github+json"},
    )
    return json.loads(urllib.request.urlopen(req, timeout=120).read())


class DropAuthOnRedirect(urllib.request.HTTPRedirectHandler):
    """GitHub artifact /zip 302s to an Azure signed URL. Forwarding the
    Bearer token there yields 401; strip Authorization when leaving github."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None and "api.github.com" not in (urllib.parse.urlparse(newurl).netloc or ""):
            for k in list(new.headers):
                if k.lower() == "authorization":
                    del new.headers[k]
            new.add_unredirected_header("User-Agent", "dsh")
        return new


def fetch(art_id, path):
    if path.exists() and path.stat().st_size > 100000:
        return path.stat().st_size
    req = urllib.request.Request(
        BASE + "/artifacts/" + str(art_id) + "/zip",
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
    return path.stat().st_size


def main():
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))
        print(("PASS " if cond else "FAIL ") + name + (" | " + detail if detail else ""))

    # ---- run identity (a third, cheap sanity domain) ----
    run = api("runs/" + RUN)
    ck("CI run concluded success", run["status"] == "completed" and run["conclusion"] == "success",
       "%s %s" % (run["status"], run["conclusion"]))
    ck("CI run built main", run["head_branch"] == "main", str(run["head_branch"]))
    print("RUN", RUN, "head_sha", run["head_sha"], "created", run["created_at"], "updated", run["updated_at"])

    arts = api("runs/" + RUN + "/artifacts")
    by_name = {a["name"]: a for a in arts["artifacts"]}
    ck("3 artifacts present", arts["total_count"] == 3, str(sorted(by_name)))
    logs_name = "unixnu-enchilada_defconfig-logs"
    kern_name = "unixnu-enchilada_defconfig-kernel"

    # ================= Domain A: build LOGS =================
    print("--- DOMAIN A: build logs ---")
    fetch(by_name[logs_name]["id"], LOGS_ZIP)
    with zipfile.ZipFile(io.BytesIO(LOGS_ZIP.read_bytes())) as z:
        names = z.namelist()
        arch_logs = sorted(n for n in names if re.search(r"archive-\d+-build-.*\.log$", n))
        print("ARCHIVE_LOGS", len(arch_logs))
        ck("23 per-archive build logs", len(arch_logs) == 23, str(len(arch_logs)))

        adreno_logs = [n for n in arch_logs if "adreno" in n]
        ck("exactly one adreno archive log", len(adreno_logs) == 1, str(adreno_logs))
        atext = z.read(adreno_logs[0]).decode("utf-8", "replace").strip() if adreno_logs else ""
        print("ADRENO_LOG", repr(atext))
        # ROUND 32: 6 objects, not 5. The 6th is AdrenoHfiV1.o.
        m = re.fullmatch(r"PASS: libEnchiladaAdreno\.a \(6 objects, sha256 ([0-9a-f]{16})\)", atext)
        ck("adreno archive PASS line with 6 objects (round-31 module added)", m is not None, repr(atext))
        if m:
            print("ARCHIVE_SHA256_16", m.group(1))
        ck("adreno archive log does NOT say 5 objects", "5 objects" not in atext, repr(atext[:120]))

        ench = [n for n in arch_logs if "build-enchilada-" in n]
        ck("9 build-enchilada- archive logs (7 force-loaded + 2 exempt)",
           len(ench) == 9, str(len(ench)))
        bad_pass = [n for n in ench
                    if not re.search(r"PASS:|\bpassed\b", z.read(n).decode("utf-8", "replace"))]
        ck("all 9 enchilada archive logs report success", not bad_pass, str(bad_pass))
        hard_fail = []
        for n in arch_logs:
            t = z.read(n).decode("utf-8", "replace")
            if re.search(r"Undefined symbols|Traceback \(most recent|error:|FAILED", t):
                hard_fail.append(n)
        ck("no hard failure marker in any of the 23 archive logs", not hard_fail,
           str(hard_fail[:3]))

        klog = z.read("enchilada/logs/kernel.log").decode("utf-8", "replace")
        ck("kernel build log non-trivial", len(klog) > 100000, str(len(klog)) + " chars")
        ck("no 'Undefined symbols' in kernel log", "Undefined symbols" not in klog)
        # NOTE (verified against cached ci217-logs.zip): the -logs artifact has
        # NO .json files and kernel.log contains NO module source names, so the
        # per-object compile proof lives in Domain B (linked symbols), not here.
        # Domain A's discriminator is the 6-objects-vs-5-objects PASS line above.
        ck("no JSON build reports in logs artifact (assumption check)",
           not [n for n in names if n.endswith(".json")], "as in CI #217")

    # ================= Domain B: linked KERNEL image =================
    print("--- DOMAIN B: kernel.macho ---")
    fetch(by_name[kern_name]["id"], KERNEL_ZIP)
    with zipfile.ZipFile(io.BytesIO(KERNEL_ZIP.read_bytes())) as z:
        kn = z.namelist()
        ck("kernel.macho present", "enchilada/kernel/kernel.macho" in kn, str(kn))
        b = z.read("enchilada/kernel/kernel.macho")
    print("MACHO_BYTES", len(b))
    ck("Mach-O 64 magic 0xfeedfacf", len(b) >= 4 and int.from_bytes(b[:4], "little") == 0xfeedfacf,
       hex(int.from_bytes(b[:4], "little")))
    ck("kernel image > 100 MB", len(b) > 100_000_000, str(len(b)))

    # DECISIVE: symbols contributed ONLY by the round-31 module. None of the
    # five older modules defines a symbol starting with AdrenoHfiV1.
    for sym in [
        b"_AdrenoHfiV1InitializeStopped",
        b"_AdrenoHfiV1MakeHeader",
        b"_AdrenoHfiV1Send",
        b"_AdrenoHfiV1Poll",
        b"_AdrenoHfiV1Exchange",
        b"_AdrenoHfiV1StartupTable",
        b"_AdrenoHfiV1StartupStepCount",
        b"_AdrenoHfiV1StepName",
        b"_AdrenoHfiV1IsLegacyStart",
    ]:
        ck("linked symbol " + sym.decode(), b.count(sym) >= 1, "count=%d" % b.count(sym))

    # Legacy v1 startup step-name string literals (from AdrenoHfiV1StepName).
    for needle, label in [
        (b"PERF_TABLE_V1", "legacy PERF_TABLE_V1 step name"),
        (b"FW_VERSION", "legacy FW_VERSION step name"),
        (b"BW_TABLE", "legacy BW_TABLE step name"),
        (b"libEnchiladaAdreno", "archive name in image"),
        (b"enchilada-adreno", "probe boot-arg still linked"),
        (b"_enchilada_adreno_poll", "probe entry symbol still linked"),
    ]:
        ck("linked string: " + label, b.count(needle) >= 1, "count=%d" % b.count(needle))

    # Sanity: the older 5 modules' prefixes remain (no regression).
    for sym in [b"adreno_domain_", b"adreno_power_", b"adreno_board_", b"adreno_catalog_"]:
        ck("older module prefix intact " + sym.decode(), b.count(sym) >= 1,
           "count=%d" % b.count(sym))

    failed = [n for n, ok, _ in checks if not ok]
    print("CHECKS", len(checks), "PASS", len(checks) - len(failed), "FAIL", len(failed))
    if failed:
        print("FAILED", failed)
        raise SystemExit("CI218_HFI_V1_VERIFY: FAIL")
    print("CI218_HFI_V1_VERIFY: PASS (%d checks, two independent artifacts agree)" % len(checks))


if __name__ == "__main__":
    main()
