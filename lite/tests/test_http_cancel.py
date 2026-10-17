"""Actual WinHTTP finite-response semantics and cancellation against local test servers."""
import argparse
import http.server
import json
import pathlib
import subprocess
import tempfile
import threading
import time

p = argparse.ArgumentParser()
p.add_argument('--helper', required=True)
args = p.parse_args()
requests = []
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_POST(self): self.do_GET()
    def do_GET(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        requests.append((self.command, self.path, body, self.headers.get('X-Fixture')))
        if self.path == '/silent-headers':
            time.sleep(8); return
        self.send_response(302 if self.path == '/redirect' else 200)
        self.send_header('Mcp-Session-Id', 'fixture-session')
        self.send_header('Location', '/must-not-follow')
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        if self.path == '/silent-body':
            self.wfile.flush(); time.sleep(8); return
        try:
            payload = json.dumps({'method': self.command, 'body': body.decode(),
                                  'unicode': '日本語', 'padding': 'x' * 140000}).encode()
            for i in range(0, len(payload), 8191):
                self.wfile.write(payload[i:i+8191]); self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError): pass

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
server.daemon_threads = True
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory(prefix='tangos-http-') as folder:
        result = pathlib.Path(folder) / 'result.json'
        def call(path, method='GET', duration=300):
            url = path if '://' in path else f'http://127.0.0.1:{server.server_port}{path}'
            proc = subprocess.run([str(pathlib.Path(args.helper).resolve()), '--http',
                                   url, method, str(result), str(duration)],
                                  capture_output=True, timeout=8)
            assert proc.returncode == 0, (proc.stdout, proc.stderr)
            return json.loads(result.read_text(encoding='utf-8'))
        for method in ('GET', 'POST'):
            data = call('/complete', method)
            assert data['status'] == 200 and data['session'] == 'fixture-session', data
            payload = json.loads(data['body'])
            assert payload['method'] == method and payload['body'] == 'fixture body'
            assert payload['unicode'] == '日本語' and len(payload['padding']) == 140000
        for path in ('/silent-headers', '/silent-body'):
            for _ in range(3):
                data = call(path)
                assert data.get('error') and data['cancelMs'] < 1000, data
        data = call('/redirect')
        assert data['status'] == 302 and data['location'] == '/must-not-follow', data
        assert not any(row[1] == '/must-not-follow' for row in requests), requests
        before = len(requests)
        assert 'error' in call('http://user:secret@127.0.0.1/blocked')
        assert 'error' in call('/complete', 'OPTIONS')
        assert len(requests) == before
        assert all(row[3] == 'test-only' for row in requests)
finally:
    server.shutdown(); server.server_close()
print('PASS async HTTP: finite UTF8/multibuffer GET/POST, headers, silent headers/body cancellation, redirect and URL/method guards')