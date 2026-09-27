#!/usr/bin/env python3
"""Build and run the hardware-independent UI regression suite (no radio/LVGL)."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache' / 'model-tests'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    if os.name == 'nt' and zig.exists():
        compiler = [str(zig), 'c++']
    else:
        compiler = [os.environ.get('CXX') or shutil.which('c++') or 'g++']
    sources = [ROOT/'test/test_ui_models.cpp', ROOT/'test/test_app_store_jobs.cpp', ROOT/'test/test_firmware_updates.cpp', ROOT/'test/test_sightline.cpp', ROOT/'test/test_diagnostics.cpp']
    sources += [ROOT/'test/test_wifi_scan.cpp', ROOT/'src/ui-touch/services/WifiScanJob.cpp']
    sources += [ROOT/'test/test_sd_restore.cpp', ROOT/'src/ui-touch/services/SdRestoreJob.cpp']
    sources += [ROOT/'test/test_tile_requests.cpp', ROOT/'src/ui-touch/services/TileRequestLedger.cpp']
    sources += [ROOT/'test/test_clock_time.cpp', ROOT/'test/test_gps_status.cpp']
    sources += [ROOT/'test/test_battery_history.cpp', ROOT/'test/test_notification_policy.cpp', ROOT/'test/test_key_bindings.cpp', ROOT/'test/test_color_choice.cpp']
    sources += sorted((ROOT/'src/ui-touch/models').glob('*.cpp'))
    sources += [ROOT/'src/ui-touch/application/UiApplication.cpp',
                ROOT/'src/ui-touch/platform/StorageAccess.cpp',
                ROOT/'src/ui-touch/services/HistoryCodec.cpp',
                ROOT/'src/ui-touch/services/RadioService.cpp',
                ROOT/'src/ui-touch/services/ConfigurationService.cpp',
                ROOT/'src/ui-touch/services/AppStoreJobs.cpp',
                ROOT/'src/ui-touch/services/FirmwareUpdateJobs.cpp', ROOT/'src/ui-touch/services/SightlineJob.cpp', ROOT/'src/ui-touch/services/StorageUsage.cpp']
    executable = OUT / ('ui-models.exe' if os.name == 'nt' else 'ui-models')
    threading_flags = [] if os.name == 'nt' else ['-pthread']
    subprocess.run([*compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    *threading_flags, '-I'+str(ROOT/'src'), *map(str, sources), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=30)

if __name__ == '__main__':
    main()
