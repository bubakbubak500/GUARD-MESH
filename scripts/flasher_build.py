# SPDX-License-Identifier: GPL-3.0-or-later
"""Single local T-Deck build job. No commands or paths accepted from HTTP clients."""
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time
import uuid
from collections import deque

PROJECT = Path(__file__).resolve().parents[1]
CACHE = PROJECT / '.flasher-cache'
ENVIRONMENT = 'LilyGo_TDeck_companion_radio_touch'


class BuildManager:
    def __init__(self):
        self.lock = threading.Lock()
        self.logs = deque(maxlen=160)
        self.state = {'status': 'idle', 'environment': ENVIRONMENT}
        self.artifact = None
        self.versions = {}
        records = list((CACHE / 'builds').glob('*.json')) + [CACHE / 'latest.json']
        for record in records:
            try:
                saved = json.loads(record.read_text(encoding='utf-8'))
                if saved['status'] != 'ready' or not re.fullmatch('[0-9a-f]{32}', saved['id']):
                    continue
                artifact = CACHE / (saved['id'] + '.bin')
                if saved.get('environment') != ENVIRONMENT or hashlib.sha256(artifact.read_bytes()).hexdigest() != saved['sha256']:
                    continue
                self.versions[saved['id']] = saved
                # Migrate the first flasher's single saved build into the archive.
                (CACHE / 'builds').mkdir(exist_ok=True)
                (CACHE / 'builds' / (saved['id'] + '.json')).write_text(json.dumps(saved), encoding='utf-8')
            except (OSError, ValueError, KeyError, TypeError):
                continue
        try:
            saved = json.loads((CACHE / 'latest.json').read_text(encoding='utf-8'))
            if saved['status'] == 'ready' and re.fullmatch('[0-9a-f]{32}', saved['id']):
                artifact = CACHE / (saved['id'] + '.bin')
                if hashlib.sha256(artifact.read_bytes()).hexdigest() == saved['sha256']:
                    self.state = saved
                    self.artifact = artifact
        except (OSError, ValueError, KeyError, TypeError):
            pass

    def snapshot(self):
        with self.lock:
            return {**self.state, 'log': '\n'.join(self.logs),
                    'versions': sorted(self.versions.values(), key=lambda v: v.get('finished', 0), reverse=True)}

    def start(self):
        with self.lock:
            if self.state['status'] == 'building':
                return False
            (CACHE / 'latest.json').unlink(missing_ok=True)
            self.logs.clear()
            self.artifact = None
            self.state = {'status': 'building', 'environment': ENVIRONMENT,
                          'id': uuid.uuid4().hex, 'started': time.time()}
        threading.Thread(target=self._build, daemon=True).start()
        return True

    def artifact_for(self, build_id):
        with self.lock:
            saved = self.versions.get(build_id)
        if saved:
            artifact = CACHE / (build_id + '.bin')
            try:
                if hashlib.sha256(artifact.read_bytes()).hexdigest() == saved['sha256']:
                    return artifact
            except OSError:
                pass
        return None

    def _log(self, line):
        with self.lock:
            self.logs.append(line.rstrip())

    def _run(self, args, env):
        flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
        with subprocess.Popen(args, cwd=PROJECT, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                              errors='replace', creationflags=flags) as process:
            for line in process.stdout:
                self._log(line)
            if process.wait():
                raise RuntimeError('Sestavení selhalo. Podrobnosti jsou v protokolu níže.')

    def _build(self):
        try:
            CACHE.mkdir(exist_ok=True)
            env = os.environ.copy()
            env.update(PLATFORMIO_CORE_DIR=str(CACHE / 'platformio'),
                       PYTHONUNBUFFERED='1', PYTHONIOENCODING='utf-8', PYTHONUTF8='1')
            # Local installation must not silently bake host Wi-Fi credentials in.
            for key in ('WIFI_SSID', 'WIFI_PWD', 'MERGED_BIN_PATH'):
                env.pop(key, None)
            python = CACHE / 'venv' / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
            if not python.is_file():
                self._log('Připravuji lokální Python prostředí…')
                self._run([sys.executable, '-m', 'venv', str(CACHE / 'venv')], env)
            marker = CACHE / 'platformio-6.1.18.ready'
            if not marker.is_file():
                self._log('Instaluji PlatformIO 6.1.18 (první spuštění potřebuje internet)…')
                self._run([str(python), '-m', 'pip', 'install', '--disable-pip-version-check',
                           'platformio==6.1.18'], env)
                marker.write_text('6.1.18', encoding='utf-8')
            if not shutil.which('git', path=env.get('PATH')):
                raise RuntimeError('Chybí Git. Nainstaluj Git a znovu spusť instalátor.')
            output = PROJECT / '.pio' / 'build' / ENVIRONMENT
            merged = output / 'firmware-merged.bin'
            # A failed merge must never expose a stale image from an older build.
            merged.unlink(missing_ok=True)
            self._log('Sestavuji aktuální lokální zdroje pro LilyGo T-Deck…')
            revision = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=PROJECT,
                                      capture_output=True, text=True)
            source = revision.stdout.strip() if revision.returncode == 0 else 'local'
            self._run([str(python), '-m', 'platformio', 'run', '-e', ENVIRONMENT,
                       '-t', 'mergebin'], env)
            app = output / 'firmware.bin'
            if not app.is_file() or not 24 <= app.stat().st_size <= 0x3E0000:
                raise RuntimeError('Firmware chybí nebo překračuje OTA oddíl T-Decku (3,875 MiB).')
            data = merged.read_bytes()
            if not 0x10018 <= len(data) <= 0x1000000 or data[0] != 0xE9 or data[0x10000] != 0xE9:
                raise RuntimeError('Sestavení nevytvořilo platný kompletní instalační obraz.')
            app_data = app.read_bytes()
            if data[0x10000:0x10000 + len(app_data)] != app_data:
                raise RuntimeError('Sloučený obraz neobsahuje právě sestavenou aplikaci.')
            artifact = CACHE / (self.state['id'] + '.bin')
            artifact.write_bytes(data)
            with self.lock:
                self.artifact = artifact
                self.state.update(status='ready', finished=time.time(), bytes=len(data),
                                  source=source,
                                  app_bytes=app.stat().st_size, sha256=hashlib.sha256(data).hexdigest(),
                                  firmware_url='/api/firmware/' + self.state['id'] + '.bin')
                metadata = CACHE / 'latest.tmp'
                metadata.write_text(json.dumps(self.state), encoding='utf-8')
                metadata.replace(CACHE / 'latest.json')
                (CACHE / 'builds').mkdir(exist_ok=True)
                record = CACHE / 'builds' / (self.state['id'] + '.json')
                record.write_text(json.dumps(self.state), encoding='utf-8')
                self.versions[self.state['id']] = dict(self.state)
            self._log('Hotovo. Obraz je připravený k instalaci přes USB.')
        except Exception as error:
            self._log(str(error))
            with self.lock:
                self.artifact = None
                self.state.update(status='failed', error=str(error))
