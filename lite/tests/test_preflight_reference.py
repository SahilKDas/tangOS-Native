"""Requirement detection compared with original Console using local disposable inputs."""
import argparse,json,pathlib,subprocess,tempfile

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True);exe=pathlib.Path(parser.parse_args().exe).resolve()
    root=pathlib.Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='tangos-preflight-reference-') as folder:
        tmp=pathlib.Path(folder);repo=tmp/'repo';repo.mkdir()
        subprocess.run(['git','init','-b','main',str(repo)],capture_output=True,check=True)
        descriptor={'tangosVersion':'1','project':{'name':'fixture','title':'Fixture'},'tools':[],'runtime':{'python':'python'},'requirements':{'compiler':'FixtureCompilerUnavailable','rom':True,'pythonPackages':['sys','TangOSLite_missing_fixture']},'data':{'dbPath':'chaos-db.json','generate':'python fixture_generate.py'}}
        runner=tmp/'reference.mjs';runner.write_text("import fs from 'node:fs';import {preflight} from "+json.dumps((root/'console/src/main/preflight.ts').as_uri())+";const f=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));process.stdout.write(JSON.stringify(await preflight(f.repo,f.descriptor)));",encoding='utf-8')
        for stage in range(3):
            if stage==1:
                (repo/'tools').mkdir();(repo/'tools/FixtureCompilerUnavailable').mkdir();(repo/'extracted').mkdir();(repo/'chaos-db.json').write_text('{"functions":[]}')
            if stage==2:descriptor['requirements']['pythonPackages']=['sys']
            (repo/'tangos.json').write_text(json.dumps(descriptor))
            fixture=tmp/'fixture.json';fixture.write_text(json.dumps({'repo':str(repo),'descriptor':descriptor}))
            ref=subprocess.run(['node','--disable-warning=MODULE_TYPELESS_PACKAGE_JSON',str(runner),str(fixture)],capture_output=True,text=True,encoding="utf-8",timeout=30);assert ref.returncode==0,ref.stderr
            expected={r['id']:r['ok'] for r in json.loads(ref.stdout)}
            request=tmp/'request.json';response=tmp/'response.json';request.write_text(json.dumps({'method':'preflight','arguments':{}}))
            run=subprocess.run([str(exe),'--backend',str(repo),str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=30);assert run.returncode==0,run.stderr
            native=json.loads(response.read_text(encoding='utf-8-sig'));checks={r['name']:r['available'] for r in native['checks']}
            actual={'python':native['python']['available'],'pypkgs':all(p['available'] for p in native['pythonPackages']),'compiler':checks['Compiler (FixtureCompilerUnavailable)'],'rom':checks['Extracted ROM'],'chaosdb':checks['Atlas data']}
            assert actual==expected,(stage,actual,expected)
    print('PASS 15 requirement comparisons against original Console: Python, packages, compiler, ROM directory and Atlas')
if __name__=='__main__':main()
