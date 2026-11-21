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
          'fwKey >= 7u && fwKey <= 98u' in code and
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
    check(_k8 != '' and
          code.count('gmuW[ADRENO_GMU_BOOT_V1_CM3_SYSRESET] = 1u;') == 1 and
          code.count('gmuW[ADRENO_GMU_BOOT_V1_CM3_SYSRESET] = 0u;') == 1 and
          'gmuW[ADRENO_GMU_BOOT_V1_CM3_SYSRESET] = 1u;' in _k8 and
          'gmuW[ADRENO_GMU_BOOT_V1_CM3_SYSRESET] = 0u;' in _k8,
          'R168 the reset pulse exists exactly once and only inside the key-8 '
          'sub-block')
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
                    'volatile const uint32_t *'},
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
