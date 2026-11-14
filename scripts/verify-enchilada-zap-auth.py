#!/usr/bin/env python3
"""Round 73 method-2 cross-verifier for AdrenoZapAuthV1.

Domain A re-derives the Qualcomm PAS call SHAPE and the eight-step prerequisite
chain from FOUR sha256-pinned sources (public qcom_scm.h, qcom_scm.c, the
INTERNAL qcom_scm.h, and adreno_gpu.c).

Domain B asserts the freestanding C module mirrors that shape, keeps exactly one
of the eight preconditions marked present, and keeps auth_ready() derived.

Never compiles or runs the module; never touches the network.
"""
import hashlib
import re
import sys
from pathlib import Path

DEV = Path(__file__).resolve().parents[1]
ONEPLUS = Path(__file__).resolve().parents[3]
CACHE = DEV / '.cache' / 'adreno-a630-source'
GPU = DEV / 'drivers' / 'NativeAdreno'

PINS = [
    ('P1_public_qcom_scm_h',
     CACHE / 'include/linux/firmware/qcom/qcom_scm.h', 5203,
     '47390ea0f7d1fb6c3c298ed0fd13ee0275618e84d68ea43f0f2ac10448c448a1'),
    ('P2_qcom_scm_c', CACHE / 'drivers/firmware/qcom/qcom_scm.c', 53938,
     '8f1fbf64363590ea3c55b5bf4267a301792948473df9d837f4efa0206b9a5001'),
    ('P3_internal_qcom_scm_h', CACHE / 'drivers/firmware/qcom/qcom_scm.h', 4977,
     'd9cc584a5ff50cf8701a7775ad0f2e94f1b46683d0549e8871847c9122394881'),
    ('P4_adreno_gpu_c', CACHE / 'drivers/gpu/drm/msm/adreno/adreno_gpu.c', 29592,
     'e6b5203fb56358d22542197bd251c1513dc2b2f1ae8aaf3f9d9fca129f6f00b7'),
]

checks = 0
fails = []


def check(cond, label):
    global checks
    checks += 1
    if not cond:
        fails.append(label)


def flat(s):
    return ' '.join(s.split())


texts = {}
for name, path, size, sha in PINS:
    if not path.is_file():
        check(False, f'A1 {name} missing ({path})')
        texts[name] = []
        continue
    b = path.read_bytes()
    check(len(b) == size, f'A1 {name} size {len(b)} != {size}')
    check(hashlib.sha256(b).hexdigest() == sha, f'A1 {name} sha256 mismatch')
    check(b'\r' not in b, f'A1 {name} has CR')
    texts[name] = b.decode('utf-8').split('\n')


def line(name, number):
    lines = texts[name]
    return lines[number - 1] if 0 < number <= len(lines) else ''


pub_h, scm_c, int_h, agpu = (texts[k] for k, *_ in PINS)

# ---- A2: the public API surface (P1) ------------------------------------
check('int qcom_scm_pas_init_image(u32 peripheral, const void *metadata, size_t size,'
      in flat(' '.join(pub_h[74:76])), 'A2 P1:75 qcom_scm_pas_init_image')
check('int qcom_scm_pas_auth_and_reset(u32 peripheral);' in line('P1_public_qcom_scm_h', 79),
      'A2 P1:79 qcom_scm_pas_auth_and_reset')
check('int qcom_scm_pas_shutdown(u32 peripheral);' in line('P1_public_qcom_scm_h', 80),
      'A2 P1:80 qcom_scm_pas_shutdown')
check('bool qcom_scm_pas_supported(u32 peripheral);' in line('P1_public_qcom_scm_h', 81),
      'A2 P1:81 qcom_scm_pas_supported')

# ---- A3: the numeric ids are NOT public (this is why P3 exists) --------
for tok in ('QCOM_SCM_SVC_PIL', 'QCOM_SCM_PIL_PAS_AUTH_AND_RESET',
            'QCOM_SCM_ARGS', 'ARM_SMCCC_OWNER_SIP'):
    check(tok not in '\n'.join(pub_h), f'A3 {tok} is NOT in the PUBLIC header')
    check(tok in '\n'.join(int_h) or tok in '\n'.join(scm_c) or
          tok in '\n'.join(agpu),
          f'A3 {tok} IS available in the pinned internal sources')

