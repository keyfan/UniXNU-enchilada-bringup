"""Round 30: prove CI run 42726356348 actually BUILT and LINKED the A630
archive, from two INDEPENDENT domains. Exit non-zero on any failed check.

  Domain A - the -logs artifact: the per-archive build log for
             build-enchilada-adreno.py must print the PASS line with 5 objects,
             and every archive log must be PASS (no FAIL/Error).
  Domain B - the -kernel artifact: kernel.macho (a Mach-O 64 image) must
             contain the linked symbol _enchilada_adreno_poll, the
             enchilada-adreno boot-arg, the GPUCC verdict strings and the
             libEnchiladaAdreno.a marker. A successful link is the decisive
             proof that -force_load took effect (an unloaded archive leaves
             no such symbol in the image).

Usage: python -X utf8 scripts/verify-ci-217-adreno.py [run_id]
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
RUN = sys.argv[1] if len(sys.argv) > 1 else "42726356348"
REPO = "keyfan/unixnu-build"
TOKEN = (WORK / "github-token.txt").read_text().strip()
BASE = "https://api.github.com/repos/" + REPO + "/actions"
CACHE = WORK / "tmp"
CACHE.mkdir(parents=True, exist_ok=True)
LOGS_ZIP = CACHE / ("ci" + RUN + "-logs.zip")
KERNEL_ZIP = CACHE / ("ci" + RUN + "-kernel.zip")


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
        m = re.fullmatch(r"PASS: libEnchiladaAdreno\.a \(5 objects, sha256 ([0-9a-f]{16})\)", atext)
        ck("adreno archive PASS line with 5 objects", m is not None, repr(atext))
        if m:
            print("ARCHIVE_SHA256_16", m.group(1))

        # The 23 archive builders use DIFFERENT success formats: the
        # build-enchilada-* scripts print 'PASS: lib*.a (...)' except dwc3
        # ('... passed'), corecrypto prints '529 kernel C sources; 0 failures',
        # arm-driver prints '... passed'. There are 9 build-enchilada- logs
        # because storage + verified-root are built but EXEMPT from
        # -force_load (see check-enchilada-archives-linked.py).
        # So: every enchilada log needs a success marker, no hard failures
        # anywhere, and the adreno log must be the exact PASS line.
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

        # the kernel build log must not report a link failure
        klog = z.read("enchilada/logs/kernel.log").decode("utf-8", "replace")
        ck("kernel build log non-trivial", len(klog) > 100000, str(len(klog)) + " chars")
        ck("no 'Undefined symbols' in kernel log",
           "Undefined symbols" not in klog)

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

    # The decisive link evidence: the weak_import is DEFINED by the archive.
    n_sym = b.count(b"_enchilada_adreno_poll")
    ck("_enchilada_adreno_poll in image symbol table", n_sym == 1, "count=%d" % n_sym)
    i = b.find(b"_enchilada_adreno_poll")
    if i >= 0:
        print("SYMBOL_CONTEXT", b[max(0, i - 60):i + 90].decode("latin1").replace("\x00", "|"))

    ck("enchilada_adreno_poll referenced (>=2)", b.count(b"enchilada_adreno_poll") >= 2,
       "count=%d" % b.count(b"enchilada_adreno_poll"))
    ck("sibling _enchilada_smmu_poll also present (monitor loop intact)",
       b.count(b"_enchilada_smmu_poll") >= 1,
       "count=%d" % b.count(b"_enchilada_smmu_poll"))

    # Runtime strings from the probe shim, linked verbatim.
    for needle, label in [
        (b"enchilada-adreno", "boot-arg"),
        (b"enchilada-adreno: %s (replay t=%u attempt=%u)", "1 Hz replay format"),
        (b"an unsafe window is a port bug", "safety-gate refusal string"),
        (b"GPUCC", "GPUCC verdict label"),
        (b"cx_on", "CX domain-state label"),
        (b"libEnchiladaAdreno", "archive name in image"),
    ]:
        ck("linked string: " + label, b.count(needle) >= 1, "count=%d" % b.count(needle))

    # The four freestanding C modules' symbols are linked too.
    for sym in [b"adreno_domain_", b"adreno_power_", b"adreno_board_", b"adreno_catalog_"]:
        ck("linked module prefix " + sym.decode(), b.count(sym) >= 1,
           "count=%d" % b.count(sym))

    failed = [n for n, ok, _ in checks if not ok]
    print("CHECKS", len(checks), "PASS", len(checks) - len(failed), "FAIL", len(failed))
    if failed:
        print("FAILED", failed)
        raise SystemExit("CI217_ADRENO_VERIFY: FAIL")
    print("CI217_ADRENO_VERIFY: PASS (%d checks, two independent artifacts agree)" % len(checks))


if __name__ == "__main__":
    main()
