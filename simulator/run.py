"""Bootstrap, build and launch the Windows T-Deck simulator. Stdlib only."""
from pathlib import Path
import argparse
import json
import os
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.sim-cache'


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def bootstrap():
    deps = json.loads((ROOT / 'simulator/dependencies.json').read_text())
    CACHE.mkdir(exist_ok=True)
    for repo in deps['repositories']:
        path = CACHE / repo['directory']
        if not path.exists():
            print('Fetching', repo['directory'], flush=True)
            run('git', 'clone', '--depth', '1', '--branch', repo['tag'], repo['url'], path)
        head = run('git', '-C', path, 'rev-parse', 'HEAD', capture_output=True, text=True).stdout.strip()
        if head != repo['commit']:
            raise RuntimeError(f'{path} is at {head}; expected {repo["commit"]}. Restore the pinned dependency before building.')
    compiler = CACHE / 'toolchain/ziglang/zig.exe'
    if not compiler.exists():
        print('Installing the portable C/C++ compiler into .sim-cache', flush=True)
        run(sys.executable, '-m', 'pip', 'install', '--disable-pip-version-check', '--no-deps',
            '--only-binary=:all:', '--target', CACHE / 'toolchain', 'ziglang==' + deps['ziglang'])
    version = run(compiler, 'version', capture_output=True, text=True).stdout.strip()
    if version != deps['ziglang']:
        raise RuntimeError(f'Wrong compiler version: {version}; expected {deps["ziglang"]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', action='store_true', help='Build and run the hidden UI integration test')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    if os.name != 'nt' or platform.machine().lower() not in ('amd64', 'x86_64'):
        parser.error('This first simulator target requires Windows x64.')
    bootstrap()
    # One build at a time; normal launches never stop an existing simulator.
    import msvcrt
    with (CACHE / 'build.lock').open('a+b') as lock:
        lock.seek(0)
        if not lock.read(1):
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as exc:
            raise RuntimeError('Another simulator build is running. Wait for it to finish.') from exc
        run(sys.executable, ROOT / 'simulator/build.py')
    executable = CACHE / 'build' / (CACHE / 'build/executable.txt').read_text().strip()
    if args.test:
        artifacts = CACHE / 'test-artifacts'
        artifacts.mkdir(exist_ok=True)
        run(executable, '--smoke', cwd=artifacts, timeout=90, creationflags=subprocess.CREATE_NO_WINDOW)
        print('UI integration test passed. Screenshots:', artifacts)
    elif not args.build_only:
        log = (CACHE / 'simulator.log').open('ab')
        process = subprocess.Popen([str(executable)], cwd=CACHE, stdout=log, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        log.close()
        (CACHE / 'simulator.pid').write_text(str(process.pid))
        print('Simulator started. F1: help. F12: screenshot. PID:', process.pid)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print('Simulator:', error, file=sys.stderr)
        print('Build diagnostics: .sim-cache/build/errors.txt', file=sys.stderr)
        sys.exit(1)
