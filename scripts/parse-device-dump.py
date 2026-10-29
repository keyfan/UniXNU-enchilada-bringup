#!/usr/bin/env python3
"""One-shot: parse backup/ (boot_a.img + gpt-sda.bin) from the OnePlus 6 and print
the values needed by UniXNU-enchilada (board.toml, uefi-enchilada config, ADT constants)."""
import struct
import sys
from pathlib import Path

# dump-device-facts.sh writes its output to ./enchilada-facts. Older dumps used ./backup, so look
# there too rather than making the caller repeat the paths.
CANDIDATES = (Path('enchilada-facts'), Path('backup'),
              Path(__file__).resolve().parents[2] / 'enchilada-facts')


def default_file(name):
    for directory in CANDIDATES:
        candidate = directory / name
        if candidate.is_file():
            return candidate
    return CANDIDATES[0] / name


def parse_gpt(path):
    data = path.read_bytes()
    off = data.find(b'EFI PART')
    assert off >= 0, 'no GPT header'
    (sig, rev, hdr_size, hdr_crc, _res, current, backup_lba, first_usable,
     last_usable, disk_guid, entries_lba, num_entries, entry_size) = struct.unpack_from('<8sIIIIQQQQ16sQII', data, off)
    out = []
    eoff = off + hdr_size if entries_lba == 1 else off + (entries_lba * 512 - 0)  # entries follow header in sgdisk backup
    # sgdisk backup = GPT header (LBA1) then entries; locate entries right after 512-aligned header
    eoff = ((off + 512 + 511) // 512) * 512
    for i in range(num_entries):
        e = data[eoff + i * entry_size: eoff + (i + 1) * entry_size]
        if len(e) < entry_size:
            break
        type_guid = e[0:16]
        if type_guid == b'\x00' * 16:
            continue
        first, last, attrs = struct.unpack_from('<QQQ', e, 32)
        name = e[56:128].decode('utf-16-le', errors='replace').rstrip('\x00')
        if not name or not all(c.isprintable() for c in name):
            continue
        out.append((name, first, last, attrs))
    return out


def parse_boot(path):
    d = path.read_bytes()
    assert d[:8] == b'ANDROID!', 'not a boot image'
    (k_size, k_addr, r_size, r_addr, s_size, s_addr, tags_addr,
     page_size, hdr_ver_or_dt, os_ver) = struct.unpack_from('<10I', d, 8)
    name = d[48:64].split(b'\x00')[0].decode(errors='replace')
    cmdline = (d[64:576] + d[592:1616]).split(b'\x00')[0].decode(errors='replace')
    hdr_ver = hdr_ver_or_dt if hdr_ver_or_dt in (0, 1, 2, 3, 4) else 0
    return dict(magic='ANDROID!', kernel_size=k_size, kernel_addr=k_addr, ramdisk_size=r_size,
                ramdisk_addr=r_addr, second_size=s_size, second_addr=s_addr, tags_addr=tags_addr,
                page_size=page_size, header_version=hdr_ver, os_version=os_ver, name=name,
                cmdline=cmdline[:200])


def parse_fdt(blob):
    (magic, total, off_struct, off_strings, _rsv, ver, last_comp, boot_cpuid,
     size_strings, size_struct) = struct.unpack_from('>10I', blob, 0)
    assert magic == 0xD00DFEED
    strings = blob[off_strings:off_strings + size_strings]
    pos = off_struct
    stack = []
    nodes = {}   # path -> {props}
    FDT_BEGIN, FDT_END, FDT_PROP, FDT_NOP, FDT_ENDN = 1, 2, 3, 4, 9
    while pos < off_struct + size_struct:
        token = struct.unpack_from('>I', blob, pos)[0]
        pos += 4
        if token == FDT_BEGIN:
            end = blob.index(b'\x00', pos)
            name = blob[pos:end].decode(errors='replace')
            pos = (end + 4) & ~3
            stack.append(name)
            nodes.setdefault('/'.join(stack) or '/', {})
        elif token == FDT_END:
            stack.pop()
        elif token == FDT_PROP:
            length, nameoff = struct.unpack_from('>II', blob, pos)
            pos += 8
            value = blob[pos:pos + length]
            pos = (pos + length + 3) & ~3
            pname = strings[nameoff:strings.index(b'\x00', nameoff)].decode(errors='replace')
            path = '/'.join(stack) or '/'
            nodes.setdefault(path, {})[pname] = value
        elif token == FDT_NOP:
            continue
        elif token == FDT_ENDN:
            break
    return nodes


def fmt_reg(v, address_cells=2, size_cells=2):
    """Format a `reg` property as `base size ...`.

    `address_cells` and `size_cells` are the parent bus's #address-cells and #size-cells: (2, 2)
    on the SDM845 `soc` and `reserved-memory` nodes as Qualcomm writes them. A CPU node's reg is
    neither: it is the bare MPIDR affinity, one cell.
    """
    cells = struct.unpack('>%dI' % (len(v) // 4), v)
    if len(cells) == 1:
        # A CPU node's reg is the bare MPIDR affinity, not a base/size pair.
        return f'{cells[0]:#x}'
    pairs = reg_pairs(v, address_cells, size_cells)
    if not pairs:
        return ' | '.join(f'{cell:#x}' for cell in cells)
    return ' | '.join(f'{base:#x} size {size:#x}' for base, size in pairs)


def reg_pairs(v, address_cells=2, size_cells=2):
    """Decode a `reg` property into [(base, size)] using the parent bus's cell counts."""
    cells = struct.unpack('>%dI' % (len(v) // 4), v)
    stride = address_cells + size_cells
    if stride <= 0 or len(cells) % stride:
        return []
    pairs = []
    for index in range(0, len(cells), stride):
        base = 0
        for cell in cells[index:index + address_cells]:
            base = (base << 32) | cell
        size = 0
        for cell in cells[index + address_cells:index + stride]:
            size = (size << 32) | cell
        pairs.append((base, size))
    return pairs


def bus_cells(nodes, path):
    """(#address-cells, #size-cells) of the bus a node sits on; Qualcomm's trees use (2, 2)."""
    parent = nodes.get(path.rsplit('/', 1)[0] or '/', {})
    def read(name, default):
        value = parent.get(name)
        if value and len(value) == 4:
            return struct.unpack('>I', value)[0]
        return default
    return read('#address-cells', 2), read('#size-cells', 2)


def fdt_blobs(blob, base=0):
    """Every flattened device tree in `blob`.

    A Qualcomm boot image does not carry its device trees in a separate section: the kernel's
    appended DTB area holds several of them back to back, one per SoC revision, and the ABL picks
    one by msm-id. Returns [(offset, bytes)].
    """
    found = []
    index = blob.find(b'\xd0\x0d\xfe\xed')
    while index != -1:
        try:
            total = struct.unpack_from('>I', blob, index + 4)[0]
            if 64 < total < 4_000_000:
                found.append((base + index, blob[index:index + total]))
        except struct.error:
            pass
        index = blob.find(b'\xd0\x0d\xfe\xed', index + 4)
    return found


def root_cells(nodes, name):
    value = nodes.get('/', {}).get(name)
    if not value:
        return None
    return list(struct.unpack('>%dI' % (len(value) // 4), value))


def describe(nodes):
    """Print the constants board.toml and make-enchilada-adt.py need, from one parsed tree."""
    model = nodes.get('/', {}).get('model', b'').split(b'\x00')[0].decode(errors='replace')
    print(f'  model: {model}')
    print(f'  qcom,msm-id: {root_cells(nodes, "qcom,msm-id")}')
    print(f'  qcom,board-id: {root_cells(nodes, "qcom,board-id")}')
    cpu_regs = []
    for path, props in sorted(nodes.items()):
        if path.startswith('/cpus/') and 'reg' in props:
            cells = struct.unpack('>%dI' % (len(props['reg']) // 4), props['reg'])
            if len(cells) == 1:
                cpu_regs.append(cells[0])
            elif len(cells) == 2:
                cpu_regs.append((cells[0] << 32) | cells[1])
    print(f'  /cpus MPIDR list: {[hex(r) for r in sorted(cpu_regs)]}')
    for path, props in sorted(nodes.items()):
        if 'reg' not in props:
            continue
        cells = bus_cells(nodes, path)
        if 'interrupt-controller@17a' in path:
            print(f'  GIC {path} reg={fmt_reg(props["reg"], *cells)}')
        elif path.startswith('/soc/ufshc@') or 'ufsphy_mem@' in path:
            print(f'  UFS {path} reg={fmt_reg(props["reg"], *cells)}')
        elif 'cont_splash' in path:
            print(f'  splash {path} reg={fmt_reg(props["reg"], *cells)}')
    reserved = []
    for path, props in nodes.items():
        if path.startswith('/reserved-memory/') and 'reg' in props:
            name = path.rsplit('/', 1)[-1].split('@')[0]
            base, size = reg_pairs(props['reg'], *bus_cells(nodes, path))[0]
            reserved.append((base, name, size, 'no-map' in props))
    print(f'  {len(reserved)} fixed reserved-memory node(s):')
    for base, name, size, no_map in sorted(reserved):
        print(f'    {name:24s} {base:#012x} + {size:#010x}{"  no-map" if no_map else ""}')


def main(argv):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot', metavar='FILE', help='boot image to read instead of backup/boot_a.img')
    parser.add_argument('--gpt', metavar='FILE', help='sgdisk backup of the GPT instead of backup/gpt-sda.bin')
    parser.add_argument('--out-dir', metavar='DIR',
                        help='write the device trees it finds to DIR (they are useful inputs)')
    args = parser.parse_args(argv[1:])

    gpt = Path(args.gpt) if args.gpt else default_file('gpt-sda.bin')
    boot = Path(args.boot) if args.boot else default_file('boot_a.img')

    if gpt.is_file():
        print('== GPT ==')
        for name, first, last, attrs in parse_gpt(gpt):
            mark = ' <== userdata' if name == 'userdata' else ''
            print(f'  {name:12s} first_lba={first:#x} ({first})  last_lba={last:#x} ({last}){mark}')
    else:
        print('== GPT ==\n  no sgdisk backup yet (run scripts/dump-device-facts.sh on the phone,\n'
              '  or pass --gpt FILE); the userdata geometry is also in that dump\'s userdata-lba.toml')

    if not boot.is_file():
        print(f'\n== boot image ==\n  no {boot}; pass --boot FILE')
        return 1
    print(f'\n== boot image header ({boot}) ==')
    header = parse_boot(boot)
    for key, value in header.items():
        print(f'  {key}: {value}')

    data = boot.read_bytes()
    page = header['page_size']
    kernel = data[page:page + header['kernel_size']]
    blobs = fdt_blobs(kernel, base=page)
    print(f'\n== device trees appended to the kernel: {len(blobs)} ==')
    print('  offsets:', [hex(offset) for offset, _ in blobs])
    if args.out_dir and blobs:
        target = Path(args.out_dir)
        target.mkdir(parents=True, exist_ok=True)
        for index, (offset, blob) in enumerate(blobs):
            path = target / f'dtb{index}-{len(blob)}.dtb'
            path.write_bytes(blob)
            print('  wrote', path)
    # The last tree is the newest SoC revision, which is the one the phone in hand boots: msm-id
    # 0x141/0x20001 is SDM845 v2.1 and matches Resources/DTBs/enchilada.dts.
    chosen = len(blobs) - 1 if blobs else None
    for index, (offset, blob) in enumerate(blobs):
        print(f'\n== device tree {index} at {offset:#x} ({len(blob)} bytes'
              f'{" , the one this phone boots" if index == chosen else ""}) ==')
        describe(parse_fdt(blob))
    if not blobs:
        print('\n== no device tree in the boot image ==')
        print('  the device tree may be in a separate section (dtbo) instead of appended:')
        print('  adb pull /dev/block/by-name/dtbo_a, then fdtget -t x dtbo_a /reserved-memory/... reg')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
