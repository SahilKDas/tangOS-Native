"""Compare repair argv against actual Console runPreflightFix without installing anything."""
import argparse
import json
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True)
    exe = pathlib.Path(parser.parse_args().exe).resolve()
    root = pathlib.Path(__file__).resolve().parents[2]
    original = (root / 'console/src/main/preflight.ts').read_text(encoding='utf-8')
    handler = original[original.index('export async function runPreflightFix('):]
    count = 0
    with tempfile.TemporaryDirectory(prefix='tangos-repair-reference-') as folder:
        tmp = pathlib.Path(folder)
        repo = tmp / 'repo with spaces'
        (repo / 'tools').mkdir(parents=True)
        subprocess.run(['git', '-C', str(repo), 'init', '-b', 'main'], check=True, capture_output=True)
        (repo / 'fixture.txt').write_text('fixture', encoding='utf-8')
        subprocess.run(['git', '-C', str(repo), 'add', '.'], check=True, capture_output=True)
        subprocess.run(['git', '-C', str(repo), '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', 'fixture'], check=True, capture_output=True)
        (repo / 'tools/unpack.py').write_text('print("synthetic only")\n', encoding='utf-8')
        local_input = tmp / 'synthetic input.bin'
        local_input.write_text('not a ROM', encoding='utf-8')
        runner = tmp / 'repair-reference.ts'
        runner.write_text(
            "import {existsSync,readFileSync} from 'node:fs'; import {join} from 'node:path';\n"
            "let argv:string[]=[];const run=async(exe:string,args:string[],cwd:string)=>{argv=[exe,...args];return {code:0,out:''};};\n" + handler +
            "\nconst request=JSON.parse(readFileSync(process.argv[2],'utf8'));await runPreflightFix(request.id,process.argv[3],request.descriptor,request.filePath);process.stdout.write(JSON.stringify(argv));\n", encoding='utf-8')
        cases = [('python', 'python', [], False), ('pypkgs', 'python', ['pillow', 'pyelftools==0.31'], False),
                 ('pypkgs', 'custom-python', ['pinned-package==1', 'second-package==2'], False), ('pypkgs', 'python', [], True),
                 ('rom', 'python', [], False)]
        for id_, python, packages, pinned in cases:
            descriptor = {'tangosVersion':'1','project':{'name':'fixture','title':'Repair fixture'},'runtime':{'python':python},'tools':[], 'requirements':{'pythonPackages':packages}}
            (repo / 'tangos.json').write_text(json.dumps(descriptor), encoding='utf-8')
            requirements = repo / 'requirements.txt'
            if pinned:
                requirements.write_text('fixture-package==1\n', encoding='utf-8')
            else:
                requirements.unlink(missing_ok=True)
            arguments = {'id':id_}
            if id_ == 'rom':
                arguments['filePath'] = str(local_input)
            fixture, request, response = tmp/'fixture.json', tmp/'request.json', tmp/'response.json'
            fixture.write_text(json.dumps({**arguments, 'descriptor':descriptor}), encoding='utf-8')
            reference = subprocess.run(['node', str(runner), str(fixture), str(repo)], capture_output=True, text=True, encoding='utf-8', timeout=20)
            assert reference.returncode == 0, reference.stderr
            request.write_text(json.dumps({'method':'preflight.fix','arguments':arguments}), encoding='utf-8')
            result = subprocess.run([str(exe), '--backend', str(repo), str(tmp/'data'), str(request), str(response)], capture_output=True, timeout=20)
            assert result.returncode == 0, result.stderr
            actual = json.loads(response.read_text(encoding='utf-8-sig'))
            assert actual['requiresConfirmation'] is True
            assert actual['details']['argv'] == json.loads(reference.stdout), (arguments, actual, reference.stdout)
            count += 1
    print(f'PASS {count} actual Console repair-command comparisons; no installations or ROM extraction')


if __name__ == '__main__':
    main()