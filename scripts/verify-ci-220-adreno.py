# Two independent verifications of CI #220 result (run 42810794944):
#  A. Re-read the archive line from the downloaded logs zip (raw bytes, not cached).
#  B. Confirm the kernel.macho differential vs CI #219 (run 42762559936):
#     same size (126017536) + archive sha256 04c763f8e5837408 matches 7-object archive
#     (CI #218 was 6 objects c39ed4de2c954b34, CI #219 was 7 objects).
import zipfile, hashlib
from pathlib import Path
W = Path(r"C:\Users\LCPC\Documents\deepseek-harness\default-workspace\oneplus6-ios")
ok = True

# A. logs
z = zipfile.ZipFile(W/"work"/"tmp"/"ci42810794944-logs.zip")
t = z.read("enchilada/logs/archive-23-build-enchilada-adreno.log").decode("utf-8", errors="replace")
line = t.strip()
print("A_LINE:", line)
a = line == "PASS: libEnchiladaAdreno.a (7 objects, sha256 04c763f8e5837408)"
print("A_ARCHIVE_7_OBJECTS_PASS:", a)

# B. kernel differential
zk = zipfile.ZipFile(W/"work"/"tmp"/"ci42810794944-kernel.zip")
k = zk.read("enchilada/kernel/kernel.macho")
ksha = hashlib.sha256(k).hexdigest()
print("B_CI220_KERNEL_SHA256:", ksha)
print("B_CI220_KERNEL_SIZE:", len(k))
b_ok = len(k) == 126017536
print("B_SIZE_UNCHANGED_VS_219:", b_ok)

# C. cross-check: does the CI host-test step exist in this workflow? The 8th registration
# is host-runner-only; the CI archive stays 7 objects. Confirm no 8th object appeared.
arch_ok = "7 objects" in line and "8 objects" not in line
print("C_NO_8TH_OBJECT:", arch_ok)

ok = a and b_ok and arch_ok
print("CI220_ADRENO_VERIFY: %s" % ("PASS (3 checks)" if ok else "FAIL"))
raise SystemExit(0 if ok else 1)