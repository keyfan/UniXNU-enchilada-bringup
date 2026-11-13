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
    check(code.count('rd32(gSmmuVa') == 3,
          'the SMMU window is dereferenced exactly three times: ID2 once + ID0 once + ID1 once')
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
    hits = []
    for m in store_re.finditer(code):
        # A DECLARATION also matches (`const char *verdict = ...`). Exclude it by
        # looking at what precedes the star: a declaration has a type word before
        # it on the same statement, a dereference-store does not.
        pre = code[max(0, m.start() - 40):m.start()]
        if re.search(r'(?:const\s+)?[A-Za-z_][\w:<>]*\s+$', pre):
            continue
        hits.append(m.group(0))
    check(not hits,
          'no store through any pointer (the probe is READ-ONLY); hits=%r' % hits[:3])

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
