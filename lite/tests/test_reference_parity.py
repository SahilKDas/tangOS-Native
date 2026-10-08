"""Compare packaged native policies with the original Console, using local fixtures only.
Node is a development reference runner; it is never shipped with TangOS Lite.
"""
import argparse
import hashlib
import itertools
import json
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    args = parser.parse_args()
    exe = pathlib.Path(args.exe).resolve()
    root = pathlib.Path(__file__).resolve().parents[2]
    count = 0
    with tempfile.TemporaryDirectory(prefix="tangos-reference-") as folder:
        tmp = pathlib.Path(folder)
        repo = tmp / "repo"
        repo.mkdir()
        (repo / "tangos.json").write_text(json.dumps({"tangosVersion":"1", "project":{"name":"reference","title":"Reference fixture"}, "tools":[]}), encoding="utf-8")
        request = tmp / "request.json"
        response = tmp / "response.json"
        def native(method, arguments):
            request.write_text(json.dumps({"method":method,"arguments":arguments}), encoding="utf-8")
            run = subprocess.run([str(exe),"--backend",str(repo),str(tmp / "data"),str(request),str(response)],capture_output=True,timeout=20)
            result = json.loads(response.read_text(encoding="utf-8-sig"))
            if run.returncode:
                raise AssertionError(f"{method}: {result}")
            return result
        adaptive = tmp / "adaptive.ts"
        original = root / "console/src/main/adaptiveRole.ts"
        adaptive.write_text(original.read_text(encoding="utf-8").replace("'../shared/types'", repr((root / "console/src/shared/types.ts").as_uri())),encoding="utf-8")
        sources = ["", "int f() { return 0; }", "dcd 0x1", "dcd 0xabcdef", "dcd 0X1", "DCD 0x1", "abcdcd 0x1", "dcd0x1", "dcd \n 0xF", "// dcd 0x1", "dcd 0xZZ", "dcd 0x0\n"+"x"*10000+"NONMATCHING", "dcd 0x0\nHAND-ASM PRIMITIVE"]
        roles = [{"role": role, "attempts":attempts, "matches":matches,"pool":{"score":score,"refinerSupply":supply}}
                 for role,attempts,matches,score,supply in itertools.product(["Hard matcher","Random","Drafter","Refiner","Custom"],[0,7,8,16],[0,1,4],[1,3,5],[0,2]) if matches <= attempts]
        pools = [[],[{"id":"a","name":"a","size":16,"matched":False}],
                 [{"id":"b","name":"b","size":2048,"matched":False}],
                 [{"id":"c","name":"c","size":2048,"matched":False,"div":2}],
                 [{"id":"d","name":"d","size":1,"matched":False,"noMatch":{"bucket":"asm-primitive","reason":"declared exemption"}}],
                 [{"id":"a","name":"a","size":512,"matched":False},{"id":"b","name":"b","size":513,"matched":False}]]
        (repo / "config").mkdir()
        attempts = [{"name":"fixture","module":"arm9","addr":33554432,"attemptId":"a","parentAttemptId":None,"matchProvenance":{"model":"local-model"},"c_source":"private C body","loggedAt":"private timestamp"},
                    {"functionId":"arm9:0x2000000","attemptId":"b","parentAttemptId":"a","divergences":"5","status":"near_miss","base":{"kind":"clean"}}]
        (repo / "config/match_attempts.jsonl").write_text("\n".join(map(json.dumps,attempts))+"\ninvalid row\n",encoding="utf-8")
        (repo / "nearmiss").mkdir()
        (repo / "nearmiss/db.jsonl").write_text(json.dumps({"name":"fixture","module":"arm9","addr":33554432,"divergences":5,"c_source":"private tip body","source":"local-model"})+"\n",encoding="utf-8")
        query = {"module":"arm9","addr":33554432,"name":"fixture"}
        fixture = tmp / "fixture.json"
        fixture.write_text(json.dumps({"sources":sources,"roles":roles,"pools":pools,"repo":str(repo),"query":query}),encoding="utf-8")
        reference = tmp / "reference.mjs"
        reference.write_text("import fs from 'node:fs';\n"+
            "import {classifySource} from "+json.dumps((root / "console/src/main/asmPolicy.ts").as_uri())+";\n"+
            "import {poolDifficulty,demotionFor,effectiveRole} from "+json.dumps(adaptive.as_uri())+";\n"+
            "import {readFunctionHistory} from "+json.dumps((root / "console/src/main/attemptHistory.ts").as_uri())+";\n"+
            "const f=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));\n"+
            "process.stdout.write(JSON.stringify({sources:f.sources.map(classifySource),roles:f.roles.map(r=>effectiveRole(demotionFor(r.role,r,r.pool)||r.role,r.pool)),pools:f.pools.map(p=>poolDifficulty(p)),history:readFunctionHistory(f.repo,{},f.query)}));", encoding="utf-8")
        run = subprocess.run(["node","--disable-warning=MODULE_TYPELESS_PACKAGE_JSON",str(reference),str(fixture)],capture_output=True,text=True,encoding="utf-8",timeout=20)
        if run.returncode:
            raise AssertionError(run.stderr)
        expected = json.loads(run.stdout)
        for source,want in zip(sources,expected["sources"]):
            assert native("policy.classify",{"source":source})["classification"] == want, source
            count += 1
        for role,want in zip(roles,expected["roles"]):
            assert native("policy.adaptive",role)["role"] == want, role
            count += 1
        for functions,want in zip(pools,expected["pools"]):
            assert native("policy.pool",{"functions":functions}) == want, functions
            count += 1
        history = native("atlas.history",query)
        want = expected["history"]
        for field in ("attemptsPath","nearMissPath"):
            history[field] = str(pathlib.Path(history[field]).resolve())
            want[field] = str(pathlib.Path(want[field]).resolve())
        assert history == want, {"native":history,"reference":want}
        count += 1
    print(f"PASS {count} differential reference cases: source classification, adaptive roles, pool difficulty, and normalized attempt history")
    for name in ("asmPolicy.ts","adaptiveRole.ts","attemptHistory.ts"):
        source=root / "console/src/main" / name
        print(f"Reference {name}: SHA256 {hashlib.sha256(source.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