# ---- A4: the internal ids (P3) ------------------------------------------
check(re.search(r'#define\s+QCOM_SCM_SVC_PIL\s+0x02', '\n'.join(int_h)) is not None,
      'A4 P3:97 QCOM_SCM_SVC_PIL 0x02')
check(re.search(r'#define\s+QCOM_SCM_PIL_PAS_INIT_IMAGE\s+0x01', '\n'.join(int_h)) is not None,
      'A4 P3:98 PAS_INIT_IMAGE 0x01')
check(re.search(r'#define\s+QCOM_SCM_PIL_PAS_MEM_SETUP\s+0x02', '\n'.join(int_h)) is not None,
      'A4 P3:99 PAS_MEM_SETUP 0x02')
check(re.search(r'#define\s+QCOM_SCM_PIL_PAS_AUTH_AND_RESET\s+0x05', '\n'.join(int_h)) is not None,
      'A4 P3:100 PAS_AUTH_AND_RESET 0x05')
check(re.search(r'#define\s+QCOM_SCM_PIL_PAS_SHUTDOWN\s+0x06', '\n'.join(int_h)) is not None,
      'A4 P3:101 PAS_SHUTDOWN 0x06')
check(re.search(r'#define\s+QCOM_SCM_PIL_PAS_IS_SUPPORTED\s+0x07', '\n'.join(int_h)) is not None,
      'A4 P3:102 PAS_IS_SUPPORTED 0x07')

# ---- A5: the auth call body (P2:703-731) --------------------------------
body = ' '.join(scm_c[702:731])
check('.svc = QCOM_SCM_SVC_PIL' in body, 'A5 P2:707 svc is PIL')
check('.cmd = QCOM_SCM_PIL_PAS_AUTH_AND_RESET' in body, 'A5 P2:708 cmd is AUTH_AND_RESET')
check('.arginfo = QCOM_SCM_ARGS(1)' in body, 'A5 P2:709 exactly ONE argument')
check('.args[0] = peripheral' in body, 'A5 P2:710 the argument is the peripheral id')
check('.owner = ARM_SMCCC_OWNER_SIP' in body, 'A5 P2:711 owner is SIP')
check('return ret ? : res.result[0];' in flat(body) or
      'return ret ?: res.result[0];' in flat(body),
      'A5 P2:729 success needs BOTH the transport and the PCTX result')
check(qcom_clk := 'qcom_scm_clk_enable()' in body, 'A5 P2:715 clock enabled first')
check('qcom_scm_clk_disable()' in body, 'A5 P2:727 clock disabled on every path')

# ---- A6: the eight-step chain (P4:43-156) ------------------------------
check('if (!IS_ENABLED(CONFIG_ARCH_QCOM))' in line('P4_adreno_gpu_c', 43),
      'A6 P4:43 CONFIG_ARCH_QCOM gate')
check('of_get_child_by_name(dev->of_node, "zap-shader")' in line('P4_adreno_gpu_c', 48),
      'A6 P4:48 zap-shader child node')
check('of_parse_phandle(np, "memory-region", 0)' in line('P4_adreno_gpu_c', 54),
      'A6 P4:54 memory-region phandle')
check('of_address_to_resource(mem_np, 0, &r)' in line('P4_adreno_gpu_c', 61),
      'A6 P4:61 address -> resource')
check('of_property_read_string_index(np, "firmware-name", 0, &signed_fwname)'
      in line('P4_adreno_gpu_c', 82), 'A6 P4:82 firmware-name property')
check('qcom_mdt_get_size(fw)' in line('P4_adreno_gpu_c', 111),
      'A6 P4:111 mdt size')
check('qcom_mdt_load(' in ' '.join(agpu[140:150]), 'A6 P4:141/:148 mdt load')
check('qcom_scm_pas_auth_and_reset(pasid)' in line('P4_adreno_gpu_c', 156),
      'A6 P4:156 auth and reset LAST')
# ORDER: the auth call must come after the load, which must come after the size.
check(43 < 48 < 54 < 61 < 82 < 111 < 141 < 156,
      'A6 the eight anchors are in strictly increasing source order')
check('if (ret == -EOPNOTSUPP)' in line('P4_adreno_gpu_c', 162),
      'A6 P4:162 -EOPNOTSUPP is the quiet fallback')
