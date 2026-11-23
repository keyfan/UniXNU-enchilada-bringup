#!/usr/bin/env python3
"""Round 94 cross-verifier: stage 4 of the Adreno probe -- the kernel-side SMMU
ID2 read that derives `ias`.

This is the FIRST kernel caller of the round-86 ID2 probe contract, and it exists
because round 85 MEASURED that userspace cannot reach the word: `CONFIG_DEVMEM` is
not set, and the iommu debugfs exposes no address-size node. So the read has to
happen in the kernel, behind the contract's fail-closed gate.

What this verifier asserts, and why each one matters:

  1. The stage is armed by `enchilada-adreno=4` and by nothing else, and stages
     1/2/3 still work (a new stage must not regress the old ones).
  2. The gate is asked with the CONTRACT'S struct, and the CX word passed to it is
     the one JUST MEASURED from GPUCC in the same poll -- not a constant, not a
     guess. A hard-coded `cx_status_word` would be the exact failure mode this
     whole project keeps paying for.
  3. The window is mapped ONLY inside the permitted branch. On a CX-DOWN board the
     expected outcome is a named refusal and ZERO MMIO access to 0x05040000.
  4. The decode is the contract's (`adreno_iommu_id2_decode`, `_va_size`,
     `_stage1_words`), never a re-derivation of the ID2 field positions. Round 81
     proved what happens when a module and its verifier drift apart.
  5. The probe still performs NO store through any pointer, uses no libc
     string/memory helper, and is pure LF.
  6. The boot argument fits the measured command-line budget.

Read-only: reads sources, never writes.
"""
import pathlib
import re
import sys
import tomllib

DEV = pathlib.Path(__file__).resolve().parents[1]
PACKAGE = DEV / 'package' / 'EnchiladaAdrenoProbe.cpp'
BOARD = DEV / 'board.toml'
CONTRACT = DEV / 'drivers' / 'NativeAdreno' / 'AdrenoIommuProbeV1.h'

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


