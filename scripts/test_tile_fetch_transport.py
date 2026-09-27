#!/usr/bin/env python3
"""Compile the real ESP32 tile transport against a deterministic fake runtime."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache' / 'tile-fetch-transport-tests'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    if os.name == 'nt' and zig.exists():
        compiler = [str(zig), 'c++']
    else:
        compiler = [os.environ.get('CXX') or shutil.which('c++') or 'g++']
    executable = OUT / ('tile-fetch-transport.exe' if os.name == 'nt' else 'tile-fetch-transport')
    sources = [
        ROOT / 'src/ui-touch/platform/esp32/TileFetchTransport.cpp',
        ROOT / 'test/tile_fetch_transport/main.cpp',
    ]
    subprocess.run([
        *compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
        '-DESP32', '-DMULTI_TRANSPORT_COMPANION',
        '-I' + str(ROOT / 'test/tile_fetch_transport/include'),
        '-I' + str(ROOT / 'src'),
        *map(str, sources), '-o', str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)


if __name__ == '__main__':
    main()
