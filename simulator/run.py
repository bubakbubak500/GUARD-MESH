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


def run_smoke(executable, mode, artifacts):
    # Explicit pipes keep diagnostics available with CREATE_NO_WINDOW on Windows.
    log = artifacts / 'smoke.log'
    try:
        # Includes Guardian settings/app navigation, paged RPC and recovery,
        # followed by all shared screen lifetime checks. Allow slower CI hosts.
        result = subprocess.run([str(executable), mode], cwd=artifacts, timeout=180,
                                creationflags=subprocess.CREATE_NO_WINDOW,
                                capture_output=True, text=True, encoding='utf-8', errors='replace')
    except subprocess.TimeoutExpired as error:
        # TimeoutExpired may carry bytes even when text=True was requested.
        def captured(value):
            return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else (value or '')
        log.write_text(captured(error.stdout) + '\n' + captured(error.stderr), encoding='utf-8')
        print('UI scenario diagnostics:', log, file=sys.stderr)
        raise
    log.write_text(result.stdout + '\n' + result.stderr, encoding='utf-8')
    if result.returncode:
        print((result.stdout + '\n' + result.stderr)[-5000:], file=sys.stderr)
        print('UI scenario diagnostics:', log, file=sys.stderr)
    result.check_returncode()


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
    parser.add_argument('--test', action='store_true', help='Run model/service tests and hidden English/Czech/keyboard-navigation UI integration tests')
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
        run(sys.executable, ROOT / 'scripts/test_ui_models.py')
        run(sys.executable, ROOT / 'scripts/test_heard_names.py')
        run(sys.executable, ROOT / 'scripts/test_storage_access.py')
        run(sys.executable, ROOT / 'scripts/test_storage_maintenance.py')
        run(sys.executable, ROOT / 'scripts/test_sd_health_monitor.py')
        run(sys.executable, ROOT / 'scripts/test_back_navigation.py')
        run(sys.executable, ROOT / 'scripts/test_screen_policy.py')
        run(sys.executable, ROOT / 'scripts/test_chat_session.py')
        run(sys.executable, ROOT / 'scripts/test_message_ingress.py')
        run(sys.executable, ROOT / 'scripts/test_thread_refresh.py')
        run(sys.executable, ROOT / 'scripts/test_network_executor.py')
        run(sys.executable, ROOT / 'scripts/test_tile_fetch_transport.py')
        run(sys.executable, ROOT / 'scripts/test_ble_command_targets.py')
        run(sys.executable, ROOT / 'scripts/test_guardian_status.py')
        run(sys.executable, ROOT / 'scripts/test_telemetry_polling.py')
        artifacts = CACHE / 'test-artifacts'
        artifacts.mkdir(exist_ok=True)
        run_smoke(executable, '--smoke', artifacts)
        czech = artifacts / 'cs'
        czech.mkdir(exist_ok=True)
        run_smoke(executable, '--smoke-cs', czech)
        navigation = artifacts / 'keyboard-nav'
        navigation.mkdir(exist_ok=True)
        run_smoke(executable, '--smoke-nav', navigation)
        print('Models/services, English/Czech UI and keyboard-navigation integration tests passed. Screenshots:', artifacts)
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
