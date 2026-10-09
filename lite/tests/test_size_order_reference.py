"""Equal hit-rate recommendations must retain original JavaScript insertion order."""
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
detail = (root / 'console/src/renderer/src/components/AiDetail.tsx').read_text(encoding='utf-8')
recommend = detail[detail.index('function recommend('):detail.index('export default function')]
buckets = ('<=0x40', '0x40-0x200', '0x200-0x800', '>0x800')
cases = [{'bySize': {name: {'attempts': 4, 'matches': 2} for name in order}}
         for order in itertools.permutations(buckets)]
with tempfile.TemporaryDirectory(prefix='tangos-size-order-') as folder:
    tmp = pathlib.Path(folder)
    (tmp / 'detail.ts').write_text(recommend.replace('function recommend(', 'export function recommend(', 1), encoding='utf-8')
    (tmp / 'oracle.mjs').write_text(
        "import fs from 'node:fs';import {recommend} from './detail.ts';"
        "const cases=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));"
        "process.stdout.write(JSON.stringify(cases.map(c=>recommend(c.bySize))));", encoding='utf-8')
    fixture = tmp / 'cases.json'
    fixture.write_text(json.dumps(cases), encoding='utf-8')
    reference = subprocess.run(['node', str(tmp / 'oracle.mjs'), str(fixture)],
                               capture_output=True, text=True, encoding='utf-8', timeout=20)
    assert reference.returncode == 0, reference.stderr
    expected = json.loads(reference.stdout)
    failures = []
    for value, wanted in zip(cases, expected):
        request, response = tmp / 'request.json', tmp / 'response.json'
        request.write_text(json.dumps({'method': 'policy.detail', 'arguments': value}), encoding='utf-8')
        run = subprocess.run([str(exe), '--backend', '-', str(tmp / 'data'), str(request), str(response)],
                             capture_output=True, timeout=20)
        assert run.returncode == 0, run.stderr
        actual = json.loads(response.read_text(encoding='utf-8-sig'))['recommendation']
        if actual != wanted:
            failures.append({'order': list(value['bySize']), 'native': actual, 'reference': wanted})
        # Read an original-style stats file, whose object order is still available.
        (tmp / 'data/stats.json').write_text(json.dumps({'agent-fixture': value}), encoding='utf-8')
        request.write_text(json.dumps({'method': 'stats.get', 'arguments': {}}), encoding='utf-8')
        run = subprocess.run([str(exe), '--backend', '-', str(tmp / 'data'), str(request), str(response)],
                             capture_output=True, timeout=20)
        assert run.returncode == 0, run.stderr
        stored = json.loads(response.read_text(encoding='utf-8-sig'))['agent-fixture']
        assert stored['bySizeOrder'] == list(value['bySize']), (stored, value)
    assert not failures, json.dumps(failures, indent=2)
print(f'PASS {len(cases)} original stable insertion-order recommendations')
