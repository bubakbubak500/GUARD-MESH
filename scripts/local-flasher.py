#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Serve the bundled flasher on loopback. Python 3.9+, standard library only."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
from urllib.parse import unquote, urlsplit
import webbrowser
import json
import secrets
from flasher_build import BuildManager

ROOT = Path(__file__).resolve().parents[1] / 'deploy' / 'flasher'
BUILD = BuildManager()
TOKEN = secrets.token_urlsafe(32)


class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                      '.js': 'text/javascript', '.mjs': 'text/javascript'}

    def _local_request(self):
        port = self.server.server_port
        hosts = {f'localhost:{port}', f'127.0.0.1:{port}'}
        origin = self.headers.get('Origin')
        return (self.headers.get('Host') in hosts and
                (origin is None or origin in {f'http://{h}' for h in hosts}))

    def _json(self, value, status=200):
        payload = json.dumps(value, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_POST(self):
        if not self._local_request() or self.headers.get('X-Flasher-Token') != TOKEN:
            self.send_error(403)
            return
        if self.path != '/api/build':
            self.send_error(404)
            return
        if self.headers.get('Transfer-Encoding') or self.headers.get('Content-Length', '0') != '0':
            self.send_error(400)
            return
        started = BUILD.start()
        self._json(BUILD.snapshot(), 202 if started else 409)

    def do_GET(self):
        if not self._local_request():
            self.send_error(403)
            return
        if self.path == '/api/build':
            self._json({**BUILD.snapshot(), 'token': TOKEN})
            return
        if self.path.startswith('/api/firmware/'):
            name = self.path.removeprefix('/api/firmware/')
            artifact = BUILD.artifact_for(name.removesuffix('.bin')) if name.endswith('.bin') else None
            if artifact is None:
                self.send_error(404)
                return
            with artifact.open('rb') as stream:
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(artifact.stat().st_size))
                self.end_headers()
                self.copyfile(stream, self.wfile)
            return
        super().do_GET()

    def send_head(self):
        if not self._local_request():
            self.send_error(403)
            return None
        path = unquote(urlsplit(self.path).path)
        requested = (ROOT / (path.lstrip('/') or 'index.html')).resolve()
        if ROOT.resolve() not in requested.parents or not requested.is_file():
            self.send_error(404)
            return None
        return super().send_head()

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy',
                         "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
                         "connect-src 'self' blob:; img-src 'self' data: blob:; "
                         "object-src 'none'; base-uri 'none'")
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8766)
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error('--port must be between 0 and 65535')
    if not (ROOT / 'vendor/esp-web-tools/install-button.js').is_file():
        parser.error('Bundled ESP Web Tools files are missing; restore deploy/flasher/vendor.')
    try:
        server = ThreadingHTTPServer(('127.0.0.1', args.port), partial(Handler, directory=str(ROOT)))
    except OSError as error:
        parser.exit(1, f'Cannot start local flasher: {error}. Try --port 8767.\n')
    url = f'http://localhost:{server.server_port}/'
    print(f'GUARD-MESH local flasher: {url}\nCtrl+C stops the server.', flush=True)
    if not args.no_browser:
        threading.Timer(0.3, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
