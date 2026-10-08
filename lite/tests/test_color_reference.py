"""Compare native tile colors with the original Console renderer (no browser)."""
import argparse, itertools, json, pathlib, subprocess, tempfile

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--exe',required=True)
    exe=pathlib.Path(parser.parse_args().exe).resolve(); root=pathlib.Path(__file__).resolve().parents[2]
    rows=[{}, {'matched':True}, {'matched':True,'author':'alias'}, {'matched':True,'author':'unknown'}, {'div':0}, {'div':None}, {'srcPath':'port/a.cpp'}, {'srcPath':''}, {'noMatch':{'bucket':'asm'},'matched':True,'author':'alias'}, {'noMatch':False}, {'noMatch':None}]
    cases=[dict(row=row,authors=authors,nearMiss=near,aliases={'alias':'Person'},colors={'Person':'#123456'}) for row,authors,near in itertools.product(rows,[False,True],[False,True])]
    with tempfile.TemporaryDirectory(prefix='tangos-color-reference-') as folder:
        tmp=pathlib.Path(folder); module=tmp/'classic.ts'
        source=root/'console/src/renderer/src/chaos/engine/render/classic.ts'
        module.write_text(source.read_text(encoding='utf-8').replace("'../anim'",repr((source.parent.parent/'anim.ts').as_uri())),encoding='utf-8')
        fixture=tmp/'cases.json'; fixture.write_text(json.dumps(cases),encoding='utf-8')
        runner=tmp/'reference.mjs'; runner.write_text("import fs from 'node:fs';import {fnColor} from "+json.dumps(module.as_uri())+";import {classic} from "+json.dumps((root/'console/src/renderer/src/chaos/themes/classic.ts').as_uri())+";process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(c=>fnColor(c.row,{theme:classic,colorBy:c.authors?'author':'status',showNearMiss:c.nearMiss,authorResolve:new Map(Object.entries(c.aliases)),authorColors:new Map(Object.entries(c.colors))}))));",encoding='utf-8')
        run=subprocess.run(['node','--disable-warning=MODULE_TYPELESS_PACKAGE_JSON',str(runner),str(fixture)],capture_output=True,text=True,encoding='utf-8',timeout=20); assert run.returncode==0,run.stderr
        for case,want in zip(cases,json.loads(run.stdout)):
            request=tmp/'request.json'; response=tmp/'response.json'; request.write_text(json.dumps({'method':'policy.color','arguments':case}),encoding='utf-8')
            run=subprocess.run([str(exe),'--backend','-',str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=20); assert run.returncode==0,run.stderr
            actual=json.loads(response.read_text(encoding='utf-8-sig')); assert actual['color']==want,(case,actual,want)
    print(f'PASS {len(cases)} reference Atlas status/draft/exemption/alias/color cases')
if __name__=='__main__':main()
