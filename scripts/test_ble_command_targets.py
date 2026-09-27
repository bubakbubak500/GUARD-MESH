#!/usr/bin/env python3
"""Check the hardware-free BLE command target used by the NimBLE worker."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache' / 'ble-command-target-tests'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    if os.name == 'nt' and zig.exists():
        compiler = [str(zig), 'c++']
    else:
        compiler = [os.environ.get('CXX') or shutil.which('c++') or 'g++']
    executable = OUT / ('ble-command-targets.exe' if os.name == 'nt' else 'ble-command-targets')
    subprocess.run([
        *compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1',
        '-DBLE_PIN_CODE', '-DMULTI_TRANSPORT_COMPANION',
        '-DBLE_COMMAND_TARGET_STANDALONE',
        '-I' + str(ROOT / 'src'),
        str(ROOT / 'test/test_ble_command_targets.cpp'), '-o', str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    print('BLE command targets: PASS')


if __name__ == '__main__':
    main()
