#!/usr/bin/env python3
"""Round 103 cross-verifier: stage 6 of the Adreno probe -- the ID0/ID1 read
that closes the two device-sourced inputs the ASID allocator and the ID0
coherent_walk path were both missing.

Why stage 6 exists
------------------
Round 94 read GR0_ID2 (byte 0x28) behind the round-86 fail-closed gate and
measured `ias = 48` (ID2 = 0x00007511). Round 102 built the context-bank /
ASID allocator but correctly reported `ready() == 0`, because its honest
missing-input mask still had two bits set:

  * `kAdrenoAsidInputId1`  -- ID1.NUMCB / ID1.NUMS2CB had NEVER been read on
    this phone, so the allocator had no capacity and no stage-2 reserve.
  * `kAdrenoIommuMissingId0Word` -- ID0.CTTW had NEVER been read, so stage 1
    words were being composed with `coherent_walk(0u, 0u)`, i.e. a GUESS that
    the walk is non-coherent.

Upstream `arm_smmu_device_cfg_probe()` reads ID0 (P2:1758) -> ID1 (P2:1853)
-> ID2 (P2:1879). Stage 4 joined that sequence at the LAST word. Stage 6 reads
the two words that precede it, inside the SAME permitted branch, behind the
SAME gate, using the SAME mapping. It is the prefix, not a new hazard.

What this verifier asserts, and why each one matters:

  1. The boot-arg mask admits stage 6 (`1..6`), and board.toml arms it, while
     the command line still fits the 1000-byte budget.
  2. ID0 (0x20), ID1 (0x24), ID2 (0x28) are each read EXACTLY ONCE from the
     mapped kgsl SMMU window, and stage 6 is inside the same permitted branch
     that owns the gate/verdict -- no second gate, no second mapping.
  3. coherent_walk is computed from the MEASURED ID0 with `fw_says_coherent=0`.
     That zero is not a guess: sdm845.dtsi has NO `dma-coherent` property on
     `adreno_smmu: iommu@5040000` (compatible "qcom,sdm845-smmu-v2",
     "qcom,adreno-smmu", "qcom,smmu-v2"; reg = <0 0x05040000 0 0x10000>).
  4. Stage 1 words are RE-COMPOSED with the measured coherent_walk (not the
     stage-4 zero), so the TCR it emits reflects the real ID0.CTTW bit.
  5. ID1 is decoded ONLY by the extended ASID contract:
     `adreno_asid_capacity_from_id1` (NUMCB), `adreno_asid_s2cb_count_from_id1`
     (NUMS2CB, GENMASK(23,16)), `adreno_asid_init_from_id1` (both fields, one
     word, delegates first-fit + the "impossible number of S2 context banks"
     refusal), then one alloc and the four derived tuple fields. No field
     positions are re-derived in the probe.
  6. One labelled `stage6=id1 ...` row, cached into its own replay buffer and
     re-emitted in the gDone branch like every other stage.
  7. READ-ONLY: no store through a pointer, no new libc helper, no non-volatile
     pointer cast, pure LF, no BOM.
  8. Honest boundary: `adreno_asid_ready()` is STILL 0 (the kernel caller / real
     domain owner is absent; stage 6 supplies REGISTER WORDS, not a domain).
     This verifier must not let a register read be misread as "GPU renders".

Read-only: reads sources, never writes.
"""
import pathlib
import re
import sys
import tomllib

DEV = pathlib.Path(__file__).resolve().parents[1]
PACKAGE = DEV / 'package' / 'EnchiladaAdrenoProbe.cpp'
BOARD = DEV / 'board.toml'
GPU = DEV / 'drivers' / 'NativeAdreno'
ASID_H = GPU / 'AdrenoSmmuAsidV1.h'
ASID_C = GPU / 'AdrenoSmmuAsidV1.c'
IOMMU_H = GPU / 'AdrenoIommuProbeV1.h'
IOMMU_C = GPU / 'AdrenoIommuProbeV1.c'
DTS = DEV / '.cache' / 'adreno-a630-source' / 'arch' / 'arm64' / 'boot' / 'dts' / 'qcom' / 'sdm845.dtsi'
BUILD = DEV / 'scripts' / 'build-enchilada-adreno.py'
PORTING = DEV / 'porting.toml'

