#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the T-Deck adapter with the installed production NMEA provider."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LIBS = ROOT / '.pio/libdeps/LilyGo_TDeck_companion_radio_touch'

def main():
    out = ROOT / '.sim-cache/tdeck-gps-tests'
    out.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    compiler = [str(zig), 'c++'] if os.name == 'nt' and zig.exists() else [
        os.environ.get('CXX') or shutil.which('c++') or 'g++']
    includes = [ROOT / 'test/gnss_stubs', ROOT / 'src', LIBS / 'MeshCore/src',
                LIBS / 'MicroNMEA/src']
    for directory in includes:
        if not directory.is_dir():
            raise RuntimeError(f'Missing installed dependency: {directory}')
    executable = out / ('tdeck-gps.exe' if os.name == 'nt' else 'tdeck-gps')
    subprocess.run([*compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-O1', '-DLILYGO_TDECK=1', '-DESP32=1',
                    *['-I' + str(path) for path in includes],
                    str(ROOT / 'test/test_tdeck_gps.cpp'),
                    str(ROOT / 'src/helpers/GnssPowerControl.cpp'),
                    str(LIBS / 'MicroNMEA/src/MicroNMEA.cpp'),
                    '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)

if __name__ == '__main__':
    main()