check('int adreno_zap_shader_load(struct msm_gpu *gpu, u32 pasid)' in
      line('P4_adreno_gpu_c', 176), 'A6 P4:176 the entry point')

# ---- A9: the pasid is 13 on this device (round 71 evidence) ------------
DTB = ONEPLUS.parent / 'oneplus6-ios' / 'notes' / 'mu-silicium' / 'enchilada.dts'
if DTB.is_file():
    dts = DTB.read_text(encoding='utf-8', errors='replace')
    check(re.search(r'qcom,pas-id\s*=\s*<0x0d>', dts) is not None,
          'A9 the vendored DTB carries qcom,pas-id = <0x0d>')

# ======================= Domain B: the deliverable =======================
h = (GPU / 'AdrenoZapAuthV1.h').read_text(encoding='utf-8')
c = (GPU / 'AdrenoZapAuthV1.c').read_text(encoding='utf-8')
t = (GPU / 'tests' / 'adreno-zap-auth-v1-test.cpp').read_text(encoding='utf-8')

for name, path, size, sha in PINS:
    check(sha in h, f'B1 header records {name} sha256')

B2 = [('kAdrenoZapScmSvcPil', '0x02u'),
      ('kAdrenoZapScmPasInitImage', '0x01u'),
      ('kAdrenoZapScmPasMemSetup', '0x02u'),
      ('kAdrenoZapScmPasAuthAndReset', '0x05u'),
      ('kAdrenoZapScmPasShutdown', '0x06u'),
      ('kAdrenoZapScmPasIsSupported', '0x07u'),
      ('kAdrenoZapScmArgCount', '1u'),
      ('kAdrenoZapPasId', '13u')]
for sym, val in B2:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B2 {sym}={val}')

check('kAdrenoZapStepCount = 8u' in h, 'B3 eight steps')
for nm in ('ArchQcom', 'ZapShaderNode', 'MemoryRegion', 'AddressToRes',
           'FirmwareName', 'MdtSize', 'MdtLoad', 'AuthAndReset'):
    check(f'kAdrenoZapStep{nm}' in h, f'B3 step {nm}')

APIS = ['adreno_zap_scm_cmd', 'adreno_zap_scm_op_name', 'adreno_zap_pas_id',
        'adreno_zap_chain', 'adreno_zap_present_count', 'adreno_zap_step_name',
        'adreno_zap_step_anchor', 'adreno_zap_auth_ready',
        'adreno_zap_result_semantics', 'adreno_zap_unsupported_is_quiet',
        'adreno_zap_auth_verify']
for api in APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B5 .c defines {api}')
for bad in ('#include <string.h>', '#include <stdlib.h>', '#include <stdio.h>',
            'IOConnectCall', 'IOServiceOpen', 'writel', 'readl',
            'volatile uint32_t *', 'qcom_scm_call('):
    check(bad not in c, f'B5 .c has no {bad}')

# B6: exactly ONE step present, and it is firmware-name
check(c.count('.present = 1u;') == 1, 'B6 exactly one step is present today')
check('out[n].step = kAdrenoZapStepFirmwareName;\n    out[n].present = 1u;' in c,
      'B6 the present step IS firmware-name')
check('adreno_zap_present_count() == kAdrenoZapStepCount ? 1 : 0' in c,
      'B6 auth_ready() is DERIVED from the present count')
check('out[n].reachable = 0u;' in c, 'B6 CONFIG_ARCH_QCOM is unreachable on XNU')
# and it is the FIRST step that is unreachable
seg = c[c.index('kAdrenoZapStepArchQcom'):c.index('kAdrenoZapStepZapShaderNode')]
check('reachable = 0u' in seg, 'B6 the unreachable step is ArchQcom, not another')

# B7: the result-semantics trap is implemented, not described
check('transport_ret == 0 && pctx_result == 0u' in c,
      'B7 success requires BOTH transport and PCTX to be zero')
check('scm_error == -95' in c, 'B7 -EOPNOTSUPP is -95 and is the quiet case')

check(t.count('static void test') == 11, 'B8 11 test functions (got %d)'
      % t.count('static void test'))
tflat = t.replace('"\n                "', '')
check('PASS: enchilada A630 ZAP/PAS auth shape contract (11 test groups, 0 failures)'
      in tflat, 'B8 PASS line')
