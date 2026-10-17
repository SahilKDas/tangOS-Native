"""Disposable loopback SSE source for the native GUI workflow; never shipped."""
import argparse
import http.server
import json
import pathlib
import time

parser = argparse.ArgumentParser()
parser.add_argument('--directory', required=True)
root = pathlib.Path(parser.parse_args().directory).resolve()
root.mkdir(parents=True, exist_ok=True)
(root / 'phase.txt').write_text('1', encoding='utf-8')

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        if self.path == '/slow-atlas':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', '100000')
            self.end_headers()
            self.wfile.flush()
            (root / 'slow-requested.txt').write_text('active', encoding='utf-8')
            time.sleep(20)
            return
        if self.path != '/live':
            self.send_error(404); return
        fixture = json.loads((root / 'functions.json').read_text(encoding='utf-8'))
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream; charset=utf-8')
        self.end_headers()
        def send(event, data):
            self.wfile.write(('event: '+event+'\ndata: '+json.dumps(data)+'\n\n').encode('utf-8'))
            self.wfile.flush()
        try:
            send('counts', {'totals': {'Live Fixture': 7}, 'daily': {'Live Fixture': 2}})
            send('claims', {'claims': [{'name': fixture['secondName'], 'status': 'active'}]})
            send('progress', {'ready': True, 'stats': fixture['stats'], 'matched': [fixture['first']]})
            changed = False
            while True:
                phase = (root / 'phase.txt').read_text(encoding='utf-8').strip()
                if phase == '2' and not changed:
                    stats = dict(fixture['stats'])
                    stats['matchedFunctions'] = 1
                    stats['matchedBytes'] = fixture['secondSize']
                    send('progress', {'ready': True, 'stats': stats, 'added': [fixture['second']],
                                      'removed': [fixture['first']],
                                      'changed': [{'id': fixture['second'], 'author': 'Live Contributor'}]})
                    changed = True
                self.wfile.write(b': heartbeat\n\n'); self.wfile.flush()
                time.sleep(.05)
        except (BrokenPipeError, ConnectionResetError, OSError): pass

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
server.daemon_threads = True
endpoint = root / 'endpoint.txt'
temporary = root / 'endpoint.new'
temporary.write_text(f'http://127.0.0.1:{server.server_port}/live', encoding='utf-8')
temporary.replace(endpoint)
server.serve_forever()