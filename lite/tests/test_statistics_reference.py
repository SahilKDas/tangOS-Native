"""Native statistics compared against the original Console; no external connections."""
import argparse,json,pathlib,subprocess,tempfile

def main():
    args=argparse.ArgumentParser();args.add_argument('--exe',required=True);exe=pathlib.Path(args.parse_args().exe).resolve()
    root=pathlib.Path(__file__).resolve().parents[2]
    rows=[{'name':'late','matched':False,'size':64,'divergences':8},
          {'name':'late','matched':False,'size':64,'divergences':4},
          {'name':'late','matched':True,'size':64,'divergences':0},
          {'name':'late','matched':True,'size':64,'divergences':0},
          {'name':'far','matched':False,'size':64,'divergences':14},
          {'name':'large','matched':False,'size':2049,'divergences':20},
          {'name':'medium','matched':True,'size':512,'tokensIn':25,'tokensOut':10},
          {'name':'upper','matched':False,'size':2048},
          {'name':'lower','matched':False,'size':65}]
    rows += [{'name':f'recent-{i}','matched':i%3==0,'size':16} for i in range(20)]
    rows += [{'matched':False,'size':64}, {'matched':True,'size':64}, {'matched':True,'size':64}]
    with tempfile.TemporaryDirectory(prefix='tangos-stats-reference-') as folder:
        tmp=pathlib.Path(folder);repo=tmp/'repo';repo.mkdir()
        (repo/'tangos.json').write_text(json.dumps({'tangosVersion':'1','project':{'name':'fixture','title':'Fixture'},'tools':[]}))
        adaptive=tmp/'adaptive.ts';adaptive.write_text((root/'console/src/main/adaptiveRole.ts').read_text(encoding='utf-8').replace("'../shared/types'",repr((root/'console/src/shared/types.ts').as_uri())),encoding='utf-8')
        stats=tmp/'statistics.ts';stats.write_text((root/'console/src/main/aiStats.ts').read_text(encoding='utf-8').replace("'./adaptiveRole'",repr(adaptive.as_uri())).replace("'../shared/types'",repr((root/'console/src/shared/types.ts').as_uri())),encoding='utf-8')
        fixture=tmp/'rows.json';fixture.write_text(json.dumps(rows))
        runner=tmp/'reference.mjs';runner.write_text("import fs from 'node:fs'; import {aiStats} from "+json.dumps(stats.as_uri())+";const rows=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));let outputs=[];for(const r of rows){aiStats.recordMatch('fixture',r.matched,r.size,r.name);if(r.divergences>0)aiStats.recordNearMiss('fixture',r.name,r.divergences,r.size);if(r.tokensIn||r.tokensOut)aiStats.recordTokens('fixture',r.tokensIn||0,r.tokensOut||0);outputs.push(JSON.parse(JSON.stringify({entry:aiStats.serialize().fixture,best:aiStats.serializeBestDiv()})));}process.stdout.write(JSON.stringify(outputs));",encoding='utf-8')
        run=subprocess.run(['node','--disable-warning=MODULE_TYPELESS_PACKAGE_JSON',str(runner),str(fixture)],capture_output=True,text=True,encoding="utf-8",timeout=20);assert run.returncode==0,run.stderr
        expected=json.loads(run.stdout);entry={};best={}
        for i,(row,want) in enumerate(zip(rows,expected)):
            request=tmp/'request.json';response=tmp/'response.json'
            request.write_text(json.dumps({'method':'policy.statistics','arguments':{'entry':entry,'best':best,'rows':[row]}}))
            run=subprocess.run([str(exe),'--backend',str(repo),str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=20)
            assert run.returncode==0,run.stderr
            actual=json.loads(response.read_text(encoding='utf-8-sig'));entry=actual['entry'];best=actual['best'];ref=want['entry']
            for native,original in [('attempts','matchAttempts'),('declaredMatches','totalMatches'),('nearMisses','nearMisses'),('bySize','bySize'),('attemptedFuncs','attemptedFuncs'),('matchedFuncs','matchedFuncs'),('nearMissFuncs','nearMissFuncs'),('tokensIn','tokensIn'),('tokensOut','tokensOut')]:
                default=[] if original.endswith('Funcs') else {} if original=='bySize' else 0
                assert entry.get(native,default)==ref.get(original,default),(i,native,entry,ref)
            assert [int(v) for v in entry['recent']]==ref.get('recentOutcomes',[]),(i,entry,ref)
            assert best==want['best'],(i,best,want['best'])
        seed_cases = [
            {'best': {}, 'functions': [{'name': 'existing', 'div': 3}]},
            {'best': {'existing': 2}, 'functions': [{'name': 'existing', 'div': 3}]},
            {'best': {'existing': 5}, 'functions': [{'name': 'existing', 'div': 3}]},
            {'best': {'existing': 5}, 'functions': [{'name': 'existing', 'matched': True, 'div': 9}]},
            {'best': {}, 'functions': [{'name': '', 'div': 2}, {'name': 'no-value'}, {'name': 'matched', 'matched': True}]},
            {'best': {'existing': 5}, 'functions': [{'name': 'existing', 'div': 3}, {'name': 'existing', 'div': 7}]},
        ]
        seed_cases += [{'best': {}, 'functions': [{'name': 'boundary', 'div': div}]}
                       for div in [-1, 0, 0.5, 1, 1.5, 998, 999, 1000, '3', None]]
        fixture.write_text(json.dumps(seed_cases))
        runner.write_text("import fs from 'node:fs'; import {aiStats} from "+json.dumps(stats.as_uri())+
                          ";const cases=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));const outputs=cases.map(c=>{aiStats.swapTo({},c.best);aiStats.seedBestDiv(c.functions);return aiStats.serializeBestDiv();});process.stdout.write(JSON.stringify(outputs));",encoding='utf-8')
        run=subprocess.run(['node','--disable-warning=MODULE_TYPELESS_PACKAGE_JSON',str(runner),str(fixture)],capture_output=True,text=True,encoding='utf-8',timeout=20)
        assert run.returncode == 0, run.stderr
        for case,want in zip(seed_cases,json.loads(run.stdout)):
            request.write_text(json.dumps({'method':'policy.statistics','arguments':{
                'best':case['best'],'atlasFunctions':case['functions'],'rows':[]}}))
            run=subprocess.run([str(exe),'--backend',str(repo),str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=20)
            assert run.returncode == 0, run.stderr
            actual=json.loads(response.read_text(encoding='utf-8-sig'))
            assert actual['best'] == want, (case,actual['best'],want)
    print(f'PASS {len(rows)} incremental statistics and {len(seed_cases)} Atlas baseline comparisons against original Console')

if __name__=='__main__':main()