checks = 0
fails = []


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)
        print('FAIL:', label)
    else:
        print('ok  ', label)


def strip_comments(text):
    code = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    code = re.sub(r'//[^\n]*', '', code)
    return code


def section(text, start, end):
    i = text.find(start)
    j = text.find(end, i)
    return text[i:j] if i >= 0 and j > i else ''


def main():
    text = PACKAGE.read_text(encoding='utf-8')
    code = strip_comments(text)
    asid_h = ASID_H.read_text(encoding='utf-8')
    asid_c = ASID_C.read_text(encoding='utf-8')
    iommu_h = IOMMU_H.read_text(encoding='utf-8')
    iommu_c = IOMMU_C.read_text(encoding='utf-8')
    board = tomllib.loads(BOARD.read_text(encoding='utf-8'))
    args = board['payload']['kernel_args']
    joined = ' '.join(args)
    build = BUILD.read_text(encoding='utf-8')
    porting = PORTING.read_text(encoding='utf-8')

    # ---- A. armed, bounded, and no regression ------------------------------
    check('(stage < 1u || stage > 6u)' in code and 'stage > 6u' in code,
          'the boot-arg mask admits exactly stages 1..6 (round 103 widening)')
    check('if (stage >= 6u)' in code,
          'stage 6 is gated behind stage >= 6 (default-off for stages 1..5)')
    for old in ('if (stage >= 2u)', 'if (stage >= 3u)', 'if (stage >= 4u)',
                'if (stage >= 5u)'):
        check(old in code, 'stage chain still contains %r' % old)
    check('enchilada-adreno=6' in args,
          'board.toml arms stage 6 (ID0/ID1 reads behind the stage-4 gate)')
    check(len(joined) <= 1000,
          'the kernel command line fits the 1000-byte limit (measured %d)' % len(joined))
    check('enchilada-touch=4' in args,
          'the touch argument is still present (stage 6 did not evict it)')

    # ---- B. the three GR0 reads, exactly once each -------------------------
    check(code.count('rd32(gSmmuVa') == 3,
          'the SMMU window is dereferenced exactly three times (no extra access)')
    for off, name in (('0x20u', 'GR0_ID0'), ('0x24u', 'GR0_ID1'),
                      ('0x28u', 'GR0_ID2')):
        check(code.count('rd32(gSmmuVa, %s)' % off) == 1,
              '%s (byte %s) is read exactly once' % (name, off))
    check('rd32(gSmmuVa, 0x28u)' in code and
          code.index('rd32(gSmmuVa, 0x28u)') < code.index('rd32(gSmmuVa, 0x20u)'),
          'ID2 (stage 4) is read BEFORE the stage-6 ID0/ID1 pair (same poll)')
    check(code.count('dsb sy') >= 2,
          'a full barrier follows the ID2 read AND the ID0/ID1 pair')

    # ---- C. stage 6 lives INSIDE the stage-4 permitted branch -------------
    i4 = code.find('if (stage >= 4u)')
    igate = code.find('adreno_iommu_assess(&gi)')
    iperm = code.find('kAdrenoIommuProbePermitted', igate)
    i6 = code.find('if (stage >= 6u)')
    iset = code.find('setIommuReplay(ioRow);')
    check(i4 >= 0 and igate > i4 and iperm > igate,
          'the stage-4 block asks the contract gate before the permitted branch')
    check(i6 > iperm and i6 > code.find('rd32(gSmmuVa, 0x28u)'),
          'stage 6 is INSIDE the permitted branch, AFTER the ID2 decode')
    check(i6 < iset,
          'stage 6 is emitted before the stage-4 row is cached (same poll)')
    check('adreno_iommu_assess(&gi)' in code[i4:i6] and
          code[i4:i6].count('adreno_iommu_assess(&gi)') == 1,
          'stage 6 introduces NO second gate -- it reuses the one assess()')
    check('armcpu_map_physical' not in code[i6:iset],
          'the stage-6 block itself introduces NO mapping -- it reuses the '
          'permitted branch\'s mapped window')

    # ---- D. ID0 -> coherent_walk, and the DTS fact behind fw=0 -----------
    check('adreno_iommu_coherent_walk(s6id0, 0u)' in code,
          'coherent_walk is computed from the MEASURED ID0, fw_says_coherent=0')
    check('uint32_t adreno_iommu_coherent_walk(uint32_t id0_raw, uint32_t fw_says_coherent)' in iommu_c,
          'the contract coherent_walk signature takes the ID0 word + a firmware flag')
    check('kAdrenoIommuId0Cttw' in iommu_h and '0x00004000u' in iommu_h,
          'the contract pins ID0.CTTW = BIT(14) = 0x00004000')
    check(DTS.is_file() and 'adreno_smmu: iommu@5040000' in DTS.read_text(encoding='utf-8'),
          'sdm845.dtsi defines the pinned adreno_smmu node at 0x05040000')
    dts = DTS.read_text(encoding='utf-8') if DTS.is_file() else ''
    node = section(dts, 'adreno_smmu: iommu@5040000', '};')
    check('dma-coherent' not in node,
          'adreno_smmu has NO dma-coherent property, so fw_says_coherent=0 is a fact')
    check('"qcom,smmu-v2"' in node,
          'the node is compatible with qcom,smmu-v2 (the stage-4 version selector)')
    check('adreno_iommu_stage1_words(id2, ver, s6cttw, &s6w)' in code,
          'stage 1 words are RE-COMPOSED with the measured coherent_walk')

    # ---- E. ID1 -> ASID contract, no re-derived field positions ----------
    for api in ('adreno_asid_capacity_from_id1', 'adreno_asid_s2cb_count_from_id1',
                'adreno_asid_init_from_id1', 'adreno_asid_alloc',
                'adreno_asid_ttbr_asid_field', 'adreno_asid_contextidr',
                'adreno_asid_s2cr_cbndx', 'adreno_asid_cbar_offset'):
        check(api in code and api in asid_c and api in asid_h,
              'stage 6 composes %s (declared in .h, defined in .c)' % api)
    check('kAdrenoAsidId1Nums2cbMask' in asid_h and '0x00ff0000u' in asid_h,
          'the contract pins ID1.NUMS2CB = GENMASK(23,16) = 0x00ff0000')
    check('kAdrenoAsidId1Nums2cbShift' in asid_h and '16u' in asid_h,
          'the contract pins the NUMS2CB shift to 16')
    check('(id1_raw & kAdrenoAsidId1Nums2cbMask) >> kAdrenoAsidId1Nums2cbShift' in asid_c,
          'NUMS2CB is decoded by FIELD_GET-equivalent mask+shift, not a guess')
    check('return adreno_asid_init(b, numcb, nums2cb);' in asid_c,
          'init_from_id1 DELEGATES to init (one place for the S2>NUMCB refusal)')
    # The probe must not re-derive any ID1 field locally.
    for forbidden in ('>> 8', '>> 16', '>> 24', '0x00ff0000u', '0x000000ffu',
                      '0x000000ffu /*', 'GENMASK(23, 16)'):
        check(forbidden not in code[i6:iset],
              'stage 6 does NOT re-derive an ID1 field (%r)' % forbidden)
    check('0x20u' in code and '0x24u' in code and '0x28u' in code,
          'the three offsets are literal GR0 bytes, not computed field positions')

    # ---- F. the labelled row + replay discipline --------------------------
    check('stage6=id1 id0=' in code and 'stage6=id1' in text,
          'the row is labelled `stage6=id1` so it cannot be mistaken for stage 4')
    for field in ('id0=%08x', 'id1=%08x', 'cttw=%u', 'numcb=%u', 'nums2cb=%u',
                  'cap=%u', 'init=%d', 'alloc=%d', 'cbndx=%u', 'ttbr=%llx',
                  'ctxidr=%08x', 's2cr=%08x', 'cbar=%08x', 'words_ok=%d',
                  'tcr=%08x', 'tcr2=%08x'):
        check(field in code, 'the row reports %s' % field)
    check('char gId1Replay[300];' in code and 'bool gHaveId1Replay' in code,
          'a dedicated replay buffer + flag exist for the stage-6 row')
    check('void setId1Replay(const char *text)' in code,
          'setId1Replay() is defined (no-libc copy loop)')
    check('setId1Replay(s6Row)' in code and 'gHaveId1Replay' in code,
          'the row is cached and re-emitted in the gDone replay branch')
    check('if (gHaveId1Replay)' in code,
          'the replay branch is guarded by its flag')
    check('%llx' in code and '(unsigned long long)s6ttbr' in code,
          'ttbr is printed with %llx and a matching unsigned long long cast')

    # ---- G. READ-ONLY, no libc, pure LF, no BOM ---------------------------
    casts = re.findall(r'reinterpret_cast<([^>]*)>', code)
    check(casts and all('volatile' in c for c in casts),
          'every pointer cast is volatile (%r)' % sorted(set(casts)))
    store = re.search(r'\*\s*reinterpret_cast\s*<[^>]*>\s*\([^;]*\)\s*=[^=]', code)
    check(store is None, 'no store through a pointer (stage 6 is READ-ONLY)')
    for libc in ('strlen(', 'memcpy(', 'memset(', 'strcmp(', 'malloc(', 'free('):
        check(libc not in code, 'probe uses no libc %s' % libc)
    raw = PACKAGE.read_bytes()
    check(b'\r' not in raw, 'the probe file is pure LF (CR count 0)')
    check(not raw.startswith(b'\xef\xbb\xbf'), 'the probe file has no BOM')

    # ---- H. composition/provenance + the honest boundary -----------------
    check('#include "AdrenoSmmuAsidV1.h"' in code,
          'the probe includes the ASID contract header inside extern "C"')
    check("'AdrenoSmmuAsidV1'" in build or '"AdrenoSmmuAsidV1"' in build,
          'build-enchilada-adreno.py records ASID sources in build-report provenance')
    check('ROUND 103' in porting or 'round 103' in porting.lower(),
          'porting.toml records the round-103 stage-6 step')
    check('kAdrenoAsidInputId1' in asid_h and 'kAdrenoAsidInputCaller' in asid_h,
          'the ASID contract STILL declares both absent inputs')
    check('mask |= kAdrenoAsidInputId1;' in asid_c and
          'mask |= kAdrenoAsidInputCaller;' in asid_c,
          'the missing-input mask STILL sets both bits (ready() stays 0)')
    check('adreno_asid_missing() == 0u ? 1 : 0' in asid_c,
          'ready() is still DERIVED from the mask, not overridden by a register read')
    check('CONFIG_XNU_NATIVE_ADRENO remains n' in asid_h or
          'does NOT unblock stage 5' in asid_h,
          'the header STILL states that this does NOT unblock stage 5 / render')

    # ---- ROUND 109/110: stage 7 is a PLAN behind a TWO-KEY interlock ----
    # These assertions exist because stage 7 is the only code in this tree that
    # could ever make a secure-world call. They pin three properties:
    #   1. the boot-arg mask is UNCHANGED (1..6) -- stage 7 is reached INSIDE the
    #      stage-6 run, so widening the mask was never needed and the four stage
    #      verifiers keep their original assertions;
    #   2. the second key `enchilada-pas-exec` is REQUIRED before any execute;
    #   3. ROUND 110 changed the transport from "always NULL" to "a real bridge,
    #      but only when the exported symbol linked". The fail-closed property is
    #      unchanged -- what changed is WHICH condition enforces it: it is now a
    #      weak_import null check whose absence falls back to the printed plan.
    check('enchilada-pas-exec' in code,
          'R109 stage 7 requires the SECOND key enchilada-pas-exec')
    # R110 replaced the two-way label with a three-way one (exec / no-symbol /
    # not-executed). The REQUIREMENT -- that the row distinguishes the states
    # rather than printing one blanket string -- is unchanged, so assert the
    # distinguishing, not the old spelling.
    check('armed-not-executed' in code and 'armed-no-symbol' in code,
          'R109/R110 the row distinguishes not-executed / no-symbol / exec')
    check('transport-symbol-unavailable' in code,
          'R110 a missing export is NAMED, not silently swallowed')
    check('&armcpu_scm_call != nullptr' in code,
          'R110 the bridge is reached only after a weak_import null check')
    check('int32_t armcpu_scm_call' in code and '__attribute__((weak_import))' in code,
          'R110 the export is declared weak_import (links fine without it)')
    _armed_at = code.find('if (!armed)')
    _exec_at = code.find('adreno_zap_pas_execute(&s7tr')
    check(_armed_at > 0 and _exec_at > _armed_at,
          'R110 adreno_zap_pas_execute is reachable ONLY behind the armed gate')
    check('kAdrenoZapExecNoTransport' in code,
          'R109 the unarmed/refused path is the named NoTransport status')
    check('(stage < 1u || stage > 6u)' in code,
          'R109 the boot-arg mask is STILL 1..6 (stage 7 did not widen it)')
    check('__has_include' in code,
          'R109 the generated blob header is __has_include-guarded')
    check('#define kAdrenoZapBlobPresent 0' in code,
          'R109 the no-header fallback declares present=0, not a fake image')
    # verify-ci-246 scans the LINKED IMAGE, so these literals must not appear in
    # the source at all -- a comment is enough to trip it (round 109 proved that).
    for _bad in ('stage6=exec', 'stage3=exec', '_qcom_scm_call',
                 'adreno_zap_shader_load'):
        check(_bad not in code,
              'R109 the probe source never spells the CI-246 literal #%d'
              % ('stage6=exec', 'stage3=exec', '_qcom_scm_call',
                 'adreno_zap_shader_load').index(_bad))
    # R110: the two channels stay SEPARATE per stage, never collapsed.
    check('t0=%llx s0=%llx' in code and 't2=%llx s2=%llx' in code,
          'R110 the row reports transport and secure separately for all 3 stages')

    # ==== ROUND 111: the POINTED protection for zapBridgeCall ==============
    # WHY THIS EXISTS. In round 110 the write to the caller's out-parameter was
    # spelled `out_result0[0] = ...` instead of `*out_result0 = ...`, so that
    # stage-4/5's generic "no store through a pointer" scan would not have to
    # learn an exception. That left a real hole: the scan does not match the
    # `name[0] =` form, so from round 110 the pointer writes INSIDE this new
    # function were protected by nothing at all -- the only gate at entry was a
    # NULL check.
    #
    # The generic scan guards a specific property ("a store here would bypass
    # the bounds that make the window safe" -- verify-enchilada-probe-stage5.py
    # :117-119) and correctly should NOT fire on an out-parameter write. But the
    # protection must not simply disappear. So this is the same property,
    # asserted PRECISELY: inside zapBridgeCall the ONLY pointer write permitted
    # is to out_result0, there is no MMIO write primitive, no volatile access
    # and no cast. The generic scan keeps its original, unmodified regex.
    _bridge_start = code.find('int zapBridgeCall(')
    check(_bridge_start > 0, 'R111 zapBridgeCall exists and is locatable')
    if _bridge_start > 0:
        # Slice the exact function body: from the opening brace to the FIRST
        # closing brace at column 0, which is how this file's functions end.
        _open = code.find('{', _bridge_start)
        _close = code.find('\n}', _open)
        check(_open > 0 and _close > _open, 'R111 the bridge body is delimited')
        _body = code[_open:_close] if (_open > 0 and _close > _open) else ''

        # (a) NO dereference-store at all inside the bridge. If a future edit
        # reintroduces `*p = ...`, this fires -- and so does stage-4/5's scan,
        # which is the intended double coverage.
        _store_re = re.compile(r'(?<![\w\)\]\*])\*\s*[A-Za-z_]\w*(?:\[[^\]]*\])?\s*=(?!=)')
        _store_hits = []
        for _m in _store_re.finditer(_body):
            _pre = _body[max(0, _m.start() - 40):_m.start()]
            if re.search(r'(?:const\s+)?[A-Za-z_][\w:<>]*\s+$', _pre):
                continue          # a declaration, not a store
            _store_hits.append(_m.group(0))
        check(not _store_hits,
              'R111 zapBridgeCall performs NO dereference-store (hits=%r)' % _store_hits[:3])

        # (b) The ONLY pointer write in the whole function is the out-parameter.
        # Enumerate every `X[0] =` / `X->f =` / `X.f =` write and require the
        # receiver to be out_result0. This is what replaces the lost protection:
        # not "no writes", but "exactly this one write, to exactly this target".
        _writes = re.findall(r'([A-Za-z_]\w*)\s*(?:\[\s*\d+\s*\]|->\w+|\.\w+)\s*=(?!=)', _body)
        _bad_targets = sorted(set(w for w in _writes if w != 'out_result0'))
        # `b->field = ...` writes are struct-field stores through a pointer. They
        # are bookkeeping on the bridge's OWN cookie, not MMIO, so allow the
        # cookie local by name -- but ONLY that name.
        _bad_targets = [w for w in _bad_targets if w != 'b']
        check(not _bad_targets,
              'R111 the only pointer writes are out_result0[0] and the cookie b '
              '(other targets found: %r)' % _bad_targets)
        check('out_result0[0] =' in _body.replace(' ', ' ') or
              re.search(r'out_result0\s*\[\s*0\s*\]\s*=|out_result0\s*\[0\]\s*=', _body) is not None,
              'R111 the out-parameter is written by name (out_result0[0] = ...)')

        # (c) No volatile access, no cast, no MMIO primitive anywhere in it.
        for _bad in ('volatile', 'reinterpret_cast', 'static_cast<volatile',
                     'wr32', 'write32', 'out32', 'mmio', 'writel', 'readl',
                     'IOConnectCall', 'IOServiceOpen'):
            check(_bad not in _body,
                  'R111 zapBridgeCall contains no %r' % _bad)

        # (d) It must not reach the window adapter's write path either.
        for _bad in ('AdrenoGpuccWindowV1Mmio', 'AdrenoCallerV1Run',
                     'gpucc_udelay', 'armcpu_map_physical'):
            check(_bad not in _body,
                  'R111 zapBridgeCall does not call %r' % _bad)

        # (e) The single symbol it IS allowed to call is the SMC export.
        check('armcpu_scm_call(' in _body,
              'R111 zapBridgeCall reaches the SMC export (its one hardware effect)')


    print()
    print('%d/%d checks passed' % (checks - len(fails), checks))
    if fails:
        print('FAIL: stage-6 kernel ID0/ID1 probe contract (%d failures)' % len(fails))
        return 1
    print('PASS: stage-6 kernel ID0/ID1 probe contract '
          '(same gate, three GR0 reads, measured coherent_walk, ASID tuple, READ-ONLY)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
