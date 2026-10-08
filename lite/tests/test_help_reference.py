"""Compare native editable help parsing with retained original Console parsers."""
import argparse,json,pathlib,subprocess,tempfile

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True);exe=pathlib.Path(parser.parse_args().exe).resolve()
    root=pathlib.Path(__file__).resolve().parents[2]
    cases=['','# comment','# comment\n\nPlain title\nBody','[thinking] @settings\nKeys\nLocal\nonly','@mcp\nConnection\nBody','[smile]\nHello\nBody','A [smile] title\nText','[unknown-emotion] @some-spot\nCustom\nBody','[smile] Tip\nBody','[smile]\r\nTitle\r\nFirst\r\nSecond','One\nbody\n \nTwo\nsecond','[idle] @toggle\nTango’s guide\n# comment\nLocal only']
    with tempfile.TemporaryDirectory(prefix='tangos-help-reference-') as folder:
        tmp=pathlib.Path(folder);fixture=tmp/'cases.json';fixture.write_text(json.dumps(cases))
        for kind in ('tour','tips'):
            source=(root/f'console/src/main/{kind}.ts').read_text(encoding='utf-8');body=source[source.index('function parse('):source.index('export function read'+('Tour' if kind=='tour' else 'Tips'))].replace('function parse(','export function parse(',1)
            module=tmp/(kind+'.ts');module.write_text(body,encoding='utf-8')
            runner=tmp/'reference.mjs';runner.write_text("import fs from 'node:fs';import {parse} from "+json.dumps(module.as_uri())+";process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(parse)));",encoding='utf-8')
            run=subprocess.run(['node','--disable-warning=MODULE_TYPELESS_PACKAGE_JSON',str(runner),str(fixture)],capture_output=True,text=True,encoding="utf-8",timeout=20);assert run.returncode==0,run.stderr
            for text,want in zip(cases,json.loads(run.stdout)):
                request=tmp/'request.json';response=tmp/'response.json';request.write_text(json.dumps({'method':'guide.parse','arguments':{'text':text,'tour':kind=='tour'}}))
                native=subprocess.run([str(exe),'--backend','-',str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=20);assert native.returncode==0,native.stderr
                actual=json.loads(response.read_text(encoding='utf-8-sig'));assert actual==want,(kind,text,actual,want)
    print(f'PASS {len(cases)*2} tour/tips parser comparisons against original Console')
if __name__=='__main__':main()
