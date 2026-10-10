#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the production ESP32 GitHub OTA transport against a fake runtime."""
from pathlib import Path
import os
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.sim-cache/firmware-transport-tests'
def main():
    OUT.mkdir(parents=True, exist_ok=True)
    # Thin generated include shims keep the fake API in one auditable header.
    for name in ('Arduino.h', 'WiFi.h', 'Update.h', 'esp_http_client.h', 'esp_heap_caps.h', 'esp_ota_ops.h', 'esp_task_wdt.h', 'mbedtls/sha256.h', 'freertos/FreeRTOS.h', 'freertos/task.h'):
        path = OUT / 'include' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "FakeRuntime.h"\n')
    zig = ROOT / '.sim-cache/toolchain/ziglang/zig.exe'
    compiler = [str(zig), 'c++'] if os.name == 'nt' and zig.exists() else [os.environ.get('CXX') or shutil.which('c++') or 'g++']
    executable = OUT / ('transport.exe' if os.name == 'nt' else 'transport')
    sources = ['test/firmware_transport/main.cpp', 'src/ui-touch/platform/esp32/FirmwareUpdateTransport.cpp',
               'src/ui-touch/services/FirmwareUpdateJobs.cpp', 'src/ui-touch/models/FirmwareRelease.cpp', 'src/ui-touch/platform/StorageAccess.cpp']
    subprocess.run([*compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-DESP32', '-DMULTI_TRANSPORT_COMPANION', '-DLILYGO_TDECK',
                    '-I'+str(OUT/'include'), '-I'+str(ROOT/'test/firmware_transport/include'), '-I'+str(ROOT/'src'), '-I'+str(ROOT/'.sim-cache/arduinojson/src'),
                    *[str(ROOT/s) for s in sources], '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=30)
if __name__ == '__main__': main()
