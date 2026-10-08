"""Report parity without inferring completion from wrappers or native-only tests."""
import argparse,collections,json,pathlib,re
parser=argparse.ArgumentParser();parser.add_argument('--require-full',action='store_true');args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parents[2]
manifest=json.loads((root/'lite/docs/parity.json').read_text(encoding='utf-8'))
reference=(root/manifest['reference']).read_text(encoding='utf-8')
contracts=set(re.findall(r'^  (\w+):',reference,re.M))
features=manifest['features'];recorded=[f['contract'] for f in features]
assert len(recorded)==len(set(recorded)), 'Duplicate parity contracts'
assert set(recorded)==contracts, {'unrecorded':sorted(contracts-set(recorded)), 'stale':sorted(set(recorded)-contracts)}
for feature in features:
    assert feature['state'] in {'verified','partial','unverified'}
    if feature['state']=='verified':assert feature['evidence'],'Verified contract lacks evidence'
counts=collections.Counter(f['state'] for f in features)
print('Public reference contracts:',len(features),';',', '.join(f'{name}={counts[name]}' for name in ('verified','partial','unverified')))
full=all(f['state']=='verified' for f in features)
assert manifest['fullParity']==full,'Parity claim disagrees with evidence inventory'
if args.require_full and not full:
    raise SystemExit('Full parity has not been verified; remaining contracts must be implemented and compared.')
