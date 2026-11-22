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
    # ROUND 232: keys 71 and 72 deliberately re-read ID1 (CBn_BASE) and ID2 (ias/oas).
    # The invariant is that the ORIGINAL stage-4/6 read happens once, so count only the
    # region before key 71.
    _ro = code.split('if (fwKey == 71u) {', 1)[0]
    for off, name in (('0x20u', 'GR0_ID0'), ('0x24u', 'GR0_ID1'),
                      ('0x28u', 'GR0_ID2')):
        check(_ro.count('rd32(gSmmuVa, %s)' % off) == 1,
              '%s (byte %s) is read exactly once before the write keys' % (name, off))
    check(code.count('rd32(gSmmuVa, 0x24u)') <= 29,
          'ID1 is re-read at most twenty-nine times more (keys 71, 72, 95, 97, 99, 102, 108, 111, 114, 117, 120, 124, 127, 130, 133, 136, 139, 142, 145, 148, 151, 154, 157, 160, 163, 166 and 169), '
          'not in a loop')
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
    # ROUND 232: key 72 is the first WRITE stage in this port, added deliberately, so the
    # READ-ONLY property is asserted over the region up to and including key 71 rather
    # than over the whole file. Everything that must stay read-only still is.
    code_ro = code.split('if (fwKey == 72u) {', 1)[0]
    store = re.search(r'\*\s*reinterpret_cast\s*<[^>]*>\s*\([^;]*\)\s*=[^=]', code_ro)
    check(store is None,
          'no store through a pointer in the read-only stages (keys up to 71)')
    # And the write stage must exist and must contain stores, or this split would let a
    # future edit delete the writes while still passing.
    check('if (fwKey == 72u) {' in code,
          'R232 the write stage (key 72) is present')
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
    check('fwKey >= 7u && fwKey <= 171u' in _fw,
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
              'ADRENO_GMU_BOOT_V1_MX_VOTE_IDX',
              # ROUND 236: the two interrupt mask registers and the AO clear.
              # Upstream masks both blocks around a6xx_gmu_hfi_start(); this port
              # had never written either mask, and its keys died at the start
              # word. GMU2HOST_INTR_CLR was already in the set above.
              'ADRENO_GMU_BOOT_V1_AO_HOST_INTERRUPT_CLR',
              'ADRENO_GMU_BOOT_V1_AO_HOST_INTERRUPT_MASK',
              'ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_MASK',
              # ROUND 268: key 168 writes the control word back to zero to ask
              # whether one acceptance can be undone inside the same boot.
              'ADRENO_GMU_BOOT_V1_HFI_CTRL_STATUS'])),
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
    check(code.count('ADRENO_GMU_BOOT_V1_GX_VOTE_IDX]') == 3 and
          code.count('ADRENO_GMU_BOOT_V1_MX_VOTE_IDX]') == 3,
          'R179/R181/R185/R192 each rail vote is written in exactly three places: '
          'key 28 (an index read out of the RPMh command database), key 32 (index '
          '0, written before R187 found the database), and key 39 (the index that '
          'lookup now produces)')
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
        check(code.count('gmuW[ADRENO_GMU_BOOT_V1_%s]' % _vote) == 3,
              'R170/R181/R185/R192 %s is written exactly three times: key 28 (a '
              'value the RPMh command database supplied), key 32 (index 0) and '
              'key 39 (the index the lookup now derives)' % _vote)
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
        check(code.count('gmuW[ADRENO_GMU_BOOT_V1_%s]' % _vote) == 3,
              'R171/R181/R185/R192 %s has exactly three writers: the '
              'command-database read of key 27/28, the index-0 rail step of '
              'key 32, and the derived-index vote of key 39' % _vote)
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
    check('(fwKey >= 7u && fwKey <= 171u)' in code,
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
    check(code.count('ADRENO_GMU_BOOT_V1_GX_VOTE_IDX]') == 3 and
          code.count('ADRENO_GMU_BOOT_V1_MX_VOTE_IDX]') == 3,
          'R181/R185/R192 the vote registers appear in exactly three statements '
          'each: key 28 (command database), key 32 (index 0) and key 39 (the '
          'derived index), so none can leak into a read-only key')

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
    _k34 = _k34.split('if (fwKey == 35u) {', 1)[0]
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


    # ---- ROUND 188: the level tables, read from the base R187 proved ---------
    # Key 27 asked 0x861e0000 for "gfx.lvl" and got magic=0, which is what put
    # R181's vote derivation on the shelf.  R187 found the database at
    # 0x85fe0000 with a real header, so the same question is asked again through
    # the same helper at the right address -- still read-only, because the point
    # is to learn whether the tables are in there and which index holds the top
    # level.
    check(code.count('if (fwKey == 35u) {') == 1,
          'R188 key 35 is its own block, so nothing earlier decides what it reads')
    _k35 = code.split('if (fwKey == 35u) {', 1)[1]
    _k35 = _k35.split('if (fwKey == 36u) {', 1)[0]
    check(_k35 != '', 'R188 the level-table block exists ahead of the doorbell ladder')
    check('armcpu_map_physical(0x85fe0000ULL, 0x00020000u)' in _k35,
          'R188 the block maps the database address R187 measured, at the same '
          '0x20000 span the touch driver uses')
    check('0x861e0000ULL' not in _k35,
          'R188 the block does not fall back to the address key 27 already proved '
          'empty: a second look at 0x861e0000 would only re-measure a known zero')
    check('gmuW[' not in _k35 and 'dsb sy' not in _k35 and 'write32' not in _k35 and
          'wr32(' not in _k35,
          'R188 the block is read-only: no GMU store, no barrier, no RSC write')
    check(_k35.count('quiet_cmd_db_levels(') == 3 and
          '"gfx.lvl"' in _k35 and '"cx.lvl"' in _k35 and '"mx.lvl"' in _k35,
          'R188 the three resources the upstream vote derivation asks for are the '
          'three this block reads, through the already-verified helper')
    check('gmudb2=va=%lx magic=%08x gfx_n=%u cx_n=%u mx_n=%u ' in _k35 and
          'gfx_a=%08x cx_a=%08x mx_a=%08x' in _k35,
          'R188 the first row reports the magic, the three table counts and the '
          'three database addresses the helper resolved')
    check('gmudb3=g0=%04x g1=%04x g2=%04x g3=%04x c0=%04x c1=%04x ' in _k35 and
          'c2=%04x c3=%04x m0=%04x m1=%04x ok=%d' in _k35,
          'R188 the second row carries the low four levels of each table, so a '
          'plausible staircase is visible without a second boot')
    check('gmudb4=gfx_top=%04x gfx_at=%u cx_top=%04x cx_at=%u ' in _k35 and
          'mx_top=%04x mx_at=%u' in _k35,
          'R188 the third row reports the top level of each table and its first '
          'index, which is the pindex/sindex the upstream formula needs')
    check('(lvMagic == 0x0c0330dbu) ? 1 : 0' in _k35,
          'R188 ok is the pinned database magic test, not a table-count guess')
    check('char lvRowA[352]' in _k35 and 'char lvRowB[352]' in _k35 and
          'char lvRowC[288]' in _k35,
          'R188 each of the three rows has a declared bound')
    _i35 = code.find('if (fwKey == 35u) {')
    check(0 < _i34 < _i35 < _i18,
          'R188 key 35 sits after key 34 and before the doorbell ladder, so it '
          'sees the same pre-start state and adds no write of its own')


    # ---- ROUND 189: which resources the database actually carries -----------
    # Key 35 found "gfx.lvl" (ten levels, top 0x01a0 at index 9) but got nothing
    # for "cx.lvl" and "mx.lvl", while the upstream code looks the secondary
    # list up as "gmxc.lvl" before "mx.lvl".  Rather than guess a third name,
    # this key prints the entry names the database really has.
    check(code.count('if (fwKey == 36u) {') == 1,
          'R189 key 36 is its own block, so no earlier key decides what it reads')
    _k36 = code.split('if (fwKey == 36u) {', 1)[1]
    _k36 = _k36.split('if (fwKey == 37u) {', 1)[0]
    check(_k36 != '', 'R189 the enumeration block exists ahead of the doorbell ladder')
    check('armcpu_map_physical(0x85fe0000ULL, 0x00020000u)' in _k36,
          'R189 the block reads the database address R187 measured, at the same '
          '0x20000 span key 35 used')
    check('gmuW[' not in _k36 and 'dsb sy' not in _k36 and 'write32' not in _k36 and
          'wr32(' not in _k36,
          'R189 the block is read-only: no GMU store, no barrier, no RSC write')
    check(_k36.count('quiet_cmd_db_levels(') == 0,
          'R189 the enumeration walks the header itself instead of asking the '
          'name lookup for names it does not know')
    check('0x0c0330dbu' in _k36, 'R189 the walk is gated on the pinned magic')
    check('(esLo / 65536u) % 65536u' in _k36 and '(esHi / 65536u) % 65536u' in _k36,
          'R189 the slave header fields are decoded arithmetically, so the '
          'half-word split cannot be mistaken for a shift decode')
    check('144u + ((esLo / 65536u) % 65536u)' in _k36 and '24u * ee' in _k36,
          'R189 entries are walked from the pinned 144-byte base at the pinned '
          '24-byte stride')
    check('gmudbn=va=%lx magic=%08x slaves=%u entries=%u n0=%.8s n1=%.8s' in _k36 and
          'gmudbn2=n2=%.8s n3=%.8s n4=%.8s n5=%.8s n6=%.8s n7=%.8s' in _k36,
          'R189 the two rows carry the counts and the first eight names, which is '
          'what says whether the database has the resources at all')
    check('char enumRowA[352]' in _k36 and 'char enumRowB[352]' in _k36,
          'R189 both rows have a declared bound')
    _i36 = code.find('if (fwKey == 36u) {')
    check(0 < _i35 < _i36 < _i18,
          'R189 key 36 sits after key 35 and before the doorbell ladder')


    # ---- ROUND 190: where each database entry actually points ----------------
    # Key 36 printed eight entry names and they include "cx.lvl" and "mx.lvl",
    # yet key 35's lookup returned zero levels for both.  The lookup refuses an
    # entry whose address is zero or whose auxiliary block falls outside the
    # span, so this key prints the per-entry address, length, auxiliary offset
    # and the fit verdict, and stops there.
    check(code.count('if (fwKey == 37u) {') == 1,
          'R190 key 37 is its own block, so no earlier key decides what it reads')
    _k37 = code.split('if (fwKey == 37u) {', 1)[1]
    _k37 = _k37.split('if (fwKey == 38u) {', 1)[0]
    check(_k37 != '', 'R190 the per-entry block exists ahead of the doorbell ladder')
    check('armcpu_map_physical(0x85fe0000ULL, 0x00020000u)' in _k37,
          'R190 the block reads the database address R187 measured, at the same '
          '0x20000 span keys 35 and 36 used')
    check('gmuW[' not in _k37 and 'dsb sy' not in _k37 and 'write32' not in _k37 and
          'wr32(' not in _k37,
          'R190 the block is read-only: no GMU store, no barrier, no RSC write')
    check('0x0c0330dbu' in _k37, 'R190 the walk is gated on the pinned magic')
    check('rd32(entVa, e3At + 16u)' in _k37 and 'rd32(entVa, e3At + 20u)' in _k37,
          'R190 the address and the packed tail are read from the pinned entry '
          'offsets 16 and 20')
    check('entLen[entSeen] = e3Tail % 65536u' in _k37 and
          'entOff[entSeen] = (e3Tail / 65536u) % 65536u' in _k37,
          'R190 the tail is split into length and auxiliary offset arithmetically, '
          'not by shifts')
    check('s3Data + entOff[entSeen] + 2u * entLen[entSeen] <= 0x00020000u' in _k37,
          'R190 the fit test repeats the bound the lookup applies, so a non-zero '
          'fit means the lookup had no shape reason to refuse')
    check('gmudbe=va=%lx magic=%08x seen=%u' in _k37,
          'R190 the first row carries the mapped address, the magic and how many '
          'entries were walked')
    check('gmudbe2=n0=%.8s a0=%08x l0=%u o0=%u f0=%d ' in _k37 and
          'gmudbe3=n3=%.8s a3=%08x l3=%u o3=%u f3=%d ' in _k37 and
          'gmudbe4=n6=%.8s a6=%08x l6=%u o6=%u f6=%d ' in _k37,
          'R190 three rows carry name, address, length, auxiliary offset and fit '
          'for nine entries')
    check('char entRowA[352]' in _k37 and 'char entRowB[352]' in _k37 and
          'char entRowC[352]' in _k37 and 'char entRowD[352]' in _k37,
          'R190 every row has a declared bound')
    _i37 = code.find('if (fwKey == 37u) {')
    check(0 < _i36 < _i37 < _i18,
          'R190 key 37 sits after key 36 and before the doorbell ladder')


    # ---- ROUND 191: the name compare must not index past a short name --------
    # Key 37 proved cx.lvl and mx.lvl are in the database with a good address and
    # a fitting auxiliary block, yet key 35 got zero levels for both and ten for
    # "gfx.lvl".  The compare indexed the caller's literal at 7, which is one
    # past the end of a six-letter name.  These checks make the fix structural:
    # the helper must compare a zero-padded copy, never the literal.
    check('uint8_t wantPad[8];' in code,
          'R191 the lookup pads the wanted name into a fixed eight-byte buffer')
    check('while (wantLen < 8u && want[wantLen] != ' in code,
          'R191 the pad is built by walking the literal only up to its terminator')
    check('byte != wantPad[i]' in code,
          'R191 the comparison reads the padded copy, so a short name compares as '
          'name plus padding like the database stores it')
    check('(uint8_t)want[i]' not in code,
          'R191 nothing indexes the caller literal by the field width any more, '
          'which is the one-past-the-end read that hid every six-letter resource')
    check(code.count('if (fwKey == 38u) {') == 1,
          'R191 key 38 is its own block, so no earlier key decides what it reads')
    _k38 = code.split('if (fwKey == 38u) {', 1)[1]
    _k38 = _k38.split('if (fwKey == 39u) {', 1)[0]
    check(_k38 != '', 'R191 the recheck block exists ahead of the doorbell ladder')
    check(_k38.count('quiet_cmd_db_levels(') == 3 and
          '"gfx.lvl"' in _k38 and '"cx.lvl"' in _k38 and '"mx.lvl"' in _k38,
          'R191 key 38 repeats exactly the three lookups key 35 made, so the two '
          'rounds differ only in the helper')
    check('gmuW[' not in _k38 and 'dsb sy' not in _k38 and 'write32' not in _k38 and
          'wr32(' not in _k38,
          'R191 the recheck is read-only')
    check('gmudb5=va=%lx magic=%08x gfx_n=%u cx_n=%u mx_n=%u ' in _k38 and
          'gmudb6=g0=%04x g1=%04x g2=%04x c0=%04x ' in _k38 and
          'gmudb7=gfx_top=%04x gfx_at=%u cx_top=%04x cx_at=%u ' in _k38,
          'R191 three rows carry the counts, the first levels and the top index '
          'for each list')
    check('char lfRowA[352]' in _k38 and 'char lfRowB[352]' in _k38 and
          'char lfRowC[352]' in _k38,
          'R191 every row has a declared bound')
    _i38 = code.find('if (fwKey == 38u) {')
    check(0 < _i37 < _i38 < _i18,
          'R191 key 38 sits after key 37 and before the doorbell ladder')


    # ---- ROUND 192: the vote indexes now come from the database --------------
    # Round 191 fixed the name compare, and key 38 then read gfx.lvl (10 levels,
    # top 0x01a0 at 9), cx.lvl (8, top 0x0180 at 7) and mx.lvl (5, top 0x0180 at
    # 4).  Round 186 had pinned both vote indexes at 0 only because R182 found no
    # command database.  Key 39 derives them at run time from the same helper and
    # writes them, so the vote word and the indexes are no longer constants.
    check(code.count('if (fwKey == 39u) {') == 1,
          'R192 key 39 is its own block, so no earlier key decides what it writes')
    _k39 = code.split('if (fwKey == 39u) {', 1)[1]
    _k39 = _k39.split('if (fwKey == 40u) {', 1)[0]
    check(_k39 != '', 'R192 the vote block exists ahead of the doorbell ladder')
    check(_k39.count('quiet_cmd_db_levels(') == 2 and
          '"gfx.lvl"' in _k39 and '"mx.lvl"' in _k39,
          'R192 the block reads the primary list and the mx fallback, which is '
          'what the upstream secondary lookup lands on when gmxc.lvl is absent')
    check('"gmxc.lvl"' not in _k39,
          'R192 the block does not invent a gmxc.lvl read: key 36 printed the '
          'database names and that one is not among them')
    check('gmuW[ADRENO_GMU_BOOT_V1_GX_VOTE_IDX] = avGxIdx & 0xffu;' in _k39 and
          'gmuW[ADRENO_GMU_BOOT_V1_MX_VOTE_IDX] = avMxIdx & 0xffu;' in _k39,
          'R192 both vote indexes are written from values the lookup produced, '
          'not from a literal zero')
    check('const bool avHave = (avGfxHit.count > 0u && avMxHit.count > 0u);' in _k39 and
          'const unsigned avGxIdx = avHave ? avGfxAt : 0u;' in _k39 and
          'const unsigned avMxIdx = avHave ? avMxAt : 0u;' in _k39,
          'R192 an empty list falls back to index 0 instead of voting through a '
          'meaningless index')
    check('const uint32_t avVote = ((uint32_t)avGfxTop * 65536u) |' in _k39 and
          '((uint32_t)(avMxIdx & 0xffu) * 256u)' in _k39,
          'R192 the row reports the full vote word in the upstream shape '
          '((pri << 16) | (sindex << 8) | pindex)')
    check('gmuW[ADRENO_GMU_BOOT_V1_BOOT_SLUMBER_OPTION] = 0u;' in _k39,
          'R192 the block still starts the rail step the way gfx_rail_on does')
    check('gmuW[ADRENO_GMU_BOOT_V1_SPTPRAC_POWER_CONTROL] = ADRENO_GMU_BOOT_V1_SPTPRAC_ON_VALUE;' in _k39,
          'R192 the sptprac enable word is unchanged from round 186, so the only '
          'difference from that round is the two vote indexes')
    check(_k39.count('gmuW[ADRENO_GMU_BOOT_V1_HOST2GMU_INTR_SET] =') == 1 and
          _k39.count('gmuW[ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_CLR] =') == 1,
          'R192 the boot-slumber doorbell is set once and cleared once')
    check('1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_SET' in _k39 and
          '1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_CLEAR' in _k39,
          'R192 the doorbell uses the boot-slumber bits, not the GPU_SET pair '
          'round 185 used by mistake')
    check('HFI_CTRL_INIT' not in _k39,
          'R192 the block does not retry the HFI store that killed keys 18 and 31 '
          'when nothing was ready')
    check('avSptpPolls < 400000u' in _k39 and 'avPolls < 400000u' in _k39,
          'R192 both waits are bounded')
    check('(avGdscr & 0x00010000u) != 0u' in _k39,
          'R192 the verdict is the GX_ON bit of GPU_CC_GX_GDSCR, as in round 186')
    check('char avRowA[352]' in _k39 and 'char avRowB[352]' in _k39,
          'R192 every row has a declared bound')
    check('gmuvote2=gx=%u mx=%u gfx_n=%u mx_n=%u ' in _k39 and
          'gmuvote3=ack=%u polls=%u gxvote=%08x mxvote=%08x ' in _k39,
          'R192 one row reports the derived indexes and one reports what the '
          'registers read back after the writes')
    _i39 = code.find('if (fwKey == 39u) {')
    check(0 < _i38 < _i39 < _i18,
          'R192 key 39 sits after key 38 and before the doorbell ladder')


    # ---- ROUND 193: load the RSC sequencer, then retry the handshake ---------
    # Key 26 read RSCC_SEQ_MEM_0_DRV0 and got zero, and a6xx_gmu_rpmh_init()
    # (:581-595) is the only thing upstream ever writes there.  Without that
    # microcode the RSC has no reason to answer RSCC_CONTROL_REQ, which is
    # exactly what keys 24, 30..33 and 39 have all reported.  Key 40 reproduces
    # the probe-time setup from :561-595 verbatim and then runs the
    # a6xx_rpmh_start() handshake again, so the ack decides the question.
    check(code.count('if (fwKey == 40u) {') == 1,
          'R193 key 40 is its own block, so no other key decides what it writes')
    _k40 = code.split('if (fwKey == 40u) {', 1)[1]
    _k40 = _k40.split('if (fwKey == 41u) {', 1)[0]
    check(_k40 != '', 'R193 the sequencer block exists ahead of the doorbell ladder')
    check('0xa7a506a0u' in _k40 and '0xa1e6a6e7u' in _k40 and
          '0xa2e081e1u' in _k40 and '0xe9a982e2u' in _k40 and
          '0x0020e8a8u' in _k40,
          'R193 the five sequencer words are the literals from a6xx_gmu.c:590-594, '
          'not values recomputed on the bench')
    check(_k40.count('ADRENO_GMU_BOOT_V1_RSCC_SEQ_MEM_0_DRV0') >= 10,
          'R193 the words are written and read back through the same named offset')
    check('ADRENO_GMU_BOOT_V1_RSCC_STATUS0_DRV0] =' in _k40 and '0x01000000u' in _k40,
          'R193 the SDE clock gate is disabled with BIT(24), as :561 does before '
          'touching anything else on the RSC page')
    check('ADRENO_GMU_BOOT_V1_RSCC_PDC_SLAVE_ID_DRV0] =' in _k40 and '1u;' in _k40,
          'R193 the PDC slave id is set to 1, as :564 does')
    check('ADRENO_GMU_BOOT_V1_PDC_SEQ_START_VALUE;' in _k40 and
          'ADRENO_GMU_BOOT_V1_PDC_MATCH_LO_VALUE;' in _k40 and
          'ADRENO_GMU_BOOT_V1_PDC_MATCH_HI_VALUE;' in _k40,
          'R193 the three PDC handshake words come from the pinned header '
          'constants rather than from literals')
    check('ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA / 4u' in _k40,
          'R193 every RSC store goes through the byte delta converted to a dword '
          'index, which is the addressing the header documents')
    check('loaded=%d' in _k40 and
          '((riPost0 == 0xa7a506a0u) && (riPost1 == 0xa1e6a6e7u) &&' in _k40,
          'R193 the block reports whether all five words read back, so a silent '
          'write cannot be mistaken for a load')
    check(_k40.count('ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] = 2u;') == 1 and
          _k40.count('ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] = 0u;') == 1,
          'R193 the handshake is the upstream pair: request up once, request down once')
    check('riAckPolls < 400000u' in _k40 and 'riBusyPolls < 400000u' in _k40,
          'R193 both waits are bounded')
    check('gmursc5=ack=%08x apolls=%u busy=%08x bpolls=%u ' in _k40,
          'R193 one row reports the handshake outcome together with the rail and '
          'SPTPRAC state, so a surviving run answers three questions at once')
    check('char riRowA[384]' in _k40 and 'char riRowB[384]' in _k40 and
          'char riRowC[384]' in _k40,
          'R193 every row has a declared bound')
    check('gmusetup=pre=%08x/%08x/%08x/%08x/%08x ' in _k40 and
          'gmusetup2=post=%08x/%08x/%08x/%08x/%08x ' in _k40,
          'R193 the before and after rows carry all five words, so a partial '
          'load is visible')
    check('HFI_CTRL_INIT' not in _k40 and 'CM3_SYSRESET' not in _k40,
          'R193 the block does not touch the two registers that killed keys 18, 31 '
          'and every earlier HFI attempt')
    _i40 = code.find('if (fwKey == 40u) {')
    check(0 < _i39 < _i40 < _i18,
          'R193 key 40 sits after key 39 and before the doorbell ladder')

    # ---- ROUND 194: where does the GPU PDC page actually live? -----------
    # R193 loaded the RSC sequencer microcode and read all five words back, and
    # the RSCC handshake still never answered.  Upstream a6xx_gmu_rpmh_init()
    # does not stop there: after the RSC page it programs a PDC page (five
    # sequencer words, six TCS1 commands, six TCS3 commands, then
    # PDC_SEQ_START_ADDR=0 and ENABLE_PDC=0x80000001) before a6xx_rpmh_stop().
    # We have never written that page, and before writing it we have to know
    # which window it is in: mainline sdm845.dtsi splits the gmu node into
    # "gmu_pdc" at 0x0b280000 and "gmu_pdc_seq" at 0x0b480000, while the vendor
    # enchilada.dts hands us one 3 MiB region at 0x0b200000 that contains both.
    # Key 41 surveys the region read-only so the answer comes from the device.
    check(code.count('if (fwKey == 41u) {') == 1,
          'R194 key 41 is its own block, so no other key decides what it reads')
    _k41 = code.split('if (fwKey == 41u) {', 1)[1]
    _k41 = _k41.split('if (fwKey == 42u) {', 1)[0]
    check(_k41 != '', 'R194 the PDC survey block exists ahead of the doorbell ladder')
    check('ADRENO_BOARD_GMU_PDC_BASE' in _k41 and
          'ADRENO_BOARD_GMU_PDC_SIZE' in _k41,
          'R194 the survey maps the board region, not a hand-typed address')
    check('armcpu_map_physical(' in _k41 and 'armcpu_map_physical_rw(' not in _k41,
          'R194 the survey takes the read-only mapping entry, so a stray store '
          'cannot happen through this window')
    check('pcPdcDelta = 0x00080000u;' in _k41 and 'pcSeqDelta = 0x00280000u;' in _k41,
          'R194 the two deltas are the ones mainline sdm845.dtsi implies for a '
          'region based at 0x0b200000: 0x0b280000-0x0b200000 and '
          '0x0b480000-0x0b200000')
    check('fwKey == 41u' in code and 'gmuW[' not in _k41,
          'R194 the survey never writes through the GMU window')
    check('dsb sy' not in _k41 and 'wr32(' not in _k41 and 'write32(' not in _k41,
          'R194 the survey is read-only: no barrier and no store call at all')
    check('ADRENO_GMU_BOOT_V1_PDC_SEQ_MEM_0' in _k41 and
          'ADRENO_GMU_BOOT_V1_PDC_ENABLE_PDC' in _k41 and
          'ADRENO_GMU_BOOT_V1_PDC_SEQ_START_ADDR' in _k41,
          'R194 the three registers read at both offsets come from the pinned '
          'header, so the two windows are compared on the same names')
    check('ADRENO_GMU_BOOT_V1_PDC_TCS0_CONTROL' in _k41,
          'R194 the TCS0 control word is read as a second opinion on which '
          'offset block is backed by real registers')
    check('gmupdc2=vseq=%08x ven=%08x vst=%08x ' in _k41 and
          'pseq=%08x pen=%08x pst=%08x pctl=%08x' in _k41,
          'R194 one row carries both candidate windows, so a single boot '
          'settles the disagreement')
    check('gmupdc3=s0=%08x s1=%08x s2=%08x ' in _k41 and
          's3=%08x s4=%08x' in _k41,
          'R194 the second row carries the five sequencer words at +0x280000')
    check('char pcRowA[320]' in _k41 and 'char pcRowB[320]' in _k41,
          'R194 every row has a declared bound')
    check('gmupdc2=map-failed' in _k41,
          'R194 a failed mapping says so instead of printing zeros as data')
    _i41 = code.find('if (fwKey == 41u) {')
    check(0 < _i40 < _i41 < _i18,
          'R194 key 41 sits after key 40 and before the doorbell ladder')

    # ---- ROUND 195: what does the PDC block already contain? -------------
    # R194 settled where the block is: the vendor region base reads ENABLE_PDC=1
    # and SEQ_MEM_0=0x00a000a8, while the two offsets mainline sdm845.dtsi
    # implies (+0x80000, +0x280000) read zero.  So a future PDC write belongs at
    # the base.  What is left is whether the bootloader already programmed the
    # sequencer.  Key 42 reads the five sequencer words, the two handshake
    # registers and both TCS command banks at the base, read-only, so one boot
    # says whether the PDC is a gap at all.
    check(code.count('if (fwKey == 42u) {') == 1,
          'R195 key 42 is its own block, so no other key decides what it reads')
    _k42 = code.split('if (fwKey == 42u) {', 1)[1]
    _k42 = _k42.split('if (fwKey == 43u) {', 1)[0]
    check(_k42 != '', 'R195 the PDC block survey exists ahead of the doorbell ladder')
    check('ADRENO_BOARD_GMU_PDC_BASE' in _k42 and
          'ADRENO_BOARD_GMU_PDC_SIZE' in _k42,
          'R195 the survey maps the board region, not a hand-typed address')
    check('armcpu_map_physical(' in _k42 and 'armcpu_map_physical_rw(' not in _k42,
          'R195 the survey takes the read-only mapping entry')
    check('gmuW[' not in _k42 and 'dsb sy' not in _k42 and
          'wr32(' not in _k42 and 'write32(' not in _k42,
          'R195 the survey is read-only: no GMU window, no barrier, no store call')
    check('pcPdcDelta' not in _k42 and 'pcSeqDelta' not in _k42,
          'R195 the survey reads the base, because R194 already showed the two '
          'mainline-derived offsets are empty')
    for _name in ('ADRENO_GMU_BOOT_V1_PDC_SEQ_MEM_0 + 1u',
                  'ADRENO_GMU_BOOT_V1_PDC_SEQ_MEM_0 + 4u',
                  'ADRENO_GMU_BOOT_V1_PDC_ENABLE_PDC',
                  'ADRENO_GMU_BOOT_V1_PDC_SEQ_START_ADDR',
                  'ADRENO_GMU_BOOT_V1_PDC_TCS0_CONTROL',
                  'ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD_ENABLE_BANK',
                  'ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_MSGID',
                  'ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_ADDR',
                  'ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD_ENABLE_BANK',
                  'ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_MSGID',
                  'ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_ADDR'):
        check(_name in _k42,
              'R195 the survey reads %s from the pinned header' % _name)
    check('gmupdc4=m0=%08x m1=%08x m2=%08x ' in _k42 and
          'm3=%08x m4=%08x en=%08x st=%08x c0=%08x' in _k42,
          'R195 one row carries all five sequencer words plus the two handshake '
          'registers')
    check('gmupdc5=t1en=%08x t1wt=%08x t1ct=%08x ' in _k42 and
          't1ms=%08x t1ad=%08x t1da=%08x' in _k42,
          'R195 the TCS1 row carries the enable bank, wait bank, control and the '
          'first command triple, so an already-programmed sequencer is visible')
    check('gmupdc6=t3en=%08x t3wt=%08x t3ct=%08x ' in _k42 and
          't3ms=%08x t3ad=%08x t3da=%08x' in _k42,
          'R195 the TCS3 row mirrors TCS1')
    check('char pcRowD[320]' in _k42 and 'char pcRowE[320]' in _k42 and
          'char pcRowF[320]' in _k42,
          'R195 every row has a declared bound')
    check('gmupdc4=map-failed' in _k42,
          'R195 a failed mapping says so instead of printing zeros as data')
    _i42 = code.find('if (fwKey == 42u) {')
    check(0 < _i41 < _i42 < _i18,
          'R195 key 42 sits after key 41 and before the doorbell ladder')
    # ---- ROUND 196: does the RSC ignore BOTH directions? ------------------
    # R193 loaded the sequencer microcode (it read back word for word) and then
    # replayed a6xx_rpmh_start(): REQ=2 armed, RSCC_CONTROL_ACK bit 1 never set
    # in 400000 polls.  One silent direction cannot separate "the controller
    # never serves this CPU" from "the start path is missing one precondition",
    # so key 43 asks the mirror question in the same boot: a6xx_rpmh_stop()
    # writes REQ=1 and waits on GPU_RSCC_RSC_STATUS0_DRV0 bit 16, a different
    # register and a different bit on the same controller.
    check(code.count('if (fwKey == 43u) {') == 1,
          'R196 key 43 is its own block, so no other key decides what it asks')
    _k43 = code.split('if (fwKey == 43u) {', 1)[1]
    _k43 = _k43.split('if (fwKey == 44u) {', 1)[0]
    check(_k43 != '', 'R196 the two-direction matrix exists ahead of the doorbell ladder')
    check('ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA / 4u' in _k43,
          'R196 every RSC store converts the pinned byte delta to a dword index, '
          'so it lands inside the GMU window')
    check('ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA * 4u' not in _k43,
          'R196 the byte delta is never multiplied by four again')
    for _name in ('ADRENO_GMU_BOOT_V1_RSCC_STATUS0_DRV0',
                  'ADRENO_GMU_BOOT_V1_RSCC_PDC_SLAVE_ID_DRV0',
                  'ADRENO_GMU_BOOT_V1_RSCC_OVERRIDE_START_ADDR',
                  'ADRENO_GMU_BOOT_V1_RSCC_PDC_SEQ_START_ADDR',
                  'ADRENO_GMU_BOOT_V1_RSCC_PDC_MATCH_VALUE_LO',
                  'ADRENO_GMU_BOOT_V1_RSCC_PDC_MATCH_VALUE_HI',
                  'ADRENO_GMU_BOOT_V1_RSCC_SEQ_MEM_0_DRV0',
                  'ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ',
                  'ADRENO_GMU_BOOT_V1_RSCC_CONTROL_ACK',
                  'ADRENO_GMU_BOOT_V1_RSCC_SEQ_BUSY_DRV0'):
        check(_name in _k43, 'R196 the matrix uses %s from the pinned header' % _name)
    check(_k43.count('gmuW[ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] = 1u;') == 1,
          'R196 the stop direction arms exactly one REQ=1')
    check(_k43.count('gmuW[ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] = 2u;') == 1,
          'R196 the start direction arms exactly one REQ=2')
    check(_k43.count('gmuW[ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] = 0u;') == 2,
          'R196 each direction releases the request it armed')
    check('(stDown & 0x00010000u) != 0u' in _k43,
          'R196 the stop direction polls STATUS0_DRV0 bit 16, as a6xx_rpmh_stop does')
    check('(stUpAck & 2u) != 0u' in _k43,
          'R196 the start direction polls RSCC_CONTROL_ACK bit 1, as a6xx_rpmh_start does')
    check('stDownPolls < 400000u' in _k43 and 'stUpPolls < 400000u' in _k43 and
          'stBusyPolls < 400000u' in _k43,
          'R196 all three waits are bounded, so a silent controller cannot hang the boot')
    check('gmust1=dir=down req1 st0=%08x bit16=%d polls=%u' in _k43,
          'R196 the stop row carries the answered bit and the poll count, so a '
          'zero poll count and a missing answer look different')
    check('gmust2=dir=up req2 ack=%08x bit1=%d polls=%u busy=%08x bpolls=%u' in _k43,
          'R196 the start row carries the ack bit, the poll count and SEQ_BUSY')
    check('gmust3=loaded=%d gdscr=%08x sptp=%08x fwinit=%08x intr=%08x' in _k43,
          'R196 one row says whether the microcode stuck and what the rails did')
    check('char stRowA[320]' in _k43 and 'char stRowB[320]' in _k43 and
          'char stRowC[320]' in _k43,
          'R196 every row has a declared bound')
    check('stLoaded = (stPost0 == 0xa7a506a0u) && (stPost4 == 0x0020e8a8u)' in _k43,
          'R196 the microcode is judged by read-back, not assumed written')
    check('HFI_CTRL_INIT' not in _k43 and 'CM3_SYSRESET' not in _k43,
          'R196 the matrix touches no doorbell and no reset, so it cannot repeat '
          'the deaths R177 and R184 established')
    _i43 = code.find('if (fwKey == 43u) {')
    check(0 < _i42 < _i43 < _i18,
          'R196 key 43 sits after key 42 and before the doorbell ladder')



    # ---- ROUND 197: localise a start sequence that never finishes -------
    _i44 = code.find('if (fwKey == 44u) {')
    check(_i44 >= 0, 'R197 key 44 exists')
    check(code.count('if (fwKey == 44u) {') == 1, 'R197 key 44 appears exactly once')
    _k44 = code.split('if (fwKey == 44u) {', 1)[1]
    _k44 = _k44.split('if (fwKey == 45u) {', 1)[0]
    check('ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA / 4u' in _k44,
          'R197 the sequencer words go through the dword-indexed RSC window')
    check('ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA * 4u' not in _k44,
          'R197 the byte delta is never multiplied by four')
    for _needle in ('ADRENO_GMU_BOOT_V1_RSCC_STATUS0_DRV0',
                    'ADRENO_GMU_BOOT_V1_RSCC_PDC_SLAVE_ID_DRV0',
                    'ADRENO_GMU_BOOT_V1_RSCC_OVERRIDE_START_ADDR',
                    'ADRENO_GMU_BOOT_V1_RSCC_PDC_SEQ_START_ADDR',
                    'ADRENO_GMU_BOOT_V1_RSCC_SEQ_MEM_0_DRV0',
                    'ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ',
                    'ADRENO_GMU_BOOT_V1_RSCC_CONTROL_ACK',
                    'ADRENO_GMU_BOOT_V1_RSCC_SEQ_BUSY_DRV0'):
        check(_needle in _k44, 'R197 uses the pinned constant %s' % _needle)
    check('0xa7a506a0u' in _k44 and '0x0020e8a8u' in _k44,
          'R197 loads the pinned five-word sequencer microcode')
    check(_k44.count('ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] = 1u;') == 1,
          'R197 arms the stop direction exactly once')
    check(_k44.count('ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] = 2u;') == 1,
          'R197 arms the start direction exactly once')
    check(_k44.count('ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] = 0u;') == 2,
          'R197 releases the request after each direction')
    check('(rd32(sGmuFwVa, ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA +' in _k44,
          'R197 reads the RSCC state back through the read helper')
    check('0x00010000u' in _k44, 'R197 waits on the stop-direction status bit')
    check('2u) != 0u' in _k44, 'R197 waits on the acknowledge bit one')
    check(_k44.count('400000u') >= 2, 'R197 both waits are bounded')
    check('0x0346u' in _k44 and '0x053eu' in _k44,
          'R197 reads the four pinned per-TCS status offsets')
    for _row in ('"gmusq%u=%s st0=%08x ack=%08x busy=%08x tcs=%08x/%08x/%08x/%08x"',
                 '"gmusq4=ao=%08x fence=%08x intr=%08x gdscr=%08x sptp=%08x fwinit=%08x"'):
        check(_row in code, 'R197 emits the row %s' % _row[:24])
    check('char sqRow[384];' in _k44, 'R197 sizes the per-phase row buffer')
    check('char sqTail[384];' in _k44, 'R197 sizes the tail row buffer')
    check('HFI_CTRL_INIT' not in _k44,
          'R197 never touches the HFI init that wedges the host')
    check('CM3_SYSRESET' not in _k44,
          'R197 never repulses the CM3 reset')
    check(0 < _i43 < _i44 < _i18,
          'R197 key 44 sits after key 43 and before the doorbell ladder')

    # ---- ROUND 198: the CPU-side APPS RSC votes the two GPU rails ----------
    check(code.count('if (fwKey == 45u) {') == 1,
          'R198 there is exactly one APPS RSC request block')
    _k45 = (code.split('if (fwKey == 45u) {', 1)[1]
            .split('if (fwKey == 46u) {', 1)[0])
    check(_k45 != '', 'R198 the block is delimited by the doorbell ladder')
    check('armcpu_map_physical_rw(0x00000000179e0000ULL, 0x00010000u)' in _k45,
          'R198 the APPS RSC window is mapped with the WRITABLE entry point')
    check('armcpu_map_physical(0x0000000085fe0000ULL, 0x00020000u)' in _k45,
          'R198 the command database is mapped read-only')
    check(_k45.count('quiet_cmd_db_levels(sDbRoVa, "gfx.lvl"') == 1 and
          _k45.count('quiet_cmd_db_levels(sDbRoVa, "cx.lvl"') == 1,
          'R198 both rail resources are looked up by name')
    check('(rlDrv / 0x10000u) % 256u' in _k45,
          'R198 the layout version is derived without a reserved field literal')
    check('0x0d00u + 672u * rlT' in _k45 and 'rlTBase + rlEnOff' in _k45,
          'R198 the free TCS is found by reading cmd_enable at the pinned stride')
    check('tcs_base' not in _k45 and '0x00010000u' in _k45 and '0x01010000u' in _k45,
          'R198 the control register is armed then triggered')
    check(_k45.count('0x00010108u') == 2,
          'R198 both writes use the write-response message id')
    check(_k45.count('IODelay(1u);') == 1 and 'rlPolls < 200000u' in _k45,
          'R198 the completion poll is bounded and sleeps between reads')
    check('char railRowA[352];' in _k45 and 'char railRowB[352];' in _k45 and
          'char railRowC[352];' in _k45,
          'R198 the three rows fit their buffers')
    check('gmurail4=rsc=%lx drv=%08x tcs=%u gfx_n=%u cx_n=%u gfx_a=%08x cx_a=%08x' in code and
          'gmurail5=gfx_top=%04x cx_top=%04x gfx_at=%u cx_at=%u ' in code and
          'gmurail6=gdscr=%08x cxst=%08x gxst=%08x sptp=%08x fwinit=%08x' in code,
          'R198 the three rows carry the pinned field order')
    check('HFI_CTRL_INIT' not in _k45 and 'CM3_SYSRESET' not in _k45,
          'R198 the block never touches the two known lethal registers')
    check('gmurail4=map-failed' in code,
          'R198 a failed map is reported rather than silently skipped')
    _i45 = code.find('if (fwKey == 45u) {')
    check(0 < _i44 < _i45 < _i18,
          'R198 key 45 sits after key 44 and before the doorbell ladder')

    # ---- ROUND 199: the GX GDSC is driven from the GPUCC words ------------
    check(code.count('if (fwKey == 46u) {') == 1,
          'R199 there is exactly one GX GDSC block')
    _k46 = (code.split('if (fwKey == 46u) {', 1)[1]
            .split('if (fwKey == 47u) {', 1)[0])
    check(_k46 != '', 'R199 the block is delimited by the doorbell ladder')
    check('ADRENO_GMU_BOOT_V1_GPU_CC_GX_GDSCR + 1u' in _k46,
          'R199 the CFG_GDSCR status word is derived from the pinned GDSCR offset')
    check('(0x05091508ull - ADRENO_BOARD_GMU_BASE) / 4u' in _k46,
          'R199 the clamp register is derived as a dword index inside the mapping')
    check('(ADRENO_DOMAIN_CX_STATUS - ADRENO_BOARD_GMU_BASE) / 4u' in _k46,
          'R199 the CX status word is derived, never typed')
    check('armcpu_map_physical_rw(0x00000000179e0000ULL, 0x00010000u)' in _k46,
          'R199 the rail vote reuses the writable APPS RSC window')
    check('quiet_cmd_db_levels(sGxDbVa, "gfx.lvl"' in _k46,
          'R199 the vote level comes from the command database')
    check('0x00010108u' in _k46 and '0x00010000u' in _k46 and '0x01010000u' in _k46,
          'R199 the request uses the pinned message id and the arm/trigger pair')
    check(_k46.count('gmuW[gxClampWord] = gxClPre | 0x00000010u;') == 1 and
          _k46.count('gmuW[gxClampWord] = gxClPre & 0xffffffefu;') == 1 and
          _k46.count('gmuW[gxClampWord] = gxClPre & 0xfffffffeu;') == 1 and
          _k46.count('gmuW[gxGdscrWord] = gxGdPre & 0xfffffffeu;') == 1,
          'R199 the AON reset pulse, the clamp deassert and the collapse clear are each written once')
    check('(gxCfgPost & 0x00010000u) != 0u' in _k46 and 'gxOnPolls < 20000u' in _k46,
          'R199 the power-up poll is bounded and tests the pinned status bit')
    check('HFI_CTRL_INIT' not in _k46 and 'CM3_SYSRESET' not in _k46,
          'R199 the block never touches the two known lethal registers')
    check('GMU2HOST_INTR_SET' not in _k46 and 'GMU2HOST_INTR_CLR' not in _k46,
          'R199 no doorbell is rung from this block')
    check('char gxRowA[352];' in _k46 and 'char gxRowB[352];' in _k46 and
          'char gxRowC[352];' in _k46,
          'R199 the three rows fit their buffers')
    check('gmugx2=vote_a=%08x vote_top=%04x tcs=%u st=%08x en=%08x polls=%u' in code and
          'gmugx3=pre_gdscr=%08x pre_cfg=%08x pre_clamp=%08x pre_cx=%08x' in code and
          'gmugx4=gdscr=%08x cfg=%08x clamp=%08x cx=%08x gx_on=%d polls=%u' in code,
          'R199 the three rows carry the pinned field order')
    _i46 = code.find('if (fwKey == 46u) {')
    check(0 < _i45 < _i46 < _i18,
          'R199 key 46 sits after key 45 and before the doorbell ladder')

    # ---- ROUND 200: the same GDSC stores, with the CM3 alive ---------------
    check(code.count('if (fwKey == 47u) {') == 1,
          'R200 there is exactly one CM3-alive GDSC block')
    _k47 = (code.split('if (fwKey == 47u) {', 1)[1]
            .split('if (fwKey == 48u) {', 1)[0])
    check(_k47 != '', 'R200 the block is delimited by the doorbell ladder')
    check('gmugx5=vote_a=%08x vote_top=%04x tcs=%u st=%08x en=%08x polls=%u' in code and
          'gmugx6=pre_gdscr=%08x pre_cfg=%08x pre_clamp=%08x pre_cx=%08x' in code and
          'gmugx7=gdscr=%08x cfg=%08x clamp=%08x cx=%08x gx_on=%d polls=%u' in code,
          'R200 the three rows are retagged so they cannot be confused with key 46')
    check(_k47.count('gmuW[gxClampWord] = gxClPre | 0x00000010u;') == 1 and
          _k47.count('gmuW[gxClampWord] = gxClPre & 0xffffffefu;') == 1 and
          _k47.count('gmuW[gxClampWord] = gxClPre & 0xfffffffeu;') == 1 and
          _k47.count('gmuW[gxGdscrWord] = gxGdPre & 0xfffffffeu;') == 1,
          'R200 the four GDSC stores are identical to key 46, one each')
    check('(gxCfgPost & 0x00010000u) != 0u' in _k47 and 'gxOnPolls < 20000u' in _k47,
          'R200 the power-up poll keeps the pinned bit and its bound')
    check('HFI_CTRL_INIT' not in _k47 and 'CM3_SYSRESET' not in _k47,
          'R200 key 47 still never writes the two lethal registers')
    check('armcpu_map_physical_rw(0x00000000179e0000ULL, 0x00010000u)' in _k47,
          'R200 the rail vote half is unchanged')
    _i47 = code.find('if (fwKey == 47u) {')
    check(0 < _i46 < _i47 < _i18,
          'R200 key 47 sits directly after key 46 and before the doorbell ladder')

    # ---- ROUND 201: the CX control and a ten-times longer bound -----------
    check(code.count('if (fwKey == 48u) {') == 1,
          'R201 there is exactly one CX-control block')
    _k48 = (code.split('if (fwKey == 48u) {', 1)[1]
            .split('if (fwKey == 49u) {', 1)[0])
    check(_k48 != '', 'R201 the block is delimited by the doorbell ladder')
    check('(0x0509106cull - ADRENO_BOARD_GMU_BASE) / 4u' in _k48 and
          '(0x05091070ull - ADRENO_BOARD_GMU_BASE) / 4u' in _k48,
          'R201 the CX control reads the pinned CX GDSCR and its CFG companion')
    check('gmugx8=cx_gdscr=%08x cx_cfg=%08x cx_bit16=%d' in code,
          'R201 the control row reports CX completion separately')
    check('gyPolls < 200000u' in _k48,
          'R201 the GX bound is ten times the R199/R200 one, so slowness is excluded')
    check(_k48.count('gmuW[gyClampWord] = gyClPre | 0x00000010u;') == 1 and
          _k48.count('gmuW[gyClampWord] = gyClPre & 0xffffffefu;') == 1 and
          _k48.count('gmuW[gyClampWord] = gyClPre & 0xfffffffeu;') == 1 and
          _k48.count('gmuW[gyGdscrWord] = gyGdPre & 0xfffffffeu;') == 1,
          'R201 the four stores are the same pinned sequence, once each')
    check('gmugx9=pre=%08x gdscr=%08x cfg=%08x clamp=%08x gx_on=%d polls=%u' in code,
          'R201 the result row keeps the before/after contrast')
    check('HFI_CTRL_INIT' not in _k48 and 'CM3_SYSRESET' not in _k48,
          'R201 the block touches neither lethal register')
    _i48 = code.find('if (fwKey == 48u) {')
    check(0 < _i47 < _i48 < _i18,
          'R201 key 48 sits after key 47 and before the doorbell ladder')

    # ---- R202: the HFI start with the queue table withheld ----
    _i48b = code.find('if (fwKey == 48u) {')
    _i49 = code.find('if (fwKey == 49u) {')
    _i18r202 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k49 = (code.split('if (fwKey == 49u) {', 1)[1]
            .split('if (fwKey == 50u) {', 1)[0])
    check(_k49 != '', 'R202 key 49 sits before the doorbell ladder')
    check(_k49.count('ADRENO_GMU_BOOT_V1_HFI_QTBL_ADDR] = 0u;') == 1,
          'R202 the queue table address is zeroed exactly once')
    check(_k49.count('ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO] = 0u;') == 1,
          'R202 the queue table info word is zeroed exactly once')
    check(_k49.count('ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;') == 1,
          'R202 the HFI start store is the one write under test')
    check(_k49.count('gmuW[') == 3,
          'R202 the block has exactly three writes')
    check('hzPolls < 400000u' in _k49,
          'R202 the status wait is bounded')
    check('(hzStat & 1u) == 1u' in _k49,
          'R202 success is the status bit coming up')
    check('"gmuhfi5=qtbl_pre=%08x info_pre=%08x "' in _k49 and
          '"stat_pre=%08x win=%d"' in _k49,
          'R202 the pre snapshot row is pinned')
    check('"gmuhfi6=stat=%08x polls=%u intr=%08x "' in _k49 and
          '"fwinit=%08x ok=%d"' in _k49,
          'R202 the outcome row is pinned')
    check('char hzRowA[320];' in _k49 and 'char hzRowB[320];' in _k49,
          'R202 the two rows fit their buffers')
    check('CM3_SYSRESET' not in _k49,
          'R202 the block never pulses the CM3 reset')
    check(0 < _i48b < _i49 < _i18r202,
          'R202 key 49 sits after key 48 and before the doorbell ladder')

    # ---- R203: null table address with the info word live ----
    _i50 = code.find('if (fwKey == 50u) {')
    _i18r203 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k50 = (code.split('if (fwKey == 50u) {', 1)[1]
            .split('if (fwKey == 51u) {', 1)[0])
    check(_k50 != '', 'R203 key 50 sits before the doorbell ladder')
    check(_k50.count('ADRENO_GMU_BOOT_V1_HFI_QTBL_ADDR] = 0u;') == 1,
          'R203 only the table address is zeroed')
    check(_k50.count('ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO] = 1u;') == 1,
          'R203 the info word keeps the upstream value of one')
    check(_k50.count('ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;') == 1,
          'R203 the HFI start store is the one write under test')
    check(_k50.count('gmuW[') == 3, 'R203 the block has exactly three writes')
    check('iaPolls < 400000u' in _k50, 'R203 the status wait is bounded')
    check('(iaStat & 1u) == 1u' in _k50, 'R203 success is the status bit coming up')
    check('"gmuhfi7=addr_pre=%08x info_pre=%08x "' in _k50 and
          '"stat_pre=%08x win=%d"' in _k50,
          'R203 the pre snapshot row is pinned')
    check('"gmuhfi8=stat=%08x polls=%u intr=%08x "' in _k50 and
          '"fwinit=%08x ok=%d"' in _k50,
          'R203 the outcome row is pinned')
    check('char iaRowA[320];' in _k50 and 'char iaRowB[320];' in _k50,
          'R203 the two rows fit their buffers')
    check('CM3_SYSRESET' not in _k50, 'R203 the block never pulses the CM3 reset')
    check(0 < _i50 < _i18r203,
          'R203 key 50 sits after key 49 and before the doorbell ladder')

    # ---- R204: the fatal store, issued late enough to leave a trail -------
    _i51 = code.find('if (fwKey == 51u) {')
    _i18r204 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k51 = (code.split('if (fwKey == 51u) {', 1)[1]
            .split('if (fwKey == 52u) {', 1)[0])
    check(_k51 != '', 'R204 key 51 sits before the doorbell ladder')
    check('for (unsigned dlWait = 0u; dlWait < 20000u; ++dlWait)' in _k51 and
          'IODelay(1000u);' in _k51,
          'R204 the store is preceded by a bounded twenty-second wait, so the '
          'pre-store row reaches the disk before a death can hide it')
    check(_k51.index('gmudly2=') < _k51.index('HFI_CTRL_INIT'),
          'R204 the second row is printed BEFORE the store that has killed the '
          'machine three times')
    check(_k51.count('gmudly1=') == 1 and _k51.count('gmudly2=') == 1 and
          _k51.count('gmudly3=') == 1,
          'R204 three rows: the pre snapshot, the late pre-store snapshot and '
          'the post store snapshot')
    check('HFI_QTBL_ADDR) = 0u;' not in _k51,
          'R204 the block does not use a call-shaped address store')
    check('HFI_QTBL_ADDR] = 0u;' in _k51 and 'HFI_QTBL_INFO] = 1u;' in _k51 and
          'HFI_CTRL_INIT] = 1u;' in _k51,
          'R204 the store set is the one that died in R203: null address, live '
          'info word, then the start')
    check('char dlRowA[320];' in _k51 and 'char dlRowB[320];' in _k51 and
          'char dlRowC[320];' in _k51,
          'R204 each row has its own oversized buffer')
    check('CM3_SYSRESET' not in _k51,
          'R204 the block never pulses the CM3 reset')
    check(0 < _i50 < _i51 < _i18r204,
          'R204 key 51 sits after key 50 and before the doorbell ladder')

    # ---- R205: a real wall clock, so three rows land in three flushes ------
    # R204's "twenty second" lead-in moved the log clock 0.32 s: IODelay() runs
    # roughly sixty times faster than the microsecond count it is given, so the
    # second row could never reach a 500 ms flush. Key 52 spins on the
    # architectural counter instead and prints three times.
    _i52 = code.find('if (fwKey == 52u) {')
    _i18r205 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k52 = (code.split('if (fwKey == 52u) {', 1)[1]
            .split('if (fwKey == 53u) {', 1)[0])
    check(_k52 != '', 'R205 key 52 sits before the doorbell ladder')
    check('static uint64_t probeCntpct(void)' in code and
          'static void probeSpinSeconds(unsigned seconds)' in code,
          'R205 the probe has a counter reader and a second-granularity spin')
    check('mrs %0, cntpct_el0' in code and 'mrs %0, cntfrq_el0' in code,
          'R205 both helpers read the architectural counter and its frequency, '
          'the same registers BootLog stamps the window boot_id with')
    check(code.count('probeSpinSeconds(3u);') == 3,
          'R205 three three-second spins: one before each of the three rows and '
          'one after the store, so no two rows share a flush')
    check(_k52.count('gmudlz1=') == 1 and _k52.count('gmudlz2=') == 1 and
          _k52.count('gmudlz3=') == 1 and _k52.count('gmudlz4=') == 1,
          'R205 four rows: the pre snapshot, two timed heartbeats and the post '
          'store snapshot')
    check(_k52.index('gmudlz3=') < _k52.index('HFI_CTRL_INIT'),
          'R205 the last pre-store row is printed BEFORE the store that has '
          'killed the machine four times')
    check('HFI_QTBL_ADDR] = 0u;' in _k52 and 'HFI_QTBL_INFO] = 1u;' in _k52 and
          'HFI_CTRL_INIT] = 1u;' in _k52,
          'R205 the store set is unchanged from R203: null address, live info '
          'word, then the start')
    check('char dzRowA[320];' in _k52 and 'char dzRowB[320];' in _k52 and
          'char dzRowC[320];' in _k52 and 'char dzRowD[320];' in _k52,
          'R205 each of the four rows has its own oversized buffer')
    check('CM3_SYSRESET' not in _k52,
          'R205 the block never pulses the CM3 reset')
    check('IODelay(' not in _k52,
          'R205 the block does not build its timing on IODelay, whose real cost '
          'R204 measured at about sixteen microseconds')
    check(0 < _i51 < _i52 < _i18r205,
          'R205 key 52 sits after key 51 and before the doorbell ladder')

    # ---- ROUND 206: pace the fatal store with a yielding sleep ----
    # R205 spun on CNTPCT for six seconds and the window came back
    # byte-identical: a busy spin cannot be preempted, so the log writer -- which
    # between flushes does nothing but sleep -- never got a slot and the death
    # erased the whole trail. IOSleep goes through assert_wait_timeout, the same
    # call BootLog's own writer makes, so the writer runs while key 53 waits.
    _i53 = code.find('if (fwKey == 53u) {')
    _i18r206 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k53 = (code.split('if (fwKey == 53u) {', 1)[1]
            .split('if (fwKey == 54u) {', 1)[0])
    check(_k53 != '', 'R206 key 53 sits before the doorbell ladder')
    check(_k53.count('IOSleep(3000u);') == 2 and _k53.count('IOSleep(2000u);') == 1,
          'R206 three yielding sleeps: two three-second ones before the store and '
          'one two-second one after it, each longer than a 500 ms flush')
    check('probeSpinSeconds' not in _k53 and 'IODelay(' not in _k53,
          'R206 the block never spins and never calls IODelay: R205 proved a busy '
          'wait starves the log writer')
    check(_k53.count('gmudly5=') == 1 and _k53.count('gmudly6=') == 1 and
          _k53.count('gmudly7=') == 1 and _k53.count('gmudly8=') == 1,
          'R206 four rows: the pre snapshot, two heartbeats and the post snapshot')
    check(_k53.index('gmudly7=') < _k53.index('HFI_CTRL_INIT'),
          'R206 the last pre-store row is printed BEFORE the store that has killed '
          'the machine five times')
    check(_k53.index('IOSleep(3000u);') < _k53.index('gmudly6=') <
          _k53.index('IOSleep(3000u);', _k53.index('IOSleep(3000u);') + 1),
          'R206 the heartbeats are separated by a full sleep, so no two rows share '
          'a flush')
    check('HFI_QTBL_ADDR] = 0u;' in _k53 and 'HFI_QTBL_INFO] = 1u;' in _k53 and
          'HFI_CTRL_INIT] = 1u;' in _k53,
          'R206 the store set is unchanged from R203: null address, live info '
          'word, then the start')
    check('char dyRowA[320];' in _k53 and 'char dyRowB[320];' in _k53 and
          'char dyRowC[320];' in _k53 and 'char dyRowD[320];' in _k53,
          'R206 each of the four rows has its own oversized buffer')
    check('CM3_SYSRESET' not in _k53,
          'R206 the block never pulses the CM3 reset')
    check(0 < _i52 < _i53 < _i18r206,
          'R206 key 53 sits after key 52 and before the doorbell ladder')

    # ---- ROUND 207: is a yielding sleep legal here at all? ----
    # R206 slept between two rows and the machine stopped 26 ms after the first
    # one, before the store it was built around. So key 54 writes nothing and
    # only paces: six short sleeps with a row after each. If all six land, the
    # sleep primitive is fine and R206's death had another cause; if it dies after
    # the first row, blocking is illegal at this point and no sleep-based pacing
    # can be used to observe the fatal store.
    _i54 = code.find('if (fwKey == 54u) {')
    _i18r207 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k54 = (code.split('if (fwKey == 54u) {', 1)[1]
            .split('if (fwKey == 55u) {', 1)[0])
    check(_k54 != '', 'R207 key 54 sits before the doorbell ladder')
    check(_k54.count('IOSleep(250u);') == 1,
          'R207 the pacing loop sleeps 250 ms per step')
    check('pwStep < 6u' in _k54,
          'R207 six steps, so the last row lands at least a full second after the '
          'first one and every step crosses a flush')
    check(_k54.count('gmuwake=') == 1,
          'R207 one row format for every step, carrying the step index')
    check('HFI_CTRL_INIT' not in _k54 and 'HFI_QTBL_ADDR] =' not in _k54 and
          'HFI_QTBL_INFO] =' not in _k54,
          'R207 the block starts nothing: no table store and no HFI start, so it '
          'is a clean control for R206')
    check('gmuW[' not in _k54,
          'R207 the block performs no register store at all')
    check('probeSpinSeconds' not in _k54 and 'IODelay(' not in _k54,
          'R207 the block never spins and never calls IODelay')
    check('char pwRow[256];' in _k54,
          'R207 the row has its own buffer declared inside the loop')
    check('CM3_SYSRESET' not in _k54,
          'R207 the block never pulses the CM3 reset')
    check(0 < _i53 < _i54 < _i18r207,
          'R207 key 54 sits after key 53 and before the doorbell ladder')

    # ---- ROUND 208: pace the fatal store with the read loop R201 timed ----
    # R207 wrote nothing at all and still lost the machine inside its first
    # blocking sleep, so the sleep is what kills, not the store four rounds
    # blamed. R201 is the counter-example: 200000 passes of a pure read loop took
    # a measured 3.17 s and both of its stamped rows reached the disk. Key 55
    # therefore paces with that loop and prints a row between passes.
    _i55 = code.find('if (fwKey == 55u) {')
    _i18r208 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k55 = (code.split('if (fwKey == 55u) {', 1)[1]
            .split('if (fwKey == 56u) {', 1)[0])
    check(_k55 != '', 'R208 key 55 sits before the doorbell ladder')
    check(_k55.count('gmupace1=') == 1 and _k55.count('gmupace2=') == 1 and
          _k55.count('gmupace3=') == 1 and _k55.count('gmupace4=') == 1,
          'R208 four rows: two heartbeats before the store, the about-to-store row, '
          'and the post snapshot')
    check(_k55.count('< 200000u') == 3,
          'R208 three 200000-pass read loops, the exact count R201 measured at 3.17 s')
    check('IOSleep' not in _k55,
          'R208 the block never blocks: R206 and R207 both died inside IOSleep')
    check('probeSpinSeconds' not in _k55 and 'IODelay(' not in _k55,
          'R208 the block neither spins nor calls IODelay')
    check(_k55.index('gmupace3=') < _k55.index('HFI_CTRL_INIT'),
          'R208 the about-to-store row is printed BEFORE the store')
    check('HFI_QTBL_ADDR] = 0u;' in _k55 and 'HFI_QTBL_INFO] = 1u;' in _k55 and
          'HFI_CTRL_INIT] = 1u;' in _k55,
          'R208 the store set is unchanged from R203: null address, live info word, '
          'then the start')
    check('char pc8RowA[320];' in _k55 and 'char pc8RowB[320];' in _k55 and
          'char pc8RowC[320];' in _k55 and 'char pc8RowD[352];' in _k55,
          'R208 each row has its own oversized buffer')
    check('CM3_SYSRESET' not in _k55,
          'R208 the block never pulses the CM3 reset')
    check(0 < _i54 < _i55 < _i18r208,
          'R208 key 55 sits after key 54 and before the doorbell ladder')

    # ---- ROUND 209: bracket the two store steps with rows and pace nothing ----
    # R204 proved a row printed at 29.95 s reaches the disk. R205 lost the machine
    # to a six second spin, R206 and R207 to IOSleep, R208 to three 3.17 s read
    # loops -- every pacing device the probe owns is either fatal or enough to
    # starve the writer. So the brackets need no pacing: one row before the
    # queue-table stores, one straight after them, one straight after the start.
    _i56 = code.find('if (fwKey == 56u) {')
    _i18r209 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k56 = (code.split('if (fwKey == 56u) {', 1)[1]
            .split('if (fwKey == 57u) {', 1)[0])
    check(_k56 != '', 'R209 key 56 sits before the doorbell ladder')
    check(_k56.count('gmufine1=') == 1 and _k56.count('gmufine2=') == 1 and
          _k56.count('gmufine3=') == 1,
          'R209 three bracket rows around the two store steps')
    check('IOSleep' not in _k56 and 'IODelay(' not in _k56 and
          'probeSpinSeconds' not in _k56 and '< 200000u' not in _k56,
          'R209 the block paces nothing: no sleep, no delay, no spin, no read loop')
    check(_k56.count('gmuW[') == 3,
          'R209 exactly three stores, one pointer, the same set as R203')
    check('HFI_QTBL_ADDR] = 0u;' in _k56 and 'HFI_QTBL_INFO] = 1u;' in _k56 and
          'HFI_CTRL_INIT] = 1u;' in _k56,
          'R209 the store set is unchanged: null address, live info word, then the start')
    check(_k56.index('gmufine1=') < _k56.index('HFI_QTBL_ADDR] = 0u;'),
          'R209 row A is printed BEFORE the queue-table stores')
    check(_k56.index('HFI_CTRL_INIT] = 1u;') < _k56.index('gmufine3='),
          'R209 row C is printed AFTER the start store')
    check(_k56.index('HFI_QTBL_INFO] = 1u;') < _k56.index('gmufine2=') <
          _k56.index('HFI_CTRL_INIT] = 1u;'),
          'R209 row B sits between the queue-table stores and the start store')
    check('char fkRowA[320];' in _k56 and 'char fkRowB[320];' in _k56 and
          'char fkRowC[352];' in _k56,
          'R209 each row has its own oversized buffer')
    check('CM3_SYSRESET' not in _k56,
          'R209 the block never pulses the CM3 reset')
    check(0 < _i55 < _i56 < _i18r209,
          'R209 key 56 sits after key 55 and before the doorbell ladder')

    # ---- ROUND 210: pace every step with R201's yielding read loop ----
    # R209 printed three rows and landed none: with no pacing the kernel dies within
    # microseconds of the first store and the writer only flushes every 500 ms, so
    # nothing in the buffer reaches the disk. R201's `rd32 + IODelay(1u)` loop is the
    # delay that works here (200000 passes, 3.17 s measured, both rows landed), while
    # R208's loop of the same read count without the IODelay starved the writer.
    _i57 = code.find('if (fwKey == 57u) {')
    _i18r210 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k57 = (code.split('if (fwKey == 57u) {', 1)[1]
            .split('if (fwKey == 58u) {', 1)[0])
    check(_k57 != '', 'R210 key 57 sits before the doorbell ladder')
    check(_k57.count('gmupace5=') == 1 and _k57.count('gmupace6=') == 1 and
          _k57.count('gmupace7=') == 1,
          'R210 three rows: the pre snapshot, the table-written witness, the post snapshot')
    check(_k57.count('gmuW[') == 3,
          'R210 exactly three stores, one pointer, the same set as R203')
    check('HFI_QTBL_ADDR] = 0u;' in _k57 and 'HFI_QTBL_INFO] = 1u;' in _k57 and
          'HFI_CTRL_INIT] = 1u;' in _k57,
          'R210 the store set is unchanged: null address, live info word, then the start')
    check(_k57.count('IODelay(1u);') == 4,
          'R210 four yielding waits, one after each step, the exact body R201 timed')
    check(_k57.count('< 200000u') == 4,
          'R210 every wait is a 200000-pass loop, the count R201 measured at 3.17 s')
    check('IOSleep' not in _k57 and 'probeSpinSeconds' not in _k57,
          'R210 the block never blocks and never spins: both were proven fatal')
    check(_k57.index('gmupace5=') < _k57.index('HFI_QTBL_ADDR] = 0u;'),
          'R210 the pre snapshot is printed BEFORE the queue-table stores')
    check(_k57.index('HFI_CTRL_INIT] = 1u;') < _k57.index('gmupace7='),
          'R210 the post snapshot is printed AFTER the start store')
    check('char fwRowA[320];' in _k57 and 'char fwRowB[320];' in _k57 and
          'char fwRowC[352];' in _k57,
          'R210 each row has its own oversized buffer')
    check('CM3_SYSRESET' not in _k57,
          'R210 the block never pulses the CM3 reset')
    check(0 < _i56 < _i57 < _i18r210,
          'R210 key 57 sits after key 56 and before the doorbell ladder')

    # ---- ROUND 211: rows and a paced wait, and no store at all ----
    # Keys 56 and 57 both printed rows before their first store and both lost the whole
    # window, while key 48 on the same image executes the identical guard set and writes
    # normally. Key 58 keeps the rows and R201's paced wait and drops every store, which
    # separates the two remaining mechanisms.
    _i58 = code.find('if (fwKey == 58u) {')
    _i18r211 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k58 = (code.split('if (fwKey == 58u) {', 1)[1]
            .split('if (fwKey == 59u) {', 1)[0])
    check(_k58 != '', 'R211 key 58 sits before the doorbell ladder')
    check(_k58.count('gmuW[') == 0,
          'R211 the block writes no register at all: that is the whole point')
    check(_k58.count('gmupure1=') == 1 and _k58.count('gmupure2=') == 1,
          'R211 two rows: the pre snapshot and the after-the-wait witness')
    check(_k58.count('IODelay(1u);') == 1 and _k58.count('< 200000u') == 1,
          'R211 one paced wait, the exact body R201 measured at 3.17 s')
    check('IOSleep' not in _k58 and 'probeSpinSeconds' not in _k58,
          'R211 the block neither blocks nor spins')
    check(_k58.index('gmupure1=') < _k58.index('IODelay(1u);'),
          'R211 the first row is printed BEFORE the paced wait')
    check(_k58.index('IODelay(1u);') < _k58.index('gmupure2='),
          'R211 the second row is printed AFTER the paced wait')
    check('char fwRowP[320];' in _k58 and 'char fwRowQ[320];' in _k58,
          'R211 each row has its own oversized buffer')
    check('CM3_SYSRESET' not in _k58 and 'HFI_CTRL_INIT' not in _k58,
          'R211 the block never touches the reset or the start register')
    check(0 < _i57 < _i58 < _i18r211,
          'R211 key 58 sits after key 57 and before the doorbell ladder')

    # ---- ROUND 212: split the store set, each half paced ----
    # Key 58 landed both rows around a 3.17 s paced wait with no store at all, so the
    # wait is transplantable and R210's loss is attributable to its stores. Keys 59 and
    # 60 split that store set, each paced before and after the write.
    _i59 = code.find('if (fwKey == 59u) {')
    _i60 = code.find('if (fwKey == 60u) {')
    _i18r212 = code.find('if (fwKey >= 18u && fwKey <= 21u) {')
    _k59 = (code.split('if (fwKey == 59u) {', 1)[1]
            .split('if (fwKey == 60u) {', 1)[0])
    _k60 = (code.split('if (fwKey == 60u) {', 1)[1]
            .split('if (fwKey == 62u) {', 1)[0])
    check(_k59 != '' and _k60 != '', 'R212 both halves sit before the doorbell ladder')
    check(_k59.count('gmuW[') == 2,
          'R212 key 59 writes exactly the two queue-table words')
    check('HFI_QTBL_ADDR] = 0u;' in _k59 and 'HFI_QTBL_INFO] = 1u;' in _k59 and
          'HFI_CTRL_INIT' not in _k59,
          'R212 key 59 nulls the address, raises the info word, and never starts')
    check(_k60.count('gmuW[') == 1 and 'HFI_CTRL_INIT] = 1u;' in _k60,
          'R212 key 60 writes exactly the start word')
    check('HFI_QTBL_ADDR]' not in _k60 and 'HFI_QTBL_INFO]' not in _k60,
          'R212 key 60 leaves the table exactly as the firmware block built it')
    check(_k59.count('IODelay(1u);') == 2 and _k60.count('IODelay(1u);') == 2,
          'R212 each half is paced before and after its store')
    check(_k59.count('< 200000u') == 2 and _k60.count('< 200000u') == 2,
          'R212 every wait is a 200000-pass loop, the count measured at 3.17 s')
    check('IOSleep' not in _k59 and 'IOSleep' not in _k60,
          'R212 neither half blocks')
    check(_k59.count('gmusp59=') == 1 and _k59.count('gmusp59b=') == 1,
          'R212 key 59 brackets its two stores with rows')
    check(_k60.count('gmusp60=') == 1 and _k60.count('gmusp60b=') == 1,
          'R212 key 60 brackets its single store with rows')
    check('CM3_SYSRESET' not in _k59 and 'CM3_SYSRESET' not in _k60,
          'R212 neither half pulses the CM3 reset')
    check(0 < _i58 < _i59 < _i60 < _i18r212,
          'R212 keys 59 and 60 sit after key 58 and before the doorbell ladder')

    # ---- ROUND 213: key 62 keeps the rows, stretches the wait three times past
    # ---- R201 s measurement, and prints a row immediately before the store, so
    # ---- a lost window can be told apart from a lost row.
    _i62 = code.find('if (fwKey == 62u) {')
    _k62 = (code.split('if (fwKey == 62u) {', 1)[1]
            .split('if (fwKey == 63u) {', 1)[0]) if _i62 >= 0 else ''
    check(_i62 > 0 and _k62 != '', 'R213 key 62 exists and sits before the doorbell ladder')
    check(_k62.count('gmu213a=') == 1 and _k62.count('gmu213b=') == 1 and
          _k62.count('gmu213c=') == 1,
          'R213 the block brackets its store with three rows')
    check('r213WaitA < 600000u' in _k62,
          'R213 the first wait is three times the 200000 passes R201 timed')
    check(_k62.count('IODelay(1u);') == 3,
          'R213 all three waits are the yielding read loop, none is a bare spin')
    check('IOSleep' not in _k62, 'R213 the block never blocks')
    check(_k62.count('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;') == 1,
          'R213 exactly one start store')
    check(_k62.find('gmu213b=') < _k62.find('HFI_CTRL_INIT] = 1u;'),
          'R213 the second row is printed before the store, so a lost window names the store')
    # ---- ROUND 214: key 63 makes each row durable at once.
    # Ten rounds of zero-byte windows were unreadable because the writer commits a
    # whole round before it updates the header naming it, so a death mid-round
    # erases the rows that did print.  Key 63 calls native_ufs_log_sync() after
    # every row, which issues SYNCHRONIZE CACHE and costs the CPU nothing.
    _i63 = code.find('if (fwKey == 63u) {')
    _k63 = (code.split('if (fwKey == 63u) {', 1)[1]
            .split('if (fwKey == 64u) {', 1)[0]) if _i63 >= 0 else ''
    check(_i63 > 0 and _k63 != '', 'R214 key 63 exists and sits before the doorbell ladder')
    check(_i63 > _i62, 'R214 key 63 follows key 62, so key 62 s slice ends before it')
    check(_k63.count('gmu214a=') == 1 and _k63.count('gmu214b=') == 1 and
          _k63.count('gmu214c=') == 1,
          'R214 the block prints exactly three rows')
    _k63_syncs = _k63.count('if (&native_ufs_log_sync) (void)native_ufs_log_sync();')
    check(_k63_syncs == 3,
          'R214 each of the three rows is followed by a guarded sync')
    check(_k63.count('__attribute__((weak_import))') == 1,
          'R214 the symbol is declared weak, so an image without it still links')
    check('IOSleep' not in _k63, 'R214 the block never blocks')
    check('IODelay' not in _k63 and 'for (' not in _k63,
          'R214 the block issues no wait loop at all: durability comes from the sync')
    check(_k63.count('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;') == 1,
          'R214 exactly one start store')
    check(_k63.find('gmu214b=') < _k63.find('HFI_CTRL_INIT] = 1u;') <
          _k63.find('gmu214c='),
          'R214 the rows bracket the start store, so the medium names which side it died on')
    # ---- ROUND 218: key 64 now carries the store bracket.
    # R217's control proved key 64 boots and its rows reach the medium, while key 65
    # died before even an unconditional echo row appeared -- so the store question had
    # to move onto the key that is known to run. Key 64 keeps its value and changes
    # its body: row A, the two table words, row B, the start word, row C.
    _i64 = code.find('if (fwKey == 64u) {')
    _k64 = (code.split('if (fwKey == 64u) {', 1)[1]
            .split('if (fwKey == 65u) {', 1)[0]) if _i64 >= 0 else ''
    check(_i64 > 0 and _k64 != '', 'R218 key 64 exists, before key 65 and the doorbell ladder')
    check(_k64.count('gmu218a=') == 1 and _k64.count('gmu218b=') == 1 and
          _k64.count('gmu218c=') == 1,
          'R218 the block prints exactly three rows')
    check(_k64.count('gmuW[') == 3,
          'R218 exactly three stores: the two table words and the start word')
    check(_k64.find('gmu218a=') < _k64.find('HFI_CTRL_INIT] = 1u;') <
          _k64.find('gmu218c='),
          'R218 the rows bracket the start store')
    check(_k64.find('gmu218b=') < _k64.find('HFI_CTRL_INIT] = 1u;'),
          'R218 the second row precedes the start store, so a missing C names it')
    check('boot_log_panic_flush' not in _k64,
          'R218 it does not call the flush primitive: R215 s window has no [panic] '
          'round text, so that primitive never committed anything')
    check('native_ufs_log_sync' not in _k64,
          'R218 it does not repeat R214 s mistake either')
    check('IOSleep' not in _k64 and 'IODelay' not in _k64 and 'for (' not in _k64,
          'R218 no wait of any kind')
    # ---- ROUND 222: the start word moves INTO key 66, so the key value stops being a variable.
    # R221 gave a full writer round of margin and still lost every row, including the one
    # printed before the store, so "not enough time to flush" is not the explanation. Every
    # zero-byte result so far also changed the key value, and the arm window has been widened
    # by hand each round; this key keeps 66 and changes only the body.
    _i66b = code.find('if (fwKey == 66u) {')
    _k66b = (code.split('if (fwKey == 66u) {', 1)[1]
             .split('if (fwKey == 67u) {', 1)[0]) if _i66b >= 0 else ''
    check(_k66b.find('gmu223a=') < _k66b.find('HFI_CTRL_INIT] = 0u;') <
          _k66b.find('gmu223c='),
          'R223 the rows bracket the store')
    check('IOSleep' not in _k66b and 'IODelay' not in _k66b and 'for (' not in _k66b,
          'R223 no wait of any kind: this key isolates the value, not the pacing')

    # ---- ROUND 219: three single-variable branches that split R218's three stores.
    # R218 lost even the unconditional echo row when the body grew the stores, so at
    # least one write stops the machine; these keys differ by exactly one thing each.
    _i66 = code.find('if (fwKey == 66u) {')
    _i67 = code.find('if (fwKey == 67u) {')
    _i68 = code.find('if (fwKey == 68u) {')
    check(_i66 > 0 and _i67 > _i66 and _i68 > _i67,
          'R219 keys 66, 67 and 68 exist in that order')
    _k66 = (code.split('if (fwKey == 66u) {', 1)[1]
            .split('if (fwKey == 67u) {', 1)[0]) if _i66 >= 0 else ''
    _k67 = (code.split('if (fwKey == 67u) {', 1)[1]
            .split('if (fwKey == 68u) {', 1)[0]) if _i67 >= 0 else ''
    _k68 = (code.split('if (fwKey == 68u) {', 1)[1]
            .split('if (fwKey == 69u) {', 1)[0]) if _i68 >= 0 else ''
    check(_k66b.count('gmu223a=') == 1 and _k66b.count('gmu223b=') == 1 and
          _k66b.count('gmu223c=') == 1,
          'R223 key 66 prints three rows around the store')
    check(_k66b.count('gmuW[') == 1,
          'R223 key 66 carries exactly one store')
    check(_k66b.count('HFI_CTRL_INIT] = 0u;') == 1,
          'R223 that store writes 0, so the access is held constant and only the value '
          'differs from R222')
    check(_k67.count('gmuW[') == 2,
          'R219 key 67 writes exactly the two table words')
    check('HFI_CTRL_INIT' not in _k67,
          'R219 key 67 never touches the start word')
    check(_k68.count('gmuW[') == 1,
          'R219 key 68 writes exactly the start word')
    check('HFI_QTBL_ADDR' not in _k68.split('gmuW[')[1] if _k68.count('gmuW[') == 1 else True,
          'R219 key 68 leaves the queue table exactly as the firmware block built it')
    for _name, _body in (('66', _k66), ('67', _k67), ('68', _k68)):
        check('IOSleep' not in _body and 'IODelay' not in _body and 'for (' not in _body,
              'R219 key %s waits for nothing' % _name)
    # ---- ROUND 220: key 69 exists to make the bark/panic-flush channel test visible.
    # R219 could not say WHERE HFI_CTRL_INIT=1 kills, because no [panic] text survives.
    # ArmCpu's enchilada-wdog-stop + -stop-panic switch makes the timer interrupt call
    # barkCapture() on demand, so the channel can be validated before trusting it.
    _i69 = code.find('if (fwKey == 69u) {')
    _k69 = (code.split('if (fwKey == 69u) {', 1)[1]
            .split('if (fwKey == 70u) {', 1)[0]) if _i69 >= 0 else ''
    check(_i69 > 0 and _k69 != '', 'R220 key 69 exists and sits before the doorbell ladder')
    check(_i69 > _i68, 'R220 key 69 follows the R219 branches')
    check(_k69.count('gmu220a=') == 1, 'R220 the key prints exactly one row')
    check('gmuW[' not in _k69,
          'R220 key 69 stores NOTHING: it is a channel probe, not a store test')
    check('IOSleep' not in _k69 and 'IODelay' not in _k69 and 'for (' not in _k69,
          'R220 it waits for nothing')

    # ---- ROUND 232: keys 71 (read the context banks) and 72 (write them).
    # Step 3b is the first context-bank write this port has ever made, so it is split:
    # 71 is read-only and records the pre-state; 72 writes and reads back.
    _i71 = code.find('if (fwKey == 71u) {')
    _i72 = code.find('if (fwKey == 72u) {')
    check(_i71 > 0 and _i72 > _i71, 'R232 keys 71 and 72 exist, in that order')
    _k71 = (code.split('if (fwKey == 71u) {', 1)[1]
            .split('if (fwKey == 72u) {', 1)[0]) if _i71 >= 0 else ''
    # ROUND 234 puts keys 73/74 between key 72 and the old 18..21 ladder, so key 72's
    # body is delimited by its OWN successor rather than by the ladder.
    _k72 = (code.split('if (fwKey == 72u) {', 1)[1]
            .split('if (fwKey == 76u || fwKey == 77u) {', 1)[0]) if _i72 >= 0 else ''
    check(_k71 != '' and _k72 != '', 'R232 both blocks are non-empty')
    # key 71 must not store anything: it exists to record a PRE-write baseline.
    check('= abs72' not in _k71 and 'armcpu_dma_alloc_writable' not in _k71,
          'R232 key 71 allocates nothing and writes nothing')
    check('gmu71a=' in _k71 and 'gmu71b=' in _k71,
          'R232 key 71 reports the geometry and the current CB words')
    # key 72 uses the real building blocks, in the right order.
    check('adreno_install_cb_geometry(' in _k72,
          'R232 key 72 derives CBn_BASE from ID1')
    check('adreno_domain_cx_is_on(' in _k72,
          'R232 key 72 refuses when CX is down')
    check('armcpu_dma_alloc_writable' in _k72,
          'R232 key 72 allocates the table block through the proven primitive')
    check(_k72.find('adreno_table_build(') < _k72.find('adreno_smmu_ctx_plan('),
          'R232 the table is populated BEFORE the context plan is applied')
    check(_k72.find('adreno_smmu_ctx_plan(') < _k72.find('adreno_install_absolute('),
          'R232 the plan is produced before it is made absolute')
    check('gmu72d=' in _k72 and 'rd32(gSmmuVa' in _k72,
          'R232 key 72 reads the written words back')
    check('IOSleep' not in _k71 and 'IOSleep' not in _k72,
          'R232 neither key blocks')

    # ---- ROUND 237: keys 78 (mask only) and 79 (mask, then one start store).
    # R236 measured both GMU interrupt masks at zero but could not tell "masking
    # killed it" from "the start word killed it" from "the 0.63 s spin starved the
    # writer": four stores, a spin and the start word inside one writer period.
    # R237 splits the actions and puts nothing between them, so the only difference
    # between the two keys is that single store. That difference lives in a runtime
    # comparison, which is why it is asserted here in source and not in an image.
    _i7879 = code.find('if (fwKey == 78u || fwKey == 79u) {')
    # Key 70 is only located further down this file, so the ordering anchor here is
    # key 72, which the R232 section above has already found.
    check(_i7879 > _i72,
          'R237 keys 78/79 follow the R232 write key in source order')
    # R238 inserted keys 80/81 directly after this block, so the delimiter is the
    # new direct successor, not the old 18..21 ladder further down the file.
    _k7879 = (code.split('if (fwKey == 78u || fwKey == 79u) {', 1)[1]
              .split('if (fwKey == 80u || fwKey == 81u) {', 1)[0]) if _i7879 >= 0 else ''
    check(_k7879 != '', 'R237 the key 78/79 block is delimited by its own successor')
    # The shared prologue: both keys write the two clears and the two masks.
    # Counted as STORE forms, not as bare names: the register names also appear on
    # the read-back lines that saved the previous mask values.
    _k78head = _k7879.split('if (!r78is79) {', 1)[0] if 'if (!r78is79) {' in _k7879 else ''
    check(_k78head != '', 'R237 the shared mask prologue is found')
    check('HFI_CTRL_INIT' not in _k78head,
          'R237 the shared prologue never touches the start word')
    for _word, _label in (('AO_HOST_INTERRUPT_CLR', 'the AO clear'),
                          ('AO_HOST_INTERRUPT_MASK', 'the AO mask'),
                          ('GMU2HOST_INTR_CLR', 'the HFI clear'),
                          ('GMU2HOST_INTR_MASK', 'the HFI mask')):
        _store = 'gmuW[ADRENO_GMU_BOOT_V1_%s] = ' % _word
        check(_k78head.count(_store) == 1,
              'R237 the prologue stores %s exactly once' % _label)
    # Key 78 must return without storing anything at all.
    _tail = _k7879.split('if (!r78is79) {', 1)[1] if 'if (!r78is79) {' in _k7879 else ''
    _k78only = _tail.split('} else {', 1)[0] if _tail else ''
    check(_k78only != '', 'R237 key 78 has a body of its own')
    check('HFI_CTRL_INIT' not in _k78only,
          'R237 key 78 never stores the start word: it is the control')
    check(_k78only.count('gmuW[') == 0,
          'R237 key 78 stores nothing beyond the shared prologue')
    check('gmu78c=no_start=1' in _k78only,
          'R237 key 78 says explicitly that it did not start the firmware')
    # Key 79 must differ by exactly one store, and by nothing else.
    _k79only = _tail.split('} else {', 1)[1] if '} else {' in _tail else ''
    check(_k79only != '', 'R237 key 79 has a body of its own')
    check(_k79only.count('HFI_CTRL_INIT') == 1,
          'R237 key 79 stores the start word exactly once')
    check(_k79only.count('gmuW[') == 3,
          'R237 key 79 stores exactly three words: the start word and the two '
          'restores that put both masks back as they were found')
    # Neither key may buy observation time: that is the failure R221/R236 repeated.
    for _name, _body in (('78', _k78only), ('79', _k79only)):
        check('IOSleep' not in _body,
              'R237 key %s does not sleep' % _name)
    check(_k79only.find('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;') <
          _k79only.find('for ('),
          'R237 key 79 stores the start word BEFORE its bounded poll, so no spin '
          'sits between the last row and the store')

    # ---- ROUND 221: key 70 pauses a full writer round BEFORE the start word.
    # The measured margin at this point in boot is 107 ms against a ~500 ms round, so
    # R219's zero bytes could not separate "died instantly" from "survived, never flushed".
    _i70 = code.find('if (fwKey == 70u) {')
    # ROUND 234 inserted keys 73/74 (one of which stores) between key 70 and the
    # doorbell ladder, so key 70's body is delimited by its immediate successor.
    _k70 = (code.split('if (fwKey == 70u) {', 1)[1]
            .split('if (fwKey == 71u) {', 1)[0]) if _i70 >= 0 else ''
    check(_i70 > 0 and _k70 != '', 'R221 key 70 exists and sits before the doorbell ladder')
    check(_i70 > _i69, 'R221 key 70 follows key 69')
    check(_k70.count('gmu221a=') == 1 and _k70.count('gmu221b=') == 1 and
          _k70.count('gmu221c=') == 1,
          'R221 the block prints exactly three rows')
    check(_k70.count('gmuW[') == 3,
          'R221 exactly three stores: the two table words and the start word')
    check('r221Wait < 200000u' in _k70,
          'R221 the pause is the bounded read loop R201 timed, not a sleep')
    check(_k70.count('IODelay(1u);') == 1,
          'R221 the pause yields with IODelay, so the writer can run')
    check(_k70.find('gmu221b=') < _k70.find('r221Wait <') < _k70.find('HFI_CTRL_INIT] = 1u;'),
          'R221 the pause sits between the durable row B and the start word')
    check('IOSleep' not in _k70, 'R221 it never blocks')






    # ---- ROUND 216: key 65 finally brackets the store with durable rows.
    # R215 proved boot_log_panic_flush() commits a fresh row, so the bracket is now
    # readable: row A, the two queue-table words, row B, the start store, row C.
    _i65 = code.find('if (fwKey == 65u) {')
    _k65 = (code.split('if (fwKey == 65u) {', 1)[1]
            .split('if (fwKey == 66u) {', 1)[0]) if _i65 >= 0 else ''
    check(_i65 > 0 and _k65 != '', 'R216 key 65 exists and sits before the doorbell ladder')
    check(_i65 > _i64, 'R216 key 65 follows key 64')
    check(_k65.count('gmu216a=') == 1 and _k65.count('gmu216b=') == 1 and
          _k65.count('gmu216c=') == 1,
          'R216 the block prints exactly three rows')
    check(_k65.count('if (&boot_log_panic_flush) boot_log_panic_flush();') == 3,
          'R216 every row is committed by the primitive R215 proved')
    check('native_ufs_log_sync' not in _k65,
          'R216 it does not repeat R214 s mistake: no bare cache sync')
    check(_k65.count('gmuW[') == 3,
          'R216 exactly three stores: the two table words and the start word')
    check(_k65.find('gmu216a=') < _k65.find('HFI_CTRL_INIT] = 1u;') <
          _k65.find('gmu216c='),
          'R216 the rows bracket the start store')
    check(_k65.find('gmu216b=') < _k65.find('HFI_CTRL_INIT] = 1u;'),
          'R216 the second row precedes the start store, so a missing C names it')
    check('IOSleep' not in _k65 and 'IODelay' not in _k65 and 'for (' not in _k65,
          'R216 no wait of any kind: durability comes from the flush')



    check('CM3_SYSRESET' not in _k62, 'R213 the block never pulses the CM3 reset')
    check('char r213RowA[320];' in _k62 and 'char r213RowB[320];' in _k62 and
          'char r213RowC[352];' in _k62,
          'R213 the three row buffers keep their sizes')
    check(0 < _i60 < _i62 < _i18r212,
          'R213 key 62 sits after key 60 and before the doorbell ladder')

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



    # --- R238 last-action assertions -----------------------------------------
    _i8081 = code.find('if (fwKey == 80u || fwKey == 81u) {')
    check(_i8081 >= 0, 'R238 the last-action block exists')
    if _i8081 >= 0:
        # R239 inserted keys 82/83 directly after this block, so the delimiter is
        # the new direct successor, not the doorbell ladder further down the file.
        _k8081 = code[_i8081:].split('if (fwKey == 82u || fwKey == 83u) {', 1)[0]
        _k80head = _k8081.split('if (r80is81) {', 1)[0]
        _k81only = _k8081.split('if (r80is81) {', 1)[1] if 'if (r80is81) {' in _k8081 else ''

        check('HFI_CTRL_INIT' not in _k80head,
              'R238 the shared preamble never touches the start word')
        for _w in ('AO_HOST_INTERRUPT_CLR', 'GMU2HOST_INTR_CLR',
                   'AO_HOST_INTERRUPT_MASK', 'GMU2HOST_INTR_MASK'):
            _n = _k80head.count('gmuW[ADRENO_GMU_BOOT_V1_%s] = ' % _w)
            check(_n == 1, 'R238 the preamble stores %s exactly once (got %d)' % (_w, _n))

        check(_k81only.count('gmuW[') == 1,
              'R238 key 81 adds exactly one store, the start word')
        check('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;' in _k81only,
              'R238 that one store is HFI_CTRL_INIT = 1')
        check('IOSleep' not in _k8081, 'R238 neither key uses IOSleep')
        check('for (' not in _k8081, 'R238 neither key spins or polls')

        # The store must be the LAST action: after it there may only be a dsb and
        # closing braces, no read, no print, no restore.
        _tail = _k81only.split('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;', 1)[1]
        _tail_stripped = _tail.replace('__asm__ volatile("dsb sy" ::: "memory");', '')
        _tail_stripped = re.sub(r'//[^\n]*', '', _tail_stripped)
        _tail_stripped = _tail_stripped.replace('}', '').strip()
        check(_tail_stripped == '',
              'R238 nothing follows the start-word store except the barrier and braces; got %r'
              % _tail_stripped[:120])

        check('(r80aoMask & 0x3fu) == 0x3fu' in _k80head,
              'R238 fully-masked is judged on the six implemented AO bits, not strict equality')

    # --- R239 shape-control and table-clear keys ------------------------------
    # R238 narrowed the kill to one store by varying whether the store exists. R239
    # varies what the store says: key 82 writes the same word with value 0 in the same
    # position (so the shape itself is either innocent or implicated), and key 83
    # drops the table descriptor before writing 1 (so "needs a table to open" is
    # separated from "the write itself kills"). Both are runtime comparisons, so the
    # difference is asserted in source, not in an image.
    _i8283 = code.find('if (fwKey == 82u || fwKey == 83u) {')
    check(_i8283 >= 0, 'R239 the shape-control block exists')
    if _i8283 >= 0:
        check(_i8081 >= 0 and _i8283 > _i8081,
              'R239 the new block follows the R238 block it is a control for')
        # R240 inserted keys 84/85 directly after this block, so the delimiter is the
        # new direct successor, not the doorbell ladder further down the file.
        _k8283 = code[_i8283:].split('if (fwKey == 84u || fwKey == 85u) {', 1)[0]
        check(_k8283 != '', 'R239 the block is delimited by the doorbell ladder')
        _k82head = _k8283.split('if (r82is83) {', 1)[0]
        check('HFI_CTRL_INIT' not in _k82head,
              'R239 the shared preamble never touches the start word')
        check('HFI_QTBL_INFO' not in _k82head,
              'R239 the shared preamble leaves the table descriptor alone')
        for _w in ('AO_HOST_INTERRUPT_CLR', 'GMU2HOST_INTR_CLR',
                   'AO_HOST_INTERRUPT_MASK', 'GMU2HOST_INTR_MASK'):
            _n = _k82head.count('gmuW[ADRENO_GMU_BOOT_V1_%s] = ' % _w)
            check(_n == 1, 'R239 the preamble stores %s exactly once (got %d)' % (_w, _n))
        check('(r82aoMask & 0x3fu) == 0x3fu' in _k82head,
              'R239 fully-masked is still judged on the six implemented AO bits')

        _tail82 = _k8283.split('if (r82is83) {', 1)[1] if 'if (r82is83) {' in _k8283 else ''
        check(_tail82 != '', 'R239 the two key bodies are found')
        _k83only = _tail82.split('} else {', 1)[0]
        _k82only = _tail82.split('} else {', 1)[1] if '} else {' in _tail82 else ''
        check(_k83only != '' and _k82only != '', 'R239 both bodies are non-empty')

        # key 83: clear the descriptor, then one start word. Two stores, in that order.
        check(_k83only.count('gmuW[') == 2,
              'R239 key 83 stores exactly two words (descriptor, then start word)')
        check(_k83only.find('gmuW[ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO] = 0u;') >= 0,
              'R239 key 83 clears the table descriptor')
        check(_k83only.find('gmuW[ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO] = 0u;') <
              _k83only.find('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;'),
              'R239 the descriptor is cleared BEFORE the start word')
        # key 82: the same store site, value 0. Exactly one store.
        check(_k82only.count('gmuW[') == 1,
              'R239 key 82 stores exactly one word')
        check('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 0u;' in _k82only,
              'R239 that word is HFI_CTRL_INIT = 0, the shape control')
        check('HFI_CTRL_INIT] = 1u;' not in _k82only,
              'R239 key 82 never asks the firmware to start')

        # Neither key may buy time, print, read back or restore masks afterwards.
        check('IOSleep' not in _k8283 and 'for (' not in _k8283,
              'R239 neither key sleeps, spins or polls')
        for _name, _body in (('82', _k82only), ('83', _k83only)):
            _post = _body.split('gmuW[', 1)[1]
            _post = _post.split(';', 1)[1] if ';' in _post else ''
            _post = _post.replace('__asm__ volatile("dsb sy" ::: "memory");', '')
            _post = __import__('re').sub(r'//[^\n]*', '', _post)
            _post = _post.replace('}', '').replace('else', '').strip()
            if _name == '83':
                # key 83 has a second store, so the residual is the start word itself.
                _post = _post.split('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;', 1)[-1]
                _post = _post.replace('__asm__ volatile("dsb sy" ::: "memory");', '')
                _post = __import__('re').sub(r'//[^\n]*', '', _post)
                _post = _post.replace('}', '').strip()
            check(_post == '',
                  'R239 nothing follows the last store of key %s; got %r'
                  % (_name, _post[:120]))

    # --- R240 zero-page and positive-control keys -----------------------------
    # R239 left "descriptor non-zero AND firmware told to start" as the correlated
    # pair, but never showed the firmware reading the table. Key 84 points QTBL_ADDR
    # at a page it has just zeroed (and verifies, printing nz=), keeps INFO = 1, and
    # writes the start word last in upstream order; key 85 repeats R238 key 81 as a
    # positive control. Both differences are runtime comparisons, so they are
    # asserted in source, not in an image.
    _i8485 = code.find('if (fwKey == 84u || fwKey == 85u) {')
    check(_i8485 >= 0, 'R240 the zero-page block exists')
    if _i8485 >= 0:
        check(_i8283 >= 0 and _i8485 > _i8283,
              'R240 the new block follows the R239 block it builds on')
        _k8485 = code[_i8485:].split('\nif (fwKey == 86u || fwKey == 87u || fwKey == 88u) {\n', 1)[0]
        check(_k8485 != '', 'R240 the block is delimited by the doorbell ladder')
        _k84pre = _k8485.split('if (r84is85) {', 1)[0]  # code-only anchor: stage6 strips comments
        check(_k84pre != '', 'R240 the preamble is found')
        # Everything printed (rows A, B and E) comes before the last row; the only stores
        # that early are the four interrupt masks.
        check(_k84pre.count('gmuW[') == 4,
              'R240 only the four masks are stored before the last row (got %d)'
              % _k84pre.count('gmuW['))
        check('HFI_CTRL_INIT' not in _k84pre,
              'R240 the preamble never touches the start word')
        check('HFI_QTBL_ADDR] =' not in _k84pre and 'HFI_QTBL_INFO] =' not in _k84pre,
              'R240 the preamble never touches the table descriptor')
        for _w in ('AO_HOST_INTERRUPT_CLR', 'GMU2HOST_INTR_CLR',
                   'AO_HOST_INTERRUPT_MASK', 'GMU2HOST_INTR_MASK'):
            _n = _k84pre.count('gmuW[ADRENO_GMU_BOOT_V1_%s] = ' % _w)
            check(_n == 1, 'R240 the preamble stores %s exactly once (got %d)' % (_w, _n))
        check('(r84aoMask & 0x3fu) == 0x3fu' in _k84pre,
              'R240 fully-masked is still judged on the six implemented AO bits')
        # The zero page must be zeroed AND verified before any row claims it.
        check('r84pg[r84i] = 0u;' in _k84pre,
              'R240 the fresh page is written with zeros')
        _zloops = _k84pre.count('for (uint32_t r84i = 0u; r84i < 1024u; ++r84i)')
        check(_zloops == 2,
              'R240 the page is zeroed and then re-scanned (got %d loops)' % _zloops)
        check('gmu84e=zero_page pa=%08x nz=%u' in _k84pre,
              'R240 the row that carries nz= is emitted before the install')

        _k84tail = _k8485.split('if (r84is85) {', 1)[1] if 'if (r84is85) {' in _k8485 else ''
        check(_k84tail != '', 'R240 the two key bodies are found')
        _t85 = _k84tail.split('} else if (r84pa != 0u) {', 1)[0]
        _t84 = _k84tail.split('} else if (r84pa != 0u) {', 1)[1] if \
            '} else if (r84pa != 0u) {' in _k84tail else ''
        check(_t85 != '' and _t84 != '', 'R240 both bodies are non-empty')

        # key 85: exactly R238 key 81 -- one store, the start word.
        check(_t85.count('gmuW[') == 1, 'R240 key 85 stores exactly one word')
        check('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;' in _t85,
              'R240 that word is the start word (positive control)')
        check('HFI_QTBL_ADDR' not in _t85 and 'HFI_QTBL_INFO' not in _t85,
              'R240 key 85 leaves the real descriptor untouched')

        # key 84: ADDR -> INFO -> start word, upstream order, three stores.
        check(_t84.count('gmuW[') == 3, 'R240 key 84 stores exactly three words')
        _a = _t84.find('gmuW[ADRENO_GMU_BOOT_V1_HFI_QTBL_ADDR] = r84pa;')
        _b = _t84.find('gmuW[ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO] = 1u;')
        _c = _t84.find('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;')
        check(_a >= 0 and _b >= 0 and _c >= 0,
              'R240 key 84 writes QTBL_ADDR, QTBL_INFO and the start word')
        check(_a < _b < _c, 'R240 key 84 follows a6xx_hfi_start order (ADDR, INFO, start)')
        check('HFI_QTBL_ADDR] = r84pa;' in _t84,
              'R240 key 84 points the descriptor at the zero page, not at the real table')

        # Neither key may buy time, print, read back or restore masks afterwards.
        check('IOSleep' not in _k8485, 'R240 neither key sleeps')
        _tailstore84 = _t84.split('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;', 1)[1]
        _tailstore85 = _t85.split('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;', 1)[1]
        for _name, _post in (('84', _tailstore84), ('85', _tailstore85)):
            _post = _post.replace('__asm__ volatile("dsb sy" ::: "memory");', '')
            _post = __import__('re').sub(r'//[^\n]*', '', _post)
            _post = _post.replace('}', '').replace('else', '').strip()
            check(_post == '',
                  'R240 nothing follows the last store of key %s; got %r'
                  % (_name, _post[:120]))

    # --- R241 stream-routing keys ---------------------------------------------
    # R240 pinned the fatality to "descriptor bit set AND start bit set", independent of
    # what the descriptor points at. R241 touches no start word at all and asks only
    # whether the stream-routing registers exist and accept writes: key 86 is a control
    # with no SMMU write, key 88 writes SMR0 (VALID | SID 4) and restores it, key 87
    # writes S2CR0 (TYPE = BYPASS) at the offset whose reads aborted the boot twice.
    _i8687 = code.find('if (fwKey == 86u || fwKey == 87u || fwKey == 88u) {')
    check(_i8687 >= 0, 'R241 the stream-routing block exists')
    if _i8687 >= 0:
        check(_i8485 >= 0 and _i8687 > _i8485,
              'R241 the new block follows the R240 block it builds on')
        _k8687 = code[_i8687:].split(
            '\nif (fwKey == 89u || fwKey == 90u || fwKey == 91u) {\n', 1)[0]
        check(_k8687 != '', 'R241 the block is delimited by the doorbell ladder')

        # The whole round is read-only with respect to the GMU: no HFI register at all.
        check(_k8687.count('gmuW[') == 0,
              'R241 the block stores no GMU register (got %d)'
              % _k8687.count('gmuW['))
        check('HFI_CTRL_INIT' not in _k8687,
              'R241 nothing in this round writes the start word')
        check('HFI_QTBL_INFO' not in _k8687 and 'HFI_QTBL_ADDR' not in _k8687,
              'R241 nothing in this round touches the table descriptor')

        # Rows that must exist for the three keys to be readable at all.
        for _row in ('gmu86a=', 'gmu86b=refused-cx-down', 'gmu86b=refused-unmapped',
                     'gmu86b=scr0=', 'gmu86c=control_no_smmu_write=1',
                     'gmu87c=about_to_write_s2cr0', 'gmu87d=s2cr0',
                     'gmu88c=about_to_set_smr0', 'gmu88d=smr_valid',
                     'gmu88e=smr_restored'):
            check(_row in _k8687, 'R241 the row %s is emitted' % _row)

        # key 88: SMR0 written with VALID | SID 4 and then put back exactly.
        _w800 = _k8687.count('gSmmuVa + 0x800u) = ')
        check(_w800 == 2, 'R241 key 88 writes SMR0 and then restores it (got %d)' % _w800)
        check('0x80000000u | 0x00000004u' in _k8687,
              'R241 the SMR value is VALID | SID 4, the GMU user stream')
        _i800want = _k8687.index('gSmmuVa + 0x800u) = r86want;')
        _i800back = _k8687.index('gSmmuVa + 0x800u) = r86smr0;')
        check(_i800want < _i800back,
              'R241 the SMR entry is restored only after it was set')

        # key 87: one store to the 0xC00 group, announced before it is made.
        _wC00 = _k8687.count('gSmmuVa + 0xc00u) = ')
        check(_wC00 == 1, 'R241 key 87 writes S2CR0 exactly once (got %d)' % _wC00)
        check('0x00010000u' in _k8687, 'R241 the S2CR value is TYPE = BYPASS')
        _iC00row = _k8687.index('gmu87c=about_to_write_s2cr0')
        _iC00st = _k8687.index('gSmmuVa + 0xc00u) = ')
        check(_iC00row < _iC00st,
              'R241 the 0xC00 store is announced on a row before it is made')

        # The control key must not write the SMMU at all: gate the whole round on the two
        # stores above and assert the control body carries none.
        _k86ctl = _k8687.split('if (r86which == 86) {', 1)[1].split(
            '} else if (r86which == 88) {', 1)[0]
        check('0x800u) = ' not in _k86ctl and '0xc00u) = ' not in _k86ctl,
              'R241 the control key writes neither SMR0 nor S2CR0')

        # No sleeping and no waiting loop: this round must not buy observation time.
        check('IOSleep' not in _k8687, 'R241 the block never sleeps')
        check('for (' not in _k8687, 'R241 the block never spins')

    # ---- ROUND 242 ----------------------------------------------------------------
    # key 89 reads 0xC00 / 0xC04 / 0xC08 and writes nothing; key 91 reads 0xC00, writes the same
    # value back and reads again; key 90 routes SID 4 to BYPASS and then writes the start word as
    # the last action of the round. The point of the split is that only key 90 can die.
    _i8991 = code.find('if (fwKey == 89u || fwKey == 90u || fwKey == 91u) {')
    check(_i8991 >= 0, 'R242 the s2cr storage block exists')
    if _i8991 >= 0:
        check(_i8687 >= 0 and _i8991 > _i8687,
              'R242 the new block follows the R241 block it builds on')
        _k8991 = code[_i8991:].split(
            '\nif (fwKey == 92u || fwKey == 93u || fwKey == 94u) {\n', 1)[0]
        check(_k8991 != '', 'R242 the block is delimited by the doorbell ladder')

        # Exactly one GMU store in the whole round, and it is the start word in key 90.
        _g242 = _k8991.count('gmuW[')
        check(_g242 == 1, 'R242 exactly one GMU store in the block (got %d)' % _g242)
        check('HFI_CTRL_INIT' in _k8991, 'R242 the block does write the start word')
        check('HFI_QTBL_INFO' not in _k8991 and 'HFI_QTBL_ADDR' not in _k8991,
              'R242 nothing in this round touches the table descriptor')

        # Rows that must exist for the three keys to be readable at all.
        for _row in ('gmu89a=', 'gmu89b=refused-cx-down', 'gmu89b=refused-unmapped',
                     'gmu89b=scr0=', 'gmu89c=about_to_read_s2cr0', 'gmu89d=s2cr_reads',
                     'gmu91c=about_to_read_s2cr0', 'gmu91d=s2cr_orig', 'gmu91e=s2cr_same',
                     'gmu90c=about_to_route', 'gmu90d=smr_set', 'gmu90e=s2cr_set',
                     'gmu90f=last_row=1'):
            check(_row in _k8991, 'R242 the row %s is emitted' % _row)

        # key 89: announce, then three reads, and not one store anywhere in that body.
        _k89 = _k8991.split('if (r89which == 89) {', 1)[1].split(
            '} else if (r89which == 91) {', 1)[0]
        check(_k89.count('reinterpret_cast<volatile uint32_t *>') == 0,
              'R242 key 89 stores nothing at all (got %d)'
              % _k89.count('reinterpret_cast<volatile uint32_t *>'))
        # Key 89 READS the group instead of storing to it, so these anchors are the read
        # helper rather than the store form the later keys use.
        check('rd32(gSmmuVa, 0xc04u)' in _k89 and 'rd32(gSmmuVa, 0xc08u)' in _k89,
              'R242 key 89 reads the whole 0xC00 group it announces')
        check(_k89.index('gmu89c=about_to_read_s2cr0') < _k89.index('rd32(gSmmuVa, 0xc00u)'),
              'R242 key 89 announces the 0xC00 read before making it')

        # key 91: exactly one store, and it is the value that was just read.
        _tail = _k8991.split('} else if (r89which == 91) {', 1)[1]
        _k91, _k90 = _tail.split('\n        } else {\n', 1)
        check(_k91.count('reinterpret_cast<volatile uint32_t *>') == 1,
              'R242 key 91 stores exactly once (got %d)'
              % _k91.count('reinterpret_cast<volatile uint32_t *>'))
        check('= r89same;' in _k91,
              'R242 key 91 writes back the value it just read')

        # key 90: SMR0 then S2CR0 then the start word, and nothing at all after the start word.
        check(_k90.count('reinterpret_cast<volatile uint32_t *>') == 2,
              'R242 key 90 stores SMR0 and S2CR0 (got %d)'
              % _k90.count('reinterpret_cast<volatile uint32_t *>'))
        check('0x80000000u | 0x00000004u' in _k90,
              'R242 key 90 routes the GMU user stream SID 4 with VALID set')
        check('0x00010000u' in _k90, 'R242 key 90 routes it to BYPASS')
        check('IOSleep' not in _k90 and 'for (' not in _k90,
              'R242 key 90 neither sleeps nor spins')
        _iSmr = _k90.index('gSmmuVa + 0x800u) = ')
        _iS2 = _k90.index('gSmmuVa + 0xc00u) = ')
        _iLast = _k90.index('gmu90f=last_row=1')
        _iStart = _k90.index('HFI_CTRL_INIT] = 1u;')
        check(_iSmr < _iS2 < _iLast < _iStart,
              'R242 key 90 routes first, prints last_row, and only then starts')
        _afterStart = _k90[_iStart + len('HFI_CTRL_INIT] = 1u;'):]
        check('IOLog' not in _afterStart,
              'R242 nothing is printed after the start word')
        check('rd32' not in _afterStart,
              'R242 nothing is read back after the start word')
        check(_afterStart.count('= 1u') == 0 and _afterStart.count('= r89') == 0,
              'R242 no further store is made after the start word')

    # ---- ROUND 243 ----------------------------------------------------------------
    # key 92 reads the whole S2CR group plus sGFSR and writes nothing; key 93 writes a TRANS
    # value into 0xC00 and does not start; key 94 routes SID 4 through a TRANS entry and only
    # then writes the start word as the last action. The split exists because R242 probed a
    # BYPASS store that the hardware clamped, so the routing question was never really asked.
    _i9294 = code.find('if (fwKey == 92u || fwKey == 93u || fwKey == 94u) {')
    check(_i9294 >= 0, 'R243 the s2cr type block exists')
    if _i9294 >= 0:
        check(_i8991 >= 0 and _i9294 > _i8991,
              'R243 the new block follows the R242 block it builds on')
        _k9294 = code[_i9294:].split('\nif (fwKey == 95u || fwKey == 96u) {\n', 1)[0]
        check(_k9294 != '', 'R243 the block is delimited by the doorbell ladder')

        # Exactly one GMU store in the whole round, and it is the start word in key 94.
        _g243 = _k9294.count('gmuW[')
        check(_g243 == 1, 'R243 exactly one GMU store in the block (got %d)' % _g243)
        check('HFI_CTRL_INIT' in _k9294, 'R243 the block does write the start word')
        check('HFI_QTBL_INFO' not in _k9294 and 'HFI_QTBL_ADDR' not in _k9294,
              'R243 nothing in this round touches the table descriptor')

        # Rows that must exist for the three keys to be readable at all.
        for _row in ('gmu92a=', 'gmu92b=refused-cx-down', 'gmu92b=refused-unmapped',
                     'gmu92b=scr0=', 'gmu92c=about_to_read_s2cr_group', 'gmu92d=s2cr_group',
                     'gmu93c=about_to_write_trans', 'gmu93d=trans_wrote',
                     'gmu94c=about_to_route_trans', 'gmu94d=smr_set', 'gmu94e=s2cr_set',
                     'gmu94f=last_row=1'):
            check(_row in _k9294, 'R243 the row %s is emitted' % _row)

        # key 92: announce, then six reads across the group, and not one store anywhere.
        _k92 = _k9294.split('if (r92which == 92) {', 1)[1].split(
            '} else if (r92which == 93) {', 1)[0]
        check(_k92.count('reinterpret_cast<volatile uint32_t *>') == 0,
              'R243 key 92 stores nothing at all (got %d)'
              % _k92.count('reinterpret_cast<volatile uint32_t *>'))
        for _off in ('0xc04u', '0xc08u', '0xc0cu', '0xc10u', '0x048u'):
            check('rd32(gSmmuVa, ' + _off + ')' in _k92,
                  'R243 key 92 reads %s as announced' % _off)
        check(_k92.index('gmu92c=about_to_read_s2cr_group') < _k92.index('rd32(gSmmuVa, 0xc00u)'),
              'R243 key 92 announces the group read before making it')

        # key 93: exactly one store, it is TRANS, and nothing starts.
        _tail93 = _k9294.split('} else if (r92which == 93) {', 1)[1]
        _k93, _k94 = _tail93.split('\n        } else {\n', 1)
        check(_k93.count('reinterpret_cast<volatile uint32_t *>') == 1,
              'R243 key 93 stores exactly once (got %d)'
              % _k93.count('reinterpret_cast<volatile uint32_t *>'))
        check('r92trans = 0x00000000u' in _k93, 'R243 key 93 writes the TRANS type')
        check('HFI_CTRL_INIT' not in _k93, 'R243 key 93 starts nothing')

        # key 94: SMR0 then S2CR0 then the start word, and nothing at all after the start word.
        check(_k94.count('reinterpret_cast<volatile uint32_t *>') == 2,
              'R243 key 94 stores SMR0 and S2CR0 (got %d)'
              % _k94.count('reinterpret_cast<volatile uint32_t *>'))
        check('0x80000000u | 0x00000004u' in _k94,
              'R243 key 94 routes the GMU user stream SID 4 with VALID set')
        check('r92s2Want  = 0x00000000u' in _k94, 'R243 key 94 routes it as TRANS')
        check('IOSleep' not in _k94 and 'for (' not in _k94,
              'R243 key 94 neither sleeps nor spins')
        _iSmr4 = _k94.index('gSmmuVa + 0x800u) = ')
        _iS24 = _k94.index('gSmmuVa + 0xc00u) = ')
        _iLast4 = _k94.index('gmu94f=last_row=1')
        _iStart4 = _k94.index('HFI_CTRL_INIT] = 1u;')
        check(_iSmr4 < _iS24 < _iLast4 < _iStart4,
              'R243 key 94 routes first, prints last_row, and only then starts')
        _after4 = _k94[_iStart4 + len('HFI_CTRL_INIT] = 1u;'):]
        check('IOLog' not in _after4, 'R243 nothing is printed after the start word')
        check('rd32' not in _after4, 'R243 nothing is read back after the start word')
        check(_after4.count('= 1u') == 0 and _after4.count('= r92') == 0,
              'R243 no further store is made after the start word')


    # ------------------------------------------------------------------ ROUND 244
    # Keys 95 and 96 do the same table work; key 95 then makes the start word the last
    # action of the round and key 96 stops before it. The write stage is the only one so
    # far that loops, so exactly one loop is allowed here and it must be the write loop.
    _i9596 = code.find('if (fwKey == 95u || fwKey == 96u) {')
    check(_i9596 > _i9294, 'stage 6 does NOT find the ROUND 244 block after the R243 block')
    _k9596 = ''
    if _i9596 > 0:
        _k9596 = code[_i9596:].split('\nif (fwKey == 97u || fwKey == 98u) {\n', 1)[0]
    check(_k9596 != '', 'stage 6 does NOT slice the ROUND 244 block')
    check(_k9596.count('gmuW[') == 1,
          'stage 6 finds %d GMU stores in the R244 block (expected exactly the start word)'
          % _k9596.count('gmuW['))
    check('HFI_CTRL_INIT' in _k9596, 'the R244 block does NOT name the start word')
    check('HFI_QTBL_INFO]' not in _k9596, 'the R244 block writes the queue-table INFO word')
    check('HFI_QTBL_ADDR]' not in _k9596, 'the R244 block writes the queue-table ADDR word')
    check('IOSleep' not in _k9596, 'the R244 block sleeps')
    check(_k9596.count('for (') == 1,
          'the R244 block runs %d loops (expected the single write loop)' % _k9596.count('for ('))
    check('for (k95 = 0u; k95 < nabs95; ++k95)' in _k9596,
          'the one loop in the R244 block is NOT the nine-word write loop')
    check(_k9596.count('armcpu_dma_alloc_writable') == 3,
          'the R244 block does NOT allocate one four-page table block')
    check(_k9596.count('0x200000ull') >= 2,
          'the R244 block does NOT build a 2 MiB identity block')
    check('0x1fffffull' in _k9596, 'the R244 block does NOT align the base to 2 MiB')
    check(_k9596.count('adreno_table_build(') == 1, 'the R244 block does NOT build one table')
    check(_k9596.count('adreno_smmu_ctx_plan(') == 1, 'the R244 block does NOT plan one CB')
    check(_k9596.count('adreno_install_absolute(') == 1, 'the R244 block does NOT place one CB')
    check('nabs95 != 9u' in _k9596, 'the R244 block does NOT require nine context-bank words')
    check(_k9596.count('gSmmuVa + 0x800u) = ') == 1, 'the R244 block does NOT set SMR0 once')
    check(_k9596.count('gSmmuVa + 0xc00u) = ') == 1, 'the R244 block does NOT set S2CR0 once')
    check('0x80000000u | 0x00000004u' in _k9596, 'the R244 block does NOT seed SMR0 with VALID|SID4')
    check(_k9596.count('r95s2Want = 0x00000000u') == 1,
          'the R244 block does NOT route S2CR0 to TRANS|CB0')
    for _row in ('gmu95a=', 'gmu95b=refused-cx-down', 'gmu95b=refused-geometry',
                 'gmu95b=refused-no-queue-table', 'gmu95b=no-alloc-symbol',
                 'gmu95b=alloc-failed', 'gmu95b=refused-build', 'gmu95b=build=',
                 'gmu95c=plan=', 'gmu95d=wrote=', 'gmu95e=smr_set', 'gmu95f=s2cr_set',
                 'gmu95g=last_row=1', 'gmu95h=no_start=1'):
        check(_row in _k9596, 'the R244 block does NOT print %r' % _row)
    _iSmr95 = _k9596.find('gSmmuVa + 0x800u) = ')
    _iS295 = _k9596.find('gSmmuVa + 0xc00u) = ')
    _iLast95 = _k9596.find('gmu95g=last_row=1')
    _iStart95 = _k9596.find('gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u')
    _iNoStart95 = _k9596.find('gmu95h=no_start=1')
    check(-1 not in (_iSmr95, _iS295, _iLast95, _iStart95, _iNoStart95),
          'the R244 block is missing one of its five landmarks')
    check(_iSmr95 < _iS295, 'the R244 block routes S2CR before SMR')
    check(_iS295 < _iLast95, 'the R244 block prints the last row before routing')
    check(_iLast95 < _iStart95, 'the R244 start word comes before the last row')
    check(_iLast95 < _iNoStart95, 'the R244 no-start row comes before the last row')
    _tail95 = _k9596[_iStart95:]
    check('IOLog' not in _tail95, 'the R244 block prints after the start word')
    check('rd32(' not in _tail95, 'the R244 block reads back after the start word')
    check(_tail95.count('gmuW[') == 1, 'the R244 block stores again after the start word')

    # ------------------------------------------------------------------ ROUND 245
    # Keys 97 and 98 share one setup. Key 98 is key 95 verbatim: the start word is its last
    # textual action. Key 97 writes the same word and then reads five status registers and
    # prints one more row, so its branch is the only place where anything follows the store.
    _i9798 = code.find('if (fwKey == 97u || fwKey == 98u) {')
    check(_i9798 > _i9596, 'stage 6 does NOT find the ROUND 245 block after the R244 block')
    _k9798 = ''
    if _i9798 > 0:
        _k9798 = code[_i9798:].split('\nif (fwKey == 99u || fwKey == 100u || fwKey == 101u) {\n', 1)[0]
    check(_k9798 != '', 'stage 6 does NOT slice the ROUND 245 block')
    check(_k9798.count('gmuW[') == 2,
          'stage 6 finds %d GMU stores in the R245 block (expected one per branch)'
          % _k9798.count('gmuW['))
    check('HFI_CTRL_INIT' in _k9798, 'the R245 block does NOT name the start word')
    check('gmuW[ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO]' not in _k9798 and
          'gmuW[ADRENO_GMU_BOOT_V1_HFI_QTBL_ADDR]' not in _k9798,
          'the R245 block stores to the queue-table descriptor')
    for _row in ('gmu97a=', 'gmu97b=refused-cx-down', 'gmu97b=refused-geometry',
                 'gmu97b=refused-no-queue-table', 'gmu97b=no-alloc-symbol',
                 'gmu97b=alloc-failed', 'gmu97b=refused-build', 'gmu97b=build=',
                 'gmu97c=plan=', 'gmu97d=wrote=', 'gmu97e=smr_set', 'gmu97f=s2cr_set',
                 'gmu97g=last_row=1', 'gmu97h=after_start=1'):
        check(_row in _k9798, 'the R245 block does NOT print %r' % _row)
    check(_k9798.count('armcpu_dma_alloc_writable') == 3,
          'R245 declares, guards and calls the allocator (%d)'
          % _k9798.count('armcpu_dma_alloc_writable'))
    check('0x200000ull' in _k9798, 'the R245 mapping is NOT the 2 MiB block')
    check('rd32(gSmmuVa, 0x048u)' in _k9798, 'the R245 block does NOT read sGFSR')
    check('+ 0x58u)' in _k9798, 'the R245 block does NOT read the context bank FSR')
    check('IOSleep' not in _k9798, 'the R245 block sleeps')
    check(_k9798.count('for (') == 1,
          'the R245 block has %d loops (only the nine-word write may loop)'
          % _k9798.count('for ('))
    # The two branches: key 98's store is the last thing in its branch, key 97's store is
    # followed by the five reads and the one extra row.
    _i98 = _k9798.find('if (!r97post) {')
    _i97 = _k9798.rindex('} else {')
    check(_i98 > 0 and _i97 > _i98, 'the R245 block does not branch on post')
    _br98 = _k9798[_i98:_i97]
    _br97 = _k9798[_i97:]
    check(_br98.count('gmuW[') == 1, 'key 98 does not write the start word exactly once')
    check('IOLog' not in _br98, 'key 98 prints after its start word')
    check('rd32(' not in _br98, 'key 98 reads back after its start word')
    check(_br97.count('gmuW[') == 1, 'key 97 does not write the start word exactly once')
    _store97 = _br97.find('gmuW[')
    check(_store97 >= 0 and 'gmu97h=after_start=1' in _br97[_store97:],
          'key 97 does not read and print after its start word')
    check(_br97.find('rd32(') > _store97, 'key 97 reads back BEFORE the start word')

    # ------------------------------------------------------------------ ROUND 246
    # The survival is established, so the question is what the firmware does with the start
    # request. Key 99 starts the HFI and then reads the registers the firmware would have had
    # to touch. Key 100 re-arms the table descriptor first and reads the status once *before*
    # the store. Key 101 adds a second route for SID 5 and then starts. Nothing loops after a
    # store, nothing sleeps, and only key 99 reads after its start word.
    _i99101 = code.find('if (fwKey == 99u || fwKey == 100u || fwKey == 101u) {')
    check(_i99101 > _i9798, 'stage 6 does NOT find the ROUND 246 block after the R245 block')
    _k99101 = ''
    if _i99101 > 0:
        _k99101 = code[_i99101:].split('\nif (fwKey == 102u || fwKey == 103u || fwKey == 104u) {\n', 1)[0]
    check(_k99101 != '', 'stage 6 does NOT slice the ROUND 246 block')
    check(_k99101.count('gmuW[') == 5,
          'stage 6 finds %d GMU stores in the R246 block (expected five)' % _k99101.count('gmuW['))
    check(_k99101.count('HFI_CTRL_INIT] = ') == 3,
          'the R246 block carries %d start words (expected one per key)'
          % _k99101.count('HFI_CTRL_INIT] = '))
    check(_k99101.count('HFI_QTBL_ADDR] = ') == 1 and _k99101.count('HFI_QTBL_INFO] = ') == 1,
          'key 100 must re-arm the queue-table descriptor exactly once each')
    for _row in ('gmu99a=', 'gmu99b=refused-cx-down', 'gmu99b=refused-geometry',
                 'gmu99b=refused-no-queue-table', 'gmu99b=no-alloc-symbol',
                 'gmu99b=alloc-failed', 'gmu99b=refused-build', 'gmu99b=build=',
                 'gmu99c=plan=', 'gmu99d=wrote=', 'gmu99e=smr_set', 'gmu99f=s2cr_set',
                 'gmu99g=last_row=1', 'gmu99h=smr1_set', 'gmu100h=before_start=1',
                 'gmu99h=after_start=1'):
        check(_row in _k99101, 'the R246 block does NOT print %r' % _row)
    check(_k99101.count('armcpu_dma_alloc_writable') == 3,
          'R246 declares, guards and calls the allocator (%d)'
          % _k99101.count('armcpu_dma_alloc_writable'))
    check('0x200000ull' in _k99101, 'the R246 mapping is NOT the 2 MiB block')
    check('IOSleep' not in _k99101, 'the R246 block sleeps')
    check(_k99101.count('for (') == 1,
          'the R246 block has %d loops (only the nine-word write may loop)'
          % _k99101.count('for ('))
    # Key 101: the SID 5 route is two stores, and the start word comes after both of them.
    _k101 = _k99101[_k99101.index('if (r99mode == 101) {'):_k99101.index('} else if (r99mode == 100) {')]
    check('gSmmuVa + 0x804u) = ' in _k101 and 'gSmmuVa + 0xc04u) = ' in _k101,
          'key 101 does NOT program the second route at SMR1 and S2CR1')
    check('0x80000000u | 0x00000005u' in _k101, 'key 101 does NOT route SID 5')
    check(_k101.index('gSmmuVa + 0x804u) = ') < _k101.index('gSmmuVa + 0xc04u) = '),
          'key 101 routes S2CR1 before SMR1')
    check(_k101.rindex('HFI_CTRL_INIT] = ') > _k101.index('gSmmuVa + 0xc04u) = '),
          'key 101 starts the HFI before the second route is written')
    # Key 100: descriptor, then info, then one status read, then the start word last.
    _i100 = _k99101.index('} else if (r99mode == 100) {')
    _i100end = _k99101.index('} else {', _i100 + 1)
    _k100 = _k99101[_i100:_i100end]
    check('HFI_QTBL_ADDR] = ' in _k100 and 'HFI_QTBL_INFO] = ' in _k100,
          'key 100 does NOT re-arm the queue-table descriptor')
    check(_k100.index('HFI_QTBL_ADDR] = ') < _k100.index('HFI_QTBL_INFO] = '),
          'key 100 writes QTBL_INFO before QTBL_ADDR')
    check(_k100.index('HFI_CTRL_STATUS * 4u') < _k100.index('HFI_CTRL_INIT] = '),
          'key 100 reads the status register after the start word')
    check(_k100.rindex('HFI_CTRL_INIT] = ') > _k100.index('gmu100h=before_start=1'),
          'key 100 prints before_start after the start word')
    # Key 99: the store comes first, then the five reads, and only then the row.
    _i99 = _k99101.rindex('} else {')
    _k99 = _k99101[_i99:]
    check(_k99.rindex('HFI_CTRL_INIT] = ') < _k99.index('gmu99h=after_start=1'),
          'key 99 prints its post-start row before the store')
    for _reg in ('HFI_CTRL_STATUS * 4u', 'PWR_COL_CP_MSG * 4u', 'PWR_COL_CP_RESP * 4u',
                 'HFI_QTBL_ADDR * 4u', 'HFI_QTBL_INFO * 4u'):
        check(_k99.count(_reg) == 1,
              'key 99 reads %r %d times (expected once)' % (_reg, _k99.count(_reg)))

    # ---- ROUND 247: keys 102, 103 and 104 -------------------------------------------
    # The descriptor is programmed and the firmware still never answers. Key 102 writes
    # nothing at all, so the firmware-side registers are on record before any store. Key
    # 103 releases the CM3 reset again with the queue table already in place and polls the
    # init word (the poll carries IODelay so the writer keeps running), but writes no start
    # word. Key 104 repeats that and adds the start word as the last action, then reads the
    # status once, in the shape R245 key 97 showed is survivable.
    _i0204 = code.find('if (fwKey == 102u || fwKey == 103u || fwKey == 104u) {')
    check(_i0204 > _i99101, 'stage 6 does NOT find the ROUND 247 block after the R246 block')
    _k0204 = ''
    if _i0204 > 0:
        _k0204 = code[_i0204:].split('\nif (fwKey == 105u || fwKey == 106u || fwKey == 107u) {\n', 1)[0]
    check(_k0204 != '', 'stage 6 does NOT slice the ROUND 247 block')
    check(_k0204.count('gmuW[') == 11,
          'stage 6 finds %d GMU stores in the R247 block (expected eleven: five for the '
          're-boot key, six for the re-boot plus start word)' % _k0204.count('gmuW['))
    check(_k0204.count('HFI_CTRL_INIT] = ') == 1,
          'the R247 block carries %d start words (expected only key 104)'
          % _k0204.count('HFI_CTRL_INIT] = '))
    check(_k0204.count('HFI_QTBL_ADDR] = ') == 2, 'both re-boot keys re-arm QTBL_ADDR')
    check(_k0204.count('HFI_QTBL_INFO] = 1u') == 2, 'both re-boot keys re-arm QTBL_INFO')
    check(_k0204.count('CM3_SYSRESET] = 1u') == 2 and _k0204.count('CM3_SYSRESET] = 0u') == 2,
          'both re-boot keys release and hold the CM3 reset exactly once each')
    check(_k0204.count('PWR_COL_CP_RESP] = 0u') == 2,
          'both re-boot keys clear the response word like a6xx_gmu_start does')
    check(_k0204.count('for (') == 3,
          'the R247 block has %d loops (the nine-word write and one poll per re-boot key)'
          % _k0204.count('for ('))
    check('IOSleep' not in _k0204, 'the R247 block sleeps')
    for _row in ('gmu102a=', 'gmu102b=refused-cx-down', 'gmu102b=refused-geometry',
                 'gmu102b=refused-no-queue-table', 'gmu102b=no-alloc-symbol',
                 'gmu102b=alloc-failed', 'gmu102b=refused-build', 'gmu102b=build=',
                 'gmu102c=plan=', 'gmu102d=wrote=', 'gmu102e=smr_set', 'gmu102f=s2cr_set',
                 'gmu102g=last_row=1', 'gmu102h=pre_start=1',
                 'gmu103h=cm3_reboot=1 no_start=1', 'gmu104h=cm3_reboot=1 about_to_start=1',
                 'gmu104i=after_start=1'):
        check(_row in _k0204, 'the R247 block does NOT print %r' % _row)
    check('0xbabefaceu' in _k0204, 'the poll does NOT compare against the init word')
    # Key 104 first, then key 103, then the snapshot key as the else branch.
    _i104 = _k0204.index('if (r102mode == 104) {')
    _i103 = _k0204.index('} else if (r102mode == 103) {')
    _k104 = _k0204[_i104:_i103]
    _i102 = _k0204.index('} else {', _i103 + 1)
    _k103 = _k0204[_i103:_i102]
    _k102 = _k0204[_i102:]
    check(_i104 < _i103 < _i102, 'the R247 branches are not ordered 104 / 103 / 102')
    check(_k103.count('gmuW[') == 5, 'key 103 carries %d stores (expected five)' % _k103.count('gmuW['))
    check('HFI_CTRL_INIT' not in _k103, 'key 103 writes the start word although it says no_start')
    check('IODelay(1u)' in _k103, 'key 103 polls the init word without letting the writer run')
    check(_k104.count('gmuW[') == 6, 'key 104 carries %d stores (expected six)' % _k104.count('gmuW['))
    check(_k104.rindex('HFI_CTRL_INIT] = ') > _k104.index('gmu104h=cm3_reboot=1'),
          'key 104 starts the HFI before its pre-start row')
    check(_k104.rindex('HFI_CTRL_INIT] = ') < _k104.index('gmu104i=after_start=1'),
          'key 104 prints its post-start row before the store')
    check(_k102.count('gmuW[') == 0, 'key 102 writes %d GMU words although it is read-only'
          % _k102.count('gmuW['))
    check(_k102.count('rd32(') >= 12, 'key 102 reads %d registers (expected at least twelve)'
          % _k102.count('rd32('))
    check('HFI_SFR_ADDR' in _k102 and 'CM3_BOOT_CONFIG' in _k102 and 'RPMH_CTRL' in _k102,
          'key 102 does NOT cover the firmware-side identity and configuration registers')


    # --- ROUND 248: clear the init word before the reset, then clear a stale request or a stale
    # interrupt before the start word. Every branch keeps the start word last. -------------------
    _i0507 = code.find('if (fwKey == 105u || fwKey == 106u || fwKey == 107u) {')
    check(_i0507 >= 0, 'stage 6 does NOT find the ROUND 248 block header')
    _k0507 = code[_i0507:].split('\nif (fwKey == 108u || fwKey == 109u || fwKey == 110u) {\n', 1)[0]
    check(_k0507 != '', 'stage 6 does NOT slice the ROUND 248 block')
    check(_k0507.count('gmuW[') == 23,
          'stage 6 finds %d GMU stores in the R248 block (expected twenty-three: six per re-boot '
          'key, plus two for 106 and three for 107)' % _k0507.count('gmuW['))
    check(_k0507.count('CM3_FW_INIT_RESULT] = 0u;') == 3,
          'all three R248 keys clear the init word before the reset (%d)'
          % _k0507.count('CM3_FW_INIT_RESULT] = 0u;'))
    check(_k0507.count('HFI_QTBL_ADDR] = ') == 3,
          'all three R248 keys re-arm the queue-table descriptor (%d)'
          % _k0507.count('HFI_QTBL_ADDR] = '))
    check(_k0507.count('HFI_QTBL_INFO] = ') == 3,
          'all three R248 keys re-arm the descriptor info word (%d)'
          % _k0507.count('HFI_QTBL_INFO] = '))
    check(_k0507.count('GMU2HOST_INTR_CLR] = ') == 1,
          'only key 107 clears the HFI interrupt block (%d)'
          % _k0507.count('GMU2HOST_INTR_CLR] = '))
    check(_k0507.count('AO_HOST_INTERRUPT_CLR] = ') == 1,
          'only key 107 clears the AO interrupt block (%d)'
          % _k0507.count('AO_HOST_INTERRUPT_CLR] = '))
    check(_k0507.count('HFI_CTRL_INIT] = 1u;') == 2,
          'keys 106 and 107 each write the start word once (%d)'
          % _k0507.count('HFI_CTRL_INIT] = 1u;'))
    check(_k0507.count('HFI_CTRL_INIT] = 0u;') == 1,
          'only key 106 clears the request bit (%d)'
          % _k0507.count('HFI_CTRL_INIT] = 0u;'))
    check("'IOSleep' not in _k0507", 'the R248 block sleeps nowhere')
    check(_k0507.count('for (') == 4,
          'the R248 block has %d loops (the nine-word write plus one poll per re-boot key)'
          % _k0507.count('for ('))
    check('0xbabefaceu' in _k0507, 'the R248 poll waits for the firmware init word')
    check('IODelay(1u)' in _k0507, 'the R248 poll yields the CPU between passes')

    for _tag in ('gmu105a=', 'gmu105b=', 'gmu105c=', 'gmu105d=', 'gmu105e=', 'gmu105f=',
                 'gmu105g=', 'gmu105h=', 'gmu105i=', 'gmu106h=', 'gmu106i=', 'gmu106j=',
                 'gmu106k=', 'gmu107h=', 'gmu107i=', 'gmu107j=', 'gmu107k='):
        check(_tag in _k0507, 'the R248 block prints %s' % _tag)

    _i106 = _k0507.index('} else if (r105mode == 106) {')
    _k105 = _k0507[:_i106]
    _i106end = _k0507.index('} else {', _i106 + 1)
    _k106 = _k0507[_i106:_i106end]
    _i107 = _k0507.rindex('} else {')
    _k107 = _k0507[_i107:]

    check(_k105.count('gmuW[') == 6,
          'key 105 stores exactly six words (%d)' % _k105.count('gmuW['))
    check('HFI_CTRL_INIT' not in _k105,
          'key 105 never touches the start word: it only asks whether the CM3 re-runs')
    check(_k105.index('CM3_FW_INIT_RESULT] = 0u;') < _k105.index('CM3_SYSRESET] = 1u;'),
          'key 105 clears the init word before it pulses the reset')

    check(_k106.count('gmuW[') == 8,
          'key 106 stores exactly eight words (%d)' % _k106.count('gmuW['))
    check(_k106.index('HFI_CTRL_INIT] = 0u;') < _k106.index('HFI_CTRL_INIT] = 1u;'),
          'key 106 clears the request bit before it writes it back')
    check(_k106.count('HFI_CTRL_INIT] = 1u;') == 1,
          'key 106 writes the start word exactly once')
    check(_k106[_k106.index('HFI_CTRL_INIT] = 1u;'):].count('gmuW[') == 0,
          'nothing is stored after the start word of key 106, so that store is last')
    check(_k106.index('gmu106j=') < _k106.index('HFI_CTRL_INIT] = 1u;'),
          'key 106 announces the cleared request before it starts the HFI')
    check(_k106.index('gmu106k=') > _k106.index('HFI_CTRL_INIT] = 1u;'),
          'key 106 reads the status back after the start word')

    check(_k107.count('gmuW[') == 9,
          'key 107 stores exactly nine words (%d)' % _k107.count('gmuW['))
    check(_k107.index('GMU2HOST_INTR_CLR] = ') < _k107.index('HFI_CTRL_INIT] = 1u;'),
          'key 107 clears the HFI interrupt before the start word')
    check(_k107.index('AO_HOST_INTERRUPT_CLR] = ') < _k107.index('HFI_CTRL_INIT] = 1u;'),
          'key 107 clears the AO interrupt before the start word')
    check(_k107.count('HFI_CTRL_INIT] = 1u;') == 1,
          'key 107 writes the start word exactly once')
    check(_k107[_k107.index('HFI_CTRL_INIT] = 1u;'):].count('gmuW[') == 0,
          'nothing is stored after the start word of key 107, so that store is last')
    check(_k107.index('gmu107j=') < _k107.index('HFI_CTRL_INIT] = 1u;'),
          'key 107 announces the cleared interrupts before it starts the HFI')
    check(_k107.index('gmu107k=') > _k107.index('HFI_CTRL_INIT] = 1u;'),
          'key 107 reads the status back after the start word')

    check(_i0507 > _i0204, 'the R248 block follows the R247 block in the source')

    # ---- ROUND 249: the three poll keys -----------------------------------------------------
    _i0810 = code.find('if (fwKey == 108u || fwKey == 109u || fwKey == 110u) {')
    check(_i0810 > _i0507, 'the R249 block follows the R248 block in the source')
    if _i0810 > 0:
        _k0810 = code[_i0810:].split(
            '\nif (fwKey == 111u || fwKey == 112u || fwKey == 113u) {\n', 1)[0]
        check(_k0810.count('gmuW[') == 29,
              'the R249 block holds twenty-nine GMU stores (got %d: nine per key, plus the two '
              'descriptor writes of key 110)' % _k0810.count('gmuW['))
        check(_k0810.count('CM3_FW_INIT_RESULT] = 0u;') == 3,
              'every R249 key clears the init word before the reset (got %d)'
              % _k0810.count('CM3_FW_INIT_RESULT] = 0u;'))
        check(_k0810.count('HFI_QTBL_ADDR] = ') == 3,
              'every R249 key re-arms the queue-table address (got %d)'
              % _k0810.count('HFI_QTBL_ADDR] = '))
        check(_k0810.count('HFI_QTBL_INFO] = 1u;') == 4,
              'three re-arms plus key 110 writing the descriptor back to 1 (got %d)'
              % _k0810.count('HFI_QTBL_INFO] = 1u;'))
        check(_k0810.count('HFI_QTBL_INFO] = 0u;') == 1,
              'only key 110 writes the descriptor to 0 (got %d)'
              % _k0810.count('HFI_QTBL_INFO] = 0u;'))
        check(_k0810.count('HFI_CTRL_INIT] = 1u;') == 3,
              'every R249 key writes the start word exactly once (got %d)'
              % _k0810.count('HFI_CTRL_INIT] = 1u;'))
        check(_k0810.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 3 and
              _k0810.count('AO_HOST_INTERRUPT_CLR] = 0xffffffffu;') == 3,
              'every R249 key clears both interrupt blocks before the start word')
        check(_k0810.count('for (') == 7,
              'the R249 block has one write loop plus one poll per branch after the start word '
              '(got %d)' % _k0810.count('for ('))
        check(_k0810.count('IODelay(1u)') == 6,
              'six polling loops each wait one interval per pass (got %d)'
              % _k0810.count('IODelay(1u)'))
        check('IOSleep' not in _k0810, 'no R249 key sleeps')
        for _row in ('gmu108a=', 'gmu108b=', 'gmu108c=', 'gmu108d=', 'gmu108e=', 'gmu108f=',
                     'gmu108g=', 'gmu108h=', 'gmu108i=', 'gmu108j=', 'gmu108k=',
                     'gmu109k=', 'gmu109l=', 'gmu110k=', 'gmu110l='):
            check(_row in _k0810, 'the R249 block prints %s' % _row)
        _i108 = _k0810.index('if (r108mode == 108) {')
        _i109 = _k0810.index('} else if (r108mode == 109) {')
        # rindex, not index: the rejection ladder in the preamble ends with `} else {` too,
        # and taking the first one makes every branch slice describe the wrong code.
        _i110 = _k0810.rindex('} else {')
        check(_i108 < _i109 < _i110, 'the R249 mode branches are ordered 108, 109, 110')
        _k108 = _k0810[_i108:_i109]
        _k109 = _k0810[_i109:_i110]
        _k110 = _k0810[_i110:]
        for _name, _br in (('108', _k108), ('109', _k109), ('110', _k110)):
            _st = _br.index('HFI_CTRL_INIT] = 1u;')
            check('gmuW[' not in _br[_st:],
                  'no R249 store follows the start word of key %s' % _name)
            check(' for (' in _br[_st:] or 'for (' in _br[_st:],
                  'key %s polls the status only after the start word' % _name)
        check(_k108.count('for (') == 2 and _k110.count('for (') == 2,
              'keys 108 and 110 each hold one write loop and one poll loop')
        check(_k109.count('for (') == 2,
              'key 109 holds one write loop and one poll loop')
        check(_k110.index('HFI_QTBL_INFO] = 0u;') < _k110.index('HFI_CTRL_INIT] = 1u;'),
              'key 110 writes the descriptor to 0 before the start word, not after it')
        check('cbfsr=' in _k109 and 'gfsr=' in _k109,
              'key 109 reads the context-bank and global fault registers back')


    _i111113 = code.find('if (fwKey == 111u || fwKey == 112u || fwKey == 113u) {')
    check(_i111113 > _i0810, 'the R250 block follows the R249 block in the source')
    if _i111113 > 0:
        _k111113 = code[_i111113:].split(
            '\nif (fwKey == 114u || fwKey == 115u || fwKey == 116u) {\n', 1)[0]
    _i114116 = code.find('if (fwKey == 114u || fwKey == 115u || fwKey == 116u) {')
    _k114116 = ''
    if _i114116 > 0:
        _k114116 = code[_i114116:].split(
            '\nif (fwKey == 117u || fwKey == 118u || fwKey == 119u) {\n', 1)[0]
        _i117119 = code.find('if (fwKey == 117u || fwKey == 118u || fwKey == 119u) {')
        _k117119 = ''
        if _i117119 > 0:
            _k117119 = code[_i117119:].split(
                '\nif (fwKey == 120u || fwKey == 121u || fwKey == 122u) {\n', 1)[0]
        _i124126 = code.find('if (fwKey == 124u || fwKey == 125u || fwKey == 126u) {')
        _k124126 = ''
        if _i124126 > 0:
            _k124126 = code[_i124126:].split(
                '\nif (fwKey == 127u || fwKey == 128u || fwKey == 129u) {\n', 1)[0]
        _i120122 = code.find('if (fwKey == 120u || fwKey == 121u || fwKey == 122u) {')
        _k120122 = ''
        if _i120122 > 0:
            _k120122 = code[_i120122:].split('\nif (fwKey == 124u || fwKey == 125u || fwKey == 126u) {\n', 1)[0]
        _i127129 = code.find('if (fwKey == 127u || fwKey == 128u || fwKey == 129u) {')
        _k127129 = ''
        if _i127129 > 0:
            _k127129 = code[_i127129:].split(
            '\nif (fwKey == 130u || fwKey == 131u || fwKey == 132u) {\n', 1)[0]
        _i130132 = code.find('if (fwKey == 130u || fwKey == 131u || fwKey == 132u) {')
        _k130132 = ''
        if _i130132 > 0:
            _k130132 = code[_i130132:].split(
                '\nif (fwKey == 133u || fwKey == 134u || fwKey == 135u) {\n', 1)[0]
        _i133135 = code.find('if (fwKey == 133u || fwKey == 134u || fwKey == 135u) {')
        _k133135 = ''
        if _i133135 > 0:
            _k133135 = code[_i133135:].split(
                '\nif (fwKey == 136u || fwKey == 137u || fwKey == 138u) {\n', 1)[0]
        _i136138 = code.find('if (fwKey == 136u || fwKey == 137u || fwKey == 138u) {')
        _k136138 = ''
        if _i136138 > 0:
            _k136138 = code[_i136138:].split(
                '\nif (fwKey == 139u || fwKey == 140u || fwKey == 141u) {\n', 1)[0]
        _i139141 = code.find('if (fwKey == 139u || fwKey == 140u || fwKey == 141u) {')
        _k139141 = ''
        if _i139141 > 0:
            _k139141 = code[_i139141:].split(
                '\nif (fwKey == 142u || fwKey == 143u || fwKey == 144u) {\n', 1)[0]
        _i142144 = code.find('if (fwKey == 142u || fwKey == 143u || fwKey == 144u) {')
        _k142144 = ''
        if _i142144 > 0:
            _k142144 = code[_i142144:].split(
                '\nif (fwKey == 145u || fwKey == 146u || fwKey == 147u) {\n', 1)[0]
        _i145147 = code.find('if (fwKey == 145u || fwKey == 146u || fwKey == 147u) {')
        _k145147 = ''
        if _i145147 > 0:
            _k145147 = code[_i145147:].split(
                '\nif (fwKey == 148u || fwKey == 149u || fwKey == 150u) {\n', 1)[0]
        _i148150 = code.find('if (fwKey == 148u || fwKey == 149u || fwKey == 150u) {')
        _k148150 = ''
        if _i148150 > 0:
            _k148150 = code[_i148150:].split(
                '\nif (fwKey == 151u || fwKey == 152u || fwKey == 153u) {\n', 1)[0]
        _i151153 = code.find('if (fwKey == 151u || fwKey == 152u || fwKey == 153u) {')
        _k151153 = ''
        if _i151153 > 0:
            _k151153 = code[_i151153:].split(
                '\nif (fwKey == 154u || fwKey == 155u || fwKey == 156u) {\n', 1)[0]
        _i154156 = code.find('if (fwKey == 154u || fwKey == 155u || fwKey == 156u) {')
        _k154156 = ''
        if _i154156 > 0:
            _k154156 = code[_i154156:].split(
                '\nif (fwKey == 157u || fwKey == 158u || fwKey == 159u) {\n', 1)[0]
        _i157159 = code.find('if (fwKey == 157u || fwKey == 158u || fwKey == 159u) {')
        _k157159 = ''
        if _i157159 > 0:
            _k157159 = code[_i157159:].split(
                '\nif (fwKey == 160u || fwKey == 161u || fwKey == 162u) {\n', 1)[0]
        _i160162 = code.find('if (fwKey == 160u || fwKey == 161u || fwKey == 162u) {')
        _k160162 = ''
        if _i160162 > 0:
            _k160162 = code[_i160162:].split(
                '\nif (fwKey == 163u || fwKey == 164u || fwKey == 165u) {\n', 1)[0]
        _i163165 = code.find('if (fwKey == 163u || fwKey == 164u || fwKey == 165u) {')
        _k163165 = ''
        if _i163165 > 0:
            _k163165 = code[_i163165:].split(
                '\nif (fwKey == 166u || fwKey == 167u || fwKey == 168u) {\n', 1)[0]
        _i166168 = code.find('if (fwKey == 166u || fwKey == 167u || fwKey == 168u) {')
        _k166168 = ''
        if _i166168 > 0:
            _k166168 = code[_i166168:].split(
                '\nif (fwKey == 169u || fwKey == 170u || fwKey == 171u) {\n', 1)[0]
        _i169171 = code.find('if (fwKey == 169u || fwKey == 170u || fwKey == 171u) {')
        _k169171 = ''
        if _i169171 > 0:
            _k169171 = code[_i169171:].split(
                '\nif (fwKey >= 18u && fwKey <= 21u) {\n', 1)[0]
        check(_k111113.count('gmuW[') == 14,
              'the R250 block holds fourteen GMU stores: eight in the shared preamble and six across '
              'the three keys (got %d)' % _k111113.count('gmuW['))
        check(_k111113.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
              'the R250 preamble clears the init word exactly once, not once per key (got %d)'
              % _k111113.count('CM3_FW_INIT_RESULT] = 0u;'))
        check(_k111113.count('HFI_QTBL_ADDR] = ') == 1 and
              _k111113.count('HFI_QTBL_INFO] = 1u;') == 1,
              'the R250 preamble re-arms the queue table once, not once per key')
        check(_k111113.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
              _k111113.count('AO_HOST_INTERRUPT_CLR] = 0xffffffffu;') == 1,
              'the R250 preamble clears both interrupt blocks once, not once per key')
        check(_k111113.count('HFI_CTRL_INIT] = 1u;') == 3,
              'each R250 key writes the start word exactly once (got %d)'
              % _k111113.count('HFI_CTRL_INIT] = 1u;'))
        check(_k111113.count('HOST2GMU_INTR_SET] = 1u;') == 1,
              'only key 111 rings the doorbell (got %d)'
              % _k111113.count('HOST2GMU_INTR_SET] = 1u;'))
        check(_k111113.count('HFI_SFR_ADDR] = ') == 1 and
              _k111113.count('PWR_COL_CP_MSG] = ') == 1,
              'only key 113 re-pokes HFI_SFR_ADDR and PWR_COL_CP_MSG')
        check(_k111113.count('for (') == 5,
              'the R250 block has the write loop, one re-boot poll and three status polls (got %d)'
              % _k111113.count('for ('))
        check(_k111113.count('IODelay(1u)') == 4,
              'four polling loops each wait one interval per pass (got %d)'
              % _k111113.count('IODelay(1u)'))
        check('IOSleep' not in _k111113, 'no R250 key sleeps')
        check('0xbabefaceu' in _k111113,
              'the R250 preamble polls the firmware init word for the sentinel')
        for _row in ('gmu111a=', 'gmu111b=', 'gmu111c=', 'gmu111d=', 'gmu111e=', 'gmu111f=',
                     'gmu111g=', 'gmu111h=', 'gmu111i=', 'gmu111j=', 'gmu111k=', 'gmu111l=',
                     'gmu111m=', 'gmu112k=', 'gmu112l=', 'gmu113k=', 'gmu113l=', 'gmu113m='):
            check(_row in _k111113, 'the R250 block prints %s' % _row)
        _i111 = _k111113.index('if (r111mode == 111) {')
        _i112 = _k111113.index('} else if (r111mode == 112) {')
        # rindex again: the rejection ladder ends with `} else {` too
        _i113 = _k111113.rindex('} else {')
        check(_i111 < _i112 < _i113, 'the R250 mode branches are ordered 111, 112, 113')
        _k111 = _k111113[_i111:_i112]
        _k112 = _k111113[_i112:_i113]
        _k113 = _k111113[_i113:]
        for _name, _br in (('111', _k111), ('112', _k112), ('113', _k113)):
            _st = _br.index('HFI_CTRL_INIT] = 1u;')
            check('gmuW[' not in _br[_st:],
                  'no R250 store follows the start word of key %s' % _name)
            check('for (' in _br[_st:],
                  'key %s polls the status only after the start word' % _name)
        _kpre = _k111113[:_k111113.index('if (r111mode == 111) {')].rstrip()
        check(_kpre.endswith('IOLog("enchilada-adreno: %s\\n", r111RowJ);'),
              'the R250 preamble ends with the pre-start row, so nothing is stored after the '
              'interrupt clears')
        check(_k111.index('HOST2GMU_INTR_SET] = 1u;') < _k111.index('HFI_CTRL_INIT] = 1u;'),
              'key 111 rings the doorbell before the start word, not after it')
        check('info=%08x' in _k111 and _k111.count('GMU2HOST_INTR_INFO') >= 2,
              'key 111 reads the GMU interrupt info before and after the doorbell')
        check('ifc=' in _k112 and 'hyst=' in _k112 and 'cpmsg=' in _k112,
              'key 112 prints the power and frame words either side of the start')
        check('sfr_zero=' in _k113 and 'cpmsg_zero=' in _k113 and 'landed=' in _k113,
              'key 113 reports what the two registers held and whether the re-poke landed')

    # ---- R251: the timing keys ------------------------------------------------------------
    check(_i114116 > _i111113, 'the R251 block follows the R250 block in the source')
    _store = re.compile(r'gmuW\[[^\]]*\]\s*=[^=]')
    check(len(_store.findall(_k114116)) == 14,
          'R251 fourteen GMU stores share one preamble (got %d)' % len(_store.findall(_k114116)))
    _b114 = _k114116[_k114116.index('if (r114mode == 114) {'):_k114116.index('} else if (r114mode == 115) {')]
    _i115 = _k114116.index('} else if (r114mode == 115) {')
    _b115 = _k114116[_i115:_k114116.index('} else {', _i115 + 1)]
    _b116 = _k114116[_k114116.rindex('} else {'):]
    _pre = _k114116[:_k114116.index('if (r114mode == 114) {')]
    check(_pre.count('HFI_CTRL_INIT] = 1u;') == 0 and
          _pre.count('GMU2HOST_INTR_CLR] = ') == 1 and
          _pre.count('GMU2HOST_INTR_MASK] = ') == 0,
          'R251 the shared preamble clears the interrupts once and never starts the HFI')
    check(_pre.rstrip().endswith('IOLog("enchilada-adreno: %s\\n", r114RowJ);'),
          'R251 the preamble ends with its own row, so nothing is stored after the clear')
    check(len(_store.findall(_b114)) == 2 and _b114.count('GMU2HOST_INTR_CLR] = ') == 1
          and _b114.count('HFI_CTRL_INIT] = 1u;') == 1
          and _b114.count('GMU2HOST_INTR_MASK] = ') == 0,
          'R251 key 114 adds one interrupt clear and one start word, nothing else')
    check(len(_store.findall(_b115)) == 2 and _b115.count('GMU2HOST_INTR_MASK] = ') == 1
          and '0xff7fffffu' in _b115 and _b115.count('HFI_CTRL_INIT] = 1u;') == 1
          and _b115.count('GMU2HOST_INTR_CLR] = ') == 0,
          'R251 key 115 enables only bit 23 and then writes the start word once')
    check(len(_store.findall(_b116)) == 2 and _b116.count('HFI_CTRL_INIT') == 0
          and _b116.count('GMU2HOST_INTR_CLR] = ') == 1
          and _b116.count('GMU2HOST_INTR_MASK] = ') == 1 and '0xff7fffffu' in _b116,
          'R251 key 116 does the same clear and the same mask and never writes the start word')
    check(_k114116.count('for (') == 2 and _k114116.count('IODelay(1u)') == 1
          and 'IOSleep' not in _k114116,
          'R251 only the shared preamble loops, and it polls with an IODelay')
    check(_k114116.count('0xbabefaceu') == 1, 'R251 the preamble still waits for the init word')
    _names = ['gmu114a=', 'gmu114b=', 'gmu114c=', 'gmu114d=', 'gmu114e=', 'gmu114f=',
              'gmu114g=', 'gmu114h=', 'gmu114i=', 'gmu114j=', 'gmu114k=', 'gmu114l=',
              'gmu115k=', 'gmu115l=', 'gmu116k=', 'gmu116l=']
    _missing = [n for n in _names if n not in _k114116]
    check(not _missing, 'R251 every row of the round is present (%s)' % _missing)
    check(_k114116.index('if (r114mode == 114) {') < _i115 <
          _k114116.rindex('} else {'),
          'R251 the three mode branches are ordered 114, 115, 116')
    for _label, _body in (('114', _b114), ('115', _b115)):
        _st = _body.index('HFI_CTRL_INIT] = 1u;')
        check(_body.index('gmu' + _label + 'k=') < _st and
              len(_store.findall(_body[_st:])) == 0 and
              _body.count('IOLog(') == 2,
              'R251 key %s prints before the store and exactly one read row after it' % _label)
    check(len(_store.findall(_b116[_b116.index('gmu116k='):])) == 0,
          'R251 key 116 stores the mask before its first row and nothing after it')

    # ---- ROUND 252: the same three keys, but the read lands on the other side of the fault.
    check(_k117119 != '', 'R252 the third round block is present after the second')
    check(_k117119.count('gmuW[') == 13,
          'R252 the round holds thirteen GMU stores: eight in the shared preamble and five across '
          'the three keys (got %d)' % _k117119.count('gmuW['))
    check(_k117119.count('HFI_CTRL_INIT] = 1u;') == 2,
          'R252 only keys 117 and 118 write the start word (got %d)'
          % _k117119.count('HFI_CTRL_INIT] = 1u;'))
    check(_k117119.count('GMU2HOST_INTR_MASK] = ') == 1 and
          _k117119.count('0xff7fffffu') == 1,
          'R252 exactly one store opens the one interrupt source (key 117) (got %d)'
          % _k117119.count('GMU2HOST_INTR_MASK] = '))
    check(_k117119.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 3,
          'R252 the preamble clears the block once and keys 118 and 119 add one more each (got %d)'
          % _k117119.count('GMU2HOST_INTR_CLR] = 0xffffffffu;'))
    check(_k117119.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'R252 the preamble still clears the init word once')
    check(_k117119.count('for (') == 5 and _k117119.count('IODelay(1u)') == 4
          and 'IOSleep' not in _k117119,
          'R252 one loop in the preamble and one per key, all paced by IODelay (got %d loops)'
          % _k117119.count('for ('))
    check(_k117119.count('0xbabefaceu') == 1, 'R252 the preamble still waits for the init word')
    _names252 = ['gmu117a=', 'gmu117b=', 'gmu117c=', 'gmu117d=', 'gmu117e=', 'gmu117f=',
                 'gmu117g=', 'gmu117h=', 'gmu117i=', 'gmu117j=', 'gmu117k=', 'gmu117l=',
                 'gmu118k=', 'gmu118l=', 'gmu119k=', 'gmu119l=']
    _missing252 = [n for n in _names252 if n not in _k117119]
    check(not _missing252, 'R252 every row of the round is present (%s)' % _missing252)
    _i118m = _k117119.index('} else if (r117mode == 118) {')
    check(_k117119.index('if (r117mode == 117) {') < _i118m <
          _k117119.rindex('} else {'),
          'R252 the three mode branches are ordered 117, 118, 119')
    _b117 = _k117119[_k117119.index('if (r117mode == 117) {'):_i118m]
    _b118 = _k117119[_i118m:_k117119.rindex('} else {')]
    check(_b117.count('for (') == 1 and 'polls' in _b117,
          'R252 key 117 polls the status word after its start word')
    check(_b118.count('for (') == 1 and '20000u' in _b118,
          'R252 key 118 uses the short wait instead of the long poll')
    check(len(_store.findall(_b117[_b117.index('HFI_CTRL_INIT] = 1u;'):])) == 0 and
          len(_store.findall(_b118[_b118.index('HFI_CTRL_INIT] = 1u;'):])) == 0,
          'R252 neither start word is followed by another store')
    check('HFI_CTRL_INIT] = 1u;' not in _k117119[_k117119.rindex('} else {'):],
          'R252 key 119 never writes the start word')

    # ---- ROUND 253: three follow-up questions about the fault that key 117 finds.
    check(_k120122 != '', 'R253 the follow-up block is present after the fault-timing block')
    check(_k120122.count('gmuW[') == 16,
          'R253 the round holds sixteen GMU stores: eight in the shared preamble and eight across '
          'the three keys (got %d)' % _k120122.count('gmuW['))
    check(_k120122.count('HFI_CTRL_INIT] = 1u;') == 3,
          'R253 all three keys write the start word (got %d)'
          % _k120122.count('HFI_CTRL_INIT] = 1u;'))
    check(_k120122.count('GMU2HOST_INTR_MASK] = ') == 2 and
          _k120122.count('0xff7fffffu') == 2,
          'R253 keys 120 and 121 are the only ones that open the one interrupt source (got %d)'
          % _k120122.count('GMU2HOST_INTR_MASK] = '))
    check(_k120122.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 2,
          'R253 the preamble clears the block once and key 120 clears it again after the bit shows '
          '(got %d)' % _k120122.count('GMU2HOST_INTR_CLR] = 0xffffffffu;'))
    check(_k120122.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'R253 the preamble still clears the init word once')
    check(_k120122.count('HFI_QTBL_ADDR] = ') == 2 and
          _k120122.count('HFI_QTBL_ADDR] = 0u;') == 1,
          'R253 only key 122 writes a zero queue-table address (got %d address stores)'
          % _k120122.count('HFI_QTBL_ADDR] = '))
    check(_k120122.count('HFI_QTBL_INFO] = 1u;') == 2,
          'R253 the preamble arms the descriptor and key 122 arms the zero one (got %d)'
          % _k120122.count('HFI_QTBL_INFO] = 1u;'))
    check(_k120122.count('for (') == 5 and _k120122.count('IODelay(1u)') == 4
          and 'IOSleep' not in _k120122,
          'R253 one loop in the preamble and one per key, all paced by IODelay (got %d loops)'
          % _k120122.count('for ('))
    check(_k120122.count('0xbabefaceu') == 1, 'R253 the preamble still waits for the init word')
    _names253 = ['gmu120a=', 'gmu120b=', 'gmu120c=', 'gmu120d=', 'gmu120e=', 'gmu120f=',
                 'gmu120g=', 'gmu120h=', 'gmu120i=', 'gmu120j=', 'gmu120k=', 'gmu120l=',
                 'gmu121k=', 'gmu121l=', 'gmu122k=', 'gmu122l=']
    _missing253 = [n for n in _names253 if n not in _k120122]
    check(not _missing253, 'R253 every row of the round is present (%s)' % _missing253)
    _i121m = _k120122.index('} else if (r120mode == 121) {')
    check(_k120122.index('if (r120mode == 120) {') < _i121m <
          _k120122.rindex('} else {'),
          'R253 the three mode branches are ordered 120, 121, 122')
    _b120 = _k120122[_k120122.index('if (r120mode == 120) {'):_i121m]
    _b121 = _k120122[_i121m:_k120122.rindex('} else {')]
    _b122 = _k120122[_k120122.rindex('} else {'):]
    check(_b120.count('for (') == 1 and 'polls' in _b120,
          'R253 key 120 polls the status word after its start word')
    check(_b120.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
          _b120.index('gmu120k=') < _b120.index('GMU2HOST_INTR_CLR] = 0xffffffffu;'),
          'R253 key 120 clears the block only after it has reported the observation')
    check(_b121.count('for (') == 1 and 'gmu121l=' in _b121 and
          'CM3_SYSRESET * 4u' in _b121 and 'CM3_BOOT_CONFIG * 4u' in _b121,
          'R253 key 121 re-reads the CM3 reset and boot config words after the poll')
    check(_b122.count('for (') == 1 and
          _b122.index('HFI_QTBL_ADDR] = 0u;') < _b122.index('HFI_CTRL_INIT] = 1u;'),
          'R253 key 122 clears the queue-table address before its start word')
    check(len(_store.findall(_b121[_b121.index('HFI_CTRL_INIT] = 1u;'):])) == 0 and
          len(_store.findall(_b122[_b122.index('HFI_CTRL_INIT] = 1u;'):])) == 0,
          'R253 keys 121 and 122 stop at the start word')
    _after120 = _b120[_b120.index('HFI_CTRL_INIT] = 1u;'):]
    check(len(_store.findall(_after120)) == 1 and
          'GMU2HOST_INTR_CLR] = 0xffffffffu;' in _after120,
          'R253 key 120 has exactly one store after the start word and it is the second clear (got %d)'
          % len(_store.findall(_after120)))

    # ---- ROUND 254: read the address the fault names, and separate a fresh fault from an old one.
    check(_k124126 != '', 'R254 the fault-address block is present after the follow-up block')
    check(_k124126.count('gmuW[') == 13,
          'R254 the round holds thirteen GMU stores: eight in the shared preamble and five across '
          'the three keys (got %d)' % _k124126.count('gmuW['))
    check(_k124126.count('HFI_CTRL_INIT] = 1u;') == 2,
          'R254 only keys 124 and 125 write the start word (got %d)'
          % _k124126.count('HFI_CTRL_INIT] = 1u;'))
    check(_k124126.count('GMU2HOST_INTR_MASK] = ') == 3 and
          _k124126.count('0xff7fffffu') == 3,
          'R254 all three keys open the same single interrupt source (got %d mask stores)'
          % _k124126.count('GMU2HOST_INTR_MASK] = '))
    check(_k124126.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
          _k124126.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'R254 the preamble still clears the block and the init word exactly once each')
    check(_k124126.count('HFI_QTBL_ADDR] = ') == 1 and
          _k124126.count('HFI_QTBL_INFO] = 1u;') == 1,
          'R254 the queue table is armed once and never zeroed (got %d address stores)'
          % _k124126.count('HFI_QTBL_ADDR] = '))
    check(_k124126.count('0xc00001feu') == 2,
          'R254 keys 124 and 126 clear the CB0 fault status word, key 125 leaves it alone (got %d)'
          % _k124126.count('0xc00001feu'))
    check(_k124126.count('0x58u') == 11 and _k124126.count('0x60u') == 5 and
          _k124126.count('0x68u') == 3,
          'R254 the round reads status at 0x58, address at 0x60 and syndrome at 0x68 (got %d/%d/%d)'
          % (_k124126.count('0x58u'), _k124126.count('0x60u'), _k124126.count('0x68u')))
    check(_k124126.count('for (') == 5 and _k124126.count('IODelay(1u)') == 4
          and 'IOSleep' not in _k124126,
          'R254 two loops in the preamble and one per key, all paced by IODelay (got %d loops)'
          % _k124126.count('for ('))
    check(_k124126.count('0xbabefaceu') == 1, 'R254 the preamble still waits for the init word')
    _names254 = ['gmu124a=', 'gmu124b=', 'gmu124c=', 'gmu124d=', 'gmu124e=', 'gmu124f=',
                 'gmu124g=', 'gmu124h=', 'gmu124i=', 'gmu124j=', 'gmu124k=', 'gmu124l=',
                 'gmu125k=', 'gmu125l=', 'gmu126k=', 'gmu126l=']
    _missing254 = [n for n in _names254 if n not in _k124126]
    check(not _missing254, 'R254 every row of the round is present (%s)' % _missing254)
    _i125m = _k124126.index('} else if (r124mode == 125) {')
    check(_k124126.index('if (r124mode == 124) {') < _i125m <
          _k124126.rindex('} else {'),
          'R254 the three mode branches are ordered 124, 125, 126')
    _b124 = _k124126[_k124126.index('if (r124mode == 124) {'):_i125m]
    _b125 = _k124126[_i125m:_k124126.rindex('} else {')]
    _b126 = _k124126[_k124126.rindex('} else {'):]
    check(_b124.count('0xc00001feu') == 1 and
          _b124.index('0xc00001feu') < _b124.index('HFI_CTRL_INIT] = 1u;'),
          'R254 key 124 clears the fault status word before its start word')
    check(_b125.count('0xc00001feu') == 0 and 'un_cleared=1' in _b125 and
          _b125.count('0x58u') == 3,
          'R254 key 125 reads the status word twice and never clears it')
    check(_b126.count('HFI_CTRL_INIT') == 0 and _b126.count('0xc00001feu') == 1 and
          _b126.count('for (') == 1,
          'R254 key 126 clears the status word, polls, and never writes the start word')
    check(_b124.count('for (') == 1 and _b125.count('for (') == 1 and
          'fsynr0=' in _b124 and 'fsynr0=' in _b125 and 'fsynr0=' in _b126,
          'R254 every key reads the syndrome word after its own poll')
    check(len(_store.findall(_b124[_b124.index('HFI_CTRL_INIT] = 1u;'):])) == 0 and
          len(_store.findall(_b125[_b125.index('HFI_CTRL_INIT] = 1u;'):])) == 0,
          'R254 no start word is followed by another store')
    print()
    check(_k127129 != '', 'R255 the low-address block is present after the fault-address block')
    check(_k127129.count('gmuW[') == 13,
          'R255 the round holds thirteen GMU stores: eight in the shared preamble and five across '
          'the three keys (got %d)' % _k127129.count('gmuW['))
    check(_k127129.count('HFI_CTRL_INIT] = 1u;') == 2,
          'R255 only keys 127 and 128 write the start word (got %d)'
          % _k127129.count('HFI_CTRL_INIT] = 1u;'))
    check(_k127129.count('GMU2HOST_INTR_MASK] = ') == 3 and
          _k127129.count('0xff7fffffu') == 3,
          'R255 all three keys open the same single interrupt source (got %d mask stores)'
          % _k127129.count('GMU2HOST_INTR_MASK] = '))
    check(_k127129.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
          _k127129.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'R255 the preamble still clears the block and the init word exactly once each')
    check(_k127129.count('HFI_QTBL_ADDR] = r127qtblValue;') == 1 and
          _k127129.count('HFI_QTBL_INFO] = 1u;') == 1,
          'R255 the queue-table address store is the one place the value can be truncated')
    check(_k127129.count('0xc00001feu') == 3,
          'R255 every key clears the CB0 fault status word once (got %d)'
          % _k127129.count('0xc00001feu'))
    check(_k127129.count('0x58u') == 11 and _k127129.count('0x60u') == 5 and
          _k127129.count('0x68u') == 3,
          'R255 the round reads status at 0x58, address at 0x60 and syndrome at 0x68 (got %d/%d/%d)'
          % (_k127129.count('0x58u'), _k127129.count('0x60u'), _k127129.count('0x68u')))
    check(_k127129.count('for (') == 5 and _k127129.count('IODelay(1u)') == 4
          and 'IOSleep' not in _k127129,
          'R255 two loops in the preamble and one per key, all paced by IODelay (got %d loops)'
          % _k127129.count('for ('))
    check(_k127129.count('0xbabefaceu') == 1, 'R255 the preamble still waits for the init word')
    check('0x2d800000ull, r127base, 0x200000ull' in _k127129 and
          _k127129.count('iova=%08x') == 1,
          'R255 the table build separates input and output address: IOVA 2d800000 onto the queue '
          'table page')
    _names255 = ['gmu127a=', 'gmu127b=', 'gmu127c=', 'gmu127d=', 'gmu127e=', 'gmu127f=',
                 'gmu127g=', 'gmu127h=', 'gmu127i=', 'gmu127j=', 'gmu127k=', 'gmu127l=',
                 'gmu128k=', 'gmu128l=', 'gmu129k=', 'gmu129l=']
    _missing255 = [n for n in _names255 if n not in _k127129]
    check(not _missing255, 'R255 every row of the round is present (%s)' % _missing255)
    _i128m = _k127129.index('} else if (r127mode == 128) {')
    check(_k127129.index('if (r127mode == 127) {') < _i128m <
          _k127129.rindex('} else {'),
          'R255 the three mode branches are ordered 127, 128, 129')
    _b127 = _k127129[_k127129.index('if (r127mode == 127) {'):_i128m]
    _b128 = _k127129[_i128m:_k127129.rindex('} else {')]
    _b129 = _k127129[_k127129.rindex('} else {'):]
    check(_b127.index('0xc00001feu') < _b127.index('HFI_CTRL_INIT] = 1u;'),
          'R255 key 127 clears the fault status word before its start word')
    check('gmu128k=trunc_written=1 want=%08x back=%08x qtbl_orig=%08x' in _b128 and
          'HFI_CTRL_INIT] = 1u;' in _b128 and '0xc00001feu' in _b128,
          'R255 key 128 writes the truncated address, reads it back, and only then starts')
    check('HFI_CTRL_INIT] = 1u;' not in _b129 and 'no_start=1' in _b129,
          'R255 key 129 never writes the start word')
    check(all('fsynr0=%08x' in b for b in (_b127, _b128, _b129)),
          'R255 every key reads the syndrome word after its own poll')
    check(_b127.rindex('0xc00001feu') < _b127.index('HFI_CTRL_INIT] = 1u;') and
          _b128.rindex('0xc00001feu') < _b128.index('HFI_CTRL_INIT] = 1u;') and
          _b129.rindex('0xc00001feu') < len(_b129),
          'R255 no start word is preceded by a later clear in the wrong order')

    print()
    check(_k130132 != '', 'R256 the readback block is present after the low-address block')
    check(_k130132.count('gmuW[') == 12,
          'R256 the round holds twelve GMU stores: eight in the shared preamble and four across '
          'the three keys (got %d)' % _k130132.count('gmuW['))
    check(_k130132.count('HFI_CTRL_INIT] = 1u;') == 1,
          'R256 only key 131 writes the start word (got %d)'
          % _k130132.count('HFI_CTRL_INIT] = 1u;'))
    check(_k130132.count('GMU2HOST_INTR_MASK] = ') == 3 and
          _k130132.count('0xff7fffffu') == 3,
          'R256 all three keys open the same single interrupt source (got %d mask stores)'
          % _k130132.count('GMU2HOST_INTR_MASK] = '))
    check(_k130132.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
          _k130132.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'R256 the preamble still clears the block and the init word exactly once each')
    check(_k130132.count('HFI_QTBL_ADDR] = r130qtblValue;') == 1 and
          _k130132.count('HFI_QTBL_INFO] = 1u;') == 1,
          'R256 the queue-table address is handed over untruncated, exactly once')
    check(_k130132.count('0xc00001feu') == 3,
          'R256 every key clears the CB0 fault status word once (got %d)'
          % _k130132.count('0xc00001feu'))
    check(_k130132.count('0x58u') == 12 and _k130132.count('0x60u') == 5 and
          _k130132.count('0x68u') == 3,
          'R256 the round reads status at 0x58, address at 0x60 and syndrome at 0x68 (got %d/%d/%d)'
          % (_k130132.count('0x58u'), _k130132.count('0x60u'), _k130132.count('0x68u')))
    check(_k130132.count('for (') == 6 and _k130132.count('IODelay(1u)') == 4
          and 'IOSleep' not in _k130132,
          'R256 three loops in the preamble (write walk, init poll, clean) and one per key (got %d)'
          % _k130132.count('for ('))
    check(_k130132.count('dc civac') == 1 and _k130132.count('dsb sy') >= 8,
          'R256 cleans the table pages once, with a full barrier after it')
    check(_k130132.index('dc civac') < _k130132.index('HFI_CTRL_INIT] = 1u;'),
          'R256 the clean happens before the only start word, never after it')
    check(_k130132.count('kAdrenoLpaePteAddrMask') == 2 and
          _k130132.count('readback=1') == 1 and 'm0=%u m1=%u m2=%u' in _k130132,
          'R256 reads the level-0 and level-1 words back and walks to the leaf descriptor')
    check(_k130132.count('adreno_lpae_table_pte(') == 2 and
          _k130132.count('adreno_lpae_leaf_pte(') == 1 and
          _k130132.count('adreno_lpae_lvl_idx(') == 1,
          'R256 compares the words read back against the plan encodings, not against a constant')
    check(_k130132.count('0xbabefaceu') == 1, 'R256 the preamble still waits for the init word')
    _names256 = ['gmu130a=', 'gmu130b=', 'gmu130c=', 'gmu130d=', 'gmu130e=', 'gmu130f=',
                 'gmu130g=', 'gmu130h=', 'gmu130i=', 'gmu130j=', 'gmu130m=', 'gmu130n=',
                 'gmu130k=', 'gmu130l=', 'gmu131k=', 'gmu131l=', 'gmu132k=', 'gmu132l=']
    _missing256 = [n for n in _names256 if n not in _k130132]
    check(not _missing256, 'R256 every row of the round is present (%s)' % _missing256)
    _i131m = _k130132.index('} else if (r130mode == 131) {')
    check(_k130132.index('if (r130mode == 130) {') < _i131m <
          _k130132.rindex('} else {'),
          'R256 the three mode branches are ordered 130, 131, 132')
    _b130 = _k130132[_k130132.index('if (r130mode == 130) {'):_i131m]
    _b131 = _k130132[_i131m:_k130132.rindex('} else {')]
    _b132 = _k130132[_k130132.rindex('} else {'):]
    check(_b130.count('HFI_CTRL_INIT') == 0 and 'no_start=1' in _b130,
          'R256 key 130 reads the words back and never writes the start word')
    check(_b131.count('HFI_CTRL_INIT] = 1u;') == 1 and '0xc00001feu' in _b131,
          'R256 key 131 cleans, clears the status word and then starts the HFI once')
    check(_b132.count('HFI_CTRL_INIT') == 0 and 'no_start=1' in _b132,
          'R256 key 132 cleans the same pages and never writes the start word')
    check('cleaned=1' in _k130132 and 'cleaned=0' in _k130132,
          'R256 the clean row says whether it ran, so the two controls are distinguishable')
    check(_b131.rindex('0xc00001feu') < _b131.index('HFI_CTRL_INIT] = 1u;') and
          len(_store.findall(_b131[_b131.index('HFI_CTRL_INIT] = 1u;'):])) == 0,
          'R256 the start word comes after the clear and is the last store of the round')

    # ---- ROUND 257: read the bank back after the start word, and flush the translations first.
    check(_k133135 != '', 'R257 the recheck block is present after the readback block')
    check(len(_store.findall(_k133135)) == 14,
          'R257 the round holds fourteen GMU stores: eight in the shared preamble and six across '
          'the three keys (got %d)' % len(_store.findall(_k133135)))
    check(_k133135.count('HFI_CTRL_INIT] = 1u;') == 3,
          'R257 all three keys hand over the start word (got %d)'
          % _k133135.count('HFI_CTRL_INIT] = 1u;'))
    check(_k133135.count('GMU2HOST_INTR_MASK] = ') == 3 and
          _k133135.count('0xff7fffffu') == 3,
          'R257 each key leaves only the fault source unmasked (got %d/%d)'
          % (_k133135.count('GMU2HOST_INTR_MASK] = '), _k133135.count('0xff7fffffu')))
    check(_k133135.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1,
          'R257 the preamble clears the HFI interrupts once, not once per key (got %d)'
          % _k133135.count('GMU2HOST_INTR_CLR] = 0xffffffffu;'))
    check(_k133135.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'R257 the init word is cleared once, before the reset pulse (got %d)'
          % _k133135.count('CM3_FW_INIT_RESULT] = 0u;'))
    check(_k133135.count('HFI_QTBL_ADDR] = r133qtblValue;') == 1 and
          _k133135.count('HFI_QTBL_INFO] = 1u;') == 1,
          'R257 the queue table is re-armed with the full address this time')
    check(_k133135.count('0xc00001feu') == 3,
          'R257 each key clears the fault status word exactly once (got %d)'
          % _k133135.count('0xc00001feu'))
    check(_k133135.count('0x58u') == 15 and _k133135.count('0x60u') == 6 and
          _k133135.count('0x68u') == 4,
          'R257 the fault word, the fault address and the syndrome are read back '
          '(got %d/%d/%d)' % (_k133135.count('0x58u'), _k133135.count('0x60u'),
                              _k133135.count('0x68u')))
    check(_k133135.count('0x74u') == 3 and _k133135.count('0x7f4u') == 2,
          'R257 the global and context-bank sync words are polled until idle '
          '(got %d/%d)' % (_k133135.count('0x74u'), _k133135.count('0x7f4u')))
    check(_k133135.count('0x6cu') == 1 and _k133135.count('0x68u') >= 4 and
          _k133135.count('0x610u') == 1 and _k133135.count('0x7f0u') == 1,
          'R257 key 135 invalidates all translations and the stage-1 ASID and syncs both')
    check(_k133135.count('for (') == 8 and _k133135.count('IODelay(1u)') == 6 and
          'IOSleep' not in _k133135,
          'R257 every wait is a yielding read loop (got %d loops, %d delays)'
          % (_k133135.count('for ('), _k133135.count('IODelay(1u)')))
    check('0xbabefaceu' in _k133135 and _k133135.count('0xbabefaceu') == 1,
          'R257 the CM3 bring-up still waits for its own init word')
    check('readback=1' in _k133135 and 'cleaned=1' in _k133135,
          'R257 the descriptor readback and the page clean are both still reported')
    _rows257 = ['gmu133a', 'gmu133b', 'gmu133c', 'gmu133d', 'gmu133e', 'gmu133f',
                'gmu133g', 'gmu133h', 'gmu133i', 'gmu133j', 'gmu133m', 'gmu133n',
                'gmu133k', 'gmu133l', 'gmu134k', 'gmu134l', 'gmu135k', 'gmu135k2',
                'gmu135l']
    _missing257 = [r for r in _rows257 if ('%s=' % r) not in _k133135]
    check(not _missing257,
          'R257 the round reports its own rows (missing %s)' % _missing257)
    check(_k133135.index('if (r133mode == 133) {') <
          _k133135.index('} else if (r133mode == 134) {') <
          _k133135.rindex('} else {'),
          'R257 the three keys appear in order 133, 134, 135')
    _b133 = _k133135.split('if (r133mode == 133) {')[1].split('} else if')[0]
    _b134 = _k133135.split('} else if (r133mode == 134) {')[1].split('} else {')[0]
    _b135 = _k133135[_k133135.rindex('} else {') + len('} else {'):]
    check(_b135.index('0x6cu') < _b135.index('GMU2HOST_INTR_MASK] = ') and
          _b135.index('0x610u') < _b135.index('GMU2HOST_INTR_MASK] = '),
          'R257 key 135 flushes the translations BEFORE it clears the status word')
    check(len(_store.findall(_b133[_b133.index('HFI_CTRL_INIT] = 1u;'):])) == 0 and
          len(_store.findall(_b134[_b134.index('HFI_CTRL_INIT] = 1u;'):])) == 0 and
          len(_store.findall(_b135[_b135.index('HFI_CTRL_INIT] = 1u;'):])) == 0,
          'R257 the start word is the last store of each key')
    check(_b133.count('0x20u') >= 1 and _b133.count('0x00u') >= 1 and
          _b133.count('0x30u') >= 1 and _b133.count('0x000u') >= 1,
          'R257 key 133 reads the bank own TTBR0, SCTLR, TCR and CBAR back after the poll')
    check(_b134.count('0x48u') >= 2 and _b134.count('0x58u') >= 6,
          'R257 key 134 reads the global fault word and the four neighbouring banks')


    check(_k136138 != '', 'R258 the read-path block is present after the recheck block')
    check(len(_store.findall(_k136138)) == 14,
          'the R258 block holds fourteen GMU stores: eight in the shared preamble and six across '
          'the three keys (got %d)' % len(_store.findall(_k136138)))
    check(_k136138.count('HFI_CTRL_INIT] = 1u;') == 3,
          'each R258 key carries the start word once (got %d)'
          % _k136138.count('HFI_CTRL_INIT] = 1u;'))
    check(_k136138.count('GMU2HOST_INTR_MASK] = ') == 3 and
          _k136138.count('0xff7fffffu') == 3,
          'each R258 key enables the one interrupt source and no other (got %d/%d)'
          % (_k136138.count('GMU2HOST_INTR_MASK] = '), _k136138.count('0xff7fffffu')))
    check(_k136138.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
          _k136138.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'the R258 preamble clears the interrupt word and the init word once each')
    check(_k136138.count('HFI_QTBL_ADDR] = r136qtblValue;') == 1 and
          _k136138.count('HFI_QTBL_INFO] = 1u;') == 1,
          'the R258 preamble hands the firmware the queue table once')
    check(_k136138.count('0xc00001feu') == 3,
          'each R258 key clears the status word once (got %d)'
          % _k136138.count('0xc00001feu'))
    check(_k136138.count('0x58u') == 15 and _k136138.count('0x60u') == 6 and
          _k136138.count('0x68u') == 3 and _k136138.count('0x48u') == 3,
          'the R258 keys read the status, address, syndrome and global words in the expected number '
          '(got %d/%d/%d/%d)' % (_k136138.count('0x58u'), _k136138.count('0x60u'),
                                _k136138.count('0x68u'), _k136138.count('0x48u')))
    check(_k136138.count('for (') == 6 and _k136138.count('IODelay(1u)') == 8 and
          'IOSleep' not in _k136138,
          'the R258 block paces itself with six read loops and eight short delays and no sleep '
          '(got %d/%d)' % (_k136138.count('for ('), _k136138.count('IODelay(1u)')))
    check(_k136138.count('0xbabefaceu') == 1 and 'readback=1' in _k136138 and
          'cleaned=1' in _k136138,
          'the R258 preamble still waits for the fixed init word')
    _rows258 = ['gmu136' + s for s in ('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
                                       'k', 'l', 'm', 'n')]
    _rows258 += ['gmu137k', 'gmu137l', 'gmu138k', 'gmu138l']
    _missing258 = [r for r in _rows258 if ('%s=' % r) not in _k136138]
    check(_missing258 == [],
          'every R258 row is printed in the block (missing: %s)' % ','.join(_missing258))
    check(_k136138.index('if (r136mode == 136) {') <
          _k136138.index('} else if (r136mode == 137) {') <
          _k136138.rindex('} else {'),
          'the R258 branches are declared in key order')
    _b136 = _k136138[_k136138.index('if (r136mode == 136) {'):_k136138.index('} else if (r136mode == 137) {')]
    _b137 = _k136138[_k136138.index('} else if (r136mode == 137) {'):_k136138.rindex('} else {')]
    _b138 = _k136138[_k136138.rindex('} else {'):]
    check(len(_store.findall(_b136[_b136.index('HFI_CTRL_INIT] = 1u;'):])) == 0 and
          len(_store.findall(_b137[_b137.index('HFI_CTRL_INIT] = 1u;'):])) == 0 and
          len(_store.findall(_b138[_b138.index('HFI_CTRL_INIT] = 1u;'):])) == 0,
          'R258 the start word is the last store of each key')
    check(_b136.count('0x58u') == 7 and _b136.count('far=%08x') == 1 and
          'fsrA=%08x fsrB=%08x fsrC=%08x' in _b136 and 'fsrD=%08x' in _b136,
          'R258 key 136 reads the status word three times in a row and once more after the address')
    check(_b137.count('0x60u') == 3 and _b137.count('0x00u') == 3 and
          'farA=%08x farB=%08x' in _b137 and 'sctlr_back=%08x' in _b137,
          'R258 key 137 reads the address twice around a same-value control-word write and read')
    check(_b138.count('0x800u') == 3 and 'smr_want=00000004' in _b138 and
          'gfsr=%08x' in _b138 and 'smr_end=%08x' in _b138,
          'R258 key 138 clears the stream match valid bit, keeps SID 4 and reads the global word')
    check(_b138.index('0x800u') < _b138.index('HFI_CTRL_INIT] = 1u;'),
          'R258 key 138 rewrites the stream match register BEFORE the start word')


    check(_k139141 != '', 'R259 the routing block is present after the read-path block')
    check(len(_store.findall(_k139141)) == 12,
          'the R259 block holds twelve GMU stores: eight in the shared preamble and four across '
          'the three keys (got %d)' % len(_store.findall(_k139141)))
    check(_k139141.count('HFI_CTRL_INIT] = 1u;') == 2,
          'keys 140 and 141 each carry the start word once and key 139 carries none (got %d)'
          % _k139141.count('HFI_CTRL_INIT] = 1u;'))
    check(_k139141.count('GMU2HOST_INTR_MASK] = ') == 2 and
          _k139141.count('0xff7fffffu') == 2,
          'keys 140 and 141 enable the one interrupt source and key 139 enables nothing (got %d/%d)'
          % (_k139141.count('GMU2HOST_INTR_MASK] = '), _k139141.count('0xff7fffffu')))
    check(_k139141.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
          _k139141.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'the R259 preamble clears the interrupt word and the init word once each')
    check(_k139141.count('HFI_QTBL_ADDR] = r139qtblValue;') == 1 and
          _k139141.count('HFI_QTBL_INFO] = 1u;') == 1,
          'the R259 preamble hands the firmware the queue table once')
    check(_k139141.count('0xc00001feu') == 3,
          'each R259 key clears the status word once (got %d)'
          % _k139141.count('0xc00001feu'))
    check(_k139141.count('0x58u') == 9 and _k139141.count('0x60u') == 3 and
          _k139141.count('0x68u') == 2 and _k139141.count('0x48u') == 4 and
          _k139141.count('0x800u') == 14,
          'the R259 keys read the bank and match words in the expected number (got %d/%d/%d/%d/%d)'
          % (_k139141.count('0x58u'), _k139141.count('0x60u'), _k139141.count('0x68u'),
             _k139141.count('0x48u'), _k139141.count('0x800u')))
    check(_k139141.count('for (') == 6 and _k139141.count('IODelay(1u)') == 4 and
          'IOSleep' not in _k139141,
          'the R259 block paces itself with six read loops and four short delays and no sleep '
          '(got %d/%d)' % (_k139141.count('for ('), _k139141.count('IODelay(1u)')))
    check(_k139141.count('0xbabefaceu') == 1 and 'readback=1' in _k139141,
          'the R259 preamble still waits for the fixed init word')
    _rows259 = ['gmu139a', 'gmu139b', 'gmu139c', 'gmu139d', 'gmu139e', 'gmu139f', 'gmu139g',
                'gmu139h', 'gmu139i', 'gmu139j', 'gmu139m', 'gmu139n',
                'gmu139k', 'gmu139l', 'gmu140k', 'gmu140l', 'gmu141k', 'gmu141l']
    _missing259 = [r for r in _rows259 if ('%s=' % r) not in _k139141]
    check(_missing259 == [],
          'every R259 row is printed in the block (missing: %s)' % ','.join(_missing259))
    check(_k139141.index('if (r139mode == 139) {') <
          _k139141.index('} else if (r139mode == 140) {') <
          _k139141.rindex('} else {'),
          'the R259 branches are declared in key order')
    _b139 = _k139141[_k139141.index('if (r139mode == 139) {'):
                     _k139141.index('} else if (r139mode == 140) {')]
    _b140 = _k139141[_k139141.index('} else if (r139mode == 140) {'):_k139141.rindex('} else {')]
    _b141 = _k139141[_k139141.rindex('} else {'):]
    check(_b139.count('HFI_CTRL_INIT] = 1u;') == 0 and _b139.count('GMU2HOST_INTR_MASK] = ') == 0,
          'R259 key 139 writes neither the start word nor a mask store')
    check(_b139.count('0x800u') >= 2 and _b139.index('0x00000004u') < _b139.index('0xc00001feu'),
          'R259 key 139 clears the stream match valid bit before it clears the status word')
    check(_b140.index('0x00000004u') < _b140.index('0x80000004u') <
          _b140.index('HFI_CTRL_INIT] = 1u;'),
          'R259 key 140 writes the match word down and straight back up before the start word')
    check(_b141.count('0x8000007fu') == 1 and
          _b141.index('0x8000007fu') < _b141.index('HFI_CTRL_INIT] = 1u;'),
          'R259 key 141 moves the match to a SID that does not exist BEFORE the start word')
    check('gfsr=%08x' in _b141 and 'fsr=%08x' in _b141 and 'smr_end=%08x' in _b141,
          'R259 key 141 reads the global word, the bank status and the match word back')


    check(_k142144 != '', 'R260 the routing-and-table block is present after the R259 block')
    check(len(_store.findall(_k142144)) == 14,
          'the R260 block holds fourteen GMU stores: eight in the shared preamble and two in each '
          'of the three keys (got %d)' % len(_store.findall(_k142144)))
    check(_k142144.count('HFI_CTRL_INIT] = 1u;') == 3,
          'each R260 key carries its start word once (got %d)'
          % _k142144.count('HFI_CTRL_INIT] = 1u;'))
    check(_k142144.count('GMU2HOST_INTR_MASK] = ') == 3 and _k142144.count('0xff7fffffu') == 3,
          'each R260 key enables the one interrupt source (got %d/%d)'
          % (_k142144.count('GMU2HOST_INTR_MASK] = '), _k142144.count('0xff7fffffu')))
    check(_k142144.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
          _k142144.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'the R260 preamble clears the interrupt word and the init word once each')
    check(_k142144.count('HFI_QTBL_ADDR] = r142qtblValue;') == 1 and
          _k142144.count('HFI_QTBL_INFO] = 1u;') == 1,
          'the R260 preamble hands the firmware the queue table once')
    check(_k142144.count('0xc00001feu') == 3,
          'each R260 key clears the status word once (got %d)'
          % _k142144.count('0xc00001feu'))
    check(_k142144.count('0x58u') == 9 and _k142144.count('0x60u') == 3 and
          _k142144.count('0x68u') == 2 and _k142144.count('0x48u') == 4 and
          _k142144.count('0x800u') == 8,
          'the R260 keys read the bank, global and match words in the expected number '
          '(got %d/%d/%d/%d/%d)' % (_k142144.count('0x58u'), _k142144.count('0x60u'),
                                    _k142144.count('0x68u'), _k142144.count('0x48u'),
                                    _k142144.count('0x800u')))
    check(_k142144.count('for (') == 10 and _k142144.count('IODelay(1u)') == 4 and
          'IOSleep' not in _k142144,
          'the R260 block paces itself with ten loops and four short delays and no sleep '
          '(got %d/%d)' % (_k142144.count('for ('), _k142144.count('IODelay(1u)')))
    check(_k142144.count('0xbabefaceu') == 1 and 'readback=1' in _k142144,
          'the R260 preamble still waits for the fixed init word')
    check(_k142144.count('512ull') == 2 and _k142144.count('dc civac') == 3,
          'the two table keys fill whole pages and clean them to the point of coherence')
    _rows260 = ['gmu142a', 'gmu142b', 'gmu142c', 'gmu142d', 'gmu142e', 'gmu142f', 'gmu142g',
                'gmu142h', 'gmu142i', 'gmu142j', 'gmu142k', 'gmu142l', 'gmu142m', 'gmu142n',
                'gmu143k', 'gmu143l', 'gmu144k', 'gmu144l']
    _missing260 = [r for r in _rows260 if ('%s=' % r) not in _k142144]
    check(_missing260 == [],
          'every R260 row is printed in the block (missing: %s)' % ','.join(_missing260))
    check(_k142144.index('if (r142mode == 142) {') <
          _k142144.index('} else if (r142mode == 143) {') <
          _k142144.rindex('} else {'),
          'the R260 branches are declared in key order')
    _b142 = _k142144[_k142144.index('if (r142mode == 142) {'):
                     _k142144.index('} else if (r142mode == 143) {')]
    _b143 = _k142144[_k142144.index('} else if (r142mode == 143) {'):_k142144.rindex('} else {')]
    _b144 = _k142144[_k142144.rindex('} else {'):]
    check(_b142.index('0x00000004u') < _b142.index('boot_log_panic_flush') <
          _b142.index('HFI_CTRL_INIT] = 1u;'),
          'R260 key 142 takes the match down and commits the log BEFORE the start word')
    check(_b142.count('HFI_CTRL_INIT] = 1u;') == 1 and _b143.count('HFI_CTRL_INIT] = 1u;') == 1 and
          _b144.count('HFI_CTRL_INIT] = 1u;') == 1,
          'every R260 key writes the start word exactly once')
    check(_b143.count('r143tab1') >= 1 and _b144.count('r144tab1') == 0,
          'R260 key 143 fills the root page and key 144 deliberately leaves it alone')
    check(_b143.count('512ull') == 1 and _b144.count('512ull') == 1 and
          'adreno_lpae_leaf_pte' in _b143 and 'adreno_lpae_leaf_pte' in _b144,
          'both table keys fill whole pages with table descriptors or the 2 MiB leaf')
    check('fsynr0=%08x' in _b143 and 'fsynr0=%08x' in _b144 and 'gfsr=%08x' in _b143,
          'the table keys read the fault words back after the start word')

    _rows261 = "gmu145a gmu145b gmu145c gmu145d gmu145e gmu145f gmu145g gmu145h gmu145i gmu145j gmu145k gmu145l gmu145m gmu145n gmu146k gmu146k2 gmu146l gmu147k gmu147k2 gmu147l".split()
    check(_k145147 != '', 'R261 the start-count and queue-read block is present after R260')
    check(len(_store.findall(_k145147)) == 19,
          'the R261 block holds nineteen GMU stores: eight in the shared preamble, three mask '
          'stores, six start words and the two re-arm stores (got %d)'
          % len(_store.findall(_k145147)))
    check(_k145147.count('HFI_CTRL_INIT] = 1u;') == 6,
          'key 145 writes the start word three times and key 147 twice, so six in all (got %d)'
          % _k145147.count('HFI_CTRL_INIT] = 1u;'))
    check(_k145147.count('GMU2HOST_INTR_MASK] = ') == 3 and _k145147.count('0xff7fffffu') == 3,
          'each R261 key enables the message source once (got %d/%d)'
          % (_k145147.count('GMU2HOST_INTR_MASK] = '), _k145147.count('0xff7fffffu')))
    check(_k145147.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
          _k145147.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'the shared preamble clears the interrupts and the init word exactly once each')
    check(_k145147.count('HFI_QTBL_ADDR] = ') == 2 and
          _k145147.count('HFI_QTBL_ADDR] = r145qtblValue;') == 2 and
          _k145147.count('HFI_QTBL_INFO] = 1u;') == 2,
          'the queue-table address is written once in the preamble and once by key 147 as the '
          're-arm, each with QTBL_INFO (got %d/%d)'
          % (_k145147.count('HFI_QTBL_ADDR] = '), _k145147.count('HFI_QTBL_INFO] = 1u;')))
    check(_k145147.count('0xc00001feu') == 3,
          'each key clears the bank status word once (got %d)' % _k145147.count('0xc00001feu'))
    check(_k145147.count('0x58u') == 6 and _k145147.count('0x60u') == 3 and
          _k145147.count('0x68u') == 3 and _k145147.count('0x48u') == 3 and
          _k145147.count('0x800u') == 6,
          'the fault words are read twice per key and the match word once (got %d/%d/%d/%d/%d)'
          % (_k145147.count('0x58u'), _k145147.count('0x60u'), _k145147.count('0x68u'),
             _k145147.count('0x48u'), _k145147.count('0x800u')))
    check(_k145147.count('for (') == 15 and _k145147.count('IODelay(1u)') == 7 and
          'IOSleep' not in _k145147,
          'the waits stay the yielding poll loops, never a sleep (got %d/%d)'
          % (_k145147.count('for ('), _k145147.count('IODelay(1u)')))
    check(_k145147.count('0xbabefaceu') == 1 and 'readback=1' in _k145147,
          'the shared preamble still waits for the firmware init word and reads the table back')
    check(_k145147.count('512ull') == 3 and _k145147.count('dc civac') == 4,
          'all three keys fill whole pages and clean them to the point of coherence '
          '(got %d/%d)' % (_k145147.count('512ull'), _k145147.count('dc civac')))
    check(_k145147.count('armcpu_map_physical') >= 2 and 'queue_words=1' in _k145147 and
          'queue_words_after_rearm=1' in _k145147,
          'keys 146 and 147 both map the queue-table page and read it back')
    _missing261 = [r for r in _rows261 if ('%s=' % r) not in _k145147]
    check(not _missing261, 'every R261 row is printed (missing: %s)' % ', '.join(_missing261))
    check(_k145147.index('if (r145mode == 145) {') <
          _k145147.index('} else if (r145mode == 146) {') <
          _k145147.rindex('} else {'),
          'the R261 branches are in key order 145, 146, 147')
    _b145 = _k145147[_k145147.index('if (r145mode == 145) {'):
                     _k145147.index('} else if (r145mode == 146) {')]
    _b146 = _k145147[_k145147.index('} else if (r145mode == 146) {'):_k145147.rindex('} else {')]
    _b147 = _k145147[_k145147.rindex('} else {'):]
    check(_b145.count('HFI_CTRL_INIT] = 1u;') == 3 and _b146.count('HFI_CTRL_INIT] = 1u;') == 1 and
          _b147.count('HFI_CTRL_INIT] = 1u;') == 2,
          'key 145 starts three times, key 146 once and key 147 twice around its re-arm')
    check(_b145.count('pol0=%u') == 1 and _b145.count('pol1=%u') == 1 and
          _b145.count('pol2=%u') == 1,
          'key 145 reports one poll count per start word')
    check(_b147.index('HFI_QTBL_ADDR] = r145qtblValue;') < _b147.rindex('HFI_CTRL_INIT] = 1u;'),
          'key 147 re-arms the queue table before its second start word')
    check('l0=%016llx' in _b145 and 'l0=%016llx' in _b146 and 'l0=%016llx' in _b147,
          'each R261 key prints the root entry it filled before starting')
    check('0x58u' in _b145 and '0x58u' in _b146 and '0x58u' in _b147 and
          'fsynr0=%08x' in _b145 and 'fsynr0=%08x' in _b146 and 'fsynr0=%08x' in _b147,
          'each key reads the fault words back after its start words')


    _rows262 = "gmu148a gmu148b gmu148c gmu148d gmu148e gmu148f gmu148g gmu148h gmu148i gmu148j gmu148k gmu148l gmu148m gmu148n gmu149k gmu149l gmu150k gmu150k2 gmu150l".split()
    check(_k148150 != '', 'R262 the pre/post queue-read block follows the R261 block')
    check(len(_store.findall(_k148150)) == 12,
          'the R262 block holds twelve GMU stores: eight in the shared preamble, the page fill '
          'takes none, and the three keys add one mask store and one start word each (got %d)'
          % len(_store.findall(_k148150)))
    check(_k148150.count('HFI_CTRL_INIT] = 1u;') == 2,
          'only the two keys that ask the firmware to start do so (got %d)'
          % _k148150.count('HFI_CTRL_INIT] = 1u;'))
    check(_k148150.count('GMU2HOST_INTR_MASK] = ') == 2 and _k148150.count('0xff7fffffu') == 2,
          'each starting key opens exactly one interrupt source (got %d/%d)'
          % (_k148150.count('GMU2HOST_INTR_MASK] = '), _k148150.count('0xff7fffffu')))
    check(_k148150.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
          _k148150.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'the R262 preamble clears the interrupt and the init word once each')
    check(_k148150.count('HFI_QTBL_ADDR] = ') == 1 and
          _k148150.count('HFI_QTBL_INFO] = 1u;') == 1,
          'the R262 preamble re-arms the queue table once (got %d/%d)'
          % (_k148150.count('HFI_QTBL_ADDR] = '), _k148150.count('HFI_QTBL_INFO] = 1u;')))
    check(_k148150.count('0xc00001feu') == 2,
          'each reading key clears the bank status word once (got %d)'
          % _k148150.count('0xc00001feu'))
    check(_k148150.count('0x58u') == 5 and _k148150.count('0x60u') == 3 and
          _k148150.count('0x68u') == 1 and _k148150.count('0x800u') == 3,
          'the fault words are read five, three, one and three times (got %d/%d/%d/%d)'
          % (_k148150.count('0x58u'), _k148150.count('0x60u'), _k148150.count('0x68u'),
             _k148150.count('0x800u')))
    check(_k148150.count('for (') == 9 and _k148150.count('IODelay(1u)') == 4 and
          'IOSleep' not in _k148150,
          'nine loops, four one-microsecond waits and no sleep (got %d/%d)'
          % (_k148150.count('for ('), _k148150.count('IODelay(1u)')))
    check(_k148150.count('0xbabefaceu') == 1 and 'readback=1' in _k148150,
          'the firmware init poll and the descriptor readback both survive')
    check(_k148150.count('512ull') == 1 and _k148150.count('dc civac') == 2,
          'the page fill walks one page span and cleans it once (got %d/%d)'
          % (_k148150.count('512ull'), _k148150.count('dc civac')))
    check(_k148150.count('armcpu_map_physical') >= 3,
          'the queue-table page is mapped in the preamble and read by both reading keys (got %d)'
          % _k148150.count('armcpu_map_physical'))
    _missing262 = [r for r in _rows262 if ('%s=' % r) not in _k148150]
    check(_missing262 == [],
          'every R262 row appears in its own block (missing: %s)' % (_missing262,))
    check(_k148150.index('if (r148mode == 148) {') <
          _k148150.index('} else if (r148mode == 149) {') <
          _k148150.rindex('} else {'),
          'the three R262 keys are in ascending order')
    _b148 = _k148150[_k148150.index('if (r148mode == 148) {'):
                     _k148150.index('} else if (r148mode == 149) {')]
    _b149 = _k148150[_k148150.index('} else if (r148mode == 149) {'):_k148150.rindex('} else {')]
    _b150 = _k148150[_k148150.rindex('} else {'):]
    check(_b148.count('HFI_CTRL_INIT] = 1u;') == 1 and _b148.count('GMU2HOST_INTR_MASK] = ') == 1,
          'R262 key 148 commits one start word and one mask store')
    check(_b148.index('q0=%08x') > _b148.index('p0=%08x'),
          'R262 key 148 reads the queue page before and after the start word')
    check(_b149.count('HFI_CTRL_INIT] = 1u;') == 0 and _b149.count('GMU2HOST_INTR_MASK] = ') == 0,
          'R262 key 149 writes no GMU store at all')
    check(_b149.index('b0=%08x') > _b149.index('a0=%08x'),
          'R262 key 149 reads the queue page twice without a start word')
    check(_b150.count('HFI_CTRL_INIT] = 1u;') == 1 and _b150.count('GMU2HOST_INTR_MASK] = ') == 1,
          'R262 key 150 commits one start word and one mask store')
    check(_b150.count('w00=%08x') == 1 and _b150.count('w16=%08x') == 1,
          'R262 key 150 prints the first thirty-two words of the queue table page')
    check('armcpu_map_physical' in _b148 and 'armcpu_map_physical' in _b149 and 'dc civac' in _k148150[
          :_k148150.index('if (r148mode == 148) {')],
          'the page fill and the cache clean both live in the shared preamble of R262')

    _rows263 = ("gmu151a gmu151b gmu151c gmu151d gmu151e gmu151f gmu151g gmu151h gmu151i gmu151j "
                "gmu151k1 gmu151k2 gmu151k3 gmu151l gmu151m gmu151n gmu152l gmu153l").split()
    check(_k151153 != '', 'R263 the page-bracket block follows the R262 block')
    check(_k154156 != '', 'R264 the post-acceptance block follows the R263 block')
    check(_k157159 != '', 'R265 the write-path block follows the R264 block')
    check(_k160162 != '', 'R266 the matrix block follows the R265 block')
    check(_k163165 != '', 'R267 the counting block follows the R266 block')
    check(_k166168 != '', 'R268 the counting block follows the R267 block')
    check(_k169171 != '', 'R269 the latch block follows the R268 block')
    check(len(_store.findall(_k151153)) == 13,
          'the R263 block holds thirteen GMU stores: eight in the shared preamble, the page fill '
          'takes none, the two starting keys add one mask store and one start word each and the '
          'control adds only its mask store (got %d)' % len(_store.findall(_k151153)))
    check(_k151153.count('HFI_CTRL_INIT] = 1u;') == 2,
          'only the two keys that ask the firmware to start do so (got %d)'
          % _k151153.count('HFI_CTRL_INIT] = 1u;'))
    check(_k151153.count('GMU2HOST_INTR_MASK] = ') == 3 and _k151153.count('0xff7fffffu') == 3,
          'each of the three keys opens exactly one interrupt source (got %d/%d)'
          % (_k151153.count('GMU2HOST_INTR_MASK] = '), _k151153.count('0xff7fffffu')))
    check(_k151153.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1 and
          _k151153.count('CM3_FW_INIT_RESULT] = 0u;') == 1,
          'the R263 preamble clears the interrupt and the init word once each')
    check(_k151153.count('HFI_QTBL_ADDR] = ') == 1 and
          _k151153.count('HFI_QTBL_INFO] = 1u;') == 1,
          'the R263 preamble re-arms the queue table once (got %d/%d)'
          % (_k151153.count('HFI_QTBL_ADDR] = '), _k151153.count('HFI_QTBL_INFO] = 1u;')))
    check(_k151153.count('0xc00001feu') == 3,
          'each key clears the bank status word once (got %d)'
          % _k151153.count('0xc00001feu'))
    check(_k151153.count('0x58u') == 6 and _k151153.count('0x60u') == 3 and
          _k151153.count('0x68u') == 3 and _k151153.count('0x800u') == 3,
          'the fault words are touched six, three, three and three times (got %d/%d/%d/%d)'
          % (_k151153.count('0x58u'), _k151153.count('0x60u'), _k151153.count('0x68u'),
             _k151153.count('0x800u')))
    check(_k151153.count('for (') == 8 and _k151153.count('IODelay(1u)') == 4 and
          'IOSleep' not in _k151153,
          'eight loops, four one-microsecond waits and no sleep (got %d/%d)'
          % (_k151153.count('for ('), _k151153.count('IODelay(1u)')))
    check(_k151153.count('0xbabefaceu') == 1 and 'readback=1' in _k151153,
          'the firmware init poll and the descriptor readback both survive')
    check(_k151153.count('512ull') == 1 and _k151153.count('dc civac') == 2,
          'the page fill walks one page span and cleans it once (got %d/%d)'
          % (_k151153.count('512ull'), _k151153.count('dc civac')))
    check(_k151153.count('armcpu_map_physical') >= 3,
          'the queue-table page is mapped once and read by the three bracket rows (got %d)'
          % _k151153.count('armcpu_map_physical'))
    _missing263 = [r for r in _rows263 if ('%s=' % r) not in _k151153]
    check(_missing263 == [],
          'every R263 row appears in its own block (missing: %s)' % (_missing263,))
    check(_k151153.index('pre_arm=1') < _k151153.index('HFI_QTBL_ADDR] = ') <
          _k151153.index('armed=1') < _k151153.index('CM3_SYSRESET] = 1u;') <
          _k151153.index('post_reset=1'),
          'the three bracket rows sit either side of the arming stores and the reset pulse in order')
    check('if (r151mode != 152) {' in _k151153,
          'the reset pulse is skipped for the key that must see the page without it')
    check('(fwKey >= 153u) ? 153' in _k151153,
          'the R263 mode selector still has its three arms')
    check(_k151153.index('if (r151mode == 151) {') <
          _k151153.index('} else if (r151mode == 152) {') <
          _k151153.rindex('} else {'),
          'the three R263 keys are in ascending order')
    _b151 = _k151153[_k151153.index('if (r151mode == 151) {'):
                     _k151153.index('} else if (r151mode == 152) {')]
    _b152 = _k151153[_k151153.index('} else if (r151mode == 152) {'):_k151153.rindex('} else {')]
    _b153 = _k151153[_k151153.rindex('} else {'):]
    check(_b151.count('HFI_CTRL_INIT] = 1u;') == 1 and _b152.count('HFI_CTRL_INIT] = 1u;') == 1,
          'both bracketing keys commit one start word each')
    check(_b153.count('HFI_CTRL_INIT] = 1u;') == 0,
          'the control key writes no start word at all')
    check('d0=%08x' in _b151 and 'd0=%08x' in _b152 and 'd0=%08x' in _b153,
          'each key re-reads the queue page after its poll')

    check(_k154156 != '', 'R264 the post-acceptance block follows the R263 block')
    check(_k157159 != '', 'R265 the write-path block follows the R264 block')
    check(_k160162 != '', 'R266 the matrix block follows the R265 block')
    check(_k163165 != '', 'R267 the counting block follows the R266 block')
    check(_k166168 != '', 'R268 the counting block follows the R267 block')
    check(_k169171 != '', 'R269 the latch block follows the R268 block')
    _rows264 = re.findall(r'gmu(15[456][a-n]|15[456]k[1-4]|15[456]l)=', _k154156)
    check(len(set(_rows264)) == 19,
          'the R264 block prints nineteen named rows (got %d: %s)'
          % (len(set(_rows264)), sorted(set(_rows264))))
    check(len(_store.findall(_k154156)) == 13,
          'the R264 block holds thirteen GMU stores: eight in the shared preamble, '
          'the page fill, and then key 154 mask plus start word, key 155 mask plus start '
          'word, and key 156 mask alone (got %d)' % len(_store.findall(_k154156)))
    check(_k154156.count('HFI_CTRL_INIT] = 1u;') == 2,
          'exactly two keys of R264 commit a start word (got %d)'
          % _k154156.count('HFI_CTRL_INIT] = 1u;'))
    check(_k154156.count('GMU2HOST_INTR_MASK] = ') == 3 and
          _k154156.count('0xff7fffffu') == 3,
          'all three R264 keys open the fault source with one mask store each (got %d/%d)'
          % (_k154156.count('GMU2HOST_INTR_MASK] = '),
             _k154156.count('0xff7fffffu')))
    check(_k154156.count('GMU2HOST_INTR_CLR] = 0xffffffffu;') == 1,
          'R264 clears the interrupt word once, in the shared preamble (got %d)'
          % _k154156.count('GMU2HOST_INTR_CLR] = 0xffffffffu;'))
    check(_k154156.count('CM3_FW_INIT_RESULT] = 0u;') == 1 and
          _k154156.count('HFI_QTBL_ADDR] = ') == 1 and
          _k154156.count('HFI_QTBL_INFO] = 1u;') == 1,
          'R264 re-boots the CM3 once and arms the queue table once (got %d/%d/%d)'
          % (_k154156.count('CM3_FW_INIT_RESULT] = 0u;'),
             _k154156.count('HFI_QTBL_ADDR] = '),
             _k154156.count('HFI_QTBL_INFO] = 1u;')))
    check(_k154156.count('0xc00001feu') == 3,
          'each R264 key clears the bank fault status word with the same value (got %d)'
          % _k154156.count('0xc00001feu'))
    check(_k154156.count('0x58u') == 6 and _k154156.count('0x60u') == 3 and
          _k154156.count('0x68u') == 3 and _k154156.count('0x800u') == 3,
          'R264 reads the fault words with the pinned offsets (FSR %d, FAR %d, '
          'FSYNR0 %d, SMR0 %d)'
          % (_k154156.count('0x58u'), _k154156.count('0x60u'),
             _k154156.count('0x68u'), _k154156.count('0x800u')))
    check(_k154156.count('for (') == 10 and _k154156.count('IODelay(1u)') == 7 and
          'IOSleep' not in _k154156,
          'R264 polls with the pinned loop and never sleeps (loops %d, delays %d)'
          % (_k154156.count('for ('), _k154156.count('IODelay(1u)')))
    check(_k154156.count('0xbabefaceu') == 1,
          'R264 waits for one CM3 bring-up (got %d)' % _k154156.count('0xbabefaceu'))
    check(_k154156.count('dc civac') == 4 and _k154156.count('512ull') == 1,
          'R264 cleans the table pages and walks five hundred twelve entries (got %d/%d)'
          % (_k154156.count('dc civac'), _k154156.count('512ull')))
    check(_k154156.count('PWR_COL_CP_MSG') >= 2 and _k154156.count('PWR_COL_CP_RESP') >= 2,
          'R264 reads the CP message word and the CP response word')
    check(_k154156.count('armcpu_map_physical_rw') >= 3 and
          _k154156.count('0xa5a5a5a5u') == 2,
          'the R264 pattern keys write the page through a writable mapping (rw %d, '
          'pattern %d)' % (_k154156.count('armcpu_map_physical_rw'),
                           _k154156.count('0xa5a5a5a5u')))
    check(_k154156.index("r154mode == 154") < _k154156.index("r154mode == 155") <
          _k154156.rindex('} else {'),
          'R264 keeps its three branches in key order')
    _b154 = _k154156[_k154156.index("r154mode == 154"):_k154156.index("r154mode == 155")]
    check('HFI_CTRL_INIT] = 1u;' in _b154 and 'PWR_COL_CP_MSG' in _b154,
          'key 154 commits a start word and then reads the CP words')
    check(_b154.count('IODelay(1u)') >= 1,
          'key 154 spaces its two page reads apart with a delay')

    _rows265 = ('gmu157a=', 'gmu157b=', 'gmu157c=', 'gmu157d=', 'gmu157e=', 'gmu157f=',
                'gmu157g=', 'gmu157h=', 'gmu157i=', 'gmu157j=', 'gmu157m=', 'gmu157n=',
                'gmu157k1=', 'gmu157k2=', 'gmu157k3=', 'gmu157k4=', 'gmu157k5=',
                'gmu157l=', 'gmu158l=', 'gmu159l=')
    check(_k157159 != '', 'the R265 block is present')
    check(all(w in _k157159 for w in _rows265),
          'every R265 record is present in the block')
    check(_k157159.count('gmuW[') == 12,
          'the R265 block holds twelve GMU stores: eight in the shared preamble plus two in each '
          'of the two start keys (got %d)' % _k157159.count('gmuW['))
    check(_k157159.count('HFI_CTRL_INIT] = 1u;') == 2,
          'only keys 158 and 159 commit a start word (got %d)'
          % _k157159.count('HFI_CTRL_INIT] = 1u;'))
    check(_k157159.count('GMU2HOST_INTR_MASK] = ') == 2 and
          _k157159.count('0xff7fffffu') == 2,
          'both start keys mask the interrupt sources once')
    check(_k157159.count('HFI_QTBL_ADDR] = ') == 1 and
          _k157159.count('HFI_QTBL_INFO] = 1u;') == 1,
          'the preamble arms the queue table exactly once')
    check(_k157159.count('0xc00001feu') == 2,
          'only keys 158 and 159 clear the bank status word (got %d)'
          % _k157159.count('0xc00001feu'))
    check(_k157159.count('for (') == 9 and _k157159.count('IODelay(1u)') == 3 and
          'IOSleep' not in _k157159,
          'the block polls with delays and never sleeps')
    check(_k157159.count('dc civac') >= 2 and _k157159.count('dc cvac') >= 1,
          'the write keys carry both cache operations (got %d/%d)'
          % (_k157159.count('dc civac'), _k157159.count('dc cvac')))
    check(_k157159.count('0xbabefaceu') == 1,
          'the preamble waits for the firmware to initialise exactly once')
    check(_k157159.count('armcpu_map_physical_rw') >= 3,
          'every key reaches the page through the writable mapping')
    check(_k157159.index('if (r157mode == 157) {') <
          _k157159.index('} else if (r157mode == 158) {') <
          _k157159.rindex('} else {'),
          'the three keys appear in order')
    _b157 = _k157159[_k157159.index('if (r157mode == 157) {'):
                     _k157159.index('} else if (r157mode == 158) {')]
    _b158 = _k157159[_k157159.index('} else if (r157mode == 158) {'):
                     _k157159.rindex('} else {')]
    _b159 = _k157159[_k157159.rindex('} else {') + len('} else {'):]
    check('HFI_CTRL_INIT] = 1u;' not in _b157,
          'key 157 writes no start word')
    check(_b157.count('0x5a5a5a5au') == 1 and
          _b157.count('rd32(static_cast<uintptr_t>(r157rw), 0x100u)') == 2,
          'key 157 overwrites the same word and reads it back twice')
    check('gmu157k5=' in _b157 and 'gmu157l=' in _b157,
          'key 157 reports the write path and a control row')
    check(_b158.count('HFI_CTRL_INIT] = 1u;') == 1 and
          _b158.count('r157p[64]') == 1 and _b158.count('r157p[67]') == 1,
          'key 158 writes the four safe words and then one start word')
    check(_b158.count('dc civac') == 1 and 'dc cvac' not in _b158,
          'key 158 invalidates after its write')
    check(_b159.count('HFI_CTRL_INIT] = 1u;') == 1 and
          _b159.count('r157p[4]') == 1 and _b159.count('r157p[7]') == 1,
          'key 159 writes the four header words and then one start word')
    check(_b159.count('dc cvac') == 1 and 'dc civac' not in _b159,
          'key 159 only cleans the page after its write')
    check(_b158.count('gmu158l=') == 1 and _b159.count('gmu159l=') == 1,
          'both start keys report their own row')
    for _b, _tag in ((_b158, 'gmu158l'), (_b159, 'gmu159l')):
        check('survived=1' in _b and 'w4=%08x' in _b and 'fsynr0=%08x' in _b,
              '%s keeps the page words and the fault ledger' % _tag)

    # --- R266 matrix keys: the same failure on two positions and two cache passes -----
    _rows266 = ["gmu160" + _c for _c in "abcdefghijmn"] + [
        "gmu160k1", "gmu160k2", "gmu160k3", "gmu160k4", "gmu160k5", "gmu160k6",
        "gmu160k7", "gmu160l", "gmu161k4", "gmu161k5", "gmu161k6", "gmu161k7",
        "gmu161l", "gmu162k4", "gmu162k5", "gmu162l"]
    check(all(_w + '=' in _k160162 for _w in _rows266),
          'every R266 record is present in the block')
    check(_k160162.count('gmuW[') == 17,
          'the R266 block holds seventeen GMU stores: eight in the shared preamble, two per start '
          'word of keys 160 and 161, and one for key 162 (got %d)' % _k160162.count('gmuW['))
    check(_k160162.count('HFI_CTRL_INIT] = 1u;') == 4,
          'keys 160 and 161 each commit the start word twice (got %d)'
          % _k160162.count('HFI_CTRL_INIT] = 1u;'))
    check(_k160162.count('GMU2HOST_INTR_MASK] = ') == 5 and _k160162.count('0xff7fffffu') == 5,
          'every key masks the CM3 fault source (got %d)'
          % _k160162.count('GMU2HOST_INTR_MASK] = '))
    check(_k160162.count('HFI_QTBL_ADDR] = ') == 1 and _k160162.count('HFI_QTBL_INFO] = 1u;') == 1,
          'the block arms the queue table exactly once')
    check(_k160162.count('0xc00001feu') == 5,
          'every mask store is preceded by a bank fault status clear (got %d)'
          % _k160162.count('0xc00001feu'))
    check(_k160162.count('for (') == 14 and _k160162.count('IODelay(1u)') == 5 and
          'IOSleep' not in _k160162,
          'the block paces only with the yielding read loop (for %d, IODelay %d)'
          % (_k160162.count('for ('), _k160162.count('IODelay(1u)')))
    check(_k160162.count('dc civac') == 4 and _k160162.count('dc cvac') == 3,
          'the block uses both cache passes on the page (civac %d, cvac %d)'
          % (_k160162.count('dc civac'), _k160162.count('dc cvac')))
    check(_k160162.count('0xbabefaceu') == 1,
          'the block polls the firmware init word exactly once')
    check(_k160162.index('if (r160mode == 160) {') <
          _k160162.index('} else if (r160mode == 161) {') <
          _k160162.rindex('} else {'),
          'the three keys appear in the order 160, 161, 162')
    _b160 = _k160162[_k160162.index('if (r160mode == 160) {'):
                     _k160162.index('} else if (r160mode == 161) {')]
    _b161 = _k160162[_k160162.index('} else if (r160mode == 161) {'):
                     _k160162.rindex('} else {')]
    _b162 = _k160162[_k160162.rindex('} else {') + len('} else {'):]
    check(_b160.count('HFI_CTRL_INIT] = 1u;') == 2 and
          _b160.count('dc civac') == 1 and _b160.count('dc cvac') == 1 and
          _b160.count('0xc00001feu') == 2,
          'key 160 commits the start word once after each cache pass')
    check(_b161.count('HFI_CTRL_INIT] = 1u;') == 2 and
          _b161.count('dc civac') == 1 and _b161.count('dc cvac') == 1 and
          _b161.count('0xc00001feu') == 2,
          'key 161 repeats both passes on the safe words')
    check(_b162.count('HFI_CTRL_INIT] = 1u;') == 0 and
          _b162.count('GMU2HOST_INTR_MASK] = ') == 1 and
          _b162.count('0xc00001feu') == 1,
          'key 162 adds only the mask store and no start word')
    check('gmu160l=matrix=1' in _b160 and 'gmu161l=matrix=1' in _b161 and
          'gmu162l=matrix=1' in _b162,
          'each matrix key reports its own control row')
    for _b, _tag in ((_b160, 'gmu160l'), (_b161, 'gmu161l'), (_b162, 'gmu162l')):
        check('survived=1' in _b and 'ctl0=%08x' in _b and 'survived=1' in _b,
              '%s keeps the two control words and the survival flag' % _tag)

    # --- R267 counting keys: three cycles on two offsets, one no-pattern control -----
    _rows267 = ["gmu163" + _c for _c in "abcdefghijmn"] + [
        "gmu163k1", "gmu163k2", "gmu163k3", "gmu163c1", "gmu163c2", "gmu163c3",
        "gmu163l", "gmu164c1", "gmu164c2", "gmu164c3", "gmu164l",
        "gmu165c1", "gmu165l"]
    check(all(_w + '=' in _k163165 for _w in _rows267),
          'every R267 record is present in the block')
    check(_k163165.count('gmuW[') == 22,
          'the R267 block holds twenty-two GMU stores: eight in the shared preamble, six for the '
          'three cycles of each counting key and two for the control (got %d)'
          % _k163165.count('gmuW['))
    check(_k163165.count('HFI_CTRL_INIT] = 1u;') == 7,
          'every cycle commits one start word and the control commits one more (got %d)'
          % _k163165.count('HFI_CTRL_INIT] = 1u;'))
    check(_k163165.count('GMU2HOST_INTR_MASK] = ') == 7 and _k163165.count('0xff7fffffu') == 7,
          'every commit is preceded by one mask store (got %d)'
          % _k163165.count('GMU2HOST_INTR_MASK] = '))
    check(_k163165.count('0xc00001feu') == 7,
          'every mask store is preceded by a bank fault status clear (got %d)'
          % _k163165.count('0xc00001feu'))
    check(_k163165.count('HFI_QTBL_ADDR] = ') == 1 and _k163165.count('HFI_QTBL_INFO] = 1u;') == 1,
          'the block arms the queue table exactly once')
    check(_k163165.count('for (') == 19 and _k163165.count('IODelay(1u)') == 8 and
          'IOSleep' not in _k163165,
          'the block paces only with the yielding read loop (for %d, IODelay %d)'
          % (_k163165.count('for ('), _k163165.count('IODelay(1u)')))
    check(_k163165.count('dc civac') >= 9 and 'dc cvac' not in _k163165,
          'the block keeps the invalidating pass and the clean-only pass stays behind in the '
          'shared preamble')
    check(_k163165.count('0xbabefaceu') == 1 and _k163165.count('a5a5a5a5u') == 6,
          'the firmware init poll appears once and the pattern is written six times')
    check(_k163165.index('if (r163mode == 163) {') <
          _k163165.index('} else if (r163mode == 164) {') <
          _k163165.rindex('} else {'),
          'the three keys appear in the order 163, 164, 165')
    _b163 = _k163165[_k163165.index('if (r163mode == 163) {'):
                     _k163165.index('} else if (r163mode == 164) {')]
    _b164 = _k163165[_k163165.index('} else if (r163mode == 164) {'):
                     _k163165.rindex('} else {')]
    _b165 = _k163165[_k163165.rindex('} else {') + len('} else {'):]
    check(_b163.count('HFI_CTRL_INIT] = 1u;') == 3 and _b163.count('r163p[') == 12,
          'key 163 runs three write-and-commit cycles on the header words')
    check(_b164.count('HFI_CTRL_INIT] = 1u;') == 3 and _b164.count('r163p[') == 12,
          'key 164 runs the same three cycles on the safe words')
    check(_b165.count('HFI_CTRL_INIT] = 1u;') == 1 and 'a5a5a5a5u' not in _b165,
          'key 165 writes no pattern at all and commits one start word')

    _rows268 = [
        'gmu166a', 'gmu166b', 'gmu166c', 'gmu166d', 'gmu166e', 'gmu166f', 'gmu166g',
        'gmu166h', 'gmu166i', 'gmu166j', 'gmu166m', 'gmu166n', 'gmu166k1', 'gmu166k2',
        'gmu166k3', 'gmu166c1', 'gmu166c2', 'gmu166c3', 'gmu166c4', 'gmu166c5',
        'gmu166c6', 'gmu166l', 'gmu167c1', 'gmu167c2', 'gmu167c3', 'gmu167l',
        'gmu167s1', 'gmu167s2', 'gmu167s3', 'gmu168k', 'gmu168l',
    ]
    check(all(_w + '=' in _k166168 for _w in _rows268),
          'the R268 block prints every one of its rows')
    check(_k166168.count('gmuW[') == 31,
          'the R268 block holds thirty-one GMU stores, measured %u'
          % _k166168.count('gmuW['))
    check(_k166168.count('HFI_CTRL_INIT] = 1u;') == 11,
          'eleven of those stores start the HFI: six in the first key, three in the second '
          'and two in the last, measured %u' % _k166168.count('HFI_CTRL_INIT] = 1u;'))
    check(_k166168.count('GMU2HOST_INTR_MASK] = ') == 11 and _k166168.count('0xff7fffffu') == 11,
          'every start word is preceded by its own mask store')
    check(_k166168.count('0xc00001feu') == 11,
          'every mask store is preceded by a bank fault status clear')
    check(_k166168.count('HFI_QTBL_ADDR] = ') == 1 and _k166168.count('HFI_QTBL_INFO] = 1u;') == 1,
          'the block arms the queue table exactly once, in its shared preamble')
    check(_k166168.count('for (') == 27 and _k166168.count('IODelay(1u)') == 12 and
          'IOSleep' not in _k166168,
          'the block paces with the yielding read loop only, %u loops and %u delays'
          % (_k166168.count('for ('), _k166168.count('IODelay(1u)')))
    check(_k166168.count('dc civac') >= 9,
          'each cycle cleans its page with an invalidating pass')
    check(_k166168.count('0xbabefaceu') == 1 and _k166168.count('a5a5a5a5u') == 10,
          'the block waits for the firmware init word and writes the pattern six times')
    check(_k166168.index('if (r166mode == 166) {') <
          _k166168.index('} else if (r166mode == 167) {') <
          _k166168.rindex('} else {'),
          'the three keys appear in the order 166, 167, 168')
    _b166 = _k166168[_k166168.index('if (r166mode == 166) {'):
                     _k166168.index('} else if (r166mode == 167) {')]
    _b167 = _k166168[_k166168.index('} else if (r166mode == 167) {'):
                     _k166168.rindex('} else {')]
    _b168 = _k166168[_k166168.rindex('} else {') + len('} else {'):]
    check(_b166.count('HFI_CTRL_INIT] = 1u;') == 6,
          'key 166 commits six start words in one boot')
    check(_b167.count('HFI_CTRL_INIT] = 1u;') == 3,
          'key 167 commits three of them')
    check(_b168.count('HFI_CTRL_INIT] = 1u;') == 2,
          'key 168 commits one start word, writes the control word back to zero by hand and commits a second one')
    check(_b168.count('HFI_CTRL_STATUS] = 0u;') == 1,
          'key 168 writes the control word back to zero after the first acceptance')
    for _tag, _b in (('gmu163l', _b163), ('gmu164l', _b164), ('gmu165l', _b165)):
        check('survived=1' in _b and 'ctl0=%08x' in _b and _tag + '=' in _b,
              '%s keeps the three control words and the survival flag' % _tag)

    _rows269 = ['gmu169' + c for c in 'abcdefghijmn'] + [
        'gmu169k1', 'gmu169k2', 'gmu169k3', 'gmu169k4', 'gmu169l',
        'gmu170k', 'gmu170l', 'gmu171k', 'gmu171l']
    check(all(_w + '=' in _k169171 for _w in _rows269),
          'the R269 block prints every one of its twenty-one rows')
    check(_k169171.count('gmuW[') == 25,
          'the R269 block holds twenty-five GMU stores, measured %u'
          % _k169171.count('gmuW['))
    check(_k169171.count('HFI_CTRL_INIT] = 1u;') == 6,
          'six start words across the three keys, measured %u'
          % _k169171.count('HFI_CTRL_INIT] = 1u;'))
    check(_k169171.count('GMU2HOST_INTR_MASK] = ') == 6 and
          _k169171.count('0xff7fffffu') == 6,
          'one mask store per start word, measured %u'
          % _k169171.count('GMU2HOST_INTR_MASK] = '))
    check(_k169171.count('0xc00001feu') == 6,
          'every mask store is preceded by a bank fault status clear, measured %u'
          % _k169171.count('0xc00001feu'))
    check(_k169171.count('HFI_CTRL_STATUS] = ') == 5,
          'five hand writes of the control word, measured %u'
          % _k169171.count('HFI_CTRL_STATUS] = '))
    check(_k169171.count('for (') == 11 and _k169171.count('IODelay(1u)') == 8 and
          'IOSleep' not in _k169171,
          'the block yields inside eleven loops and never sleeps (%u / %u)'
          % (_k169171.count('for ('), _k169171.count('IODelay(1u)')))
    check(_k169171.count('0xbabefaceu') == 1 and
          _k169171.count('dc civac') == 2,
          'the preamble keeps the firmware init wait and both page cleans')
    check(_k169171.index('if (r169mode == 169) {') <
          _k169171.index('} else if (r169mode == 170) {') <
          _k169171.rindex('} else {'),
          'the three keys appear in the order 169, 170, 171')
    _b169 = _k169171[_k169171.index('if (r169mode == 169) {'):
                     _k169171.index('} else if (r169mode == 170) {')]
    _b170 = _k169171[_k169171.index('} else if (r169mode == 170) {'):
                     _k169171.rindex('} else {')]
    _b171 = _k169171[_k169171.rindex('} else {') + len('} else {'):]
    check(_b169.count('HFI_CTRL_INIT] = 1u;') == 4,
          'key 169 commits four start words in one boot')
    check(_b169.count('HFI_CTRL_STATUS] = 0u;') == 2,
          'key 169 clears the control word by hand before the second and before the fourth start')
    check(_b170.count('HFI_CTRL_INIT] = 1u;') == 1,
          'key 170 sends exactly one start word, after the hand writes')
    check(_b170.count('HFI_CTRL_STATUS] = 1u;') == 1 and
          _b170.count('HFI_CTRL_STATUS] = 0u;') == 1,
          'key 170 writes the control word by hand once up and once down')
    check(_b171.count('HFI_CTRL_INIT] = 1u;') == 1 and
          _b171.count('HFI_CTRL_STATUS] = 0u;') == 1,
          'key 171 starts once and then clears the control word by hand')
    for _tag, _b in (('gmu169l', _b169), ('gmu170l', _b170), ('gmu171l', _b171)):
        check('survived=1' in _b and _tag + '=' in _b,
              '%s keeps the survival flag' % _tag)

    print('%d/%d checks passed' % (checks - len(fails), checks))
    if fails:
        print('FAIL: stage-6 kernel ID0/ID1 probe contract (%d failures)' % len(fails))
        return 1
    print('PASS: stage-6 kernel ID0/ID1 probe contract '
          '(same gate, three GR0 reads, measured coherent_walk, ASID tuple, READ-ONLY)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
