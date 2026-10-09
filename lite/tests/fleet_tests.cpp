#include "fleet.h"
#include "mcp.h"
#include "atlas_layout.h"
#include "client_setup.h"
#include "activity.h"
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
    write(repo / "tools/queue_hold.py",
          "import argparse,json,time\nfrom pathlib import Path\n"
          "p=argparse.ArgumentParser();p.add_argument('--prompt');p.add_argument('--wl');"
          "p.add_argument('--out');a=p.parse_args()\n"
          "assert 'ROOT_RULE' in Path(a.prompt).read_text(encoding='utf-8')\n"
          "Path('port/queue-started.flag').write_text('controlled fixture')\n"
          "deadline=time.monotonic()+30\n"
          "while not Path('port/queue-release.flag').exists():\n"
          " assert time.monotonic()<deadline, 'fixture release timed out'\n"
          " time.sleep(.02)\n"
          "rows=[json.loads(line) for line in Path(a.wl).read_text().splitlines() if line]\n"
          "Path(a.out).write_text(json.dumps({'results':[dict(r,matched=False) for r in rows]}))\n"
          "print('controlled queue fixture finished',flush=True)\n");
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
 assert os.environ['TANGOS_EFFORT']=='medium', 'unknown model uses original family default'
 assert a.jobs=='1', 'Use agents off must drive serially'
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
    descriptor["tools"].push_back(
        {{"id", "match"},
         {"label", "Verify match"},
         {"readOnly", true},
         {"command", "{python} -c {value}"},
         {"args", Json::array({{{"name", "value"}, {"type", "string"}, {"required", true}}})}});
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
      auto configPath = dir / "client-config.json";
      write(configPath,
            Json{{"theme", "keep"}, {"mcpServers", {{"unrelated", {{"command", "keep.exe"}}}}}}
                .dump());
      auto plan = previewClientSetup("Claude Desktop", fs::u8path(selfExecutable()),
                                     data / "mcp.json", "External fixture", configPath);
      expect(plan.outcome.at("action") == "added" &&
                 !Json::parse(read(configPath))["mcpServers"].contains("tangos-lite"),
             "MCP client preview is side-effect free");
      auto installed = installClientSetup(plan);
      auto merged = Json::parse(read(configPath));
      expect(merged.at("theme") == "keep" && merged.at("mcpServers").contains("unrelated") &&
                 merged.at("mcpServers").at("tangos-lite").at("args")[0] == "--mcp-stdio" &&
                 fs::exists(fs::u8path(installed.at("backup").get<std::string>())),
             "native client install preserves unrelated settings and retains a backup");
      expect(previewClientSetup("Claude Desktop", fs::u8path(selfExecutable()), data / "mcp.json",
                                "External fixture", configPath)
                     .outcome.at("action") == "unchanged",
             "MCP reconnect detects unchanged native setup");
      auto changedPlan = previewClientSetup("Claude Desktop", fs::u8path(selfExecutable()),
                                            data / "mcp.json", "Different agent", configPath);
      expect(changedPlan.outcome.at("action") == "updated", "MCP changed identity previews update");
      write(configPath, "{\"changedElsewhere\":true}");
      reject([&] { installClientSetup(changedPlan); },
             "MCP config change invalidates reviewed install");
      write(configPath, "{ malformed");
      reject(
          [&] {
            previewClientSetup("Claude Desktop", fs::u8path(selfExecutable()), data / "mcp.json",
                               "Agent", configPath);
          },
          "MCP installation never replaces malformed client configuration");
      write(configPath, "{ // retain in "
                        "backup\n\"servers\":{\"other\":{\"command\":\"https://example.invalid/a/"
                        "*literal*/\",},},/* block */\"inputs\":[],}");
      auto vscode = previewClientSetup("VS Code", fs::u8path(selfExecutable()), data / "mcp.json",
                                       "Agent", configPath);
      auto vscodeInstall = installClientSetup(vscode);
      auto vscodeMerged = Json::parse(read(configPath));
      expect(vscodeMerged["servers"]["other"]["command"] ==
                     "https://example.invalid/a/*literal*/" &&
                 vscodeMerged["servers"]["tangos-lite"]["type"] == "stdio" &&
                 read(fs::u8path(vscodeInstall["backup"].get<std::string>()))
                         .find("retain in backup") != std::string::npos,
             "VS Code JSONC comments and trailing commas preserve values and exact backup");
      reject([&] { Fleet second(repo, data / "projects/fixture", desc, settings); },
             "cross-instance ownership");
      AgentSpec a;
      a.name = "API A";
      a.key = "TEST_API_KEY";
      a.model = "fixture";
      a.baseUrl = "http://127.0.0.1:" + trim(read(dir / "port.txt"));
      a.count = 1;
      AgentState roleFixture;
      roleFixture.id = "abc123";
      roleFixture.spec = a;
      roleFixture.spec.roles = {"Drafter", "Refiner"};
      auto multipleRoles = parseAgent(agentJson(roleFixture));
      expect(multipleRoles.spec.roles == Args{"Drafter", "Refiner"} &&
                 multipleRoles.spec.role == "Drafter",
             "multiple assigned roles preserve order and first-role scheduler compatibility");
      auto legacyRole = agentJson(roleFixture);
      legacyRole["spec"].erase("roles");
      expect(parseAgent(legacyRole).spec.roles == Args{"Drafter"},
             "legacy single-role fleet state migrates to ordered assigned roles");
      auto noRoles = agentJson(roleFixture);
      noRoles["spec"]["roles"] = Json::array();
      expect(parseAgent(noRoles).spec.role == "Unassigned",
             "explicit empty roles restore automatic selection");
      auto badRoles = agentJson(roleFixture);
      badRoles["spec"]["roles"] = {"Drafter", "Drafter"};
      reject([&] { parseAgent(badRoles); }, "duplicate roles rejected");
      badRoles["spec"]["roles"] = {"unsupported-role"};
      reject([&] { parseAgent(badRoles); }, "unknown roles rejected");
      auto first = fleet.add(a);
      auto assigned = a;
      assigned.roles = {"Drafter", "Refiner"};
      fleet.configure(first, assigned);
      expect(fleet.snapshot().front().spec.roles == Args{"Drafter", "Refiner"},
             "fleet configuration retains several assigned roles");
      fleet.configure(first, a);
      a.name = "API B";
      auto second = fleet.add(a);
      {
        AgentSpec hold;
        hold.name = "Active queue fixture";
        hold.kind = "cli";
        hold.count = 1;
        hold.cli = "python tools/queue_hold.py --prompt {prompt} --wl {worklist} --out {out}";
        auto id = fleet.add(hold);
        fleet.enqueue(id, Json::array({{{"id", "active-queue-a"}},
                                       {{"id", "waiting-queue-b"}},
                                       {{"id", "waiting-queue-c"}}}));
        fleet.start(id);
        auto state = [&] {
          for (auto &agent : fleet.snapshot())
            if (agent.id == id)
              return agent;
          throw std::runtime_error("Active queue fixture disappeared");
        };
        auto deadline = GetTickCount64() + 15000;
        while ((state().worktree.empty() ||
                !fs::exists(state().worktree / "port/queue-started.flag")) &&
               GetTickCount64() < deadline)
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        expect(state().active && fs::exists(state().worktree / "port/queue-started.flag"),
               "controlled CLI assignment is running");
        auto runningInstructions = read(state().prompt);
        auto nextWork = state().spec;
        nextWork.roles = {"Drafter", "Refiner"};
        nextWork.role = "Drafter";
        nextWork.count = 2;
        nextWork.attempts = 7;
        fleet.configure(id, nextWork);
        expect(state().configurationPending && state().spec.roles == nextWork.roles &&
                   state().spec.count == 2 && state().spec.attempts == 7,
               "running work settings are visible as deferred configuration");
        expect(read(state().prompt) == runningInstructions && state().active,
               "live configuration preserves current instructions and running driver");
        auto unsafeConfiguration = nextWork;
        unsafeConfiguration.cli = "different executable";
        reject([&] { fleet.configure(id, unsafeConfiguration); },
               "running driver replacement remains prohibited");
        auto persistedConfiguration = Json::parse(read(data / "projects/fixture/fleet.json"));
        bool savedNext = false;
        for (auto &saved : persistedConfiguration.at("agents"))
          if (saved.at("id") == id)
            savedNext = saved.at("next_spec").at("roles") == Json(nextWork.roles) &&
                        saved.at("spec").at("count") == 1;
        expect(savedNext, "pending settings persist without overwriting in-flight configuration");
        auto assigned = state().assigned;
        expect(assigned.size() == 1 && assigned[0].at("id") == "active-queue-a",
               "controlled assignment protects its first target");
        reject([&] { fleet.editQueue(id, 0, 0, true); }, "cannot remove an in-flight target");
        reject([&] { fleet.editQueue(id, 1, -1); },
               "waiting target cannot move ahead of current assignment");
        fleet.editQueue(id, 1, 1);
        expect(state().queue[1].at("id") == "waiting-queue-c" && state().assigned == assigned,
               "waiting queue can reorder while current work stays unchanged");
        fleet.editQueue(id, 2, 0, true);
        fleet.clear(id);
        auto retained = state();
        expect(retained.active && retained.assigned == assigned && retained.queue.size() == 1 &&
                   retained.queue[0].at("id") == "active-queue-a",
               "clearing waiting work preserves the running target and runner");
        bool activeHistory = false;
        for (auto &batch : fleet.batches())
          if (batch.at("agentId") == id) {
            activeHistory = batch.at("status") == "active" && batch.at("items").size() == 1;
          }
        expect(activeHistory, "waiting clear retains active batch history");
        write(retained.worktree / "port/queue-release.flag", "release controlled fixture");
        wait(fleet);
        expect(!state().active && state().queue.empty() && state().completed == 1,
               "retained current target completes without resurrecting cleared work");
        fleet.enqueue(id, Json::array({{{"id", "next-settings-a"}}, {{"id", "next-settings-b"}}}));
        fleet.start(id);
        wait(fleet);
        expect(!state().configurationPending && state().spec.roles == nextWork.roles &&
                   state().spec.attempts == 7 && state().queue.empty() &&
                   read(state().prompt).find("Role: Drafter") != std::string::npos,
               "deferred configuration reaches the next actual two-target driver run");
        fleet.remove(id);
      }
      auto statePath = data / "projects/fixture/fleet.json";
      HANDLE lockedState = CreateFileW(statePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      expect(lockedState != INVALID_HANDLE_VALUE, "fleet state lock fixture opened");
      std::thread releaseLock([lockedState] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CloseHandle(lockedState);
      });
      try {
        fleet.saveDraft({{"items", Json::array()}});
      } catch (...) {
        releaseLock.join();
        throw;
      }
      releaseLock.join();
      expect(Json::parse(read(statePath)).at("agents").size() == 2 &&
                 !fs::exists(data / "projects/fixture/fleet.tmp"),
             "fleet state survives transient file lock");
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
      bool editedDuringGeneration = false;
      auto editedDraft = Json{{"title", "User edits while generating"},
                              {"prompt", "Keep my instructions"},
                              {"items", Json::array()}};
      reject(
          [&] {
            fleet.generateDraft("Hard matcher", 3, generation, [&](const std::string &) {
              if (!editedDuringGeneration) {
                editedDuringGeneration = true;
                fleet.saveDraft(editedDraft);
              }
            });
          },
          "concurrent draft edits reject stale generation results");
      expect(editedDuringGeneration && fleet.draft().at("title") == editedDraft.at("title") &&
                 fleet.draft().at("prompt") == editedDraft.at("prompt"),
             "generation preserves concurrent user edits");
      bool reservedDuringGeneration = false;
      auto refreshed = fleet.generateDraft("Hard matcher", 3, generation, [&](const std::string &) {
        if (!reservedDuringGeneration) {
          reservedDuringGeneration = true;
          fleet.enqueue("", Json::array({{{"id", "0"}, {"name", "target0"}, {"module", "port"}}}),
                        "Concurrent reservation");
        }
      });
      expect(reservedDuringGeneration && refreshed.at("items").size() == 2 &&
                 refreshed.at("items")[0].at("id") != "0",
             "generation drops targets reserved while scheduler ran");
      fleet.editBatch(fleet.batches().back().at("id").get<std::string>(), 0, true);
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
      std::string runningBatch;
      for (auto &batch : fleet.batches())
        if (batch.at("agentId") == first && batch.at("status") != "done")
          runningBatch = batch.at("id").get<std::string>();
      expect(!runningBatch.empty(), "running agent batch exists");
      reject([&] { fleet.editBatch(runningBatch, 1); },
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
        expect(read(state.prompt).find("Role: Random") != std::string::npos,
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
      auto preferencesPath = data / "preferences.json";
      auto previousPreferences = fs::exists(preferencesPath) ? read(preferencesPath) : "{}";
      auto safePreferences = Json::parse(previousPreferences);
      safePreferences["safeMode"] = true;
      write(preferencesPath, safePreferences.dump(2));
      reject([&] { fleet.takeBatch(external); }, "safe mode refuses an already queued MCP batch");
      write(preferencesPath, previousPreferences);
      auto batch = fleet.takeBatch(external);
      expect(batch["targets"].size() == 1 && batch["targets"][0]["id"] == "external",
             "MCP receives only prepared targets");
      expect(batch["status"] == "assigned", "MCP batch delivered");
      expect(batch.at("instructions").get<std::string>().find("Do not spawn or delegate") !=
                     std::string::npos &&
                 batch.at("instructions").get<std::string>().find("Do not use Ghidra drafts") !=
                     std::string::npos,
             "MCP handoff preserves the same delegation and matching policy as native drivers");
      expect(fleet.takeBatch(external)["status"] == "empty", "MCP batch not assigned twice");
      fleet.finishBatch(external);
      for (auto &state : fleet.snapshot())
        if (state.id == external)
          expect(state.queue.size() == 1 && state.queue[0]["id"] == "external-later",
                 "MCP finish retains later queue additions");
      fleet.clear(external);
      a.count = 3;
      fleet.configure(external, a);
      fleet.enqueue(external,
                    Json::array({{{"id", "observed-hit"}, {"name", "observed_hit"}},
                                 {{"id", "observed-miss"}, {"name", "observed_miss"}},
                                 {{"id", "observed-pending"}, {"name", "observed_pending"}}}));
      fleet.start(external);
      wait(fleet);
      fleet.takeBatch(external);
      auto hit = fleet.runTool(external, "match",
                               {{"value", "print('MATCHING VERSIONS: 1.2')"},
                                {"func", "observed_hit"},
                                {"size", "0x40"}});
      auto miss = fleet.runTool(external, "match",
                                {{"value", "print('MATCHING VERSIONS: none; divergences=2')"},
                                 {"func", "observed_miss"},
                                 {"size", "0x40"}});
      expect(hit.code == 0 && miss.code == 0, "actual MCP match commands executed");
      auto observedStats = fleet.backend("stats.get", Json::object()).at(external);
      expect(observedStats.at("attempts") == 2 && observedStats.at("declaredMatches") == 1 &&
                 observedStats.at("nearMisses") == 1,
             "actual MCP verdicts feed live match and near-miss statistics");
      fleet.finishBatch(external);
      for (auto &state : fleet.snapshot())
        if (state.id == external)
          expect(state.queue.size() == 1 && state.queue[0]["name"] == "observed_pending",
                 "observed MCP attempts consume worked targets and preserve untouched targets");
      fleet.clear(external);
      a.count = 1;
      fleet.configure(external, a);
      McpServer mcp(fleet, desc, data / "mcp.json");
      expect(mcp.port() != 0 && mcp.configuration().find("127.0.0.1") != std::string::npos,
             "authenticated loopback MCP config");
      fleet.enqueue(external, Json::array({{{"id", "external-http"}, {"name", "external_http"}}}));
      fleet.start(external);
      wait(fleet);
      write(dir / "mcp_client.py",
            R"PY(import json,pathlib,sys,time,threading,urllib.request,urllib.error
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
def modern(method,params=None,version='2026-07-28',changes=None,expected=200):
 p=dict(params or {});p['_meta']={'io.modelcontextprotocol/protocolVersion':version,'io.modelcontextprotocol/clientCapabilities':{}}
 h=dict(headers);h.update({'MCP-Protocol-Version':version,'Mcp-Method':method,'Content-Type':'application/json'})
 if method=='tools/call':h['Mcp-Name']=p['name']
 if changes:h.update(changes)
 body=json.dumps({'jsonrpc':'2.0','id':9,'method':method,'params':p}).encode()
 try:response=urllib.request.urlopen(urllib.request.Request(url,body,h))
 except urllib.error.HTTPError as error:response=error
 with response:
  assert response.status==expected,(response.status,response.read())
  assert not response.headers.get('Mcp-Session-Id')
  return json.load(response)
discovery=modern('server/discover')['result']
assert '2026-07-28' in discovery['supportedVersions'] and discovery['resultType']=='complete'
assert discovery['ttlMs']==0 and discovery['cacheScope']=='private'
assert modern('tools/list')['result']['resultType']=='complete'
assert modern('ping',changes={'Mcp-Method':'tools/list'},expected=400)['error']['code']==-32020
unsupported=modern('ping',version='2100-01-01',expected=400)['error']
assert unsupported['code']==-32022 and unsupported['data']['requested']=='2100-01-01'
assert modern('absent/method',expected=404)['error']['code']==-32601
assert modern('tools/call',{'name':'next_batch','arguments':{}},expected=400)['error']['code']==-32602
assert modern('tools/call',{'name':'progress','arguments':{}},changes={'Mcp-Name':'=?base64?cHJvZ3Jlc3M=?='})['result']['resultType']=='complete'
for version in ['2024-11-05','2025-03-26','2025-06-18','2025-11-25','2100-01-01']:
 negotiated=call('initialize',{'protocolVersion':version,'clientInfo':{'name':'External fixture','version':'1'},'capabilities':{}})
 assert negotiated['protocolVersion']==(version if version!='2100-01-01' else '2025-11-25')
 with urllib.request.urlopen(urllib.request.Request(url,headers=headers,method='DELETE')) as response:assert response.status==200
 headers.pop('Mcp-Session-Id')
call('initialize',{'protocolVersion':'2025-03-26','clientInfo':{'name':'External fixture','version':'1'},'capabilities':{}})
assert any(t['name']=='next_batch' for t in call('tools/list')['tools'])
batch=call('tools/call',{'name':'next_batch','arguments':{}})
assert json.loads(batch['content'][0]['text'])['status']=='assigned'
output=call('tools/call',{'name':'echo','arguments':{'value':"print('native-mcp-output')"}})
assert 'native-mcp-output' in output['content'][0]['text']
def raw(payload, selected=None):
 with urllib.request.urlopen(urllib.request.Request(url,json.dumps(payload).encode(),dict(selected or headers)),timeout=15) as response:
  return response.status,response.read()
def pending(request_id, name, arguments):
 results=[]
 thread=threading.Thread(target=lambda:results.append(raw({'jsonrpc':'2.0','id':request_id,'method':'tools/call','params':{'name':name,'arguments':arguments}})))
 thread.start();return thread,results
def cancel(request_id, selected=None):
 status,body=raw({'jsonrpc':'2.0','method':'notifications/cancelled','params':{'requestId':request_id}}, selected)
 assert status==202 and not body
# Unknown/malformed IDs and another initialized session cannot cancel our request.
cancel('unknown');raw({'jsonrpc':'2.0','method':'notifications/cancelled','params':{}})
saved=headers.copy()
call('initialize',{'clientInfo':{'name':'External fixture','version':'1'},'capabilities':{}})
other=headers.copy();headers.clear();headers.update(saved)
thread,responses=pending(901,'echo',{'value':"import time;print('request-cancel-fixture',flush=True);time.sleep(30);print('must-not-complete')"})
time.sleep(.5);cancel(901,other);cancel('901');time.sleep(.3);assert thread.is_alive(),'different session or ID type cancelled our tool'
cancel(901);thread.join(5);assert not thread.is_alive() and responses==[(202,b'')],responses
with urllib.request.urlopen(urllib.request.Request(url,headers=other,method='DELETE')) as response:assert response.status==200
output=call('tools/call',{'name':'echo','arguments':{'value':"print('batch-remains-usable')"}})
assert 'batch-remains-usable' in output['content'][0]['text']
thread,responses=pending(902,'next_batch',{'timeoutMs':30000})
time.sleep(.2);cancel(902);thread.join(3);assert not thread.is_alive() and responses==[(202,b'')],responses
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
by_id={r['id']:r for r in responses}
assert any(t['name']=='next_batch' for t in by_id[2]['result']['tools'])
print('native stdio MCP initialize, notification, session, tools, ping and EOF passed')
)PY");
      auto bridge = setup.run(
          {{"python", utf8((dir / "stdio_client.py").wstring()),
            utf8((fs::u8path(selfExecutable()).parent_path() / "TangOSLite.exe").wstring()),
            utf8((data / "mcp.json").wstring())},
           dir});
      expect(bridge.code == 0, "MCP stdio integration: " + bridge.output);
      write(dir / "modern_stdio.py", R"PY(import subprocess,json,sys
meta={'io.modelcontextprotocol/protocolVersion':'2026-07-28','io.modelcontextprotocol/clientCapabilities':{}}
requests=[{'jsonrpc':'2.0','id':i,'method':method,'params':{'_meta':meta}} for i,method in enumerate(['server/discover','tools/list','ping'],1)]
p=subprocess.run([sys.argv[1],'--mcp-stdio',sys.argv[2],'External fixture'],input=''.join(json.dumps(r)+'\n' for r in requests),text=True,capture_output=True,timeout=20)
assert p.returncode==0,p.stderr
responses={r['id']:r for r in map(json.loads,p.stdout.splitlines())}
assert len(responses)==3,responses
assert responses[1]['result']['supportedVersions'][0]=='2026-07-28'
assert any(t['name']=='next_batch' for t in responses[2]['result']['tools'])
assert all(r['result']['resultType']=='complete' for r in responses.values())
print('modern MCP stdio discovery, per-request metadata, tools, ping and EOF passed')
)PY");
      auto modernBridge = setup.run(
          {{"python", utf8((dir / "modern_stdio.py").wstring()),
            utf8((fs::u8path(selfExecutable()).parent_path() / "TangOSLite.exe").wstring()),
            utf8((data / "mcp.json").wstring())},
           dir},
          {}, data / "modern-stdio.log");
      expect(modernBridge.code == 0, "Modern MCP stdio integration: " + modernBridge.output);
      fleet.enqueue(external, Json::array({{{"id", "stdio-cancel"}, {"name", "stdio_cancel"}}}));
      fleet.start(external);
      wait(fleet);
      write(dir / "stdio_cancel.py", R"PY(import subprocess,json,sys,time,threading,queue
p=subprocess.Popen([sys.argv[1],'--mcp-stdio',sys.argv[2],'External fixture'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
messages=queue.Queue()
def reader():
 for line in p.stdout:messages.put(json.loads(line))
threading.Thread(target=reader,daemon=True).start()
def send(method,params=None,request_id=None):
 body={'jsonrpc':'2.0','method':method,'params':params or {}}
 if request_id is not None:body['id']=request_id
 p.stdin.write(json.dumps(body)+'\n');p.stdin.flush()
def receive(request_id):
 result=messages.get(timeout=8)
 assert result['id']==request_id and 'error' not in result,result
 return result['result']
try:
 send('initialize',{'clientInfo':{'name':'client-controlled-name','version':'1'},'capabilities':{}},1);receive(1)
 send('tools/call',{'name':'next_batch','arguments':{}},10)
 assert json.loads(receive(10)['content'][0]['text'])['status']=='assigned'
 send('tools/call',{'name':'echo','arguments':{'value':"import time;print('stdio-cancel-started',flush=True);time.sleep(30)"}},11)
 time.sleep(.4)
 send('notifications/cancelled',{'requestId':11})
 send('ping',request_id=12);receive(12)
 send('tools/call',{'name':'echo','arguments':{'value':"print('stdio-after-cancel')"}},13)
 assert 'stdio-after-cancel' in receive(13)['content'][0]['text']
 send('tools/call',{'name':'finish_batch','arguments':{}},14);receive(14)
 p.stdin.close();assert p.wait(timeout=10)==0
 assert messages.empty(),'Cancelled request received an unwanted response'
finally:
 if p.poll() is None:p.kill();p.wait()
print('native stdio cancellation keeps input responsive, suppresses cancelled response and preserves batch')
p=subprocess.Popen([sys.argv[1],'--mcp-stdio',sys.argv[2],'External fixture'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
p.stdin.write(json.dumps({'jsonrpc':'2.0','id':1,'method':'initialize','params':{'clientInfo':{'name':'ignored','version':'1'},'capabilities':{}}})+'\n');p.stdin.flush()
assert 'error' not in json.loads(p.stdout.readline())
p.stdin.write(json.dumps({'jsonrpc':'2.0','id':20,'method':'tools/call','params':{'name':'next_batch','arguments':{'timeoutMs':30000}}})+'\n');p.stdin.flush()
time.sleep(.3);p.stdin.close()
assert p.wait(timeout=5)==0,'EOF failed to cancel a pending batch request'
print('native stdio EOF cancels pending session work before joining')
p=subprocess.Popen([sys.argv[1],'--mcp-stdio',sys.argv[2],'External fixture'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
p.stdin.write(json.dumps({'jsonrpc':'2.0','id':1,'method':'initialize','params':{'clientInfo':{'name':'ignored','version':'1'},'capabilities':{}}})+'\n');p.stdin.flush()
assert 'error' not in json.loads(p.stdout.readline())
p.stdin.write(json.dumps({'jsonrpc':'2.0','id':21,'method':'tools/call','params':{'name':'next_batch','arguments':{'timeoutMs':30000}}})+'\n');p.stdin.flush()
time.sleep(.2)
try:p.stdin.write('x'*(1024*1024+2));p.stdin.flush()
except BrokenPipeError:pass
assert p.wait(timeout=5)==1,'Oversized request did not close cleanly while workers were active'
print('native stdio malformed-input teardown preserves worker lifetimes')
)PY");
      auto stdioCancelled = setup.run(
          {{"python", utf8((dir / "stdio_cancel.py").wstring()),
            utf8((fs::u8path(selfExecutable()).parent_path() / "TangOSLite.exe").wstring()),
            utf8((data / "mcp.json").wstring())},
           dir});
      expect(stdioCancelled.code == 0, "MCP stdio cancellation: " + stdioCancelled.output);
      fleet.enqueue(external, Json::array({{{"id", "request-stop"}, {"name", "request_stop"}}}));
      fleet.start(external);
      wait(fleet);
      expect(fleet.takeBatch(external)["status"] == "assigned", "UI-stop fixture assigned");
      Runner requestRunner;
      Result cancelledResult{};
      std::exception_ptr toolError;
      std::thread activeTool([&] {
        try {
          cancelledResult = fleet.runTool(
              external, "echo",
              {{"value", "import time;print('ui-stop-started',flush=True);time.sleep(30)"}},
              &requestRunner);
        } catch (...) {
          toolError = std::current_exception();
        }
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      fleet.stop(external);
      activeTool.join();
      if (toolError)
        std::rethrow_exception(toolError);
      expect(requestRunner.isCancelled() && cancelledResult.code == ERROR_CANCELLED,
             "UI Stop reaches caller-owned MCP process runner");
      fleet.enqueue(external, Json::array({{{"id", "http-ui-stop"}, {"name", "http_ui_stop"}}}));
      fleet.start(external);
      wait(fleet);
      write(dir / "http_ui_stop.py", R"PY(import json,pathlib,sys,urllib.request
config=json.loads(pathlib.Path(sys.argv[1]).read_text())['mcpServers']['tangos-lite']
headers=config['headers'].copy();headers['Content-Type']='application/json'
def call(method,params):
 body=json.dumps({'jsonrpc':'2.0','id':1,'method':method,'params':params}).encode()
 with urllib.request.urlopen(urllib.request.Request(config['url'],body,dict(headers)),timeout=8) as response:
  if response.headers.get('Mcp-Session-Id'):headers['Mcp-Session-Id']=response.headers['Mcp-Session-Id']
  result=json.load(response);assert 'error' not in result,result
  return result['result']
call('initialize',{'clientInfo':{'name':'External fixture','version':'1'},'capabilities':{}})
call('tools/call',{'name':'next_batch','arguments':{}})
pathlib.Path(sys.argv[2]).write_text('client-ready')
result=call('tools/call',{'name':'echo','arguments':{'value':"import time;print('http-ui-stop',flush=True);time.sleep(30)"}})
assert result['isError'] and '[CANCELLED]' in result['content'][0]['text'],result
with urllib.request.urlopen(urllib.request.Request(config['url'],headers=headers,method='DELETE')) as response:assert response.status==200
print('UI Stop returns a tool error to the client instead of silently dropping its response')
)PY");
      auto marker = dir / "http-ui-stop-ready";
      Runner stopClient;
      Result stoppedReply{};
      std::thread stopRequest([&] {
        stoppedReply =
            stopClient.run({{"python", utf8((dir / "http_ui_stop.py").wstring()),
                             utf8((data / "mcp.json").wstring()), utf8(marker.wstring())},
                            dir});
      });
      auto deadline = GetTickCount64() + 5000;
      while (!fs::exists(marker) && GetTickCount64() < deadline)
        Sleep(10);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      fleet.stop(external);
      stopRequest.join();
      expect(stoppedReply.code == 0, "UI Stop MCP response: " + stoppedReply.output);
      auto inspector = GetEnvironmentVariableW(L"TANGOS_MCP_INSPECTOR", nullptr, 0);
      if (inspector) {
        std::wstring inspectorPath(inspector, 0);
        GetEnvironmentVariableW(L"TANGOS_MCP_INSPECTOR", inspectorPath.data(), inspector);
        inspectorPath.resize(inspector - 1);
        auto clientConfig = data / "inspector-client.json";
        write(clientConfig,
              mcpClientConfiguration("Generic",
                                     fs::u8path(selfExecutable()).parent_path() / "TangOSLite.exe",
                                     data / "mcp.json", "External fixture")
                  .dump(2));
        auto actual = setup.run(
            {{"node", utf8(inspectorPath), "--cli", "--config", utf8(clientConfig.wstring()),
              "--server", "tangos-lite", "--method", "tools/list"},
             dir},
            {}, data / "inspector-tools.log");
        expect(actual.code == 0 && actual.output.find("next_batch") != std::string::npos,
               "official MCP Inspector interoperability: " + actual.output);
      }
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
    Command secretCommand{{"python", utf8((dir / "split.py").wstring())},
                          dir,
                          {{"TEST_API_KEY", "fixture-secret-123456"}}};
    secretCommand.activityArguments =
        Json{{"nested", {{"apiKey", "fixture-secret-123456"}, {"note", "fixture-secret-123456"}}}}
            .dump();
    auto secretResult = setup.run(secretCommand, {}, secretLog);
    expect(read(secretLog).find("fixture-secret-123456") == std::string::npos &&
               read(secretLog).find("[REDACTED]") != std::string::npos,
           "stream-spanning secret redaction");
    expect(secretResult.output.find("fixture-secret-123456") == std::string::npos,
           "captured process output is redacted before returning");
    bool observedSecretRun = false;
    for (auto &run : activityBus().snapshot())
      if (run.value("log", std::string()) == utf8(secretLog.wstring())) {
        observedSecretRun = true;
        expect(run.dump().find("fixture-secret-123456") == std::string::npos,
               "activity arguments and output never expose nested credentials");
      }
    expect(observedSecretRun, "redaction test observes actual retained activity");
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
