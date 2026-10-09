#include "fleet.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace lite;

// Explicit opt-in live test. Uses an already installed Ollama model only;
// no model download, external endpoint, credentials or user repository.
int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: lite_provider_tests MODEL REPORT_PATH\n";
    return 2;
  }
  auto root = fs::temp_directory_path() / fs::u8path("TangOS local provider " + uniqueId());
  auto repo = root / "repository", data = root / "data/projects/provider";
  try {
    fs::create_directories(repo / "tools");
    fs::create_directories(repo / "src");
    fs::create_directories(repo / "port");
    write(repo / "AGENTS.md", "LOCAL_PROVIDER_FIXTURE: preserve src/ and do not edit files.\n");
    write(repo / "src/original.cpp", "int original = 1;\n");
    write(repo / "tools/port_refcheck.py",
          "from pathlib import Path\nassert Path('src/original.cpp').read_text() == 'int original "
          "= 1;\\n'\nprint('fixture source preserved')\n");
    write(repo / "tools/driver.py", R"PY(import argparse,json,os,pathlib,urllib.request
p=argparse.ArgumentParser();p.add_argument('--wl');p.add_argument('--out');p.add_argument('--prompt');a=p.parse_args()
instructions=pathlib.Path(a.prompt).read_text(encoding='utf-8')
assert 'LOCAL_PROVIDER_FIXTURE' in instructions and 'never modify src/' in instructions
assert os.environ['GLM_BASE_URL']=='http://127.0.0.1:11434/v1'
body={'model':os.environ['GLM_MODEL'],'stream':False,'temperature':0,'max_tokens':16,
      'messages':[{'role':'system','content':instructions},{'role':'user','content':'Reply with OK. Do not use tools or edit files.'}]}
request=urllib.request.Request(os.environ['GLM_BASE_URL']+'/chat/completions',json.dumps(body).encode(),{'Content-Type':'application/json'})
with urllib.request.urlopen(request,timeout=120) as response: answer=json.load(response)
content=answer['choices'][0]['message']['content']
assert isinstance(content,str) and content.strip(), 'provider returned no message'
rows=[json.loads(line) for line in pathlib.Path(a.wl).read_text().splitlines() if line]
pathlib.Path(a.out).write_text(json.dumps({'results':[dict(row,matched=False,note='Actual local provider transport tested; no byte proof') for row in rows]}))
print('actual local provider returned a nonempty OpenAI-compatible message',flush=True)
)PY");
    Json descriptor{
        {"tangosVersion", "1"},
        {"project", {{"name", "provider"}, {"title", "Disposable provider test"}}},
        {"console", {{"driver", "drive"}}},
        {"tools",
         Json::array(
             {{{"id", "drive"},
               {"readOnly", false},
               {"command", "{python} tools/driver.py --wl {wl} --out {out} --prompt {prompt}"}}})}};
    write(repo / "tangos.json", descriptor.dump(2));
    Runner runner;
    auto git = [&](Args args) {
      Args command{"git", "-c", "core.autocrlf=false"};
      command.insert(command.end(), args.begin(), args.end());
      auto result = runner.run({command, repo});
      if (result.code)
        throw std::runtime_error(result.output);
    };
    git({"init", "-b", "main"});
    git({"config", "core.autocrlf", "false"});
    git({"config", "user.name", "Disposable provider fixture"});
    git({"config", "user.email", "provider@example.invalid"});
    git({"add", "."});
    git({"commit", "-m", "Disposable local provider fixture"});
    {
      Fleet fleet(repo, data, loadDescriptor(repo), Settings{});
      AgentSpec agent;
      agent.name = "Actual local Ollama";
      agent.model = argv[1];
      agent.baseUrl = "http://127.0.0.1:11434/v1";
      agent.provider = "Ollama";
      agent.role = "Hard matcher";
      agent.count = agent.attempts = agent.jobs = 1;
      auto id = fleet.add(agent);
      fleet.enqueue(id, Json::array({{{"id", "local-provider-target"}, {"name", "fixture"}}}));
      fleet.start(id);
      auto deadline = GetTickCount64() + 150000;
      while (fleet.running() && GetTickCount64() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (fleet.running()) {
        fleet.stopAll();
        throw std::runtime_error("Actual local provider timed out");
      }
      auto state = fleet.snapshot().front();
      if (state.completed != 1 || !state.queue.empty() || state.phase != "review")
        throw std::runtime_error("Actual provider Fleet failed: " + state.phase + ": " +
                                 state.detail);
      if (read(state.log).find("nonempty OpenAI-compatible message") == std::string::npos ||
          read(state.worktree / "src/original.cpp") != "int original = 1;\n")
        throw std::runtime_error("Actual provider response/source safety evidence missing");
      if (runner.run({{"git", "diff", "--quiet", "HEAD", "--", "src"}, state.worktree}).code)
        throw std::runtime_error("Actual provider modified tracked source");
      auto reportPath = fs::u8path(argv[2]);
      fs::create_directories(reportPath.parent_path());
      write(reportPath, Json{{"state", "passed"},
                             {"provider", "Ollama"},
                             {"model", argv[1]},
                             {"endpoint", agent.baseUrl},
                             {"nativeFleet", true},
                             {"instructionsDelivered", true},
                             {"completed", state.completed},
                             {"trackedSourceUnchanged", true},
                             {"byteMatching", "unverified: transport fixture only"},
                             {"fixture", utf8(root.wstring())},
                             {"log", utf8(state.log.wstring())}}
                            .dump(2));
    }
    std::cout << "PASS actual Ollama provider: native Fleet, instruction delivery, retained log, "
                 "source safety\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << "\nFixture retained: " << root << "\n";
    return 1;
  }
}
