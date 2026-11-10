#!/usr/bin/env python3
"""Host-only type-check of package/EnchiladaAdrenoProbe.cpp and the four NativeAdreno C modules.

WHY: the kernel CI is the only place the shim is really compiled (it needs the XNU IOKit headers,
which do not exist on a Windows host). A shim that does not compile would burn a CI run and a
20-minute queue slot. This check compiles the SAME sources against a deliberately minimal IOKit
stub, with the same strict warning set the builders use, and verifies that every object is an
arm64 Mach-O. It cannot prove the ABI against a real XNU -- only CI can -- but it does prove the
C++ is well formed, that every symbol the shim calls is declared, and that no libc symbol crept in.

The stub (scripts/data/iokit-stub) declares ONLY what the probes use: IOLog and
PE_parse_boot_argn. If a probe grows a new kernel dependency this check fails, which is the
point -- the stub must not silently widen.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys

SCRIPTS = Path(__file__).resolve().parent
DEV = SCRIPTS.parent
GPU = DEV / 'drivers/NativeAdreno'
STUB = SCRIPTS / 'data/iokit-stub'

C_MODULES = ['AdrenoDomainState', 'AdrenoPowerGate', 'AdrenoBoardInputs', 'AdrenoCatalog']
STRICT = ['-std=c11', '-ffreestanding', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion',
          '-Wsign-conversion', '-Wshadow', '-Wpedantic', '-Wundef', '-Wcast-qual', '-Wstrict-prototypes']
STRICT_CXX = ['-std=c++17', '-ffreestanding', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion',
              '-Wsign-conversion', '-Wshadow', '-Wpedantic', '-Wundef']
FORBIDDEN = ('strlen', 'memcpy', 'memset', 'strcmp', 'printf(', 'malloc(', 'free(')


def macho_ok(path):
    data = path.read_bytes()
    if len(data) < 16:
        return False, 'too short'
    magic, cpu, subtype, kind = struct.unpack_from('<4I', data)
    return (magic, cpu, subtype, kind) == (0xfeedfacf, 0x100000c, 0, 1), \
        f'magic={magic:#x} cpu={cpu:#x} sub={subtype} kind={kind}'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', required=True)
    ap.add_argument('--cxx', required=True)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    checks, failures = 0, []

    def check(ok, label):
        nonlocal checks
        checks += 1
        print(('ok   ' if ok else 'FAIL ') + label)
        if not ok:
            failures.append(label)

    # The stub must stay minimal: naming a libc function here would let the shim
    # use it without the kernel providing it.
    stub_text = '\n'.join(p.read_text() for p in STUB.rglob('*.h'))
    check(sorted(p.name for p in STUB.rglob('*.h')) == ['IOLib.h', 'pexpert.h'], 'stub has exactly the two expected headers')
    # IOLog + snprintf + PE_parse_boot_argn: exactly the kernel surface the shim uses, and nothing else.
    check(stub_text.count('void IOLog') == 1 and stub_text.count('int snprintf') == 1
          and stub_text.count('int PE_parse_boot_argn') == 2,
          'stub declares exactly IOLog + snprintf + PE_parse_boot_argn')

    for stem in C_MODULES:
        src = GPU / f'{stem}.c'
        obj = out / f'{stem}.o'
        cmd = [args.cc, '-target', 'arm64-apple-macos11', *STRICT, '-I', str(GPU), '-c', str(src), '-o', str(obj)]
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (out / f'{stem}.log').write_text(r.stdout)
        check(r.returncode == 0, f'{stem}.c strict C11 arm64 ({r.returncode})')
        if r.returncode == 0:
            ok, detail = macho_ok(obj)
            check(ok, f'{stem}.o is an arm64 Mach-O object ({detail})')

    shim = DEV / 'package/EnchiladaAdrenoProbe.cpp'
    stub_obj = out / 'iokit_stub.o'
    stub_src = out / 'iokit_stub.c'
    stub_src.write_text('#include <IOKit/IOLib.h>\n'
                        'void IOLog(const char *f, ...) {(void)f;}\n'
                        'int PE_parse_boot_argn(const char *a, void *p, int m) {(void)a;(void)p;(void)m;return 0;}\n')
    r = subprocess.run([args.cc, '-target', 'arm64-apple-macos11', '-std=c11', '-ffreestanding', '-O2',
                        '-I', str(STUB), '-c', str(stub_src), '-o', str(stub_obj)],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    check(r.returncode == 0, f'IOLib stub compiles ({r.returncode})')

    obj = out / 'EnchiladaAdrenoProbe.o'
    cmd = [args.cxx, '-target', 'arm64-apple-macos11', *STRICT_CXX,
           '-I', str(STUB), '-I', str(GPU), '-c', str(shim), '-o', str(obj)]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out / 'EnchiladaAdrenoProbe.log').write_text(r.stdout)
    check(r.returncode == 0, f'EnchiladaAdrenoProbe.cpp strict C++17 arm64 ({r.returncode})')
    if r.returncode == 0:
        ok, detail = macho_ok(obj)
        check(ok, f'EnchiladaAdrenoProbe.o is an arm64 Mach-O object ({detail})')
        # Mach-O prefixes every symbol with an underscore, so compare bare names.
        nm_exe = str(Path(args.cc).with_name('llvm-nm.exe'))
        nm = subprocess.run([nm_exe, str(obj)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        defined, undefined = [], []
        for line in nm.stdout.splitlines():
            parts = line.split()
            if len(parts) < 2:
                continue
            kind, name = parts[-2], parts[-1]
            bare = name[1:] if name.startswith('_') else name
            if len(parts) >= 3:
                defined.append(bare)          # "address KIND name"
            elif kind in ('U', 'u'):
                undefined.append(bare)        # "U name"
        check('enchilada_adreno_poll' in defined, f'shim defines enchilada_adreno_poll (T symbols: {[d for d in defined if "poll" in d]})')
        # What the shim is ALLOWED to leave undefined: the three kernel entry points
        # the stub declares, the platform mapping hook, the stack protector, and the
        # host-tested adreno_* decision functions it deliberately does not define.
        # Round 48: AdrenoGmuBootV1BootState (host-tested C module, linked from
        # libEnchiladaAdreno.a) + _Z19armcpu_map_physicalyy (the C++-mangled
        # weak_import alias of the same platform hook) are also allowed.
        allowed = {'IOLog', 'PE_parse_boot_argn', 'snprintf', '__stack_chk_fail', '__stack_chk_guard',
                   'AdrenoGmuBootV1BootState', '_Z19armcpu_map_physicalyy'}
        unexpected = sorted(u for u in undefined
                            if u not in allowed and not u.startswith('adreno_') and u != 'armcpu_map_physical')
        check(not unexpected, f'shim has no unexpected undefined symbols (unexpected={unexpected})')
        text = shim.read_text()
        casts = [c for c in __import__('re').findall(r'reinterpret_cast<([^>]*)>', text)]
        check(casts and all('volatile' in c for c in casts),
              f'every pointer cast in the shim is volatile ({sorted(set(casts))})')
        check('= *reinterpret_cast' in text or 'return *reinterpret_cast' in text,
              'the only dereference is a read, not a store')
        # A store through the mapping would look like `*reinterpret_cast<...>(...) = value`.
        store = __import__('re').search(r'\*\s*reinterpret_cast\s*<[^>]*>\s*\([^;]*\)\s*=[^=]', text)
        check(store is None, f'shim contains no store through a pointer (found: {store.group(0) if store else None})')

    print(f'\n{checks - len(failures)}/{checks} checks passed')
    if failures:
        for f in failures:
            print('  FAILED: ' + f)
        return 1
    print('PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