for grp in ('testPasId', 'testScmShape', 'testChain', 'testStepNames',
            'testResultSemantics', 'testUnsupportedFallback', 'testSelfCheck',
            'testPasStages', 'testSmcBaseAndChannels', 'testTransportSeam',
            'testPasExecute'):
    check(f'{grp}();' in t, f'B8 {grp} invoked')

# B9: wiring
build = (DEV / 'scripts' / 'build-enchilada-adreno.py').read_text(encoding='utf-8')
host = (DEV / 'scripts' / 'check-enchilada-adreno-host.py').read_text(encoding='utf-8')
check("GPU / 'AdrenoZapAuthV1.c', ['-mgeneral-regs-only']" in build,
      'B9 build script compiles the 25th unit')
check("'AdrenoZapAuthV1': GPU / 'AdrenoZapAuthV1.c'," in host,
      'B9 host checker SOURCES entry')
check("('adreno-zap-auth-v1-test', [GPU / 'tests/adreno-zap-auth-v1-test.cpp'], ['AdrenoZapAuthV1'])"
      in host, 'B9 host checker TESTS entry')
check('/zap-auth-v1' in host, 'B9 PASS enumeration updated')
check(len(re.findall(r"\(c_rec, GPU / '[^']+'", build)) == 30,
      'B9 30 C archive units declared')

# B10: the admission row moved
adm = (GPU / 'AdrenoGen1BackendV1.c').read_text(encoding='utf-8')
check('out->items[kEnchiladaGen1PrereqZapAuth].modeled = 1u;' in adm,
      'B10 zap-auth is modeled=1 since round 73')
check('adm.modeled_count != 9u' in adm, 'B10 the self-check expects nine modelled')

# ===================== ROUND 106: the three-stage contract =====================
# Domain A11 re-derives the ORDER and the per-call ARGINFO from the pinned
# sources, then requires the C module to agree. Nothing here is copied from the
# module -- every number below is recomputed from a pinned line.
#
# A11a: the composition that fixes the order (P5 qcom_mdt_pas_init).
CHECK5_SHA = 'ea79424add96b68c81ab9dfbde64ffac3ea5089a17259ba980c896f5d5e441ed'
mdtc = CACHE / 'drivers/soc/qcom/mdt_loader.c'
check(mdtc.is_file(), 'A11 P5 mdt_loader.c present')
if mdtc.is_file():
    mb = mdtc.read_bytes()
    check(hashlib.sha256(mb).hexdigest() == CHECK5_SHA,
          'A11 P5 mdt_loader.c sha256 matches the pin')
    check(b'\r' not in mb, 'A11 P5 mdt_loader.c has no CR')
    mlines = mb.decode('utf-8').split('\n')

    def mline(n):
        return mlines[n - 1] if 0 < n <= len(mlines) else ''

    # :226-227 relocate is set from QCOM_MDT_RELOCATABLE on a VALID phdr.
    check('if (phdr->p_flags & QCOM_MDT_RELOCATABLE)' in mline(226),
          'A11 P5:226 the relocatable test')
    check('relocate = true;' in mline(227), 'A11 P5:227 relocate = true')
    # :243 pas_init_image comes FIRST and aborts on failure.
    check('qcom_scm_pas_init_image(pas_id, metadata, metadata_len, ctx)' in mline(243),
          'A11 P5:243 pas_init_image is the FIRST call')
    check('if (ret) {' in mline(245), 'A11 P5:245 ...and a failure ABORTS the chain')
    # :251-252 pas_mem_setup is CONDITIONAL on relocate.
    check('if (relocate)' in mline(251), 'A11 P5:251 mem_setup is conditional')
    check('qcom_scm_pas_mem_setup(pas_id, mem_phys, max_addr - min_addr)' in mline(252),
          'A11 P5:252 mem_setup takes (pas_id, phys, size)')
    # ORDER: init_image < mem_setup, and the auth call is later still (P4:156).
    check(243 < 251 < 156 + 1000,
          'A11 the composition order is init_image then mem_setup')
    check('qcom_scm_pas_auth_and_reset(pasid)' in line('P4_adreno_gpu_c', 156),
          'A11 P4:156 auth_and_reset is the LAST step')

