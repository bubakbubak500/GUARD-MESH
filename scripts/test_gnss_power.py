#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and run the hardware-free GNSS protocol and power-state tests."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache' / 'gnss-power-tests'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    compiler = [str(zig), 'c++'] if os.name == 'nt' and zig.exists() else [
        os.environ.get('CXX') or shutil.which('c++') or 'g++']
    executable = OUT / ('gnss-power.exe' if os.name == 'nt' else 'gnss-power')
    sources = [ROOT / 'test/test_gnss_power.cpp', ROOT / 'src/helpers/GnssPowerControl.cpp']
    subprocess.run([*compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1',
                    '-I' + str(ROOT / 'src'), *map(str, sources),
                    '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    print('GNSS power: PASS')


if __name__ == '__main__':
    main()
