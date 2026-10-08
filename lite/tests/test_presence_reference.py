"""Compare native presence states with the actual retained Controller function."""
import argparse
import json
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True)
exe = pathlib.Path(parser.parse_args().exe).resolve()
root = pathlib.Path(__file__).resolve().parents[2]
source = (root / 'console/src/renderer/src/components/Controller.tsx').read_text(encoding='utf-8')
body = source[source.index('const PRESENCE_GREEN_MS'):source.index('export interface AgentView')]
body = body.replace('function presenceClass(', 'export function presenceClass(', 1)
now = 1800000000000
cases = []
for kind in ('api', 'mcp', 'cli'):
    for seen in (0, now+1, now, now-299999, now-300000, now-3599999, now-3600000, now-7200000):
        for live in (False, True):
            cases.append({'kind': kind, 'lastSeen': seen, 'live': live, 'now': now})
with tempfile.TemporaryDirectory(prefix='tangos-presence-reference-') as folder:
    tmp = pathlib.Path(folder)
    module = tmp / 'presence.ts'
    module.write_text('type AiAgent = {kind: string; lastSeen?: number};\n' + body, encoding='utf-8')
    fixture = tmp / 'cases.json'
    fixture.write_text(json.dumps(cases), encoding='utf-8')
    runner = tmp / 'reference.mjs'
    runner.write_text("import fs from 'node:fs';import {presenceClass} from " + json.dumps(module.as_uri()) +
                      ";process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(c=>presenceClass(c,c.live,c.now))));",
                      encoding='utf-8')
    oracle = subprocess.run(['node', '--disable-warning=MODULE_TYPELESS_PACKAGE_JSON', str(runner), str(fixture)],
                            capture_output=True, text=True, encoding='utf-8', timeout=20)
    assert oracle.returncode == 0, oracle.stderr
    for case, expected in zip(cases, json.loads(oracle.stdout)):
        request, response = tmp / 'request.json', tmp / 'response.json'
        request.write_text(json.dumps({'method': 'policy.presence', 'arguments': case}), encoding='utf-8')
        result = subprocess.run([str(exe), '--backend', '-', str(tmp / 'data'), str(request), str(response)],
                                capture_output=True, timeout=20)
        assert result.returncode == 0, result.stderr
        actual = json.loads(response.read_text(encoding='utf-8-sig'))
        assert actual == expected, (case, actual, expected)
print(f'PASS {len(cases)} original presence comparisons: API availability, live pulse, future/no timestamps, exact 5 minute/1 hour decay')
