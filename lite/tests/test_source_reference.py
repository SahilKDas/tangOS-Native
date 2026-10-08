"""Compare packaged native source queries with the original best-effort handler."""
import argparse,json,pathlib,subprocess,tempfile

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True);exe=pathlib.Path(parser.parse_args().exe).resolve();root=pathlib.Path(__file__).resolve().parents[2]
    original=(root/'console/src/main/index.ts').read_text(encoding='utf-8');start=original.index("ipcMain.handle('atlas:source'");end=original.index("\nipcMain.handle(",start+1);handler=original[start:end]
    with tempfile.TemporaryDirectory(prefix='tangos-source-reference-') as folder:
        tmp=pathlib.Path(folder);repo=tmp/'repo';(repo/'port').mkdir(parents=True)
        descriptor={'tangosVersion':'1','project':{'name':'fixture','title':'Source fixture'},'tools':[]}
        (repo/'tangos.json').write_text(json.dumps(descriptor),encoding='utf-8')
        texts=['','int value = 1;','a\n','a\r\nb\r','// café and 日本語\n','\n'.join('line '+str(i) for i in range(400)),'\n'.join('line '+str(i) for i in range(401)),'\n'.join('line '+str(i) for i in range(400))+'\n']
        cases=[]
        for index,source in enumerate(texts):
            name=f'port/source-{index}.cpp';(repo/name).write_bytes(source.encode('utf-8'));cases.append({'request':{'id':'function','srcPath':name},'rows':[]})
        for disasm in ['','demo mov r0, r1\n','\n'.join('demo instruction '+str(i) for i in range(401))]:
            for path in ['','port/missing.cpp','../outside.cpp']:
                cases.append({'request':{'id':'function','srcPath':path},'rows':[{'id':'function','disasm':disasm}]})
        cases.extend([{'request':{},'rows':[]},{'request':{'id':None,'srcPath':'port/source-0.cpp'},'rows':[]},{'request':{'id':'unknown'},'rows':[]}])
        (tmp/'outside.cpp').write_text('Outside repository',encoding='utf-8');fixture=tmp/'cases.json';fixture.write_text(json.dumps(cases),encoding='utf-8');runner=tmp/'reference.ts'
        runner.write_text("import fs from 'node:fs';import {resolve,relative,isAbsolute} from 'node:path';const {existsSync,readFileSync}=fs;const handlers=new Map();const ipcMain={handle:(name,fn)=>handlers.set(name,fn)};const state={repoPath:process.argv[3]};const activeProjectId='fixture';const atlasCache={project:'fixture',local:null as any,live:null};const SOURCE_LINE_CAP=400;\n"+handler+"\nprocess.stdout.write(JSON.stringify(JSON.parse(readFileSync(process.argv[2],'utf8')).map(c=>{atlasCache.local={functions:c.rows};return handlers.get('atlas:source')(null,c.request);})));",encoding='utf-8')
        expected=subprocess.run(['node',str(runner),str(fixture),str(repo)],capture_output=True,text=True,encoding='utf-8',timeout=20);assert expected.returncode==0,expected.stderr
        for case,want in zip(cases,json.loads(expected.stdout)):
            (repo/'chaos-db.json').write_text(json.dumps({'functions':case['rows']}),encoding='utf-8');request=tmp/'request.json';response=tmp/'response.json';request.write_text(json.dumps({'method':'atlas.source','arguments':case['request']}),encoding='utf-8')
            actual=subprocess.run([str(exe),'--backend',str(repo),str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=20);assert actual.returncode==0,(case,actual.stderr,response.read_text(encoding='utf-8-sig') if response.exists() else '')
            value=json.loads(response.read_text(encoding='utf-8-sig'));assert value==want,(case,value,want)
    print(f'PASS {len(cases)} original source-query cap/newline/Unicode/fallback cases')
if __name__=='__main__':main()
