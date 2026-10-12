#!/usr/bin/env python3
"""Compile real transport against pinned rcheevos headers; use only loopback HTTP."""
import argparse
import http.server
from pathlib import Path
import subprocess
import tempfile
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.respond()

    def do_POST(self):
        self.respond()

    def respond(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        status = 200
        if self.path == '/slow':
            time.sleep(0.3)
            body = b'slow'
        elif self.path == '/error':
            status = 503
        elif self.path == '/redirect':
            self.send_response(302)
            self.send_header('Location', '/echo')
            self.end_headers()
            return
        elif self.path == '/large':
            body = b'x' * (16 * 1024 * 1024 + 1)
        self.send_response(status)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rcheevos', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = root / 'soh/soh/Enhancements/retroachievements'
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory() as temp:
            for with_curl in (True, False):
                exe = Path(temp) / ('http' if with_curl else 'fallback')
                command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                           '-I' + str(args.rcheevos.resolve() / 'include'), '-I' + str(source),
                           str(source / 'RetroAchievementsHttp.cpp'),
                           str(root / 'tests/retroachievements/http_transport.cpp'), '-o', str(exe)]
                if with_curl:
                    command += ['-DSHIPWRIGHT_RA_HTTP_CURL=1', '-lcurl']
                subprocess.run(command, check=True)
                subprocess.run([str(exe), f'http://127.0.0.1:{server.server_port}'], check=True, timeout=15)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == '__main__':
    main()
