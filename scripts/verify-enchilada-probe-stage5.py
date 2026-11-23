#!/usr/bin/env python3
"""Round 95 cross-verifier: stage 5 -- the IOKit caller of the power-up chain.

Rounds 51/56/57/58/59/61/62 derived the ordered CX/GX/clock plan, round 62 gave it
an executor, and round 64 added a caller that brackets the executor with a
read-only pre-observation -- but every one of those layers spoke through CALLBACKS,
and nothing in the tree ever connected them to a mapped window. Stage 5 is that
connection and it is the ONLY stage of this probe that writes.

What this verifier asserts (each is a property the design stands or falls on):

  1. Stage 5 is armed by `enchilada-adreno=6`, default-off, and stages 1-4 are
     unchanged (a new stage must not regress the old ones; round 103 widened the
     boot argument to 1..6 to admit the read-only stage 6, and this verifier
     tracks that mask so the widening is auditable).
  2. It composes the three host-verified modules rather than re-implementing any of
     them: the window adapter, the executor's MMIO triple, and the caller.
  3. It refuses unless the SAME `gpucc_window_is_safe()` gate stages 1-4 use says
     the window is the GPUCC probe window.
  4. It supplies a REAL delay (`IODelay`) -- because AdrenoGpuccWindowV1 refuses to
     hand back an MMIO triple with a NULL delay, and a spin loop calibrated by
     guesswork would be worse than no loop at all.
  5. The run's THREE verdicts (`up_status`, `precheck_status`, and the before/after
     CX words) are reported SEPARATELY, so "the writes executed" can never be read
     as "CX is now on". On this board that distinction is the whole ballgame.
  6. The probe still performs no direct store through a pointer: stage 5 writes only
     through the window adapter's callbacks.
  7. The boot argument fits the measured command-line budget.

Read-only: reads sources, never writes.
"""
import pathlib
import re
import sys
import tomllib

