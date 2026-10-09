"""Compare Simple-mode roles with the actual original renderer, without providers."""
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
renderer = root / 'console/src/renderer/src'
names = ['unknown', 'Fable 5', 'Opus', 'gpt5', 'gpt-5', 'o3', 'o34', 'Sonnet', 'Grok',
         'DeepSeek', 'Kimi', 'Moonshot', 'GLM', 'Zhipu', 'Nemotron', 'Nemo', 'Gemma', 'Mistral', 'Haiku', 'OpenAI']
providers = ['', 'Claude', 'GLM', 'GPT', 'Grok', 'DeepSeek', 'Nemotron', 'Requesty', 'Kimi', 'custom']
stats = [{}, {'matchAttempts': 3, 'hitRate': .8, 'bySize': {}},
         {'matchAttempts': 4, 'hitRate': .8},
         {'matchAttempts': 4, 'hitRate': .1, 'bySize': {}},
         {'matchAttempts': 4, 'hitRate': .5, 'bySize': {}},
         {'matchAttempts': 4, 'hitRate': .2, 'bySize': {'>0x800': {'attempts': 5, 'matches': 2}}}]
agents = [{'name': name, 'provider': provider, 'stats': stat, 'roles': [], 'hiddenRole': hidden}
          for name, provider, stat, hidden in itertools.product(names, providers, stats,
              ['', 'Hard matcher', 'Random', 'Drafter', 'Refiner', 'invalid'])]
agents += [{'name': 'Opus', 'stats': stats[-1], 'roles': [role], 'hiddenRole': hidden}
           for role, hidden in itertools.product(['Hard matcher', 'Random', 'Drafter', 'Refiner'], ['', 'Refiner'])]
