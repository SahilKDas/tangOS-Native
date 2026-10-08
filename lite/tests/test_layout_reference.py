"""Compare native Atlas geometry and grouping with the original Console."""
import argparse,json,pathlib,subprocess,tempfile,math

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True);exe=pathlib.Path(parser.parse_args().exe).resolve();root=pathlib.Path(__file__).resolve().parents[2]
    fixtures=[[],[{'id':'single','name':'single','module':'arm9','size':80,'matched':False}],
        [{'id':'a','name':'a','module':'arm9','size':80,'matched':False}, {'id':'b','name':'b','module':'ov1','size':80,'matched':True,'author':'Local'}, {'id':'c','name':'c','module':'arm9','size':16,'matched':False,'noMatch':{'bucket':'asm'}}, {'id':'d','name':'d','module':'ov1','size':120,'matched':False,'div':3}, {'id':'e','name':'e','module':'ov2','size':32,'matched':False,'srcPath':'port/e.cpp'}, {'id':'f','name':'f','module':'ov3','size':80,'matched':True,'author':'Other'}, {'id':'g','name':'g','module':'ov3','size':8,'matched':False,'author':'Local'}]]
    cases=[{'functions':rows,'mode':mode,'width':w,'height':h} for rows in fixtures for mode in ('ov','size','match','author') for w,h in [(800,600),(600,800)]]
    with tempfile.TemporaryDirectory(prefix='tangos-layout-reference-') as folder:
        tmp=pathlib.Path(folder);module=tmp/'layout.ts';module.write_text((root/'console/src/renderer/src/chaos/layout.ts').read_text(encoding='utf-8').replace("'../atlas/squarify'",repr((root/'console/src/renderer/src/atlas/squarify.ts').as_uri())),encoding='utf-8')
        fixture=tmp/'cases.json';fixture.write_text(json.dumps(cases));runner=tmp/'reference.mjs';runner.write_text("import fs from 'node:fs';import {buildWorld} from "+json.dumps(module.as_uri())+";process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(c=>{const w=buildWorld({functions:c.functions},c.width,c.height,c.mode);return w.fns.map(f=>({id:f.f.id,x:f.x,y:f.y,width:f.w,height:f.h}));})));",encoding='utf-8')
        run=subprocess.run(['node','--disable-warning=MODULE_TYPELESS_PACKAGE_JSON',str(runner),str(fixture)],capture_output=True,text=True,encoding='utf-8',timeout=20);assert run.returncode==0,run.stderr
        for case,want in zip(cases,json.loads(run.stdout)):
            request=tmp/'request.json';response=tmp/'response.json';request.write_text(json.dumps({'method':'policy.layout','arguments':case}))
            native=subprocess.run([str(exe),'--backend','-',str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=20);assert native.returncode==0,native.stderr
            actual=json.loads(response.read_text(encoding='utf-8-sig'));assert len(actual)==len(want),(case,actual,want)
            for a,b in zip(actual,want):
                assert a['id']==b['id'],(case,a,b)
                for field in ('x','y','width','height'):assert math.isclose(a[field],b[field],abs_tol=1e-7,rel_tol=1e-9),(case,field,a,b)
    print(f'PASS {len(cases)} exact reference Atlas layout/group/order/padding cases')
if __name__=='__main__':main()
