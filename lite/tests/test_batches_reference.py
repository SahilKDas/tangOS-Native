"""Compare native batch removal/reordering/history retention with original Console."""
import argparse,json,pathlib,re,subprocess,tempfile

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True);exe=pathlib.Path(parser.parse_args().exe).resolve();root=pathlib.Path(__file__).resolve().parents[2]
    source=(root/'console/src/main/index.ts').read_text(encoding='utf-8')
    chunks=[]
    for name in ['remove','reorder','clearDone']:
        start=source.index("ipcMain.handle('batch:"+name+"'");end=source.index("\nipcMain.handle(",start+1);chunks.append(source[start:end])
    prune=re.search(r'^function pruneDoneBatches\(\): void \{[\s\S]*?^\}',source,re.M);assert prune
    cases=[]
    for n in [0,1,6,33,40]:
        batches=[dict(id=str(i),agentId='agent',targetAgent='Fixture',title='Batch '+str(i),prompt='',items=[],status='queued' if i%7==0 else 'done',createdAt=i) for i in range(n)]
        for action,identity,direction in [('remove','0',1),('remove','missing',1),('reorder','0',-1),('reorder','0',1),('reorder',str(n-1),1),('reorder','missing',-1),('clearDone','',1),('prune','',1)]:cases.append(dict(batches=batches,action=action,id=identity,direction=direction))
    with tempfile.TemporaryDirectory(prefix='tangos-batches-reference-') as folder:
        tmp=pathlib.Path(folder);fixture=tmp/'cases.json';fixture.write_text(json.dumps(cases),encoding='utf-8');runner=tmp/'reference.ts'
        script="import fs from 'node:fs';let state={batches:[] as any[]};const handlers=new Map();const ipcMain={handle:(name,fn)=>handlers.set(name,fn)};const pushState=()=>{};const enrichedRows=new Map();const DONE_BATCHES_KEPT=30;\n"+'\n'.join(chunks)+'\n'+prune.group(0)+"\nprocess.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(c=>{state.batches=c.batches;if(c.action==='prune')pruneDoneBatches();else if(c.action==='reorder')handlers.get('batch:reorder')(null,{id:c.id,dir:c.direction===-1?'up':'down'});else handlers.get('batch:'+c.action)(null,c.id);return state.batches;})));"
        runner.write_text(script,encoding='utf-8');reference=subprocess.run(['node',str(runner),str(fixture)],capture_output=True,text=True,encoding='utf-8',timeout=20);assert reference.returncode==0,reference.stderr
        for case,want in zip(cases,json.loads(reference.stdout)):
            request=tmp/'request.json';response=tmp/'response.json';request.write_text(json.dumps({'method':'policy.batches','arguments':case}),encoding='utf-8')
            result=subprocess.run([str(exe),'--backend','-',str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=20);assert result.returncode==0,result.stderr
            actual=json.loads(response.read_text(encoding='utf-8-sig'));assert actual==want,(case,actual,want)
    print(f'PASS {len(cases)} original batch removal/reorder/clear/history retention cases')
if __name__=='__main__':main()