with tempfile.TemporaryDirectory(prefix='tangos-auto-role-reference-') as folder:
    tmp = pathlib.Path(folder)
    source = (renderer / 'roleRec.ts').read_text(encoding='utf-8')
    source = source.replace("'../../shared/types'", "'./types.ts'").replace("'./efforts'", "'./efforts.ts'")
    (tmp / 'roleRec.ts').write_text(source, encoding='utf-8')
    (tmp / 'types.ts').write_text((root / 'console/src/shared/types.ts').read_text(encoding='utf-8'), encoding='utf-8')
    (tmp / 'efforts.ts').write_text((renderer / 'efforts.ts').read_text(encoding='utf-8'), encoding='utf-8')
    cases = tmp / 'agents.json'
    cases.write_text(json.dumps(agents), encoding='utf-8')
    oracle = tmp / 'oracle.mjs'
    oracle.write_text("import fs from 'node:fs';import {autoRole} from './roleRec.ts';"
                      "process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(autoRole)));", encoding='utf-8')
    run = subprocess.run(['node', str(oracle), str(cases)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stderr
    expected = json.loads(run.stdout)
    request, response = tmp / 'request.json', tmp / 'response.json'
    request.write_text(json.dumps({'method': 'policy.autoRole', 'arguments': {'agents': agents}}), encoding='utf-8')
    native = subprocess.run([str(exe), '--backend', '-', str(tmp / 'data'), str(request), str(response)], capture_output=True, timeout=30)
    assert native.returncode == 0, native.stderr
    actual = json.loads(response.read_text(encoding='utf-8-sig'))
    assert len(actual) == len(expected)
    for agent, got, wanted in zip(agents, actual, expected):
        assert got == wanted, (agent, got, wanted)
print(f'PASS {len(agents)} original Simple-mode automatic role comparisons')

effort_agents = [{'name': name, 'provider': provider, 'effort': effort}
                 for name, provider, effort in itertools.product(names, providers,
                 ['', 'invalid', 'off', 'minimal', 'low', 'medium', 'high', 'xhigh', 'max',
                  'chat', 'reasoner', 'nvidia/nemotron-3-super-120b-a12b', 'poolside/laguna-m.1'])]
with tempfile.TemporaryDirectory(prefix='tangos-effort-reference-') as folder:
    tmp = pathlib.Path(folder)
    (tmp / 'efforts.ts').write_text((renderer / 'efforts.ts').read_text(encoding='utf-8'), encoding='utf-8')
    cases = tmp / 'agents.json'
    cases.write_text(json.dumps(effort_agents), encoding='utf-8')
    oracle = tmp / 'oracle.mjs'
    oracle.write_text("import fs from 'node:fs';import {familyOf,effortSpec,currentEffort} from './efforts.ts';"
                     "process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(a=>({family:familyOf(a),spec:effortSpec(a),current:currentEffort(a)}))));", encoding='utf-8')
    run = subprocess.run(['node', str(oracle), str(cases)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stderr
    expected = json.loads(run.stdout)
    request, response = tmp / 'request.json', tmp / 'response.json'
    request.write_text(json.dumps({'method': 'policy.effort', 'arguments': {'agents': effort_agents}}), encoding='utf-8')
    native = subprocess.run([str(exe), '--backend', '-', str(tmp / 'data'), str(request), str(response)], capture_output=True, timeout=30)
    assert native.returncode == 0, native.stderr
    actual = json.loads(response.read_text(encoding='utf-8-sig'))
    assert len(actual) == len(expected)
    for agent, got, wanted in zip(effort_agents, actual, expected):
        assert got == wanted, (agent, got, wanted)
print(f'PASS {len(effort_agents)} original provider effort comparisons')

main = (root / 'console/src/main/index.ts').read_text(encoding='utf-8')
start = main.index('  const jobs =', main.index('const attempts = agentAttempts[agentName] ?? DEFAULT_ATTEMPTS'))
jobs_source = main[start:main.index('  batch.status =', start)]
start = main.index('  const v = Math.floor(Number(n))', main.index("'policy:setAgentFanout'"))
fanout_source = main[start:main.index('  saveSettings()', start)]
driver_cases = [{'agent': {'name': name, 'jobs': 3},
                 'preferences': {'useAgents': enabled, 'agentFanout': fanout}, 'targets': targets}
                for name, enabled, fanout, targets in itertools.product(
                    ['Opus', 'Fable', 'Sonnet', 'GPT', 'GLM', 'Grok', 'DeepSeek', 'Nemotron', 'Requesty', 'Kimi', 'unknown'],
                    [False, True], [-2, -.1, 0, .9, 1, 1.99, 8, 8.9, 64, 65, 200, None], [0, 1, 7, 16, 32])]
with tempfile.TemporaryDirectory(prefix='tangos-driver-policy-reference-') as folder:
    tmp = pathlib.Path(folder)
    cases = tmp / 'cases.json'
    cases.write_text(json.dumps(driver_cases), encoding='utf-8')
    oracle = tmp / 'oracle.mjs'
    oracle.write_text("import fs from 'node:fs';function jobsFor(agentName,state){" + jobs_source + "return jobs;}"
                     "function fanoutFor(n){const state={};" + fanout_source + "return state.agentFanout;}"
                     "process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(c=>{"
                     "const f=fanoutFor(c.preferences.agentFanout);return {jobs:jobsFor(c.agent.name,c.preferences),"
                     "functionsPerAgent:f,subAgents:Math.max(1,Math.round(c.targets/f)),useAgents:c.preferences.useAgents};})));",
                     encoding='utf-8')
    run = subprocess.run(['node', str(oracle), str(cases)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stderr
    expected = json.loads(run.stdout)
    request, response = tmp / 'request.json', tmp / 'response.json'
    request.write_text(json.dumps({'method': 'policy.drive', 'arguments': {'cases': driver_cases}}), encoding='utf-8')
    native = subprocess.run([str(exe), '--backend', '-', str(tmp / 'data'), str(request), str(response)], capture_output=True, timeout=30)
    assert native.returncode == 0, native.stderr
    actual = json.loads(response.read_text(encoding='utf-8-sig'))
    assert len(actual) == len(expected)
    for case, got, wanted in zip(driver_cases, actual, expected):
        assert got == wanted, (case, got, wanted)
print(f'PASS {len(driver_cases)} original worker/sub-agent policy comparisons')
