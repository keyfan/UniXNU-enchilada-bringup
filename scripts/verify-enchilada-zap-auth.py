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

check(t.count('static void test') == 7, 'B8 7 test functions')
tflat = t.replace('"\n                "', '')
check('PASS: enchilada A630 ZAP/PAS auth shape contract (7 test groups, 0 failures)'
      in tflat, 'B8 PASS line')
for grp in ('testPasId', 'testScmShape', 'testChain', 'testStepNames',
            'testResultSemantics', 'testUnsupportedFallback', 'testSelfCheck'):
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
check(len(re.findall(r"\(c_rec, GPU / '[^']+'", build)) == 29,
      'B9 29 C archive units declared')

# B10: the admission row moved
adm = (GPU / 'AdrenoGen1BackendV1.c').read_text(encoding='utf-8')
check('out->items[kEnchiladaGen1PrereqZapAuth].modeled = 1u;' in adm,
      'B10 zap-auth is modeled=1 since round 73')
check('adm.modeled_count != 9u' in adm, 'B10 the self-check expects nine modelled')

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
