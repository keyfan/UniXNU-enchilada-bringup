#!/usr/bin/env python3
"""Round 63 cross-check: re-derive the bounded GPUCC window's limits in Python.

Method 2 (independent of the C and the C++ test): this script does NOT read
AdrenoGpuccWindowV1.c's arithmetic and does not trust the test's expectations.
It re-derives the same three bounds from the pinned board facts and the pinned
Linux source lines, then asserts the C source actually contains those values
and that the executor's real offsets fit inside the result.

The point of a cross-check is to catch a number that is self-consistent inside
the module but wrong against the board. The interesting one here is that the
DTB window (0x9000) and the vendor regmap limit (0x8008) are DIFFERENT, so a
window that trusts the DTB alone is 4 KiB too generous.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GPU = ROOT / 'drivers' / 'NativeAdreno'

checks = 0
failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print(f'FAIL: {what}')


def read(path: Path) -> str:
    return path.read_text(encoding='utf-8', errors='replace')


def define(text: str, name: str):
    """Read a #define value, tolerating a trailing /* comment */."""
    m = re.search(rf'^#define\s+{re.escape(name)}\s+(\S+?)\s*(?:/\*.*?\*/)?\s*$',
                  text, re.MULTILINE)
    if not m:
        return None
    raw = m.group(1).rstrip('uUlL')
    try:
        return int(raw, 0)
    except ValueError:
        return raw


