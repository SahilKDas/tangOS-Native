"""Run explicitly selected discovered checks through the packaged native backend.

Requires a user-owned checkout and toolchain. Complete logs/results remain in
--output. It never updates baselines, stages files or publishes anything.
"""
import argparse
import json
import pathlib
import subprocess
import re

p = argparse.ArgumentParser()
p.add_argument('--exe', required=True)
p.add_argument('--repo', required=True)
p.add_argument('--output', required=True)
p.add_argument('--python', help='Explicit local validation environment; does not change global Python')
p.add_argument('--check', action='append', required=True,
               choices=['Port references', 'Declaration agreement', 'Dead references', 'Link checks', 'Byte matching'])
p.add_argument('--timeout', type=int, default=1800)
args = p.parse_args()
exe, repo, output = [pathlib.Path(v).resolve() for v in (args.exe, args.repo, args.output)]
output.mkdir(parents=True, exist_ok=True)
def source_snapshot():
    return subprocess.run(['git', 'diff', '--binary', 'HEAD', '--', 'src'], cwd=repo,
                          capture_output=True, check=True, timeout=60).stdout

original_source = source_snapshot()
if args.python:
    state = output / 'state'
    state.mkdir(parents=True, exist_ok=True)
    (state / 'settings.ini').write_text('python=' + str(pathlib.Path(args.python).resolve()) + '\n', encoding='utf-8')

def call(method, arguments, label):
    request, response = output / (label + '-request.json'), output / (label + '-response.json')
    request.write_text(json.dumps({'method': method, 'arguments': arguments}), encoding='utf-8')
    result = subprocess.run([str(exe), '--backend', str(repo), str(output / 'state'), str(request), str(response)],
                            capture_output=True, timeout=args.timeout)
    value = json.loads(response.read_text(encoding='utf-8-sig'))
    if result.returncode != 0:
        raise RuntimeError(value)
    return value

checks = call('checks.list', {}, 'available')
summary = []
for n, name in enumerate(args.check):
    label = 'check-' + str(n)
    selected = next(c for c in checks if c['name'] == name)
    if not selected['available']:
        summary.append({'name': name, 'state': 'unavailable', 'requirement': selected['requirement']})
        continue
    arguments = {'name': name}
    preview = call('checks.run', arguments, label + '-preview')
    print('Reviewed command:', preview['details']['command'], flush=True)
    arguments['confirmation'] = preview['confirmation']
    result = call('checks.run', arguments, label)
    log = pathlib.Path(result['log'])
    assert log.is_file() and result['output'].replace('\r\n', '\n') in log.read_text(encoding='utf-8')
    state = 'passed' if result['exit'] == 0 else 'failed'
    if state == 'passed' and name == 'Link checks' and 'nothing to verify' in result['output']:
        state = 'skipped'
    existing = re.search(r'(\d+) disagreement\(s\)', result['output'])
    if state == 'passed' and name == 'Declaration agreement' and existing and int(existing[1]):
        state = 'passed-with-baseline'
    summary.append({'name': name, 'state': state, 'exit': result['exit'], 'log': str(log),
                    'command': preview['details']['command']})
    if existing:
        summary[-1]['existingDisagreements'] = int(existing[1])
    print(name + ': ' + summary[-1]['state'] + '; log=' + str(log), flush=True)
(output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
assert source_snapshot() == original_source, 'Checks changed src/; inspect the checkout immediately'
(output / 'source-safety.json').write_text(json.dumps({'trackedSourceUnchanged': True}), encoding='utf-8')
print(json.dumps(summary, indent=2))
