"""Hardware-free protocol validation with the project's portable compiler."""
from pathlib import Path
import os
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache/guardian-tests'
OUT.mkdir(parents=True, exist_ok=True)
zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
compiler = [str(zig), 'c++'] if os.name == 'nt' and zig.exists() else [shutil.which('c++') or 'g++']
exe = OUT / ('guardian-tests.exe' if os.name == 'nt' else 'guardian-tests')
subprocess.run([*compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1',
                '-I'+str(ROOT/'src'), str(ROOT/'test/test_guardian_status.cpp'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=15)
transport = OUT / ('guardian-transport.exe' if os.name == 'nt' else 'guardian-transport')
subprocess.run([*compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1', '-DLILYGO_TDECK', '-DBLE_PIN_CODE',
                '-I'+str(ROOT/'test/stubs/guardian_ble'), '-I'+str(ROOT/'src'),
                str(ROOT/'test/test_guardian_transport.cpp'),
                str(ROOT/'src/helpers/esp32/GuardianBLEInterface.cpp'),
                str(ROOT/'src/ui-touch/services/GuardianLink.cpp'),
                str(ROOT/'src/ui-touch/services/GuardianRpcLink.cpp'), '-o', str(transport)], check=True)
subprocess.run([str(transport)], check=True, timeout=15)
rpc = OUT / ('guardian-rpc.exe' if os.name == 'nt' else 'guardian-rpc')
json_include = ROOT/'.sim-cache/arduinojson/src'
if not json_include.exists():
    json_include = ROOT/'.pio/libdeps/LilyGo_TDeck_companion_radio_touch/ArduinoJson/src'
subprocess.run([*compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1',
                '-I'+str(ROOT/'src'),
                '-I'+str(json_include),
                str(ROOT/'test/test_guardian_rpc.cpp'),
                str(ROOT/'src/ui-touch/services/GuardianDraft.cpp'), '-o', str(rpc)], check=True)
subprocess.run([str(rpc)], check=True, timeout=15)
