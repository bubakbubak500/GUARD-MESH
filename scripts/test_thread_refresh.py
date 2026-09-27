#!/usr/bin/env python3
"""Build and run the directory refresh policy regression."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache/thread-refresh-tests'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    compiler = [str(zig), 'c++'] if os.name == 'nt' and zig.exists() else [os.environ.get('CXX') or shutil.which('c++') or 'g++']
    executable = OUT / ('thread-refresh.exe' if os.name == 'nt' else 'thread-refresh')
    subprocess.run([*compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1',
                    '-I' + str(ROOT / 'src'), str(ROOT / 'test/test_thread_refresh.cpp'),
                    '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    print('Thread directory refresh: PASS (60s idle, invalidation, count changes, wrap)')

if __name__ == '__main__':
    main()
