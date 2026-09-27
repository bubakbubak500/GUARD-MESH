#!/usr/bin/env python3
"""Compile the real ESP32 network executor against a deterministic fake runtime."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache' / 'network-executor-tests'
CASES = (
    'queuecreatefail', 'stackallocfail', 'taskcreatefail', 'fullqueue',
    'immediate', 'activeforget', 'priority', 'diagnostics', 'deferred',
)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    if os.name == 'nt' and zig.exists():
        compiler = [str(zig), 'c++']
    else:
        compiler = [os.environ.get('CXX') or shutil.which('c++') or 'g++']
    executable = OUT / ('network-executor.exe' if os.name == 'nt' else 'network-executor')
    sources = [
        ROOT / 'src/ui-touch/platform/esp32/SharedNetworkExecutor.cpp',
        ROOT / 'src/ui-touch/services/TileRequestLedger.cpp',
        ROOT / 'test/network_executor/main.cpp',
    ]
    subprocess.run([
        *compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
        '-DESP32', '-DMULTI_TRANSPORT_COMPANION',
        '-I' + str(ROOT / 'test/network_executor/include'),
        '-I' + str(ROOT / 'src'),
        *map(str, sources), '-o', str(executable),
    ], check=True)
    for scenario in CASES:
        subprocess.run([str(executable), scenario], check=True, timeout=15)


if __name__ == '__main__':
    main()