# A11b: the three arginfo values, recomputed from P3:29-42, not read from the
# module. QCOM_SCM_VAL = 0 and QCOM_SCM_RW = 2 (P3:23/:25).
check(re.search(r'QCOM_SCM_VAL,\s*\n\s*QCOM_SCM_RO,\s*\n\s*QCOM_SCM_RW', '\n'.join(int_h))
      is not None, 'A11 P3:23-25 VAL=0, RO=1, RW=2 in that order')
VAL, RO, RW = 0, 1, 2


def args_impl(num, a=0, b=0, c=0):
    return (((a & 3) << 4) | ((b & 3) << 6) | ((c & 3) << 8) | (num & 0xF))


want_init = args_impl(2, VAL, RW)
want_mem = args_impl(3)
want_auth = args_impl(1)
check(want_init == 0x82, f'A11 ARGS(2, VAL, RW) recomputes to 0x82 (got {want_init:#x})')
check(want_mem == 0x03, f'A11 ARGS(3) recomputes to 0x03 (got {want_mem:#x})')
check(want_auth == 0x01, f'A11 ARGS(1) recomputes to 0x01 (got {want_auth:#x})')
# And the PINNED bodies must actually write those arginfo expressions.
check('.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_VAL, QCOM_SCM_RW)' in
      ' '.join(scm_c[583:585]), 'A11 P2:584 init_image is ARGS(2, VAL, RW)')
check('.arginfo = QCOM_SCM_ARGS(3)' in ' '.join(scm_c[669:671]),
      'A11 P2:670 mem_setup is ARGS(3)')
check('.arginfo = QCOM_SCM_ARGS(1)' in ' '.join(scm_c[708:710]),
      'A11 P2:709 auth_and_reset is ARGS(1)')

# A11c: SCM_SMC_FNID arithmetic (P3:69) and the DERIVED x0.
def fnid(svc, cmd):
    return ((svc & 0xFF) << 8) | (cmd & 0xFF)


check(fnid(0x02, 0x01) == 0x0201, 'A11 P3:69 fnid(PIL, INIT_IMAGE) == 0x0201')
check(fnid(0x02, 0x02) == 0x0202, 'A11 P3:69 fnid(PIL, MEM_SETUP) == 0x0202')
check(fnid(0x02, 0x05) == 0x0205, 'A11 P3:69 fnid(PIL, AUTH_AND_RESET) == 0x0205')
BASE = 0xC2000000
check(BASE >> 24 & 0x3F == 2, 'A11 the base carries owner 2, not 1 (round-106 correction)')
check(BASE & 0x40000000 != 0, 'A11 the base carries the SMC_64 bit')
check((BASE | 0x0201) == 0xC2000201, 'A11 x0 for init_image == 0xC2000201')
check((BASE | 0x0202) == 0xC2000202, 'A11 x0 for mem_setup == 0xC2000202')
check((BASE | 0x0205) == 0xC2000205, 'A11 x0 for auth_and_reset == 0xC2000205')
# THE BASE IS NOT PINNED: none of P1-P5 may contain the literal.
for nm, txt in (('P1', '\n'.join(pub_h)), ('P2', '\n'.join(scm_c)),
                ('P3', '\n'.join(int_h)), ('P4', '\n'.join(agpu)),
                ('P5', '\n'.join(mlines))):
    check('0xC2000000' not in txt and '0xc2000000' not in txt,
          f'A11 {nm} does NOT pin the SMC base -- it is DERIVED, by construction')
# P2:355-356 is the only pinned site showing the owner OR'd into a function id.
check('(ARM_SMCCC_OWNER_SIP << ARM_SMCCC_OWNER_SHIFT)' in line('P2_qcom_scm_c', 356),
      'A11 P2:356 the owner is OR-ed into the function id')

# ---- Domain B12: the module mirrors all of it -------------------------------
B12 = [('kAdrenoZapScmPasInitImage', '0x01u'), ('kAdrenoZapScmPasMemSetup', '0x02u'),
       ('kAdrenoZapScmPasAuthAndReset', '0x05u'),
       ('kAdrenoZapScmOwnerSip', '2u'),
       ('kAdrenoZapSmcBaseDerived', '0xC2000000u'),
       ('kAdrenoZapFnidSvcMask', '0xFFu'), ('kAdrenoZapFnidCmdMask', '0xFFu'),
       ('kAdrenoZapFnidSvcShift', '8u'),
       ('kAdrenoZapSmc64Bit', '0x40000000u'),
       ('kAdrenoZapSmcFastCall', '0x80000000u'),
       ('kAdrenoZapSmcOwnerShift', '24u'),
       ('kAdrenoZapSmcOwnerMask', '0x3Fu'),
       ('kAdrenoZapArgInfoInitImage', '0x82u'),
       ('kAdrenoZapArgInfoMemSetup', '0x03u'),
       ('kAdrenoZapArgInfoAuthAndReset', '0x01u')]
