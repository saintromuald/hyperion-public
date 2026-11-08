"""Disposable loopback proxy: forward everything, but drop the first POST response.
Used only for ReplayKraken timeout-after-possible-success tests with synthetic data.
"""
import http.client
import json
import socketserver
import sys


class Handler(socketserver.BaseRequestHandler):
    posts = 0

    def handle(self):
        data = b""
        while b"\r\n\r\n" not in data:
            part = self.request.recv(65536)
            if not part:
                return
            data += part
            if len(data) > 65536:
                return
        head, body = data.split(b"\r\n\r\n", 1)
        lines = head.split(b"\r\n")
        method, path, _ = lines[0].decode("latin1").split(" ", 2)
        headers = {}
        for line in lines[1:]:
            name, value = line.decode("latin1").split(":", 1)
            headers[name.strip().lower()] = value.strip()
        length = int(headers.get("content-length", "0"))
        while len(body) < length:
            part = self.request.recv(min(65536, length - len(body)))
            if not part:
                return
            body += part
        if len(body) > length:
            return
        upstream = http.client.HTTPConnection("127.0.0.1", 18787, timeout=10)
        upstream.request(method, path, body=body,
                         headers={"Content-Type": headers.get("content-type", "application/json"),
                                  "Authorization": headers.get("authorization", ""),
                                  "Content-Length": str(length), "Connection": "close"})
        response = upstream.getresponse()
        payload = response.read()
        upstream.close()
        if method == "POST" and path == "/api/matches":
            type(self).posts += 1
            if type(self).posts == 1:
                with open("/tmp/opencode/hyperion-replays-evidence/drop-first-proxy.json", "w") as out:
                    json.dump({"acceptedUpstream": True, "droppedResponse": True, "posts": type(self).posts}, out)
                self.close_connection = True
                return
        self.request.sendall(f"HTTP/1.1 {response.status} {response.reason}\r\n".encode())
        self.request.sendall(f"Content-Length: {len(payload)}\r\nConnection: close\r\n\r\n".encode())
        self.request.sendall(payload)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True


if __name__ == "__main__":
    with Server(("127.0.0.1", 18788), Handler) as server:
        print("drop-first proxy on 18788", flush=True)
        server.serve_forever()
