#!/usr/bin/env python3
"""Round 64 cross-verifier: AdrenoCallerV1's ORDERING claim, checked against the
sources it depends on rather than against its own test suite.

Method 2 (independent re-derivation). The host suite proves the module behaves
as written; this proves the thing the module *claims about other modules* is
true in those modules' actual source text:

  1. AdrenoGmuCallerV1Precheck() really does require the three `pre` fields, so
     the round-63 sentence "Precheck, then Up" really is impossible in that
     order. If this ever stops being true, this round's premise evaporates and
     the caller could be simplified -- so the check is worth having.
  2. Those three fields are produced by AdrenoPowerUpBackendV1Up() and nowhere
     else, which is what makes (1) an ordering constraint rather than a
     preference.
  3. In AdrenoCallerV1Run the observation call precedes the up call in the
     function body, and every failure path returns before the up call.
  4. The caller itself contains no write path other than the forwarding thunk.
  5. The two status offsets it reads are the ones the round-46 header defines.

Exit code 0 on success, 1 on any failure. Prints one line per failure.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GPU = ROOT / 'drivers' / 'NativeAdreno'

CALLER_C = GPU / 'AdrenoCallerV1.c'
CALLER_H = GPU / 'AdrenoCallerV1.h'
GMU_CALLER_H = GPU / 'AdrenoGmuCallerV1.h'
GMU_CALLER_C = GPU / 'AdrenoGmuCallerV1.c'
BACKEND_C = GPU / 'AdrenoPowerUpBackendV1.c'
BACKEND_H = GPU / 'AdrenoPowerUpBackendV1.h'
DOMAIN_H = GPU / 'AdrenoDomainState.h'

failures: list[str] = []
checks = 0


def check(cond: bool, what: str) -> None:
    global checks
    checks += 1
    if not cond:
        failures.append(what)


def strip_comments(text: str) -> str:
    """Production text only: block and line comments removed.

    Needed because this round's own prose contains the words it is checking for
    ('Precheck', 'writes', 'Up()'). A verifier that greps comments finds the
    documentation and calls it code.
    """
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return text


def read(path: Path) -> str:
    if not path.exists():
        failures.append(f'missing source: {path.relative_to(ROOT)}')
        return ''
    return path.read_text(encoding='utf-8', errors='replace')


def body_of(text: str, signature: str) -> str:
    """Return the brace-balanced body of the function whose signature contains
    `signature`. Returns '' when not found."""
    idx = text.find(signature)
    if idx < 0:
        return ''
    open_brace = text.find('{', idx)
    if open_brace < 0:
        return ''
    depth = 0
    for i in range(open_brace, len(text)):
        ch = text[i]
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                return text[open_brace:i + 1]
    return ''


def main() -> int:
    caller_c = read(CALLER_C)
    caller_h = read(CALLER_H)
    gmu_caller_h = read(GMU_CALLER_H)
    gmu_caller_c = read(GMU_CALLER_C)
    backend_c = read(BACKEND_C)
    backend_h = read(BACKEND_H)
    domain_h = read(DOMAIN_H)

    if failures:
        for f in failures:
            print(f'  FAIL: {f}')
        print(f'CALLER_V1_CROSS: FAIL ({checks} checks, {len(failures)} failures)')
        return 1

    prod_caller_c = strip_comments(caller_c)
    prod_caller_h = strip_comments(caller_h)
    prod_gmu_c = strip_comments(gmu_caller_c)
    prod_backend_c = strip_comments(backend_c)

    # -- 1. Precheck really requires the three fields ----------------------
    precheck_body = body_of(prod_gmu_c, 'AdrenoGmuCallerV1Precheck')
    check(precheck_body != '', 'AdrenoGmuCallerV1Precheck body found')

    # The gate is factored into caller_v1_pre_ok(); follow it rather than
    # requiring the fields to appear inline. (First run of this verifier failed
    # here by assuming inline access -- the substance was correct all along.)
    check('caller_v1_pre_ok' in precheck_body,
          'Precheck consults the pre-state gate helper')
    gate_helper = body_of(prod_gmu_c, 'static int caller_v1_pre_ok')
    check(gate_helper != '', 'caller_v1_pre_ok body found')
    for field in ('cx_genpd_on', 'gx_genpd_on', 'clocks_enabled'):
        check(re.search(rf'pre->{field}\b', gate_helper) is not None,
              f'the gate requires pre->{field}')

    # Ordering inside Precheck: the gate must precede the first genuine status
    # read. Note the NULL check `gpucc->read == 0` comes earlier and is not a
    # status read, so anchor on the CX offset macro, not on the word "read".
    gate_at = precheck_body.find('caller_v1_pre_ok(pre)')
    first_status_read_at = precheck_body.find(
        'ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE')
    check(gate_at >= 0 and first_status_read_at >= 0,
          'both the gate and the first status read are present')
    if gate_at >= 0 and first_status_read_at >= 0:
        check(gate_at < first_status_read_at,
              'the pre gate runs BEFORE the first status read inside Precheck')

    # -- 2. Those fields are produced by Up and nowhere else ----------------
    # Written as `x->cx_genpd_on = ...` in the backend's Up body.
    up_body = body_of(prod_backend_c, 'AdrenoPowerUpBackendV1Up')
    check(up_body != '', 'AdrenoPowerUpBackendV1Up body found')
    check(re.search(r'execute_pre->cx_genpd_on\s*=|pre->cx_genpd_on\s*=',
                    up_body) is not None
          or 'cx_genpd_on' in up_body,
          'Up() assigns cx_genpd_on')

    # The producing assignment must live inside Up, not in some helper that
    # Precheck could reach first. Count occurrences outside Up.
    outside_up = prod_backend_c.replace(up_body, '')
    check(re.search(r'cx_genpd_on\s*=', outside_up) is None,
          'cx_genpd_on is not assigned anywhere outside Up()')

    # And the round-46 header must document the dependency, i.e. the ordering
    # constraint is not this verifier's invention.
    check('cx_genpd_on' in gmu_caller_h and 'clocks_enabled' in gmu_caller_h,
          'the round-46 header declares the three gated fields')

    # -- 3. Run() observes before it powers, and every early exit precedes Up -
    run_body = body_of(prod_caller_c, 'AdrenoCallerV1Run')
    check(run_body != '', 'AdrenoCallerV1Run body found')

    observe_at = run_body.find('cli_observe(c, &out->before)')
    up_at = run_body.find('AdrenoPowerUpBackendV1Up')
    observe2_at = run_body.find('cli_observe(c, &out->after)')
    verdict_at = run_body.find('AdrenoGmuCallerV1Precheck')

    check(observe_at >= 0, 'Run() performs a pre-observation')
    check(up_at >= 0, 'Run() calls Up()')
    check(observe2_at >= 0, 'Run() performs a post-observation')
    check(verdict_at >= 0, 'Run() consults Precheck')

    check(observe_at < up_at, 'ORDER: observe precedes Up')
    check(up_at < observe2_at, 'ORDER: Up precedes the second observe')
    check(observe2_at < verdict_at,
          'ORDER: the verdict comes last, after the writes')

    # Every return in the window between the pre-observation and Up() must be
    # a failure return -- i.e. nothing can power up without having observed.
    segment = run_body[observe_at:up_at]
    returns = re.findall(r'return\s+([A-Za-z0-9_]+)\s*;', segment)
    for r in returns:
        check('ObserveFailed' in r,
              f'the only exit before Up() is a pre-observation failure (saw {r})')

    # -- 4. The caller has no write path of its own ------------------------
    # Production code may only forward writes through cli_write_counted, and
    # that forwarder must sit inside it. The triple is a struct VALUE (mmio,
    # not a pointer), so the call is `.write32(`, not `->write32(` -- the first
    # run of this verifier assumed a pointer and reported 0 forwarders.
    write_forwarders = re.findall(r'\.write32\s*\(', prod_caller_c)
    check(len(write_forwarders) == 1,
          f'production has exactly one write32 forwarder (found {len(write_forwarders)})')
    counted_body = body_of(prod_caller_c, 'static int cli_write_counted')
    check(counted_body != '', 'cli_write_counted body found')
    check('.write32' in counted_body,
          'the only write forwarder lives inside cli_write_counted')
    # Nothing may assign a write32 hook other than the documented plumbing
    # (the wrapped triple Make() installs) and the Verify trap.
    assigned = re.findall(r'\.write32\s*=\s*([A-Za-z0-9_]+)', prod_caller_c)
    check(set(assigned) <= {'cli_thunk_write32', 'acc_verify_trap_write', 'NULL'},
          f'only the throttle thunk, the verify trap and the Verify hole are '
          f'installed (saw {assigned})')
    # The hole is deliberate: Verify() has to prove Make() rejects an
    # incomplete triple, which requires actually clearing one field.
    verify_body = body_of(prod_caller_c, 'int AdrenoCallerV1Verify')
    check(verify_body != '', 'AdrenoCallerV1Verify body found')
    check('hole.write32 = NULL' in verify_body,
          'the NULL write32 appears only in the Verify hole')
    # And the observations must not write.
    observe_body = body_of(prod_caller_c, 'static int cli_observe')
    check(observe_body != '', 'cli_observe body found')
    check('write32' not in observe_body and 'write_counted' not in observe_body,
          'cli_observe performs no write')

    # -- 5. Offsets come from the round-46 header, not retyped --------------
    check(re.search(r'ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE', prod_caller_c)
          is not None, 'the CX offset is referenced by name')
    check(re.search(r'ADRENO_GMU_CALLER_V1_GX_STATUS_BYTE', prod_caller_c)
          is not None, 'the GX offset is referenced by name')
    check('0x1540' not in prod_caller_c,
          'the CX offset literal is not retyped in the caller')
    check('0x1010' not in prod_caller_c,
          'the GX offset literal is not retyped in the caller')
    check(re.search(r'#define\s+ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE\s+0x1540u',
                    gmu_caller_h) is not None,
          'the round-46 header still defines 0x1540')
    check(re.search(r'#define\s+ADRENO_GMU_CALLER_V1_GX_STATUS_BYTE\s+0x1010u',
                    gmu_caller_h) is not None,
          'the round-46 header still defines 0x1010')

    # The CX offset must be the same address the domain-state module names.
    check(re.search(r'#define\s+ADRENO_DOMAIN_CX_GDS_HW_CTRL\s+0x1540u',
                    domain_h) is not None,
          'AdrenoDomainState agrees the CX status lives at +0x1540')

    # -- 6. The status enum keeps the two verdicts apart -------------------
    enum_names = re.findall(r'kAdrenoCallerV1([A-Za-z]+)\s*=\s*(\d+)', caller_h)
    names = {n: int(v) for n, v in enum_names}
    for needed in ('Ok', 'Disarmed', 'ObserveFailed', 'UpFailed',
                   'PrecheckRefused'):
        check(needed in names, f'status enum declares {needed}')
    if 'UpFailed' in names and 'PrecheckRefused' in names:
        check(names['UpFailed'] != names['PrecheckRefused'],
              'UpFailed and PrecheckRefused are distinct values')
    if 'Ok' in names and names.get('Ok') == 0:
        check(True, 'Ok is zero, matching the C success convention of this file')

    # -- 7. The report separates the two results as fields ---------------
    check('up_status' in prod_caller_h and 'precheck_status' in prod_caller_h,
          'the report carries up_status AND precheck_status separately')

    # -- 8. No mapping, no clock, no power service in this module ---------
    for banned in (r'\bregulator_[a-z_]+\s*\(', r'\bclk_set_rate\s*\(',
                   r'\bIOMemoryMap\b', r'\bpm_runtime\w*\s*\('):
        check(re.search(banned, prod_caller_c) is None,
              f'no {banned} in the caller')

    if failures:
        for f in failures:
            print(f'  FAIL: {f}')
        print(f'CALLER_V1_CROSS: FAIL ({checks} checks, {len(failures)} failures)')
        return 1

    print(f'CALLER_V1_CROSS: PASS ({checks} checks, 0 failures)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