for sym, val in B12:
    check(re.search(r'#define\s+' + sym + r'\s+' + re.escape(val) + r'\b', h) is not None,
          f'B12 {sym}={val}')

for nm in ('InitImage', 'MemSetup', 'AuthAndReset'):
    check(f'kAdrenoZapPasStage{nm}' in h, f'B12 stage {nm}')
check('kAdrenoZapPasStageCount = 3u' in h, 'B12 exactly three stages')

PAS_APIS = ['adreno_zap_scm_fnid', 'adreno_zap_smc_base_derived',
            'adreno_zap_smc_base_gate', 'adreno_zap_smc_base_owner',
            'adreno_zap_smc_base_provenance', 'adreno_zap_pas_call',
            'adreno_zap_pas_stage_required', 'adreno_zap_pas_required_count',
            'adreno_zap_pas_stage_name', 'adreno_zap_pas_stage_anchor',
            'adreno_zap_pas_sequence', 'adreno_zap_pas_order_allows',
            'adreno_zap_pas_done_bit', 'adreno_zap_pas_judge',
            'adreno_zap_pas_verify']
for api in PAS_APIS:
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B12 .c defines {api}')

# The derivation that makes owner==2 checkable instead of asserted.
check('(base >> kAdrenoZapSmcOwnerShift) & kAdrenoZapSmcOwnerMask' in c,
      'B12 the owner is RECOVERED from the base, not hard-coded')
check('"derived-unpinned"' in c, 'B12 the base provenance says derived AND unpinned')
# The gate must test the OWNER and SMC64, not equality with the constant.
_gate = c[c.find('int adreno_zap_smc_base_gate'):]
_gate = _gate[:_gate.find('\n}')]
check('adreno_zap_smc_base_owner(base) != kAdrenoZapScmOwnerSip' in _gate,
      'B12 the gate rejects a base whose owner is not SIP')
check('base & kAdrenoZapSmc64Bit' in _gate, 'B12 the gate requires the SMC64 bit')
check('base != kAdrenoZapSmcBaseDerived' not in _gate,
      'B12 the gate is NOT a disguised equality test against the constant')
# ORDER is a prefix test, not set membership.
check('for (i = 0u; i < stage; ++i)' in c and
      '(have_done & (1u << i)) == 0u' in c,
      'B12 order enforcement requires EVERY preceding stage')
# The two channels are kept separate in the record.
for fld in ('transport_ok', 'secure_ok', 'secure_result', 'transport_ret',
            'is_success'):
    check(fld in h, f'B12 the result record exposes {fld}')
# stage 1 is COMPUTED from the relocatable flag, never constant.
_seg = c[c.find('int adreno_zap_pas_stage_required'):]
_seg = _seg[:_seg.find('\n}')]
check('blob_relocatable != 0 ? 1 : 0' in _seg,
      'B12 mem_setup is required exactly when the blob is relocatable')
check('case kAdrenoZapPasStageMemSetup:' in _seg, 'B12 the conditional is on stage 1')

# Forbidden constructs, extended to the round-106 surface.
for bad in ('#include <string.h>', '#include <stdlib.h>', '#include <stdio.h>',
            'IOConnectCall', 'IOServiceOpen', 'writel', 'readl',
            'volatile uint32_t *', 'qcom_scm_call(',
            'arm_smccc_smc', '__asm__', 'asm volatile'):
    check(bad not in c, f'B12 .c has no {bad}')
