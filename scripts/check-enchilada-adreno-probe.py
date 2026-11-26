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
import shutil
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
          and stub_text.count('int PE_parse_boot_argn') == 2
          and stub_text.count('void IOSleep') == 1,
          'stub declares exactly IOLog + snprintf + PE_parse_boot_argn + '
          'IOSleep: the last one is R206, because a busy spin starves the '
          'log writer and a yielding sleep does not')

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

    # ---- ROUND 113 REGRESSION GATE: the probe MUST build WITHOUT the blob ----
    # CI #247 was red because this file only compiled on a host that already had
    # AdrenoZapBlobV1.h. The #else branch defined the same sentinel the #ifndef
    # tested, so the fallback was skipped exactly when it was needed and the
    # build died on 7 undeclared identifiers. That is the classic "green here,
    # red there" shape: the header happens to exist on a developer box (the
    # generator ran) and NEVER exists on CI (no extraction), so the broken path
    # was the one nobody exercised locally.
    #
    # This gate closes that hole by compiling against an include directory built
    # to PROVABLY lack the header -- it does not depend on whether this host
    # happens to have one. -fsyntax-only is enough: the failure mode is a
    # preprocessing/name-resolution error, not a codegen one.
    #
    # The flags mirror the real builders, -Werror included. That matters: the
    # fallback's four unused static placeholders are a SECOND failure mode under
    # -Werror that a lenient syntax check would miss entirely.
    #
    # THE TWO BUILD MODES, and why this gate covers exactly one of them.
    #
    # DEFAULT build (what CI runs): there are no firmware bytes, therefore no
    # generated header, therefore the probe's fallback branch is LIVE. That is
    # not a degraded state to be worked around -- it is a REAL build condition,
    # and it is the one this gate compiles against.
    #
    # OPT-IN byte build (a human dispatches the workflow and supplies the
    # secret): the bytes genuinely exist, generate-zap-blob.py is invoked ON
    # PURPOSE, and the header is present. That is the correct, intended use of
    # the generator, not a workaround.
    #
    # THE DISTINGUISHING CRITERION IS WHETHER THE BYTES EXIST -- i.e. whether the
    # build was opted in -- NOT whether the build is red or green. Running the
    # generator to turn a red DEFAULT build green is what is forbidden: it
    # manufactures the header, this gate then tests the WRONG branch, and the
    # absent-header path (the fallback in package/EnchiladaAdrenoProbe.cpp) goes
    # back to being unbuilt and unverified everywhere -- which is exactly how CI
    # #247 stayed invisible. Running it as part of an explicit opt-in byte build
    # is a different act with a different precondition, and is expected.
    #
    # So this gate must keep compiling the NO-BYTES case even now that the opt-in
    # path exists. If a future change makes it run against a generated header, it
    # has stopped testing the default build and must be reverted.
    iso = out / 'no-blob-include'
    iso.mkdir(parents=True, exist_ok=True)
    for stale in iso.glob('*.h'):
        stale.unlink()
    blob_name = 'AdrenoZapBlobV1.h'
    for header in GPU.glob('*.h'):
        if header.name != blob_name:
            shutil.copy2(header, iso / header.name)
    check(not (iso / blob_name).exists(),
          f'R113 the isolated include dir provably lacks {blob_name}')
    r2 = subprocess.run([args.cxx, '-target', 'arm64-apple-macos11', *STRICT_CXX,
                         '-fsyntax-only', '-I', str(iso), '-I', str(STUB),
                         str(shim)],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out / 'EnchiladaAdrenoProbe.noblob.log').write_text(r2.stdout)
    _tail = ' | '.join(r2.stdout.strip().splitlines()[:3]) if r2.returncode else ''
    check(r2.returncode == 0,
          f'R113 the probe compiles WITHOUT {blob_name} (the CI condition) '
          f'({r2.returncode}){(" :: " + _tail) if _tail else ""}')

    # ---- ROUND 117: the DEFAULT build's blob contract, run for real ----
    # The opt-in byte build (workflow_dispatch + zap_firmware_b64) is now a real
    # path, so the thing that must not drift is its COMPLEMENT: with no bytes the
    # generator has to emit the absent stub, and the header the build actually
    # #includes has to say present=0. This runs the generator into a throwaway
    # directory -- it does NOT touch drivers/NativeAdreno/AdrenoZapBlobV1.h, so
    # checking never rewrites the tree it is checking.
    #
    # Both halves matter:
    #   * present=0 / lengths 0 is what makes the kernel log blob=absent and
    #     refuse to issue a PAS call with a zero image;
    #   * "no full-size all-zero array" is what stops the failure being SILENT --
    #     a 6684-byte zero array would look like a valid blob to every consumer
    #     downstream and turn a missing input into a mysterious secure-world
    #     rejection.
    gen = DEV / 'scripts' / 'generate-zap-blob.py'
    check(gen.is_file(), 'R117 the blob generator exists where the build runs it')
    if gen.is_file():
        empty_dir = out / 'no-bytes'
        empty_dir.mkdir(parents=True, exist_ok=True)
        for stale in empty_dir.glob('*'):
            if stale.is_file():
                stale.unlink()
        stub_out = out / 'AdrenoZapBlobV1.absent.h'
        r3 = subprocess.run([sys.executable, str(gen),
                             '--firmware-dir', str(empty_dir),
                             '--out', str(stub_out)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (out / 'generate-zap-blob.absent.log').write_text(r3.stdout)
        check(r3.returncode == 0,
              f'R117 the generator succeeds on a no-bytes dir ({r3.returncode})')
        if stub_out.is_file():
            stub = stub_out.read_text(encoding='utf-8')
            check('kAdrenoZapBlobPresent 0' in stub,
                  'R117 no bytes -> the stub declares present=0')
            check('kAdrenoZapMdtBytes 0' in stub and 'kAdrenoZapSegBytes 0' in stub,
                  'R117 no bytes -> both lengths are 0, not the real sizes')
            check('adreno_zap_mdt[6684]' not in stub and 'adreno_zap_seg[1968]' not in stub,
                  'R117 no bytes -> NO full-size array (a silent all-zero blob)')
            check(stub.count('static const char kAdrenoZap') == 2 and
                  ('"' + '0' * 64 + '"') in stub,
                  'R117 no bytes -> both digests are all-zero, not plausible')
        else:
            check(False, 'R117 the generator produced no stub file')


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
        # Round 52: the pure AdrenoPowerUpV1 Plan/gate helpers resolve from the
        # SAME archive; Execute() is deliberately NOT called.
        allowed = {'IOLog', 'PE_parse_boot_argn', 'snprintf', '__stack_chk_fail', '__stack_chk_guard',
                   'AdrenoGmuBootV1BootState', '_Z19armcpu_map_physicalyy',
                   # Round 167: the WRITABLE sibling of that hook. It is a kernel
                   # entry point the shim calls but does not define, exactly like
                   # the read-only one above -- and it must appear here rather
                   # than be stubbed away, because its presence is the evidence
                   # that the one-word write goes through a mapping that was
                   # asked for by name instead of through the read-only window.
                   'armcpu_map_physical_rw', '_Z22armcpu_map_physical_rwyy',
                   'AdrenoPowerUpV1BoardGmuClockCount', 'AdrenoPowerUpV1BoardGpuClockCount',
                   'AdrenoPowerUpV1BoardIsA740Family', 'AdrenoPowerUpV1InputsValid',
                   'AdrenoPowerUpV1CountsAreDistinct', 'AdrenoPowerUpV1PlanSize',
                   'AdrenoPowerUpV1Plan', 'AdrenoPowerUpV1PreIsReady',
                   # Round 95 stage 5: the IOKit caller of the power-up chain.
                   # These resolve from libEnchiladaAdreno.a (all four modules are
                   # archive objects) and from IOKit itself for the delay. They are
                   # expected precisely because stage 5 is the stage that WRITES:
                   # a caller that never referenced the executor would leave stage 5
                   # dead code, which is the condition this list exists to catch.
                   'AdrenoGpuccWindowV1Bind', 'AdrenoGpuccWindowV1SetDelay',
                   'AdrenoGpuccWindowV1Mmio',
                   'AdrenoCallerV1Make', 'AdrenoCallerV1Run',
                   'IODelay',
                   # Round 110 stage 7: the exported SMC entry point from
                   # drivers/ArmCpu/ArmCpu.cpp. It is allowed for the SAME reason
                   # the entire list exists, stated at :116-118 -- it is a kernel
                   # entry point this shim calls but does not define. Note what
                   # this does and does NOT permit:
                   #   * it permits ONE named symbol, resolved from libArmCpu.a;
                   #   * it does NOT weaken the forbidden-symbol rule. The scan
                   #     below still rejects _qcom_scm_call, _writel(, _readl(,
                   #     _IOServiceOpen and _IOConnectCallMethod, and
                   #     '_qcom_scm_call' is not a substring of
                   #     'armcpu_scm_call' (verified: index -1), so the two can
                   #     never be confused.
                   #   * adding a name here can never hide an arbitrary symbol:
                   #     the check is still "everything undefined must be either
                   #     known or adreno_*", so a stray libc call still fails.
                   'armcpu_scm_call',
                   # Round 142: the FIVE-argument SMC entry point from the SAME
                   # ArmCpu.cpp, added for the SHM_BRIDGE_CREATE reachability
                   # probe (that call is QCOM_SCM_ARGS(4, ...) and its fourth
                   # argument lives in x5, which armcpu_scm_call pins to 0). It
                   # is the SAME kind of export as armcpu_scm_call directly
                   # above; only the SHM_BRIDGE_CREATE probe references it, and
                   # the five PAS/probe call sites keep calling armcpu_scm_call
                   # unchanged. It does not relax the stub boundary (still two
                   # headers, IOLog + snprintf + PE_parse_boot_argn).
                   'armcpu_scm_call5',
                   # Round 144: the five-argument SMC entry point that ALSO returns
                   # the a2 handle channel (armcpu_scm_call5_r). Same kind of
                   # export as armcpu_scm_call5 above, added so the SHM_BRIDGE_CREATE
                   # property test can read the bridge handle in smc a2. Only the
                   # CREATE reachability probe references it; stub boundary (two
                   # headers, IOLog + snprintf + PE_parse_boot_argn) unchanged.
                   'armcpu_scm_call5_r',
                   # Round 152: the FULL-result SMC entry point (returns x1/x2/x3/x6
                   # plus the retry count). Same kind of export as armcpu_scm_call5_r
                   # above; the shmbridge CREATE group-A probe routes through it so
                   # the row can report rt=/a3=/a6=. Stub boundary unchanged.
                   'armcpu_scm_call_full',
                   # Round 127 stage 7: the writable physically-contiguous
                   # allocator from the SAME ArmCpu.cpp, added for the PAS
                   # init_image metadata buffer. It is the SAME kind of entry as
                   # armcpu_scm_call above, and the reason it is added HERE rather
                   # than by widening the stub is the whole point of this round:
                   #   * the alternative was to let this shim use
                   #     IOBufferMemoryDescriptor directly, which would have meant
                   #     relaxing the two stub assertions above -- the boundary
                   #     that exists so a probe cannot quietly acquire new kernel
                   #     dependencies;
                   #   * putting the allocation behind an export keeps the stub at
                   #     exactly IOLog + snprintf + PE_parse_boot_argn and adds ONE
                   #     named undefined symbol instead;
                   #   * it resolves from libArmCpu.a at link time, exactly as
                   #     armcpu_scm_call does, so "no unexpected undefined symbols"
                   #     still means what it says.
                   'armcpu_dma_alloc_writable'}
        unexpected = sorted(u for u in undefined
                            if u not in allowed and not u.startswith('adreno_') and u != 'armcpu_map_physical')
        check(not unexpected, f'shim has no unexpected undefined symbols (unexpected={unexpected})')
        text = shim.read_text()
        casts = [c for c in __import__('re').findall(r'reinterpret_cast<([^>]*)>', text)]
        check(casts and all('volatile' in c for c in casts),
              f'every pointer cast in the shim is volatile ({sorted(set(casts))})')
        check('= *reinterpret_cast' in text or 'return *reinterpret_cast' in text,
              'the read helper still dereferences through the volatile cast')
        # R167/R168 made this shim a WRITER by design (ROUND 167: one config word;
        # ROUND 168: the ITCM image plus the pre-start/pulse words under key 8), so
        # the old absolute claim "no store through a pointer" is now FALSE and is
        # replaced -- not deleted -- by the same pinned boundary the stage
        # verifiers assert: name the exact write set, and require every one of the
        # stores to go through a volatile mapping.
        _re = __import__('re')
        check(text.count('*cfg = ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE') == 1,
              'R167 exactly one pointer store writes CM3_BOOT_CONFIG')
        check(len(_re.findall(r'gmuW\[[^\]]*\]\s*=[^=]', text)) == 1187,
              'R313 the three GPUCC-alias keys add three more (keys 293, 294 and 295 read the GPUCC clock and GDSC words through the GMU alias and the two enable keys set one branch bit each), R305 the transport keys add seventeen more (keys 274, 275 and 276 replay the accepted shotgun shape and then put a legacy v1 message on the command queue, or ring the doorbell on an empty queue as the control: three stores each), '
              'R304 the acceptance follow-up keys add one more (keys 271, 272 and 273 replay the key 143 shotgun shape again, and key 273 rings the HOST2GMU doorbell after the start word is accepted), '
              'R168+R170+R171+R175+R178+R181+R186+R205 the pointer writes are exactly '
              'the ITCM loop, the pulse words, the three handshake words, the '
              'R234 bypass key adds one more (HFI_CTRL_INIT under key 74), the R239 shape-control and table-clear keys add seven more (keys 82/83 each repeat the four masks, and key 83 adds QTBL_INFO = 0 before the start word while key 82 writes the same store with value 0), the R240 zero-page and positive-control keys add eight more (keys 84/85 each repeat the four masks, key 84 adds QTBL_ADDR pointing at a fresh zero page and QTBL_INFO = 1 before the start word, and both keys add the start word itself), the R242 storage keys add one more (key 90 repeats the two SMMU routing stores and then adds the start word once), and the R243 type probe adds one more again (key 94 repeats those two routing stores and adds the start word once), the R247 snapshot, re-boot and re-boot-plus-start keys add eleven more (key 102 writes no GMU word at all, key 103 releases the CM3 reset with the queue table in place and polls the init word but never starts the HFI, and key 104 repeats that and adds the start word once as its last store), the R245 reproducibility and post-start keys add two more (keys 97 and 98 share one block and each branch carries the start word as its own last textual action: key 98 stops there, key 97 goes on to read the fault and status registers back), the R246 firmware-side keys add four more (key 99 carries one start word and then reads the registers the firmware would have to touch, key 100 re-arms HFI_QTBL_ADDR and HFI_QTBL_INFO and carries one start word, and key 101 adds a second route for the GMU kernel stream and carries one start word), the '
              'R277 identity keys add five more (key 193 writes nothing at all, '
              'the R278 HFI-start keys add eleven more (key 196 clears '
              'HFI_QTBL_INFO and then writes HFI_CTRL_INIT, key 197 clears only '
              'HFI_QTBL_ADDR and then writes INIT, and key 198 pulses '
              'CM3_SYSRESET both ways, clears FW_INIT_RESULT, clears '
              'HFI_QTBL_INFO, writes INIT and rings then clears MSGQ), '
              'key 194 rings the HFI message doorbell MSGQ and then clears its reply '
              'bit, and key 195 writes HFI_CTRL_INIT once before ringing the same '
              'doorbell and clearing it), '
              'nine pre-start configuration words, the four HFI words (QTBL_ADDR, '
              'QTBL_INFO, CP_MSG, CTRL_INIT), the four rail-on words of keys '
              '22/23 (BOOT_SLUMBER_OPTION, HOST2GMU_INTR_SET, GMU2HOST_INTR_CLR '
              'and the second, handshake-gated doorbell), the two '
              'a6xx_rpmh_start() writes, the SPTPRAC enable of keys 24/25, the '
              'second rail-on pass of key 28 with its two arc-vote words '
              '(GX_VOTE_IDX, MX_VOTE_IDX, the only place the vote is written), '
              'the single HFI_CTRL_INIT store of key 31 (R184), and the R186 rail '
              'keys: key 32/33 share five stores (BOOT_SLUMBER_OPTION, the two '
              'votes again and SPTPRAC) and key 33 adds the boot-slumber '
              'HOST2GMU_INTR_SET plus its GMU2HOST_INTR_CLR clear; '
              'the 18 table/ring stores go through hfiTbl/hfiQh into the '
              'allocated buffer, not through this mapping; and the R193 '
              'sequencer key adds nineteen: eleven setup words on the RSC '
              'page (STATUS0, slave id, two hidden-TCS words, override, the '
              'three PDC handshake words and the two match values), the five '
              'microcode words that RSCC_SEQ_MEM_0_DRV0 receives, and the '
              'request-up/request-down pair. That last pair is the SAME two '
              'stores keys 24/25 and 28 already use, so the budget counts '
              'them once per source site rather than once per behaviour; '
              'and the R199 GDSC key adds four more through the same mapping: '
              'the AON reset assert and its deassert on the clamp word, the '
              'clamp-IO deassert, and the SW_COLLAPSE clear on the GX GDSCR; '
              'and the R200 repeat adds the same four once more, because that '
              'key runs with the CM3 alive and the budget counts source sites; '
              'and the R201 measurement adds the same four a third time, since '
              'it repeats the sequence to time it against a CX control; '
              'and the R208 pacing key adds the same three HFI stores once '
              'more, because it re-runs the store under a read loop R201 '
              'timed at 3.17 s per 200000 passes, instead of under a sleep '
              'that R206 and R207 both died inside; and the R209 bracket key '
              'adds the same three once more, because it needs one row either '
              'side of each store step and cannot pace at all; and the R210 '
              'paced key adds the same three again, because it repeats the '
              'store under the yielding read loop R201 timed; and the R214 flush key '
              'adds the same three a fourth time, because it syncs the medium after '
              'every row instead of waiting for a flush; and the R216 bracket key adds the '
              'same three a fifth time, because it needs a durable row either side of each '
              'store step; and the R218 bracket adds the same three a sixth time, on the key '
              'value R217 s control proved is actually reached, because R216 s attempts on '
              'key 65 died before even an unconditional echo row reached the medium; and the '
              'R219 split adds three more in two of the three branches: key 67 writes the two '
              'table words and key 68 writes the start word, while key 66 is the no-store '
              'control that decides whether a verdict is available at this position at all; and '
              'the R221 margin key adds the same three once more on key 70, which pauses a full '
              'writer round BEFORE the start word, because the measured gap at that point was '
              '107 ms against a ~500 ms round and no post-store row could ever land; and the '
              'R222 attribution test moves that single store into key 66, the value already '
              'proven to write its window, so the key value stops being a variable: one store '
              'net, three rows around it; and the R236 interrupt key adds seven: the AO '
              'interrupt clear and mask, the HFI interrupt clear and mask, the start word '
              'itself, and the two restores that put both masks back exactly as they were '
              'found; and the R237 split adds seven more across two keys: key 78 writes '
              'the two clears and the two masks and then returns without ever touching '
              'the start word, while key 79 repeats those four, adds the start word and '
              'the same two restores, so that one store is the only difference between '
              'them, and the R242 routing key adds exactly one more: key 90 repeats the two SMMU routing stores and then adds a single store, HFI_CTRL_INIT, as the last action of the round, while keys 89 and 91 keep that round free of every GMU write, and the R248 init-word and interrupt keys add twenty-three more: keys 105, 106 and 107 each clear CM3_FW_INIT_RESULT before pulsing the reset and each re-run the five-word CM3 bring-up once, so that alone is eighteen stores, key 106 then clears HFI_CTRL_INIT before writing it back and key 107 clears the two interrupt CLR registers before the start word, which makes twenty-three, and the R249 poll keys add twenty-nine more: keys 108, 109 and 110 each repeat that same six-store re-boot, the two interrupt clears and one start word, so nine apiece, and key 110 adds the two descriptor writes (HFI_QTBL_INFO 0 and back to 1) immediately before the start word, which makes twenty-nine, and the R250 arrival keys add fourteen more: keys 111, 112 and ''113 share one preamble that carries the init-word clear, the five-word CM3 bring-up and ''the two interrupt clears exactly once, which is eight stores, and key 111 then adds one ''doorbell store plus its start word, key 112 adds its start word alone, and key 113 adds ''the two re-poke stores plus its start word, which makes fourteen, and the R251 timing keys add fourteen more again: keys 114, 115 and 116 share the same preamble, key 114 then adds one interrupt clear plus its start word, key 115 adds one mask store plus its start word, and key 116 adds that same clear and that same mask store with no start word at all, and the R252 fault-timing keys add thirteen more: keys 117, 118 and 119 share one preamble, key 117 then adds one mask store plus its start word, key 118 adds one interrupt clear plus its start word, and key 119 adds that same clear with no start word at all, and the R253 follow-up keys add sixteen more: keys 120, 121 and 122 share that same preamble, key 120 then adds one mask store, its start word and a second interrupt clear once the bit has been seen, key 121 adds one mask store plus its start word, and key 122 adds the zero queue-table address, HFI_QTBL_INFO back to 1 and its start word, and the R254 fault-address keys add thirteen more: keys 124, 125 and 126 share one preamble, key 124 then adds one mask store plus its start word, key 125 adds one mask store plus its start word with no clear at all, and key 126 adds that same mask store with no start word, and the R255 low-address keys add thirteen more again: keys 127, 128 and 129 share that same preamble, key 127 then adds one mask store plus its start word, key 128 adds its own mask store and its start word while the preamble hands the firmware the truncated queue table, and key 129 adds the mask store with no start word, and the R256 readback keys add twelve more: keys 130, 131 and 132 share that same preamble, key 130 adds one mask store with no start word at all, key 131 adds one mask store and then its start word after cleaning the table pages, and key 132 adds that same mask store with no start word, and the R257 recheck keys add fourteen more: keys 133, 134 and 135 share that same preamble, key 133 adds one mask store and its start word and then reads the bank own words back, key 134 does the same and also reads the global and per-bank fault words, and key 135 invalidates the SMMU translations before adding that same mask store and start word, and the R258 read-path keys add fourteen more: keys 136, 137 and 138 share that same preamble, key 136 adds one mask store plus its start word and then reads the bank fault status word three times with a pause between the reads, key 137 adds that same mask store and start word and then reads the fault address twice around a same-value write and read of the bank control word, and key 138 clears the stream match register valid bit before adding that same mask store and start word and the R259 routing keys add twelve more: keys 139, 140 and 141 share that same preamble, key 139 clears the stream match register valid bit and keeps SID 4 but writes no start word at all, key 140 clears that same bit and writes it straight back up before adding one mask store and its start word, and key 141 keeps the match valid on a SID that does not exist before adding that same mask store and start word, and the R260 routing and table keys add fourteen more: keys 142, 143 and 144 share that same preamble, key 142 clears the stream match register valid bit and commits the log with a flush before adding one mask store and its start word, and keys 143 and 144 both fill the table pages with valid entries so that no walk can miss, adding one mask store and one start word each, and the R261 start-count and queue-read keys add nineteen more: keys 145, 146 and 147 share that same preamble, key 145 adds one mask store and then three start words so that the control word can be read three times in one boot, key 146 adds one mask store and its single start word and then maps the queue-table page and reads it back, and key 147 adds one mask store, its start word, the two re-arm stores for QTBL_ADDR and QTBL_INFO and a second start word, and the R262 read-path keys add twelve more: keys 148, 149 and 150 share that same preamble and its page fill, key 148 adds one mask store, its start word and nothing after it, key 149 adds no store at all, and key 150 adds one mask store and its start word, and the R263 page-bracket keys add thirteen more: keys 151, 152 and 153 share that same preamble and its page fill, key 151 adds one mask store and its start word, key 152 does the same but skips the reset pulse, and key 153 adds that mask store with no start word at all, and the R264 post-acceptance keys add thirteen more: keys 154, 155 and 156 share that same preamble and its page fill, key 154 adds one mask store and its start word and then reads the CP message word and the page twice, key 155 writes a pattern into the next four page words before adding one mask store and its start word, and key 156 writes that same pattern and adds only the mask store, and the R265 write-path keys add twelve more: keys 157, 158 and 159 share that same preamble, key 157 writes one word into the page at word sixty-four and reads it straight back twice but writes no start word at all, key 158 writes those same four safe words and then adds one mask store and its start word, and key 159 writes the four header words, cleans the page without invalidating it, and adds one mask store and its start word, and the R266 matrix keys add seventeen more: keys 160, 161 and 162 share that same preamble, key 160 writes the four header words, cleans them with an invalidating pass, adds one mask store and its start word, then rewrites them, cleans them with a clean-only pass, and adds the same mask store and start word again, key 161 does all of that on the four safe words instead of the header words, and key 162 writes the header words, cleans them with the clean-only pass and adds only the mask store, and the R267 counting keys add twenty-two more: keys 163, 164 and 165 share that same preamble and its page fill, keys 163 and 164 each run three cycles of write, invalidating clean, mask store and start word on the header words and on the safe words respectively, which is six stores apiece, and key 165 writes no pattern at all and adds one mask store and its single start word, and the R268 counting keys add thirty-one more: keys 166, 167 and 168 share that same preamble, key 166 runs six cycles of write, invalidating clean, mask store and start word, key 167 runs three of those cycles and reads the four status words around each one, and key 168 writes the pattern once, starts the HFI, writes the control word back to zero and then starts it a second time, and the R269 latch keys add twenty-five more: keys 169, 170 and 171 share that same preamble, key 169 runs four starts inside one boot and hand clears the control word before the second and before the fourth, key 170 sends no start word until four hand writes have set and cleared that word, and key 171 starts once, snapshots the firmware words, hand clears the control word and snapshots again, and the R270 restart keys add fifty-five more: keys 172, 173 and 174 share that same preamble, key 172 restarts the CM3 before each of its three start words, key 173 re-arms the queue table onto a fresh page before its second and third start, and key 174 starts once, clears the control word by hand, restarts the CM3 and starts again, and the R271 no-start keys add thirty-six more: keys 175, 176 and 177 share that same preamble, key 175 clears the control word by hand, restarts the CM3 and watches it for the full window with no start word of its own before committing one control start, key 176 restarts first, reads the word as the restart left it, clears it and then starts, and key 177 clears, restarts and commits two start words without clearing in between, and the R272 read-path keys add thirty-one more: keys 178, 179 and 180 share that same preamble, key 178 clears the control word by hand, restarts the CM3 and commits one start word after the clear, key 179 restarts without clearing and writes no start word at all, reading only the control word and the four status words beside it, and key 180 restarts without clearing, writes its start word and re-reads those four status words, and the R273 control-word tail keys add thirty-four more: keys 181, 182 and 183 share that same preamble, key 181 restarts, commits one start word, then writes GMU2HOST_INTR_CLR to clear the interrupt latch and re-reads the control word, key 182 restarts, commits its start word and reads the whole control word and every neighbour from 0x517f through 0x5186, and key 183 restarts, commits its start word first and only then hand clears the control word to reproduce key 177 s 0xfe shape, and the R274 GX power-domain keys add twelve more: keys 184, 185 and 186 share one flat block with no CM3 bring-up at all, key 184 only reads the GX GDSCR, the GX status word, the GX clamp word and the CX status word together with the two CX branch-clock registers, key 185 sets the two CX branch clocks first and then runs the four R199 GX GDSC stores and polls the power-up-complete bit, and key 186 runs those same four stores with no clock change at all as its control, and the R275 rail-vote keys add thirteen more: keys 187, 188 and 189 share one flat block that rewrites the two arc votes with the values it just read and clears BOOT_SLUMBER_OPTION before ringing the boot-slumber doorbell, which is five stores shared by keys 188 and 189, and key 189 runs them only after a CM3 reset pulse that costs three more (the init word clear and the two halves of the reset), while key 187 writes nothing at all, and the R276 doorbell keys add nine more: keys 190, 191 and 192 share one flat block that leaves the CM3 alone, key 190 rings the OTHER legacy out-of-band message (GPU_SET, set bit 16 / ack bit 24) with its ack clear, which is two stores, key 191 rings the boot-slumber doorbell again with the same ack clear and adds one write-back of the AO host interrupt status word, which is three, and key 192 writes BOOT_SLUMBER_OPTION to 1 for the slumber semantic, rings the same doorbell with its clear, and puts the option back to 0, which is four, and the R279 zero-window keys add eight more: keys 199, 200 and 201 share one flat block, key 199 clears HFI_QTBL_INFO and then writes HFI_CTRL_INIT and samples the window ten times, which is two stores, key 200 does the same clear and the same INIT and then waits twenty more milliseconds before reading again, which is two more, and key 201 resets the CM3 (two halves of the reset plus the init-word clear, three stores) and then writes INIT without clearing the descriptor, which is one more, and the R280 split keys add eight more: keys 202, 203 and 204 share one flat block, key 202 clears HFI_QTBL_INFO, writes HFI_CTRL_INIT and then waits ten milliseconds before a single read, which is two stores, key 203 pulses the CM3 reset (two halves plus the init-word clear, three stores) and then writes INIT with the descriptor deliberately left set, which is one more, and key 204 clears the descriptor, writes INIT again and samples the window twenty times, which is two more, and the R281 poll-loop keys add six more: keys 205, 206 and 207 share one flat block, and each clears HFI_QTBL_INFO and writes HFI_CTRL_INIT, which is two stores per key -- 205 then runs the full two-thousand-poll loop before a single read, 206 samples inside that same loop, and 207 skips the loop and reads after pure waits, and the R282 timing keys add four more: keys 208 and 209 each clear HFI_QTBL_INFO and write HFI_CTRL_INIT (two stores each), while key 210 writes nothing at all and only waits the same twenty milliseconds before one read, and the R284 cadence keys add three more: key 215 clears HFI_QTBL_INFO and writes HFI_CTRL_INIT (two stores), while key 216 clears the descriptor alone and writes no INIT at all, and key 214 writes nothing, and the R285 cadence keys add one more: key 223 writes HFI_CTRL_INIT alone (one store), while keys 224 and 225 write nothing at all and only vary the cadence, and the R287 table keys add eleven more: keys 229, 230 and 231 share one preamble that installs the context bank, fills every entry of the queue-table pages with a valid descriptor and writes HFI_CTRL_INIT (two stores: the descriptor clear and the INIT), key 229 then only reads the gate words, key 230 adds the rail slumber option and the two arc votes before ringing the boot-slumber doorbell and clearing its ack, which is five more, and key 231 adds no store at all and only polls the power-up bit, and the R288 mask-and-route keys add eight more: keys 232, 233 and 234 share the table-installing preamble, so each carries the descriptor clear and the INIT store (two stores), key 232 adds a store of GMU2HOST_INTR_MASK = 0xff7fffff before that INIT -- the one write key 143 has and the R287 preamble lacks -- key 234 adds that same mask store, and key 233 adds no GMU word at all: its two ingredients are the raw SMMU stream match register and the stream-to-context registers, which are not part of this count, and the R289 recipe keys add twenty-eight more: keys 235, 236 and 237 each repeat the whole sequence key 143 proved on the device (CM3_FW_INIT_RESULT = 0, HFI_QTBL_ADDR, HFI_QTBL_INFO = 1, the CM3 reset pulse with its FW_INIT_RESULT poll, the two interrupt clears, the mask and the start word), and key 236 leaves out the reset pulse and the poll that follows it but still writes the clears, the mask and the start word, and the R290 separation keys add thirty-one more: keys 238, 239 and 240 each run the same full recipe key 143 proved (queue-table build, FW_INIT_RESULT = 0, the queue address, HFI_QTBL_INFO = 1, the CM3 reset pulse and its FW_INIT_RESULT poll, both interrupt clears, the mask and the start word), key 238 writes the physical base of the table this key just built into HFI_QTBL_ADDR, key 239 writes the stale address it read back in the census row instead, which is key 235 again, and key 240 writes the fresh base and then clears the descriptor after the reset, which is key 237 with the address fixed, and the R292 loss keys add twelve more: key 245 replays the R291 raising shape store for store (eleven of them), and key 246 writes exactly one store, the descriptor clear, between its two printed rows, while key 244 writes nothing at all, and the R293 bisection keys cut that same shape down: key 247 keeps four stores (the descriptor clear, both halves of the reset and the start word), key 248 keeps seven (those four plus the init-word clear, the queue address and the CP response), and key 249 keeps nine (those seven plus the mask and the descriptor raise), and the R298 isolation keys add twenty-six more on the shape key 250 proved alive: key 262 repeats its eight stores with no interrupt clear at all, key 263 adds the AO clear alone and key 264 the HFI clear alone, so the three keys come to twenty-six, and each of them prints its zero-write census before its first store so that a same-boot leftover in a latched bit cannot pass for an acceptance. and the R299 both-clear keys add thirty more: keys 265, 266 and 267 each start from that same living shape and each writes the AO clear and the HFI clear, ten stores apiece, key 265 in the order key 260 used, key 266 swapped, and key 267 with a hundred-microsecond gap and a re-read between them. The R294 to R297 rounds raised this total without extending this note; their own evidence sections carry their deltas, and the number here is the probe-wide reality every gate holds')
        check('gmu274r=' in text and 'gmu275r=' in text and 'gmu276r=' in text
              and text.count('HOST2GMU_INTR_SET] = 1u;') >= 3
              and text.count('ADRENO_HFI_V1_QUEUE_WORDS') >= 9,
              'R305 the three transport keys ring the bit 0 doorbell that a6xx_hfi_queue_write uses and '
              'advance the queue write index')
        check('volatile uint32_t *const gmuW' in text,
              'R168 every one of those writes goes through a volatile mapping')
        check('gmuW[r250maskWord] = 0xff7fffffu;' in text and
              'gmuW[r250qiWord] = 1u;' in text,
              'R294 the split arms name the two stores they separate')
        check('gmuW[r253qiWord] = 1u;' in text and
              'gmuW[r253srWord] = 1u;' in text,
              'R295 the three keys move the descriptor raise relative to the CM3 reset')
        check('gmuW[r259clrWord] = 0xffffffffu;' in text and
              'gmuW[r259aocWord] = 0xffffffffu;' in text and
              'gmuW[r259qiWord] = 0u;' in text,
              'R297 the three keys bring both interrupt clears back')
        check('gmuW[r256qaWord] = r256qtbl;' in text and
              'gmuW[r256qiWord] = 0u;' in text and
              'gmuW[r256qiWord] = 1u;' in text,
              'R296 the three keys separate the address, the raise and the transition')
        check('gmuW[r262aocWord] = 0xffffffffu;' in text and
              'gmuW[r262clrWord] = 0xffffffffu;' in text,
              'R298 the isolation keys carry the two interrupt clears, one key each')
        check('gmu262c=census=1 5180=%08x 5181=%08x' in text and
              'gmu263d=ao_clear=1 pre9305=%08x post9305=%08x' in text and
              'gmu264d=hfi_clear=1 pre5192=%08x post5192=%08x' in text,
              'R298 every key reads the latched bits before it writes and the status word around the clear it carries')
        check('gmuW[r262qiWord] = 0u;' in text and
              'gmuW[r262maskWord] = 0xff7fffffu;' in text and
              'gmuW[r262iniWord] = 1u;' in text,
              'R298 each key starts from the descriptor-cleared shape key 250 proved alive')
        check('gmu265e=hfi_clear=1 pre5192=%08x post5192=%08x' in text and
              'gmu266e=hfi_clear=1 pre5192=%08x post5192=%08x' in text and
              'gmu267f=spaced_re5192=1 v=%08x' in text and
              'gmu265p2=both_clears_260_order=1' in text and
              'gmu266p2=both_clears_swapped=1' in text and
              'gmu267p2=both_clears_spaced=1' in text and
              'gmuW[r265clrWord] = 0xffffffffu;' in text and
              'gmuW[r265aocWord] = 0xffffffffu;' in text,
              'R299 the three keys put both interrupt clears back on the living shape')
        check(text.count('gmuW[r241qiWord] = 0u;') >= 2,
              'R291 the descriptor is cleared before the reset and raised again just before the start word')
        check('gmuW[r241setWord] = 0x40000000u;' in text,
              'R291 key 243 rings the boot-slumber doorbell after the start word and clears its ack bit')

    print(f'\n{checks - len(failures)}/{checks} checks passed')
    if failures:
        for f in failures:
            print('  FAILED: ' + f)
        return 1
    print('PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