def main() -> int:
    win_h = read(GPU / 'AdrenoGpuccWindowV1.h')
    win_c = read(GPU / 'AdrenoGpuccWindowV1.c')
    clk_h = read(GPU / 'AdrenoClkCtlV1.h')
    dom_h = read(GPU / 'AdrenoDomainState.h')

    # --- 1. the two independent board facts must agree -------------------
    regmap = define(clk_h, 'ADRENO_CLK_CTL_V1_GPUCC_MAX_REG')
    dtb = define(clk_h, 'ADRENO_CLK_CTL_V1_GPUCC_SIZE')
    dom_reg = define(dom_h, 'ADRENO_DOMAIN_GPUCC_MAX_REGISTER')
    dom_size = define(dom_h, 'ADRENO_DOMAIN_GPUCC_SIZE')

    check(regmap == 0x8008, f'clk-ctl regmap limit is 0x8008 (got {regmap!r})')
    check(dtb == 0x9000, f'clk-ctl DTB window is 0x9000 (got {dtb!r})')
    check(dom_reg == 0x8008, f'domain-state regmap limit agrees (got {dom_reg!r})')
    check(dom_size == 0x9000, f'domain-state DTB window agrees (got {dom_size!r})')
    check(regmap != dtb, 'the regmap limit and the DTB window are DIFFERENT numbers')

    # --- 2. the window header must DERIVE, not retype -------------------
    check(define(win_h, 'ADRENO_GPUCC_WINDOW_V1_MAX_REGISTER') ==
          'ADRENO_CLK_CTL_V1_GPUCC_MAX_REG',
          'window max_register is derived from the round-61 constant')
    check(define(win_h, 'ADRENO_GPUCC_WINDOW_V1_DTB_BYTES') ==
          'ADRENO_CLK_CTL_V1_GPUCC_SIZE',
          'window DTB size is derived from the round-61 constant')
    check(define(win_h, 'ADRENO_GPUCC_WINDOW_V1_STRIDE') == 4,
          'window stride is 4')

    # Guard against a retyped literal sneaking back in.
    check('0x8008' not in win_h.split('ADRENO_GPUCC_WINDOW_V1_MAX_REGISTER')[1]
          .split('\n')[0],
          'the max_register define line contains no retyped 0x8008')

    # --- 3. re-derive the usable size the way the C does ----------------
    usable_from_dtb = min(dtb, regmap + 4)
    check(usable_from_dtb == 0x800c, f'derived usable size is 0x800c (got {usable_from_dtb:#x})')
    check(usable_from_dtb != dtb, 'derived usable size is NOT the DTB size')

    # The C must compute it, not hardcode 0x800c. Verify() is allowed to assert
    # the derived value (that is what a self-check is for), so the scan stops
    # before Verify and only inspects the production code above it.
    verify_start = win_c.find('int AdrenoGpuccWindowV1Verify')
    check(verify_start > 0, 'the C declares AdrenoGpuccWindowV1Verify')
    production = win_c[:verify_start] if verify_start > 0 else win_c
    check('0x800c' not in production,
          'the production code does not hardcode the usable size 0x800c')
    check('max_register + (uint64_t)ADRENO_GPUCC_WINDOW_V1_STRIDE' in production,
          'the production code adds one stride to max_register')
    check('(uint64_t)' in production, 'the C widens to 64 bits before adding')

    # --- 4. the executor's real offsets must fit ------------------------
    # These are the offsets the round-62 derivation writes/reads; re-derived
    # here from AdrenoClkCtlV1.h rather than copied from the test.
    rcg_cmd = define(clk_h, 'ADRENO_CLK_CTL_V1_RCG_CMD_RCGR')
    check(rcg_cmd == 0x1120, f'RCG CMD offset is 0x1120 (got {rcg_cmd!r})')
    offsets = {
        'CX GDSCR': 0x106c,
        'CX status': 0x1540,
        'GX status': 0x1010,
        'gmu CBCR': define(clk_h, 'ADRENO_CLK_CTL_V1_CX_GMU_CBCR'),
        'cxo CBCR': define(clk_h, 'ADRENO_CLK_CTL_V1_CXO_CBCR'),
        'RCG CMD': rcg_cmd,
        'RCG CFG': rcg_cmd + 4,
    }
    highest = max(offsets.values())
    check(highest == 0x1540, f'highest real offset is the CX status at 0x1540 (got {highest:#x})')
    check(highest + 4 <= usable_from_dtb,
          f'every real offset fits inside 0x{usable_from_dtb:x}')
    print(f'  highest real offset 0x{highest:x} (fits: {highest + 4 <= usable_from_dtb})')
    print(f'  usable 0x{usable_from_dtb:x} (regmap 0x{regmap:x} + 4), DTB says 0x{dtb:x}')

    # --- 5. the three bounds must each be present in the source ---------
    check('agw_aligned(byte_offset, ADRENO_GPUCC_WINDOW_V1_STRIDE)' in production,
          'bound 1 (alignment) is implemented in the C')
    check('+ (uint64_t)ADRENO_GPUCC_WINDOW_V1_STRIDE >' in production,
          'bound 2 (window end) is implemented in the C')
    check('AdrenoGpuccWindowV1UsableBytes(w)' in production,
          'bound 3 (regmap limit) is folded through usable in the C')

    # --- 6. the MMIO triple must refuse without a delay -----------------
    check('w->udelay == 0' in win_c, 'Mmio() checks for a missing delay')
    check('apb_delay' in read(GPU / 'AdrenoPowerUpBackendV1.c'),
          'the executor really calls a delay (so the NULL refusal matters)')

    # --- 7. byte vs word offset convention ------------------------------
    # AdrenoGmuBusV1 shifts word offsets; this adapter takes byte offsets.
    # The expression spans a line break in that file, so this is a regex.
    bus_c = read(GPU / 'AdrenoGmuBusV1.c')
    check(re.search(r'word_offset\s*<<\s*2', bus_c) is not None,
          'AdrenoGmuBusV1 shifts WORD offsets by 2 (different convention)')
    check('bytes vs words' in win_h or 'BYTE offsets' in win_h,
          'the window header states its byte-offset convention')
    check('<< 2' not in production,
          'this adapter does NOT shift (it takes byte offsets)')

    # --- 8. no MMIO in the header, no mapping anywhere ------------------
    check('#define' in win_h and 'volatile uint8_t *base' in win_h,
          'the header declares the caller-owned window pointer')
    check('armcpu_map_physical' not in win_c,
          'the adapter maps nothing itself')
    # The file header COMMENT says "no mapping, clock, regulator or power
    # action here"; only real calls would be a violation, so match call syntax.
    check(re.search(r'\bclk_set_rate\s*\(', production) is None,
          'the adapter calls no clk_set_rate')
    check(re.search(r'\bregulator_[a-z_]+\s*\(', production) is None,
          'the adapter calls no regulator API')

    print(f'\nGPUCC_WINDOW_V1_CROSS: '
          f'{"PASS" if failures == 0 else "FAIL"} ({checks} checks, {failures} failures)')
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
