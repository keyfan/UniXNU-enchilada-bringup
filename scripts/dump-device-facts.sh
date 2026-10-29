#!/usr/bin/env bash
# Collect the OnePlus 6 (enchilada) facts needed by UniXNU-enchilada/board.toml: the userdata
# GPT geometry, the stock boot image header, the stock device tree and the CPU topology.
#
# Run on the PC with the phone connected and USB debugging on. Nothing on the phone is modified.
# The sysfs reads need no root; the boot partition read does, so a rooted phone (or `adb root` on
# a userdebug build) is required for that one line. Everything else works on a stock system.
#
#   bash scripts/dump-device-facts.sh                     # the phone on the USB cable
#   ANDROID_SERIAL=xxx bash scripts/dump-device-facts.sh  # pick one of several adb devices
#   bash scripts/dump-device-facts.sh --boot boot_a.img   # skip the phone, parse an image
#   bash scripts/dump-device-facts.sh --force             # ignore the model guard (never needed)
#
# Output: ./enchilada-facts/ with the facts, boot_a.img and the parsed summary.
set -euo pipefail

OUT=enchilada-facts
FORCE=
BOOT=
while [ $# -gt 0 ]; do
  case $1 in
    --boot) BOOT=${2:-}; shift 2 ;;
    --force) FORCE=1; shift ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
if [ -n "$BOOT" ] && [ ! -s "$BOOT" ]; then
  echo "no such boot image: $BOOT" >&2
  exit 2
fi

mkdir -p "$OUT"
ADB=${ADB:-adb}
HERE=$(cd "$(dirname "$0")" && pwd)

# On Windows, `python3` on PATH is often the Microsoft Store stub: it exists, prints nothing and
# exits 49. Probe the interpreters by running them, and take the first that actually reports a
# usable version. PYTHON from the environment wins.
if [ -z "${PYTHON:-}" ]; then
  for candidate in python3 python; do
    if "$candidate" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 8) else 1)' \
         >/dev/null 2>&1; then
      PYTHON=$candidate
      break
    fi
  done
fi
if [ -z "${PYTHON:-}" ]; then
  echo "no working python3 (need 3.8 or newer); set PYTHON=/path/to/python3" >&2
  exit 1
fi

say() { printf '\n== %s ==\n' "$1"; }

# The guard matters more than it looks. adb happily lists a wirelessly connected tablet or a
# different phone, and every number this script prints is written into board.toml: a userdata
# geometry from another device means the root image is flashed over somebody else's partition.
# The device tree and the boot header would be wrong in a way no later check can catch.
if [ -n "$BOOT" ]; then
  echo "using the boot image on the command line; the phone is not read at all"
  PHONE=
else
  PHONE=$($ADB shell getprop ro.product.device 2>/dev/null | tr -d '\r') || PHONE=
  if [ -z "$PHONE" ]; then
    echo "no device answers: connect the OnePlus 6, enable USB debugging, and accept the prompt." >&2
    echo "  adb devices -l    # must list an enchilada (or run this with --boot FILE)" >&2
    exit 1
  fi
  if [ "$PHONE" != "enchilada" ] && [ -z "$FORCE" ]; then
    echo "refusing to run: the connected device reports ro.product.device=$PHONE, not enchilada." >&2
    echo "  a phone that is not the OnePlus 6 has a different partition table, so its userdata" >&2
    echo "  geometry must never reach board.toml. Disconnect it, or pass --force if this really" >&2
    echo "  is the phone and the property was renamed by a custom ROM." >&2
    exit 1
  fi
  say "device"
  echo "$PHONE" | tee "$OUT/device.txt"
  $ADB shell getprop ro.product.model | tr -d '\r' | tee "$OUT/model.txt" || true
  $ADB shell getprop ro.build.version.release | tr -d '\r' | tee "$OUT/android.txt" || true
  $ADB shell getprop ro.build.display.id | tr -d '\r' | tee "$OUT/firmware.txt" || true
fi

