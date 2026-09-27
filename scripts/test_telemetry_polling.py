#!/usr/bin/env python3
"""Compile the hardware-free telemetry poll owner and its model regression."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache' / 'telemetry-polling-tests'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    if os.name == 'nt' and zig.exists():
        compiler = [str(zig), 'c++']
    else:
        compiler = [os.environ.get('CXX') or shutil.which('c++') or 'g++']
    executable = OUT / ('telemetry-polling.exe' if os.name == 'nt' else 'telemetry-polling')
    subprocess.run([
        *compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1',
        '-DTELEMETRY_POLLING_STANDALONE', '-I' + str(ROOT / 'src'),
        str(ROOT / 'src/ui-touch/services/TelemetryPolling.cpp'),
        str(ROOT / 'test/test_telemetry_polling.cpp'),
        '-o', str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)


if __name__ == '__main__':
    main()
