"""Compare all native function-list sort modes with the retained original sortFns."""
import argparse, json, pathlib, subprocess, tempfile

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--exe', required=True)
    exe = pathlib.Path(parser.parse_args().exe).resolve()
    root = pathlib.Path(__file__).resolve().parents[2]
    rows = [dict(id=str(i), name=name, module=module, addr=addr, size=size, matched=matched)
            for i, (name, module, addr, size, matched) in enumerate([
                ('alpha', 'ov2', 50, 16, False), ('Alpha', 'ov1', 40, 16, True),
                ('beta2', 'ov10', 30, 64, False), ('beta10', 'ov2', 20, 64, False),
                ('éclair', 'ARM9', 10, 128, True), ('Eclair', 'arm9', 10, 8, False),
                ('_start', 'ov1', 40, 16, False), ('alpha', 'ov2', 50, 16, False)])]
    keys = ('unmatched', 'size-desc', 'size-asc', 'name', 'addr', 'module')
    cases = [dict(functions=fixture, sort=key) for fixture in ([], rows[:1], rows, list(reversed(rows))) for key in keys]
    with tempfile.TemporaryDirectory(prefix='tangos-sort-reference-') as folder:
        tmp = pathlib.Path(folder)
        fixture = tmp / 'cases.json'; fixture.write_text(json.dumps(cases), encoding='utf-8')
        runner = tmp / 'reference.mjs'
        runner.write_text("import fs from 'node:fs';import {sortFns} from " + json.dumps((root/'console/src/renderer/src/atlas/sort.ts').as_uri()) + ";process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(c=>sortFns(c.functions,c.sort).map(f=>f.id))));", encoding='utf-8')
        run = subprocess.run(['node','--disable-warning=MODULE_TYPELESS_PACKAGE_JSON',str(runner),str(fixture)], capture_output=True, text=True, encoding='utf-8', timeout=20)
        assert run.returncode == 0, run.stderr
        for case, expected in zip(cases, json.loads(run.stdout)):
            request = tmp/'request.json'; response = tmp/'response.json'
            request.write_text(json.dumps(dict(method='policy.sort', arguments=case)), encoding='utf-8')
            native = subprocess.run([str(exe),'--backend','-',str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=20)
            assert native.returncode == 0, native.stderr
            actual = json.loads(response.read_text(encoding='utf-8-sig'))
            assert actual == expected, (case, actual, expected)
    print(f'PASS {len(cases)} original function-list sort cases including ties, case and accents')
if __name__ == '__main__': main()
