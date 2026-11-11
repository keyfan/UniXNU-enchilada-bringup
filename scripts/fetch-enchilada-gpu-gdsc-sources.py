#!/usr/bin/env python3
"""Fetch only the SHA-pinned Linux v6.12 GDSC sources used by the round-56
GPU GDSC status-register DERIVATION (pin-before-derive, same method as
scripts/fetch-enchilada-kgsl-smmu-sources.py). The .cache/ tree is gitignored.

WHY: until round 55 the four constants in AdrenoDomainState.h were asserted in
a comment. gdsc.c makes them a FUNCTION of the GDSC descriptor, so the values
become derivable rather than asserted -- but only from exact pinned bytes.

Reads scripts/data/enchilada-gpu-gdsc-inputs.json, fetches from the immutable
jsdelivr v6.12 tag mirror, verifies exact bytes + sha256, and never executes or
trusts any fetched content beyond its hash. With $env:NO_PROXY='*' set for the
local Clash proxy that otherwise breaks GitHub/CDN TLS.

Usage: python -X utf8 scripts/fetch-enchilada-gpu-gdsc-sources.py
"""
import concurrent.futures
import hashlib
import json
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PINS = json.loads((ROOT / 'scripts/data/enchilada-gpu-gdsc-inputs.json').read_text(encoding='utf-8'))
LINUX = PINS['linux']
CACHE = ROOT / '.cache/adreno-a630-source'


def fetch(item):
    name, expected = item
    path = CACHE / name
    if path.exists():
        data = path.read_bytes()
    else:
        url = LINUX['mirror'] + name
        request = urllib.request.Request(url, headers={'User-Agent': 'enchilada-gpu-gdsc-pin'})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read()
    if len(data) != expected['bytes'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
        raise ValueError('Pinned input hash/size differs: ' + name)
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return len(data)


if __name__ == '__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        sizes = list(executor.map(fetch, LINUX['files'].items()))
    print('PASS: %d exact Linux v6.12 GDSC source inputs, %d bytes; derivation pins only, no device claim'
          % (len(sizes), sum(sizes)))
