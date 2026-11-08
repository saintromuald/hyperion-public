"""Loopback HTTP fixture for native port_http transport tests (synthetic data only)."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import time


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/redirect/same':
            return self.redirect('/api/matches?limit=1&offset=0')
        if self.path == '/redirect/cross':
            return self.redirect('https://other.invalid/api/matches')
        if self.path == '/redirect/downgrade':
            return self.redirect('http://127.0.0.1:18790/api/matches')
        if self.path == '/api/users/@me':
            if self.headers.get('Authorization') == 'Bearer fixture-token':
                return self.reply(200, b'{"username":"alice"}')
            return self.reply(401)
        if self.path.startswith('/api/matches?'):
            return self.reply(200, b'{"matches":[],"total":0,"limit":50,"offset":0}')
        if self.path == '/api/matches/uuid/replay':
            return self.reply(200, b'abc', content_type='application/octet-stream')
        if self.path == '/malformed':
            return self.reply(200, b'{not-json')
        if self.path == '/large':
            return self.reply(200, b'x' * 64)
        if self.path == '/slow':
            time.sleep(0.25)
            return self.reply(200, b'ok')
        return self.reply(404)

    def redirect(self, location):
        self.send_response(302)
        self.send_header('Location', location)
        self.send_header('Content-Length', '0')
        self.send_header('Connection', 'close')
        self.end_headers()

    def reply(self, status, body=b'', content_type='application/json'):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Connection', 'close')
        self.end_headers()
        if body:
            self.wfile.write(body)

    def log_message(self, *_):
        pass


if __name__ == '__main__':
    ThreadingHTTPServer(('127.0.0.1', 18790), Handler).serve_forever()
