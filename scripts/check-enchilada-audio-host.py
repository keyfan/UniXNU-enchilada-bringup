#!/usr/bin/env python3
"""Host runner for the stage-7 audio feasibility study.

Mirrors check-enchilada-modem-host.py: compile the C core strictly, then link the
C++17 test against it. Sanitizers are used only when the toolchain really has
them, because a missing runtime would look like a test failure.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIO = ROOT / "drivers/NativeAudio"
W = ROOT.parents[1]


def sanitizers(cc, cxx):
    probe = tempfile.mkdtemp(prefix="audio-san-")
    src = Path(probe) / "probe.c"
    src.write_text("int main(void){return 0;}\n", encoding="utf-8")
    out = []
    for flag in ("-fsanitize=address", "-fsanitize=undefined"):
        r = subprocess.run([cc, flag, str(src), "-o", str(Path(probe) / "p")],
                           capture_output=True, text=True)
        if r.returncode == 0:
            out.append(flag)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc", default=str(W / "tools/llvm-mingw/bin/clang.exe"))
    ap.add_argument("--cxx", default=str(W / "tools/llvm-mingw/bin/clang++.exe"))
    ap.add_argument("--out", default=str(ROOT / "work/tmp/audio-host-out"))
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    san = sanitizers(args.cc, args.cxx)
    if not san:
        print("note: sanitizers unavailable, strict build only")

    core = out / "AudioRoute.o"
    cmd = [args.cc, "-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
           *san, "-I", str(AUDIO), "-c", str(AUDIO / "AudioRoute.c"),
           "-o", str(core)]
    subprocess.run(cmd, check=True)

    exe = out / "audio-route-test"
    cmd = [args.cxx, "-std=c++17", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
           *san, "-I", str(AUDIO), str(AUDIO / "tests/audio-route-test.cpp"),
           str(core), "-o", str(exe)]
    subprocess.run(cmd, check=True)

    r = subprocess.run([str(exe)], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode != 0:
        sys.exit(r.returncode)
    print("PASS: enchilada stage-7 audio feasibility host tests "
          "(without sanitizers)" if not san else
          "PASS: enchilada stage-7 audio feasibility host tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
