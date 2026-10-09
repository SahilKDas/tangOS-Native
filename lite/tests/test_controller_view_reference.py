"""Compare the native Controller view with the retained React transformation."""
import argparse
import itertools
import json
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True)
exe = pathlib.Path(parser.parse_args().exe).resolve()
root = pathlib.Path(__file__).resolve().parents[2]
source = (root / 'console/src/renderer/src/components/Controller.tsx').read_text(encoding='utf-8')
helpers = source[source.index('function runName('):source.index('// Presence dot thresholds')]
latest = source[source.index('    const m = new Map<string, ActivityRun>()'):source.index('  }, [runs])')]
views = source[source.index('    return agents.map((agent) => {'):source.index('  }, [agents, latestByName, batches])')]
cases = []
for status, current, output, ordering in itertools.product(
        ('queued', 'active', 'done'), (None, '', 'Compiling port/fixture.cpp'),
        ('', 'first\nsecond\r\n', 'x' * 430, 'λ' * 95 + '\u00a0\ufeff', 'old\n' + 'live' * 30),
        ('ascending', 'descending', 'equal')):
    agent = {'name': 'Fixture', 'stats': {'currentTask': current}}
    batches = [
        {'id': 'older', 'targetAgent': 'Fixture', 'createdAt': 1, 'status': 'done',
         'title': 'Previous batch', 'items': [{'worked': True}, {'done': True}]},
        {'id': 'latest', 'targetAgent': 'Fixture', 'createdAt': 2, 'status': status,
         'title': 'Latest batch', 'items': [{'worked': True}, {'done': True}, {'worked': False}, {}]},
        {'id': 'other', 'targetAgent': 'Other', 'createdAt': 99, 'status': 'queued',
         'title': 'Other agent', 'items': [{}]},
    ]
    if ordering == 'descending':
        batches.reverse()
    elif ordering == 'equal':
        batches[1]['createdAt'] = 1
    runs = [
        {'source': 'ai', 'client': {'name': 'Fixture'}, 'startedAt': 10, 'status': 'finished',
         'label': 'Older run', 'output': 'older'},
        {'source': 'ai', 'client': {'name': 'Fixture'}, 'startedAt': 20, 'status': 'running',
         'label': 'Live run', 'output': output},
        {'source': 'ui', 'startedAt': 100, 'status': 'running', 'label': 'Human', 'output': 'human'},
    ]
    if ordering == 'descending':
        runs.reverse()
    elif ordering == 'equal':
        runs[1]['startedAt'] = 10
    cases.append({'agent': agent, 'batches': batches, 'runs': runs})
cases += [
    {'agent': {'name': 'Fixture', 'stats': {}}, 'batches': [], 'runs': []},
    {'agent': {'name': 'AI', 'stats': {}}, 'batches': [], 'runs': [
        {'source': 'ai', 'client': None, 'startedAt': 1, 'status': 'running', 'label': 'Anonymous'}]},
    {'agent': {'name': 'You', 'stats': {}}, 'batches': [], 'runs': [
        {'source': 'ui', 'startedAt': 1, 'status': 'finished', 'label': 'Manual check'}]},
]
with tempfile.TemporaryDirectory(prefix='tangos-controller-view-') as folder:
    tmp = pathlib.Path(folder)
    module = tmp / 'controller.ts'
    module.write_text('type ActivityRun=any; type AiAgent=any;\n' + helpers +
                      '\nexport function view(c:any){const agents=[c.agent],runs=c.runs,batches=c.batches;\n'
                      'const latestByName=(()=>{\n' + latest + '\n})();\n' + views + '\n}\n', encoding='utf-8')
    fixture = tmp / 'cases.json'
    fixture.write_text(json.dumps(cases), encoding='utf-8')
    runner = tmp / 'reference.mjs'
    runner.write_text("import fs from 'node:fs';import {view} from " + json.dumps(module.as_uri()) +
                      ";process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(c=>view(c)[0])));",
                      encoding='utf-8')
    oracle = subprocess.run(['node', '--disable-warning=MODULE_TYPELESS_PACKAGE_JSON', str(runner), str(fixture)],
                            capture_output=True, text=True, encoding='utf-8', timeout=20)
    assert oracle.returncode == 0, oracle.stderr
    for case, expected in zip(cases, json.loads(oracle.stdout)):
        request, response = tmp / 'request.json', tmp / 'response.json'
        request.write_text(json.dumps({'method': 'policy.controllerView', 'arguments': case}), encoding='utf-8')
        result = subprocess.run([str(exe), '--backend', '-', str(tmp / 'data'), str(request), str(response)],
                                capture_output=True, timeout=20)
        assert result.returncode == 0, result.stderr
        actual = json.loads(response.read_text(encoding='utf-8-sig'))
        assert actual == expected, (case, actual, expected)
print(f'PASS {len(cases)} original Controller view comparisons: batch selection, progress, queue totals, latest output and task precedence')
