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

ROOT = Path(__file__).resolve().parents[1] / 'deploy' / 'flasher'


class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                      '.js': 'text/javascript', '.mjs': 'text/javascript'}

    def send_head(self):
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