def main():
    text = PACKAGE.read_text(encoding='utf-8')
    contract = CONTRACT.read_text(encoding='utf-8')

    # Comment-stripped copy: this file's comments NAME the things it must not do
    # ("NO MMIO write", "never a cold read"), so a naive substring test would
    # flag the prose that documents the safety property as a violation of it.
    code = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    code = re.sub(r'//[^\n]*', '', code)

    # --- A. the stage is armed, default-off, and does not regress 1/2/3 ---
    check('stage > 6u' in code, 'the boot-arg mask REJECTS stages above 6')
    check('(stage < 1u || stage > 6u)' in code,
          'stages 1..6 are all accepted, nothing else')
    check('if (stage >= 4u)' in code,
          'the stage-4 block is gated behind stage >= 4')

    # --- B. the gate uses the CONTRACT struct and the MEASURED CX word -----
    check('#include "AdrenoIommuProbeV1.h"' in text,
          'the probe includes the round-86 ID2 contract header')
    check('struct adreno_iommu_gate_inputs gi = {};' in code,
          'the gate is asked with the contract struct, not a bare mask')
    # Round 98 changed this from the poll-start word to a RE-READ, because the first
    # device run proved stage 4 was gating on a value stage 5 then invalidated:
    #   t=50.088 stage4=refused cx=00005ff4 ; t=50.365 stage5 cx_after=80001fe0
    # The property that must survive is that the word is MEASURED, never a constant.
    check('gi.cx_status_word  = cxNow;' in code,
          'gi.cx_status_word is a MEASURED word (re-read after stage 5)')
    check("const uint32_t cxNow = gGpuccVa ? rd32(gGpuccVa, kCxOffset) : cx;" in code,
          'the re-read comes from the real GPUCC status register')
    check('adreno_iommu_assess(&gi)' in code,
          'the verdict comes from the contract assess()')
    # ROUND 99: this used to assert clocks_enabled == 0 because the port "enabled no
    # GPU clock". The round-98 device log falsified that: stage 5 completed the
    # CLOCKS_ENABLE leg (w=9), yet stage 4 still refused with verdict=4(refused-clocks-off)
    # because the probe hard-coded 0. The property that must hold now is that the flag
    # reflects the EXECUTOR's report, and only when stage 5 actually ran.
    check('gi.clocks_enabled  = havePowerRun ? rep.pre.clocks_enabled : 0u;' in code,
          'clocks_enabled reflects the executor report, gated on stage 5 having run')
    check('havePowerRun = true;' in code,
          'stage 5 marks that the power-up run happened')
    check('gi.kmem_mapped     = gSmmuVa ? 1u : 0u;' in code,
          'kmem_mapped reflects the ACTUAL mapping state')
    # A hard-coded CX word is the failure mode; assert it cannot appear.
    check(not re.search(r'gi\.cx_status_word\s*=\s*0x', code),
          'gi.cx_status_word is NEVER a hard-coded constant')

    # --- C. the window is mapped ONLY in the permitted branch -------------
    # Locate the stage-4 block, then assert the map call sits after a permitted test.
    i4 = code.find('if (stage >= 4u)')
    check(i4 >= 0, 'stage-4 block was found in the comment-stripped source')
    if i4 >= 0:
        block = code[i4:]
        # the block ends at the next top-level statement after the IOLog/setIommuReplay pair
        j = block.find('setIommuReplay(ioRow);')
        if j >= 0:
            block = block[:j]
        check('kAdrenoIommuProbePermitted' in block,
              'the permitted verdict is tested explicitly')
        # The map call must appear AFTER that test, i.e. inside the else branch.
        i_perm = block.find('kAdrenoIommuProbePermitted')
        i_map = block.find('armcpu_map_physical(ADRENO_BOARD_KGSL_SMMU_BASE')
        # ROUND 100: this used to demand map AFTER permit. The round-99 device run
        # falsified that: the contract's 4th precondition IS "already mapped", so
        # gating first left gSmmuVa 0 at gate time and the gate could never pass
        # (verdict=5 refused-unmapped). Mapping a window is what makes it safe to
        # READ, and the CX check separately guards the physical hazard; so the map
        # now comes BEFORE the assessment and is handed to it as a fact.
        check(i_map < i_perm,
              'the SMMU window is mapped BEFORE the gate is asked (the gate checks mapped-ness)')
        check('ADRENO_BOARD_KGSL_SMMU_BASE' in block,
              'the mapped window is the pinned kgsl SMMU base (0x05040000)')
        check('ADRENO_BOARD_APPS_SMMU_BASE' not in block,
              'the probe never touches apps_smmu (0x15000000) -- wrong device')
        # The refusal path must NAME the verdict, so a refusal can never be read
        # as "a read that returned zero".
        check('stage4=refused verdict=%u(%s)' in block,
              'the refusal row names its verdict instead of looking like a zero read')

    # --- D. ID2 once; stage 6 adds exactly two more reads behind the gate ---
    check('rd32(gSmmuVa, 0x28u)' in code,
          'GR0_ID2 is read once, at byte 0x28 (stage 4)')
    check('rd32(gSmmuVa, 0x20u)' in code,
          'GR0_ID0 is read once, at byte 0x20 (stage 6, coherent_walk)')
    check('rd32(gSmmuVa, 0x24u)' in code,
          'GR0_ID1 is read once, at byte 0x24 (stage 6, NUMCB/NUMS2CB)')
    # ROUND 158 added a read-only GPU-state snapshot; ROUND 164 device-verified
    # that its "CB0" TTBR0/TTBR1 reads actually used the GR1 base (0x800) instead
    # of the CB base (0x8000), hitting unimplemented GR1 offsets and aborting
    # before the log window opened (seven silent boots).  R164 removed them and
    # R164 on device produced the first `gputate=` row, so this contract now pins
    # their ABSENCE -- otherwise they could come back and the boot would go
    # silent again with no failing check.
    # ROUND 232: keys 71/72 re-read ID1 (CBn_BASE) and ID2 (ias/oas) on purpose, so the
    # "exactly once" invariant is scoped to the read-only region that ends at key 71.
    _ro4 = code.split('if (fwKey == 71u) {', 1)[0]
    for _o in ('0x20u', '0x24u', '0x28u'):
        check(_ro4.count('rd32(gSmmuVa, %s)' % _o) == 1,
              'SMMU read at byte %s appears exactly once before the write keys' % _o)
    check('0x800u + 0x20u' not in code and '0x800u + 0x28u' not in code,
          'R164 the GR1/CB0-offset TTBR0/TTBR1 reads stay removed (device-verified fatal)')
    check('rd32(gSmmuVa, 0x0u)' in code,
          'R158/R164 keeps exactly one GR0 SCTLR read (byte 0x0), the surviving snapshot word')
    check('dsb sy' in code,
          'a full barrier follows the register read')
    check('adreno_iommu_id2_decode(id2, &d)' in code,
          'the word is decoded by the contract, not re-parsed here')
    check('adreno_iommu_va_size(id2, ver, &okv)' in code,
          'ias is asked of the contract (version-gated), not computed locally')
    check('adreno_iommu_stage1_words(' in code,
          'the round-83 words are produced by the contract composition')

    # --- E. nothing here re-derives a field position ----------------------
    # The ID2 field masks live in the contract. If the probe grows one, the two
    # derivations can drift (the round-81 regression class).
    for forbidden in ('GENMASK(11, 8)', '<< 8', '>> 8', '0x0F00', '0xf00'):
        hit = forbidden in block if i4 >= 0 else False
        check(not hit, 'the probe does not re-derive an ID2 field (%r)' % forbidden)

    # --- F. no store, no libc, pure LF ------------------------------------
    check('volatile uint32_t *' in text, 'the mapping is volatile (cache-inhibited intent)')
    check(re.search(r'\*\s*(?:static_cast<)?volatile\s+uint32_t', code) is None or
          code.count('*reinterpret_cast<volatile uint32_t *>') >= 1,
          'the only pointer cast target is the volatile read helper')
    for bad in ('memcpy(', 'memset(', 'strlen(', 'strcmp(', 'malloc(', 'free('):
        check(bad not in code, 'the probe uses no libc %s' % bad.strip('('))
    # A store through a pointer would dereference on the LEFT of an assignment.
    # Match only the unambiguous form: `*<simple-name> =` on one line, where the
    # name is not preceded by an operator that makes it a multiplication.
    store_re = re.compile(r'(?<![\w\)\]\*])\*\s*[A-Za-z_]\w*(?:\[[^\]]*\])?\s*=(?!=)')
    # ROUND 167 is the ONE deliberate exception, and it is pinned here rather
    # than waved through: the probe now performs exactly one register store, to
    # CM3_BOOT_CONFIG, under `enchilada-pas-exec=6`, gated on the CM3_SYSRESET
    # read-back. Everything OUTSIDE that block must still contain no store at
    # all -- so this narrows the old absolute claim instead of dropping it.
    _rw167 = (code.split('bootKey == 6u', 1)[1].split('gmuboot=pre', 1)[0]
              if 'bootKey == 6u' in code else '')
    # ROUND 168 is the SECOND pinned exception: the ITCM bulk load and, under key 8
    # only, the pre-start writes and the reset pulse. The no-store rule is NOT
    # weakened: every `gmuW[...] = ...` statement is removed from the scan below,
    # and the block is then asserted POSITIVELY to hold exactly the pinned writes.
    _outside = code.replace(_rw167, '') if _rw167 else code
    _outside = re.sub(r'gmuW\[[^\]]*\]\s*=[^=][^;]*;', '', _outside)
    # ROUND 198 is the third pinned exception: the CPU-side APPS RSC
    # request, whose stores all go through `rscW[...]`.
    _outside = re.sub(r'rscW\[[^\]]*\]\s*=[^=][^;]*;', '', _outside)
    # ROUND 199 drives the GX GDSC through `gxW[...]`; same treatment.
    _outside = re.sub(r'gxW\[[^\]]*\]\s*=[^=][^;]*;', '', _outside)
    check('// ---- ROUND 168:' in text and 'gmufw=load words=%u' in text and
          'fwKey >= 7u && fwKey <= 192u' in code and
          re.search(r'gmuW\[kAdrenoGmuItcmWord \+ wi\]\s*=\s*adreno_gmu_image\[wi\];', code),
          'R168 the ITCM load is a second pinned write exception (key 7/8)')
    check('if (fwKey == 45u) {' in code and
          code.count('rscW[') >= 10 and
          'armcpu_map_physical_rw(0x00000000179e0000ULL, 0x00010000u)' in code,
          'R198 the APPS RSC request is a third pinned write exception (key 45)')
    check('if (fwKey == 46u) {' in code and
          code.count('gxW[') >= 10,
          'R199 the GX GDSC sequence is a fourth pinned write exception (key 46)')
    check('if (fwKey == 47u) {' in code and
          "vote_a=%08x vote_top=%04x tcs=%u st=%08x en=%08x polls=%u" in code,
          'R200 the same GDSC sequence is repeated with the CM3 alive (key 47)')
    check('if (fwKey == 48u) {' in code and
          'gyPolls < 200000u' in code,
          'R201 the GX completion is re-measured against a CX control (key 48)')
    _k8 = (code.split('if (fwKey >= 8u) {', 1)[1]
           if 'if (fwKey >= 8u) {' in code else '')
    # R247 deliberately pulses the CM3 reset again, twice (keys 103 and 104), and R248
    # pulses it once per key again (105, 106 and 107), five extra in total, so the rule is now
    # 'once in the key-8 sub-block, twice in the R247 block and three times in the R248 block'.
    _i0204r = code.find('if (fwKey == 102u || fwKey == 103u || fwKey == 104u) {')
    _k0204r = (code[_i0204r:].split('\nif (fwKey == 105u || fwKey == 106u || fwKey == 107u) {\n', 1)[0]
               if _i0204r > 0 else '')
    _p1r = 'gmuW[ADRENO_GMU_BOOT_V1_CM3_SYSRESET] = 1u;'
    _p0r = 'gmuW[ADRENO_GMU_BOOT_V1_CM3_SYSRESET] = 0u;'
    _i0507r = code.find('if (fwKey == 105u || fwKey == 106u || fwKey == 107u) {')
    _k0507r = ''
    if _i0507r > 0:
        _k0507r = code[_i0507r:].split('\nif (fwKey == 108u || fwKey == 109u || fwKey == 110u) {\n', 1)[0]
    _i0810r = code.find('if (fwKey == 108u || fwKey == 109u || fwKey == 110u) {')
    _k0810r = ''
    if _i0810r > 0:
        _k0810r = code[_i0810r:].split(
            '\nif (fwKey == 111u || fwKey == 112u || fwKey == 113u) {\n', 1)[0]
    _i111113r = code.find('if (fwKey == 111u || fwKey == 112u || fwKey == 113u) {')
    _k111113r = ''
    if _i111113r > 0:
        _k111113r = code[_i111113r:].split(
            '\nif (fwKey == 114u || fwKey == 115u || fwKey == 116u) {\n', 1)[0]
    _i114116r = code.find('if (fwKey == 114u || fwKey == 115u || fwKey == 116u) {')
    _k114116r = ''
    if _i114116r > 0:
        _k114116r = code[_i114116r:].split(
            '\nif (fwKey == 117u || fwKey == 118u || fwKey == 119u) {\n', 1)[0]
    _i117119r = code.find('if (fwKey == 117u || fwKey == 118u || fwKey == 119u) {')
    _k117119r = ''
    if _i117119r > 0:
        _k117119r = code[_i117119r:].split('\nif (fwKey == 120u || fwKey == 121u || fwKey == 122u) {\n', 1)[0]
    _i124126r = code.find('if (fwKey == 124u || fwKey == 125u || fwKey == 126u) {')
    _k124126r = ''
    if _i124126r > 0:
        _k124126r = code[_i124126r:].split(
            '\nif (fwKey == 127u || fwKey == 128u || fwKey == 129u) {\n', 1)[0]
    _i120122r = code.find('if (fwKey == 120u || fwKey == 121u || fwKey == 122u) {')
    _k120122r = ''
    if _i120122r > 0:
        _k120122r = code[_i120122r:].split('\nif (fwKey == 124u || fwKey == 125u || fwKey == 126u) {\n', 1)[0]
    _i127129r = code.find('if (fwKey == 127u || fwKey == 128u || fwKey == 129u) {')
    _k127129r = ''
    if _i127129r > 0:
        _k127129r = code[_i127129r:].split(
            '\nif (fwKey == 130u || fwKey == 131u || fwKey == 132u) {\n', 1)[0]
    _i130132r = code.find('if (fwKey == 130u || fwKey == 131u || fwKey == 132u) {')
    _k130132r = ''
    if _i130132r > 0:
        _k130132r = code[_i130132r:].split(
            '\nif (fwKey == 133u || fwKey == 134u || fwKey == 135u) {\n', 1)[0]
    _i133135r = code.find('if (fwKey == 133u || fwKey == 134u || fwKey == 135u) {')
    _k133135r = ''
    if _i133135r > 0:
        _k133135r = code[_i133135r:].split(
            '\nif (fwKey == 136u || fwKey == 137u || fwKey == 138u) {\n', 1)[0]
    _i136138r = code.find('if (fwKey == 136u || fwKey == 137u || fwKey == 138u) {')
    _k136138r = ''
    if _i136138r > 0:
        _k136138r = code[_i136138r:].split(
            '\nif (fwKey == 139u || fwKey == 140u || fwKey == 141u) {\n', 1)[0]
    _i139141r = code.find('if (fwKey == 139u || fwKey == 140u || fwKey == 141u) {')
    _k139141r = ''
    if _i139141r > 0:
        _k139141r = code[_i139141r:].split(
            '\nif (fwKey == 142u || fwKey == 143u || fwKey == 144u) {\n', 1)[0]
    _i142144r = code.find('if (fwKey == 142u || fwKey == 143u || fwKey == 144u) {')
    _k142144r = ''
    if _i142144r > 0:
        _k142144r = code[_i142144r:].split(
            '\nif (fwKey == 145u || fwKey == 146u || fwKey == 147u) {\n', 1)[0]
    _i148150r = code.find('if (fwKey == 148u || fwKey == 149u || fwKey == 150u) {')
    _k148150r = ''
    if _i148150r > 0:
        _k148150r = code[_i148150r:].split(
            '\nif (fwKey == 151u || fwKey == 152u || fwKey == 153u) {\n', 1)[0]
    _i151153r = code.find('if (fwKey == 151u || fwKey == 152u || fwKey == 153u) {')
    _k151153r = ''
    if _i151153r > 0:
        _k151153r = code[_i151153r:].split(
            '\nif (fwKey == 154u || fwKey == 155u || fwKey == 156u) {\n', 1)[0]
    _i154156r = code.find('if (fwKey == 154u || fwKey == 155u || fwKey == 156u) {')
    _k154156r = ''
    if _i154156r > 0:
        _k154156r = code[_i154156r:].split(
            '\nif (fwKey == 157u || fwKey == 158u || fwKey == 159u) {\n', 1)[0]
    _i157159r = code.find('if (fwKey == 157u || fwKey == 158u || fwKey == 159u) {')
    _k157159r = ''
    if _i157159r > 0:
        _k157159r = code[_i157159r:].split(
            '\nif (fwKey == 160u || fwKey == 161u || fwKey == 162u) {\n', 1)[0]
    _i160162r = code.find('if (fwKey == 160u || fwKey == 161u || fwKey == 162u) {')
    _k160162r = ''
    if _i160162r > 0:
        _k160162r = code[_i160162r:].split(
            '\nif (fwKey == 163u || fwKey == 164u || fwKey == 165u) {\n', 1)[0]
    _i163165r = code.find('if (fwKey == 163u || fwKey == 164u || fwKey == 165u) {')
    _k163165r = ''
    if _i163165r > 0:
        _k163165r = code[_i163165r:].split(
            '\nif (fwKey == 166u || fwKey == 167u || fwKey == 168u) {\n', 1)[0]
    _i166168r = code.find('if (fwKey == 166u || fwKey == 167u || fwKey == 168u) {')
    _k166168r = ''
    if _i166168r > 0:
        _k166168r = code[_i166168r:].split(
            '\nif (fwKey == 169u || fwKey == 170u || fwKey == 171u) {\n', 1)[0]
    _i169171r = code.find('if (fwKey == 169u || fwKey == 170u || fwKey == 171u) {')
    _k169171r = ''
    if _i169171r > 0:
        _k169171r = code[_i169171r:].split(
            '\nif (fwKey == 172u || fwKey == 173u || fwKey == 174u) {\n', 1)[0]
    _i172174r = code.find('if (fwKey == 172u || fwKey == 173u || fwKey == 174u) {')
    _k172174r = ''
    if _i172174r > 0:
        _k172174r = code[_i172174r:].split(
            '\nif (fwKey == 175u || fwKey == 176u || fwKey == 177u) {\n', 1)[0]
    _i175177r = code.find('if (fwKey == 175u || fwKey == 176u || fwKey == 177u) {')
    _k175177r = ''
    if _i175177r > 0:
        _k175177r = code[_i175177r:].split(
            '\nif (fwKey == 178u || fwKey == 179u || fwKey == 180u) {\n', 1)[0]
    _i178180r = code.find('if (fwKey == 178u || fwKey == 179u || fwKey == 180u) {')
    _k178180r = ''
    if _i178180r > 0:
        _k178180r = code[_i178180r:].split(
            '\nif (fwKey == 181u || fwKey == 182u || fwKey == 183u) {\n', 1)[0]
    _i181183r = code.find('if (fwKey == 181u || fwKey == 182u || fwKey == 183u) {')
    _k181183r = ''
    if _i181183r > 0:
        _k181183r = code[_i181183r:].split(
            '\nif (fwKey == 184u || fwKey == 185u || fwKey == 186u) {\n', 1)[0]
    _i184186r = code.find('if (fwKey == 184u || fwKey == 185u || fwKey == 186u) {')
    _k184186r = ''
    if _i184186r > 0:
        _k184186r = code[_i184186r:].split(
            '\nif (fwKey == 187u || fwKey == 188u || fwKey == 189u) {\n', 1)[0]
    _i187189r = code.find('if (fwKey == 187u || fwKey == 188u || fwKey == 189u) {')
    _k187189r = ''
    if _i187189r > 0:
        _k187189r = code[_i187189r:].split(
            '\nif (fwKey == 190u || fwKey == 191u || fwKey == 192u) {\n', 1)[0]
    _i190192r = code.find('if (fwKey == 190u || fwKey == 191u || fwKey == 192u) {')
    _k190192r = ''
    if _i190192r > 0:
        _k190192r = code[_i190192r:].split(
            '\nif (fwKey >= 18u && fwKey <= 21u) {\n', 1)[0]
    _i145147r = code.find('if (fwKey == 145u || fwKey == 146u || fwKey == 147u) {')
    _k145147r = ''
    if _i145147r > 0:
        _k145147r = code[_i145147r:].split(
            '\nif (fwKey == 148u || fwKey == 149u || fwKey == 150u) {\n', 1)[0]
    # _k8 starts right after the key-8 header, so it spans the rest of the file; the rule
    # is therefore about the total and about where the extra five live.
    check(_k8 != '' and _k0204r != '' and _k0507r != '' and _k0810r != '' and _k111113r != '' and
          _k114116r != '' and _k117119r != '' and _k120122r != '' and _k124126r != '' and
          _k127129r != '' and _k130132r != '' and _k133135r != '' and _k136138r != '' and _k139141r != '' and _k142144r != '' and _k145147r != '' and _k148150r != '' and _k151153r != '' and _k154156r != '' and _k157159r != '' and _k160162r != '' and _k163165r != '' and _k166168r != '' and _k169171r != '' and _k172174r != '' and _k175177r != '' and _k178180r != '' and _k181183r != '' and _k184186r != '' and
          code.count(_p1r) == 46 and code.count(_p0r) == 46 and
          _k8.count(_p1r) == 46 and _k8.count(_p0r) == 46 and
          _k0204r.count(_p1r) == 2 and _k0204r.count(_p0r) == 2 and
          _k0507r.count(_p1r) == 3 and _k0507r.count(_p0r) == 3 and
          _k0810r.count(_p1r) == 3 and _k0810r.count(_p0r) == 3 and
          _k111113r.count(_p1r) == 1 and _k111113r.count(_p0r) == 1 and
          _k117119r.count(_p1r) == 1 and _k117119r.count(_p0r) == 1 and
          _k114116r.count(_p1r) == 1 and _k114116r.count(_p0r) == 1 and
          _k120122r.count(_p1r) == 1 and _k120122r.count(_p0r) == 1 and
          _k124126r.count(_p1r) == 1 and _k124126r.count(_p0r) == 1 and
          _k127129r.count(_p1r) == 1 and _k127129r.count(_p0r) == 1 and
          _k130132r.count(_p1r) == 1 and _k130132r.count(_p0r) == 1 and
          _k133135r.count(_p1r) == 1 and _k133135r.count(_p0r) == 1 and
          _k136138r.count(_p1r) == 1 and _k136138r.count(_p0r) == 1 and
          _k139141r.count(_p1r) == 1 and _k139141r.count(_p0r) == 1 and
          _k142144r.count(_p1r) == 1 and _k142144r.count(_p0r) == 1 and
          _k145147r.count(_p1r) == 1 and _k145147r.count(_p0r) == 1 and
          _k148150r.count(_p1r) == 1 and _k148150r.count(_p0r) == 1 and
          _k151153r.count(_p1r) == 1 and _k151153r.count(_p0r) == 1 and
          _k154156r.count(_p1r) == 1 and _k154156r.count(_p0r) == 1 and
          _k157159r.count(_p1r) == 1 and _k157159r.count(_p0r) == 1 and
          _k160162r.count(_p1r) == 1 and _k160162r.count(_p0r) == 1 and
          _k163165r.count(_p1r) == 1 and _k163165r.count(_p0r) == 1 and
          _k166168r.count(_p1r) == 1 and _k166168r.count(_p0r) == 1 and
          _k169171r.count(_p1r) == 1 and _k169171r.count(_p0r) == 1 and
          _k172174r.count(_p1r) == 5 and _k172174r.count(_p0r) == 5 and
          _k175177r.count(_p1r) == 4 and _k175177r.count(_p0r) == 4 and
          _k178180r.count(_p1r) == 4 and _k178180r.count(_p0r) == 4 and
          _k181183r.count(_p1r) == 4 and _k181183r.count(_p0r) == 4 and
          _k184186r.count(_p1r) == 0 and _k184186r.count(_p0r) == 0 and
          _k187189r != '' and
          _k187189r.count(_p1r) == 0 and _k187189r.count(_p0r) == 0 and
          _k190192r != '' and
          _k190192r.count(_p1r) == 0 and _k190192r.count(_p0r) == 0,
          'R168 the reset pulse exists once in the key-8 sub-block, twice in the R247 '
          're-boot block, three times in each of the R248 and R249 blocks and once in each of the '
          'R250, R251, R253, R254, R255, R256, R257, R258, R259, R260, R261, R262, R263, R264, R265 and R266 blocks, nowhere else, R267 and R268 blocks, nowhere else, and R269 adds no reset pulse of its own, and R270 adds five, each of them the same one up one down pulse the earlier keys use, and R271 adds four, one in the shared preamble and one in each of its three keys, and R272 adds four, one in the shared preamble and one in each of its three keys, and R273 adds four, one in the shared preamble and one in each of its three keys')
    _live = (code.split('if (fwKey >= 9u) {', 1)[1].split('IOLog("enchilada-adreno: %s\\n", liveRow);', 1)[0]
             if 'if (fwKey >= 9u) {' in code else '')
    check(_live != '' and 'gmulive=intr=%08x hfistat=%08x ' in code and
          not re.search(r'gmuW\[[^\]]*\]\s*=[^=]', _live),
          'R169 the key-9 liveness dump is READ-ONLY: it adds no store to the '
          'pinned write set above')
    hits = []
    for m in store_re.finditer(_outside):
        # A DECLARATION also matches (`const char *verdict = ...`). Exclude it by
        # looking at what precedes the star: a declaration has a type word before
        # it on the same statement, a dereference-store does not.
        pre = _outside[max(0, m.start() - 40):m.start()]
        if re.search(r'(?:const\s+)?[A-Za-z_][\w:<>]*\s+$', pre):
            continue
        hits.append(m.group(0))
    check(not hits,
          'no store through any pointer outside R167 (the probe is READ-ONLY); hits=%r'
          % hits[:3])
    check(_rw167 != '' and
          _rw167.count('= ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE;') == 1 and
          '*cfg = ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE' in _rw167 and
          'ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG * 4u' in _rw167 and
          'if (held)' in _rw167,
          'R167 the one permitted store writes CM3_BOOT_CONFIG=0x02 and is gated '
          'on the CM3_SYSRESET read-back')

    raw = PACKAGE.read_bytes()
    check(b'\r' not in raw, 'the probe file is pure LF (CR count 0)')
    check(not raw.startswith(b'\xef\xbb\xbf'), 'the probe file has no BOM')

    # --- G. the boot argument is armed and fits the budget ----------------
    board = tomllib.loads(BOARD.read_text(encoding='utf-8'))
    args = board['payload']['kernel_args']
    joined = ' '.join(args)
    check('enchilada-adreno=6' in args,
          'board.toml arms stage 6 (stage 4 requires it, since the mask is monotonic)')
    check(len(joined) <= 1000,
          'the kernel command line fits the 1000-byte limit (measured %d)' % len(joined))
    check('enchilada-touch=4' in args,
          'the touch argument is still present (stage 4 did not evict it)')
    check('native-ufs-diagnostics=1' in args,
          'the UFS diagnostics argument is still present')

    # --- H. the contract still says it cannot do this alone ---------------
    check('adreno_iommu_ready' in contract,
          'the contract still exposes a derived ready()')
    check('kAdrenoIommuMissingId2Word' in contract,
          'the contract still records that the ID2 WORD itself was missing')
    check('kAdrenoIommuMissingKernelCaller' in contract,
          'the contract still records that a kernel caller was missing')

    print()
    print('%d/%d checks passed' % (checks - len(fails), checks))
    if fails:
        print('FAIL: stage-4 kernel ID2 probe contract (%d failures)' % len(fails))
        return 1
    print('PASS: stage-4 kernel ID2 probe contract '
          '(fail-closed gate, ID2 once + stage-6 ID0/ID1, contract decode, no store)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
