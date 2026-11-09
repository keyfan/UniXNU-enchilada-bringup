#!/usr/bin/env python3
"""Fetch only the SHA-pinned Linux v6.12 A6XX sources used by derive-a630-hwinit.py.

Mirrors UniXNU's scripts/fetch-adreno-inputs.py method (pin-before-derive), but for
the A630 generation: fetch from an immutable jsdelivr tag mirror, verify the exact
bytes+sha256 recorded in scripts/data/adreno-a630-inputs.json, and never execute or
trust any fetched content beyond its hash. The .cache/ tree is gitignored.
"""
import concurrent.futures
import hashlib
import json
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
pins = json.loads((ROOT / "scripts/data/adreno-a630-inputs.json").read_text())
linux = pins["linux"]
CACHE = ROOT / ".cache/adreno-a630-source"


def fetch(item):
    name, expected = item
    path = CACHE / name
    if path.exists():
        data = path.read_bytes()
    else:
        url = linux["mirror"] + name
        request = urllib.request.Request(url, headers={"User-Agent": "enchilada-a630-pin"})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read()
    if len(data) != expected["bytes"] or hashlib.sha256(data).hexdigest() != expected["sha256"]:
        raise ValueError("Pinned input hash/size differs: " + name)
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return len(data)


if __name__ == "__main__":
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        sizes = list(executor.map(fetch, linux["files"].items()))
    print("PASS: %d exact A6XX GEN1 source inputs, %d bytes; no GPU execution/authenticity claim"
          % (len(sizes), sum(sizes)))
