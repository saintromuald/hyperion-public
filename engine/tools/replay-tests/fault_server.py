"""Loopback-only ReplayKraken fault fixture. Mode is read from a temp file per request."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json

MODE_FILE = Path('/tmp/opencode/hyperion-replays-evidence/fault-mode')


def mode():
    try:
        return MODE_FILE.read_text().strip()
    except OSError:
        return '429'


class Handler(BaseHTTPRequestHandler):
    def reply(self, status, body=b'', retry_after=None):
        self.send_response(status)
        self.send_header('Content-Type', 'application/json' if body else 'text/plain')
        self.send_header('Content-Length', str(len(body)))
        if retry_after is not None:
            self.send_header('Retry-After', str(retry_after))
        self.send_header('Connection', 'close')
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self):
        current = mode()
        if current == '401':
            return self.reply(401)
        if self.path == '/api/users/@me':
            return self.reply(200, b'{"username":"alice"}')
        if self.path.startswith('/api/matches?'):
            if current == 'malformed':
                return self.reply(200, b'{not-json')
            if current == 'oversized':
                return self.reply(200, b'[' + b' ' * (9 * 1024 * 1024) + b']')
            return self.reply(200, b'{"matches":[],"total":0,"limit":50,"offset":0}')
        if self.path.startswith('/api/matches/') and self.path.endswith('/replay'):
            return self.reply(404)
        return self.reply(404)

    def do_POST(self):
        length = int(self.headers.get('Content-Length', '0'))
        self.rfile.read(length)
        current = mode()
        if current == '401':
            return self.reply(401)
        if current == '500':
            return self.reply(500, b'db error', retry_after=1)
        return self.reply(429, b'too many', retry_after=1)

    def log_message(self, *_):
        pass


if __name__ == '__main__':
    ThreadingHTTPServer(('127.0.0.1', 18789), Handler).serve_forever()
