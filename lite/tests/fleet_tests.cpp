#include "fleet.h"
#include "mcp.h"
#include "atlas_layout.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace lite;
int assertions = 0;
void expect(bool ok, const std::string &label) {
  if (!ok)
    throw std::runtime_error(label);
  ++assertions;
}
template <class F> void reject(F f, const std::string &label) {
  bool threw = false;
  try {
    f();
  } catch (const std::exception &) {
    threw = true;
  }
  expect(threw, label);
}
static void wait(Fleet &fleet) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (fleet.running()) {
    if (std::chrono::steady_clock::now() > end)
      throw std::runtime_error("Fleet timed out");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
}
int main(int argc, char **argv) {
  if (argc == 2) {
    auto root = fs::u8path(argv[1]);
    Fleet restored(root / "repository", root / "data/projects/fixture",
                   loadDescriptor(root / "repository"), Settings{});
    std::cout << restored.snapshot().size() << " restored agents\n";
    return 0;
  }
  auto dir = fs::temp_directory_path() / fs::u8path("TangOS fleet " + uniqueId());
  Runner setup, server;
  std::thread serverThread;
  try {
    fs::create_directories(dir);
    auto repo = dir / "repository";
    fs::create_directories(repo / "tools");
    fs::create_directories(repo / "port");
    fs::create_directories(repo / "src");
    auto git = [&](Args args) {
      Args all = {"git", "-c", "core.autocrlf=false"};
      all.insert(all.end(), args.begin(), args.end());
      auto result = setup.run({all, repo});
      if (result.code)
        throw std::runtime_error(result.output);
    };
    git({"init", "-b", "main"});
    git({"config", "user.name", "Fleet fixture"});
    git({"config", "user.email", "fleet@example.invalid"});
    write(repo / "AGENTS.md",
          "ROOT_RULE: only change port files; no src changes or copyrighted assets.\n");
    write(repo / "port/AGENTS.md", "NESTED_RULE: run independent port reference check.\n");
    write(repo / "src/original.cpp", "int original = 1;\n");
    write(repo / "tools/port_refcheck.py",
          "from pathlib import Path\nassert Path('src/original.cpp').read_text() == 'int original "
          "= 1;\\n'\nprint('independent verification passed', flush=True)\n");
    write(
        repo / "tools/schedule.py",
        "import "
        "argparse,json\np=argparse.ArgumentParser();p.add_argument('--out');p.add_argument('--"
        "limit',type=int);a=p.parse_args()\nwith open(a.out,'w') as f:\n for n in range(a.limit): "
        "f.write(json.dumps({'id':str(n),'name':'target'+str(n),'module':'port'})+'\\n')\n");
    write(repo / "tools/driver.py", R"PY(import argparse,json,os,time,urllib.request,pathlib,sys
INSTRUCTIONS = 'base driver rules'
def main():
 p=argparse.ArgumentParser();p.add_argument('--wl');p.add_argument('--out');p.add_argument('--prompt');p.add_argument('--jobs');p.add_argument('--attempts');a=p.parse_args()
 instructions=pathlib.Path(a.prompt).read_text(encoding='utf-8')
 assert 'ROOT_RULE' in instructions and 'NESTED_RULE' in instructions and 'never modify src/' in instructions
 if os.environ['GLM_MODEL']=='exhausted':print('402 payment required',flush=True);return
 if os.environ['GLM_MODEL']=='empty':pathlib.Path(a.out).write_text('{}');return
 if os.environ['GLM_MODEL']=='split-code':sys.stdout.write('402');sys.stdout.flush();time.sleep(.05);print('0');pathlib.Path(a.out).write_text('{}');return
 targets=[json.loads(s) for s in pathlib.Path(a.wl).read_text().splitlines()]
 if os.environ['GLM_MODEL']=='untouched':pathlib.Path(a.out).write_text(json.dumps({'results':[]}));return
 if os.environ['GLM_MODEL']=='partial':targets=targets[:1]
 if targets and targets[0]['name']=='one':assert 'CUSTOM_BATCH_RULE' in instructions
 body=json.dumps({'model':os.environ['GLM_MODEL'],'messages':[{'role':'user','content':instructions}]}).encode()
 request=urllib.request.Request(os.environ['GLM_BASE_URL']+'/chat/completions',body,{'Authorization':'Bearer '+os.environ['GLM_API_KEY'],'Content-Type':'application/json'})
 with urllib.request.urlopen(request) as response: assert json.load(response)['choices'][0]['message']['content']=='verified fixture response'
 print('private-key='+os.environ['GLM_API_KEY'],flush=True)
 time.sleep(0.6)
 if os.environ['GLM_MODEL']=='bad':pathlib.Path('src/original.cpp').write_text('bad source edit')
 else:
  for row in targets:pathlib.Path('port/'+row['name']+'.txt').write_text('reviewed native fleet output')
 pathlib.Path(a.out).write_text(json.dumps({'results':[{'name':r['name'],'matched':False} for r in targets]} if os.environ['GLM_MODEL']=='partial' else {'worked':len(targets)}))
 print('driver finished',flush=True)
if __name__=='__main__':main()
)PY");
    Json descriptor = {
        {"tangosVersion", "1"},
        {"project", {{"name", "fixture"}, {"title", "Portable fleet fixture"}}},
        {"runtime", {{"python", "python"}, {"envKeys", Json::array({"TEST_API_KEY"})}}},
        {"console", {{"scheduler", "schedule"}, {"driver", "drive"}, {"land", "land"}}},
        {"tools",
         Json::array(
             {{{"id", "schedule"},
               {"readOnly", false},
               {"command", "{python} tools/schedule.py --out {out} --limit {limit}"}},
              {{"id", "drive"},
               {"readOnly", false},
               {"command", "{python} tools/driver.py --wl {wl} --out {out} --prompt {prompt} "
                           "--jobs {jobs} --attempts {attempts}"}},
              {{"id", "land"}, {"readOnly", false}, {"command", "{python} tools/land.py"}},
              {{"id", "echo"},
               {"readOnly", true},
               {"command", "{python} -c {value}"},
               {"args",
                Json::array({{{"name", "value"}, {"type", "string"}, {"required", true}}})}}})}};
    write(repo / "tools/land.py",
          "from pathlib import Path\nPath('port/landed.txt').write_text('landed fixture')\n");
    write(repo / "tangos.json", descriptor.dump(2));
    git({"add", "."});
    git({"commit", "-m", "Disposable fleet fixture"});
    write(repo / "AGENTS.md",
          read(repo / "AGENTS.md") + "LOCAL_ROOT_RULE: preserve local instructions.\n");
    fs::create_directories(repo / "port/local");
    write(repo / "port/local/AGENTS.md", "UNTRACKED_NESTED_RULE: coordinate local work.\n");
    auto layout = squarify({{0, 80}, {1, 120}, {2, 60}}, 0, 0, 800, 600);
    double area = 0;
    for (auto &tile : layout) {
      area += tile.width * tile.height;
      expect(tile.x >= 0 && tile.y >= 0 && tile.x + tile.width <= 800.001 &&
                 tile.y + tile.height <= 600.001,
             "atlas tile bounds");
    }
    expect(layout.size() == 3 && std::abs(area - 800. * 600.) < .1,
           "Console squarify conserves area");
    auto desc = loadDescriptor(repo);
    expect(desc.title == "Portable fleet fixture", "descriptor project detection");
    auto c = toolCommand(desc, desc.tool("echo"), {{"value", "print('a b')"}}, repo, false);
    expect(c.argv.back() == "print('a b')", "placeholder is single argv");
    reject([&] { toolCommand(desc, desc.tool("drive"), Json::object(), repo, false); },
           "write gate");
    reject([&] { confinedPath(repo, "../escape"); }, "descriptor cwd confinement");
    reject([&] { confinedPath(repo, std::string("file\0suffix", 11)); },
           "embedded NUL path denied");
    reject([&] { confinedPath(repo, "AGENTS.md:hidden"); }, "alternate data stream denied");
    reject([&] { confinedPath(repo, R"(\\?\C:\fixture)"); }, "device path denied");
    auto invalid = descriptor;
    invalid["tools"].push_back(invalid["tools"][0]);
    reject([&] { parseDescriptor(invalid.dump()); }, "duplicate tool IDs");
    expect(
        parseAtlas(
            R"({"functions":[{"id":"x","name":"f","module":"port","size":80,"matched":true}]})")[0]
                .state == "matched",
        "atlas state");
    auto data = dir / "data";
    Vault vault(data / "vault");
    vault.set("TEST_API_KEY", "fixture-secret-123456");
    expect(vault.values().at("TEST_API_KEY") == "fixture-secret-123456", "DPAPI roundtrip");
    expect(read(data / "vault/TEST_API_KEY.dpapi").find("fixture-secret-123456") ==
               std::string::npos,
           "DPAPI at rest");
    write(dir / "server.py", R"PY(import http.server,json,pathlib,sys
class Handler(http.server.BaseHTTPRequestHandler):
 def do_POST(self):
  n=int(self.headers['Content-Length']);body=json.loads(self.rfile.read(n))
  assert self.headers['Authorization']=='Bearer fixture-secret-123456'
  assert 'ROOT_RULE' in body['messages'][0]['content']
  payload=json.dumps({'choices':[{'message':{'content':'verified fixture response'}}]}).encode()
  self.send_response(200);self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
 def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
pathlib.Path(sys.argv[1]).write_text(str(server.server_port));server.serve_forever()
)PY");
    serverThread = std::thread([&] {
      server.run(
          {{"python", utf8((dir / "server.py").wstring()), utf8((dir / "port.txt").wstring())},
           dir},
          [](const std::string &) {});
    });
    for (int n = 0; !fs::exists(dir / "port.txt") && n < 150; n++)
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    expect(fs::exists(dir / "port.txt"), "local API fixture started");
    Settings settings;
    {
      Fleet fleet(repo, data / "projects/fixture", desc, settings);
      reject([&] { Fleet second(repo, data / "projects/fixture", desc, settings); },
             "cross-instance ownership");
      AgentSpec a;
      a.name = "API A";
      a.key = "TEST_API_KEY";
      a.model = "fixture";
      a.baseUrl = "http://127.0.0.1:" + trim(read(dir / "port.txt"));
      a.count = 1;
      auto first = fleet.add(a);
      a.name = "API B";
      auto second = fleet.add(a);
      fleet.saveDraft({{"title", "Global fixture"},
                       {"prompt", "Preserve handoff prompt"},
                       {"items", Json::array({{{"id", "global-only"}, {"name", "global_only"}}})}});
      fleet.enqueueDraft("");
      auto global = fleet.batches().back().at("id").get<std::string>();
      expect(fleet.batches().back().at("agentId") == "", "unassigned global batch persists");
      reject([&] { fleet.enqueue(first, Json::array({{{"id", "global-only"}}})); },
             "global reservation prevents duplicate assignment");
      fleet.handoff(global, first);
      expect(fleet.batches().back().at("prompt") == "Preserve handoff prompt",
             "handoff preserves instructions/history");
      fleet.handoff(global, second);
      fleet.handoff(global, "");
      expect(fleet.batches().back().at("agentId") == "", "batch can return to the global queue");
      fleet.editBatch(global, 0, true);
      Runner generation;
      auto generated = fleet.generateDraft("Hard matcher", 3, generation);
      expect(generated.at("items").size() == 3 && generated.at("title") == "Hard matcher draft",
             "declared scheduler generates an isolated reviewable draft");
      generation.cancel();
      reject([&] { fleet.generateDraft("Hard matcher", 3, generation); },
             "generation can be cancelled before execution");
      expect(fleet.draft() == generated, "cancelled generation preserves saved draft");
      generation.reset();
      fleet.saveDraft({{"items", Json::array()}});
      for (auto client : {"Claude Code", "Claude Desktop", "Cursor", "VS Code", "Generic"}) {
        auto config = mcpClientConfiguration(client, dir / "TangOSLite.exe", data / "mcp.json",
                                             "External fixture");
        auto servers = config.at(std::string(client) == "VS Code" ? "servers" : "mcpServers");
        expect(servers.at("tangos-lite").at("args")[0] == "--mcp-stdio" &&
                   config.dump().find("local-key") == std::string::npos,
               "native MCP client template references a local connection file without embedded "
               "secrets");
      }
      auto duplicate = a;
      duplicate.name = "  api a  ";
      reject([&] { fleet.add(duplicate); },
             "duplicate agent names cannot route clients ambiguously");
      reject([&] { fleet.configure(second, duplicate); }, "duplicate rename refused");
      duplicate.name = "   ";
      reject([&] { fleet.add(duplicate); }, "blank trimmed agent name refused");
      fleet.enqueue(
          first,
          Json::array({{{"id", "one"}, {"name", "one"}, {"module", "port"}, {"claim", nullptr}}}),
          "Custom fixture batch", "CUSTOM_BATCH_RULE: obey repository rules and verify results");
      reject(
          [&] {
            fleet.enqueue(second,
                          Json::array({{{"id", "one"}, {"name", "one"}, {"module", "port"}}}));
          },
          "duplicate target claim");
      fleet.enqueue(second, Json::array({{{"id", "two"}, {"name", "two"}, {"module", "port"}}}));
      fleet.start(first);
      reject([&] { fleet.editBatch(fleet.batches()[0].at("id").get<std::string>(), 1); },
             "active batch edits rejected without stopping work");
      fleet.start(second);
      fleet.enqueue(first, Json::array({{{"id", "later"}, {"name", "later"}, {"module", "port"}}}));
      wait(fleet);
      for (auto &state : fleet.snapshot())
        if (state.id == first)
          expect(state.completed == 2 && state.queue.empty() &&
                     fs::exists(state.worktree / "port/later.txt"),
                 "one-shot agent drains targets queued during execution");
      fleet.enqueue(first, Json::array({{{"id", "later"}, {"name", "later"}, {"module", "port"}}}));
      fleet.enqueue(first, Json::array({{{"id", "last"}, {"name", "last"}, {"module", "port"}}}));
      fleet.editQueue(first, 1, -1);
      for (auto &state : fleet.snapshot())
        if (state.id == first)
          expect(state.queue[0]["id"] == "last", "queue reordered persistently");
      fleet.editQueue(first, 0, 0, true);
      fleet.clear(first);
      auto history = fleet.batches();
      expect(std::any_of(history.begin(), history.end(),
                         [](const Json &b) { return b.at("status") == "done"; }),
             "completed fleet batches retained in history");
      fleet.saveDraft({{"title", "Draft fixture"},
                       {"prompt", "Follow NESTED_RULE"},
                       {"items", Json::array({{{"id", "draft-target"},
                                               {"name", "draft_target"},
                                               {"module", "port"}}})}});
      fleet.enqueueDraft(first);
      expect(fleet.draft().at("items").empty() &&
                 fleet.batches().back().at("title") == "Draft fixture",
             "draft consumed only after successful queue assignment");
      auto draftBatch = fleet.batches().back().at("id").get<std::string>();
      fleet.editBatch(draftBatch, -1);
      fleet.editBatch(draftBatch, 1, true);
      for (auto &state : fleet.snapshot())
        if (state.id == first)
          expect(state.queue.empty(), "removing queued batch updates execution queue");
      fleet.saveDraft({{"title", "Persisted fixture draft"},
                       {"prompt", "Follow repository rules"},
                       {"items", Json::array()}});
      auto states = fleet.snapshot();
      for (auto &state : states) {
        expect(state.phase == "review",
               state.spec.name + " completed and verified: " + state.detail);
        expect(fs::exists(state.worktree / "port" /
                          (state.spec.name == "API A" ? "one.txt" : "two.txt")),
               "isolated output exists");
        expect(read(state.log).find("fixture-secret-123456") == std::string::npos,
               "provider key not logged");
        expect(read(state.prompt).find("Role: Hard matcher") != std::string::npos,
               "resolved automatic role reaches driver instructions");
        if (state.id == first) {
          bool retained = false;
          for (auto &file : fs::directory_iterator(state.prompt.parent_path()))
            if (file.is_regular_file() &&
                file.path().filename().string().find("-instructions.txt") != std::string::npos &&
                read(file.path()).find("CUSTOM_BATCH_RULE") != std::string::npos)
              retained = true;
          expect(retained, "complete per-run batch instructions preserved after queue draining");
        }
        expect(read(state.prompt).find("NESTED_RULE") != std::string::npos,
               "scoped instructions delivered");
        expect(read(state.prompt).find("LOCAL_ROOT_RULE") != std::string::npos &&
                   read(state.prompt).find("UNTRACKED_NESTED_RULE") != std::string::npos,
               "modified and untracked checkout instructions reach isolated agents");
      }
      expect(!fs::exists(repo / "port/one.txt") && !fs::exists(repo / "port/two.txt"),
             "main checkout untouched by fleet");
      reject([&] { fleet.land(first); }, "port-only landing refused");
      auto decompSettings = settings;
      decompSettings.portOnly = false;
      fleet.setPolicy(decompSettings);
      fleet.land(first);
      wait(fleet);
      for (auto &state : fleet.snapshot())
        if (state.id == first)
          expect(state.phase == "review" && fs::exists(state.worktree / "port/landed.txt"),
                 "explicit isolated landing and verification");
      fleet.setPolicy(settings);
      auto diff = fleet.review(first);
      expect(diff.find("one.txt") != diff.npos, "complete agent diff review");
      Runner review;
      Repository r(review, states[0].id == first ? states[0].worktree : states[1].worktree,
                   settings);
      auto tree = r.safetyIndex();
      fleet.commitReviewed(first, "Reviewed fixture", tree);
      expect(fleet.snapshot()[0].phase == "committed" || fleet.snapshot()[1].phase == "committed",
             "reviewed isolated commit");
      a.name = "Partial ledger fixture";
      a.model = "partial";
      a.count = 2;
      auto partial = fleet.add(a);
      fleet.enqueue(partial, Json::array({{{"id", "partial1"}, {"name", "partial_one"}},
                                          {{"id", "partial2"}, {"name", "partial_two"}},
                                          {{"id", "partial3"}, {"name", "partial_three"}}}));
      fleet.start(partial);
      wait(fleet);
      for (auto &state : fleet.snapshot())
        if (state.id == partial)
          expect(state.completed == 3 && state.queue.empty() &&
                     fs::exists(state.worktree / "port/partial_two.txt") &&
                     fs::exists(state.worktree / "port/partial_three.txt"),
                 "authoritative partial ledger retains and eventually executes untouched targets");
      a.name = "Untouched ledger fixture";
      a.model = "untouched";
      auto untouched = fleet.add(a);
      fleet.enqueue(untouched, Json::array({{{"id", "untouched"}, {"name", "untouched"}}}));
      fleet.start(untouched);
      wait(fleet);
      for (auto &state : fleet.snapshot())
        if (state.id == untouched)
          expect(state.completed == 0 && state.queue.size() == 1 && state.phase == "partial",
                 "empty authoritative results preserve pending work and do not spin forever");
      a.count = 1;
      a.name = "Bad agent";
      a.model = "bad";
      auto bad = fleet.add(a);
      fleet.enqueue(bad, Json::array({{{"id", "bad"}, {"name", "bad"}}}));
      fleet.start(bad);
      wait(fleet);
      auto snapshot = fleet.snapshot();
      for (auto &state : snapshot)
        if (state.id == bad)
          expect(state.phase == "failed" && state.detail.find("src/") != state.detail.npos,
                 "src change refused");
      reject([&] { fleet.review(bad); }, "src never committed");
      expect(read(repo / "src/original.cpp") == "int original = 1;\n", "main src unchanged");
      a.name = "External fixture";
      a.kind = "mcp";
      a.model = "";
      auto external = fleet.add(a);
      fleet.enqueue(external, Json::array({{{"id", "external"}, {"name", "external"}}}));
      fleet.start(external);
      wait(fleet);
      fleet.enqueue(external,
                    Json::array({{{"id", "external-later"}, {"name", "external_later"}}}));
      auto batch = fleet.takeBatch(external);
      expect(batch["targets"].size() == 1 && batch["targets"][0]["id"] == "external",
             "MCP receives only prepared targets");
      expect(batch["status"] == "assigned", "MCP batch delivered");
      expect(fleet.takeBatch(external)["status"] == "empty", "MCP batch not assigned twice");
      fleet.finishBatch(external);
      for (auto &state : fleet.snapshot())
        if (state.id == external)
          expect(state.queue.size() == 1 && state.queue[0]["id"] == "external-later",
                 "MCP finish retains later queue additions");
      fleet.clear(external);
      McpServer mcp(fleet, desc, data / "mcp.json");
      expect(mcp.port() != 0 && mcp.configuration().find("127.0.0.1") != std::string::npos,
             "authenticated loopback MCP config");
      fleet.enqueue(external, Json::array({{{"id", "external-http"}, {"name", "external_http"}}}));
      fleet.start(external);
      wait(fleet);
      write(dir / "mcp_client.py", R"PY(import json,pathlib,sys,urllib.request,urllib.error
config=json.loads(pathlib.Path(sys.argv[1]).read_text())['mcpServers']['tangos-lite']
url=config['url'];headers=config['headers'].copy()
def call(method,params=None,auth=True):
 h=headers.copy() if auth else {};h['Content-Type']='application/json'
 body=json.dumps({'jsonrpc':'2.0','id':1,'method':method,'params':params or {}}).encode()
 with urllib.request.urlopen(urllib.request.Request(url,body,h)) as response:
  session=response.headers.get('Mcp-Session-Id')
  if session:headers['Mcp-Session-Id']=session
  result=json.load(response);assert 'error' not in result,result
  return result['result']
try:call('ping',auth=False);raise AssertionError('unauthenticated request accepted')
except urllib.error.HTTPError as error:assert error.code==403
call('initialize',{'protocolVersion':'2025-03-26','clientInfo':{'name':'External fixture','version':'1'},'capabilities':{}})
assert any(t['name']=='next_batch' for t in call('tools/list')['tools'])
batch=call('tools/call',{'name':'next_batch','arguments':{}})
assert json.loads(batch['content'][0]['text'])['status']=='assigned'
output=call('tools/call',{'name':'echo','arguments':{'value':"print('native-mcp-output')"}})
assert 'native-mcp-output' in output['content'][0]['text']
call('tools/call',{'name':'finish_batch','arguments':{}})
for _ in range(150):call('ping')
with urllib.request.urlopen(urllib.request.Request(url,headers=headers,method='DELETE')) as response:assert response.status==200
try:call('ping');raise AssertionError('terminated session remained usable')
except urllib.error.HTTPError as error:assert error.code==404
print('authenticated MCP protocol, tools, batch lifecycle and long polling passed')
)PY");
      auto rpc = setup.run(
          {{"python", utf8((dir / "mcp_client.py").wstring()), utf8((data / "mcp.json").wstring())},
           dir});
      expect(rpc.code == 0, "MCP HTTP integration: " + rpc.output);
      write(dir / "stdio_client.py", R"PY(import subprocess,json,sys
requests=[{'jsonrpc':'2.0','id':1,'method':'initialize','params':{'protocolVersion':'2025-03-26','clientInfo':{'name':'Actual client name','version':'1'},'capabilities':{}}},
 {'jsonrpc':'2.0','method':'notifications/initialized'}, {'jsonrpc':'2.0','id':2,'method':'tools/list'}, {'jsonrpc':'2.0','id':3,'method':'ping'}]
p=subprocess.run([sys.argv[1],'--mcp-stdio',sys.argv[2],'External fixture'],input=''.join(json.dumps(r)+'\n' for r in requests),text=True,capture_output=True,timeout=20)
assert p.returncode==0,(p.returncode,p.stderr)
responses=[json.loads(line) for line in p.stdout.splitlines()]
assert len(responses)==3,responses
assert all('error' not in r for r in responses),responses
assert any(t['name']=='next_batch' for t in responses[1]['result']['tools'])
print('native stdio MCP initialize, notification, session, tools, ping and EOF passed')
)PY");
      auto bridge = setup.run(
          {{"python", utf8((dir / "stdio_client.py").wstring()),
            utf8((fs::u8path(selfExecutable()).parent_path() / "TangOSLite.exe").wstring()),
            utf8((data / "mcp.json").wstring())},
           dir});
      expect(bridge.code == 0, "MCP stdio integration: " + bridge.output);
      auto serverState = mcp.state();
      expect(serverState["connectedClients"] == 0, "MCP DELETE disconnects client");
      expect(serverState["requestsSeen"].get<int>() >= 155 &&
                 serverState["lastContactAt"].get<int64_t>() > 0,
             "MCP traffic telemetry counts requests without secrets");
      a.name = "Stopped fixture";
      a.kind = "cli";
      a.cli = "python -c \"import time; print('running',flush=True); time.sleep(20)\"";
      auto stopped = fleet.add(a);
      fleet.enqueue(stopped, Json::array({{{"id", "stop"}, {"name", "stop"}}}));
      fleet.start(stopped);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      {
        McpServer temporary(fleet, desc, data / "temporary-mcp.json");
      }
      expect(fleet.running(), "Stopping MCP preserves unrelated CLI jobs");
      fleet.stop(stopped);
      wait(fleet);
      for (auto &state : fleet.snapshot())
        if (state.id == stopped)
          expect(state.phase == "cancelled", "per-agent cancellation");
    }
    {
      Fleet restored(repo, data / "projects/fixture", desc, settings);
      expect(restored.draft().at("title") == "Persisted fixture draft",
             "project draft persists through fleet restart");
      expect(restored.snapshot().size() == 7, "persistent fleet restoration: loaded " +
                                                  std::to_string(restored.snapshot().size()) +
                                                  " agents");
      bool pendingRecovered = false;
      for (auto &state : restored.snapshot())
        if (state.spec.name == "Untouched ledger fixture")
          pendingRecovered =
              state.completed == 0 && state.queue.size() == 1 && state.phase == "partial";
      expect(pendingRecovered, "unattempted targets and partial phase survive restart");
    }
    {
      auto file = data / "projects/fixture/fleet.json";
      auto original = read(file);
      auto legacy = Json::parse(original);
      legacy.erase("batchBook");
      write(file, legacy.dump(2));
      {
        Fleet migrated(repo, data / "projects/fixture", desc, settings);
        expect(!migrated.batches().empty() &&
                   migrated.batches()[0].at("title") == "Recovered queue",
               "legacy target queues migrate without losing unfinished work");
      }
      write(file, original);
    }
    {
      Fleet guarded(repo, data / "projects/usage-guard", desc, settings);
      AgentSpec api;
      api.name = "Usage fixture";
      api.key = "TEST_API_KEY";
      api.model = "exhausted";
      api.baseUrl = "http://127.0.0.1:" + trim(read(dir / "port.txt"));
      api.count = 1;
      api.loop = true;
      auto id = guarded.add(api);
      guarded.enqueue(id, Json::array({{{"id", "preserved"}, {"name", "preserved"}}}));
      guarded.start(id);
      wait(guarded);
      auto stopped = guarded.snapshot()[0];
      expect(stopped.phase == "exhausted" && !stopped.spec.loop && stopped.completed == 0 &&
                 stopped.detail.find("API reported") != std::string::npos &&
                 stopped.queue.size() == 1 &&
                 read(stopped.log).find("402 payment required") != std::string::npos,
             "explicit usage exhaustion stops only this agent and preserves pending work/log");
      api.model = "empty";
      guarded.configure(id, api);
      guarded.start(id);
      wait(guarded);
      stopped = guarded.snapshot()[0];
      expect(stopped.phase == "exhausted" && !stopped.spec.loop && stopped.completed == 4 &&
                 stopped.queue.size() == 1 &&
                 stopped.detail.find("5 fast empty") != std::string::npos,
             "five fast empty runs stop refill loop; manual restart clears previous exhaustion");
      api.name = "Split boundary fixture";
      api.model = "split-code";
      api.loop = false;
      auto split = guarded.add(api);
      guarded.enqueue(split, Json::array({{{"id", "split"}, {"name", "split"}}}));
      guarded.start(split);
      wait(guarded);
      for (auto &state : guarded.snapshot())
        if (state.id == split)
          expect(state.phase == "review" && state.completed == 1,
                 "split numeric output 4020 is not an explicit quota signal");
    }
    write(dir / "split.py", "import "
                            "os,time,sys\ns=os.environ['TEST_API_KEY'];sys.stdout.write(s[:8]);sys."
                            "stdout.flush();time.sleep(.1);print(s[8:])\n");
    auto secretLog = dir / "split.log";
    setup.run({{"python", utf8((dir / "split.py").wstring())},
               dir,
               {{"TEST_API_KEY", "fixture-secret-123456"}}},
              {}, secretLog);
    expect(read(secretLog).find("fixture-secret-123456") == std::string::npos &&
               read(secretLog).find("[REDACTED]") != std::string::npos,
           "stream-spanning secret redaction");
    server.cancel();
    serverThread.join();
    std::cout << assertions << " fleet/descriptor/vault assertions passed\n";
    std::cout << "Fixture retained for inspection: " << utf8(dir.wstring()) << "\n";
    return 0;
  } catch (const std::exception &e) {
    server.cancel();
    if (serverThread.joinable())
      serverThread.join();
    std::cerr << "FAIL: " << e.what() << "\nFixture: " << utf8(dir.wstring()) << "\n";
    return 1;
  }
}
