"""Exercise actual retained ActivityBus/AiDetail transforms against the native backend."""
import argparse
import json
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True)
exe = pathlib.Path(parser.parse_args().exe).resolve()
root = pathlib.Path(__file__).resolve().parents[2]
bus = (root / 'console/src/main/activityBus.ts').read_text(encoding='utf-8')
detail = (root / 'console/src/renderer/src/components/AiDetail.tsx').read_text(encoding='utf-8')
recommend = detail[detail.index('function recommend('):detail.index('export default function')]
streams = detail[detail.index("    const text = latest?.output ?? ''"):detail.index('  }, [latest?.output])')]
streams = streams.replace("    const text = latest?.output ?? ''", '')
cases = []
for count in (0, 1, 10, 301, 320):
    events = []
    for i in range(count):
        events.extend([
            {'kind': 'run-started', 'run': {'runId': str(i), 'output': '', 'status': 'running'}},
            {'kind': 'run-output', 'runId': str(i), 'chunk': 'first\n'},
            {'kind': 'run-output', 'runId': str(i), 'chunk': 'é漢字\n'},
            {'kind': 'run-finished', 'runId': str(i), 'status': 'ok' if i % 2 else 'error',
             'exitCode': i % 2, 'finishedAt': i + 1}])
    events.append({'kind': 'run-output', 'runId': 'unknown', 'chunk': 'ignored'})
    cases.append(events)
cases.append([{'kind': 'run-started', 'run': {'runId': 'tail', 'output': '', 'status': 'running'}},
              {'kind': 'run-output', 'runId': 'tail', 'chunk': 'a' * 210000 + 'é漢🙂'}])
cases.append([{'kind': 'run-started', 'run': {'runId': 'duplicate', 'output': 'old', 'status': 'running'}},
              {'kind': 'run-started', 'run': {'runId': 'duplicate', 'output': 'new', 'status': 'running'}},
              {'kind': 'run-output', 'runId': 'duplicate', 'chunk': '!'}])
outputs = ['', 'plain\n', 'one\ntwo', '⟦vendor/a⟧ one\n⟦vendor/b⟧two\nuntagged\n⟦vendor/a⟧ three\n',
           '⟦a⟧\n⟦b⟧  leading\n', '⟦⟧ invalid\n⟦vendor/a⟧é漢🙂', '⟦all⟧ reserved\n', '⟦v/a⟧x\r\n⟦v/b⟧y']
sizes = [{}, {'<=0x40': {'attempts': 1, 'matches': 1}},
         {'<=0x40': {'attempts': 4, 'matches': 3}, '>0x800': {'attempts': 2, 'matches': 0}},
         {'<=0x40': {'attempts': 8, 'matches': 1}, '>0x800': {'attempts': 3, 'matches': 2}}]
details = [{'output': output, 'bySize': sizes[i % len(sizes)]} for i, output in enumerate(outputs)]
roles = [{'matchAttempts': attempts, 'hitRate': hit, 'bySize': size}
         for attempts in (0, 3, 4, 8) for hit in (0, .24, .25, .49, .5, 1) for size in sizes]
role_source = (root / 'console/src/renderer/src/roleRec.ts').read_text(encoding='utf-8')
role_source = role_source[role_source.index('export function recommendRole'):role_source.index('export interface AutoRole')]
with tempfile.TemporaryDirectory(prefix='tangos-activity-reference-') as folder:
    tmp = pathlib.Path(folder)
    # Only type-only imports are removed; execute the original implementation.
    (tmp / 'bus.ts').write_text(bus.replace("import type { ActivityEvent, ActivityRun } from '../shared/types'", ''), encoding='utf-8')
    (tmp / 'detail.ts').write_text(recommend.replace('function recommend(', 'export function recommend(', 1) +
                                  '\nexport function streams(text: string) {\n' + streams + '\n}\n', encoding='utf-8')
    (tmp / 'role.ts').write_text(role_source, encoding='utf-8')
    fixture = tmp / 'cases.json'
    fixture.write_text(json.dumps({'events': cases, 'details': details, 'roles': roles}, ensure_ascii=False), encoding='utf-8')
    runner = tmp / 'oracle.mjs'
    runner.write_text("import fs from 'node:fs';import {activityBus} from './bus.ts';import {streams,recommend} from './detail.ts';"
                      "import {recommendRole} from './role.ts';"
                      "const c=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));const events=c.events.map(es=>{activityBus.clear();"
                      "for(const e of es)activityBus.publish(e);return activityBus.snapshot()});"
                      "process.stdout.write(JSON.stringify({events,roles:c.roles.map(stats=>recommendRole({stats})),details:c.details.map(c=>({streams:streams(c.output),recommendation:recommend(c.bySize)}))}));",
                      encoding='utf-8')
    result = subprocess.run(['node', str(runner), str(fixture)], capture_output=True, text=True, encoding='utf-8', timeout=30)
    assert result.returncode == 0, result.stderr
    oracle = json.loads(result.stdout)
    for method, inputs, expected in [('policy.activity', [{'events': e} for e in cases], oracle['events']),
                                      ('policy.role', [{'stats': r} for r in roles], oracle['roles']),
                                      ('policy.detail', details, oracle['details'])]:
        for value, wanted in zip(inputs, expected):
            request, response = tmp / 'request.json', tmp / 'response.json'
            request.write_text(json.dumps({'method': method, 'arguments': value}, ensure_ascii=False), encoding='utf-8')
            run = subprocess.run([str(exe), '--backend', '-', str(tmp / 'data'), str(request), str(response)], capture_output=True, timeout=30)
            assert run.returncode == 0, run.stderr
            actual = json.loads(response.read_text(encoding='utf-8-sig'))
            assert actual == wanted, (method, value if method != 'policy.activity' else len(value['events']), actual, wanted)
print(f'PASS {len(cases) + len(details) + len(roles)} original activity/detail comparisons: retention, Unicode tails, lifecycle, model streams and recommendations')
