"""Compare driver token normalization against the original result ingestion block."""
import argparse, json, pathlib, subprocess, tempfile

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True)
    exe=pathlib.Path(parser.parse_args().exe).resolve();root=pathlib.Path(__file__).resolve().parents[2]
    original=(root/'console/src/main/index.ts').read_text(encoding='utf-8')
    begin=original.index('      const landedRaw = out.landedNames ?? out.landed ?? out.matches ?? []')
    end=original.index('      if (tin || tout) aiStats.recordTokens(agentName, tin, tout)',begin)
    end += len('      if (tin || tout) aiStats.recordTokens(agentName, tin, tout)')
    block=original[begin:end]
    bases=[{}, {'results':[{'name':'real','matched':True}]}, {'landedNames':['real']},
           {'landedNames':['real','asm'], 'sources':{'asm':'dcd 0x12345678'}},
           {'landedNames':None,'landed':[{'name':'real'}, {'name':'other'}]},
           {'landedNames':[],'landed':['ignored']}]
    variants=[{}, {'inputTokens':47,'outputTokens':12}, {'tokensIn':4,'inputTokens':47,'tokensOut':3,'outputTokens':12},
              {'tokensIn':None,'inputTokens':47,'tokensOut':None,'outputTokens':12},
              {'tokensPerLanded':9}, {'tokensOut':None,'outputTokens':None,'tokensPerLanded':9},
              {'tokensOut':0,'tokensPerLanded':9}, {'tokensIn':0,'inputTokens':47,'outputTokens':0}]
    cases=[dict(base,**variant) for base in bases for variant in variants]
    with tempfile.TemporaryDirectory(prefix='tangos-driver-token-reference-') as folder:
        tmp=pathlib.Path(folder);fixture=tmp/'cases.json';fixture.write_text(json.dumps(cases),encoding='utf-8')
        runner=tmp/'reference.ts'
        runner.write_text("import fs from 'node:fs';import {classifySource} from "+json.dumps((root/'console/src/main/asmPolicy.ts').as_uri())+";function summary(out:any){let landed:string[]=[],nearMissNames:string[]=[],transcribedRejected:string[]=[],tin=0,tout=0;const agentName='fixture',outPath='local';const recorded=new Set<string>();const batch={items:[] as any[]};const aiStats={recordMatch(...args:any[]){},recordTokens(...args:any[]){}};function rejectTranscription(...args:any[]){}\n"+block+"\nreturn {tokensIn:tin,tokensOut:tout};}process.stdout.write(JSON.stringify(JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(summary)));",encoding='utf-8')
        run=subprocess.run(['node','--disable-warning=MODULE_TYPELESS_PACKAGE_JSON',str(runner),str(fixture)],capture_output=True,text=True,encoding='utf-8',timeout=20)
        assert run.returncode==0,run.stderr
        for case,want in zip(cases,json.loads(run.stdout)):
            req=tmp/'request.json';res=tmp/'response.json';req.write_text(json.dumps({'method':'policy.driverTokens','arguments':case}),encoding='utf-8')
            native=subprocess.run([str(exe),'--backend','-',str(tmp/'data'),str(req),str(res)],capture_output=True,timeout=20)
            assert native.returncode==0,native.stderr
            actual=json.loads(res.read_text(encoding='utf-8-sig'));assert actual==want,(case,actual,want)
    print(f'PASS {len(cases)} original driver token alias/null/fallback/transcription cases')
if __name__=='__main__':main()
