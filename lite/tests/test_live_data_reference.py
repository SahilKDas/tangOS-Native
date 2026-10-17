"""Compare native feed, event and GitHub identity contracts with retained Console code."""
import argparse
import json
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--exe', required=True)
    exe = pathlib.Path(parser.parse_args().exe).resolve()
    root = pathlib.Path(__file__).resolve().parents[2]
    count = 0
    with tempfile.TemporaryDirectory(prefix='tangos-live-reference-') as folder:
        tmp = pathlib.Path(folder); repo = tmp / 'repo'; repo.mkdir()
        (repo / 'tangos.json').write_text(json.dumps({'tangosVersion': '1', 'project': {'name': 'fixture', 'title': 'Fixture'}, 'tools': []}))
        runner = tmp / 'reference.mts'
        runner.write_text(r'''
import fs from 'node:fs';
import { githubCredits } from GH_MODULE;
import { fetchCosmetics, fetchCounts, fetchProgress, bustCosmeticsCache } from FEED_MODULE;
const a=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
let result;
if(a.op==='credits') {
  globalThis.fetch=async (url:any)=>({ok:true,json:async()=>url.includes('/contributors?')?a.contributors:url.includes('/pulls?')?a.pulls:a.commits[Number(new URL(url).searchParams.get('page'))-1]??[]}) as any;
  result=await githubCredits('https://github.com/fixture/project');
} else if(a.op==='feed') {
  globalThis.fetch=async()=>({ok:true,json:async()=>a.data}) as any;
  bustCosmeticsCache();
  result=a.method==='atlas.cosmetics'?await fetchCosmetics(null,'https://fixture.invalid'):a.method==='atlas.counts'?await fetchCounts(null,'https://fixture.invalid'):await fetchProgress(null,'https://fixture.invalid');
} else if(a.op==='progress') {
  const source=fs.readFileSync(ATLAS_SOURCE,'utf8');
  const part=source.slice(source.indexOf('const next = new Set<string>'),source.indexOf('return { stats, matched: next, records }'));
  fs.writeFileSync(PROGRESS_MODULE,'export function apply(prev:any,d:any){ const stats=d.stats;'+part+'return {stats,matched:[...next],records:Object.fromEntries(records)};}');
  const {apply}=await import(PROGRESS_URL);
  let value=a.initial??{};
  for(const event of a.events) {
    if(!event.ready||!event.stats)continue;
    const prev={matched:value.matched??[],records:new Map(Object.entries(value.records??{}))};
    value={ready:true,...apply(prev,event)};
  }
  const live={matched:new Set(value.matched??[]),records:new Map(Object.entries(value.records??{})),stats:value.stats??{}};
  const rawDb=a.database;
  const overlay=source.slice(source.indexOf('const functions = rawDb.functions.map'),source.indexOf('}, [rawDb, live, source])'));
  fs.writeFileSync(OVERLAY_MODULE,'export function overlay(rawDb:any,live:any){'+overlay+'}');
  const {overlay:overlayDb}=await import(OVERLAY_URL);
  result={progress:value,database:overlayDb(rawDb,live)};} else if(a.op==='sse') {
  const source=fs.readFileSync(SSE_SOURCE,'utf8');
  const parse=source.slice(source.indexOf('function parseFrame('));
  fs.writeFileSync(PARSE_MODULE,parse+'\nexport {parseFrame};');
  const {parseFrame}=await import(PARSE_URL);
  result=a.chunks.join('').replace(/\r\n/g,'\n').split('\n\n').map(parseFrame).filter(Boolean);
}
process.stdout.write(JSON.stringify(result));
'''.replace('GH_MODULE', json.dumps((root/'console/src/main/github.ts').as_uri())).replace('FEED_MODULE', json.dumps((root/'console/src/main/cosmetics.ts').as_uri())).replace('ATLAS_SOURCE', json.dumps(str(root/'console/src/renderer/src/components/AtlasView.tsx'))).replace('PROGRESS_MODULE', json.dumps(str(tmp/'progress.mts'))).replace('PROGRESS_URL', json.dumps((tmp/'progress.mts').as_uri())).replace('OVERLAY_MODULE', json.dumps(str(tmp/'overlay.mts'))).replace('OVERLAY_URL', json.dumps((tmp/'overlay.mts').as_uri())).replace('SSE_SOURCE', json.dumps(str(root/'console/src/main/cosmetics.ts'))).replace('PARSE_MODULE', json.dumps(str(tmp/'parse.mts'))).replace('PARSE_URL', json.dumps((tmp/'parse.mts').as_uri())), encoding='utf-8')
        def compare(a):
            nonlocal count
            request, response, fixture = tmp/'request.json', tmp/'response.json', tmp/'fixture.json'
            request.write_text(json.dumps({'method':'policy.liveData','arguments':a})); fixture.write_text(json.dumps(a))
            native = subprocess.run([str(exe),'--backend',str(repo),str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=30)
            assert native.returncode == 0, (native.stderr, response.read_text())
            reference = subprocess.run(['node','--disable-warning=MODULE_TYPELESS_PACKAGE_JSON',str(runner),str(fixture)],capture_output=True,text=True,encoding='utf-8',timeout=30)
            assert reference.returncode == 0, reference.stderr
            assert json.loads(response.read_text(encoding='utf-8-sig')) == json.loads(reference.stdout), (a,response.read_text(),reference.stdout)
            count += 1
        for data in [{}, {'colors':{'alice':'#Ab12EF','bad':'red'},'stars':[{'function':'日本語','by':'alice','at':'now'},{'function':'f','by':'b'},{'by':'invalid'}]}, {'colors':{},'stars':[]}]:
            compare({'op':'feed','method':'atlas.cosmetics','data':data})
        for method in ['atlas.counts','atlas.progress']:
            for data in [{}, {'totals':{'alice':2},'daily':{'alice':1}}, {'ready':False,'stats':{}}, {'ready':True,'stats':{'totalFunctions':3,'matchedFunctions':1,'totalBytes':20,'matchedBytes':10},'matched':['arm9:1']}]:
                compare({'op':'feed','method':method,'data':data})
        for email, name in [('42+Alice@users.noreply.github.com','Alice'),('Alice@users.noreply.github.com','Alice'),('Alias@EXAMPLE.invalid','Alice'),('','  Alice  '),('@invalid',' Name '),('user@example.invalid','')]:
            rows = [{'author':{'login':'alice'},'commit':{'author':{'email':email,'name':name}}},{'author':{'login':'other'},'commit':{'author':{'email':email,'name':name}}},{'author':None,'commit':{'author':{'email':'unknown@invalid'}}}]
            compare({'op':'credits','contributors':[{'login':'alice','contributions':2},{'login':'x[BoT]','contributions':3},{'login':'github-actions','contributions':1}],'commits':[rows],'pulls':[{'user':{'login':'alice'}},{'user':{'login':'alice'}},{'user':{'login':'ci[bot]'}},{'user':None}]})
        database = {'functions':[{'id':'a','matched':True,'author':'local'},{'id':'b','matched':False}],'stats':{'custom':9}}
        sequences = [
            [{'ready':True,'stats':{'matchedFunctions':1},'matched':['a']}],
            [{'ready':True,'stats':{'matchedFunctions':1},'matched':['a']},{'ready':True,'stats':{'matchedFunctions':1},'added':['b'],'removed':['a'],'changed':[{'id':'a','author':'remote'}]}],
            [{'ready':True,'stats':{},'matched':['a']},{'ready':True,'stats':{},'matched':[]}],
            [{'ready':True,'stats':{},'matched':['a']},{'ready':False,'stats':{},'removed':['a']}],
            [{'ready':True,'stats':{},'matched':[],'added':['b','b'],'changed':[{'id':'b','author':'first'}]},{'ready':True,'stats':{},'changed':[{'id':'b','author':'last'}]}]
        ]
        for events in sequences:
            compare({'op':'progress','initial':{},'events':events,'database':database})
        for text in [': heartbeat\n\nevent: progress\ndata: {"ready":false}\n\n','data: bad\n\ndata: {"日本語":2}\n\n','event: counts\r\ndata: {\r\ndata: "daily":{}}\r\n\r\n']:
            compare({'op':'sse','chunks':list(text)})
    print(f'PASS {count} original Console live-feed, SSE and GitHub identity comparisons')

if __name__ == '__main__': main()