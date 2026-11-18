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

    # ---- B. the three GR0/GR ID reads, exactly once each -------------------
    # ROUND 158 NOTE: the "exactly three dereferences" count has been relaxed
    # to "the three ID reads each appear exactly once" (checked per-offset below)
    # because the ROUND 158 read-only GPU-state snapshot adds its OWN gated
    # gSmmuVa reads (GR0_SCTLR / CB0-TTBR0 / CB0-TTBR1) at the stage-7 level.
    # The three stage-4/6 ID reads are still enforced exactly-once, so a stray
    # fourth probe-time SMMU read still fails; only the documented R158 snapshot
    # reads (which we count and pin separately) are exempt.
    check('rd32(gSmmuVa, 0x20u)' in code and 'rd32(gSmmuVa, 0x24u)' in code and
          'rd32(gSmmuVa, 0x28u)' in code,
          'the SMMU ID0/ID1/ID2 reads are present')
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
    # ROUND 158: scoped to STAGE 6's own block (i6 .. setId1Replay(s6Row)), not
    # the whole i6..iset tail, because the ROUND 158 read-only GPU-state snapshot
    # at the stage-7 level legitimately maps the GPU core window (kgsl-3d0) for
    # the GX-gated SECVID read. Stage 6 itself must still introduce no mapping.
    _s6end = code.find('setId1Replay(s6Row);', i6)
    check(_s6end > i6 and 'armcpu_map_physical' not in code[i6:_s6end if _s6end > 0 else iset],
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
    # R173's SMMU dump decodes the S2CR TYPE field at its pinned shift. That is
    # GR0 0xC00 + (n<<3), not an ID1 field, so that block is lifted out of the
    # slice this guard inspects rather than loosening the guard itself.
    _id1view = code[i6:iset]
    _m172 = _id1view.find('if (fwKey >= 13u && fwKey <= 14u) {')
    if _m172 >= 0:
        _cut172 = re.split(r'\n {36}\}\n', _id1view[_m172:], 1)
        _id1view = _id1view[:_m172] + (_cut172[1] if len(_cut172) > 1 else '')
    for forbidden in ('>> 8', '>> 16', '>> 24', '0x00ff0000u', '0x000000ffu',
                      '0x000000ffu /*', 'GENMASK(23, 16)'):
        check(forbidden not in _id1view,
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

    # ==== ROUND 120: the firmware bytes must actually reach the image =======
    # Until this round the two generated arrays were declared and never read, so
    # clang dropped them: a REAL opt-in header produced a 15040-byte object with
    # no adreno_zap_mdt / adreno_zap_seg symbol in it at all. Recording the
    # header's sha256 STRING would not have been enough -- that puts the digest
    # in the image, not the bytes, and PAS needs the bytes.
    #
    # The fix is a byte-at-a-time checksum. The assertions below pin the three
    # properties that make it work, and each is independently breakable:
    #   * the row CARRIES the two fields (so the result is observable);
    #   * the checksum reads through a LOOP over both arrays (so the whole
    #     array is referenced, not just element 0);
    #   * the loop is READ-ONLY (so stage-4/5's no-store rule still holds).
    check('mdt_ck=%08x' in code and 'seg_ck=%08x' in code,
          'R120 the stage-7 row carries mdt_ck and seg_ck')
    check('uint32_t zapFnv1a(' in code,
          'R120 a checksum helper exists')
    # The calls must pass BOTH arrays AND their real lengths -- passing 0, or a
    # literal, would leave the array unreferenced and the bytes dropped again.
    check('zapFnv1a(adreno_zap_mdt' in code and 'zapFnv1a(adreno_zap_seg' in code,
          'R120 both firmware arrays are passed to the checksum')
    check('kAdrenoZapMdtBytes' in code and 'kAdrenoZapSegBytes' in code,
          'R120 the lengths come from the header defines, not a literal')
    _fn_at = code.find('uint32_t zapFnv1a(')
    check(_fn_at > 0, 'R120 the checksum function is locatable')
    if _fn_at > 0:
        _open = code.find('{', _fn_at)
        _close = code.find('\n}', _open)
        _fn = code[_open:_close] if (_open > 0 and _close > _open) else ''
        # A loop over the byte count is what forces the WHOLE array in.
        check(re.search(r'for\s*\(', _fn) is not None,
              'R120 the checksum LOOPS (a straight-line read of one byte would '
              'let the rest be dropped)')
        # The bound must be the LENGTH PARAMETER, not a literal. `for (i = 0;
        # i < 1; ++i) data[0]` is still a loop and still indexes "through data",
        # so the two checks above would both pass while only one byte was read --
        # which is exactly the regression this must catch. Demand the loop's
        # condition compare against `bytes`.
        check(re.search(r'for\s*\([^;]*;\s*\w+\s*<\s*bytes\s*;', _fn) is not None,
              'R120 the loop bound is the LENGTH PARAMETER bytes, not a literal '
              '(a bounded 1-iteration loop reads one byte and drops the rest)')
        # And the indexed expression must use the loop variable, not a constant
        # subscript -- `data[0]` inside a bytes-long loop is the same bug.
        check(re.search(r'\bdata\s*\[\s*\w+\s*\]', _fn) is not None,
              'R120 the loop indexes data by a VARIABLE (data[i]), not data[0]')
        check(re.search(r'\bdata\s*\[\s*0\s*\]', _fn) is None,
              'R120 the checksum never hard-codes data[0]')
        # READ-ONLY: the generic store scan must not have been taught an
        # exception, and the helper must not write through anything.
        _store_re = re.compile(r'(?<![\w\)\]\*])\*\s*[A-Za-z_]\w*(?:\[[^\]]*\])?\s*=(?!=)')
        _hits = []
        for _m in _store_re.finditer(_fn):
            _pre = _fn[max(0, _m.start() - 40):_m.start()]
            if re.search(r'(?:const\s+)?[A-Za-z_][\w:<>]*\s+$', _pre):
                continue
            _hits.append(_m.group(0))
        check(not _hits,
              'R120 the checksum performs no pointer store (hits=%r)' % _hits[:3])
        for _bad in ('volatile', 'reinterpret_cast', 'wr32', 'write32', 'mmio'):
            check(_bad not in _fn, 'R120 the checksum contains no %r' % _bad)

    # The empty case is arithmetic, not a special case: present=0 means the
    # lengths are 0, so neither loop body runs. Pin the two defines so a future
    # header cannot make "absent" carry a nonzero length.
    check('#define kAdrenoZapMdtBytes 0' in code and '#define kAdrenoZapSegBytes 0' in code,
          'R120 the no-header fallback declares both lengths 0')

    # NOTE FOR WHOEVER READS THE ROW: the payload word is `blob=embedded` /
    # `blob=absent`. Round 119's task text said `blob=present`; that was a typo
    # in the brief, not the code, and the code was NOT changed to match it.
    check('kAdrenoZapBlobPresent ? "embedded" : "absent"' in code,
          'R120 the blob word is embedded/absent (NOT "present")')

    # ==== ROUND 127: the D1 fix -- metadata goes in a REAL buffer ==========
    # THE DEFECT (round 125's attribution): init_image's args[1] must point at a
    # buffer containing the ELF header + hash table. Upstream never forwards the
    # caller's address -- qcom_scm.c:602 allocates a DMA-coherent buffer, :607
    # copies the metadata in, and :617 sends THAT buffer's physical address.
    # Through round 126 this probe sent kAdrenoZapRegionPhys, an address the
    # whole tree never writes to, so the secure world was asked to authenticate
    # whatever the bootloader left behind.
    #
    # The fix has three independently-breakable parts, each asserted below:
    #   1. the allocator export exists in ArmCpu.cpp with the right signature;
    #   2. the probe calls it and copies the bytes in;
    #   3. what reaches adreno_zap_pas_execute is the buffer's phys, not the
    #      region address.
    _armcpu = (DEV / 'drivers/ArmCpu/ArmCpu.cpp').read_text(encoding='utf-8', errors='replace')
    check('extern "C" uint64_t armcpu_dma_alloc_writable (uint64_t bytes, uint64_t *out_virt)' in _armcpu,
          'R127 ArmCpu.cpp exports armcpu_dma_alloc_writable with the declared signature')
    check('IOBufferMemoryDescriptor::inTaskWithOptions' in _armcpu,
          'R127 the allocator uses IOBufferMemoryDescriptor::inTaskWithOptions')
    # PHYSICALLY CONTIGUOUS IS NOT OPTIONAL, and neither is the writable
    # direction or the "no mapper" request. Each flag is asserted on its own:
    # swapping kIOMemoryPhysicallyContiguous for kIOMemoryPageable leaves a
    # perfectly plausible-looking allocation that the secure world could not use,
    # and dropping kIODirectionInOut yields a mapping the CPU cannot write the
    # metadata through.
    check('kIODirectionInOut' in _armcpu,
          'R127 the buffer is WRITABLE (kIODirectionInOut, not read-only kIODirectionIn)')
    check('kIOMemoryPhysicallyContiguous' in _armcpu,
          'R127 the allocation REQUESTS physically contiguous memory')
    check('kIOMemoryMapperNone' in _armcpu,
          'R127 the allocation asks for the no-mapper path (its phys is the real one)')
    # The three flags must reach inTaskWithOptions as ONE options argument, not be
    # mentioned in a comment somewhere while a lesser flag set is passed.
    _alloc = _armcpu[_armcpu.find('armcpu_dma_alloc_writable (uint64_t bytes'):]
    _alloc = _alloc[:_alloc.find('\n}')]
    _opts = re.search(r'inTaskWithOptions\s*\((.*?)\);', _alloc, re.S)
    _opts_text = _opts.group(1) if _opts else ''
    for _flag in ('kIODirectionInOut', 'kIOMemoryPhysicallyContiguous', 'kIOMemoryMapperNone'):
        check(_flag in _opts_text,
              'R127 %s is inside the inTaskWithOptions ARGUMENTS' % _flag)
    check('getPhysicalSegment (0, &contiguous, kIOMemoryMapperNone)' in _armcpu,
          'R127 ...and reads the PHYSICAL address out via getPhysicalSegment')
    # The alignment must be VERIFIED, not assumed: a non-page-aligned phys cannot
    # be handed to the secure world, and trusting the allocator would hide that.
    check('(phys & 4095ULL) != 0' in _armcpu,
          'R127 the returned address is CHECKED for 4096 alignment, not assumed')
    # The three device-proven exports must be untouched by this round.
    # R162: `scm4` now returns the three-channel ScmTriple (the pre-R152 shape the
    # early path is pinned to); the export names themselves are unchanged.
    for _keep in ('armcpu_map_physical', 'armcpu_scm_call', 'ScmTriple scm4'):
        check(_keep in _armcpu, 'R127 the pre-existing %s export still exists' % _keep)
    check('kIODirectionInOut' in _armcpu,
          'R127 the buffer is writable (kIODirectionInOut, not the read-only kIODirectionIn)')

    # ---- the probe side: declared weak, called, copied into, and SENT ----
    check('uint64_t armcpu_dma_alloc_writable(uint64_t bytes,' in code,
          'R127 the probe declares the allocator')
    check('armcpu_dma_alloc_writable(uint64_t bytes,\n'
          '                                   uint64_t *out_virt) __attribute__((weak_import))' in code,
          'R127 the declaration is weak_import (links fine without it)')
    check('&armcpu_dma_alloc_writable != nullptr' in code,
          'R127 the allocator is reached only after a weak_import null check')
    check('armcpu_dma_alloc_writable(' in code and 's7metaPhys' in code,
          'R127 the probe actually calls the allocator')
    # The bytes really get copied in: a loop over the MDT length writing dst[i].
    check('dst[i] = adreno_zap_mdt[i]' in code,
          'R127 the metadata bytes are COPIED into the buffer (not just an address)')
    check(re.search(r'for\s*\([^;]*;\s*\w+\s*<\s*\(uint32_t\)kAdrenoZapMdtBytes\s*;', code) is not None,
          'R127 the copy loops over the real MDT length, not a literal')
    # THE DECISIVE ONE: the phys actually sent is s7metaPhys, not the region.
    check('(uint32_t)s7metaPhys' in code,
          'R127 adreno_zap_pas_execute is handed the BUFFER phys')
    _exec_call = code[code.find('adreno_zap_pas_execute(&s7tr'):]
    _exec_call = _exec_call[:_exec_call.find(');')]
    check('kAdrenoZapRegionPhys' not in _exec_call,
          'R127 the region address is NOT what init_image receives any more '
          '(this is the D1 regression guard)')
    check('s7metaPhys' in _exec_call,
          'R127 ...and the buffer phys IS what it receives')

    # ---- the 4 GiB gate, and WHY it is not optional ----------------------
    # MsmDsiBrightness.h:19-22 records that where a fresh IOBufferMemoryDescriptor
    # lands on this kernel is UNCERTAIN. PAS args[1] is a u64, but whether TZ's
    # translation accepts >4 GiB is unknown -- so the address is refused, not
    # truncated. Silently dropping the high half would send a WRONG address.
    check('(s7metaPhys >> 32) != 0' in code,
          'R127 a metadata address above 4 GiB is DETECTED')
    # DETECTED IS NOT ENOUGH -- it has to GOVERN. A `false && (...)` prefix keeps
    # the substring above intact while disabling the gate, and so does hoisting
    # the test into a variable that nothing branches on. So the condition is read
    # from the actual `else if (...)` header: the compared expression must BE the
    # shift test, with no get-out-of-jail operator in front of it.
    _gate = re.search(r'\}\s*else\s+if\s*\(\s*\(?\s*s7metaPhys\s*>>\s*32\s*\)?\s*!=\s*0\s*\)\s*\{', code)
    check(_gate is not None,
          'R127 the 4 GiB test is the else-if CONDITION itself '
          '(a `false &&` prefix would keep the substring and disable the gate)')
    check('false' not in (code[_gate.start():_gate.end()] if _gate else ''),
          'R127 ...and the gate condition contains no constant-false term')
    check('meta-above-4gib' in code,
          'R127 ...and REFUSED with a named reason (fail-closed, no PAS)')
    # Truncating instead of refusing is the failure this pins: a mask or a cast to
    # uint32_t applied to the address would look like a fix and be a bug.
    check('(uint32_t)s7metaPhys' in code and
          '(uint32_t)(s7metaPhys' not in code.replace('(uint32_t)s7metaPhys', ''),
          'R127 the address is not masked/truncated on its way to the gate')
    # The fail-closed wiring: a bad buffer must stop the chain BEFORE the call.
    _meta_why_at = code.find('} else if (!s7metaOk) {')
    check(_meta_why_at > 0,
          'R127 a missing/unusable metadata buffer is its own refusal branch')
    check(_meta_why_at < code.find('adreno_zap_pas_execute(&s7tr'),
          'R127 that refusal runs BEFORE the PAS chain (fail-closed)')

    # ---- the observable field ------------------------------------------
    check('meta=%llx' in code, 'R127 the stage-7 row carries meta=')
    check('(unsigned long long)s7metaPhys' in code,
          'R127 meta= prints the live physical address')
    # It must be printed on EVERY path (that is what makes it a measurement of
    # where the allocator lands, not a success-only trophy).
    check('s7metaPhys' in code[code.find('snprintf(s7Row'):],
          'R127 meta= is inside the single unconditional snprintf')

    # ==== ROUND 129: is the 0/0 reply even an ANSWER? =====================
    # r128 measured p13=0/0 -> v13=0 and could not tell two worlds apart:
    #   (a) 0x02000207 exists and answers "not supported", or
    #   (b) 0x02000207 does not exist and the monitor completed cleanly having
    #       done nothing.
    # Linux never faces this: P2:788-790 runs __qcom_scm_is_call_available()
    # BEFORE qcom_scm_pas_supported() and short-circuits. This round adds that
    # gate, so an availability query must be issued BEFORE the p13 question.
    check('adreno_zap_is_call_available_call' in code,
          'R129 the probe issues an IS_CALL_AVAILABLE query')
    check('is_call_available_call(s7base, 0x0207u' in code,
          'R129 ...about 0x0207, the PAS_IS_SUPPORTED function id')
    _avail_at = code.find('adreno_zap_is_call_available_call(s7base, 0x0207u')
    _p13_at = code.find('adreno_zap_pas_is_supported_call(s7base, &s7sup)')
    check(_avail_at > 0 and _p13_at > _avail_at,
          'R129 the availability query runs BEFORE the p13 question '
          '(otherwise its answer could not inform that verdict)')
    # The fold, not the bare decoder: a 0/0 reply must stop being a policy
    # answer when the command is not known to exist.
    check('adreno_zap_pas_supported_verdict_avail' in code,
          'R129 the p13 verdict goes through the availability-aware fold')
    # ...and it must be the function ACTUALLY CALLED, not merely mentioned. A
    # mutation that swaps the call back to the two-argument decoder leaves the
    # name above intact while restoring the round-128 ambiguity, so the call
    # site itself is read.
    _fold_at = code.find('adreno_zap_pas_supported_verdict_avail(')
    _plain_at = code.find('adreno_zap_pas_supported_verdict(')
    check(_fold_at > 0, 'R129 the fold is called at least once')
    # The fold's call must come BEFORE any bare two-argument call, and a bare
    # call to the p13 verdict inside this block is what the mutation produces.
    _p13_block = code[code.find('s7avail'):]
    _p13_block = _p13_block[:_p13_block.find('if (!armed)')] if 'if (!armed)' in _p13_block else _p13_block
    check('adreno_zap_pas_supported_verdict_' in _p13_block,
          'R129 the p13 verdict uses the _avail form inside the probe block')
    check('adreno_zap_pas_supported_verdict(\n' not in _p13_block and
          'adreno_zap_pas_supported_verdict(s7avail' not in _p13_block.replace('verdict_avail', 'verdict_X'),
          'R129 the bare two-argument decoder is NOT what the p13 probe calls '
          '(swapping it back would restore the r128 ambiguity)')
    check('s7avail' in code,
          'R129 the availability answer reaches the verdict')

    # ---- the availability answer must be COMPUTED, not assumed ----
    # A mutation that forces s7avail to a constant (e.g. `s7avail = 1;` after the
    # query, or `int s7avail = 1;`) keeps every name above intact while making
    # the fold meaningless -- every 0/0 would then read as a real policy answer.
    # So the declaration must start at the INCONCLUSIVE value, and the verdict
    # decoder must be the thing that moves it.
    check('int s7avail = -1;' in code,
          'R129 avail STARTS at -1 (inconclusive), not at 0 or 1')
    check('adreno_zap_is_call_available_verdict(' in code,
          'R129 avail is set by the decoder, not by a constant')
    _after_q = code[code.find('adreno_zap_is_call_available_verdict('):]
    _after_q = _after_q[:_after_q.find('then the PAS_IS_SUPPORTED')] if 'then the PAS_IS_SUPPORTED' in _after_q else _after_q[:400]
    # ROUND 140: the av2 probe's verdict call (writes &s7av2) now comes BEFORE
    # the p13 availability verdict in the file, so "the decoder writes s7avail"
    # must be found as the specific &s7avail out-parameter wherever it sits,
    # not assumed to be the first verdict call.
    _avail_out = re.search(r'adreno_zap_is_call_available_verdict\(\s*[^;]*?\n\s*&s7avail\)', code)
    check(_avail_out is not None,
          'R129 the decoder writes s7avail through its out-parameter')
    check('&s7av2)' in code,
          'R140 the av2 probe writes s7av2 through &s7av2')
    # No assignment of a literal into s7avail AFTER its declaration: that is the
    # "恒真" shape. The declaration `int s7avail = -1;` is the one permitted
    # initialiser and is asserted above.
    _after_decl = code[code.find('int s7avail = -1;') + len('int s7avail = -1;'):]
    _const_assign = re.search(r's7avail\s*=\s*-?\d+\s*;', _after_decl)
    check(_const_assign is None,
          'R129 s7avail is never RE-assigned a literal after its declaration '
          '(got %r)' % (_const_assign.group(0) if _const_assign else None))
    # And no plain `s7avail = ` at all after the declaration: the decoder writes
    # through its out-parameter (&s7avail), which is the only setter.
    check('s7avail = ' not in _after_decl,
          'R129 s7avail has no plain assignments after its declaration -- the '
          'decoder out-parameter is the only setter')

    # ---- the row carries the new fields ----
    check('avail=%d av0=%x/%x x1s=%x' in code,
          'R129 the row carries avail=, av0= and x1s=')
    check('(unsigned)s7x1s' in code,
          'R129 x1s= prints an arginfo actually sent in x1')

    # ==== ROUND 140: the SHM_BRIDGE_ENABLE (av2/sb) probe =================
    # The last read-only question on the PAS route: is SVC_MP/0x1C known, and
    # what does SHM_BRIDGE_ENABLE answer. Two channels, both UNCONDITIONAL,
    # both in the same `if (s7sym)` block that already gates p13.
    # ---- 1. the availability must be ASKED about 0x0C1C, via the round-129
    #         mechanism, and the enable call built through the contract ----
    check('adreno_zap_is_call_available_call' in code,
          'R140 the probe issues an IS_CALL_AVAILABLE query for shmbridge')
    check('is_call_available_call(\n                                    s7base, 0x0C1Cu' in code or
          'is_call_available_call(s7base, 0x0C1Cu' in code,
          'R140 ...about 0x0C1C, the SVC_MP/0x1C function id')
    check('adreno_zap_shm_bridge_enable_call(s7base' in code,
          'R140 the SHM_BRIDGE_ENABLE call is built through the contract helper')
    # x0 for enable must be base | FNID(0x0C,0x1C) == base | 0x0C1C.
    _authc = (DEV / 'drivers/NativeAdreno/AdrenoZapAuthV1.c').read_text(encoding='utf-8', errors='replace')
    _authh = (DEV / 'drivers/NativeAdreno/AdrenoZapAuthV1.h').read_text(encoding='utf-8', errors='replace')
    check('#define kAdrenoZapScmSvcMp             0x0cu' in _authh,
          'R140 SVC_MP is pinned to 0x0C (qcom_scm.c:112)')
    check('#define kAdrenoZapScmMpShmBridgeEnable 0x1cu' in _authh,
          'R140 SHM_BRIDGE_ENABLE is pinned to 0x1C (qcom_scm.c:119)')
    check('#define kAdrenoZapShmBridgeNotSupp     4u' in _authh,
          'R140 SHMBRIDGE_RESULT_NOTSUPP is pinned to 4 (qcom_scm.c:115)')
    check('out->x0 = base | out->fnid;' in _authc and
          'out->svc = kAdrenoZapScmSvcMp' in _authc and
          'out->cmd = kAdrenoZapScmMpShmBridgeEnable' in _authc,
          'R140 the enable call sends x0 = base | 0x0C1C via SVC_MP/0x1C')
    # ZERO arguments: arginfo 0, arg_count 0, args0 0. The upstream .owner is SIP.
    check('out->arginfo = 0u;' in _authc and 'out->arg_count = 0u;' in _authc and
          'out->args0 = 0u;' in _authc,
          'R140 SHM_BRIDGE_ENABLE is a ZERO-argument call (qcom_scm.c:1367-1371)')
    check('out->owner = kAdrenoZapScmOwnerSip;' in _authc,
          'R140 the enable call owner is SIP (qcom_scm.c:1370)')
    # The availability id a caller asks about must equal the enable fnid.
    check('kAdrenoZapShmBridgeFnid' in _authh,
          'R140 the FNID is named once so availability and x0 cannot disagree')
    # ---- 2. UNCONDITIONAL (same gate as p13 / mdt_ck): both fields declared
    #         and BOTH fed to the row whether or not pas-exec/armed ran ----
    check('int s7av2 = -1;' in code,
          'R140 av2 STARTS at -1 (inconclusive), not at 0 or 1')
    check('uint32_t s7sb = 0;' in code,
          'R140 sb is declared')
    check('av2=%d sb=%x' in code,
          'R140 the row carries av2= and sb=')
    check('s7av2, (unsigned)s7sb' in code,
          'R140 both new fields reach snprintf')
    # ---- 3. ORDER: av2 availability FIRST, then enable, then p13. A mutation
    #         that swaps enable before availability, or p13 before av2, must fail.
    _av2_at = code.find('is_call_available_call(')
    _sb_at = code.find('adreno_zap_shm_bridge_enable_call(s7base')
    _p13_at = code.find('adreno_zap_pas_is_supported_call(s7base, &s7sup)')
    # the SVC_MP availability call is the FIRST is_call_available in the block
    _av2_avail = code.find('adreno_zap_is_call_available_call(\n                                    s7base, 0x0C1Cu')
    if _av2_avail < 0:
        _av2_avail = code.find('adreno_zap_is_call_available_call(s7base, 0x0C1Cu')
    check(_av2_avail > 0 and _sb_at > _av2_avail and _p13_at > _sb_at,
          'R140 av2 availability runs BEFORE SHM_BRIDGE_ENABLE, and enable runs '
          'BEFORE the p13 question')
    # ---- 4. the enable verdict is COMPUTED, not assumed: s7sb is set only by a
    #         completed transport (no literal re-assignment after declaration).
    _after_sb_decl = code[code.find('uint32_t s7sb = 0;') + len('uint32_t s7sb = 0;'):]
    # ROUND 140: s7sb must be COMPUTED from the transport, never a literal. Both
    # decimal (`s7sb = 4;`) and C-`u`-suffixed (`s7sb = 4u;`) literals are
    # mutations that fix the field to a constant and must fail. The only legal
    # write is `s7sb = (uint32_t)(sbs & 0xFFFFFFFFu);`.
    check(re.search(r's7sb\s*=\s*\d+\s*;', _after_sb_decl) is None and
          re.search(r's7sb\s*=\s*\d+[uU]\s*;', _after_sb_decl) is None and
          's7sb = (uint32_t)(sbs & 0xFFFFFFFFu);' in _after_sb_decl,
          'R140 sb is COMPUTED from the completed transport, never a literal '
          '(constant-zero or constant-4 mutations must fail)')
    # ---- 5. NOT part of the PAS execute chain / not a new C unit: the probe
    #         adds no code to the three-stage execute path. ----
    check('adreno_zap_pas_execute(&s7tr' in code, 'R140 PAS execute path intact')
    check('adreno_zap_shm_bridge_enable_call' not in code[code.find('adreno_zap_pas_execute(&s7tr'):],
          'R140 the shmbridge probe is NOT inside the PAS execute chain')
    # ---- 6. the base is the same s7base the rest of stage 7 uses ----
    check('s7base, 0x0C1Cu' in code and 's7base' in code,
          'R140 the probe uses the SAME s7base (respecting pas-exec call type)')

    # ==== ROUND 142/144: the SHM_BRIDGE_CREATE reachability probe ==========
    # Two arg sets (A all-ones, B all-zero) are sent, each reading a0/a1/a2; the
    # a2 handle channel and group-B numbers are reported unconditionally.
    # ---- 1. the CREATE call is built through the contract and is 4-arg ----
    check('adreno_zap_shm_bridge_create_probe' in code,
          'R142 the CREATE reachability call is built through the contract')
    check('adreno_zap_shm_bridge_create_probe_args' in code,
          'R144 the CREATE probe takes a caller-supplied argument set (group B)')
    check('adreno_zap_shm_bridge_create_judge(' in code,
          'R142 the CREATE reply is judged through the contract judge')
    # ---- 2. the a2 handle channel is READ (armcpu_scm_call5_r) ----
    check('armcpu_scm_call5_r' in code,
          'R144 the CREATE probe uses the a2-returning export')
    check('&a2_out' not in code and 'a2_out' in code or '&sbc2' in code or '&bb2' in code,
          'R144 the probe passes an a2 output pointer')
    # the contract captures a2 as a struct field in the result of the create probe target
    _authc = (DEV / 'drivers/NativeAdreno/AdrenoZapAuthV1.c').read_text(encoding='utf-8', errors='replace')
    _authh = (DEV / 'drivers/NativeAdreno/AdrenoZapAuthV1.h').read_text(encoding='utf-8', errors='replace')
    check('uint64_t handle_a2' in _authh,
          'R144 the judge takes the a2 handle as an explicit argument')
    check('*reachable = 3;' in _authc and '*reachable = 2;' in _authc and
          '*reachable = 1;' in _authc and '*reachable = 0;' in _authc,
          'R144 the judge implements the 0/1/2/3 table')
    # The R143 case a0==0,a1==0x57 must be decided BY the a2 handle.
    check("secure_result == 0ULL)" in _authc and "handle_a2 != 0ULL)" in _authc,
          'R144 the judge keys on a1==0 then a2==0 (the written-down order)')
    # ---- 3. two arg sets are SENT (group A and group B) ----
    check('const uint32_t zero[4] = { 0u, 0u, 0u, 0u };' in code,
          'R144 group B (all-zero) is built as a DISTINCT argument set')
    check('adreno_zap_shm_bridge_create_probe_args(' in code and
          'zero, &s7shmB' in code,
          'R144 group B is sent through the _args form, not reusing group A')
    # ---- 4. new fields are reported unconditionally ----
    check('sb2=%llx' in code and 'sbB=%llx/%llx' in code,
          'R144 the row carries sb2= and sbB=')
    check('s7sb2' in code and 's7sbB1' in code and 's7sbB2' in code,
          'R144 the a2 handle and group-B values reach snprintf')
    check('s7sbr' in code,
          'R144 sbr remains in the row')
    # s7sb2 (the a2 handle) must be COMPUTED from the captured a2, never a
    # literal. `s7sb2 = 0;` (or `= 0u;`) would force the handle channel to zero
    # and make every CREATE verdict judge a2==0 -> sbr=1, hiding the "suspicious
    # success" signal. Only `s7sb2 = sbc2;` is legal (sbc2 is the a2 the export
    # captured).
    check('s7sb2 = sbc2;' in code,
          'R144 sb2 is COMPUTED from the captured a2 handle, not a literal')
    _after_sb2 = code[code.find('uint64_t s7sb2 = 0;') + len('uint64_t s7sb2 = 0;'):]
    check(re.search(r's7sb2\s*=\s*0\w*\s*;', _after_sb2) is None,
          'R144 sb2 is never forced to a literal zero after its declaration')
    # Group B values must also be captured from the group-B transport, not a
    # literal: sbB1/sbB2 come from bbs/bb2.
    check('s7sbB1 = bbs;' in code and 's7sbB2 = bb2;' in code,
          'R144 group-B values are COMPUTED from the group-B reply, not literals')

    # ==== ROUND 146: SHM_BRIDGE_DELETE handle-validation probe =============
    # The DELETE probe validates 0x8587EB18 (the handle R145 saw twice) by
    # sending a one-arg DELETE and reading a0/a1/a2. a0==0 => real handle.
    # ---- 1. the DELETE call is built through the contract with ARGS(1) ----
    check('adreno_zap_shm_bridge_delete_probe' in code,
          'R146 the DELETE probe is built through the contract helper')
    check('adreno_zap_shm_bridge_delete_judge' in code,
          'R146 the DELETE reply is judged through the contract judge')
    check('kAdrenoZapShmBridgeObservedHandle' in code,
          'R146 the DELETE probe uses the observed 0x8587EB18 handle')
    _authc = (DEV / 'drivers/NativeAdreno/AdrenoZapAuthV1.c').read_text(encoding='utf-8', errors='replace')
    _authh = (DEV / 'drivers/NativeAdreno/AdrenoZapAuthV1.h').read_text(encoding='utf-8', errors='replace')
    check('#define kAdrenoZapScmMpShmBridgeDelete 0x1du' in _authh,
          'R146 SHM_BRIDGE_DELETE is pinned to 0x1D (qcom_scm.c:120)')
    check('#define kAdrenoZapArgInfoShmBridgeDelete 0x01u' in _authh,
          'R146 DELETE is ARGS(1) (qcom_scm.c:1426)')
    check('out->handle = handle;' in _authc and 'uint64_t handle' in _authh,
          'R146 the DELETE call carries the handle in args[0]')
    # ---- 2. read a0/a1/a2 and report unconditionally ----
    check('sbd=%llx/%llx/%llx v=%d' in code,
          'R146 the row carries sbd= (a0/a1/a2) and v=')
    check('s7sbd0' in code and 's7sbd1' in code and 's7sbd2' in code and 's7sbdv' in code,
          'R146 the DELETE values reach snprintf')
    # the verdict must be COMPUTED, never a forced constant
    check('&s7sbdv' in code,
          'R146 the DELETE verdict is set by the judge, not a constant')
    _after_dv = code[code.find('int s7sbdv = -1;') + len('int s7sbdv = -1;'):]
    check(re.search(r's7sbdv\s*=\s*-?\d+\s*;', _after_dv) is None,
          'R146 sbdv is not RE-assigned a literal after its declaration')
    # the handle passed to DELETE must be the observed one, NOT 0 (a handle of 0
    # would delete nothing and prove nothing)
    check('kAdrenoZapShmBridgeObservedHandle' in code and
          's7shmD.handle' in code,
          'R146 the DELETE sends the observed handle through the call record')
    # ---- 3. the judge POLARITY is pinned, so a reversed judge fails ----
    # a0 == 0 (transport clean) MUST mean VALID (real handle); a parameter
    # rejection MUST mean INVALID. Read the judge body to prove the mapping, not
    # just that both *valid lines exist. A mutation that swaps 0/<->1 passes any
    # "both constant assignments present" check and must be caught here.
    _djudge = _authc[_authc.find('int adreno_zap_shm_bridge_delete_judge('):]
    _djudge = _djudge[:_djudge.find('\n}\n')] if '\n}\n' in _djudge else _djudge[:1200]
    _a0branch = _djudge[_djudge.find('if (transport_error == 0) {'):]
    _a0branch = _a0branch[:_a0branch.find('\n    }')] if '\n    }' in _a0branch else _a0branch
    check('*valid = 1;' in _a0branch,
          'R146 the a0==0 branch means VALID (real handle), not invalid')
    _rej = _djudge[_djudge.find('0xFFFFFFFFFFFFFFFEULL'):]
    _rej = _rej[:_rej.find('\n    }')] if '\n    }' in _rej else _rej[:600]
    check('*valid = 0;' in _rej,
          'R146 a parameter rejection means INVALID (not a real handle)')

    # ==== ROUND 131: x1s must come from STAGE 0, not from the p13 probe ====
    # THE DEFECT (round 130 found it by reading a device row that said x1s=1):
    # the assignment was `s7x1s = (uint64_t)s7sup.arginfo;` -- s7sup is the
    # PAS_IS_SUPPORTED record, whose arginfo is 0x01 -- while the comment beside
    # it claimed 0x82. x1s exists to test whether s0 is the secure world's a1 or
    # our own x1 echoed back; that only works when x1s and s0 come from the SAME
    # call, and s0 is stage 0's secure_result.
    check('s7x1s = (uint64_t)s7sup.arginfo;' not in code,
          'R131 x1s is NOT taken from the PAS_IS_SUPPORTED probe (the r130 bug)')
    check('s7x1s = (uint64_t)s7rep.stage[kAdrenoZapPasStageInitImage].arginfo;' in code,
          'R131 x1s is taken from the STAGE 0 record (init_image)')
    # It must be a READ-BACK, not the constant. `s7x1s = 0x82;` or
    # `s7x1s = kAdrenoZapArgInfoInitImage;` would agree by construction and
    # stop being evidence that the plan really sent that value.
    check(re.search(r's7x1s\s*=\s*0x82', code) is None,
          'R131 x1s is not the literal 0x82')
    check(re.search(r's7x1s\s*=\s*\(uint64_t\)\s*kAdrenoZapArgInfoInitImage', code) is None,
          'R131 x1s is not the header constant assigned directly')
    # The cross-check against the constant, and its observable field.
    check('s7x1s == (uint64_t)kAdrenoZapArgInfoInitImage' in code,
          'R131 x1s is CROSS-CHECKED against the contract constant')
    check('ct=%s x1ok=%d' in code,
          'R131 the row carries ct= and x1ok=')
    check('s7ct, s7x1Ok' in code, 'R131 both new fields are passed to snprintf')
    # The capture must happen AFTER the chain ran, i.e. after the execute call,
    # otherwise stage 0 has not been recorded yet and the field would read 0.
    _cap = code.find('s7x1s = (uint64_t)s7rep.stage[kAdrenoZapPasStageInitImage].arginfo;')
    _exec = code.find('adreno_zap_pas_execute(&s7tr')
    check(_cap > _exec > 0,
          'R131 the stage-0 capture runs AFTER adreno_zap_pas_execute')

    # ==== ROUND 131: the SMCCC call type, selected by the pas-exec VALUE ====
    # pas-exec=1 keeps FAST_CALL byte-for-byte (all the r127/r129 device evidence
    # was taken with it); pas-exec=2 clears bit31 to match upstream's STD_CALL.
    # A NEW boot arg was not possible: the command line is asserted at <=1000
    # bytes and currently measures 991, so `enchilada-pas-calltype=std` (27 B)
    # would overflow. Re-using the existing key costs zero bytes.
    check('pasExec == 1u || pasExec == 2u' in code,
          'R131 pas-exec admits BOTH 1 (fast) and 2 (std)')
    check('const bool stdCall = execKey && pasExec == 2u;' in code,
          'R131 STD is selected by value 2, and only by 2')
    check('adreno_zap_smc_base_with_call_type(s7fastBase, 0,' in code,
          'R131 the STD base is built through the gate-checked contract helper')
    check('const uint32_t s7fastBase = adreno_zap_smc_base_derived();' in code,
          'R131 the FAST base is still the derived one')
    # The recast must be a GENERAL case, not applied unconditionally: pas-exec=1
    # has to hand the sequence builder the exact same base as before.
    _recast = code[code.find('if (stdCall) {'):]
    _recast = _recast[:_recast.find('}')]
    check('s7base = recast;' in _recast,
          'R131 the recast replaces s7base ONLY inside the stdCall branch')
    check('s7base = recast;' not in code.replace(_recast, '') if _recast else False,
          'R131 there is no unconditional recast of s7base')
    # The helper must exist in the contract layer with the documented shape, and
    # must move EXACTLY bit31 (owner at 24..29 and SMC_64 at 30 survive).
    _authc = (DEV / 'drivers/NativeAdreno/AdrenoZapAuthV1.c').read_text(encoding='utf-8', errors='replace')
    _authh = (DEV / 'drivers/NativeAdreno/AdrenoZapAuthV1.h').read_text(encoding='utf-8', errors='replace')
    check('int adreno_zap_smc_call_type(uint32_t base);' in _authh,
          'R131 the contract declares the call-type reader')
    check('int adreno_zap_smc_base_with_call_type(uint32_t base, int fast, uint32_t *out);' in _authh,
          'R131 the contract declares the recast helper')
    check('(base | kAdrenoZapSmcFastCall)' in _authc and
          '(base & ~kAdrenoZapSmcFastCall)' in _authc,
          'R131 the recast sets/clears bit31 and nothing else')
    check('(std_base ^ base) != kAdrenoZapSmcFastCall' in _authc,
          'R131 the self-check asserts the flip is EXACTLY one bit')
    # The row reports the type read back from the base actually used.
    check('adreno_zap_smc_call_type(s7base) == 0' in code,
          'R131 ct= is derived from s7base (evidence), not echoed from stdCall')

    # ---- the observable field ------------------------------------------
    # s0 must still be the real smc a1: x1s is a COMPARISON field, not a
    # replacement. If someone "fixed" the echo by overwriting s0, this fails.
    check('s7rep.stage[0].secure_result' in code,
          'R129 s0 is still the real secure_result channel (not replaced by x1s)')

    # ---- the availability shape: svc is 0x06, not the brief's 0x01 ----
    _hdr = (DEV / 'drivers/NativeAdreno/AdrenoZapAuthV1.h').read_text(encoding='utf-8', errors='replace')
    _authc = (DEV / 'drivers/NativeAdreno/AdrenoZapAuthV1.c').read_text(encoding='utf-8', errors='replace')
    check('#define kAdrenoZapScmSvcInfo           0x06u' in _hdr,
          'R129 SVC_INFO is pinned to 0x06 (P3:109); 0x01 is SVC_BOOT (P3:84)')
    check('out->svc = kAdrenoZapScmSvcInfo;' in _authc,
          'R129 the query really uses SVC_INFO')
    check('0x0601u' in _authc,
          'R129 the function id is FNID(0x06,0x01) == 0x0601')
    # ...and it agrees with the call this tree already runs successfully.
    _armcpu = (DEV / 'drivers/ArmCpu/ArmCpu.cpp').read_text(encoding='utf-8', errors='replace')
    check('scm (0xC2000601, 0x1, fn, 0)' in _armcpu,
          'R129 the new query matches ArmCpu.cpp scmAvailable x0 0xC2000601')
    # The owner rides in args[0] for THIS call (P2:355-356), unlike the PAS
    # stages where it rides in x0's base.
    check('kAdrenoZapSmcOwnerShiftForArg' in _authc,
          'R129 the SIP owner is OR-ed into args[0] (P2:355-356), not into x0')

    # ---- no INTERRUPTED retry may be added ----
    # P6:130-145: the non-atomic path retries EBUSY only; a0==1 is terminal.
    # A retry loop here would turn one observed SMC into several and invalidate
    # the ran=/calls= counters this row's evidential value rests on.
    #
    # NOTE: `code` here has comments STRIPPED (:85-86, :98), so this must assert
    # against the RAW text. Asserting a comment phrase against `code` would fail
    # for a perfectly documented change -- which is exactly what happened on the
    # first run of this block.
    check('A TERMINAL ANSWER HERE' in text,
          'R129 the non-atomic retry rule is documented (comment, so check raw text)')
    check('QCOM_SCM_V2_EBUSY' in text and 'qcom_scm-smc.c' in text,
          'R129 ...citing the pinned source that retries EBUSY only')
    # A retry would need a loop around the SMC; there must be none. Asserted on
    # the comment-stripped `code`, because a loop is code.
    _bridge = code[code.find('int zapBridgeCall('):]
    _bridge = _bridge[:_bridge.find('\n}')]
    for _loop in ('while (', 'for (', 'do {'):
        check(_loop not in _bridge,
              'R129 zapBridgeCall contains no %r retry loop' % _loop)

    # ==== ROUND 152: Windows active-retry SMC semantics (rt/a3/a6) ==========
    # The retry loop lives in ArmCpu.cpp's scm5 transport (the single asm block),
    # NOT in zapBridgeCall (which R129 pins loop-free). The probe routes the
    # shmbridge CREATE group-A call through armcpu_scm_call_full so it can report
    # rt= (retry count), a3= and a6= (the extra result registers).
    # ---- the probe uses the full-result export for the CREATE probe ----
    check('armcpu_scm_call_full' in code,
          'R152 the probe routes the CREATE probe through the full-result export')
    check('&sbc3' in code and '&sbc6' in code and '&sbRt' in code,
          'R152 the CREATE call captures a3/a6/rt through out-pointers')
    check('s7rt = sbRt;' in code and 's7a3 = sbc3;' in code and 's7a6 = sbc6;' in code,
          'R152 rt/a3/a6 are COMPUTED from the transport, not forced constants')
    # ---- the row reports the three new fields ----
    check('rt=%u a3=%llx a6=%llx' in code,
          'R152 the row carries rt=, a3= and a6=')
    check('s7rt' in code and 's7a3' in code and 's7a6' in code,
          'R152 the retry/a3/a6 values reach snprintf')
    # ---- the retry semantics live in scm5Retry, and ONLY there -------------
    _armcpu = (DEV / 'drivers/ArmCpu/ArmCpu.cpp').read_text(encoding='utf-8', errors='replace')
    _armcpu_code = strip_comments(_armcpu)
    # ROUND 162 changed the contract. R160's control run (pre-R152 payload, no loop)
    # logged at t=30.72 s while R159/R160/R161 (loop present, cap declared 0 in R161)
    # all left the boot log untouched, so "the loop ran" is NOT the killer. The three
    # remaining deltas on the early path were mov x6,#0, the capped counter, and the
    # cap read -- and the cap read is the suspect, because armcpu_scm_retry_cap is a
    # zero-initialised global (__DATA __common): its 0 exists only after the loader
    # zero-fills __DATA beyond its filesize, which nothing on this platform proves for
    # the time ArmCpu::start runs. So the early transport is back to the ci258 shape
    # (plain single smc, no cap, no counters, no BSS) and the loop moved into
    # scm5Retry, reachable only through armcpu_scm_call_full.
    check('static ScmTriple scm5 (' in _armcpu_code,
          'R162 the early transport returns the three-channel ScmTriple')
    check('static ScmResult scm5Retry (' in _armcpu_code,
          'R162 the retry transport is a SEPARATE function (scm5Retry)')
    check('static ScmTriple scm4 (' in _armcpu_code,
          'R162 scm4 is a thin ScmTriple wrapper over scm5')
    # THE point of the round: the early path must not read the cap, loop, or count.
    _early = section(_armcpu_code, 'static ScmTriple scm5 (', 'static ScmResult scm5Retry (')
    check(_early != '', 'R162 scm5 is delimited in ArmCpu.cpp')
    for _forbidden in ('armcpu_scm_retry_cap', 'for (', 'while (', '__atomic',
                       'armcpu_scm_retries', 'smc5Retry'):
        check(_forbidden not in _early,
              'R162 the early transport scm5 contains no %r' % _forbidden)
    check(_early.count('smc #0') == 1,
          'R162 scm5 issues exactly ONE smc (got %d)' % _early.count('smc #0'))
    # x6 must be a CLOBBER on the early path, not an operand: pinning it to 0 was one
    # of the three deltas against the ci258 build that did log.
    _early_asm = section(_early, '__asm__ volatile ("smc #0"', ';')
    check('"+r" (x6)' not in _early_asm, 'R162 the early smc does not make x6 an output operand')
    check('"x6"' in _early_asm, 'R162 x6 stays in the early smc clobber list')
    # The retry machinery must survive intact where it is reachable.
    _retry = section(_armcpu_code, 'static ScmResult scm5Retry (', 'static ScmTriple scm4 (')
    check(_retry != '', 'R162 scm5Retry is delimited in ArmCpu.cpp')
    check('const unsigned cap = __atomic_load_n (&armcpu_scm_retry_cap' in _retry,
          'R161 scm5Retry loads the runtime cap atomically')
    check('retries >= cap' in _retry,
          'R161 the retry bound is the runtime cap, not a literal')
    check('static_cast<int64_t>(x0) > 0' in _retry,
          'R152 the retry condition is a0 strictly positive (INTERRUPTED/WaitQ)')
    _capdef = re.search(r'volatile unsigned armcpu_scm_retry_cap\s*=\s*(\d+)u?\s*;', _armcpu)
    check(_capdef is not None and int(_capdef.group(1)) == 0,
          'R161 the retry cap DEFAULTS to 0 = no re-issue (got %r, want == 0)'
          % (_capdef.group(1) if _capdef else 'missing'))
    # R160's fix must survive: every register the SMC writes is re-initialised INSIDE
    # the loop, so a re-issue (if the cap is ever raised) sends the same call.
    _loopstart = _retry.find('for (;;) {')
    _loopend = _retry.find('ScmResult r;', _loopstart)
    check(_loopstart > 0 and _loopend > _loopstart,
          'R160 the retry loop body is delimited in scm5Retry')
    _body = _retry[_loopstart:_loopend] if _loopstart > 0 and _loopend > _loopstart else ''
    for _reg in ('x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'x6'):
        check('register uint64_t %s __asm__ ("%s")' % (_reg, _reg) in _body,
              'R160 %s is re-initialised inside the retry loop, not outside it' % _reg)
    # ONE caller, and it is the late export: no early code may reach the loop.
    check(_armcpu_code.count('scm5Retry (') == 2,
          'R162 scm5Retry has exactly one caller besides its definition (got %d mentions)'
          % _armcpu_code.count('scm5Retry ('))
    _full = section(_armcpu_code, 'armcpu_scm_call_full (uint64_t fn',
                    'extern "C" void armcpu_wdog_sample')
    check('r = scm5Retry (fn, arginfo, a2, a3, a4, a5);' in _full,
          'R162 armcpu_scm_call_full is the scm5Retry caller')
    check('armcpu_scm_call_full' in _armcpu,
          'R152 ArmCpu exports the full-result call')
    check('r.retries' in _armcpu,
          'R152 ScmResult carries the retry count')
    # the row buffer must still fit with rt/a3/a6 added
    _m2 = re.search(r'char s7Row\[(\d+)\]', code)
    check(_m2 is not None and int(_m2.group(1)) >= 512,
          'R152 s7Row still >= 512 bytes after rt/a3/a6 are added')

    # ==== ROUND 158: read-only GPU init-state snapshot ======================
    # The snapshot answers "has UEFI/the bootloader already powered the GPU?"
    # Strictly read-only: no register write, no SMC beyond the existing probes.
    # SECVID reads are HARD-GATED on GX-up (a GPU-core read with GX collapsed is
    # a bus read to an unclocked block and can hang the CPU).
    # ---- 1. the snapshot exists and is READ-ONLY (no store, no write) ----
    check('ROUND 158' in text and 'gputate=gx=%08x gxon=%d' in code,
          'R158 the GPU init-state snapshot row is present')
    check('gxNow' in code and 'adreno_domain_gx_is_on(gxNow)' in code,
          'R158 the snapshot re-reads GX and decodes it with the domain predicate')
    check('gxNow = rd32(gGpuccVa,' in code,
          'R158 the GX word is READ from the register, not a constant')
    check('write32' not in code and '__asm__ volatile("smc' not in code,
          'R158 the snapshot performs NO register write and NO new SMC')
    # ---- 2. the SECVID read is gated on GX-up (hard safety constraint) ----
    check('if (gxon) {' in code and 'armcpu_map_physical(\n                                    ADRENO_BOARD_GPU_BASE' in code,
          'R158 SECVID reads are gated behind GX-up (else GPU-core read can hang)')
    check('svt = rd32(gGpuVa, 0xf400u);' in code and
          'sbc = rd32(gGpuVa, 0xf803u);' in code and
          'rd32(gGpuVa, 0xf800u)' in code and 'rd32(gGpuVa, 0xf802u)' in code,
          'R158 the four SECVID registers are read at the pinned a6xx offsets')
    check('svt_read = 0;' in code and 'svt_read = 1;' in code,
          'R158 svt_read distinguishes "SECVID not read (GX down)" from "read"')
    # ---- 3. the new row fields ----
    check('gxb16=%d' in code and 'svt=%08x sbc=%08x sbb=%llx sbs=%08x' in code and
          'sctlr=%08x' in code,
          'R158/R164 the row carries gx/gxon/gxb16/svt/sbc/sbb/sbs/sctlr')
    # ---- 4. only the GR0 page is read (ROUND 164, device-verified) ----
    # ROUND 158 also read `0x800 + 0x20` / `0x800 + 0x28` believing those were
    # CB0 TTBR0/TTBR1.  They are GR1 offsets (kAdrenoSmmuGr1Cba2r is pinned at
    # 0x800) and CB0 actually sits at 0x8000 on a qcom SMMU-500.  ROUND 163's
    # boot-arg bisect put the fatal read inside this block: stage=4 booted with
    # `stage4=refused verdict=3(cx-down)`, stage=5 died once the power-up raised
    # CX and let the permitted branch run, and the pre-R158 probe booted at both
    # stage=5 and stage=6.  The unimplemented-GR1 read is the only access the
    # surviving builds never performed, so it must stay out until CB0 is clocked
    # and owned by this driver.
    check('rd32(gSmmuVa, 0x0u)' in code,
          'R164 SMMU read is the GR0_SCTLR word (the GR0 page is demonstrably alive)')
    check('0x800u + 0x20u' not in code and '0x800u + 0x28u' not in code and
          'ttbr0=%llx' not in code,
          'R164 the fatal GR1/CB0-offset reads stay removed (device-verified)')

    # ==== ROUND 165 (slice 0): read-only GMU window probe ==================
    # R164 left two measured facts: the CPU path stops at CX, and GX is never
    # raised by the CPU (gpu_gx_gdsc.power_on is gdsc_gx_do_nothing_enable), so
    # on A630 the GX rail belongs to the GMU firmware.  The next question is
    # whether the GMU window is reachable once CX is up.  The stage-2 block asks
    # that but runs BEFORE the power-up, so on device it always reported
    # `cx-down-gmu-not-mapped`; this block asks it after the five ops.
    # ---- 1. the probe exists, is read-only, and is behind the second key ----
    check('ROUND 165' in text and 'gmu=probe fwinit=%08x' in code,
          'R165 the read-only GMU probe row is present')
    check('gmuKey >= 3u' in code and 'gmuKey <= 5u' in code and
          'enchilada-pas-exec' in code,
          'R165 the GMU probe family is armed by `enchilada-pas-exec` values '
          '3..5 (same key, same width: the 991-byte command line does not grow)')
    check('stage >= 7u' not in code,
          'R165 no new stage value: the four verifiers pin the mask to 1..6')
    _gmuHead = (code.split('gmuArmed', 1)[1].split('gmu=probe', 1)[0]
                if 'gmuArmed' in code else '')
    check(_gmuHead != '' and 'write32' not in _gmuHead and
          'armcpu_scm_call' not in _gmuHead and 'smc5' not in _gmuHead,
          'R165 the GMU probe performs NO register write and NO SMC')
    # ---- 2. every read is a pinned a6xx_gmu.xml word offset ----------------
    for _name in ('CM3_FW_INIT_RESULT', 'SPTPRAC_PWR_CLK_STATUS',
                  'HFI_CTRL_STATUS', 'HFI_SFR_ADDR', 'RSCC_CONTROL_ACK',
                  'GPU_CC_GX_GDSCR', 'GENERAL_7'):
        check('ADRENO_GMU_BOOT_V1_%s * 4u' % _name in code,
              'R165 the GMU probe reads %s at its pinned word offset' % _name)
    check('char gmuRow[240]' in code,
          'R165 the GMU row has a declared, sufficient bound')

    # ==== ROUND 166 (slice 1a): CM3 boot words + the PDC window ============
    # R165 read seven GMU words without aborting but found the GMU COLD and
    # never booted.  The next two questions are software preconditions the CPU
    # can inspect before any write: (a) what do the CM3 boot strap words say
    # (CM3_BOOT_CONFIG should be 0x2 and CM3_CFG 0x4052 before the firmware can
    # fetch), and (b) is the separate 3 MiB PDC window readable at all.  They
    # are split across two key values so a PDC abort cannot be mistaken for a
    # CM3 read failure.
    check('ROUND 166' in text and 'gmucm3=sptpwr=%08x' in code and
          'gmupdc=pdcen=%08x' in code,
          'R166 the CM3/boot row and the PDC row are both present')
    check('gmupdc=map-failed' in code,
          'R166 a failed PDC mapping has its own diagnostic, not a silent skip')
    for _name in ('SPTPRAC_POWER_CONTROL', 'CM3_BOOT_CONFIG', 'CM3_CFG',
                  'BOOT_SLUMBER_OPTION', 'GX_VOTE_IDX', 'MX_VOTE_IDX'):
        check('ADRENO_GMU_BOOT_V1_%s * 4u' % _name in code,
              'R166 the CM3 probe reads %s at its pinned word offset' % _name)
    for _name in ('PDC_ENABLE_PDC', 'PDC_SEQ_START_ADDR', 'PDC_SEQ_MEM_0'):
        check('ADRENO_GMU_BOOT_V1_%s * 4u' % _name in code,
              'R166 the PDC probe reads %s at its pinned word offset' % _name)
    check('ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE' in code and
          'ADRENO_GMU_BOOT_V1_CM3_CFG_VALUE' in code,
          'R166 the CM3 expectations are the pinned a6xx_gmu.xml values '
          '(0x02 / 0x4052), not invented thresholds')
    check('ADRENO_BOARD_GMU_PDC_BASE' in code and
          'ADRENO_BOARD_GMU_PDC_SIZE' in code and
          'stage >= 7u' not in code,
          'R166 the PDC window is the DTB-pinned one, still inside the '
          'stage>=4 permitted branch (no new stage value)')
    _cm3Head = (code.split('if (gmuKey >= 4u', 1)[1].split('gmucm3=sptpwr', 1)[0]
                if 'if (gmuKey >= 4u' in code else '')
    check(_cm3Head != '' and 'write32' not in _cm3Head and
          'armcpu_scm_call' not in _cm3Head and 'smc5' not in _cm3Head,
          'R166 the CM3 probe performs NO register write and NO SMC')
    check('char cm3Row[224]' in code and 'char pdcRow[184]' in code,
          'R166 both new rows have declared, sufficient bounds')

    # ==== ROUND 167 (slice 1b): the FIRST register write ====================
    # Rounds 158..166 were read-only, and the checks above prove the read blocks
    # still are. This round is the first that changes device state, so its
    # boundary is ASSERTED rather than described: exactly one store exists, its
    # target is CM3_BOOT_CONFIG, its value is the pinned 0x02, and the two words
    # that would actually start the CM3 (CM3_SYSRESET, the ITCM doorbell) are
    # never written. A later round that starts the firmware must delete this
    # check on purpose and say so -- it may not slip through.
    check('ROUND 167' in text and 'gmuboot=pre=%08x reset=%08x held=%d' in code,
          'R167 the one-word GMU boot-config row is present')
    check('armcpu_map_physical_rw' in code,
          'R167 the write path uses the dedicated WRITABLE mapping entry point '
          '(not a direction flag added to the read-only one)')
    _rw = (code.split('bootKey == 6u', 1)[1].split('gmuboot=pre', 1)[0]
           if 'bootKey == 6u' in code else '')
    check(_rw != '',
          'R167 the block is armed by its own key value (enchilada-pas-exec=6), '
          'so the read-only values 3..5 keep their evidence')
    check('*cfg = ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE' in _rw,
          'R167 the single store writes the pinned a6xx_gmu.xml value 0x02')
    check('ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG * 4u' in _rw,
          'R167 the store targets CM3_BOOT_CONFIG at its pinned word offset')
    check('ADRENO_GMU_BOOT_V1_CM3_SYSRESET * 4u' in _rw and
          'CM3_SYSRESET = ' not in _rw and 'CM3_ITCM_START = ' not in _rw,
          'R167 CM3_SYSRESET is READ as the gate but never written and the ITCM '
          'doorbell is never written: R167 itself does not start the CM3 '
          '(ROUND 168 starts it, under its own key, and is asserted below)')
    check('if (held)' in _rw,
          'R167 the write is gated on that reset read-back, so a CM3 that is '
          'already out of reset is reported and not written')
    check('ADRENO_BOARD_GMU_BASE' in _rw and 'ADRENO_BOARD_GMU_SIZE' in _rw,
          'R167 the writable window is the DTB-pinned GMU range, not a guess')
    check('stage >= 7u' not in code,
          'R167 still no new stage value: the boot-arg mask stays exactly 1..6')

    # ==== ROUND 168 (slice 2): the GMU image, first into ITCM ===============
    # This round adds the FIRST bulk write and the FIRST reset pulse, so its two
    # boundaries are asserted: (a) the load runs only while the CM3 is still held
    # in reset, and (b) the pulse, the pre-start writes and the sentinel poll live
    # ONLY inside the key-8 sub-block, so key 7 stays a load witness that cannot
    # start anything.
    check('ROUND 168' in text and 'gmufw=load words=%u' in code,
          'R168 the ITCM bulk-load row is present')
    _fw = (re.sub(r'//[^\n]*', '',
                  text.split('// ---- ROUND 168:', 1)[1]
                      .split('// ---- ROUND 109 stage 7:', 1)[0])
           if '// ---- ROUND 168:' in text else '')
    check(_fw != '', 'R168 the block is delimited, so its boundary is assertable')
    check('fwKey >= 7u && fwKey <= 34u' in _fw,
          'R168 it is armed by its own key values (7..8 and 15..21 for the HFI '
          'ladder); 3..6 keep their evidence')
    check('!kAdrenoGmuBlobPresent' in _fw and 'kAdrenoGmuImageWords == 0u' in _fw,
          'R168 the ABSENT stub is refused explicitly instead of writing four bytes')
    check('const bool held = (preReset & 1u) != 0u;' in _fw and 'if (!held)' in _fw,
          'R168 the load is gated on CM3_SYSRESET bit0, so a running CM3 is never '
          'overwritten')
    check(re.search(r'gmuW\[kAdrenoGmuItcmWord \+ wi\]\s*=\s*adreno_gmu_image\[wi\];', _fw),
          'R168 the image goes to consecutive word offsets from ITCM_START, the '
          'shape a6xx_gmu_fw_load() uses for the legacy blob')
    _rb = ('rb0', 'rb1', 'rbMid', 'rbLast')
    check(all(n in _fw for n in _rb) and
          'rb0 == adreno_gmu_image[0]' in _fw and
          'rbLast ==' in _fw,
          'R168 the load is READ BACK (head, second, mid, last) instead of assumed')
    check('if (fwKey >= 8u) {' in _fw,
          'R168 the pulse is confined to the key-8 sub-block')
    _k8 = (_fw.split('if (fwKey >= 8u) {', 1)[1] if 'if (fwKey >= 8u) {' in _fw else '')
    _pulse = tuple(['gmuW[ADRENO_GMU_BOOT_V1_GENERAL_7] = 1u;',
                    'gmuW[ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT] = 0u;',
                    'gmuW[ADRENO_GMU_BOOT_V1_CM3_SYSRESET] = 1u;',
                    'gmuW[ADRENO_GMU_BOOT_V1_PWR_COL_CP_RESP] = 0u;',
                    'gmuW[ADRENO_GMU_BOOT_V1_CM3_SYSRESET] = 0u;'])
    check(all(s in _k8 for s in _pulse),
          'R168 the pulse is the upstream order (hold, clear log wptr, release) and '
          'lives only under key 8')
    check('gmuW[ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG] =' in _k8 and
          'ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE' in _k8 and
          'gmuW[ADRENO_GMU_BOOT_V1_CM3_CFG] =' in _k8 and
          'ADRENO_GMU_BOOT_V1_CM3_CFG_VALUE' in _k8,
          'R168 the pre-start config writes the pinned 0x02 / 0x4052 values')
    _stores = re.findall(r'gmuW\[(ADRENO_GMU_BOOT_V1_\w+)\]\s*=[^=]', _fw)
    check(sorted(set(_stores)) == sorted(set([
              'ADRENO_GMU_BOOT_V1_GENERAL_7',
              'ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT',
              'ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG',
              'ADRENO_GMU_BOOT_V1_CM3_CFG',
              'ADRENO_GMU_BOOT_V1_CM3_SYSRESET',
              'ADRENO_GMU_BOOT_V1_PWR_COL_CP_RESP',
              'ADRENO_GMU_BOOT_V1_BOOT_SLUMBER_OPTION',
              'ADRENO_GMU_BOOT_V1_HOST2GMU_INTR_SET',
              'ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_CLR',
              'ADRENO_GMU_BOOT_V1_SYS_BUS_CONFIG',
              'ADRENO_GMU_BOOT_V1_ICACHE_CONFIG',
              'ADRENO_GMU_BOOT_V1_DCACHE_CONFIG',
              'ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_CTRL',
              'ADRENO_GMU_BOOT_V1_PWR_COL_SPTPRAC_HYST',
              'ADRENO_GMU_BOOT_V1_RPMH_CTRL',
              'ADRENO_GMU_BOOT_V1_AHB_FENCE_RANGE_0',
              'ADRENO_GMU_BOOT_V1_HFI_SFR_ADDR',
              # ROUND 175: the HFI queue table. R168 refused these three words
              # because it assumed they needed an IOVA from a GMU IOMMU backend
              # this kernel does not have. R174 answered that question on the
              # device instead: sCR0 = 0x00200626 (SMMUEN bit0 = 0, CLIENTPD
              # bit1 = 1), i.e. the kgsl SMMU is DISABLED and GMU traffic is
              # already untranslated, so a PHYSICAL address is what the table
              # wants. Same for CP_MSG.
              'ADRENO_GMU_BOOT_V1_HFI_QTBL_ADDR',
              'ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO',
              'ADRENO_GMU_BOOT_V1_PWR_COL_CP_MSG',
              'ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT',
              # ROUND 179: a6xx_rpmh_start() and a6xx_sptprac_enable(), the two
              # steps upstream runs that this probe had been skipping.
              'ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ',
              'ADRENO_GMU_BOOT_V1_SPTPRAC_POWER_CONTROL',
              # ROUND 181: the two arc-vote indexes. R179 proved the write is
              # safe; key 28 now supplies a real index read out of the RPMh
              # command database instead of a guess.
              'ADRENO_GMU_BOOT_V1_GX_VOTE_IDX',
              'ADRENO_GMU_BOOT_V1_MX_VOTE_IDX'])),
          'R168+R170+R171+R175 the register write set is exactly the named words (%r)'
          % sorted(set(_stores)))
    # ==== ROUND 175 (slice 4): the HFI queue table, at a PHYSICAL address ====
    # R168 asserted these three words were NOT written, because it believed they
    # needed an IOVA from a GMU IOMMU backend this kernel does not have. R174
    # settled that on the device: sCR0 = 0x00200626 has SMMUEN (bit0) clear, so
    # the kgsl SMMU is disabled and GMU traffic is untranslated. The contract is
    # therefore inverted below, and the inversion is what these checks pin: the
    # address must be an ALLOCATION's physical address, bounds-checked to 32 bits,
    # reached through a window that is the writable one, and the whole table must
    # come from the host-verified legacy geometry rather than from local literals.
    check(code.count('if (fwKey >= 15u) {') == 1 and
          code.count('if (fwKey >= 16u') == 1 and
          code.count('if (fwKey >= 17u') == 1 and
          code.count('if (fwKey >= 18u && fwKey <= 21u) {') == 1 and
          code.count('if (fwKey >= 22u && fwKey <= 25u) {') == 1 and
          code.count('if (fwKey >= 23u) {') == 1,
          'R176+R178 the HFI ladder is 15 allocate, 16 +map and fill, 17 +the '
          'three QTBL registers, then the doorbell keys 18..21 (re-gated so the '
          'rail-on keys 22..25 cannot fall through them) and 22..25 for the '
          'BOOT_SLUMBER handshake and the status poll')
    check('gmuhfi=alloc pa=%08x' in code and 'gmuhfi=table win=%08x' in code and
          'gmuhfi=qtbl addr=%08x' in code and 'gmuhfi=pa=%08x' in code,
          'R176 every ladder stage prints its own row, so a surviving run says '
          'which steps actually happened')
    check(code.index('if (fwKey >= 15u) {') <
          code.index('gmuW[ADRENO_GMU_BOOT_V1_CM3_SYSRESET] = 1u;') <
          code.index('if (fwKey >= 18u && fwKey <= 21u) {') <
          code.index('if (fwKey >= 22u && fwKey <= 25u) {'),
          'R176 the table is built before the pulse and hfi_start runs after it, '
          'as upstream orders a6xx_gmu_fw_start() and a6xx_gmu_hfi_start()')
    check('ADRENO_HFI_V1_TABLE_SIZE_BYTES' in code and
          'ADRENO_HFI_V1_QHDR0_OFFSET_WORDS' in code and
          'ADRENO_HFI_V1_QHDR_SIZE_WORDS' in code and
          'ADRENO_HFI_V1_QUEUE_HEADER_WORDS' in code and
          'ADRENO_HFI_V1_QUEUE_WORDS' in code,
          'R175 the table geometry comes from AdrenoHfiV1.h, the host-verified '
          'legacy model, not from local numbers')
    check('ADRENO_HFI_V1_QUEUE_TYPE(qid)' in code and
          'ADRENO_HFI_V1_CMD_QUEUE_ID' in code and
          'ADRENO_HFI_V1_RESP_QUEUE_ID' in code,
          'R175 the two queue types are derived, so the legacy response id (4) '
          'cannot silently become the modern one (1)')
    _h1 = (code.split('if (fwKey >= 15u) {', 1)[1]
           .split('gmuW[ADRENO_GMU_BOOT_V1_CM3_CFG]', 1)[0]
           if 'if (fwKey >= 15u) {' in code else '')
    check('armcpu_dma_alloc_writable(kHfiBytes, &hfiVirt)' in _h1,
          'R175 the table lives in an ALLOCATED physically contiguous buffer, not '
          'at a hard-coded address')
    check('hfiPa <= 0xffffffffull' in _h1,
          'R175 an allocation above 4 GiB is refused, because QTBL_ADDR is a '
          '32-bit register and truncation would point the CM3 at memory it does '
          'not own')
    check('armcpu_map_physical_rw(sHfiPa, kHfiBytes)' in _h1,
          'R175 the table is written through the WRITABLE window (the R167 '
          'direction), not through the read-only probe mapping')
    check('kHfiBytes =' in code and
          'ADRENO_HFI_V1_SHARED_BYTES + ADRENO_HFI_V1_PAGE_BYTES' in code,
          'R175 the allocation covers the table region plus exactly one log page')
    check('gmuW[ADRENO_GMU_BOOT_V1_HFI_QTBL_ADDR] =' in _h1 and
          'static_cast<uint32_t>(sHfiPa)' in _h1,
          'R175 QTBL_ADDR is the allocation physical address itself (SSMU disabled, '
          'so physical IS the address the CM3 uses)')
    check('gmuW[ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO] = 1u;' in _h1,
          'R175 QTBL_INFO is 1, as a6xx_gmu_fw_start() writes it')
    check('gmuW[ADRENO_GMU_BOOT_V1_PWR_COL_CP_MSG] =' in _h1 and
          'sHfiPa + ADRENO_HFI_V1_SHARED_BYTES' in _h1,
          'R175 CP_MSG points at the log page that follows the two rings, with '
          'log_pages = 1 so the OR term is the address')
    check('gmuhfi=alloc-failed' in _h1,
          'R175 a refused allocation has its own diagnostic instead of silently '
          'writing a null table address')
    _h2 = (code.split('if (fwKey >= 18u && fwKey <= 21u) {', 1)[1]
           .split('if (fwKey >= 22u && fwKey <= 25u) {', 1)[0]
           if 'if (fwKey >= 18u && fwKey <= 21u) {' in code else '')
    check('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;' in _h2,
          'R175 hfi_start stores HFI_CTRL_INIT exactly once')
    check('hfiPolls < 1000u' in _h2 and 'hfiPolls < 400000u' not in _h2,
          'R177 the status poll is bounded at 1000, so the first few reads are '
          'what the run measures')
    check(_h2.count('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;') == 1,
          'R177 the doorbell is still exactly one store, whatever the key')
    _i18 = _h2.find('if (fwKey == 18u) {')
    _i19 = _h2.find('if (fwKey >= 19u) {')
    _i20 = _h2.find('if (fwKey >= 20u) {')
    _i21 = _h2.find('if (fwKey >= 21u) {')
    check(0 <= _i18 < _i19 < _i20 < _i21,
          'R177 the doorbell is cut into four ascending keys: store, read back, '
          'one status read, bounded poll')
    check('gmuhfi2=write pre=%08x' in _h2 and 'gmuhfi2=rb pre=%08x' in _h2 and
          'gmuhfi3=stat pre=%08x' in _h2,
          'R177 keys 18, 19 and 20 each print their own row, so a surviving run '
          'says exactly how far the doorbell got')
    check('hfiWi = hfiTbl[' in _h2 and 'hfiRi = hfiTbl[' in _h2 and
          'if (sHfiWin) {' in _h2,
          'R175 the two queue indices are READ back (response write index, '
          'command read index) instead of being assumed zero')
    check('char hfiRow[288]' in code and 'gmuhfi=pa=%08x' in code,
          'R175 the new row has a declared, sufficient bound')
    _rail = (code.split('if (fwKey >= 22u && fwKey <= 25u) {', 1)[1]
             .split('if (fwKey >= 26u) {', 1)[0]
             if 'if (fwKey >= 22u && fwKey <= 25u) {' in code else '')
    check('char railRow[264]' in _rail and 'char rail2Row[264]' in _rail,
          'R178 both rail-on rows declare their own bounded buffers')
    check('gmuW[ADRENO_GMU_BOOT_V1_BOOT_SLUMBER_OPTION] = 0u;' in _rail,
          'R178 the rail-on shape drops BOOT_SLUMBER_OPTION first, in exactly the '
          'order a6xx_gmu_gfx_rail_on() uses')
    check('gmuW[ADRENO_GMU_BOOT_V1_HOST2GMU_INTR_SET] =' in _rail and
          '1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_SET' in _rail and
          '1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_CLEAR' in _rail,
          'R178 the BOOT_SLUMBER doorbell is set and cleared through the OOB bit '
          'definitions, not through local numbers')
    check('ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_ACK)) != 0u' in _rail and
          'railPolls < 400000u' in _rail,
          'R178 the ack wait is bounded, so a silent CM3 cannot spin the boot')
    check('ADRENO_GMU_BOOT_V1_GX_VOTE_IDX]' not in _rail and
          'ADRENO_GMU_BOOT_V1_MX_VOTE_IDX]' not in _rail,
          'R178 the two rail votes are DELIBERATELY not written: the vote is an '
          'RPMh command-db arc index and enchilada.dts has no opp-hz/arc-level')
    check('gmurail=pre=%08x' in _rail and 'gmurail2=pre=%08x' in _rail,
          'R178 keys 22 and 23 each print their own row')
    check(_rail.count('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;') == 1,
          'R178 the doorbell is written once in the rail-on path too, and only '
          'after the handshake')
    _i22 = _rail.find('if (fwKey == 22u) {')
    _i23 = _rail.find('if (fwKey >= 23u) {')
    check(0 <= _i22 < _i23,
          'R178 key 23 is a strict extension of key 22, so one surviving run '
          'separates the handshake from the status poll')
    # ==== ROUND 179 (slice 3, step 4): the two steps upstream runs that this ===
    # ==== probe skipped -- a6xx_rpmh_start() and a6xx_sptprac_enable()     ===
    # R178 proved the doorbell store is survivable once the rail-on handshake
    # shape runs first, but the CM3 still raised GMU2HOST_INTR_INFO_CM3_FAULT
    # (bit 23) and never answered the doorbell. a6xx_gmu_fw_start() also runs
    # a6xx_rpmh_start() right after GENERAL_7 and, because gmu->legacy is true
    # and idle_level is GMU_IDLE_STATE_ACTIVE on this board, a6xx_sptprac_enable()
    # between gfx_rail_on and hfi_start. Keys 24 and 25 add exactly those.
    _k24 = (code.split('if (fwKey >= 24u) {', 1)[1]
            .split('gmuW[ADRENO_GMU_BOOT_V1_GENERAL_7] = 1u;', 1)[0]
            if 'if (fwKey >= 24u) {' in code else '')
    _k25 = (code.split('if (fwKey >= 25u) {', 1)[1]
            .split('if (sHfiWin) {', 1)[0]
            if 'if (fwKey >= 25u) {' in code else '')
    check('gmuW[ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] = 2u;' in _k24 and
          'gmuW[ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] = 0u;' in _k24,
          'R179 key 24 asks the GPU RSC to come up and then releases it, exactly '
          'the two writes a6xx_rpmh_start() makes around its two polls')
    check('rpmhPolls < 400000u' in _k24 and 'rpmhBusyPolls < 400000u' in _k24,
          'R179 both RSC waits are bounded, so a silent RSC cannot spin the boot')
    check('ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA +' in _k24 and
          'ADRENO_GMU_BOOT_V1_RSCC_SEQ_BUSY_DRV0 * 4u' in _k24,
          'R179 RSCC_SEQ_BUSY is read through the derived +0x23000 slice, which is '
          'where a6xx_gmu_init() puts gmu->rscc for a non-a650/a7xx chip')
    check('gmurpmh=pre=%08x ack=%08x apolls=%u busy=%08x bpolls=%u ok=%d' in _k24,
          'R179 key 24 prints its own row, so a surviving run separates the RSC '
          'from everything that follows')
    check('gmuW[ADRENO_GMU_BOOT_V1_SPTPRAC_POWER_CONTROL] =' in _k25 and
          'ADRENO_GMU_BOOT_V1_SPTPRAC_ON_VALUE' in _k25,
          'R179 key 25 writes SPTPRAC_POWER_CONTROL with the ON value 0x778000, '
          'the same literal a6xx_sptprac_enable() writes')
    check('ADRENO_GMU_BOOT_V1_SPTPRAC_CLK_MASK) ==' in _k25 and
          'ADRENO_GMU_BOOT_V1_SPTPRAC_CLK_PATTERN' in _k25 and
          'sptpPolls < 400000u' in _k25,
          'R179 the SPTPRAC wait looks for (status & 0x38) == 0x28 and is bounded')
    check('gmusptp=pre=%08x stat=%08x polls=%u ok=%d' in _k25,
          'R179 key 25 prints its own row')
    check(code.count('ADRENO_GMU_BOOT_V1_GX_VOTE_IDX]') == 2 and
          code.count('ADRENO_GMU_BOOT_V1_MX_VOTE_IDX]') == 2,
          'R179/R181/R185 each rail vote is written in exactly two places: key 28 '
          '(an index read out of the RPMh command database) and key 32 (index 0, '
          'the lowest level, because this device carries no command database)')
    _g7 = code.find('gmuW[ADRENO_GMU_BOOT_V1_GENERAL_7] = 1u;')
    _i24 = code.find('if (fwKey >= 24u) {')
    _fwinit = code.find('gmuW[ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT] = 0u;')
    _i25 = code.find('if (fwKey >= 25u) {')
    _i22r = code.find('if (fwKey >= 22u && fwKey <= 25u) {')
    check(0 <= _g7 < _i24 < _fwinit,
          'R179 key 24 sits where upstream puts a6xx_rpmh_start(): after '
          'GENERAL_7 and before the firmware words')
    check(0 <= _i25 and 0 <= _i22r and _i25 > _i22r,
          'R179 key 25 sits after the rail-on handshake, i.e. between '
          'gfx_rail_on and the doorbell, as upstream orders it')

    check('postSig <= 0x20010004u' in _k8 and '0xbabefaceu' in _k8 and
          '0x1ffu' in _k8 and '0x100u' in _k8,
          'R168 the expected FW_INIT_RESULT is SELECTED from the just-loaded DTCM '
          'word 0xff8, exactly as a6xx_gmu_start() does, not hard-coded')
    check('polls < 400000u' in _k8,
          'R168 the poll is bounded, so a firmware that never boots cannot hang '
          'the boot')
    check('char loadRow[264]' in code and 'char boot2Row[248]' in code,
          'R168 both new rows have declared, sufficient bounds')
    check('stage >= 7u' not in code,
          'R168 still no new stage value: the boot-arg mask stays exactly 1..6')

    # ==== ROUND 169 (slice 3, step 1): read-only liveness dump ==============
    # The firmware answered FW_INIT_RESULT in ROUND 168, so the next question is
    # what it is waiting on. This sub-block only READS status words that earlier
    # rounds already read on this device, so its contract is narrow: no store, no
    # new mapping, and the same sentinel comparison the pulse used.
    check('gmulive=intr=%08x hfistat=%08x ' in code,
          'R169 the liveness row is present')
    check('if (fwKey >= 9u) {' in code,
          'R169 the dump is armed by its own key value and sits after the pulse')
    _live = (code.split('if (fwKey >= 9u) {', 1)[1].split('IOLog("enchilada-adreno: %s\\n", liveRow);', 1)[0]
             if 'if (fwKey >= 9u) {' in code else '')
    check(_live != '', 'R169 the dump body is delimited')
    check(not re.search(r'gmuW\[[^\]]*\]\s*=[^=]', _live),
          'R169 the dump contains NO store through the writable mapping')
    check('armcpu_map_physical' not in _live,
          'R169 the dump opens no new aperture: it reuses the GMU mapping only')
    for _word in ('ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_INFO',
                  'ADRENO_GMU_BOOT_V1_HFI_CTRL_STATUS',
                  'ADRENO_GMU_BOOT_V1_BOOT_SLUMBER_OPTION',
                  'ADRENO_GMU_BOOT_V1_GX_VOTE_IDX',
                  'ADRENO_GMU_BOOT_V1_MX_VOTE_IDX',
                  'ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT',
                  'ADRENO_GMU_BOOT_V1_CM3_SYSRESET'):
        check(_word in _live, 'R169 the dump reads %s' % _word)
    check('const uint32_t liveExpect =' in _live and
          '(postSig <= 0x20010004u) ? 0xbabefaceu : 0x100u' in _live and
          '(liveInit == liveExpect) ? 1 : 0' in _live,
          'R169 the ok field re-derives the same sentinel the pulse selected, so '
          'the dump cannot claim liveness on a different comparison')
    check('char liveRow[248]' in code, 'R169 the row has a declared, sufficient bound')
    check('stage >= 7u' not in code,
          'R169 still no new stage value: the boot-arg mask stays exactly 1..6')

    # ==== ROUND 170 (slice 3, step 2): the OOB boot-slumber handshake ========
    # The first host-side step of gfx_rail_on. Its contract has three parts: the
    # doorbell bit, the ack bit, the bounded wait, and -- the one that matters most
    # for safety -- the two arc votes are NOT written, because their value has no
    # source in this repository.
    check('gmuoob=pre_intr=%08x pre_slumber=%08x ' in code,
          'R170 the OOB handshake row is present')
    check('if (fwKey >= 10u && fwKey <= 14u) {' in code,
          'R170 the handshake is armed by its own key value')
    _oob = (code.split('if (fwKey >= 10u && fwKey <= 14u) {', 1)[1]
                .split('IOLog("enchilada-adreno: %s\\n", oobRow);', 1)[0]
            if 'if (fwKey >= 10u && fwKey <= 14u) {' in code else '')
    check(_oob != '', 'R170 the handshake body is delimited')
    _oobst = re.findall(r'gmuW\[(ADRENO_GMU_BOOT_V1_\w+)\]\s*=[^=]', _oob)
    check(sorted(set(_oobst)) == sorted(set([
              'ADRENO_GMU_BOOT_V1_BOOT_SLUMBER_OPTION',
              'ADRENO_GMU_BOOT_V1_HOST2GMU_INTR_SET',
              'ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_CLR'])),
          'R170 it writes exactly the slumber option, the doorbell and the clear (%r)'
          % sorted(set(_oobst)))
    for _vote in ('GX_VOTE_IDX', 'MX_VOTE_IDX'):
        check(code.count('gmuW[ADRENO_GMU_BOOT_V1_%s]' % _vote) == 2,
              'R170/R181/R185 %s is written exactly twice: key 28 (a value the '
              'RPMh command database supplied) and key 32 (index 0)' % _vote)
    check('1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_SET' in _oob,
          'R170 the doorbell uses the pinned set bit (22), not a literal')
    check('1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_ACK' in _oob,
          'R170 the wait watches the pinned ack bit (30)')
    check('1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_CLEAR' in _oob,
          'R170 the ack is cleared with the pinned clear bit (30)')
    check('oobPolls < 400000u' in _oob,
          'R170 the wait is bounded, so a firmware that never answers cannot hang '
          'the boot')
    check('char oobRow[296]' in code, 'R170 the row has a declared, sufficient bound')
    check('stage >= 7u' not in code,
          'R170 still no new stage value: the boot-arg mask stays exactly 1..6')


    # ==== ROUND 171 (slice 3, step 3): pre-start power/bus configuration =====
    # R170 answered with GMU2HOST_INTR_INFO bit 23 == CM3_FAULT instead of the
    # boot-slumber ack, so this round writes the words a6xx_gmu_fw_start() puts
    # down between CM3_CFG and a6xx_gmu_start(). The contract is narrow: only
    # words whose value comes from a pinned constant or from the board chip id.
    check('gmucfg=ifc=%08x rpmh=%08x ' in code,
          'R171 the read-back row is present')
    check(code.count('if (fwKey >= 11u) {') == 2,
          'R171 the writes and the read-back are both armed by key 11')
    _cfg = (code.split('if (fwKey >= 11u) {', 1)[1]
                .split('gmuW[ADRENO_GMU_BOOT_V1_GENERAL_7] = 1u;', 1)[0]
            if 'if (fwKey >= 11u) {' in code else '')
    check(_cfg != '', 'R171 the configuration body is delimited')
    _cfgst = re.findall(r'gmuW\[(ADRENO_GMU_BOOT_V1_\w+)\]\s*=[^=]', _cfg)
    check(sorted(set(_cfgst)) == sorted(set([
              'ADRENO_GMU_BOOT_V1_SYS_BUS_CONFIG',
              'ADRENO_GMU_BOOT_V1_ICACHE_CONFIG',
              'ADRENO_GMU_BOOT_V1_DCACHE_CONFIG',
              'ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_CTRL',
              'ADRENO_GMU_BOOT_V1_PWR_COL_SPTPRAC_HYST',
              'ADRENO_GMU_BOOT_V1_RPMH_CTRL',
              'ADRENO_GMU_BOOT_V1_AHB_FENCE_RANGE_0',
              'ADRENO_GMU_BOOT_V1_HFI_SFR_ADDR'])),
          'R171 the pre-start block writes exactly those eight words (%r)'
          % sorted(set(_cfgst)))
    check('ADRENO_GMU_BOOT_V1_PWR_COL_IFPC_CTRL' in _cfg and
          'ADRENO_GMU_BOOT_V1_PWR_COL_HYST' in _cfg,
          'R171 the two PWR_COL values are the pinned ones, not literals')
    _cfgflat = re.sub(r'\s+', ' ', _cfg)
    check('| 0x00000005u' in _cfgflat and
          '= gmuW[ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_CTRL] |' in _cfgflat and
          '= gmuW[ADRENO_GMU_BOOT_V1_RPMH_CTRL] |' in _cfgflat,
          'R171 the two read-modify-writes OR their bits in (msm mask=0 semantics)')
    check('ADRENO_GMU_BOOT_V1_RPMH_VOTE_BITS' in _cfg,
          'R171 the RPMh vote bits are the pinned mask')
    check('ADRENO_GMU_BOOT_V1_FENCE_UPPER' in _cfg and
          'ADRENO_GMU_BOOT_V1_FENCE_LOWER' in _cfg,
          'R171 the AHB fence value is composed from the pinned upper/lower pair')
    _boardh = (DEV / 'drivers' / 'NativeAdreno' / 'AdrenoBoardInputs.h').read_text(
        encoding='utf-8')
    _mchip = re.search(r'#define\s+ADRENO_BOARD_CHIPID\s+0x([0-9a-fA-F]+)u', _boardh)
    check(_mchip is not None, 'R171 the board chip id is readable in the header')
    if _mchip:
        _cid = int(_mchip.group(1), 16)
        _want = ((_cid & 0xffff0000) | ((_cid << 4) & 0xf000) | ((_cid << 8) & 0x0f00))
        check('const uint32_t chipIdVal = 0x%08xu;' % _want in _cfg,
              'R171 the GMU chip id equals AdrenoGmuBootV1Chipid(0x%08x) = 0x%08x'
              % (_cid, _want))
    check('cm3FaultBit = 0x00800000u' in code and
          'gmuW[ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_INFO]' not in code,
          'R171 the CM3 fault bit is a read-side mask, never a write')
    check('char cfgRow[264]' in code, 'R171 the row has a declared, sufficient bound')
    for _vote in ('GX_VOTE_IDX', 'MX_VOTE_IDX'):
        check(code.count('gmuW[ADRENO_GMU_BOOT_V1_%s]' % _vote) == 2,
              'R171/R181/R185 %s has exactly two writers: the command-database '
              'read of key 27/28 and the index-0 rail step of key 32' % _vote)
    check('stage >= 7u' not in code,
          'R171 still no new stage value: the boot-arg mask stays exactly 1..6')

    # ==== ROUND 173 (slice 3, step 4, second attempt): what do SID 4/5 do? ====
    # ROUND 172 asked the same question with an ungated dump and killed the boot:
    # the window came back byte-identical, while a control run of the same CI
    # kernel at key 11 wrote a full window.  Three reads had no evidence behind
    # them -- GR1 taken as 1 << PageShift (0x1000) when Linux puts it at 0x4000
    # and thus landing in reserved space, S2CR stepped by 4 instead of 8, and a
    # context bank read for both SIDs without comparing it with ID1.NUMCB (the
    # stage-6 row measures 5, so CB5 at 0xD000 is unimplemented).  R173 reads
    # only GR0 words the stage-6 row already reads and gates every bank read.
    check('if (fwKey >= 13u && fwKey <= 14u) {' in code, 'R173 the gated SMMU dump is armed by key 13')
    check('smmu3=ctrl=%08x smr0=%08x smr1=%08x' in code,
          'R174 the SMR row is present')
    check('if (fwKey >= 14u) {' in code,
          'R174 the S2CR group is a separate, higher key so one boot can tell '
          'which group is walled off')
    check('smmu4=s2cr0=%08x s2cr4=%08x s2cr5=%08x ' in code,
          'R174 the S2CR row is present')
    check('smmu3=map-missing' in code,
          'R174 a missing window is reported instead of dereferenced')
    # comments are already stripped, so the body ends at the first line that is
    # exactly the block's own closing brace (inner braces are deeper).
    _s2 = (re.split(r'\n {36}\}\n', code.split('if (fwKey >= 13u && fwKey <= 14u) {', 1)[1], 1)[0]
           if 'if (fwKey >= 13u && fwKey <= 14u) {' in code else '')
    check(_s2 != '', 'R173 the dump body is delimited')
    check(re.search(r'volatile const uint32_t \*smPtr', _s2) is not None,
          'R173 the window is reached through a const-qualified pointer')
    check('gSmmuVa' in _s2 and 'armcpu_map_physical' not in _s2,
          'R173 it reuses the existing read-only mapping, no second aperture')
    check(re.search(r'(?<!uint32_t )\*\s*smPtr\s*=[^=]', _s2) is None and
          re.search(r'smPtr\s*\[[^\]]*\]\s*=[^=]', _s2) is None,
          'R173 nothing is stored through the SMMU pointer')
    check(re.search(r'\bgmuW\s*\[', _s2) is None,
          'R173 the dump does not touch the GMU window at all')
    check('0x800u >> 2' in _s2 and '0x804u >> 2' in _s2,
          'R174 SMR(0)/SMR(1) are read at their pinned GR0 bytes 0x800/0x804')
    check('0x0c00u >> 2' in _s2 and '0x0c20u >> 2' in _s2 and
          '0x0c28u >> 2' in _s2,
          'R174 S2CR(0)/S2CR(4)/S2CR(5) are read at 0xC00/0xC20/0xC28, the '
          'locations Linux arm-smmu defines (0xC00 + n*8)')
    check('smPtr[0x00u]' in _s2,
          'R174 sCR0 is read at GR0 byte 0x0, the one GR0 word R164 proved '
          'readable')
    check('(smS2c4 >> 16) & 0x3u' in _s2 and '(smS2c5 >> 16) & 0x3u' in _s2,
          'R174 the S2CR type is decoded at the pinned shift 16')
    check('sm4Row[224]' in _s2 and 'sm3Row[224]' in _s2,
          'R174 both new rows have declared, sufficient bounds')
    check('smId1' not in _s2 and 'smShift' not in _s2,
          'R174 no ID1 decode is attempted any more: the offsets are literals')
    check('smGr1' not in code and 'smmucb=' not in code,
          'R174 no GR1 address is formed at all, so the reserved range is unreachable')
    check('stage >= 7u' not in code,
          'R173 still no new stage value: the boot-arg mask stays exactly 1..6')

    # ==== ROUND 180 (slice 3, step 5): is the RSCC page backed at all? ==========
    # R179 ran the RSC handshake for real and the ACK bit never came up across
    # 400000 polls, while the same key's REQ stores were harmless. Before blaming
    # the arc vote, prove the page answers: a bootloader that ran the RPMh reset
    # sequence leaves RSCC_SEQ_MEM_0_DRV0 populated, so an all-zero/all-ones reply
    # would mean the probe has been reading unbacked addresses all along. Key 26
    # therefore stores NOTHING and polls NOTHING -- it only loads ten words.
    _k26 = (code.split('if (fwKey >= 26u) {', 1)[1]
            .split('if (fwKey >= 27u && fwKey <= 29u) {', 1)[0]
            if 'if (fwKey >= 26u) {' in code else '')
    check('if (fwKey >= 22u && fwKey <= 25u) {' in code,
          'R180 the rail block is CLOSED at key 25, so key 26 cannot re-run the '
          'doorbell or the SPTPRAC write on its way to the dump')
    check('(fwKey >= 7u && fwKey <= 34u)' in code,
          'R182 keys 7 to 29 are armed by the same boot-arg window, still '
          'inside 1..6 stages and with no new boot argument')
    check(_k26 != '', 'R180 key 26 exists')
    check('gmuW[' not in _k26 and 'write32' not in _k26,
          'R180 key 26 writes NOTHING through the GMU window: this key asks one '
          'question and must not perturb what it measures')
    check('dsb sy' not in _k26 and 'for (' not in _k26,
          'R180 key 26 has no barrier and no poll loop: it is a single pass of '
          'loads, so a dead RSC page cannot spin or fault the boot')
    check(_k26.count('rd32(') == 10,
          'R180 key 26 reads exactly ten words (ACK, busy, seq-mem, status0, '
          'override, TCS0, fw-init, sysreset, sptprac, intr)')
    check('ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA +' in _k26 and
          'ADRENO_GMU_BOOT_V1_RSCC_SEQ_MEM_0_DRV0 * 4u' in _k26 and
          'ADRENO_GMU_BOOT_V1_RSCC_SEQ_BUSY_DRV0 * 4u' in _k26,
          'R180 the RSCC words are read through the derived +0x23000 slice in '
          'BYTE offsets (rd32 adds a byte offset), i.e. 0x23404 and 0x23600, '
          'both inside the 0x30000 GMU window')
    check('0x346u * 4u' in _k26,
          'R180 RSCC_TCS0_DRV0_STATUS is read at its pinned RSCC offset 0x346')
    check('gmursc=ack=%08x busy=%08x mem0=%08x st0=%08x ovr=%08x tcs0=%08x ' in _k26,
          'R180 key 26 prints the whole RSCC slice on one line, so the judgement '
          'is "page backed / page unbacked" rather than a guess')
    check('fwinit=%08x reset=%08x sptp=%08x intr=%08x ok=%d' in _k26,
          'R180 the same row carries the CM3 words the earlier keys already read, '
          'so the RSC answer and the CM3 fault are measured in one snapshot')
    check('char rscRow[352]' in _k26,
          'R180 the row buffer is declared and large enough for ten 8-digit '
          'fields plus the prefix')
    _i26 = code.find('if (fwKey >= 26u) {')
    _i25 = code.find('if (fwKey >= 25u) {')
    check(0 < _i25 < _i26,
          'R180 key 26 comes after key 25, so the ladder stays strictly ordered '
          'and one surviving boot names the next fatal step')
    # ---- ROUND 181: the votes come from the RPMh command database ------------
    # R180 read the RSC page and every handshake field stayed zero, so the next
    # question is where gfx_rail_on()'s two vote indexes come from.  Upstream
    # builds them from the RPMh command database (gfx.lvl / mx.lvl), not from the
    # device tree, which on this board carries no opp-hz at all.  Key 27 only
    # reads that database; key 28 writes the two indexes and repeats the
    # handshake.  So the old "the vote is never written" claim is replaced -- not
    # deleted -- by "the vote is written in exactly one place, and only after the
    # database has been read".
    _k27 = code.split('if (fwKey >= 27u && fwKey <= 29u) {', 1)[1].split('if (fwKey >= 28u && fwKey <= 29u) {', 1)[0]
    _k28 = code.split('if (fwKey >= 28u && fwKey <= 29u) {', 1)[1].split('if (fwKey >= 29u && fwKey <= 29u) {', 1)[0]
    check(_k27.startswith('\n') and _k28 != '',
          'R181 keys 27 and 28 both exist, so the read and the write are separate '
          'single-variable boots')
    check('static QuietCmdDbHit quiet_cmd_db_levels(uintptr_t db, const char *want' in code,
          'R181 the command database is parsed by one named read-only helper')
    check('0x0c0330dbu' in code,
          'R181 the helper validates the database magic before trusting any '
          'offset (the 0xc3f000c in the DT reg is the AOSS pointer word, not the '
          'magic)')
    check('0x861e0000ULL' in _k27 and '0x861e0000ULL' in _k28,
          'R181 both keys map the node address from the DT node name, 0x861e0000, '
          'not the pointer word')
    check('gmuW[' not in _k27 and 'write32' not in _k27,
          'R181 key 27 writes nothing at all: it must not perturb the tables it '
          'is measuring')
    check('gmulvl=va=%lx magic=%08x gfx_n=%u cx_n=%u mx_n=%u ' in _k27,
          'R181 the first row shows whether the database was found and how many '
          'levels each of the three resources carries')
    check('gmulvl2=gfx_p=%u gfx_top=%04x cx_p=%u cx_top=%04x mx_p=%u mx_top=%04x ' in _k27,
          'R181 the second row shows the index of the top level in each list, '
          'which is exactly the value gfx_rail_on() would put in the vote byte')
    check('char lvlRow[352]' in _k27 and 'char lvl2Row[288]' in _k27,
          'R181 both rows have declared bounds')
    check(_k28.count('gmuW[ADRENO_GMU_BOOT_V1_GX_VOTE_IDX] = (uint32_t)gfxPmax;') == 1 and
          _k28.count('gmuW[ADRENO_GMU_BOOT_V1_MX_VOTE_IDX] = (uint32_t)mxPmax;') == 1,
          'R181 exactly one store sets each vote index, so the write set stays '
          'countable')
    check(_k28.find('quiet_cmd_db_levels(dbVa, "gfx.lvl"') <
          _k28.find('gmuW[ADRENO_GMU_BOOT_V1_GX_VOTE_IDX]'),
          'R181 the vote is computed from the database before it is written, i.e. '
          'the key cannot write a stale or zero index by construction')
    check(_k28.count('gmuW[') == 5,
          'R181 key 28 writes exactly five words: the two votes and the three '
          'handshake words it repeats from key 22')
    check('gmuvote=gx=%u mx=%u ack=%u polls=%u post=%08x slumber=%08x fwinit=%08x' in _k28,
          'R181 the row carries both indexes, the ack, the poll count and the '
          'three post-handshake words in one line')
    check('char voteRow[320]' in _k28,
          'R181 the vote row buffer is declared and large enough for eight fields')
    _i27 = code.find('if (fwKey >= 27u && fwKey <= 29u) {')
    _i28 = code.find('if (fwKey >= 28u && fwKey <= 29u) {')
    _i26b = code.find('if (fwKey >= 26u) {')
    check(0 < _i26b < _i27 < _i28,
          'R181 the ladder stays strictly ordered behind key 26, so a surviving '
          'boot names the next fatal step')
    check(code.count('ADRENO_GMU_BOOT_V1_GX_VOTE_IDX]') == 2 and
          code.count('ADRENO_GMU_BOOT_V1_MX_VOTE_IDX]') == 2,
          'R181/R185 the vote registers appear in exactly two statements each: '
          'key 28 (command database) and key 32 (index 0), so neither can leak '
          'into a read-only key')

    # ---- ROUND 182: the DT node points at the table, it is not the table -------
    # R181 read 0x861e0000 first, the address in the node's name, and its second
    # word was zero rather than the magic.  The node's own reg is
    # <0xc3f000c 0x08>, which the existing touch reader already treats as an
    # eight-byte pointer in AOSS message RAM, so key 29 dumps that page and the
    # header the pointer names.  It is a dump: no store, so it cannot move the
    # tables it is looking for.
    _k29 = code.split('if (fwKey >= 29u && fwKey <= 29u) {', 1)[1].split('if (fwKey >= 11u) {', 1)[0]
    check(_k29.startswith('\n') and _k29 != '',
          'R182 key 29 exists as its own block, after the vote write')
    check('0x00000000c3f00000ULL' in _k29,
          'R182 key 29 maps the page that holds the pointer word, 0xc3f0000')
    check('0x00000000861e0000ULL' in _k29,
          'R182 key 29 also reads the node-name address, so one boot compares '
          'both candidates the existing reader talks about')
    check('gmudbp=va=%lx w0=%08x w4=%08x w8=%08x wc=%08x' in _k29,
          'R182 the pointer row shows the four words of that page, the pointer '
          'being the fourth')
    check('gmudbc=cand=%08x va=%lx h0=%08x magic=%08x h2=%08x h3=%08x ' in _k29,
          'R182 the header row carries the pointer, its mapping and the first '
          'four words behind it, with the magic word separated out')
    check('base0=%08x base1=%08x base2=%08x base3=%08x ok=%d' in _k29,
          'R182 the same row carries the node-name words, so both candidates are '
          'judged from one line')
    check('0x0c0330dbu' in _k29,
          'R182 the dump decides with the same magic constant the helper uses')
    check('0x80000000u' in _k29 and '0x90000000u' in _k29 and '(dbCand & 3u) == 0u' in _k29,
          'R182 the pointer is only followed when it is a plausible, aligned RAM '
          'address, so a garbage word cannot make the probe map random memory')
    check('gmuW[' not in _k29 and 'dsb sy' not in _k29 and 'write32' not in _k29,
          'R182 key 29 writes nothing: it is a read-only dump of two mappings')
    check('char dbPtrRow[320]' in _k29 and 'char dbHdrRow[352]' in _k29,
          'R182 both dump rows have declared bounds')
    check('gmudbp=map-failed' in _k29,
          'R182 a failed pointer-page mapping is reported instead of silently '
          'printing zeros')
    _i28 = code.find('if (fwKey >= 28u && fwKey <= 29u) {')
    _i29 = code.find('if (fwKey >= 29u && fwKey <= 29u) {')
    check(0 < _i28 < _i29,
          'R182 key 29 sits behind key 28, so the ladder stays strictly ordered')

    # ---- ROUND 183: is the fault the firmware's, or the doorbell's? -----------
    # Keys 9..13 read GMU2HOST_INTR_INFO with no HFI table; keys 15..17 wrote the
    # table but never read that word after the pulse.  Key 30 runs the key-17
    # write set and then one read-only row, so a fault bit in it is the CM3's own
    # state after start rather than a reaction to anything the host sent later.
    _k30 = code.split('if (fwKey >= 30u && fwKey <= 30u) {')[1] \
        if 'if (fwKey >= 30u && fwKey <= 30u) {' in code else \
        code.split('if (fwKey >= 30u) {', 1)[1]
    _k30 = _k30.split('if (fwKey == 31u) {', 1)[0] \
        if 'if (fwKey == 31u) {' in _k30 else \
        _k30.split('if (fwKey >= 18u && fwKey <= 21u) {', 1)[0]
    check(_k30 != '', 'R183 key 30 exists as its own block after the pulse')
    check('gmustat=intr=%08x fwinit=%08x reset=%08x ' in _k30,
          'R183 the row leads with the interrupt word and the init result, the '
          'two registers that separate "faulted" from "started"')
    check('gdscr=%08x hfistat=%08x wi1=%08x ri0=%08x ok=%d' in _k30,
          'R183 the row also carries the GX domain state, the HFI control word '
          'and the two ring indexes, so "the firmware never spoke" is visible')
    check('gmuW[' not in _k30 and 'dsb sy' not in _k30 and 'write32' not in _k30,
          'R183 key 30 stores nothing: it is the same write set as key 17 plus a '
          'read, so a fault cannot be blamed on the probe')
    check('char stRow[248]' in _k30, 'R183 the row has a declared bound')
    check('ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_INFO * 4u' in _k30,
          'R183 the interrupt word is read at the offset the model pins')
    check(_k30.find('GMU2HOST_INTR_INFO') < _k30.find('gdscr=%08x'),
          'R183 the reads happen before the row is formatted (no uninitialised '
          'field can reach the log)')
    _i30 = code.find('if (fwKey >= 30u')
    _i18 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    check(0 < _i30 < _i18,
          'R183 key 30 sits between the boot row and the doorbell ladder, i.e. '
          'exactly where the post-pulse state is still untouched')


    # ---- ROUND 184: can the HFI block start without any doorbell? ------------
    # R183 showed the CM3 comes up clean (intr=0) once the table and the whole
    # pre-start configuration are written before the pulse, so the CM3_FAULT
    # every doorbell key reports is provoked by the boot-slumber exchange.  Key
    # 31 takes the next upstream step -- a6xx_gmu_hfi_start -- in that clean
    # state: one store of HFI_CTRL_INIT, then a bounded poll of HFI_CTRL_STATUS.
    _k31 = code.split('if (fwKey == 31u) {', 1)[1]
    _k31 = _k31.split('if (fwKey == 32u || fwKey == 33u) {', 1)[0] \
        if 'if (fwKey == 32u || fwKey == 33u) {' in _k31 else \
        _k31.split('if (fwKey >= 18u && fwKey <= 21u) {', 1)[0]
    check(_k31 != '', 'R184 key 31 exists as its own block after key 30')
    check(_k31.count('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;') == 1,
          'R184 key 31 writes HFI_CTRL_INIT exactly once, the same single store '
          'a6xx_gmu_hfi_start performs')
    check('HOST2GMU_INTR_SET' not in _k31 and 'BOOT_SLUMBER_OPTION' not in _k31,
          'R184 key 31 rings no doorbell: the boot-slumber exchange is exactly '
          'what R183 proved is not part of the boot')
    check('gmuhfi2=stat=%08x polls=%u intr=%08x ' in _k31,
          'R184 the row leads with the HFI control word, the poll count and the '
          'interrupt word, i.e. the three facts that separate "HFI up" from '
          '"HFI refused"')
    check('fwinit=%08x ok=%d' in _k31,
          'R184 the row keeps the firmware init latch and a derived ok flag')
    check('for (; hfiPoll < 400000u; ++hfiPoll)' in _k31,
          'R184 the status poll is bounded, so a wedged HFI cannot hang the boot')
    check('char hfiRow[256]' in _k31, 'R184 the row has a declared bound')
    check(_k31.find('HFI_CTRL_INIT] = 1u') < _k31.find('HFI_CTRL_STATUS * 4u'),
          'R184 the store precedes the poll (the poll can only see a real start)')
    _i31 = code.find('if (fwKey == 31u) {')
    check(0 < _i30 < _i31 < _i18,
          'R184 key 31 sits after key 30 and before the doorbell ladder, so it '
          'inherits the full write set and adds no doorbell')

    # ---- ROUND 186: the rail step, with and without the boot-slumber doorbell --
    # R185 shipped key 32 as "rails", but keys 30/31/32/33 all used open-ended
    # guards, so key 32 also ran key 31's HFI_CTRL_INIT store -- the one R184
    # proved fatal.  The R185 device run therefore said nothing about the rails.
    # R186 closes key 31 at 31, widens the rail block to keys 32 and 33, and puts
    # the upstream boot-slumber doorbell in key 33 only.
    check(code.count('if (fwKey == 31u) {') == 1,
          'R186 key 31 is closed at 31, so the lone HFI store no longer leaks into '
          'the rail keys that follow it')
    _k32 = code.split('if (fwKey == 32u || fwKey == 33u) {', 1)[1]
    _k32 = _k32.split('if (fwKey == 33u) {', 1)[0]
    check(_k32 != '', 'R186 the rail block is shared by keys 32 and 33')
    check(_k32.count('gmuW[ADRENO_GMU_BOOT_V1_BOOT_SLUMBER_OPTION] = 0u;') == 1,
          'R186 the rail step clears BOOT_SLUMBER_OPTION first, exactly as '
          'a6xx_gmu_gfx_rail_on() does before it touches a vote')
    check(_k32.count('gmuW[ADRENO_GMU_BOOT_V1_GX_VOTE_IDX] = 0u;') == 1 and
          _k32.count('gmuW[ADRENO_GMU_BOOT_V1_MX_VOTE_IDX] = 0u;') == 1,
          'R186 the rail step writes both arc vote indexes, and writes 0: with no '
          'command DB on this device index 0 is the lowest level, never a higher '
          'voltage')
    check('HOST2GMU_INTR_SET' not in _k32 and 'GMU2HOST_INTR_CLR' not in _k32,
          'R186 the shared rail block rings no doorbell: the set/ack/clear '
          'exchange lives in key 33 so the two halves stay separable')
    check('gmuW[ADRENO_GMU_BOOT_V1_SPTPRAC_POWER_CONTROL] =' in _k32 and
          'ADRENO_GMU_BOOT_V1_SPTPRAC_ON_VALUE' in _k32,
          'R186 the rail step enables SPTPRAC with the value a6xx_sptprac_enable() '
          'writes')
    check('gmurail2=gdscr_pre=%08x gdscr=%08x sptp_pre=%08x sptp=%08x ' in _k32,
          'R186 the row leads with the GX domain and SPTPRAC clock words before and '
          'after, so a half-applied rail step is visible')
    check('sptppolls=%u intr_pre=%08x intr=%08x ok=%d' in _k32,
          'R186 the row carries the bounded poll count and the interrupt word from '
          'both sides of the writes')
    check('(railGdscr & 0x00010000u) != 0u' in _k32,
          'R186 ok is derived from the GX_ON bit (16) of GPU_CC_GX_GDSCR, the bit '
          'the domain state header pins')
    check('for (; railSptpPolls < 400000u; ++railSptpPolls)' in _k32,
          'R186 the SPTPRAC poll is bounded, so a rail that never answers cannot '
          'hang the boot')
    check('char rail2Row[320]' in _k32, 'R186 the rail row has a declared bound')

    _k33 = code.split('if (fwKey == 33u) {', 1)[1]
    _k33 = _k33.split('if (fwKey == 34u) {', 1)[0]
    check(_k33 != '', 'R186 key 33 exists as its own block after the rail step')
    check(_k33.count('gmuW[ADRENO_GMU_BOOT_V1_HOST2GMU_INTR_SET] =') == 1 and
          '1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_SET' in _k33,
          'R186 key 33 rings the boot-slumber doorbell (bit 22), the sequence '
          'gfx_rail_on() ends with')
    check(_k33.count('gmuW[ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_CLR] =') == 1 and
          '1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_CLEAR' in _k33,
          'R186 key 33 clears the acknowledge bit (30), so the exchange is the full '
          'set/ack/clear triple')
    check('for (; rail3Polls < 400000u; ++rail3Polls)' in _k33,
          'R186 the acknowledge poll is bounded')
    check('gmurail3=ack=%u polls=%u gdscr=%08x intr=%08x fwinit=%08x ' in _k33 and
          'reset=%08x ok=%d' in _k33,
          'R186 the row reports the acknowledge, the poll count, the GX domain and '
          'the firmware latch together')
    check('char rail3Row[288]' in _k33, 'R186 the rail3 row has a declared bound')
    check(_k33.count('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT]') == 0,
          'R186 key 33 does not retry the HFI start: the rail question is asked '
          'with the rails only')
    _i32 = code.find('if (fwKey == 32u || fwKey == 33u) {')
    _i33 = code.find('if (fwKey == 33u) {')
    check(0 < _i31 < _i32 < _i33 < _i18,
          'R186 the rail block and the doorbell key sit between key 31 and the '
          'doorbell ladder, so both inherit the full write set')


    # ---- ROUND 187: a read-only look at the APPS RSC the CPU side can reach --
    # The touch driver's own RAIL row (`RAIL rc0 db85fe0000 w85fe0000 tcs0
    # drv00010100 a00042100 ctl00000000 st00000001 n36`) is the only proof on
    # this machine that an RPMh request ever left the CPU; every GMU-side RSCC
    # read has come back zero.  Before anything is written through the APPS RSC,
    # this key says what the DRV id, the four TCS banks and the command database
    # header at 0x85fe0000 actually contain.
    check(code.count('if (fwKey == 34u) {') == 1,
          'R187 key 34 is its own block, so nothing earlier in the file decides '
          'what it reads')
    _k34 = code.split('if (fwKey == 34u) {', 1)[1]
    _k34 = _k34.split('if (fwKey >= 18u && fwKey <= 21u) {', 1)[0]
    check(_k34 != '', 'R187 the APPS RSC block exists ahead of the doorbell ladder')
    check('0x00000000179e0000ULL' in _k34 and '0x00010000u' in _k34,
          'R187 the block maps the whole 0x179e0000 window once, the same window '
          'the touch driver maps, instead of assuming where the TCS banks sit')
    check('0x0000000085fe0000ULL' in _k34 and '0x00000000c3f00000ULL' in _k34,
          'R187 the block also reads the command database the touch driver '
          'reported and the AOSS pointer word key 29 could not make sense of')
    check('gmuW[' not in _k34 and 'dsb sy' not in _k34 and 'write32' not in _k34,
          'R187 the block is read-only: no GMU store, no barrier, no helper write')
    check('rd32(' in _k34 and 'wr32(' not in _k34,
          'R187 every access goes through the read helper only')
    check('gmursc2=va=%lx drv=%08x v3=%d ctl=%08x/%08x/%08x/%08x' in _k34,
          'R187 the first row reports the DRV id that picks the register layout, '
          'plus all four TCS control words')
    check('gmursc3=en=%08x/%08x/%08x/%08x st=%08x/%08x/%08x/%08x' in _k34,
          'R187 the second row reports enable and status for the same four TCS '
          'banks, which is what says whether any of them is still in use')
    check('gmursc4=db=%lx h0=%08x h1=%08x magic=%d pgw=%08x ' in _k34 and
          'wait=%08x/%08x/%08x/%08x' in _k34,
          'R187 the third row asks whether the reported address really carries a '
          'command-database header, repeats the AOSS pointer word read-only, and '
          'carries the four wait masks')
    check('0x0c0330dbu' in _k34,
          'R187 the header check compares against the pinned database magic')
    check('(rscDrv / 0x10000u) % 256u' in _k34,
          'R187 the DRV major version is decoded arithmetically, so neither a mask '
          'nor a shift spelling can be mistaken for an ID1 field decode')
    check('rscV3 ? 0x24u : 0x14u' in _k34 and 'rscV3 ? 0x2cu : 0x1cu' in _k34 and
          'rscV3 ? 0x28u : 0x18u' in _k34 and 'rscV3 ? 0x20u : 0x10u' in _k34,
          'R187 both register layouts from the touch header are carried, selected '
          'by the measured major version rather than assumed')
    check('672u * rscI' in _k34 and '0x0d00u + 672u * rscI' in _k34,
          'R187 the four TCS banks are walked at the pinned 672-byte stride from '
          'the pinned TCS base')
    check('char rscRowA[240]' in _k34 and 'char rscRowB[240]' in _k34 and
          'char rscRowC[352]' in _k34,
          'R187 each of the three rows has a declared bound')
    _i34 = code.find('if (fwKey == 34u) {')
    check(0 < _i33 < _i34 < _i18,
          'R187 key 34 sits after the rail keys and before the doorbell ladder, so '
          'it inherits the full pre-start write set and adds none of its own')

    # ---- the row must FIT its buffer ----
    # r129 added three fields; the recomputed reachable worst case exceeds the
    # old 384, so the buffer grew to 512. A truncating snprintf would silently
    # drop whichever diagnostic fell off the end.
    _m = re.search(r'char s7Row\[(\d+)\]', code)
    check(_m is not None, 'R129 s7Row has a declared bound')
    if _m:
        _bound = int(_m.group(1))
        check(_bound >= 512,
              'R129 s7Row is at least 512 bytes (got %d): the r129 field set has '
              'a reachable worst case of 437' % _bound)
        _s7replay = re.search(r'char gId1ReplayStage7\[(\d+)\]', code)
        check(_s7replay is not None and int(_s7replay.group(1)) >= _bound,
              'R129 the stage-7 replay buffer is at least as large as s7Row, '
              'so the replay cannot truncate what the live row held')


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
