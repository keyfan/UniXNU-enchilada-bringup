#!/usr/bin/env python3
"""Fetch only the SHA-pinned Linux v6.12 clock sources used by the round-61
GPUCC clock-control DERIVATION (pin-before-derive, same method as
scripts/fetch-enchilada-gpu-gdsc-sources.py). The .cache/ tree is gitignored.

WHY: rounds 45-60 left the three CLOCK steps of the A630 power-up plan opaque
-- a bus->set_rate()/bus->clocks_enable() callback could do anything and every
existing test still passed. The clock tree makes those steps a FUNCTION of
pinned source, so their content becomes derivable rather than assumed -- but
only from exact pinned bytes.

Reads scripts/data/enchilada-gpu-clk-inputs.json, fetches from the immutable
jsdelivr v6.12 tag mirror, verifies exact bytes + sha256, and never executes or
trusts any fetched content beyond its hash. With $env:NO_PROXY='*' set for the
local Clash proxy that otherwise breaks GitHub/CDN TLS.

Usage: python -X utf8 scripts/fetch-enchilada-gpu-clk-sources.py
"""
import concurrent.futures
import hashlib
import json
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PINS = json.loads((ROOT / 'scripts/data/enchilada-gpu-clk-inputs.json').read_text(encoding='utf-8'))
LINUX = PINS['linux']
CACHE = ROOT / '.cache/adreno-a630-source'


def fetch(item):
    name, expected = item
    path = CACHE / name
    if path.exists():
        data = path.read_bytes()
    else:
        url = LINUX['mirror'] + name
        request = urllib.request.Request(url, headers={'User-Agent': 'enchilada-gpu-clk-pin'})
        with urllib.request.urlopen(request, timeout=90) as response:
            data = response.read()
    # A pin without a hash would silently accept anything; refuse rather than skip.
    if 'sha256' not in expected or 'bytes' not in expected:
        raise ValueError('Incomplete pin (bytes/sha256 required): ' + name)
    if len(data) != expected['bytes'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
        raise ValueError('Pinned input hash/size differs: ' + name)
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return len(data)


if __name__ == '__main__':
    files = {k: v for k, v in LINUX['files'].items() if 'bytes' in v and 'sha256' in v}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        sizes = list(executor.map(fetch, files.items()))
    print('PASS: %d exact Linux v6.12 GPUCC clock source inputs, %d bytes; derivation pins only, no device claim'
          % (len(sizes), sum(sizes)))
