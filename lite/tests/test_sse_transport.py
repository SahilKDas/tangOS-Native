"""Exercise the actual Windows WinHTTP SSE transport against disposable loopback endpoints."""
import argparse
import http.server
import json
import pathlib
import subprocess
import tempfile
import threading
import time


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--helper', required=True)
    args = p.parse_args()
    helper = pathlib.Path(args.helper).resolve()
    requests = []
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def do_GET(self):
            requests.append((self.path, self.headers.get('Authorization'), self.headers.get('Accept')))
            if self.path == '/redirect':
                self.send_response(302)
                self.send_header('Location', '/unexpected')
                self.end_headers()
                return
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.end_headers()
            try:
                if self.path == '/silent':
                    time.sleep(10)
                    return
                index = sum(row[0] == '/events' for row in requests)
                frame = (': heartbeat\r\n\r\ndata: broken\n\nevent: counts\r\ndata: ' +
                         json.dumps({'totals': {'日本語': index}, 'daily': {}}, ensure_ascii=False) + '\r\n\r\n').encode()
                for byte in frame:
                    self.wfile.write(bytes([byte]))
                    self.wfile.flush()
                # EOF on first connection; second stream stays open so cancellation interrupts read.
                if index > 1: time.sleep(10)
            except (BrokenPipeError, ConnectionResetError): pass
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='tangos-sse-') as folder:
            root = pathlib.Path(folder)
            repo, data = root / 'repository', root / 'data'
            repo.mkdir(); data.mkdir()
            (repo / 'tangos.json').write_text(json.dumps({'tangosVersion': '1', 'project': {'name': 'fixture', 'title': 'Fixture'}, 'tools': []}))
            def watch(endpoint, duration, enabled=True):
                profile = {'enabled': enabled, 'url': f'http://127.0.0.1:{server.server_port}/{endpoint}', 'keyEnv': 'FIXTURE_KEY'}
                (data / 'connections.json').write_text(json.dumps({'atlas.events': profile}))
                result_path = root / 'result.json'
                result = subprocess.run([str(helper), '--watch', str(repo), str(data), str(result_path), str(duration)], capture_output=True, timeout=duration/1000+10)
                assert result.returncode == 0, (result.stdout, result.stderr)
                return json.loads(result_path.read_text())
            result = watch('events', 4200)
            events = [e for e in result['events'] if e['event'] == 'counts']
            assert len(events) == 2 and events[0]['data']['totals']['日本語'] == 1 and events[1]['data']['totals']['日本語'] == 2, result
            assert result['cancelMs'] < 1000, result
            assert all(key == 'Bearer fixture-only' and accept == 'text/event-stream' for path, key, accept in requests), requests
            caches = list((data / 'atlas-state').glob('*.json'))
            assert any(json.loads(path.read_text()).get('extras', {}).get('atlas.counts', {}).get('totals', {}).get('日本語') == 2 for path in caches)
            assert all('fixture-only' not in path.read_text() for path in caches), 'credential leaked to cache'
            before = len(requests)
            disabled = watch('events', 150, False)
            assert not disabled['events'] and len(requests) == before
            for _ in range(4):
                silent = watch('silent', 300)
                assert not silent['events'] and silent['cancelMs'] < 1000, silent
            redirect = watch('redirect', 400)
            assert any(e['data'].get('status') == 302 for e in redirect['events']), redirect
            assert not any(path == '/unexpected' for path, *_ in requests), requests
    finally:
        server.shutdown(); server.server_close()
    print('PASS actual WinHTTP SSE: UTF8/CRLF fragmentation, reconnect, cache persistence, opt-in, cancellation, redirect isolation')

if __name__ == '__main__': main()