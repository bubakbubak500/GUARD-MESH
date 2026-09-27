#!/usr/bin/env python3
"""Build and run portable SD lifecycle owner tests."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache' / 'storage-maintenance-tests'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    if os.name == 'nt' and zig.exists():
        compiler = [str(zig), 'c++']
    else:
        compiler = [os.environ.get('CXX') or shutil.which('c++') or 'g++']
    executable = OUT / ('storage-maintenance.exe' if os.name == 'nt' else 'storage-maintenance')
    threading_flags = [] if os.name == 'nt' else ['-pthread']
    subprocess.run([
        *compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
        *threading_flags, '-I' + str(ROOT / 'src'),
        str(ROOT / 'src/ui-touch/platform/StorageAccess.cpp'),
        str(ROOT / 'src/ui-touch/services/StorageMaintenance.cpp'),
        str(ROOT / 'test/test_storage_maintenance.cpp'), '-o', str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)


if __name__ == '__main__':
    main()
