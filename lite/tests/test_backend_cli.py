"""Packaged CLI/native WinHTTP tests. Only a loopback fixture service is contacted."""
import argparse
import http.server
import json
import os
import pathlib
import subprocess
import tempfile
import threading


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    opts = parser.parse_args()
    requests = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            requests.append(("GET", self.path))
            if self.path == "/redirect":
                self.send_response(302)
                self.send_header("Location", "/must-not-follow")
                self.end_headers()
                return
            assert self.headers.get("Authorization") == "Bearer fixture-local-key"
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({"echo": "fixture-local-key", "claims": []}).encode())

        def do_POST(self):
            requests.append(("POST", self.path))
            assert self.headers.get("Authorization") == "Bearer fixture-local-key"
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({"accepted": body}).encode())

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix="lite-backend-cli-") as tmp:
            root = pathlib.Path(tmp)
            data = root / "data"
            env = dict(os.environ, TEST_SECRET="fixture-local-key")

            def call(method, arguments=None, okay=True):
                req, out = root / "request.json", root / "response.json"
                req.write_text(json.dumps({"method": method, "arguments": arguments or {}}), encoding="utf-8")
                result = subprocess.run([opts.exe, "--backend", "-", str(data), str(req), str(out)], env=env, timeout=20)
                response = json.loads(out.read_text(encoding="utf-8"))
                assert (result.returncode == 0) == okay, response
                assert "fixture-local-key" not in out.read_text(), "credential leaked into response"
                return response

            def confirm(method, args):
                preview = call(method, args)
                assert preview["requiresConfirmation"]
                return call(method, dict(args, confirmation=preview["confirmation"]))

            base = f"http://127.0.0.1:{server.server_port}"
            config = {"read": {"enabled": False, "url": base + "/read", "keyEnv": "TEST_SECRET"},
                      "write": {"enabled": True, "url": base + "/write", "method": "POST", "keyEnv": "TEST_SECRET"},
                      "redirect": {"enabled": True, "url": base + "/redirect"}}
            confirm("connections.set", config)
            call("network.read", {"connection": "read"}, okay=False)
            assert not requests, "disabled connection performed network request"
            config["read"]["enabled"] = True
            confirm("connections.set", config)
            assert call("network.read", {"connection": "read"})["data"]["echo"] == "[REDACTED]"
            preview = call("network.write", {"connection": "write", "body": {"module": "fixture"}})
            assert preview["details"]["method"] == "POST"
            assert len(requests) == 1, "write happened before confirmation"
            response = call("network.write", {"connection": "write", "body": {"module": "fixture"}, "confirmation": preview["confirmation"]})
            assert response["data"]["accepted"]["module"] == "fixture"
            assert call("network.read", {"connection": "redirect"})["status"] == 302
            assert len(requests) == 3 and all("must-not-follow" not in path for _, path in requests)
            confirm("preferences.set", {"reports": True})
            report = confirm("reports.export", {})
            assert "fixture-local-key" not in pathlib.Path(report["path"]).read_text()
            assert "network.write" in call("catalog")
            print("PASS packaged backend: disabled/no request, user environment key, native GET/POST, confirmed writes, redirect refusal, safe export")
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    main()
