#!/usr/bin/env python3
"""Build and run the bounded heard-name index and persistence regressions."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache/heard-name-tests'

def main():
    OUT.mkdir(exist_ok=True)
    compiler = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    executable = OUT / ('heard-names.exe' if os.name == 'nt' else 'heard-names')
    subprocess.run([str(compiler), 'c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-I' + str(ROOT / 'simulator/include'), '-I' + str(ROOT / 'src'),
                    str(ROOT / 'test/test_heard_names.cpp'),
                    str(ROOT / 'src/ui-touch/models/HeardNameCache.cpp'),
                    str(ROOT / 'src/ui-touch/services/HeardNameService.cpp'),
                    str(ROOT / 'src/ui-touch/platform/StorageAccess.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=30)

if __name__ == '__main__':
    main()
