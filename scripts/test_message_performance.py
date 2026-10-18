#!/usr/bin/env python3
"""Message index regressions and deterministic work counters; optional Git baseline."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache/message-performance-tests'


def test_echo(compiler):
    # Compile the actual inline methods without MyMesh's radio/platform base classes.
    source = (ROOT / 'src/MyMesh.h').read_text(encoding='utf-8')
    start = source.index('  static const int UI_ECHO_SLOTS =')
    end = source.index('  // ---- Live signal strength', start)
    fields = source[start:end].replace('  RegionRegistry _region_reg;\n', '')
    methods = []
    for signature in ('uint8_t uiRepeatsForFp(', 'void uiTrackSentFp(', 'bool uiCountEcho(', 'bool takeEchoDirty('):
        start = source.index(signature)
        end = source.index('{', start) + 1
        depth = 1
        while depth:
            if source[end] == '{':
                depth += 1
            elif source[end] == '}':
                depth -= 1
            end += 1
        methods.append(source[start:end])
    header = '#include <cstdint>\n#include <cstring>\nclass MessageEcho {\npublic:\n' + fields + '\n'.join(methods) + '\n};\n'
    (OUT / 'MessageEchoMethods.h').write_text(header, encoding='utf-8')
    executable = OUT / ('echo.exe' if os.name == 'nt' else 'echo')
    subprocess.run([*compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1',
                    '-I' + str(OUT), str(ROOT / 'test/test_message_echo.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref', help='also compile the original MessageStore at this Git ref')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    compiler = [str(zig), 'c++'] if os.name == 'nt' and zig.exists() else [os.environ.get('CXX') or shutil.which('c++') or 'g++']
    model = ROOT / 'src/ui-touch/models/MessageStore.cpp'
    builds = [('current', model)]
    if args.baseline_ref:
        source = subprocess.check_output(['git', 'show', args.baseline_ref + ':src/ui-touch/models/MessageStore.cpp'], cwd=ROOT).decode('utf-8')
        source = source.replace('namespace ui {', 'namespace ui {\nMessageStoreWork messageStoreWork;', 1)
        begin = source.index('void MessageStore::rebuildThreadHistoryFlags() const')
        end = source.index('bool MessageStore::threadHasMention', begin)
        segment = source[begin:end].replace('  for (int i = 0; i < MAX_UI_THREADS; ++i)', '  ++messageStoreWork.historyRebuilds;\n  for (int i = 0; i < MAX_UI_THREADS; ++i)', 1)
        segment = segment.replace('  for (int i = 0; i < _ui_msg_count; ++i) {', '  for (int i = 0; i < _ui_msg_count; ++i) {\n    ++messageStoreWork.historyRecords;', 1)
        segment = segment.replace('    for (int t = 0; t < MAX_UI_THREADS; ++t) {', '    for (int t = 0; t < MAX_UI_THREADS; ++t) {\n      ++messageStoreWork.historyComparisons;', 1)
        source = source[:begin] + segment + source[end:]
        begin = source.index('bool MessageStore::lastThreadMessage(')
        end = source.index('int MessageStore::newestUnread(', begin)
        segment = source[begin:end].replace('  for (int i = 0; i < _ui_msg_count; ++i) {', '  for (int i = 0; i < _ui_msg_count; ++i) {\n    ++messageStoreWork.latestRecords;', 1)
        source = source[:begin] + segment + source[end:]
        baseline = OUT / 'baseline.cpp'
        baseline.write_text(source, encoding='utf-8')
        builds.insert(0, ('baseline', baseline))
    results = {}
    for name, source in builds:
        executable = OUT / (name + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([*compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                        '-DGUARD_MESSAGE_STORE_METRICS=1', '-I' + str(ROOT / 'src'),
                        '-I' + str(model.parent), str(ROOT / 'test/test_message_performance.cpp'),
                        str(source), '-o', str(executable)], check=True)
        print(name + ':', flush=True)
        result = subprocess.run([str(executable), *(['--baseline'] if name == 'baseline' else [])],
                                check=True, timeout=30, capture_output=True, text=True)
        print(result.stdout, end='', flush=True)
        results[name] = [dict(field.split('=', 1) for field in line.split()[1:])
                         for line in result.stdout.splitlines() if line.startswith('PERF ')]
    if 'baseline' in results:
        assert len(results['baseline']) == len(results['current'])
        for old, new in zip(results['baseline'], results['current']):
            for field in ('scenario', 'capacity', 'iterations', 'checksum'):
                assert old[field] == new[field], (field, old, new)
    (OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    test_echo(compiler)


if __name__ == '__main__':
    main()