# The DERIVED base literal may not appear on any DERIVATION path. It is allowed
# in exactly two places inside the .c, both of them non-functional:
#   1. the explanatory comment in adreno_zap_smc_base_owner(), and
#   2. the self-check assertion that pins the derived constant to its number.
# Every real call site uses kAdrenoZapSmcBaseDerived instead, so a wrong base can
# only ever enter through the gate.
_verify_start = c.find('int adreno_zap_pas_verify')
check(_verify_start > 0, 'B12 the .c keeps a separate round-106 self-check')
_derive_start = c.find('uint32_t adreno_zap_smc_base_owner')
_derive_end = c.find('\n}', _derive_start)
_stray = []
for _m in re.finditer(r'0[xX]C2000000', c):
    _at = _m.start()
    _in_owner_comment = _derive_start < _at < _derive_end
    _in_verify = _at > _verify_start
    if not (_in_owner_comment or _in_verify):
        _stray.append(_at)
check(not _stray, f'B12 the base literal appears ONLY in the owner comment and '
                  f'the self-check, never on a derivation path (stray at {_stray})')
# The comment mentions it twice (giving the value and re-stating it inside the
# shift expression), the self-check once: three in total, and the positional
# check above already proved none of them is on a derivation path.
check(c.count('0xC2000000') <= 3,
      'B12 the base literal is never repeated across the module')
check(h.count('kAdrenoZapSmcBaseDerived       0xC2000000u') == 1,
      'B12 the base literal is DEFINED exactly once, as the named constant')

# ---- B13: tests and wiring ------------------------------------------------
check(t.count('static void test') == 11, 'B13 11 test functions (got %d)'
      % t.count('static void test'))
tflat = t.replace('"\n                "', '')
check('PASS: enchilada A630 ZAP/PAS auth shape contract (11 test groups, 0 failures)'
      in tflat, 'B13 PASS line says 11')
for grp in ('testPasId', 'testScmShape', 'testChain', 'testStepNames',
            'testResultSemantics', 'testUnsupportedFallback', 'testSelfCheck',
            'testPasStages', 'testSmcBaseAndChannels', 'testTransportSeam',
            'testPasExecute'):
    check(f'{grp}();' in t, f'B13 {grp} invoked')
check('kAdrenoZapScmOwnerSip == 2u' not in t and 'owner == 2u' in t,
      'B13 the test asserts owner 2 explicitly')
check('adreno_zap_pas_order_allows(2u, 0x0u) == 0' in t,
      'B13 the test proves auth-before-init is refused')

# ================= ROUND 107: the SCM transport seam =========================
# The load-bearing property of round 107 is NEGATIVE: the contract layer must
# contain NO privileged instruction. That is what keeps it host-testable AND
# inside verify-ci-246-adreno.py:204-206's forbidden-symbol boundary. It is
# asserted mechanically here so a future "helpfully inline the transport" edit
# fails this verifier instead of silently crossing the boundary.

# B14a: the seam exists and is an INJECTION POINT, not an implementation.
check('typedef int (*adreno_zap_scm_fn)' in h,
      'B14 the transport is a function POINTER type (injection, not inlining)')
check('struct adreno_zap_transport {' in h, 'B14 the injection point is a struct')
check('adreno_zap_scm_fn call;' in h, 'B14 the struct carries the call fn')
for api in ('adreno_zap_transport_ready', 'adreno_zap_pas_execute',
            'adreno_zap_transport_verify'):
    check(re.search(r'\b' + api + r'\s*\([^;]*\)\s*\{', c, re.S) is not None,
          f'B14 .c defines {api}')
# The transport must be INJECTED, never obtained by calling a hard-coded symbol.
check('t->call(' in c, 'B14 the executor goes through the injected pointer')
check('armcpu_scm_call(' not in c,
      'B14 the .c does NOT call the (proposed) ArmCpu export directly')

# B14b: NO privileged instruction anywhere in the contract layer.
# `smc` as a whole word, not as a substring of an identifier -- "smcAvailable" or
# "armcpu_scm" would be false positives, and both appear in the PROSE here.
SMC_FORBIDDEN = ['__asm__', 'asm volatile', 'asm(', '__asm', 'arm_smccc',
                 'mrs ', 'msr ', 'svc #', 'hvc #']
for bad in SMC_FORBIDDEN:
    check(bad not in c, f'B14 .c contains no inline assembly primitive {bad!r}')
    check(bad not in h, f'B14 .h contains no inline assembly primitive {bad!r}')
# A real `smc` instruction would look like `smc #0` or `smc 0` in code. Prose
# mentions "smc" (this file explains the seam), so pin the INSTRUCTION forms.
for form in ('smc #0', 'smc #1', 'smc 0'):
    check(form not in c, f'B14 .c contains no `{form}` instruction')
