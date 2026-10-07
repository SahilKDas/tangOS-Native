#include "fleet.h"
#include <wincrypt.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
namespace lite {
static void validKey(const std::string &name) {
  if (name.empty() || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != name.npos)
    throw std::runtime_error("Credential name must be an uppercase environment variable");
}
void Vault::set(const std::string &name, const std::string &secret) {
  validKey(name);
  if (secret.empty() || secret.size() > 65536)
    throw std::runtime_error("Invalid credential length");
  DATA_BLOB input{(DWORD)secret.size(), (BYTE *)secret.data()}, output{};
  if (!CryptProtectData(&input, L"TangOS Lite credential", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &output))
    throw std::runtime_error("Windows DPAPI encryption failed");
  try {
    write(directory / fs::u8path(name + ".dpapi"),
          std::string((char *)output.pbData, output.cbData));
  } catch (...) {
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    throw;
  }
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
}
void Vault::remove(const std::string &name) {
  validKey(name);
  fs::remove(directory / fs::u8path(name + ".dpapi"));
}
std::map<std::string, std::string> Vault::values() const {
  std::map<std::string, std::string> values;
  if (!fs::exists(directory))
    return values;
  for (auto &entry : fs::directory_iterator(directory))
    if (entry.path().extension() == ".dpapi") {
      auto name = utf8(entry.path().stem().wstring());
      validKey(name);
      auto bytes = read(entry.path());
      DATA_BLOB in{(DWORD)bytes.size(), (BYTE *)bytes.data()}, out{};
      if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                              &out))
        throw std::runtime_error("Cannot decrypt credential: " + name);
      values[name] = std::string((char *)out.pbData, out.cbData);
      SecureZeroMemory(out.pbData, out.cbData);
      LocalFree(out.pbData);
    }
  return values;
}
static Json specJson(const AgentSpec &s) {
  return {{"name", s.name},       {"kind", s.kind},         {"role", s.role},
          {"effort", s.effort},   {"model", s.model},       {"base_url", s.baseUrl},
          {"dialect", s.dialect}, {"key", s.key},           {"cli", s.cli},
          {"count", s.count},     {"attempts", s.attempts}, {"jobs", s.jobs},
          {"loop", s.loop}};
}
Json agentJson(const AgentState &s) {
  return {{"spec", specJson(s.spec)},
          {"id", s.id},
          {"phase", s.phase},
          {"detail", s.detail},
          {"branch", s.branch},
          {"last_line", s.lastLine},
          {"worktree", utf8(s.worktree.wstring())},
          {"log", utf8(s.log.wstring())},
          {"prompt", utf8(s.prompt.wstring())},
          {"worklist", utf8(s.worklist.wstring())},
          {"queue", s.queue},
          {"completed", s.completed},
          {"total", s.total}};
}
AgentState parseAgent(const Json &j) {
  AgentState s;
  auto v = j.at("spec");
  s.spec.name = v.at("name");
  s.spec.kind = v.value("kind", std::string("api"));
  s.spec.role = v.value("role", std::string("Unassigned"));
  s.spec.effort = v.value("effort", std::string("high"));
  s.spec.model = v.value("model", std::string());
  s.spec.baseUrl = v.value("base_url", std::string());
  s.spec.dialect = v.value("dialect", std::string("openai"));
  s.spec.key = v.value("key", std::string());
  s.spec.cli = v.value("cli", std::string());
  s.spec.count = v.value("count", 16);
  s.spec.attempts = v.value("attempts", 4);
  s.spec.jobs = v.value("jobs", 1);
  s.spec.loop = v.value("loop", false);
  s.id = j.at("id");
  s.phase = j.value("phase", std::string("idle"));
  s.detail = j.value("detail", std::string());
  s.branch = j.value("branch", std::string());
  s.lastLine = j.value("last_line", std::string());
  s.worktree = fs::u8path(j.value("worktree", std::string()));
  s.log = fs::u8path(j.value("log", std::string()));
  s.prompt = fs::u8path(j.value("prompt", std::string()));
  s.worklist = fs::u8path(j.value("worklist", std::string()));
  s.queue = j.value("queue", Json::array());
  s.completed = j.value("completed", 0);
  s.total = j.value("total", 0);
  if (!s.queue.is_array() || s.id.empty() ||
      s.id.find_first_not_of("0123456789abcdef-") != s.id.npos)
    throw std::runtime_error("Invalid fleet state");
  if (s.spec.count < 1 || s.spec.count > 200 || s.spec.attempts < 1 || s.spec.attempts > 20 ||
      s.spec.jobs < 1 || s.spec.jobs > 16)
    throw std::runtime_error("Invalid agent limits");
  if (s.spec.name.empty() || s.spec.name.size() > 120 ||
      (s.spec.kind != "api" && s.spec.kind != "cli" && s.spec.kind != "mcp"))
    throw std::runtime_error("Invalid agent name/kind");
  if (s.spec.kind == "api" && (s.spec.model.empty() || s.spec.baseUrl.empty()))
    throw std::runtime_error("API agents require an explicit model and API base URL");
  if (s.spec.kind == "api" && s.spec.baseUrl.rfind("https://", 0) != 0 &&
      s.spec.baseUrl.rfind("http://127.0.0.1:", 0) != 0 &&
      s.spec.baseUrl.rfind("http://localhost:", 0) != 0)
    throw std::runtime_error(
        "API endpoints require HTTPS; HTTP is allowed only for local model servers");
  return s;
}
Fleet::Fleet(fs::path repo, fs::path dir, Descriptor desc, Settings prefs, Sink sink)
    : repository(fs::weakly_canonical(repo)), directory(std::move(dir)),
      descriptor(std::move(desc)), settings(std::move(prefs)),
      vault(directory.parent_path().parent_path() / "vault"), events(std::move(sink)) {
  fs::create_directories(directory);
  controllerOwnership =
      CreateFileW((directory / "controller.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (controllerOwnership == INVALID_HANDLE_VALUE)
    throw std::runtime_error("This project's fleet is already open in another Lite window");
  auto file = directory / "fleet.json";
  try {
    auto persisted = fs::exists(file) ? Json::parse(read(file)) : Json({{"agents", Json::array()}});
    for (auto &j : persisted.at("agents")) {
      auto job = std::make_shared<Job>();
      job->state = parseAgent(j);
      if (!job->state.worktree.empty())
        confinedPath(directory, utf8(job->state.worktree.wstring()));
      if (job->state.phase == "running" || job->state.phase == "scheduling" ||
          job->state.phase == "verifying") {
        job->state.phase = "interrupted";
        job->state.detail =
            "Previous run interrupted; inspect preserved worktree/log before resuming";
      }
      jobs[job->state.id] = job;
    }
  } catch (...) {
    CloseHandle(controllerOwnership);
    controllerOwnership = INVALID_HANDLE_VALUE;
    throw;
  }
}
Fleet::~Fleet() {
  stopAll();
  for (auto &item : jobs)
    if (item.second->worker.joinable())
      item.second->worker.join();
  if (controllerOwnership != INVALID_HANDLE_VALUE)
    CloseHandle(controllerOwnership);
}
void Fleet::saveLocked() {
  Json j = {{"version", 1}, {"repository", utf8(repository.wstring())}, {"agents", Json::array()}};
  for (auto &item : jobs)
    j["agents"].push_back(agentJson(item.second->state));
  auto temp = directory / "fleet.tmp";
  write(temp, j.dump(2));
  if (!MoveFileExW(temp.c_str(), (directory / "fleet.json").c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot save fleet state");
}
std::string Fleet::add(AgentSpec spec) {
  auto job = std::make_shared<Job>();
  job->state.spec = std::move(spec);
  job->state.id = uniqueId();
  parseAgent(agentJson(job->state));
  std::lock_guard<std::mutex> lock(mutex);
  jobs[job->state.id] = job;
  saveLocked();
  return job->state.id;
}
void Fleet::configure(const std::string &id, AgentSpec spec) {
  std::lock_guard<std::mutex> lock(mutex);
  auto j = jobs.at(id);
  if (j->active)
    throw std::runtime_error("Stop agent before changing configuration");
  auto s = j->state;
  s.spec = std::move(spec);
  parseAgent(agentJson(s));
  j->state = s;
  saveLocked();
}
void Fleet::remove(const std::string &id) {
  stop(id);
  std::shared_ptr<Job> job;
  {
    std::lock_guard<std::mutex> lock(mutex);
    job = jobs.at(id);
  }
  if (job->worker.joinable())
    job->worker.join();
  std::lock_guard<std::mutex> lock(mutex);
  jobs.erase(id);
  saveLocked();
}
std::vector<AgentState> Fleet::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex);
  std::vector<AgentState> out;
  for (auto &item : jobs) {
    auto s = item.second->state;
    s.active = item.second->active;
    out.push_back(s);
  }
  return out;
}
static std::string target(const Json &row) {
  if (row.contains("ref"))
    return row["ref"].is_string() ? row["ref"].get<std::string>() : row["ref"].dump();
  if (row.contains("id"))
    return row["id"].is_string() ? row["id"].get<std::string>() : row["id"].dump();
  return row.value("module", std::string()) + ":" + row.value("name", std::string());
}
void Fleet::enqueue(const std::string &id, const Json &rows) {
  if (!rows.is_array())
    throw std::runtime_error("Worklist must be an array");
  std::lock_guard<std::mutex> lock(mutex);
  auto job = jobs.at(id);
  std::set<std::string> taken;
  for (auto &item : jobs)
    for (auto &row : item.second->state.queue)
      taken.insert(target(row));
  Json fresh = Json::array();
  for (auto &row : rows) {
    if (row.value("matched", false) || row.contains("noMatch") || row.contains("claim"))
      throw std::runtime_error("Target is already matched, exempt or externally claimed");
    auto ref = target(row);
    if (ref.empty() || ref == ":")
      throw std::runtime_error("Work target needs id, ref or name");
    if (!taken.insert(ref).second)
      throw std::runtime_error("Target already claimed: " + ref);
    fresh.push_back(row);
  }
  for (auto &row : fresh)
    job->state.queue.push_back(row);
  job->state.total = (int)job->state.queue.size();
  saveLocked();
}
void Fleet::clear(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex);
  auto job = jobs.at(id);
  if (job->active)
    throw std::runtime_error("Stop agent before clearing its queue");
  job->state.queue = Json::array();
  job->state.total = 0;
  saveLocked();
}
bool Fleet::running() const {
  std::lock_guard<std::mutex> lock(mutex);
  for (auto &item : jobs)
    if (item.second->active)
      return true;
  return false;
}
void Fleet::stop(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex);
  auto job = jobs.at(id);
  job->runner.cancel();
  job->state.spec.loop = false;
  if (job->state.spec.kind == "mcp" && !job->externalTask) {
    job->active = false;
    job->state.phase = "cancelled";
    job->state.detail = "External batch revoked; partial work retained";
    saveLocked();
  }
}
void Fleet::stopAll() {
  std::lock_guard<std::mutex> lock(mutex);
  for (auto &item : jobs) {
    item.second->runner.cancel();
    item.second->state.spec.loop = false;
    if (item.second->state.spec.kind == "mcp" && !item.second->externalTask) {
      item.second->active = false;
      item.second->state.phase = "cancelled";
    }
  }
}
void Fleet::setPolicy(const Settings &next) {
  std::lock_guard<std::mutex> lock(mutex);
  for (auto &item : jobs)
    if (item.second->active)
      throw std::runtime_error("Stop agents before changing safety policy");
  settings = next;
}
void Fleet::start(const std::string &id, bool execute) {
  std::shared_ptr<Job> job;
  {
    std::lock_guard<std::mutex> lock(mutex);
    job = jobs.at(id);
    if (job->active)
      throw std::runtime_error("Agent already running");
  }
  if (job->worker.joinable())
    job->worker.join();
  job->runner.reset();
  job->active = true;
  job->worker = std::thread([this, job, execute] {
    try {
      drive(job, execute);
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(mutex);
      job->state.phase = "failed";
      job->state.detail = std::string("Fleet state/log failure: ") + e.what();
      job->active = false;
    }
  });
}
void Fleet::land(const std::string &id) {
  std::shared_ptr<Job> job;
  const Tool *tool = descriptor.role("land");
  {
    std::lock_guard<std::mutex> lock(mutex);
    job = jobs.at(id);
    if (settings.portOnly) throw std::runtime_error("Landing decomp results may change src/. Disable port-only mode explicitly first.");
    if (job->active) throw std::runtime_error("Stop agent before landing results");
    if (!tool) throw std::runtime_error("No console.land tool declared by this repository");
    if (!fs::exists(directory / id / "results.output")) throw std::runtime_error("No driver results.output exists for this agent");
  }
  if (job->worker.joinable()) job->worker.join();
  job->runner.reset(); job->active = true;
  job->worker = std::thread([this, job, tool] {
    auto update = [&](const std::string &phase, const std::string &detail) {
      std::lock_guard<std::mutex> lock(mutex);
      job->state.phase = phase; job->state.detail = detail; saveLocked();
    };
    try {
      auto dir = directory / job->state.id;
      update("landing", "Applying declared land tool in isolated worktree");
      auto command = toolCommand(descriptor, *tool,
        {{"out", utf8((dir / "results.output").wstring())},
         {"wl", utf8(job->state.worklist.wstring())}, {"no_claims", true}},
        job->state.worktree, true, true);
      job->state.log = dir / "land.log";
      auto result = job->runner.run(command, events, job->state.log);
      if (result.code) throw std::runtime_error("Land tool failed; inspect land.log");
      audit(job);
      update("verifying", "Checking landed changes independently");
      int gates = 0;
      for (auto &check : discoverChecks(job->state.worktree, settings)) {
        if (!check.available || check.name == "ROM verification") continue;
        auto result = job->runner.run(check.command, events, dir / ("land-verify-" + std::to_string(gates++) + ".log"));
        if (result.code) throw std::runtime_error("Landed changes failed: " + check.name);
      }
      audit(job);
      update("review", gates ? "Landed changes passed checks; review complete diff" : "Landed changes unverified; no available gates");
    } catch (const std::exception &e) {
      try { update(job->runner.isCancelled() ? "cancelled" : "failed", e.what()); } catch (...) {}
    }
    job->active = false;
  });
}
static Result git(Runner &r, const fs::path &cwd, Args args) {
  Args all = {"git", "--no-pager",    "--literal-pathspecs", "-c", "core.fsmonitor=false",
              "-c",  "color.ui=false"};
  all.insert(all.end(), args.begin(), args.end());
  return r.run({all, cwd});
}
Json Fleet::schedule(const std::shared_ptr<Job> &job, const fs::path &cwd) {
  auto role = job->state.spec.role;
  const Tool *tool = descriptor.role(role == "Refiner"  ? "refineScheduler"
                                     : role == "Random" ? "randomScheduler"
                                                        : "scheduler");
  if (!tool)
    tool = descriptor.role("scheduler");
  if (!tool)
    throw std::runtime_error(
        "No scheduler declared in tangos.json console.scheduler; choose targets in Chaos Viewer");
  auto out = directory / fs::u8path(job->state.id) / "scheduled.jsonl";
  fs::remove(out);
  std::set<std::string> taken;
  {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto &item : jobs)
      for (auto &row : item.second->state.queue)
        taken.insert(target(row));
  }
  Json values = {{"limit", job->state.spec.count + (int)taken.size() + 16},
                 {"count", job->state.spec.count + (int)taken.size() + 16},
                 {"out", utf8(out.wstring())}};
  auto c = toolCommand(descriptor, *tool, values, cwd, true);
  auto res = job->runner.run(c, {}, directory / fs::u8path(job->state.id) / "scheduler.log");
  if (res.code)
    throw std::runtime_error("Scheduler failed; inspect scheduler.log");
  auto text = fs::exists(out) ? read(out) : res.output;
  Json rows = Json::array();
  for (auto &line : split(text, '\n')) {
    auto s = trim(line);
    if (s.empty() || s[0] != '{')
      continue;
    auto row = Json::parse(s);
    if (taken.insert(target(row)).second)
      rows.push_back(row);
    if (rows.size() >= (size_t)job->state.spec.count)
      break;
  }
  if (rows.empty())
    throw std::runtime_error("Scheduler returned no work");
  // Claims are acquired together with queue persistence, before either agent starts its driver.
  enqueue(job->state.id, rows);
  return rows;
}
void Fleet::audit(const std::shared_ptr<Job> &job) {
  Repository r(job->runner, job->state.worktree, settings);
  auto changes = parseStatus(r.git({"status", "--porcelain=v1", "-z", "--untracked-files=all"}));
  for (auto &change : changes) {
    auto why = blockedPath(change.path, settings);
    if (!why.empty())
      throw std::runtime_error("Agent change blocked: " + change.path + " - " + why);
    if (!change.original.empty()) {
      why = blockedPath(change.original, settings);
      if (!why.empty())
        throw std::runtime_error("Agent rename blocked: " + change.original);
    }
    auto p = confinedPath(job->state.worktree, change.path);
    if (fs::is_symlink(fs::symlink_status(p)))
      throw std::runtime_error("Agent symlink blocked: " + change.path);
    if (fs::is_regular_file(p)) {
      if (fs::file_size(p) > 16 * 1024 * 1024)
        throw std::runtime_error("Agent blob too large: " + change.path);
      auto reason = blockedBlob(read(p));
      if (!reason.empty())
        throw std::runtime_error("Agent blob blocked: " + change.path + " - " + reason);
    }
  }
}
void Fleet::drive(const std::shared_ptr<Job> &job, bool execute) {
  auto update = [&](const std::string &phase, const std::string &detail) {
    std::lock_guard<std::mutex> lock(mutex);
    job->state.phase = phase;
    job->state.detail = detail;
    saveLocked();
  };
  auto id = job->state.id;
  auto dir = directory / fs::u8path(id);
  fs::create_directories(dir);
  HANDLE ownership = CreateFileW((dir / "ownership.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                 nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (ownership == INVALID_HANDLE_VALUE) {
    job->active = false;
    update("blocked", "Another Console instance owns this agent");
    return;
  }
  try {
    auto secrets = vault.values();
    if (job->state.worktree.empty()) {
      update("preparing", "Creating isolated Git worktree");
      auto path = dir / "worktree";
      auto branch = "codex/lite-" + id;
      auto res = git(job->runner, repository,
                     {"worktree", "add", "-b", branch, utf8(path.wstring()), "HEAD"});
      if (res.code)
        throw std::runtime_error("Cannot create agent worktree: " + res.output);
      std::lock_guard<std::mutex> lock(mutex);
      job->state.worktree = path;
      job->state.branch = branch;
      saveLocked();
    }
    do {
      update("scheduling", "Preparing instructions and worklist");
      Json rows;
      {
        std::lock_guard<std::mutex> lock(mutex);
        rows = job->state.queue;
      }
      if (rows.empty())
        rows = schedule(job, job->state.worktree);
      if (auto enrich = descriptor.role("enrich")) {
        for (auto &row : rows)
          if (!row.contains("disasm") && row.contains("addr")) {
            Json addr = row["addr"];
            std::string address;
            if (addr.is_string())
              address = addr.get<std::string>();
            else if (addr.is_number_unsigned()) {
              std::ostringstream hex;
              hex << "0x" << std::hex << addr.get<uint64_t>();
              address = hex.str();
            }
            if (address.empty())
              throw std::runtime_error("Cannot enrich target without an address");
            auto result =
                job->runner.run(toolCommand(descriptor, *enrich,
                                            {{"addr", address},
                                             {"module", row.value("module", std::string())},
                                             {"limit", 1}},
                                            job->state.worktree, true),
                                {}, dir / "enrich.log");
            if (result.code)
              throw std::runtime_error("Target enrichment failed; inspect enrich.log");
            bool found = false;
            for (auto &line : split(result.output, '\n')) {
              auto value = trim(line);
              if (!value.empty() && value.front() == '{') {
                auto full = Json::parse(value);
                if (row.contains("id"))
                  full["id"] = row["id"];
                row = full;
                found = true;
                break;
              }
            }
            if (!found)
              throw std::runtime_error("No enriched context returned for target");
          }
      }
      auto prompt = dir / "instructions.txt", wl = dir / "worklist.jsonl",
           out = dir / "results.output";
      Repository r(job->runner, job->state.worktree, settings);
      std::string instructions = "TangOS Lite coordinated agent\nRole: " + job->state.spec.role +
                                 "\n" + r.agentHandoff() + "\n";
      auto project = descriptor.document.at("project");
      for (auto field : {"readFirst", "rules", "submitting", "knownWalls", "nearMissNote"})
        instructions += project.value(field, std::string()) + "\n";
      auto roleResource = FindResourceW(nullptr, MAKEINTRESOURCEW(211), RT_RCDATA);
      if (roleResource) {
        auto rules =
            Json::parse(std::string((char *)LockResource(LoadResource(nullptr, roleResource)),
                                    SizeofResource(nullptr, roleResource)));
        instructions += rules.value(job->state.spec.role, std::string()) + "\n";
      }
      instructions += "Keep changes in this isolated worktree. Do not commit or push. Never "
                      "include ROM data, extracted assets, credentials or excluded local files. ";
      if (settings.portOnly)
        instructions += "PORT ONLY: never modify src/. ";
      instructions += "Run declared verification tools. Report failures honestly. A successful "
                      "process exit is not proof of byte matching.\nTargets:\n" +
                      rows.dump(2);
      write(prompt, instructions);
      std::string list;
      for (auto &row : rows)
        list += row.dump() + "\n";
      write(wl, list);
      {
        std::lock_guard<std::mutex> lock(mutex);
        job->state.prompt = prompt;
        job->state.worklist = wl;
        job->state.log = dir / "driver.log";
        saveLocked();
      }
      if (job->state.spec.kind == "mcp" || !execute) {
        update("queued", "Batch assigned; Go drives it, or external MCP agent may pull it");
        break;
      }
      Command c;
      if (job->state.spec.kind == "cli") {
        c.cwd = job->state.worktree;
        for (auto &arg : tokenize(job->state.spec.cli))
          c.argv.push_back(arg == "{prompt}"     ? utf8(prompt.wstring())
                           : arg == "{worklist}" ? utf8(wl.wstring())
                           : arg == "{out}"      ? utf8(out.wstring())
                                                 : arg);
      } else {
        auto driver = descriptor.role("driver");
        if (!driver)
          throw std::runtime_error("No driver declared in tangos.json console.driver");
        c = toolCommand(descriptor, *driver,
                        {{"wl", utf8(wl.wstring())},
                         {"out", utf8(out.wstring())},
                         {"prompt", utf8(prompt.wstring())},
                         {"jobs", job->state.spec.jobs},
                         {"attempts", job->state.spec.attempts}},
                        job->state.worktree, true);
        // Existing Console Python drivers expose a standing INSTRUCTIONS block
        // but have no prompt option. Inject AGENTS instructions before main().
        bool hasPrompt = driver->command.find("{prompt}") != std::string::npos;
        for (auto &arg : driver->args)
          if (arg.name == "prompt")
            hasPrompt = true;
        if (!hasPrompt && c.argv.size() > 1 && fs::u8path(c.argv[1]).extension() == ".py") {
          auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(209), RT_RCDATA);
          if (!resource)
            throw std::runtime_error("Missing embedded Python agent adapter");
          auto adapter = dir / "agent_adapter.py";
          write(adapter, std::string((char *)LockResource(LoadResource(nullptr, resource)),
                                     SizeofResource(nullptr, resource)));
          c.argv.insert(c.argv.begin() + 1, utf8(adapter.wstring()));
        } else if (!hasPrompt)
          throw std::runtime_error(
              "Driver must accept {prompt} or use a supported Python instruction adapter");
        if (!job->state.spec.key.empty()) {
          if (!secrets.count(job->state.spec.key))
            throw std::runtime_error("Store " + job->state.spec.key + " in Settings > Key vault");
          c.environment["GLM_API_KEY"] = secrets.at(job->state.spec.key);
        }
        if (!job->state.spec.model.empty())
          c.environment["GLM_MODEL"] = job->state.spec.model;
        if (!job->state.spec.baseUrl.empty())
          c.environment["GLM_BASE_URL"] = job->state.spec.baseUrl;
        c.environment["GLM_DIALECT"] = job->state.spec.dialect;
      }
      for (auto &key : secrets)
        c.environment[key.first] = key.second;
      c.environment["TANGOS_EFFORT"] = job->state.spec.effort;
      c.environment["TANGOS_AGENT_INSTRUCTIONS"] = utf8(prompt.wstring());
      c.environment["TANGOS_PORT_ONLY"] = settings.portOnly ? "1" : "0";
      update("running", "Driving " + std::to_string(rows.size()) + " targets");
      auto result = job->runner.run(
          c,
          [&](const std::string &text) {
            auto clean = redact(text, secrets);
            {
              std::lock_guard<std::mutex> lock(mutex);
              job->state.lastLine = trim(clean).substr(0, 180);
            }
            if (events)
              events("[" + job->state.spec.name + "] " + clean);
          },
          job->state.log);
      audit(job);
      if (job->runner.isCancelled()) {
        update("cancelled", "Stopped; partial worktree and complete logs retained");
        break;
      }
      if (result.code)
        throw std::runtime_error("Driver exited " + std::to_string(result.code) +
                                 "; inspect driver.log");
      update("verifying", "Running independent repository gates");
      int gates = 0;
      for (auto &check : discoverChecks(job->state.worktree, settings))
        if (check.available && check.name != "ROM verification") {
          auto checked =
              job->runner.run(check.command, events,
                              dir / fs::u8path("verify-" + std::to_string(gates++) + ".log"));
          if (checked.code)
            throw std::runtime_error("Verification failed: " + check.name);
        }
      audit(job);
      {
        std::lock_guard<std::mutex> lock(mutex);
        job->state.completed += (int)rows.size();
        job->state.queue = Json::array();
        saveLocked();
      }
      update("review", gates ? "Repository checks passed; review diff before commit"
                             : "No verification gate available; unverified changes require review");
      bool again;
      {
        std::lock_guard<std::mutex> lock(mutex);
        again = job->state.spec.loop;
      }
      if (!again)
        break;
    } while (!job->runner.isCancelled());
  } catch (const std::exception &e) {
    update(job->runner.isCancelled() ? "cancelled" : "failed", e.what());
    if (events)
      events("[" + job->state.spec.name + "] " + e.what() + "\n");
  }
  CloseHandle(ownership);
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (job->state.spec.kind != "mcp" || job->state.phase != "running")
      job->active = false;
  }
}
std::string Fleet::review(const std::string &id) {
  std::shared_ptr<Job> job;
  {
    std::lock_guard<std::mutex> lock(mutex);
    job = jobs.at(id);
    if (job->active)
      throw std::runtime_error("Stop agent before reviewing");
  }
  job->runner.reset();
  audit(job);
  auto res = git(job->runner, job->state.worktree, {"add", "--all"});
  if (res.code)
    throw std::runtime_error(res.output);
  Repository r(job->runner, job->state.worktree, settings);
  return r.commitPreview();
}
void Fleet::commitReviewed(const std::string &id, const std::string &message,
                           const std::string &tree) {
  std::shared_ptr<Job> job;
  {
    std::lock_guard<std::mutex> lock(mutex);
    job = jobs.at(id);
    if (job->active)
      throw std::runtime_error("Agent is still running");
  }
  Repository r(job->runner, job->state.worktree, settings);
  if (r.safetyIndex() != tree)
    throw std::runtime_error("Agent index changed; review again");
  auto c = r.action("Commit staged", "", "", message);
  auto res = job->runner.run(c, events);
  if (res.code)
    throw std::runtime_error("Commit failed");
  std::lock_guard<std::mutex> lock(mutex);
  job->state.phase = "committed";
  job->state.detail = "Reviewed commit on " + job->state.branch + "; merge/push using Git tools";
  saveLocked();
}
Json Fleet::takeBatch(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex);
  auto job = jobs.at(id);
  if (job->state.spec.kind != "mcp" || job->state.phase != "queued")
    return {{"status", "empty"}};
  job->state.phase = "running";
  job->state.detail = "External agent owns batch";
  saveLocked();
  job->active = true;
  return {{"status", "assigned"},
          {"worktree", utf8(job->state.worktree.wstring())},
          {"instructions", read(job->state.prompt)},
          {"targets", job->state.queue}};
}
void Fleet::finishBatch(const std::string &id) {
  std::shared_ptr<Job> job;
  {
    std::lock_guard<std::mutex> lock(mutex);
    job = jobs.at(id);
    if (job->state.spec.kind != "mcp" || job->state.phase != "running")
      throw std::runtime_error("No active external batch");
  }
  std::lock_guard<std::mutex> taskLock(job->externalMutex);
  job->externalTask = true;
  try {
    audit(job);
    int gates = 0;
    for (auto &check : discoverChecks(job->state.worktree, settings))
      if (check.available && check.name != "ROM verification") {
        auto result = job->runner.run(check.command, events,
                                      directory / fs::u8path(id) /
                                          ("mcp-verify-" + std::to_string(gates++) + ".log"));
        if (result.code)
          throw std::runtime_error("External verification failed: " + check.name);
      }
    audit(job);
    std::lock_guard<std::mutex> lock(mutex);
    job->state.completed += (int)job->state.queue.size();
    job->state.queue = Json::array();
    job->state.phase = "review";
    job->state.detail = gates ? "External batch independently verified; review required"
                              : "External batch unverified; no gate available";
    saveLocked();
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lock(mutex);
    job->state.phase = job->runner.isCancelled() ? "cancelled" : "failed";
    job->state.detail = e.what();
    saveLocked();
    job->active = false;
    job->externalTask = false;
    throw;
  }
  job->active = false;
  job->externalTask = false;
}
Result Fleet::runTool(const std::string &id, const std::string &tool, const Json &args) {
  std::shared_ptr<Job> job;
  {
    std::lock_guard<std::mutex> lock(mutex);
    job = jobs.at(id);
    if (job->state.spec.kind != "mcp" || job->state.worktree.empty() ||
        job->state.phase != "running")
      throw std::runtime_error("No active external-agent batch");
  }
  std::lock_guard<std::mutex> taskLock(job->externalMutex);
  job->externalTask = true;
  try {
    auto c = toolCommand(descriptor, descriptor.tool(tool), args, job->state.worktree, true, false);
    for (auto &entry : vault.values())
      c.environment[entry.first] = entry.second;
    auto result = job->runner.run(c, {}, directory / fs::u8path(id) / (uniqueId() + "-tool.log"));
    audit(job);
    job->externalTask = false;
    if (job->runner.isCancelled()) {
      std::lock_guard<std::mutex> lock(mutex);
      job->active = false;
      job->state.phase = "cancelled";
      saveLocked();
    }
    return result;
  } catch (...) {
    job->externalTask = false;
    throw;
  }
}
} // namespace lite