if [ -n "$PHONE" ]; then
say "userdata geometry (no root: the kernel exposes the start sector in sysfs)"
# /sys/class/block/sdaN/start is in 512-byte sectors; the GPT uses 4096-byte LBAs on this phone
# (lba_bytes in board.toml), so first_lba = start_512 / 8. This is the value that must be right:
# writing the root image one LBA early overwrites the partition in front of userdata.
$ADB shell 'ls -l /dev/block/by-name/userdata' | tr -d '\r' | tee "$OUT/userdata-link.txt" || true
NODE=$($ADB shell 'readlink -f /dev/block/by-name/userdata' | tr -d '\r' | sed 's#.*/##') || true
if [ -n "${NODE:-}" ]; then
  echo "block node: $NODE" | tee -a "$OUT/userdata-link.txt"
  $ADB shell "cat /sys/class/block/$NODE/start" | tr -d '\r' | tee "$OUT/userdata-start-512.txt" || true
  $ADB shell "cat /sys/class/block/$NODE/size"  | tr -d '\r' | tee "$OUT/userdata-size-512.txt"  || true
  if [ -s "$OUT/userdata-start-512.txt" ] && [ -s "$OUT/userdata-size-512.txt" ]; then
    "$PYTHON" - "$OUT" <<'PY'
import pathlib, sys
out = pathlib.Path(sys.argv[1])
start = int((out / 'userdata-start-512.txt').read_text().strip() or -1)
size = int((out / 'userdata-size-512.txt').read_text().strip() or -1)
if start < 0 or size <= 0:
    raise SystemExit('could not read the start sector and size out of sysfs')
if start % 8:
    raise SystemExit(f'userdata starts at 512-byte sector {start}, which is not 4096-aligned')
first = start // 8                      # 512-byte sectors to the 4096-byte LBAs in board.toml
last = first + size // 8 - 1
lines = [
    '[userdata]',
    f'first_lba = {first:#x}',
    f'last_lba = {last:#x}',
    'lba_bytes = 4096',
]
print('\n'.join(lines))
print(f'# {size * 512 / (1 << 30):.2f} GiB')
(out / 'userdata-lba.toml').write_text('\n'.join(lines) + '\n')
PY
  fi
fi
# The primary GPT header says how many entries exist and where they start; useful when the sysfs
# numbers look odd. sgdisk may not be installed, so this is best effort.
$ADB shell "su -c 'sgdisk --print /dev/block/sda'" 2>/dev/null | tr -d '\r' | tee "$OUT/gpt.txt" || true

say "pull boot_a for the boot image header and the stock device tree (needs root)"
if $ADB shell "su -c 'dd if=/dev/block/by-name/boot_a of=/sdcard/boot_a.img'" 2>/dev/null; then
  :
elif $ADB root 2>/dev/null; then
  $ADB shell 'dd if=/dev/block/by-name/boot_a of=/sdcard/boot_a.img' || true
else
  echo "no root: boot_a not readable. Either root the phone, or pass a boot.img with --boot:" >&2
  echo "  $0 --boot path/to/boot.img" >&2
fi
$ADB pull /sdcard/boot_a.img "$OUT/boot_a.img" 2>/dev/null || true

if [ -z "$BOOT" ] && [ -s "$OUT/boot_a.img" ]; then
  BOOT="$OUT/boot_a.img"
fi

say "CPU topology from the running kernel (cross-check cpu_regs)"
$ADB shell 'cat /sys/devices/system/cpu/cpu*/topology/cluster_id' 2>/dev/null \
  | tr -d '\r' | tee "$OUT/cpu-cluster.txt" || true

say "memory size (fill [payload] dram_size: 6 GiB = 0x180000000, 8 GiB = 0x200000000)"
$ADB shell 'cat /proc/meminfo' 2>/dev/null | tr -d '\r' | head -n 3 | tee "$OUT/meminfo.txt" || true
fi   # end of the part that needs the phone

if [ -n "$BOOT" ] && [ -s "$BOOT" ]; then
  say "parse the boot image (header, appended device trees, CPU map, GIC, UFS, splash)"
  # parse-device-dump.py prints the same facts the phone dump would, straight from the image.
  "$PYTHON" "$HERE/parse-device-dump.py" --boot "$BOOT" --out-dir "$OUT" | tee "$OUT/parsed.txt"
else
  say "no boot image: pass --boot FILE, or root the phone so boot_a can be read"
fi

cat <<'EOF'

Done. Fill in, from what this produced:
  1. board.toml [userdata] first_lba / last_lba   <- userdata-lba.toml (sysfs, no root needed)
  2. board.toml [payload]  dram_size              <- meminfo.txt (6 GiB = 0x180000000, 8 GiB = 0x200000000)
  3. board.toml [bootimg.image]                   <- parsed.txt (compare with the current values)
  4. scripts/make-enchilada-adt.py GIC/UFS        <- parsed.txt (should already match)
  5. scripts/check_stock_reserved.py --dtb enchilada-facts/dtb2-*.dtb
EOF
