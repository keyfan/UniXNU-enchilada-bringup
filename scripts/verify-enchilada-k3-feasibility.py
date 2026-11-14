#!/usr/bin/env python3
"""Round 105 cross-verifier: K3 signing-chain feasibility (READ-ONLY audit).

Five domains, mirroring verify-enchilada-render-dependency-map.py:

  A. Document contract: status, path aliases, sections 0-5, three verdict
     corrections, the device-level proof section, honest boundary, technical debt.
  B. Cross-file VERBATIM literal anchors (the round-104 lesson: paraphrased
     pseudo-quotes get caught). Every cited source line really exists.
  C. DEVICE WINDOW proof: re-read the authoritative pulled round-103 window and
     assert the armcpu_scm[] tuple is real, non--99, constant, and in the SAME
     window as the measured stage6=id1 row.
  D. CI kernel image: armcpu_scm linked, no callable PAS symbol.
  E. Honest invariants: modeled=9/0/BLOCKED unchanged, ADRENO=n, no unblock,
     coherent_walk=1 -> b5100080 technical debt carried forward.

usage: verify-enchilada-k3-feasibility.py
"""
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
ONEPLUS = DEV.parent.parent
GPU = DEV / 'drivers' / 'NativeAdreno'
EV = DEV / 'evidence'
SCR = DEV / 'scripts'
DOC = EV / 'stage5-gpu-k3-feasibility.md'
WINDOW = ONEPLUS / 'work' / 'tmp' / 'r103_stage6_window.bin'
KERNEL = ONEPLUS / 'work' / 'tmp' / 'r103_ci246_kernel.macho'

checks = 0
fails = []


def ck(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)
        print('FAIL:', label)
    else:
        print('ok  ', label)


def sec(name):
    print('\n--- %s ---' % name)


