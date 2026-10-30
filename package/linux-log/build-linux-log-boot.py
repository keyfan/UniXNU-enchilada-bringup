#!/usr/bin/env python3
"""Build the enchilada mainline log-reader boot image.

This is the enchilada counterpart of UniXNU-sheng's package/linux-log/build-linux-log-boot.py,
written against the same documented contract (https://unixnu.com/porting/log-capture):

  * a target kernel + device tree + storage driver + USB device controller + CDC ACM,
  * the log region copied into RAM, never mounted, with the source partition read only,
  * a serial shell on the gadget so the host can pull the raw region,
  * and a way back to the bootloader.

What differs from sheng, and why
--------------------------------
Input image.  sheng reads a prebuilt mainline boot image from its own release and reuses
the kernel region byte for byte.  We do the same, with the postmarketOS enchilada boot
image: it already carries a mainline sdm845 kernel with UFS (ufshcd), dwc3 and configfs
built in, plus the enchilada device tree appended to the kernel region.  Reusing those
bytes verbatim is deliberate -- the region is `gzip(Image)` followed immediately by the
DTB, and the bootloader expects the DTB at the end of the region it loads, so rebuilding
it from parts would only add ways to get that layout wrong.

Return path.  sheng's rebootbl asks for `reboot("bootloader")` and lets its PMIC SDAM
reboot-reason cell (driven by the nvmem-reboot-mode node in its device tree) select
fastboot.  The enchilada device tree in this boot image has no such node -- `reboot` and
`sdam` both appear zero times in its 121,416 bytes -- so that path would restart into
whatever Android the reason cell happens to say.  rebootbl.cpp therefore performs a plain
cold restart, and the host script tells the operator to issue `adb reboot bootloader`.

Usage
-----
    python -X utf8 package/linux-log/build-linux-log-boot.py \
        --boot work/tmp/linux-log/pmos-boot.img \
        --initramfs work/tmp/linux-log/initramfs.cpio \
        --out work/out/linux-log.img
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import struct
import sys
from pathlib import Path

PAGE = 4096
CMDLINE = b"rdinit=/init console=tty0 loglevel=4 consoleblank=0"

# Android boot image v0 header, as laid out in <bootimg.h>: the ten little-endian words
# after the magic, then name[16], cmdline[512], id[8] (the first 20 bytes hold the sha1).
HEADER_WORDS = struct.Struct("<10I")
MAGIC = b"ANDROID!"
NAME_OFF, CMDLINE_OFF, ID_OFF, ID_LEN = 48, 64, 576, 32

MODULES = ("libcomposite", "u_serial", "usb_f_acm")
INITRAMFS_DIRS = ("bin", "dev", "proc", "sys", "run", "tmp", "lib", "lib/modules")

# postmarketOS's busybox is dynamically linked against musl, with
# INTERP = /lib/ld-musl-aarch64.so.1 and NEEDED = libc.musl-aarch64.so.1 (which on musl is
# a symlink to that same loader).  We stage /init with a `#!/bin/busybox sh` shebang, so
# the loader has to be in the initramfs too -- without it the kernel finds no runnable
# init at all.  Take both out of the distribution initramfs rather than guessing at a
# version that matches the binary.
LOADER = "ld-musl-aarch64.so.1"
LOADER_LINK = "libc.musl-aarch64.so.1"


def cpio_newc(entries: list[tuple[str, int, bytes]]) -> bytes:
    """newc archive.  Same encoder as the sheng reader: 13 %08x fields per entry."""
    out = io.BytesIO()
    for name, mode, data in entries:
        raw = name.encode()
        out.write(b"070701")
        out.write(
            f"{0:08x}{mode:08x}{0:08x}{0:08x}{1:08x}{0:08x}{len(data):08x}"
            f"{0:08x}{0:08x}{0:08x}{0:08x}{len(raw) + 1:08x}{0:08x}".encode()
        )
        out.write(raw + b"\0")
        out.write(b"\0" * (-(110 + len(raw) + 1) % 4))
        out.write(data)
        out.write(b"\0" * (-len(data) % 4))
    # Trailer, which cpio requires to know the archive ended cleanly.
    out.write(b"070701" + b"0" * 104)
    out.write(b"TRAILER!!!\0")
    out.write(b"\0" * (-(110 + 11) % 4))
    return out.getvalue()


def walk_cpio(blob: bytes):
    """Yield (name, data) for every entry in a newc archive.

    Used to lift files straight out of the distribution initramfs instead of tracking
    down matching versions elsewhere: whatever busybox needs, it was shipped with.
    """
    pos = 0
    while True:
        pos = blob.find(b"070701", pos)
        if pos < 0:
            return
        fields = blob[pos + 6: pos + 106]
        if len(fields) < 100:
            return
        try:
            size = int(fields[48:56], 16)
            name_len = int(fields[88:96], 16)
        except ValueError:
            pos += 6
            continue
        name_start = pos + 110
        name = blob[name_start: name_start + max(name_len - 1, 0)].decode(errors="replace")
        data_start = name_start + name_len
        data_start += -(110 + name_len) % 4
        if name == "TRAILER!!!":
            return
        yield name, blob[data_start: data_start + size]
        pos = data_start + size


def gather_modules(initramfs: Path, out_dir: Path) -> list[tuple[str, bytes]]:
    """Pull the three gadget modules out of the distribution initramfs and decompress them.

    postmarketOS ships them as .ko.zst inside its own initramfs; sheng instead had them
    next to the reader.  Either way the reader must insmod them in dependency order,
    because libcomposite <- u_serial <- usb_f_acm and none of the three is built in.
    """
    import zstandard

    zstd = zstandard.ZstdDecompressor()
    found: dict[str, bytes] = {}
    for name, data in walk_cpio(initramfs.read_bytes()):
        if name.endswith(".ko.zst"):
            stem = Path(name).name[: -len(".ko.zst")]
            if stem in MODULES:
                found[stem] = zstd.decompress(data, max_output_size=64 << 20)

    missing = [m for m in MODULES if m not in found]
    if missing:
        raise SystemExit(f"initramfs is missing modules: {', '.join(missing)}")

    out_dir.mkdir(parents=True, exist_ok=True)
    result = []
    for m in MODULES:                     # dependency order, as init insmods them
        data = found[m]
        (out_dir / f"{m}.ko").write_bytes(data)
        result.append((m, data))
    return result


def gather_loader(initramfs: Path, out_dir: Path) -> list[tuple[str, int, bytes]]:
    """Take musl's loader out of the distribution initramfs.

    busybox in postmarketOS is dynamically linked, so /init's `#!/bin/busybox sh` only
    works if the loader it names is present.  Sourced from the same distribution image so
    the versions cannot drift apart.

    The archive carries libc.musl-aarch64.so.1 as a *symlink* to ld-musl-aarch64.so.1
    (mode 0120644, 20 bytes of target path), so it has to be re-emitted as a symlink:
    writing those 20 bytes as a regular file would leave NEEDED pointing at a text file.
    """
    wanted = {LOADER, LOADER_LINK}
    found: dict[str, bytes] = {}
    for name, data in walk_cpio(initramfs.read_bytes()):
        base = Path(name).name
        if base in wanted and data:
            found[base] = data

    loader = found.get(LOADER)
    if not loader:
        raise SystemExit(f"initramfs does not carry {LOADER}; busybox would not start")

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / LOADER).write_bytes(loader)
    target = found.get(LOADER_LINK) or LOADER.encode()
    return [(LOADER, 0o100755, loader), (LOADER_LINK, 0o120777, target)]


def build(args: argparse.Namespace) -> Path:
    boot = bytearray(args.boot.read_bytes())
    if boot[:8] != MAGIC:
        raise SystemExit(f"{args.boot}: not an Android boot image")
    (kernel_size, kernel_addr, ramdisk_size, ramdisk_addr, second_size,
     second_addr, tags_addr, page, version, os_version) = HEADER_WORDS.unpack_from(boot, 8)
    if version != 0:
        raise SystemExit(f"{args.boot}: header version {version}, this builder writes v0")

    kernel = bytes(boot[page: page + kernel_size])
    if kernel[:2] != b"\x1f\x8b":
        raise SystemExit("kernel region does not start with gzip; refusing to rebuild it")

    # The DTB has to stay at the tail of the region: the bootloader loads one contiguous
    # chunk and the kernel finds the device tree at its end.  Assert that assumption
    # rather than trusting it, since we reuse the region rather than re-deriving it.
    fdt = kernel.rfind(b"\xd0\x0d\xfe\xed")
    if fdt < 0:
        raise SystemExit("no flattened device tree inside the kernel region")
    fdt_total = struct.unpack_from(">I", kernel, fdt + 4)[0]
    if fdt + fdt_total != kernel_size:
        raise SystemExit(
            f"device tree ends at {fdt + fdt_total}, kernel region is {kernel_size}: "
            "the appended-DTB layout has changed"
        )

    modules = gather_modules(args.initramfs, args.modules_dir)
    loader = gather_loader(args.initramfs, args.modules_dir)
    entries: list[tuple[str, int, bytes]] = []
    for d in INITRAMFS_DIRS:
        entries.append((d, 0o040755, b""))
    entries.append(("dev/console", 0o020600, b""))
    entries.append(("init", 0o100755, Path(args.init).read_bytes()))
    entries.append(("bin/busybox", 0o100755, Path(args.busybox).read_bytes()))
    entries.append(("bin/rebootbl", 0o100755, Path(args.rebootbl).read_bytes()))
    for name, mode, data in loader:
        entries.append((f"lib/{name}", mode, data))
    for name, data in modules:
        entries.append((f"lib/modules/{name}.ko", 0o100644, data))

    cpio = cpio_newc(entries)
    ramdisk = gzip.compress(cpio, 9, mtime=0)

    header = bytearray(page)
    header[:8] = MAGIC
    HEADER_WORDS.pack_into(header, 8, len(kernel), kernel_addr, len(ramdisk),
                           args.ramdisk_addr or ramdisk_addr, 0, 0, tags_addr, page,
                           0, os_version)
    if len(CMDLINE) > 511:
        raise SystemExit("cmdline does not fit the v0 header field")
    header[CMDLINE_OFF: CMDLINE_OFF + len(CMDLINE)] = CMDLINE
    header[NAME_OFF: NAME_OFF + 16] = b"linux-log\0\0\0\0\0\0\0"

    # The id field is a sha1 over each section followed by its own length, exactly as
    # mkbootimg computes it; the bootloader does not check it, but tools display it.
    digest = hashlib.sha1()
    for section in (kernel, ramdisk):
        digest.update(section)
        digest.update(struct.pack("<I", len(section)))
    header[ID_OFF: ID_OFF + 20] = digest.digest()

    def pad(blob: bytes) -> bytes:
        return blob + b"\0" * (-len(blob) % page)

    image = bytes(header) + pad(kernel) + pad(ramdisk)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(image)

    print(f"kernel region  {len(kernel):>10,} B  (gzip Image + {fdt_total:,} B DTB)")
    print(f"initramfs      {len(ramdisk):>10,} B  ({len(cpio):,} B cpio, {len(entries)} entries)")
    print(f"modules        {', '.join(m for m, _ in modules)}")
    print(f"cmdline        {CMDLINE.decode()}")
    print(f"image          {len(image):>10,} B  -> {args.out}")
    print(f"sha256         {hashlib.sha256(image).hexdigest()}")
    return args.out


def main(argv: list[str] | None = None) -> int:
    here = Path(__file__).resolve().parent      # .../work/UniXNU-enchilada/package/linux-log
    work = here.parents[2]                      # .../work (build scratch shared with the tree)
    tmp = work / "tmp" / "linux-log"
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--boot", type=Path, default=tmp / "pmos-boot.img",
                   help="prebuilt mainline boot image to take the kernel region from")
    p.add_argument("--initramfs", type=Path, default=tmp / "initramfs.cpio",
                   help="distribution initramfs to take busybox and the gadget modules from")
    p.add_argument("--busybox", type=Path, default=tmp / "pmos-files" / "busybox")
    p.add_argument("--init", type=Path, default=here / "init")
    p.add_argument("--rebootbl", type=Path, default=tmp / "rebootbl")
    p.add_argument("--modules-dir", type=Path, default=tmp / "modules")
    p.add_argument("--out", type=Path, default=work / "out" / "linux-log.img")
    p.add_argument("--ramdisk-addr", type=int, default=0,
                   help="override ramdisk load address (default: keep the input image's)")
    args = p.parse_args(argv)

    for path, what in ((args.boot, "boot image"), (args.initramfs, "initramfs"),
                       (args.busybox, "busybox"), (args.init, "init"),
                       (args.rebootbl, "rebootbl")):
        if not path.is_file():
            raise SystemExit(f"{what} not found: {path}")

    build(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
