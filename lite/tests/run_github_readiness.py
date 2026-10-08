"""Read a real PR through the native backend; never create, merge, or push."""
import argparse
import json
import pathlib
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--exe', required=True)
p.add_argument('--checkout', required=True)
p.add_argument('--repository', required=True, help='GitHub owner/repository')
p.add_argument('--pr', required=True)
p.add_argument('--output', required=True)
a = p.parse_args()
output = pathlib.Path(a.output).resolve()
output.mkdir(parents=True, exist_ok=True)
exe, checkout = pathlib.Path(a.exe).resolve(), pathlib.Path(a.checkout).resolve()

def call(arguments, label):
    request, response = output / (label + '-request.json'), output / (label + '-response.json')
    request.write_text(json.dumps({'method': 'git.action', 'arguments': arguments}), encoding='utf-8')
    run = subprocess.run([str(exe), '--backend', str(checkout), str(output / 'state'),
                          str(request), str(response)], capture_output=True, timeout=120)
    value = json.loads(response.read_text(encoding='utf-8-sig'))
    assert run.returncode == 0, value
    return value

summary = []
for action in ('PR readiness', 'PR checks'):
    arguments = {'action': action, 'remote': a.repository, 'ref': a.pr}
    preview = call(arguments, action.replace(' ', '-') + '-preview')
    arguments['confirmation'] = preview['confirmation']
    result = call(arguments, action.replace(' ', '-'))
    assert pathlib.Path(result['log']).is_file(), result
    summary.append({'action': action, 'exit': result['exit'], 'log': result['log']})
    print(action + ': exit=' + str(result['exit']) + '; log=' + result['log'])
(output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
