#include "fleet.h"
#include "backend.h"
#include "activity.h"
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
          {"loop", s.loop},       {"provider", s.provider}};
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
          {"results", utf8(s.results.wstring())},
          {"queue", s.queue},
          {"assigned", s.assigned},
          {"observed", s.observed},
          {"completed", s.completed},
          {"total", s.total}};
}
AgentState parseAgent(const Json &j) {
  AgentState s;
  auto v = j.at("spec");
  s.spec.name = v.at("name");
  s.spec.kind = v.value("kind", std::string("api"));
  s.spec.role = v.value("role", std::string("Unassigned"));
  s.spec.provider = v.value("provider", std::string());
  s.spec.effort = v.value("effort", std::string());
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
  s.results = fs::u8path(j.value("results", std::string()));
  s.queue = j.value("queue", Json::array());
  s.assigned = j.value("assigned", Json::array());
  s.observed = j.value("observed", Json::array());
  s.completed = j.value("completed", 0);
  s.total = j.value("total", 0);
  if (!s.queue.is_array() || !s.assigned.is_array() || !s.observed.is_array() || s.id.empty() ||
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
    batchBook.restore(persisted.value("batchBook", Json::object()));
    for (auto &j : persisted.at("agents")) {
      auto job = std::make_shared<Job>();
      job->state = parseAgent(j);
      if (job->state.results.empty())
        job->state.results = directory / fs::u8path(job->state.id) / "results.output";
      for (auto &path :
           {job->state.log, job->state.prompt, job->state.worklist, job->state.results})
        if (!path.empty())
          confinedPath(directory, utf8(path.wstring()));
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
    if (!persisted.contains("batchBook")) {
      bool migrated = false;
      for (auto &entry : jobs)
        if (!entry.second->state.queue.empty()) {
          batchBook.add(uniqueId(), entry.first, entry.second->state.spec.name,
                        entry.second->state.queue, std::time(nullptr) * int64_t(1000),
                        "Recovered queue", "");
          migrated = true;
        }
      if (migrated)
        saveLocked();
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
  j["batchBook"] = batchBook.serialize();
  auto temp = directory / "fleet.tmp";
  write(temp, j.dump(2));
  DWORD error = ERROR_SUCCESS;
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (MoveFileExW(temp.c_str(), (directory / "fleet.json").c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return;
    error = GetLastError();
    if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION &&
        error != ERROR_ACCESS_DENIED)
      break;
    Sleep(50);
  }
  throw std::runtime_error(
      "Cannot save fleet state: " + utf8((directory / "fleet.json").wstring()) +
      " (Windows error " + std::to_string(error) + ")");
}
static std::string agentNameKey(std::string name) {
  name = trim(name);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return name;
}
std::string Fleet::add(AgentSpec spec) {
  spec.name = trim(spec.name);
  auto job = std::make_shared<Job>();
  job->state.spec = std::move(spec);
  job->state.id = uniqueId();
  parseAgent(agentJson(job->state));
  std::lock_guard<std::mutex> lock(mutex);
  for (auto &entry : jobs)
    if (agentNameKey(entry.second->state.spec.name) == agentNameKey(job->state.spec.name))
      throw std::runtime_error("Agent name already exists; choose a unique name");
  jobs[job->state.id] = job;
  saveLocked();
  return job->state.id;
}
void Fleet::configure(const std::string &id, AgentSpec spec) {
  std::lock_guard<std::mutex> lock(mutex);
  auto j = jobs.at(id);
  if (j->active)
    throw std::runtime_error("Stop agent before changing configuration");
  spec.name = trim(spec.name);
  for (auto &entry : jobs)
    if (entry.first != id && agentNameKey(entry.second->state.spec.name) == agentNameKey(spec.name))
      throw std::runtime_error("Agent name already exists; choose a unique name");
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
  batchBook.clearAgent(id);
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
void Fleet::enqueue(const std::string &id, const Json &rows, const std::string &title,
                    const std::string &prompt) {
  if (!rows.is_array())
    throw std::runtime_error("Worklist must be an array");
  auto claims = backend("claims.read", Json::object());

  std::lock_guard<std::mutex> lock(mutex);
  auto job = id.empty() ? std::shared_ptr<Job>() : jobs.at(id);
  std::set<std::string> taken;
  for (auto &item : jobs)
    for (auto &row : item.second->state.queue)
      taken.insert(batchTarget(row));
  for (auto &batch : batchBook.snapshot())
    if (batch.at("agentId") == "" && batch.at("status") != "done")
      for (auto &row : batch.at("items"))
        if (!row.value("worked", false) && !row.value("removed", false))
          taken.insert(batchTarget(row));
  Json fresh = Json::array();
  for (auto &row : rows) {
    if (heldTarget(row, claims) || row.value("matched", false) || exemptTarget(row) ||
        (row.contains("claim") && !row["claim"].is_null() && row["claim"] != false))
      throw std::runtime_error("Target is already matched, exempt or externally claimed");
    auto ref = batchTarget(row);
    if (ref.empty() || ref == ":")
      throw std::runtime_error("Work target needs id, ref or name");
    if (!taken.insert(ref).second)
      throw std::runtime_error("Target already claimed: " + ref);
    fresh.push_back(row);
  }
  batchBook.add(uniqueId(), id, job ? job->state.spec.name : "Unassigned", fresh,
                std::time(nullptr) * int64_t(1000), title, prompt);
  if (job) {
    for (auto &row : fresh)
      job->state.queue.push_back(row);
    job->state.total = job->state.completed + (int)job->state.queue.size();
  }
  saveLocked();
}
Json Fleet::batches() const {
  std::lock_guard<std::mutex> lock(mutex);
  return batchBook.snapshot();
}
Json Fleet::draft() const {
  std::lock_guard<std::mutex> lock(mutex);
  return batchBook.draft();
}
void Fleet::saveDraft(const Json &draft) {
  std::lock_guard<std::mutex> lock(mutex);
  batchBook.setDraft(draft);
  saveLocked();
}
void Fleet::enqueueDraft(const std::string &id) {
  auto staged = draft();
  if (staged.at("items").empty())
    throw std::runtime_error("Draft has no targets");
  enqueue(id, staged.at("items"), staged.value("title", std::string()),
          staged.value("prompt", std::string()));
  // Do not erase a draft edited concurrently while claims were checked.
  std::lock_guard<std::mutex> lock(mutex);
  if (batchBook.draft() == staged)
    batchBook.setDraft({{"title", "Batch draft"}, {"prompt", ""}, {"items", Json::array()}});
  saveLocked();
}
void Fleet::handoff(const std::string &batchId, const std::string &destination) {
  auto claims = backend("claims.read", Json::object());
  std::lock_guard<std::mutex> lock(mutex);
  auto batches = batchBook.snapshot();
  auto found = std::find_if(batches.begin(), batches.end(),
                            [&](const Json &b) { return b.at("id") == batchId; });
  if (found == batches.end() || found->at("status") != "queued")
    throw std::runtime_error("Select a queued batch");
  auto source = found->at("agentId").get<std::string>();
  if (source == destination)
    return;
  auto from = source.empty() ? std::shared_ptr<Job>() : jobs.at(source);
  auto to = destination.empty() ? std::shared_ptr<Job>() : jobs.at(destination);
  if ((from && from->active) || (to && to->active))
    throw std::runtime_error("Stop both agents before handing off work");
  Json rows = Json::array();
  std::set<std::string> moving;
  for (auto &row : found->at("items"))
    if (!row.value("worked", false) && !row.value("removed", false)) {
      if (heldTarget(row, claims) || row.value("matched", false) || exemptTarget(row))
        throw std::runtime_error("A handoff target is now claimed, matched or exempt");
      rows.push_back(row);
      moving.insert(batchTarget(row));
    }
  for (auto &job : jobs)
    if (job.first != source)
      for (auto &row : job.second->state.queue)
        if (moving.count(batchTarget(row)))
          throw std::runtime_error("Handoff would duplicate a reserved target");
  if (from) {
    Json remaining = Json::array();
    for (auto &row : from->state.queue)
      if (!moving.count(batchTarget(row)))
        remaining.push_back(row);
    from->state.queue = remaining;
    from->state.assigned = Json::array();
    from->state.total = from->state.completed + int(remaining.size());
  }
  if (to) {
    for (auto &row : rows)
      to->state.queue.push_back(row);
    to->state.total = to->state.completed + int(to->state.queue.size());
  }
  batchBook.assign(batchId, destination, to ? to->state.spec.name : "Unassigned");
  saveLocked();
}
Json Fleet::generateDraft(const std::string &role, int count, Runner &runner, Sink progress) {
  if (count < 1 || count > 500)
    throw std::runtime_error("Draft count must be 1 to 500");
  if (role != "Hard matcher" && role != "Drafter" && role != "Refiner" && role != "Random")
    throw std::runtime_error("Choose a supported generation role");
  auto tool = descriptor.role(role == "Refiner"  ? "refineScheduler"
                              : role == "Random" ? "randomScheduler"
                                                 : "scheduler");
  if (!tool)
    tool = descriptor.role("scheduler");
  if (!tool)
    throw std::runtime_error("No scheduler declared; use Viewer cart or import draft JSON");
  std::set<std::string> taken;
  {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto &b : batchBook.snapshot())
      if (b.at("status") != "done")
        for (auto &row : b.at("items"))
          if (!row.value("worked", false) && !row.value("removed", false))
            taken.insert(batchTarget(row));
    for (auto &job : jobs)
      for (auto &row : job.second->state.queue)
        taken.insert(batchTarget(row));
  }
  auto out = directory / (uniqueId() + "-draft.jsonl");
  auto worktree = directory / "generation-worktrees" / uniqueId();
  fs::create_directories(worktree.parent_path());
  auto created = runner.run(
      {{"git", "worktree", "add", "--detach", utf8(worktree.wstring()), "HEAD"}, repository},
      progress, directory / (uniqueId() + "-generation-setup.log"));
  if (created.code)
    throw std::runtime_error("Cannot create isolated generation worktree; inspect setup log");
  auto result = runner.run(toolCommand(descriptor, *tool,
                                       {{"role", role},
                                        {"limit", count + int(taken.size()) + 16},
                                        {"count", count + int(taken.size()) + 16},
                                        {"out", utf8(out.wstring())}},
                                       worktree, true),
                           progress, directory / (uniqueId() + "-draft.log"));
  if (result.code || runner.isCancelled())
    throw std::runtime_error("Draft generation failed or cancelled; inspect the generation log");
  Repository audit(runner, worktree, settings);
  for (auto &change :
       parseStatus(audit.git({"status", "--porcelain=v1", "-z", "--untracked-files=all"}))) {
    if (!blockedPath(change.path, settings).empty())
      throw std::runtime_error("Generation changed a protected path in its isolated worktree: " +
                               change.path);
  }
  auto claims = backend("claims.read", Json::object());
  Json items = Json::array();
  for (auto &line : split(fs::exists(out) ? read(out) : result.output, '\n')) {
    auto text = trim(line);
    if (text.empty() || text.front() != '{')
      continue;
    auto row = Json::parse(text);
    auto key = batchTarget(row);
    if (key.empty() || key == ":" || row.value("matched", false) || exemptTarget(row) ||
        heldTarget(row, claims))
      continue;
    if (taken.insert(key).second)
      items.push_back(row);
    if (items.size() >= size_t(count))
      break;
  }
  if (items.empty())
    throw std::runtime_error("No available targets; refresh the repository or choose another role");
  Json draft = {{"title", role + " draft"},
                {"prompt", "Follow repository AGENTS.md; verify every result independently."},
                {"items", items}};
  saveDraft(draft);
  return draft;
}
void Fleet::clearDoneBatches() {
  std::lock_guard<std::mutex> lock(mutex);
  batchBook.clearDone();
  saveLocked();
}
void Fleet::editBatch(const std::string &batchId, int direction, bool remove) {
  std::lock_guard<std::mutex> lock(mutex);
  auto history = batchBook.snapshot();
  auto found = std::find_if(history.begin(), history.end(),
                            [&](const Json &b) { return b.at("id") == batchId; });
  if (found == history.end())
    throw std::runtime_error("Select a batch");
  auto id = found->at("agentId").get<std::string>();
  auto jobIt = jobs.find(id);
  if (jobIt != jobs.end()) {
    auto &job = jobIt->second;
    if (job->active)
      throw std::runtime_error("Stop agent before editing its batches");
    if (remove && found->at("status") != "done") {
      std::set<std::string> removed;
      for (auto &row : found->at("items"))
        removed.insert(batchTarget(row));
      Json remaining = Json::array();
      for (auto &row : job->state.queue)
        if (!removed.count(batchTarget(row)))
          remaining.push_back(row);
      job->state.queue = remaining;
    }
  }
  if (remove)
    batchBook.remove(batchId);
  else
    batchBook.reorder(batchId, direction);
  if (jobIt != jobs.end()) {
    auto &state = jobIt->second->state;
    if (!remove) {
      std::map<std::string, Json> queued;
      for (auto &row : state.queue)
        queued[batchTarget(row)] = row;
      Json ordered = Json::array();
      for (auto &batch : batchBook.snapshot())
        if (batch.at("agentId") == id && batch.at("status") != "done")
          for (auto &row : batch.at("items")) {
            auto key = batchTarget(row);
            auto it = queued.find(key);
            if (it != queued.end()) {
              ordered.push_back(it->second);
              queued.erase(it);
            }
          }
      for (auto &row : state.queue)
        if (queued.erase(batchTarget(row)))
          ordered.push_back(row);
      state.queue = ordered;
    }
    state.assigned = Json::array();
    state.total = state.completed + int(state.queue.size());
    if (state.phase == "queued")
      state.phase = "idle";
  }
  saveLocked();
}
void Fleet::clear(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex);
  auto job = jobs.at(id);
  if (job->active)
    throw std::runtime_error("Stop agent before clearing its queue");
  batchBook.clearAgent(id);
  job->state.queue = Json::array();
  job->state.assigned = Json::array();
  if (job->state.phase == "queued")
    job->state.phase = "idle";
  job->state.total = job->state.completed;
  saveLocked();
}
void Fleet::editQueue(const std::string &id, size_t index, int direction, bool remove) {
  std::lock_guard<std::mutex> lock(mutex);
  auto job = jobs.at(id);
  if (job->active)
    throw std::runtime_error("Stop the agent before editing its queue");
  if (index >= job->state.queue.size())
    throw std::runtime_error("Select a queued target");
  if (remove)
    job->state.queue.erase(index);
  else {
    if (direction != -1 && direction != 1)
      throw std::runtime_error("Queue direction must be up or down");
    auto next = (int64_t)index + direction;
    if (next >= 0 && next < (int64_t)job->state.queue.size())
      std::swap(job->state.queue[index], job->state.queue[(size_t)next]);
  }
  job->state.assigned = Json::array();
  if (job->state.phase == "queued")
    job->state.phase = "idle";
  job->state.total = job->state.completed + (int)job->state.queue.size();
  batchBook.reconcile(id, job->state.queue);
  saveLocked();
}
static Json attemptedTargets(const Json &assigned, const Json &results) {
  Json records;
  if (results.is_array())
    records = results;
  else if (results.is_object() && results.contains("results"))
    records = results["results"].is_array() ? results["results"] : Json::array();
  else
    return assigned; // Legacy drivers and explicit MCP completion have no per-target ledger.
  std::set<std::string> attempted;
  for (auto &record : records) {
    if (!record.is_object())
      continue;
    for (auto field : {"name", "ref", "id", "functionId"})
      if (record.contains(field) && record[field].is_string() &&
          !record[field].get<std::string>().empty())
        attempted.insert(record[field].get<std::string>());
  }
  Json completed = Json::array();
  for (auto &row : assigned) {
    bool reached = false;
    for (auto field : {"name", "ref", "id", "functionId"})
      if (row.contains(field) && row[field].is_string() &&
          attempted.count(row[field].get<std::string>()))
        reached = true;
    if (reached)
      completed.push_back(row);
  }
  return completed;
}
static Json readDriverResults(const fs::path &path) {
  if (!fs::exists(path))
    return Json();
  if (fs::file_size(path) > 32 * 1024 * 1024)
    throw std::runtime_error("Agent results exceed 32 MiB; complete logs retained");
  return Json::parse(read(path), nullptr, false);
}
static void consumeBatch(AgentState &state) {
  std::set<std::string> finished;
  for (auto &row : state.assigned)
    finished.insert(batchTarget(row));
  Json remaining = Json::array();
  for (auto &row : state.queue)
    if (!finished.count(batchTarget(row)))
      remaining.push_back(row);
  state.completed += (int)state.assigned.size();
  state.queue = std::move(remaining);
  state.assigned = Json::array();
  state.total = state.completed + (int)state.queue.size();
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
  batchBook.park(id, "Stopped; partial work and complete logs retained");
  job->runner.cancel();
  if (job->requestRunner)
    job->requestRunner->cancel();
  job->state.spec.loop = false;
  if (job->state.spec.kind == "mcp" && !job->externalTask) {
    job->active = false;
    job->state.phase = "cancelled";
    job->state.detail = "External batch revoked; partial work retained";
    saveLocked();
  }
}
void Fleet::stopExternal() {
  std::lock_guard<std::mutex> lock(mutex);
  for (auto &item : jobs) {
    auto &job = item.second;
    if (job->state.spec.kind != "mcp")
      continue;
    job->runner.cancel();
    if (job->requestRunner)
      job->requestRunner->cancel();
    batchBook.park(item.first, "Stopped; partial work and complete logs retained");
    job->state.spec.loop = false;
    if (!job->externalTask && job->active) {
      job->active = false;
      job->state.phase = "cancelled";
      job->state.detail = "MCP server stopped; partial work and logs retained";
    }
  }
}
void Fleet::stopAll() {
  std::lock_guard<std::mutex> lock(mutex);
  for (auto &item : jobs) {
    item.second->runner.cancel();
    if (item.second->requestRunner)
      item.second->requestRunner->cancel();
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
  job->executionRole.clear();
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
    if (settings.portOnly)
      throw std::runtime_error(
          "Landing decomp results may change src/. Disable port-only mode explicitly first.");
    if (job->active)
      throw std::runtime_error("Stop agent before landing results");
    if (!tool)
      throw std::runtime_error("No console.land tool declared by this repository");
    if (!fs::exists(job->state.results))
      throw std::runtime_error("No retained driver result exists for this agent");
  }
  if (job->worker.joinable())
    job->worker.join();
  job->runner.reset();
  job->active = true;
  job->worker = std::thread([this, job, tool] {
    auto update = [&](const std::string &phase, const std::string &detail) {
      std::lock_guard<std::mutex> lock(mutex);
      job->state.phase = phase;
      if (phase == "failed" || phase == "cancelled")
        batchBook.park(job->state.id, detail);
      job->state.detail = detail;
      saveLocked();
    };
    try {
      auto dir = directory / job->state.id;
      update("landing", "Applying declared land tool in isolated worktree");
      auto command = toolCommand(descriptor, *tool,
                                 {{"out", utf8(job->state.results.wstring())},
                                  {"wl", utf8(job->state.worklist.wstring())},
                                  {"no_claims", true}},
                                 job->state.worktree, true, true);
      auto landLog = dir / "land.log";
      {
        std::lock_guard<std::mutex> lock(mutex);
        job->state.log = landLog;
      }
      auto result = job->runner.run(command, events, landLog);
      if (result.code)
        throw std::runtime_error("Land tool failed; inspect land.log");
      audit(job);
      update("verifying", "Checking landed changes independently");
      int gates = 0;
      for (auto &check : discoverChecks(job->state.worktree, settings)) {
        if (!check.available || check.name == "ROM verification")
          continue;
        auto result = job->runner.run(check.command, events,
                                      dir / ("land-verify-" + std::to_string(gates++) + ".log"));
        if (result.code)
          throw std::runtime_error("Landed changes failed: " + check.name);
      }
      audit(job);
      update("review", gates ? "Landed changes passed checks; review complete diff"
                             : "Landed changes unverified; no available gates");
    } catch (const std::exception &e) {
      try {
        update(job->runner.isCancelled() ? "cancelled" : "failed", e.what());
      } catch (...) {
      }
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
std::string Fleet::chooseRole(const std::shared_ptr<Job> &job) {
  auto role = job->state.spec.role;
  if (role == "Unassigned") {
    role = "Hard matcher";
    auto stats = backend("stats.get", Json::object()).value(job->state.id, Json::object());
    auto recent = stats.value("recent", Json::array());
    int hits = 0;
    for (auto &v : recent)
      if (v == true)
        ++hits;
    auto db = confinedPath(repository, descriptor.database);
    auto rung = stats.value("adaptiveRole", role);
    if (fs::exists(db))
      role = adaptiveRole(rung, (int)recent.size(), hits, poolDifficulty(parseAtlas(read(db))));
    if (role != rung && rung != "Refiner")
      Backend(repository, directory.parent_path().parent_path(), settings)
          .resetRecent(job->state.id);
    job->runtimeRole = rung == "Refiner" ? rung : role;
    role = automaticRole({{"name", job->state.spec.name},
                          {"provider", job->state.spec.provider},
                          {"roles", Json::array()},
                          {"stats", stats},
                          {"hiddenRole", job->runtimeRole}})
               .at("role");
  } else
    job->runtimeRole = role;
  job->executionRole = role;
  return role;
}
Json Fleet::schedule(const std::shared_ptr<Job> &job, const fs::path &cwd) {
  auto role = chooseRole(job);
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
        taken.insert(batchTarget(row));
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
    if (taken.insert(batchTarget(row)).second)
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
  // Cancellation stops execution, but partial changes still need a safety audit.
  Runner auditRunner;
  Repository r(auditRunner, job->state.worktree, settings);
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
      auto content = read(p);
      if (classifySource(content) == "transcribed")
        throw std::runtime_error("ASM transcription refused: " + change.path);
      auto reason = blockedBlob(content);
      if (!reason.empty())
        throw std::runtime_error("Agent blob blocked: " + change.path + " - " + reason);
    }
  }
}
void Fleet::drive(const std::shared_ptr<Job> &job, bool execute) {
  auto update = [&](const std::string &phase, const std::string &detail) {
    std::lock_guard<std::mutex> lock(mutex);
    job->state.phase = phase;
    if (phase == "failed" || phase == "cancelled")
      batchBook.park(job->state.id, detail);
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
  struct OwnershipGuard {
    HANDLE h;
    ~OwnershipGuard() { CloseHandle(h); }
  } ownershipGuard{ownership};
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
    int quickFailStreak = 0;
    do {
      update("scheduling", "Preparing instructions and worklist");
      Json rows;
      {
        std::lock_guard<std::mutex> lock(mutex);
        rows = Json::array();
        for (auto &row : job->state.queue) {
          if (rows.size() >= (size_t)job->state.spec.count)
            break;
          rows.push_back(row);
        }
      }
      if (rows.empty())
        rows = schedule(job, job->state.worktree);
      else
        chooseRole(job);
      if (job->runner.isCancelled()) {
        update("cancelled", "Stopped after scheduling; driver was not launched");
        break;
      }
      for (auto &row : rows)
        row["ref"] = batchTarget(row);
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
                full["ref"] = row["ref"];
                row = full;
                found = true;
                break;
              }
            }
            if (!found)
              throw std::runtime_error("No enriched context returned for target");
          }
      }
      auto runId = uniqueId();
      auto prompt = dir / fs::u8path(runId + "-instructions.txt"),
           wl = dir / fs::u8path(runId + "-worklist.jsonl"),
           out = dir / fs::u8path(runId + "-results.output");
      Repository r(job->runner, job->state.worktree, settings);
      auto executionRole =
          job->state.spec.role == "Unassigned"
              ? (job->executionRole.empty() ? std::string("Hard matcher") : job->executionRole)
              : job->state.spec.role;
      Repository selected(job->runner, repository, settings);
      std::string instructions =
          "TangOS Lite coordinated agent\nRole: " + executionRole + "\n" + r.agentHandoff() +
          "\nCurrent selected-checkout coordination instructions (including local updates):\n" +
          selected.agentHandoff() + "\n";
      auto project = descriptor.document.at("project");
      for (auto field : {"readFirst", "rules", "submitting", "knownWalls", "nearMissNote"})
        instructions += project.value(field, std::string()) + "\n";
      auto roleResource = FindResourceW(nullptr, MAKEINTRESOURCEW(211), RT_RCDATA);
      if (roleResource) {
        auto rules =
            Json::parse(std::string((char *)LockResource(LoadResource(nullptr, roleResource)),
                                    SizeofResource(nullptr, roleResource)));
        instructions += rules.value(executionRole, std::string()) + "\n";
      }
      instructions += "Keep changes in this isolated worktree. Do not commit or push. Never "
                      "include ROM data, extracted assets, credentials or excluded local files. ";
      if (settings.portOnly)
        instructions += "PORT ONLY: never modify src/. ";
      instructions += "Run declared verification tools. Report failures honestly. A successful "
                      "process exit is not proof of byte matching.\nTargets:\n" +
                      rows.dump(2);
      {
        std::lock_guard<std::mutex> lock(mutex);
        instructions += batchBook.instructions(id, rows);
      }
      auto policy = backend("preferences.get", Json::object());
      auto delegation = driverPolicy({{"name", job->state.spec.name},
                                      {"provider", job->state.spec.provider},
                                      {"jobs", job->state.spec.jobs}},
                                     policy, rows.size());
      if (execute && policy.value("safeMode", false))
        throw std::runtime_error("User safe mode prohibits agent writes");
      instructions += "\n\nUser delegation policy: " +
                      (policy.value("useAgents", false)
                           ? std::string("Delegation enabled: put approximately ") +
                                 std::to_string(delegation.at("functionsPerAgent").get<int>()) +
                                 " functions in EACH sub-agent (this batch suggests about " +
                                 std::to_string(delegation.at("subAgents").get<int>()) +
                                 "). Do not spawn one sub-agent per function: it multiplies setup "
                                 "and token cost."
                           : std::string("Do not spawn or delegate to additional agents."));
      instructions +=
          "\nMatching policy: " +
          (policy.value("allowNearMiss", true)
               ? std::string("Near-miss tips allowed. ")
               : std::string("Do not use near-miss tips or nearmiss_* tools. ")) +
          (policy.value("allowGhidra", false) ? std::string("Ghidra drafts allowed. ")
                                              : std::string("Do not use Ghidra drafts. "));
      write(prompt, instructions);
      write(out, "");
      std::string list;
      for (auto &row : rows)
        list += row.dump() + "\n";
      write(wl, list);
      {
        std::lock_guard<std::mutex> lock(mutex);
        job->state.assigned = rows;
        job->state.observed = Json::array();
        if (execute && job->state.spec.kind != "mcp")
          batchBook.activate(id, rows, std::time(nullptr) * int64_t(1000));
        job->state.prompt = prompt;
        job->state.worklist = wl;
        job->state.results = out;
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
                         {"jobs", delegation.at("jobs")},
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
      if (policy.value("safeMode", false))
        throw std::runtime_error("User safe mode prohibits agent writes");
      c.environment["TANGOS_USE_AGENTS"] = policy.value("useAgents", false) ? "1" : "0";
      c.environment["TANGOS_AGENT_FANOUT"] =
          std::to_string(delegation.at("functionsPerAgent").get<int>());
      c.environment["TANGOS_WORKERS"] = std::to_string(delegation.at("jobs").get<int>());
      c.environment["TANGOS_ALLOW_NEAR_MISS"] = policy.value("allowNearMiss", true) ? "1" : "0";
      c.environment["TANGOS_ALLOW_GHIDRA"] = policy.value("allowGhidra", false) ? "1" : "0";
      auto effort = effortPolicy({{"name", job->state.spec.name},
                                  {"provider", job->state.spec.provider},
                                  {"effort", job->state.spec.effort}});
      auto family = effort.at("family").get<std::string>();
      auto choice = effort.at("current").get<std::string>();
      c.environment["TANGOS_EFFORT"] =
          family == "GLM" || family == "DeepSeek" || family == "Nemotron" ? "off" : choice;
      if (job->state.spec.kind == "api" && family == "DeepSeek")
        c.environment["GLM_MODEL"] = choice == "chat" ? "deepseek-chat" : "deepseek-reasoner";
      if (job->state.spec.kind == "api" && family == "Requesty")
        c.environment["GLM_MODEL"] = choice;
      c.environment["TANGOS_AGENT_INSTRUCTIONS"] = utf8(prompt.wstring());
      c.environment["TANGOS_PORT_ONLY"] = settings.portOnly ? "1" : "0";
      auto connections = backend("connections.get", Json::object());
      if (connections.contains("claims") && connections["claims"].value("enabled", false)) {
        auto holds = backend("claims.read", {{"connection", "claims"}});
        for (auto &row : rows)
          if (heldTarget(row, holds))
            throw std::runtime_error("A remote worker holds this target; choose unclaimed work");
      }
      auto leaseBackend =
          Backend(repository, directory.parent_path().parent_path(), settings, secrets);
      auto lease =
          leaseBackend.reserve(rows, job->state.spec.name, [job] { job->runner.cancel(); });
      update("running", "Driving " + std::to_string(rows.size()) + " targets");
      auto runStarted = GetTickCount64();
      c.activityTool = job->state.spec.kind == "cli" ? "cli" : "drive";
      c.activityLabel = "Drive " + job->state.spec.name;
      c.activityAgent = job->state.spec.name;
      c.activityRole = job->executionRole;
      c.activityRepository = repository;
      bool exhaustionSignal = false;
      std::string usageWindow;
      auto result = job->runner.run(
          c,
          [&](const std::string &text) {
            auto clean = redact(text, secrets);
            if (job->state.spec.kind == "api" && !exhaustionSignal) {
              usageWindow += clean;
              exhaustionSignal = driverUsage(usageWindow + "x", true, 0, 0).at("stopped");
              if (usageWindow.size() > 128)
                usageWindow.erase(0, usageWindow.size() - 128);
            }
            {
              std::lock_guard<std::mutex> lock(mutex);
              job->state.lastLine = trim(clean).substr(0, 180);
            }
            if (events)
              events("[" + job->state.spec.name + "] " + clean);
          },
          job->state.log);
      audit(job);
      if (lease)
        lease->check();
      if (job->runner.isCancelled()) {
        update("cancelled", "Stopped; partial worktree and complete logs retained");
        break;
      }
      auto results = readDriverResults(out);
      if (job->state.spec.kind == "api") {
        auto usage = driverUsage(exhaustionSignal ? "402" : usageWindow, productiveDriver(results),
                                 GetTickCount64() - runStarted, quickFailStreak);
        quickFailStreak = usage.at("streak");
        if (usage.at("stopped") == true) {
          {
            std::lock_guard<std::mutex> lock(mutex);
            job->state.spec.loop = false;
            batchBook.park(id, usage.at("reason"));
          }
          update("exhausted", usage.at("reason"));
          break;
        }
      }
      if (result.code)
        throw std::runtime_error("Driver exited " + std::to_string(result.code) +
                                 "; inspect driver.log");
      if (policy.value("autoLand", false)) {
        if (settings.portOnly)
          throw std::runtime_error("Automatic decomp landing is prohibited in port-only mode");
        auto landTool = descriptor.role("land");
        if (!landTool)
          throw std::runtime_error("Auto-land enabled but no console.land tool declared");
        update("landing", "User-enabled auto-land in isolated worktree");
        auto command = toolCommand(descriptor, *landTool,
                                   {{"out", utf8(out.wstring())},
                                    {"wl", utf8(job->state.worklist.wstring())},
                                    {"no_claims", true}},
                                   job->state.worktree, true, true);
        auto landed = job->runner.run(command, events, dir / "auto-land.log");
        if (landed.code)
          throw std::runtime_error("Auto-land failed; inspect auto-land.log");
        audit(job);
      }
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
      Backend(repository, directory.parent_path().parent_path(), settings)
          .recordAgent(job->state.id, out, "review", gates,
                       job->state.spec.role == "Unassigned" ? job->runtimeRole : std::string());
      bool madeProgress;
      {
        std::lock_guard<std::mutex> lock(mutex);
        job->state.assigned = attemptedTargets(job->state.assigned, results);
        madeProgress = !job->state.assigned.empty();
        batchBook.complete(job->state.id, job->state.assigned);
        consumeBatch(job->state);
        saveLocked();
      }
      if (!madeProgress) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          job->state.spec.loop = false;
          batchBook.park(job->state.id,
                         "Driver results report no attempted targets; pending work retained");
        }
        update("partial", "No assigned targets reached; inspect results and restart manually");
        break;
      }
      update("review", gates ? "Repository checks passed; review diff before commit"
                             : "No verification gate available; unverified changes require review");
      bool again;
      {
        std::lock_guard<std::mutex> lock(mutex);
        again = job->state.spec.loop || !job->state.queue.empty();
      }
      if (!again)
        break;
    } while (!job->runner.isCancelled());
  } catch (const std::exception &e) {
    update(job->runner.isCancelled() ? "cancelled" : "failed", e.what());
    if (events)
      events("[" + job->state.spec.name + "] " + e.what() + "\n");
  }
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
  if (backend("preferences.get", Json::object()).value("safeMode", false))
    throw std::runtime_error("User safe mode prohibits claiming an agent batch");
  job->state.phase = "running";
  job->state.detail = "External agent owns batch";
  batchBook.activate(id, job->state.assigned, std::time(nullptr) * int64_t(1000));
  saveLocked();
  job->active = true;
  return {{"status", "assigned"},
          {"worktree", utf8(job->state.worktree.wstring())},
          {"instructions", read(job->state.prompt)},
          {"targets", job->state.assigned.empty() ? job->state.queue : job->state.assigned},
          {"resultsPath", utf8(job->state.results.wstring())}};
}
void Fleet::finishBatch(const std::string &id, Runner *request) {
  std::shared_ptr<Job> job;
  {
    std::lock_guard<std::mutex> lock(mutex);
    job = jobs.at(id);
    if (job->state.spec.kind != "mcp" || job->state.phase != "running")
      throw std::runtime_error("No active external batch");
  }
  std::lock_guard<std::mutex> taskLock(job->externalMutex);
  auto &process = request ? *request : job->runner;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (job->state.phase != "running" || job->runner.isCancelled() || process.isCancelled())
      throw std::runtime_error("External batch stopped or request cancelled");
    job->externalTask = true;
    job->requestRunner = request;
  }
  try {
    audit(job);
    int gates = 0;
    for (auto &check : discoverChecks(job->state.worktree, settings))
      if (check.available && check.name != "ROM verification") {
        auto result = process.run(check.command, events,
                                  directory / fs::u8path(id) /
                                      ("mcp-verify-" + std::to_string(gates++) + ".log"));
        if (result.code)
          throw std::runtime_error("External verification failed: " + check.name);
      }
    if (process.isCancelled())
      throw std::runtime_error("External verification cancelled");
    audit(job);
    Backend(repository, directory.parent_path().parent_path(), settings)
        .recordAgent(id, job->state.results, "review", gates);
    auto results = readDriverResults(job->state.results);
    std::lock_guard<std::mutex> lock(mutex);
    if (!job->state.observed.empty()) {
      auto rows = driverResultRows(results);
      for (auto &row : job->state.observed)
        rows.push_back(row);
      results = {{"results", rows}};
    }
    if (job->state.assigned.empty())
      job->state.assigned = job->state.queue;
    job->state.assigned = attemptedTargets(job->state.assigned, results);
    batchBook.complete(job->state.id, job->state.assigned);
    consumeBatch(job->state);
    job->state.phase = "review";
    job->state.detail = gates ? "External batch independently verified; review required"
                              : "External batch unverified; no gate available";
    saveLocked();
    job->requestRunner = nullptr;
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lock(mutex);
    job->state.phase = process.isCancelled() || job->runner.isCancelled() ? "cancelled" : "failed";
    job->requestRunner = nullptr;
    job->state.detail = e.what();
    saveLocked();
    job->active = false;
    job->externalTask = false;
    throw;
  }
  job->active = false;
  job->externalTask = false;
}
Json Fleet::backend(const std::string &method, const Json &args) {
  if (Backend::mutation(method, args))
    throw std::runtime_error(
        "Agents cannot authorize backend mutations; user must preview them locally");
  return Backend(repository, directory.parent_path().parent_path(), settings, vault.values())
      .invoke(method, args);
}
bool Fleet::toolEnabled(const std::string &id) {
  auto prefs = backend("preferences.get", Json::object());
  return enabledTool(prefs, id);
}
Result Fleet::runTool(const std::string &id, const std::string &tool, const Json &args,
                      Runner *request) {
  std::shared_ptr<Job> job;
  {
    std::lock_guard<std::mutex> lock(mutex);
    job = jobs.at(id);
    if (job->state.spec.kind != "mcp" || job->state.worktree.empty() ||
        job->state.phase != "running")
      throw std::runtime_error("No active external-agent batch");
  }
  if (!toolEnabled(tool))
    throw std::runtime_error("Tool disabled by user policy");
  if (!descriptor.tool(tool).readOnly &&
      backend("preferences.get", Json::object()).value("safeMode", false))
    throw std::runtime_error("User safe mode prohibits mutations");
  std::lock_guard<std::mutex> taskLock(job->externalMutex);
  auto &process = request ? *request : job->runner;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (job->state.phase != "running" || job->runner.isCancelled() || process.isCancelled())
      throw std::runtime_error("External batch stopped or request cancelled");
    job->externalTask = true;
    job->requestRunner = request;
  }
  try {
    auto c = toolCommand(descriptor, descriptor.tool(tool), args, job->state.worktree, true, false);
    for (auto &entry : vault.values())
      c.environment[entry.first] = entry.second;
    c.activityTool = tool;
    c.activityArguments = args.dump();
    c.activityLabel = descriptor.tool(tool).label;
    c.activityReadOnly = descriptor.tool(tool).readOnly;
    c.activityAgent = job->state.spec.name;
    c.activityRole = job->state.spec.role;
    c.activityRepository = repository;
    auto result = process.run(c, {}, directory / fs::u8path(id) / (uniqueId() + "-tool.log"));
    audit(job);
    if (tool == "match" && result.code != ERROR_CANCELLED) {
      std::string source;
      if (args.contains("c") && args["c"].is_string()) {
        auto candidate = fs::u8path(args["c"].get<std::string>());
        if (candidate.is_absolute())
          candidate = candidate.lexically_relative(job->state.worktree);
        auto path = confinedPath(job->state.worktree, utf8(candidate.wstring()));
        if (fs::exists(path) && fs::file_size(path) <= 1024 * 1024)
          source = read(path);
        else
          source = "dcd 0x00000000"; // Missing candidate cannot receive match credit.
      }
      auto observed = matchObservation(args, result.output, result.code, source);
      auto record = directory / fs::u8path(id) / (uniqueId() + "-observed.json");
      write(record, Json{{"results", Json::array({observed})}}.dump(2));
      Backend(repository, directory.parent_path().parent_path(), settings)
          .recordAgent(id, record, "running", 0);
      std::lock_guard<std::mutex> lock(mutex);
      job->state.observed.push_back(observed);
      if (job->state.observed.size() > 2000)
        job->state.observed.erase(job->state.observed.begin());
      saveLocked();
    }
    job->externalTask = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      job->requestRunner = nullptr;
    }
    if (job->runner.isCancelled()) {
      std::lock_guard<std::mutex> lock(mutex);
      job->active = false;
      job->state.phase = "cancelled";
      saveLocked();
    }
    return result;
  } catch (...) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      job->requestRunner = nullptr;
    }
    job->externalTask = false;
    throw;
  }
}
} // namespace lite