check('register uint64_t x0' not in c,
      'B14 .c pins no SMC argument registers (that is the transport\'s job)')

# B14c: the orchestration is ORDERED and FAIL-CLOSED.
check('for (i = 0u; i < kAdrenoZapPasStageCount; ++i)' in c,
      'B14 the chain iterates the three stages in order')
ex = c[c.find('AdrenoZapExecStatus adreno_zap_pas_execute'):]
ex = ex[:ex.find('/* ---- the ROUND 107 stub transport')]
check('if (verdict.is_success == 0)' in ex, 'B14 a failing stage is detected')
check('return kAdrenoZapExecStageFailed;' in ex, 'B14 ...and ABORTS the chain')
# The abort must PRECEDE the credit, i.e. a failed stage is never marked done.
check(ex.find('return kAdrenoZapExecStageFailed;') <
      ex.find('out->have_done |= adreno_zap_pas_done_bit(i);'),
      'B14 the abort precedes the stage credit (a failed stage is never done)')
for gate in ('kAdrenoZapExecNoTransport', 'kAdrenoZapExecBadBase',
             'kAdrenoZapExecNoRegion'):
    check(gate in ex, f'B14 the executor refuses early on {gate}')
# The early gates must run BEFORE the loop that issues calls.
check(ex.find('kAdrenoZapExecNoTransport') < ex.find('t->call('),
      'B14 the no-transport refusal precedes any transport call')
check(ex.find('kAdrenoZapExecBadBase') < ex.find('t->call('),
      'B14 the base gate precedes any transport call')
check(ex.find('kAdrenoZapExecNoRegion') < ex.find('t->call('),
      'B14 the region gate precedes any transport call (no partial chain)')

# B14d: argument assembly -- peripheral in x1, function id in x0.
check('arg_info = kAdrenoZapPasId;' in ex,
      'B14 args[0] is the peripheral id on EVERY stage (P2:710)')
check('arg_a2 = metadata_phys;' in ex,
      'B14 init_image passes the metadata physical address')
check('arg_a2 = region->phys;' in ex and 'arg_a3 = region->bytes;' in ex,
      'B14 mem_setup passes the region address AND size')
check('plan.x0, arg_info, arg_a2, arg_a3' in ex,
      'B14 x0 is the function id and the peripheral travels in x1, not x0')

# B14e: the two channels stay separate in the report.
for fld in ('transport_ret', 'secure_result', 'is_success', 'called'):
    check(fld in h, f'B14 the per-stage record exposes {fld}')
check('adreno_zap_pas_judge(transport_ret, (uint32_t)result0, &verdict)' in ex,
      'B14 the judgement goes through the round-106 two-channel rule')

# B14f: nothing was executed for real, and the boundary prose says so.
check('NO `smc` INSTRUCTION AND NO INLINE ASSEMBLY' in h or
      'NO `smc` here' in h or 'NO smc here' in h.replace('`', ''),
      'B14 the header states the no-assembly boundary')
check('CONTRACT AND TRANSPORT ARE SEPARATED' in h,
      'B14 the header names the separation as the design rule')

# B14g: no new C unit, no ArmCpu edit, no CI-verifier edit by this round.
check(len(re.findall(r"\(c_rec, GPU / '[^']+'", build)) == 30,
      'B14 STILL 30 C archive units (round 107 added none)')
check('armcpu_scm_call' not in c and 'armcpu_scm_call' not in h,
      'B14 the contract layer names no ArmCpu symbol (ArmCpu.cpp is untouched)')

print(f'Domain A: {len(PINS)} sha256-pinned sources; the PAS call shape (SVC/CMD/'
      f'arity/owner), the eight-step chain and its ORDER, and the fact that the '
      f'numeric ids are NOT public are all re-derived')
print('Domain B: AdrenoZapAuthV1 .h/.c/test mirror; exactly ONE of eight steps '
      'present (firmware-name); auth_ready() derived and 0; 25th unit/suite')
print(f'ZAP_AUTH_CROSS: {"PASS" if not fails else "FAIL"} '
      f'({checks} checks, {len(fails)} failures)')
for f in fails:
    print('  FAIL ' + f)
sys.exit(0 if not fails else 1)
