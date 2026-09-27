# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with python scripts/test_local_flasher.py; no toolchain or board needed."""
import importlib.util
import json
import tempfile
import threading
import unittest
from functools import partial
from http.client import HTTPConnection
from pathlib import Path
from unittest.mock import patch
import flasher_build as build

spec = importlib.util.spec_from_file_location('local_flasher', Path(__file__).with_name('local-flasher.py'))
server_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server_module)


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.cache = self.root / '.flasher-cache'
        self.cache.mkdir()
        self.patches = [patch.object(build, 'PROJECT', self.root), patch.object(build, 'CACHE', self.cache)]
        for p in self.patches:
            p.start()
        self.manager = build.BuildManager()
        self.manager.state.update(status='building', id='a' * 32)
        (self.cache / 'platformio-6.1.18.ready').write_text('ready')

    def tearDown(self):
        for p in self.patches:
            p.stop()
        self.temp.cleanup()

    def test_failed_build_never_exposes_stale_binary(self):
        output = self.root / '.pio' / 'build' / build.ENVIRONMENT
        output.mkdir(parents=True, exist_ok=True)
        merged = output / 'firmware-merged.bin'
        merged.write_bytes(b'old firmware')
        def run(args, env):
            if 'platformio' in args:
                raise RuntimeError('Compiler failed')
        with patch.object(self.manager, '_run', side_effect=run), patch('shutil.which', return_value='git'):
            self.manager._build()
        self.assertEqual(self.manager.snapshot()['status'], 'failed')
        self.assertFalse(merged.exists())
        self.assertIsNone(self.manager.artifact_for('a' * 32))

    def test_success_survives_restart_and_detects_modified_artifact(self):
        def run(args, env):
            if 'platformio' in args:
                output = self.root / '.pio' / 'build' / build.ENVIRONMENT
                output.mkdir(parents=True, exist_ok=True)
                (output / 'firmware.bin').write_bytes(b'\xe9' + bytes(255))
                image = bytearray(0x10100)
                image[0] = image[0x10000] = 0xe9
                (output / 'firmware-merged.bin').write_bytes(image)
        with patch.object(self.manager, '_run', side_effect=run), patch('shutil.which', return_value='git'):
            self.manager._build()
        self.assertEqual(self.manager.snapshot()['status'], 'ready')
        restored = build.BuildManager()
        self.assertIsNotNone(restored.artifact_for('a' * 32))
        self.manager.state['id'] = 'b' * 32
        with patch.object(self.manager, '_run', side_effect=run), patch('shutil.which', return_value='git'):
            self.manager._build()
        archived = build.BuildManager()
        self.assertEqual(len(archived.snapshot()['versions']), 2)
        self.assertIsNotNone(archived.artifact_for('a' * 32))
        self.assertIsNotNone(archived.artifact_for('b' * 32))
        self.assertIsNone(archived.artifact_for('../latest'))
        # Failed new jobs retain explicitly selectable historical versions.
        with patch.object(archived, '_run', side_effect=RuntimeError('failed')):
            archived._build()
        self.assertIsNotNone(archived.artifact_for('a' * 32))
        self.assertIsNotNone(archived.artifact_for('b' * 32))
        archived.artifact_for('b' * 32).write_bytes(b'corrupt')
        restored.artifact.write_bytes(b'corrupt')
        self.assertEqual(build.BuildManager().snapshot()['status'], 'idle')

    def test_duplicate_start_is_rejected(self):
        self.assertFalse(self.manager.start())


class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = server_module.ThreadingHTTPServer(('127.0.0.1', 0), partial(server_module.Handler, directory=str(server_module.ROOT)))
        cls.thread = threading.Thread(target=cls.server.serve_forever)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def request(self, method, path, headers=None):
        conn = HTTPConnection('127.0.0.1', self.server.server_port)
        conn.request(method, path, headers=headers or {})
        response = conn.getresponse()
        status, data = response.status, response.read()
        conn.close()
        return status, data

    def test_cross_origin_and_bad_host_cannot_access_api(self):
        self.assertEqual(self.request('GET', '/api/build', {'Host': 'attacker.invalid'})[0], 403)
        self.assertEqual(self.request('POST', '/api/build', {'Origin': 'https://attacker.invalid', 'X-Flasher-Token': server_module.TOKEN})[0], 403)
        self.assertEqual(self.request('POST', '/api/build')[0], 403)

    def test_status_static_and_unknown_artifact(self):
        code, data = self.request('GET', '/api/build')
        self.assertEqual(code, 200)
        self.assertEqual(json.loads(data)['token'], server_module.TOKEN)
        self.assertEqual(self.request('GET', '/')[0], 200)
        self.assertEqual(self.request('GET', '/api/firmware/unknown.bin')[0], 404)
        self.assertEqual(self.request('GET', '/../platformio.ini')[0], 404)

    def test_authorized_build_starts_only_fixed_job(self):
        with patch.object(server_module.BUILD, 'start', return_value=True) as start:
            code, _ = self.request('POST', '/api/build', {'X-Flasher-Token': server_module.TOKEN})
        self.assertEqual(code, 202)
        start.assert_called_once_with()


if __name__ == '__main__':
    unittest.main()
