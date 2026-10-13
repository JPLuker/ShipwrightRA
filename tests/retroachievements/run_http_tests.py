#!/usr/bin/env python3
"""Compile real transport against pinned rcheevos headers; use only loopback HTTP."""
import argparse
import http.server
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from urllib.parse import parse_qs


class Handler(http.server.BaseHTTPRequestHandler):
    awards = 0
    test_set = {
        'Success': True, 'GameId': 1234, 'Title': 'Synthetic N64 test', 'ConsoleId': 2,
        'ImageIconUrl': 'http://localhost/test.png', 'RichPresenceGameId': 1234, 'RichPresencePatch': '',
        'Sets': [{'AchievementSetId': 1111, 'GameId': 1234, 'Title': None, 'Type': 'core',
                  'ImageIconUrl': 'http://localhost/test.png', 'Leaderboards': [],
                  'Achievements': [{'ID': aid, 'Title': title, 'Description': 'Synthetic test condition',
                                    'Flags': 3, 'Points': 5, 'MemAddr': mem, 'Author': 'Test',
                                    'BadgeName': '00000', 'Created': 1367266583, 'Modified': 1376929305}
                                   for aid, title, mem in ((5501, 'Mapped', '0xH0000=1'),
                                                           (5502, 'Unsupported', '0xH0100=1'))]}]}

    def log_message(self, *args):
        pass

    def do_GET(self):
        self.respond()

    def do_POST(self):
        self.respond()

    def respond(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        status = 200
        if self.path == '/dorequest.php':
            fields = parse_qs(body.decode())
            api = fields['r'][0]
            if api == 'login2':
                user = fields['u'][0]
                if user in ('stale', 'shutdown'):
                    time.sleep(0.3)
                result = {'Success': False, 'Error': 'Invalid test credentials'} if user == 'invalid' else {
                    'Success': True, 'User': user, 'Token': 'synthetic',
                    'Score': 0, 'SoftcoreScore': 0, 'Messages': 0}
            elif api == 'achievementsets':
                result = self.test_set
            elif api == 'startsession':
                assert type(self).awards == 0, 'Test mode submitted an unlock'
                result = {'Success': True, 'Unlocks': [], 'HardcoreUnlocks': []}
            elif api == 'awardachievement':
                type(self).awards += 1
                result = {'Success': True, 'AchievementID': 5501, 'Score': 0, 'SoftcoreScore': 5,
                          'AchievementsRemaining': 1}
            else:
                result = {'Success': True}
            body = json.dumps(result).encode()
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
    parser.add_argument('--spdlog', type=Path, help='Also build manager lifecycle tests using these headers')
    parser.add_argument('--fmt-external', action='store_true', help='Link system fmt for distro spdlog headers')
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
            if args.spdlog:
                objects = []
                rcheevos = args.rcheevos.resolve()
                sources = list((rcheevos / 'src').glob('*.c'))
                for folder in ('rcheevos', 'rapi', 'rhash'):
                    sources += list((rcheevos / 'src' / folder).glob('*.c'))
                for index, path in enumerate(sources):
                    if path.name in ('rc_libretro.c', 'rc_client_external.c'):
                        continue
                    obj = Path(temp) / f'rc{index}.o'
                    subprocess.run(['cc', '-DRC_DISABLE_LUA', '-DRC_CLIENT_SUPPORTS_HASH',
                                    '-I' + str(rcheevos / 'include'), '-c', str(path), '-o', str(obj)], check=True)
                    objects.append(str(obj))
                exe = Path(temp) / 'manager'
                subprocess.run(['g++', '-std=c++17', '-pthread', '-DSHIPWRIGHT_RA_HTTP_CURL=1',
                                '-DRC_CLIENT_SUPPORTS_HASH', '-I' + str(rcheevos / 'include'),
                                '-I' + str(args.spdlog.resolve() / 'include'), '-I' + str(source),
                                str(source / 'RetroAchievementsManager.cpp'),
                                str(source / 'RetroAchievementsHttp.cpp'),
                                str(root / 'tests/retroachievements/manager_lifecycle.cpp'),
                                *objects, *(['-DSPDLOG_FMT_EXTERNAL', '-lfmt'] if args.fmt_external else []),
                                '-lcurl', '-o', str(exe)], check=True)
                rom = Path(temp) / 'synthetic.z64'
                rom.write_bytes(bytes.fromhex('80371240') + bytes(65532))
                subprocess.run([str(exe), f'http://127.0.0.1:{server.server_port}', str(rom)], check=True, timeout=15)
                assert Handler.awards == 1, f'Expected only the explicit softcore test submission, got {Handler.awards}'

    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == '__main__':
    main()
