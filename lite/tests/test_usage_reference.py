"""Compare native agent usage stopping with the actual original driver branch."""
import argparse,json,pathlib,subprocess,tempfile

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True)
    exe=pathlib.Path(parser.parse_args().exe).resolve();root=pathlib.Path(__file__).resolve().parents[2]
    source=(root/'console/src/main/index.ts').read_text(encoding='utf-8')
    start=source.index('    const bankedSomething = landed.length > 0 || nearMissNames.length > 0')
    end=source.index('\n    // Land the run',start)
    branch=source[start:end]
    cases=[dict(output=output,productive=productive,elapsedMs=elapsed,streak=streak)
           for output in ['', '402', '1402', '24020', 'Payment required', 'INSUFFICIENT',
                          'out of quota', 'quota exhausted', 'billing', 'no credit', 'network timeout']
           for productive in [False,True] for elapsed in [19999,20000,45000] for streak in [0,3,4]]
    with tempfile.TemporaryDirectory(prefix='tangos-usage-reference-') as folder:
        tmp=pathlib.Path(folder);fixture=tmp/'cases.json';fixture.write_text(json.dumps(cases),encoding='utf-8')
        script="import fs from 'node:fs';const QUICK_FAIL_MS=20000,QUICK_FAIL_LIMIT=5;process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(c=>{const agentName='fixture',quickFailStreak=new Map([[agentName,c.streak]]),Date={now:()=>c.elapsedMs},runStartedAt=0,landed=c.productive?['match']:[],nearMissNames=[],res={output:c.output};let reason='';const autoStopExhausted=(name,text)=>{quickFailStreak.delete(name);reason=text};\n"+branch+"\nreturn {streak:quickFailStreak.get(agentName)??0,reason,stopped:!!reason};})));"
        runner=tmp/'reference.ts';runner.write_text(script,encoding='utf-8')
        result=subprocess.run(['node',str(runner),str(fixture)],capture_output=True,text=True,encoding='utf-8',timeout=20)
        assert result.returncode==0,result.stderr
        for case,want in zip(cases,json.loads(result.stdout)):
            request=tmp/'request.json';response=tmp/'response.json'
            request.write_text(json.dumps({'method':'policy.usage','arguments':case}),encoding='utf-8')
            run=subprocess.run([str(exe),'--backend','-',str(tmp/'data'),str(request),str(response)],capture_output=True,timeout=20)
            assert run.returncode==0,run.stderr
            actual=json.loads(response.read_text(encoding='utf-8-sig'));assert actual==want,(case,actual,want)
    print(f'PASS {len(cases)} original usage-exhaustion/streak/boundary/reset cases')
if __name__=='__main__':main()
