#!/usr/bin/env python3
"""Build and run the hardware-free message ingress regression suite."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache' / 'message-ingress-tests'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    if os.name == 'nt' and zig.exists():
        compiler = [str(zig), 'c++']
    else:
        compiler = [os.environ.get('CXX') or shutil.which('c++') or 'g++']
    executable = OUT / ('message-ingress.exe' if os.name == 'nt' else 'message-ingress')
    sources = [ROOT / 'test/test_message_ingress.cpp',
               ROOT / 'src/ui-touch/application/MessageIngress.cpp']
    subprocess.run([*compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1',
                    '-I' + str(ROOT / 'src'), *map(str, sources),
                    '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    print('Message ingress: PASS')


if __name__ == '__main__':
    main()