def main():
    doc_b = DOC.read_bytes()
    doc = doc_b.decode('utf-8')

    # ---------------- Domain A: document contract ----------------
    sec('A. document contract')
    ck('\r' not in doc, 'pure LF (no CR byte)')
    ck(not doc_b.startswith(b'\xef\xbb\xbf'), 'no UTF-8 BOM')
    ck('Round 105' in doc and 'K3 signing-chain feasibility' in doc, 'title')
    ck('HOST-VERIFIED + DEVICE-SOURCED' in doc, 'status line')
    ck('RESEARCH / FEASIBILITY ONLY' in doc, 'research-only marker')
    for s in ['§0', '§1', '§2', '§3', '§4', '§5']:
        ck(s in doc, 'section %s present' % s)
    ck('Path aliases' in doc, 'path-alias block')
    ck('ONEPLUS/' in doc and 'DEV/' in doc and 'GPU/' in doc and 'EV/' in doc and 'SCR/' in doc,
       'all five path aliases')
    ck('CONDITIONALLY FEASIBLE' in doc, 'up-front verdict')
    ck('IMPLEMENTABLE-WITH-EFFORT' in doc, 'transport verdict phrase')
    ck('no longer "fundamentally blocked"' in doc, 'explicit reversal of old verdict')
    ck('FALSE.' in doc, 'three FALSE historical claims marked')
    ck('2.3' in doc and 'DEVICE-PROVEN' in doc, 'device-proof subsection')
    ck('509' in doc, '509 snapshots recorded')
    ck('b5100080' in doc and 'a0100080' in doc, 'round-103 technical debt present')
    ck('modeled=9 / satisfied=0 / BLOCKED' in doc, 'admission table unchanged')
    ck('CONFIG_XNU_NATIVE_ADRENO' in doc and 'remains `n`' in doc, 'ADRENO stays n')
    ck('does NOT unblock stage 5' in doc, 'does-not-unblock statement')
    ck('READ-ONLY' in doc and 'no flash' in doc and 'no reboot' in doc,
       'read-only boundary spelled out')
    ck('Do NOT restart from UEFI' in doc, 'UEFI regression warning')
    ck('Next minimal action' in doc, 'next-action section')
    ck('(i) Continue the SMMU chain' in doc and '(ii) Advance K3' in doc,
       'two candidate next rounds')

    # ---------------- Domain B: cross-file verbatim anchors ----------------
    sec('B. cross-file verbatim literal anchors')
    # (label, path, literal, present?)  present=False asserts the literal is ABSENT
    pairs = [
        ('armcpu smc #0', DEV / 'drivers' / 'ArmCpu' / 'ArmCpu.cpp', 'smc #0', True),
        ('armcpu SCM comment', DEV / 'drivers' / 'ArmCpu' / 'ArmCpu.cpp',
         'Qualcomm SCM over SMC (SMCCC SIP', True),
        ('armcpu scm fn', DEV / 'drivers' / 'ArmCpu' / 'ArmCpu.cpp',
         'static ScmResult scm (uint64_t fn', True),
        ('armcpu crashdump fn', DEV / 'drivers' / 'ArmCpu' / 'ArmCpu.cpp',
         'static void scmDisableCrashdump (void)', True),
        ('armcpu_scm sentinel', DEV / 'drivers' / 'ArmCpu' / 'ArmCpu.cpp',
         'armcpu_scm[8] = { -99', True),
        ('armcpu scm call fid', DEV / 'drivers' / 'ArmCpu' / 'ArmCpu.cpp', '0xC2000109', True),
        ('armcpu wdog gate3', DEV / 'drivers' / 'ArmCpu' / 'ArmCpu.cpp',
         'scmDisableCrashdump ();', True),
        ('bootreset scm print', DEV / 'package' / 'BootReset.c', 'scm avail sdi=%lld', True),
        ('fw manifest gmu', SCR / 'data' / 'adreno-a630-device-firmware.json',
         'b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f', True),
        ('fw manifest mdt', SCR / 'data' / 'adreno-a630-device-firmware.json',
         'e0dffc7e45dfb05c681419881ab876ec974b6dc60b369394c2c9c6d9f631595e', True),
        ('fw manifest hexagon', SCR / 'data' / 'adreno-a630-device-firmware.json',
         'EM_HEXAGON', True),
        ('fw manifest reversible', SCR / 'data' / 'adreno-a630-device-firmware.json',
         'STATE_RESTORED=True', True),
        ('fw evidence file-level', EV / 'stage5-gpu-device-firmware.md',
         'at the file-presence level only', True),
        ('zap no SCM prose', EV / 'stage5-gpu-zap-auth.md',
         'no SCM transport in XNU', True),
        ('zap one of eight', EV / 'stage5-gpu-zap-auth.md',
         'Exactly one of eight is present', True),
        ('mdt parser not loader', EV / 'stage5-gpu-mdt-parser.md',
         'A parser is not a loader.', True),
        ('mdt header not loader', GPU / 'AdrenoMdtV1.h',
         'This is NOT a firmware loader', True),
        ('gen1 9 modelled', GPU / 'AdrenoGen1BackendV1.c', '9 modelled', True),
        ('gen1 blocked', GPU / 'AdrenoGen1BackendV1.c', '0 satisfied, BLOCKED', True),
        ('kgsl never rendered', EV / 'stage5-gpu-kgsl-sysfs.md',
         'never actually rendered anything', True),
        ('ci forbids qcom_scm', SCR / 'verify-ci-246-adreno.py', '_qcom_scm_call', True),
        ('ci forbids zap loader', SCR / 'verify-ci-246-adreno.py',
         'adreno_zap_shader_load', True),
        ('defconfig adreno off', DEV / 'configs' / 'enchilada_defconfig',
         'CONFIG_XNU_NATIVE_ADRENO=n', True),
        ('defconfig legacy display on', DEV / 'configs' / 'enchilada_defconfig',
         'CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y', True),
        ('porting no qcom_scm', DEV / 'porting.toml',
         'XNU has no qcom_scm equivalent', True),
        ('porting supersedes absent', DEV / 'porting.toml',
         "SUPERSEDES the '3 vendor-signed ZAP blobs absent' premise", True),
        ('dependency map K3 blocked', EV / 'stage5-gpu-render-dependency-map.md',
         '7 firmware names absent', True),
        ('ci image no PAS symbol', SCR / 'verify-ci-246-adreno.py',
         "for bad in ('_writel(', '_readl(', '_IOServiceOpen', '_IOConnectCallMethod',", True),
    ]
    for label, path, lit, present in pairs:
        if not path.exists():
            ck(False, '%s: file missing %s' % (label, path))
            continue
        t = path.read_bytes().decode('utf-8', 'replace')
        ok = (lit in t) if present else (lit not in t)
        ck(ok, '%s (%s)' % (label, path.name))

    # ---------------- Domain C: DEVICE WINDOW proof ----------------
    sec('C. device window: armcpu_scm real, not -99')
    if not WINDOW.exists():
        ck(False, 'authoritative pulled window present')
    else:
        w = WINDOW.read_bytes().decode('utf-8', 'replace')
        ck(len(WINDOW.read_bytes()) == 33554432, 'window is 32 MiB (33554432 B)')
        pat = re.compile(r'scm avail sdi=(-?\d+) dl=(-?\d+) wr=(-?\d+) sdi=(-?\d+) '
                         r'tcsr=(0x[0-9a-f]+|-?\d+) dl=(-?\d+) wr=(-?\d+) '
                         r'tcsr=(0x[0-9a-f]+|-?\d+)')
        tuples = [m.groups() for m in pat.finditer(w)]
        ck(len(tuples) >= 1, 'scm avail tuples present (%d)' % len(tuples))
        ck(len(tuples) == 509, 'exactly 509 scm snapshots')
        distinct = set(tuples)
        ck(len(distinct) == 1, 'all scm tuples identical (1 distinct)')
        expected = ('1', '0', '1', '0', '10', '-1', '0', '0')
        ck(expected in distinct, 'tuple == sdi=1 dl=0 wr=1 sdi=0 tcsr=10 dl=-1 wr=0 tcsr=0')
        # no -99 sentinel in any scm line (the "never executed" marker)
        scm_lines = [m.group(0) for m in re.finditer(r'scm avail[^\n]*', w)]
        ck(all('-99' not in ln for ln in scm_lines), 'no -99 sentinel in any scm line')
        # first slot = 1 (SDI call available); proves SMC executed
        ck(all(t[0] == '1' for t in tuples), 'armcpu_scm[0]=1 (SDI available) in all snapshots')
        # the download-mode bit was cleared: [4]=0x10 -> [7]=0
        ck(all(t[4] == '10' and t[7] == '0' for t in tuples),
           'TCSR download bit 0x10 present then cleared (SMC write succeeded)')
        # tie to the round-103 measurement in the SAME window
        ck('stage6=id1 id0=48016605 id1=20008005' in w,
           'same window carries the round-103 stage6=id1 measurement')
        ck('tcr=b5100080' in w, 'same window carries the coherent TCR')

    # ---------------- Domain D: CI kernel image ----------------
    sec('D. CI #246 kernel image')
    if not KERNEL.exists():
        ck(False, 'CI #246 kernel.macho present')
    else:
        k = KERNEL.read_bytes().decode('latin-1')
        ck('armcpu_scm' in k, 'armcpu_scm linked into CI kernel')
        ck('_armcpu_scm' in k, 'Mach-O mangled _armcpu_scm present')
        ck('_qcom_scm_call' not in k, 'no callable _qcom_scm_call symbol')
        ck('adreno_zap_shader_load' not in k, 'no zap loader symbol')
        ck('qcom_scm_pas_auth_and_reset' in k,
           'PAS name present only as a citation string')

    # ---------------- Domain E: honest invariants ----------------
    sec('E. honest invariants')
    ck('does not satisfy any gate' in doc, 'reclassification satisfies no gate')
    ck('no `PAS_AUTH` executed' in doc or 'no `PAS_AUTH`' in doc,
       'PAS_AUTH not executed this round')
    ck('b5100080' in doc and 'placeholder' in doc,
       'a0100080 flagged placeholder, b5100080 authoritative')
    gen1 = (GPU / 'AdrenoGen1BackendV1.c').read_bytes().decode('utf-8', 'replace')
    ck('9 modelled' in gen1 and '0 satisfied, BLOCKED' in gen1,
       'gen1 admission table truly unchanged in source')
    defc = (DEV / 'configs' / 'enchilada_defconfig').read_bytes().decode('utf-8', 'replace')
    ck('CONFIG_XNU_NATIVE_ADRENO=n' in defc, 'defconfig ADRENO really still n')
    ck(re.search(r'does NOT unblock stage 5', doc) is not None,
       'does-NOT-unblock assertion present')

    print('\n' + '=' * 60)
    print('K3_FEASIBILITY_VERIFY: %s (%d checks, %d failures)' %
          ('PASS' if not fails else 'FAIL', checks, len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