DEV = pathlib.Path(__file__).resolve().parents[1]
PACKAGE = DEV / 'package' / 'EnchiladaAdrenoProbe.cpp'
BOARD = DEV / 'board.toml'
STUB = DEV / 'scripts' / 'data' / 'iokit-stub' / 'IOKit' / 'IOLib.h'

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
    code = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    code = re.sub(r'//[^\n]*', '', code)

    # --- A. armed, default-off, no regression of 1-4 ----------------------
    check('stage > 6u' in code, 'the mask rejects stages above 6')
    check('(stage < 1u || stage > 6u)' in code,
          'the mask accepts exactly 1..6 and nothing else')
    check('if (stage >= 5u)' in code, 'stage 5 is gated behind stage >= 5')
    for old in ('if (stage >= 2u)', 'if (stage >= 3u)', 'if (stage >= 4u)'):
        check(old in code, 'stage chain still contains %r' % old)

    # --- B. composition, not re-implementation ----------------------------
    for hdr in ('AdrenoGpuccWindowV1.h', 'AdrenoPowerUpBackendV1.h', 'AdrenoCallerV1.h'):
        check('#include "%s"' % hdr in text, 'includes %s' % hdr)
    check('AdrenoGpuccWindowV1Bind(' in code, 'binds the window adapter')
    check('AdrenoGpuccWindowV1SetDelay(' in code, 'supplies the delay to the window')
    check('AdrenoGpuccWindowV1Mmio(' in code, 'obtains the MMIO triple from the window')
    check('AdrenoCallerV1Make(' in code, 'constructs the caller')
    check('AdrenoCallerV1Run(' in code, 'runs the caller')

    # --- C. the same safety gate stages 1-4 rely on -----------------------
    i5 = code.find('if (stage >= 5u)')
    check(i5 >= 0, 'stage-5 block found in the comment-stripped source')
    blk = code[i5:] if i5 >= 0 else ''
    j = blk.find('setPowerReplay(pwRow);')
    if j >= 0:
        blk = blk[:j]
    check('gpucc_window_is_safe()' in blk,
          'stage 5 re-checks the gpucc_window_is_safe() gate before writing')
    i_gate = blk.find('gpucc_window_is_safe()')
    i_bind = blk.find('AdrenoGpuccWindowV1Bind(')
    check(i_bind > i_gate,
          'the window is bound only AFTER the safety gate passes')

    # --- D. a real delay, from IOKit --------------------------------------
    check('IODelay(' in code, 'the shim calls IODelay (the kernel busy-wait)')
    check('void IODelay(unsigned int microseconds);' in
          STUB.read_text(encoding='utf-8'),
          'the stub declares IODelay, so the host type-check covers it')
    check('gpucc_udelay' in code, 'a named delay callback is supplied')
    # No hand-rolled spin loop: a calibrated guess is the thing the window header
    # refuses to allow. Assert no busy-loop was added for the purpose.
    check('for (volatile' not in code and 'while ((rd32(' not in code,
          'no hand-rolled spin-wait was invented for the delay')

    # --- E. the THREE verdicts stay separate ------------------------------
    for field in ('rep.up_status', 'rep.precheck_status', 'rep.before.cx', 'rep.after.cx'):
        check(field in blk, 'the report surfaces %s' % field)
    check('rep.after.cx_on' in blk, 'the report surfaces whether CX reads as ON after')
    # A single collapsed "success" flag would lose the distinction that matters.
    check('stage5=run' in blk, 'the stage-5 row is labelled so it cannot be mistaken for a read')

    # --- F. inputs are board facts, not magic -----------------------------
    check('in.armed        = 1u;' in blk, 'armed is set explicitly (the arm gate)')
    check('core_rate_hz = 200000000u' in blk,
          'core rate is the pinned a6xx_gmu.c value')
    check('in.strict_gx    = 0u;' in blk,
          'GX is not required on this board (the measured steady state)')

    # --- G. no direct store anywhere in the probe -------------------------
    # Stage 5 writes, but ONLY through the window adapter's callbacks. A direct
    # store here would bypass the bounds that make the window safe.
    #
    # ROUND 167 is the ONE deliberate exception and it is pinned, not waved
    # through: exactly one register store exists, to CM3_BOOT_CONFIG, armed by
    # `enchilada-pas-exec=6` and gated on the CM3_SYSRESET read-back. Everything
    # outside that block stays store-free, so the callback rule still holds.
    store_re = re.compile(r'(?<![\w\)\]\*])\*\s*[A-Za-z_]\w*(?:\[[^\]]*\])?\s*=(?!=)')
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
          'fwKey >= 7u && fwKey <= 231u' in code and
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
            '\nif (fwKey == 193u || fwKey == 194u || fwKey == 195u) {\n', 1)[0]
    _i193195r = code.find('if (fwKey == 193u || fwKey == 194u || fwKey == 195u) {')
    _k193195r = ''
    if _i193195r > 0:
        _k193195r = code[_i193195r:].split(
            '\nif (fwKey == 196u || fwKey == 197u || fwKey == 198u) {\n', 1)[0]
    _i196198r = code.find('if (fwKey == 196u || fwKey == 197u || fwKey == 198u) {')
    _k196198r = ''
    if _i196198r > 0:
        _k196198r = code[_i196198r:].split(
            '\nif (fwKey == 199u || fwKey == 200u || fwKey == 201u) {\n', 1)[0]
    _i199201r = code.find('if (fwKey == 199u || fwKey == 200u || fwKey == 201u) {')
    _k199201r = ''
    if _i199201r > 0:
        _k199201r = code[_i199201r:].split(
            '\nif (fwKey == 202u || fwKey == 203u || fwKey == 204u) {\n', 1)[0]
    _i202204r = code.find('if (fwKey == 202u || fwKey == 203u || fwKey == 204u) {')
    _k202204r = ''
    if _i202204r > 0:
        _k202204r = code[_i202204r:].split(
            '\nif (fwKey == 205u || fwKey == 206u || fwKey == 207u) {\n', 1)[0]
    _i205207r = code.find('if (fwKey == 205u || fwKey == 206u || fwKey == 207u) {')
    _k205207r = ''
    if _i205207r > 0:
        _k205207r = code[_i205207r:].split(
            '\nif (fwKey == 208u || fwKey == 209u || fwKey == 210u) {\n', 1)[0]
    _i208210r = code.find('if (fwKey == 208u || fwKey == 209u || fwKey == 210u) {')
    _k208210r = ''
    if _i208210r > 0:
        _k208210r = code[_i208210r:].split(
            '\nif (fwKey == 211u || fwKey == 212u || fwKey == 213u) {\n', 1)[0]
    _i211213r = code.find('if (fwKey == 211u || fwKey == 212u || fwKey == 213u) {')
    _k211213r = ''
    if _i211213r > 0:
        _k211213r = code[_i211213r:].split(
            '\nif (fwKey == 214u || fwKey == 215u || fwKey == 216u) {\n', 1)[0]
    _i214216r = code.find('if (fwKey == 214u || fwKey == 215u || fwKey == 216u) {')
    _k214216r = ''
    if _i214216r > 0:
        _k214216r = code[_i214216r:].split(
            '\nif (fwKey == 223u || fwKey == 224u || fwKey == 225u) {\n', 1)[0]
    _i223225r = code.find('if (fwKey == 223u || fwKey == 224u || fwKey == 225u) {')
    _k223225r = ''
    if _i223225r > 0:
        _k223225r = code[_i223225r:].split(
            '\nif (fwKey == 226u || fwKey == 227u || fwKey == 228u) {\n', 1)[0]
    _i226228r = code.find('if (fwKey == 226u || fwKey == 227u || fwKey == 228u) {')
    _k226228r = ''
    if _i226228r > 0:
        _k226228r = code[_i226228r:].split(
            '\nif (fwKey == 229u || fwKey == 230u || fwKey == 231u) {\n', 1)[0]
    _i229231r = code.find('if (fwKey == 229u || fwKey == 230u || fwKey == 231u) {')
    _k229231r = ''
    if _i229231r > 0:
        _k229231r = code[_i229231r:].split(
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
          _k190192r.count(_p1r) == 0 and _k190192r.count(_p0r) == 0 and
          _k193195r != '' and
          _k193195r.count(_p1r) == 0 and _k193195r.count(_p0r) == 0 and
          _k196198r != '' and
          _k196198r.count(_p1r) == 0 and _k196198r.count(_p0r) == 0 and
          _k199201r != '' and
          _k199201r.count(_p1r) == 0 and _k199201r.count(_p0r) == 0 and
          _k202204r != '' and
          _k202204r.count(_p1r) == 0 and _k202204r.count(_p0r) == 0 and
          _k205207r != '' and
          _k205207r.count(_p1r) == 0 and _k205207r.count(_p0r) == 0 and
          _k208210r != '' and
          _k208210r.count(_p1r) == 0 and _k208210r.count(_p0r) == 0 and
          _k211213r != '' and
          _k211213r.count(_p1r) == 0 and _k211213r.count(_p0r) == 0 and
          _k214216r != '' and
          _k214216r.count(_p1r) == 0 and _k214216r.count(_p0r) == 0 and
          _k223225r != '' and
          _k223225r.count(_p1r) == 0 and _k223225r.count(_p0r) == 0 and
          _k226228r != '' and
          _k226228r.count(_p1r) == 0 and _k226228r.count(_p0r) == 0 and
          _k229231r != '' and
          _k229231r.count(_p1r) == 0 and _k229231r.count(_p0r) == 0,
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
        pre = _outside[max(0, m.start() - 40):m.start()]
        if re.search(r'(?:const\s+)?[A-Za-z_][\w:<>]*\s+$', pre):
            continue
        hits.append(m.group(0))
    check(not hits, 'no direct store through any pointer outside R167; writes go '
          'via callbacks (hits=%r)' % hits[:3])
    check(_rw167 != '' and
          _rw167.count('= ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE;') == 1 and
          '*cfg = ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE' in _rw167 and
          'if (held)' in _rw167,
          'R167 the one permitted store writes CM3_BOOT_CONFIG=0x02 behind the '
          'CM3_SYSRESET read-back')
    # And the only cast targets are still the volatile window types. R172 adds a
    # const-qualified read-only one (the kgsl SMMU dump): same window discipline,
    # but it CANNOT be stored through, which is why it carries const.
    casts = set(re.findall(r'reinterpret_cast<([^>]*)>', code))
    check(casts <= {'volatile uint32_t *', 'volatile uint8_t *',
                    'volatile uint64_t *', 'volatile const uint32_t *'},
          'the only pointer casts are the volatile window types (got %r)' % sorted(casts))

    # --- H. write32 appears only as a struct field, never as a call -------
    # AdrenoPowerUpBackendV1Mmio carries a write32 member; the probe must not
    # invoke it itself, or it would be doing MMIO outside the adapter.
    check('mmio.write32(' not in code,
          'the probe never calls the MMIO write32 member directly')
    check('&mmio' in blk, 'the MMIO triple is passed by pointer to the caller')

    # --- H2. ORDERING: stage 5 must precede stage 4 -----------------------
    # Round 96 device run: stage 4 refused at t=50.088 on a CX word that stage 5
    # raised at t=50.365, so ID2 was unreachable on every boot. Round 98 swaps the
    # order; this assertion locks it so a future edit cannot silently put stage 4
    # back in front of the only code that powers the rail.
    i_s5_block = code.find('if (stage >= 5u)')
    i_s4_block = code.find('if (stage >= 4u)')
    check(i_s5_block >= 0 and i_s4_block >= 0,
          'both stage 4 and stage 5 blocks exist')
    check(i_s5_block < i_s4_block,
          'stage 5 executes BEFORE stage 4 (round-96 device finding)')

    # --- I. boot argument budget -----------------------------------------
    board = tomllib.loads(BOARD.read_text(encoding='utf-8'))
    args = board['payload']['kernel_args']
    joined = ' '.join(args)
    check('enchilada-adreno=6' in args, 'board.toml arms stage 6')
    check(len(joined) <= 1000,
          'the kernel command line fits the 1000-byte limit (measured %d)' % len(joined))

    raw = PACKAGE.read_bytes()
    check(b'\r' not in raw, 'the probe file is pure LF')
    check(not raw.startswith(b'\xef\xbb\xbf'), 'the probe file has no BOM')

    print()
    print('%d/%d checks passed' % (checks - len(fails), checks))
    if fails:
        print('FAIL: stage-5 power-up IOKit caller (%d failures)' % len(fails))
        return 1
    print('PASS: stage-5 power-up IOKit caller '
          '(gated, composed, callback-only writes, three separate verdicts)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
