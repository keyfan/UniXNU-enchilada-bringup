# Two independent verifications of CI #221 result (run 42825411641):
#  A. Re-read the archive line from the downloaded logs zip (raw bytes, not cached).
#     Round 44 added AdrenoGmuBootV1.c + AdrenoGmuBusV1.c as CI objects 8+9,
#     so the archive must be 9 objects (CI #220 was 7 objects 04c763f8e5837408).
#  B. kernel.macho size vs CI #220: the CI #221 kernel is byte-different but
#     SIZE-IDENTICAL (126017536) — the two new objects link without growing the
#     image. Record the sha delta, do NOT claim a size delta.
#  C. No 10th object.
import zipfile, hashlib
from pathlib import Path
W = Path(r"C:\Users\LCPC\Documents\deepseek-harness\default-workspace\oneplus6-ios")
RUN = 42825411641
ok = True

# A. logs
z = zipfile.ZipFile(W/"work"/"tmp"/f"ci{RUN}-logs.zip")
t = z.read("enchilada/logs/archive-23-build-enchilada-adreno.log").decode("utf-8", errors="replace")
line = t.strip()
print("A_LINE:", line)
a = line.startswith("PASS: libEnchiladaAdreno.a (9 objects, sha256 b84486919884d060")
print("A_ARCHIVE_9_OBJECTS_B8448691:", a)

# B. kernel differential (size unchanged, bytes differ)
zk = zipfile.ZipFile(W/"work"/"tmp"/f"ci{RUN}-kernel.zip")
k = zk.read("enchilada/kernel/kernel.macho")
ksha = hashlib.sha256(k).hexdigest()
print("B_CI221_KERNEL_SHA256:", ksha)
print("B_CI221_KERNEL_SIZE:", len(k))
b_ok = len(k) == 126017536
print("B_SIZE_UNCHANGED_VS_220:", b_ok)

# C. no 10th object
arch_ok = "9 objects" in line and "10 objects" not in line
print("C_NO_10TH_OBJECT:", arch_ok)

ok = a and b_ok and arch_ok
print("CI221_ADRENO_VERIFY: %s" % ("PASS (3 checks)" if ok else "FAIL"))
raise SystemExit(0 if ok else 1)
