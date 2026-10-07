#include "backend.h"
#include "repository.h"
#include <regex>
#include <set>
#include <sstream>
#include <ctime>
#include <cmath>
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
namespace lite {
namespace {
std::string fingerprint(const std::string &s) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    throw std::runtime_error("Cannot initialize snapshot digest");
  struct A {
    BCRYPT_ALG_HANDLE h;
    ~A() { BCryptCloseAlgorithmProvider(h, 0); }
  } a{alg};
  if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0)
    throw std::runtime_error("Cannot initialize hash");
  struct H {
    BCRYPT_HASH_HANDLE h;
    ~H() { BCryptDestroyHash(h); }
  } h{hash};
  unsigned char digest[32];
  if (BCryptHashData(hash, (PUCHAR)s.data(), (ULONG)s.size(), 0) < 0 ||
      BCryptFinishHash(hash, digest, 32, 0) < 0)
    throw std::runtime_error("Cannot hash snapshot");
  std::string result;
  static const char hex[] = "0123456789abcdef";
  for (auto byte : digest) {
    result += hex[byte >> 4];
    result += hex[byte & 15];
  }
  return result;
}
Json fileJson(const fs::path &p, Json fallback = Json::object()) {
  if (!fs::exists(p))
    return fallback;
  if (fs::file_size(p) > 128 * 1024 * 1024)
    throw std::runtime_error("Data file exceeds 128 MiB");
  return Json::parse(read(p));
}
void saveJson(const fs::path &p, const Json &j) {
  fs::create_directories(p.parent_path());
  auto tmp = p;
  tmp += "." + uniqueId() + ".tmp";
  write(tmp, j.dump(2) + "\n");
  if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    fs::remove(tmp);
    throw std::runtime_error("Cannot save local state");
  }
}
Json jsonLines(const fs::path &p) {
  Json result = Json::array();
  if (!fs::exists(p))
    return result;
  if (fs::file_size(p) > 32 * 1024 * 1024)
    throw std::runtime_error("JSONL exceeds 32 MiB");
  for (auto &line : split(read(p), '\n')) {
    if (trim(line).empty())
      continue;
    auto j = Json::parse(line, nullptr, false);
    if (!j.is_discarded())
      result.push_back(j);
  }
  return result;
}
Json orderHistory(const Json &rows) {
  std::map<std::string, Json> nodes;
  std::map<std::string, std::vector<std::string>> children;
  int n = 0;
  for (auto row : rows) {
    auto id = row.value("attemptId", row.value("id", "anon-" + std::to_string(n++)));
    row["attemptId"] = id;
    row.erase("loggedAt");
    row.erase("ts");
    nodes[id] = row;
  }
  std::vector<std::string> roots;
  for (auto &pair : nodes) {
    auto parent = pair.second.value("parentAttemptId", std::string());
    if (parent != pair.first && nodes.count(parent))
      children[parent].push_back(pair.first);
    else
      roots.push_back(pair.first);
  }
  Json result = Json::array();
  std::set<std::string> visited;
  std::function<void(const std::string &, int)> walk = [&](const std::string &id, int depth) {
    if (!visited.insert(id).second)
      return;
    auto row = nodes.at(id);
    row["depth"] = depth;
    result.push_back(row);
    for (auto &child : children[id])
      walk(child, depth + 1);
  };
  for (auto &root : roots)
    walk(root, 0);
  for (auto &pair : nodes)
    if (!visited.count(pair.first))
      walk(pair.first, 0);
  return result;
}
uint64_t number(const Json &j) {
  if (j.is_number_unsigned())
    return j.get<uint64_t>();
  if (j.is_number_integer())
    return (uint64_t)std::max<int64_t>(0, j.get<int64_t>());
  if (j.is_string()) {
    auto s = j.get<std::string>();
    size_t n = 0;
    auto v = std::stoull(s, &n, 0);
    if (n == s.size())
      return v;
  }
  throw std::runtime_error("Invalid address");
}
bool sameFunction(const Json &r, const Json &q) {
  if (!q.value("functionId", std::string()).empty() &&
      (r.value("functionId", r.value("id", std::string())) == q["functionId"]))
    return true;
  if (r.contains("addr") && q.contains("addr") &&
      r.value("module", std::string()) == q.value("module", std::string()) &&
      number(r["addr"]) == number(q["addr"]))
    return true;
  return !q.value("name", std::string()).empty() && r.value("name", std::string()) == q["name"] &&
         (!r.contains("module") ||
          r.value("module", std::string()) == q.value("module", std::string()));
}
std::string encode(const std::string &s) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out += (char)c;
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
void noCredentials(const Json &j) {
  if (j.is_object())
    for (auto it = j.begin(); it != j.end(); ++it) {
      auto name = it.key();
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (name == "apikey" || name == "api_key" || name == "password" || name == "token" ||
          name == "authorization" || name == "secret")
        throw std::runtime_error(
            "Store credentials in the local vault or environment, not JSON settings");
      noCredentials(it.value());
    }
  else if (j.is_array())
    for (auto &v : j)
      noCredentials(v);
  else if (j.is_string()) {
    auto value = j.get<std::string>();
    if (value.rfind("https://", 0) == 0 || value.rfind("http://", 0) == 0) {
      auto hostEnd = value.find('/', value.find("://") + 3);
      auto host =
          value.substr(value.find("://") + 3,
                       hostEnd == std::string::npos ? hostEnd : hostEnd - (value.find("://") + 3));
      if (host.find('@') != host.npos ||
          std::regex_search(
              value,
              std::regex("[?&](api[_-]?key|access[_-]?token|token|password|secret|authorization)=",
                         std::regex::icase)))
        throw std::runtime_error(
            "Credential-bearing URLs are forbidden; use the user's vault/environment header");
    }
  }
}
} // namespace
Json parseClaimRows(const std::string &md) {
  Json rows = Json::array();
  std::regex status("\\*\\*(active|partial)\\*\\*", std::regex::icase), addr("0x[0-9a-fA-F]{6,8}"),
      sym("(_Z[A-Za-z0-9_]+|__sinit_[A-Za-z0-9_]+|func_(?:ov[0-9]+_)?[0-9a-fA-F]{6,8}|[A-Za-z_][A-"
          "Za-z0-9_]*::[A-Za-z_][A-Za-z0-9_]*)");
  for (auto &line : split(md, '\n')) {
    auto s = trim(line);
    std::smatch m;
    if (s.empty() || s[0] != '|' || !std::regex_search(s, m, status))
      continue;
    auto cells = split(s, '|');
    std::string handle = cells.size() > 2 ? trim(cells[2]) : "someone";
    for (std::sregex_iterator i(s.begin(), s.end(), addr), end; i != end; ++i)
      rows.push_back({{"start", number(i->str())},
                      {"end", number(i->str()) + 1},
                      {"handle", handle},
                      {"status", m[1].str()}});
    for (std::sregex_iterator i(s.begin(), s.end(), sym), end; i != end; ++i)
      rows.push_back({{"name", i->str()}, {"handle", handle}, {"status", m[1].str()}});
  }
  return rows;
}
bool heldTarget(const Json &row, const Json &claims) {
  for (auto &c : claims) {
    auto state = c.value("status", std::string("active"));
    std::transform(state.begin(), state.end(), state.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (state != "active" && state != "partial")
      continue;
    if (c.contains("name") && c["name"] == row.value("name", std::string()))
      return true;
    if (!c.contains("start") || !row.contains("addr"))
      continue;
    if (c.contains("module") && c["module"] != row.value("module", std::string()))
      continue;
    auto a = number(row["addr"]),
         b = a + std::max<uint64_t>(1, row.contains("size") ? number(row["size"]) : 1);
    if (b < a)
      throw std::runtime_error("Address range overflow");
    auto start = number(c["start"]), end = c.contains("end") ? number(c["end"]) : start + 1;
    if (a < end && b > start)
      return true;
  }
  return false;
}
std::string classifySource(const std::string &s) {
  if (s.find("HAND-ASM PRIMITIVE") != s.npos || s.find("NONMATCHING") != s.npos)
    return "ok";
  return std::regex_search(s, std::regex("\\bdcd\\s+0x[0-9a-fA-F]")) ? "transcribed" : "ok";
}
Json poolDifficulty(const std::vector<AtlasFunction> &fns) {
  int pending = 0, easy = 0, refiners = 0;
  for (auto &f : fns) {
    if (f.state == "matched" || f.state == "no_match" || f.row.value("noMatch", false))
      continue;
    ++pending;
    bool isNear = f.row.contains("div") && f.row["div"].is_number() &&
                  f.row["div"].get<double>() >= 1 && f.row["div"].get<double>() < 999;
    if (isNear)
      ++refiners;
    if (isNear || f.size <= 512)
      ++easy;
  }
  return {
      {"score", pending ? std::clamp((int)std::floor(5 - 4.0 * easy / pending + 0.5), 1, 5) : 5},
      {"refinerSupply", refiners}};
}
std::string adaptiveRole(const std::string &role, int attempts, int matches, const Json &pool) {
  std::vector<std::string> ladder = {"Hard matcher", "Random", "Drafter", "Refiner"};
  auto at = std::find(ladder.begin(), ladder.end(), role);
  std::string chosen = role;
  if (at != ladder.end() && at + 1 != ladder.end() && attempts >= 8 &&
      double(matches) / attempts < 0.125 * (1 - (pool.value("score", 1) - 1) / 8.0)) {
    if (*(at + 1) != "Refiner" || pool.value("refinerSupply", 0) > 0)
      chosen = *(at + 1);
  }
  return chosen == "Refiner" && pool.value("refinerSupply", 0) == 0 ? "Drafter" : chosen;
}
Backend::Backend(fs::path repo, fs::path data, Settings prefs,
                 std::map<std::string, std::string> keys, Transport http)
    : repository(std::move(repo)), directory(std::move(data)), settings(std::move(prefs)),
      transport(std::move(http)), secrets(std::move(keys)) {
  fs::create_directories(directory);
}
bool Backend::mutation(const std::string &m, const Json &) {
  return m == "projects.register" || m == "descriptor.write" || m == "preferences.set" ||
         m == "connections.set" || m == "git.action" || m == "tools.run" || m == "reports.export" ||
         m == "stats.clear" || m == "network.write" || m == "queue.adopt" || m == "git.clone" ||
         m == "git.backup" || m == "git.discard";
}
Json Backend::catalog() {
  return Json::array(
      {"projects.list",   "projects.register", "descriptor.preview", "descriptor.write",
       "preferences.get", "preferences.set",   "connections.get",    "connections.set",
       "network.read",    "network.write",     "atlas.load",         "atlas.source",
       "atlas.history",   "claims.read",       "preflight",          "git.status",
       "git.syncPreview", "git.action",        "git.clone",          "git.backup",
       "git.discard",     "tools.list",        "tools.run",          "stats.get",
       "stats.clear",     "reports.list",      "reports.export",     "queue.adopt",
       "policy.classify", "policy.adaptive",   "projects.get",       "github.credits",
       "atlas.cosmetics", "atlas.counts",      "atlas.progress",     "atlas.live",
       "update.check",    "harvest.list"});
}
Json Backend::invoke(const std::string &m, Json a) {
  HANDLE lock = CreateFileW((directory / "backend.lock").c_str(),
                            mutation(m, a) ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                            mutation(m, a) ? 0 : FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (lock == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Another backend operation is active");
  struct Guard {
    HANDLE h;
    ~Guard() { CloseHandle(h); }
  } guard{lock};
  if (m == "catalog")
    return catalog();
  if (mutation(m, a)) {
    noCredentials(a);
    auto ticket = a.value("confirmation", std::string());
    a.erase("confirmation");
    std::string baseline;
    if (!repository.empty() && fs::exists(repository / ".git")) {
      Runner r;
      Repository repo(r, repository, settings);
      baseline = repo.git({"rev-parse", "HEAD"}) +
                 repo.git({"status", "--porcelain=v1", "-z", "--untracked-files=all"}) +
                 repo.git({"diff", "--binary", "HEAD"});
      for (auto &change :
           parseStatus(repo.git({"status", "--porcelain=v1", "-z", "--untracked-files=all"}))) {
        auto path = confinedPath(repository, change.path);
        if (fs::is_regular_file(path))
          baseline += utf8(path.wstring()) + sha256File(path);
      }
    }
    for (auto name : {"connections.json", "preferences.json", "projects.json"})
      if (fs::exists(directory / name))
        baseline += read(directory / name);
    baseline = fingerprint(baseline);
    if (ticket.empty()) {
      Json details = Json::object();
      if (m == "git.action") {
        Runner r;
        Repository repo(r, repository, settings);
        auto action = a.at("action").get<std::string>();
        if (action == "Commit staged")
          details["diff"] = repo.commitPreview();
        if (action == "Push reviewed")
          details["commits"] = repo.pushPreview(a.at("remote"), a.at("ref"));
      }
      if (m == "network.write") {
        auto c = fileJson(directory / "connections.json").at(a.at("connection").get<std::string>());
        details = {{"url", c.at("url")},
                   {"method", c.value("method", std::string("GET"))},
                   {"keyEnv", c.value("keyEnv", std::string())}};
      }
      if (m == "tools.run") {
        auto d = loadDescriptor(repository);
        details["command"] =
            preview(toolCommand(d, d.tool(a.at("tool")), a.value("values", Json::object()),
                                repository, true, a.value("apply", false)));
      }
      auto nonce = uniqueId();
      saveJson(directory / "confirmations" / (nonce + ".json"),
               {{"method", m},
                {"args", a},
                {"repository", utf8(repository.wstring())},
                {"baseline", baseline},
                {"created", std::time(nullptr)}});
      return {{"requiresConfirmation", true},
              {"confirmation", nonce},
              {"method", m},
              {"arguments", a},
              {"repository", utf8(repository.wstring())},
              {"details", details},
              {"notice", "Inspect this operation, then resubmit the same arguments with "
                         "confirmation. External connections use your local settings only."}};
    }
    if (!std::regex_match(ticket, std::regex("[0-9a-f]{32}")))
      throw std::runtime_error("Invalid confirmation");
    auto path = directory / "confirmations" / (ticket + ".json");
    auto preview = fileJson(path);
    if (preview.value("method", std::string()) != m || preview.value("args", Json()) != a ||
        preview.value("repository", std::string()) != utf8(repository.wstring()) ||
        preview.value("baseline", std::string()) != baseline ||
        std::time(nullptr) - preview.value("created", int64_t(0)) > 600)
      throw std::runtime_error("Preview expired or operation/repository changed; preview again");
    fs::remove(path);
  }
  return execute(m, a);
}
namespace {
Json leaseBody(Json value, const Json &fields) {
  if (value.is_string()) {
    auto text = value.get<std::string>();
    if (text.size() > 2 && text.front() == '{' && text.back() == '}') {
      auto key = text.substr(1, text.size() - 2);
      if (fields.contains(key))
        return fields[key];
      throw std::runtime_error("Unknown lease template field: " + key);
    }
  } else if (value.is_object())
    for (auto it = value.begin(); it != value.end(); ++it)
      it.value() = leaseBody(it.value(), fields);
  else if (value.is_array())
    for (auto &item : value)
      item = leaseBody(item, fields);
  return value;
}
} // namespace
struct RemoteLease::Impl {
  Backend backend;
  Json profiles, held = Json::array();
  std::string agent;
  std::function<void()> cancel;
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  std::string error;
  std::thread heartbeat;
  Impl(Backend b, std::string a, std::function<void()> c)
      : backend(std::move(b)), agent(std::move(a)), cancel(std::move(c)) {
    profiles = fileJson(backend.directory / "connections.json");
  }
  Json call(const std::string &operation, const Json &fields) {
    auto key = "claims." + operation;
    auto profile = profiles.at(key);
    if (!profile.value("enabled", false) || !profile.value("automatic", false))
      throw std::runtime_error(
          "Remote leases require user-enabled automatic acquire/heartbeat/release profiles");
    auto body = leaseBody(profile.value("bodyTemplate", fields), fields);
    auto response = backend.execute("network.write", {{"connection", key}, {"body", body}});
    if (!response.at("ok").get<bool>())
      throw std::runtime_error("Remote lease " + operation + " failed");
    auto data = response.at("data");
    auto field = profile.value("successField", std::string("ok"));
    if (!data.is_object() || !data.value(field, false))
      throw std::runtime_error("Remote lease " + operation + " refused");
    return data;
  }
  ~Impl() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      done = true;
    }
    cv.notify_all();
    if (heartbeat.joinable())
      heartbeat.join();
    for (auto &fields : held)
      try {
        call("release", fields);
      } catch (...) {
      }
  }
  void start(const Json &targets) {
    for (auto operation : {"acquire", "heartbeat", "release"}) {
      auto key = "claims." + std::string(operation);
      if (!profiles.contains(key) || !profiles[key].value("enabled", false) ||
          !profiles[key].value("automatic", false))
        throw std::runtime_error(
            "Configure and enable all three remote lease profiles before agent execution");
    }
    for (auto &row : targets) {
      if (!row.contains("addr") || !row.contains("module"))
        throw std::runtime_error(
            "Remote lease target needs module and addr; enrich the target first");
      auto begin = number(row["addr"]),
           end = begin + (row.contains("size") ? std::max<uint64_t>(1, number(row["size"])) : 1);
      if (end <= begin)
        throw std::runtime_error("Invalid remote claim range");
      Json fields = {{"module", row["module"]},
                     {"start", begin},
                     {"end", end},
                     {"agent", agent},
                     {"name", row.value("name", std::string())}};
      auto response = call("acquire", fields);
      auto field = profiles["claims.acquire"].value("leaseField", std::string("lease"));
      fields["lease"] = response.value(field, Json());
      held.push_back(fields);
    }
    int seconds = std::clamp(profiles["claims.heartbeat"].value("heartbeatSeconds", 15), 1, 120);
    heartbeat = std::thread([this, seconds] {
      for (;;) {
        {
          std::unique_lock<std::mutex> lock(mutex);
          if (cv.wait_for(lock, std::chrono::seconds(seconds), [&] { return done; }))
            return;
        }
        try {
          for (auto &fields : held)
            call("heartbeat", fields);
        } catch (...) {
          {
            std::lock_guard<std::mutex> lock(mutex);
            error = "Remote claim heartbeat failed; stop and verify claims before retrying";
          }
          cancel();
          return;
        }
      }
    });
  }
};
RemoteLease::RemoteLease(Backend backend, Json targets, std::string agent,
                         std::function<void()> cancel)
    : impl(std::make_unique<Impl>(std::move(backend), std::move(agent), std::move(cancel))) {
  impl->start(targets);
}
RemoteLease::~RemoteLease() = default;
void RemoteLease::check() {
  std::lock_guard<std::mutex> lock(impl->mutex);
  if (!impl->error.empty())
    throw std::runtime_error(impl->error);
}
std::unique_ptr<RemoteLease> Backend::reserve(const Json &targets, const std::string &agent,
                                              std::function<void()> cancel) {
  auto profiles = fileJson(directory / "connections.json");
  if (!profiles.contains("claims.acquire") || !profiles["claims.acquire"].value("enabled", false))
    return {};
  return std::make_unique<RemoteLease>(*this, targets, agent, std::move(cancel));
}
void Backend::recordAgent(const std::string &id, const fs::path &results, const std::string &phase,
                          int gates, const std::string &adaptive) {
  HANDLE lock = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 100 && lock == INVALID_HANDLE_VALUE; ++attempt) {
    lock = CreateFileW((directory / "backend.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE)
      Sleep(10);
  }
  if (lock == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Statistics store busy; complete logs retained");
  struct Guard {
    HANDLE h;
    ~Guard() { CloseHandle(h); }
  } guard{lock};
  if (fs::exists(results) && fs::file_size(results) > 32 * 1024 * 1024)
    throw std::runtime_error("Agent results exceed 32 MiB; complete driver logs retained");
  auto stats = fileJson(directory / "stats.json");
  auto entry = stats.value(id, Json::object());
  Json rows = Json::array();
  if (fs::exists(results)) {
    auto contents = read(results);
    auto parsed = Json::parse(contents, nullptr, false);
    if (!parsed.is_discarded()) {
      if (parsed.is_array())
        rows = parsed;
      else
        rows.push_back(parsed);
    } else
      rows = jsonLines(results);
  }
  int attempts = 0, claims = 0, nearMisses = 0;
  Json recent = entry.value("recent", Json::array()), harvest = Json::array();
  for (auto &row : rows) {
    if (!row.is_object())
      continue;
    auto source = row.contains("c_source") && row["c_source"].is_string()
                      ? row["c_source"].get<std::string>()
                      : std::string();
    bool transcribed = classifySource(source) == "transcribed";
    bool matched = row.contains("matched") && row["matched"].is_boolean() &&
                   row["matched"].get<bool>() && !transcribed;
    if (row.contains("divergences") && row["divergences"].is_number() &&
        row["divergences"].get<double>() != 0)
      matched = false;
    attempts += row.contains("attempts") && row["attempts"].is_number_integer()
                    ? std::max(1, row["attempts"].get<int>())
                    : 1;
    if (matched)
      ++claims;
    if (row.contains("divergences") && row["divergences"].is_number() &&
        row["divergences"].get<double>() > 0 && row["divergences"].get<double>() < 999)
      ++nearMisses;
    recent.push_back(matched);
    while (recent.size() > 16)
      recent.erase(recent.begin());
    if (matched)
      harvest.push_back({{"name", row.value("name", std::string())},
                         {"sourceClassification", transcribed ? "transcribed" : "ok"},
                         {"declaredMatch", true},
                         {"verifiedGates", gates}});
  }
  if (!adaptive.empty())
    entry["adaptiveRole"] = adaptive;
  entry["attempts"] = entry.value("attempts", 0) + attempts;
  entry["declaredMatches"] = entry.value("declaredMatches", 0) + claims;
  entry["nearMisses"] = entry.value("nearMisses", 0) + nearMisses;
  entry["recent"] = recent;
  entry["phase"] = phase;
  entry["updated"] = std::time(nullptr);
  entry["verifiedGates"] = gates;
  stats[id] = entry;
  saveJson(directory / "stats.json", stats);
  auto prefs = fileJson(directory / "preferences.json");
  if (prefs.value("reports", false)) {
    auto report = directory / "activity.jsonl";
    std::ofstream stream(report, std::ios::app);
    stream << Json({{"agent", id},
                    {"phase", phase},
                    {"attempts", attempts},
                    {"declaredMatches", claims},
                    {"time", std::time(nullptr)}})
                  .dump()
           << "\n";
  }
  if (!harvest.empty())
    saveJson(directory / "harvest" / (id + ".json"),
             {{"agent", id},
              {"repository", utf8(repository.wstring())},
              {"results", utf8(results.wstring())},
              {"phase", phase},
              {"matches", harvest},
              {"notice", "Driver-declared matches require independent matching proof and user "
                         "review before publication"}});
}
Json Backend::execute(const std::string &m, const Json &a) {
  if (m == "connections.get")
    return fileJson(directory / "connections.json");
  if (m == "connections.set") {
    noCredentials(a);
    saveJson(directory / "connections.json", a);
    return {{"saved", true}};
  }
  if (m == "preferences.get")
    return fileJson(directory / "preferences.json");
  if (m == "preferences.set") {
    noCredentials(a);
    if (a.value("autoPush", false))
      throw std::runtime_error("Automatic push is disabled: every push requires its own complete "
                               "outgoing-commit preview");
    for (auto key : {"reports", "safeMode", "useAgents", "autoLand"})
      if (a.contains(key) && !a[key].is_boolean())
        throw std::runtime_error("Policy must be boolean: " + std::string(key));
    if (a.contains("agentFanout") &&
        (!a["agentFanout"].is_number_integer() || a["agentFanout"].get<int>() < 1 ||
         a["agentFanout"].get<int>() > 200))
      throw std::runtime_error("Agent fanout must be 1..200");
    auto prefs = fileJson(directory / "preferences.json");
    prefs.merge_patch(a);
    saveJson(directory / "preferences.json", prefs);
    return prefs;
  }
  if (m == "projects.list")
    return fileJson(directory / "projects.json", Json::array());
  if (m == "projects.register") {
    noCredentials(a);
    auto list = fileJson(directory / "projects.json", Json::array());
    auto entry = a;
    auto id = entry.at("id").get<std::string>();
    if (id.empty())
      throw std::runtime_error("Project id required");
    if (entry.contains("repository")) {
      Runner r;
      Repository repo(r, fs::u8path(entry["repository"].get<std::string>()), settings);
      entry["repository"] = utf8(repo.root.wstring());
    } else if (!entry.contains("descriptor"))
      throw std::runtime_error("A remote project needs a local descriptor; fetching it requires "
                               "your enabled connection");
    bool found = false;
    for (auto &v : list)
      if (v.at("id") == id) {
        v = entry;
        found = true;
      }
    if (!found)
      list.push_back(entry);
    saveJson(directory / "projects.json", list);
    return entry;
  }
  if (m == "projects.get") {
    for (auto &p : fileJson(directory / "projects.json", Json::array()))
      if (p.at("id") == a.at("id"))
        return p;
    throw std::runtime_error("Unknown registered project");
  }
  if (m == "github.credits" || m == "atlas.cosmetics" || m == "atlas.counts" ||
      m == "atlas.progress" || m == "atlas.live" || m == "update.check") {
    auto args = a;
    if (!args.contains("connection"))
      args["connection"] = m;
    return execute("network.read", args);
  }
  if (m == "git.clone") {
    auto url = a.at("url").get<std::string>();
    if (url.empty() || url[0] == '-' || url.find('?') != url.npos || url.find('#') != url.npos ||
        (url.find("https://") == 0 && url.substr(8).find('@') != url.npos))
      throw std::runtime_error("Use a credential-free Git URL or a local repository path; "
                               "authentication belongs in your Git credential helper");
    auto dest = directory / "clones" / uniqueId();
    fs::create_directories(dest.parent_path());
    Runner r;
    auto log = directory / (uniqueId() + "-clone.log");
    auto result = r.run(
        {{"git", "clone", "--progress", "--", url, utf8(dest.wstring())}, directory}, {}, log);
    return {{"exit", result.code},
            {"repository", utf8(dest.wstring())},
            {"output", redact(result.output, secrets)},
            {"log", utf8(log.wstring())}};
  }
  if (m == "network.read" || m == "network.write") {
    auto connection =
        fileJson(directory / "connections.json").at(a.at("connection").get<std::string>());
    if (!connection.value("enabled", false))
      throw std::runtime_error(
          "Connection disabled; the user must configure and enable it locally");
    auto method = connection.value("method", std::string("GET"));
    if ((m == "network.read") != (method == "GET"))
      throw std::runtime_error("Writes need a confirmed network.write operation");
    auto url = connection.at("url").get<std::string>();
    if (a.contains("query"))
      for (auto it = a["query"].begin(); it != a["query"].end(); ++it)
        url += (url.find('?') == url.npos ? "?" : "&") + encode(it.key()) + "=" +
               encode(it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
    std::map<std::string, std::string> headers;
    auto key = connection.value("keyEnv", std::string());
    if (!key.empty()) {
      auto value = secrets.find(key);
      if (value == secrets.end()) {
        auto envName = wide(key);
        DWORD n = GetEnvironmentVariableW(envName.c_str(), nullptr, 0);
        if (n && n <= 65536) {
          std::wstring buffer(n, 0);
          GetEnvironmentVariableW(envName.c_str(), buffer.data(), n);
          buffer.resize(n - 1);
          secrets[key] = utf8(buffer);
          value = secrets.find(key);
        }
      }
      if (value == secrets.end())
        throw std::runtime_error("Missing user credential variable: " + key);
      headers[connection.value("keyHeader", std::string("Authorization"))] =
          connection.value("keyPrefix", std::string("Bearer ")) + value->second;
    }
    auto response =
        transport(url, method, a.contains("body") ? a["body"].dump() : std::string(), headers);
    auto clean = redact(response.body, secrets);
    auto json = Json::parse(clean, nullptr, false);
    return {{"status", response.status},
            {"ok", response.status >= 200 && response.status < 300},
            {"data", json.is_discarded() ? Json(clean) : json}};
  }
  if (m == "harvest.list") {
    Json records = Json::array();
    auto path = directory / "harvest";
    if (fs::exists(path))
      for (auto &p : fs::directory_iterator(path))
        if (p.path().extension() == ".json")
          records.push_back(fileJson(p.path()));
    return records;
  }
  if (m == "stats.get")
    return fileJson(directory / "stats.json");
  if (m == "stats.clear") {
    saveJson(directory / "stats.json", Json::object());
    return {{"cleared", true}};
  }
  if (m == "reports.list") {
    Json files = Json::array();
    for (auto &p : fs::directory_iterator(directory))
      if (p.is_regular_file() && p.path().extension() == ".jsonl")
        files.push_back({{"file", utf8(p.path().filename().wstring())}, {"bytes", p.file_size()}});
    return files;
  }
  if (m == "reports.export") {
    auto bundle = Json{{"projects", fileJson(directory / "projects.json", Json::array())},
                       {"preferences", fileJson(directory / "preferences.json")},
                       {"stats", fileJson(directory / "stats.json")}};
    auto name = "report-" + uniqueId() + ".json";
    saveJson(directory / "exports" / name, bundle);
    return {{"path", utf8((directory / "exports" / name).wstring())}};
  }
  if (m == "policy.classify")
    return {{"classification", classifySource(a.at("source").get<std::string>())}};
  if (m == "policy.adaptive")
    return {{"role", adaptiveRole(a.at("role"), a.at("attempts"), a.at("matches"), a.at("pool"))}};
  Descriptor descriptor;
  if (a.contains("descriptor"))
    descriptor = parseDescriptor(a["descriptor"].dump());
  else if (m.rfind("atlas.", 0) == 0 || m.rfind("tools.", 0) == 0 || m == "preflight")
    descriptor = loadDescriptor(repository);
  if (m == "descriptor.preview") {
    Json tools = Json::array();
    for (auto &check : discoverChecks(repository, settings))
      if (check.available)
        tools.push_back({{"id", "check_" + std::to_string(tools.size())},
                         {"label", check.name},
                         {"readOnly", true},
                         {"command", preview(check.command)}});
    return {{"tangosVersion", "1"},
            {"project",
             {{"name", utf8(repository.filename().wstring())},
              {"title", utf8(repository.filename().wstring())}}},
            {"runtime", {{"python", settings.python}, {"shell", false}}},
            {"tools", tools}};
  }
  if (m == "descriptor.write") {
    auto valid = parseDescriptor(a.at("descriptor").dump());
    saveJson(repository / "tangos.json", valid.document);
    return {{"saved", true}};
  }
  if (m == "atlas.load")
    return fileJson(confinedPath(repository, descriptor.database));
  if (m == "atlas.source") {
    auto path = confinedPath(repository, a.at("path"));
    auto why = blockedPath(a.at("path"), settings);
    if (!why.empty() && a.at("path").get<std::string>().rfind("src/", 0) != 0)
      throw std::runtime_error("Protected source path");
    if (fs::file_size(path) > 1024 * 1024)
      throw std::runtime_error("Source exceeds 1 MiB");
    auto source = read(path);
    if (!blockedBlob(source).empty())
      throw std::runtime_error("Protected source content");
    return {{"path", a["path"]}, {"source", source}, {"classification", classifySource(source)}};
  }
  if (m == "atlas.history") {
    auto conventions = descriptor.document.value("project", Json::object())
                           .value("matchConventions", Json::object());
    Json attempts = Json::array(), tips = Json::array();
    for (auto &r : jsonLines(confinedPath(
             repository,
             conventions.value("attemptsPath", std::string("config/match_attempts.jsonl")))))
      if (sameFunction(r, a))
        attempts.push_back(r);
    for (auto &r : jsonLines(confinedPath(
             repository, conventions.value("nearMissDb", std::string("nearmiss/db.jsonl")))))
      if (sameFunction(r, a))
        tips.push_back(r);
    Json best = nullptr;
    double score = 1e100;
    for (auto &tip : tips)
      if (tip.contains("divergences") && tip["divergences"].is_number() &&
          tip["divergences"].get<double>() < score) {
        best = tip;
        score = tip["divergences"].get<double>();
      }
    return {{"attempts", orderHistory(attempts)}, {"tips", tips}, {"tip", best}};
  }
  if (m == "claims.read") {
    Json claims = fs::exists(repository / "CLAIMS.md")
                      ? parseClaimRows(read(repository / "CLAIMS.md"))
                      : Json::array();
    if (a.contains("connection")) {
      auto response = execute("network.read", a);
      if (!response["ok"].get<bool>())
        throw std::runtime_error("Cannot verify remote claims; refuse assignment");
      auto data = response["data"];
      for (auto &c : data.value("claims", Json::array()))
        claims.push_back(c);
    }
    return claims;
  }
  if (m == "queue.adopt") {
    auto rows = jsonLines(confinedPath(repository, a.at("path")));
    auto claims = execute("claims.read", Json::object());
    Json ready = Json::array();
    std::set<std::string> ids;
    for (auto &row : rows) {
      auto id = row.value("id", row.value("module", std::string()) + ":" +
                                    row.value("name", std::string()));
      if (id.empty() || !ids.insert(id).second || heldTarget(row, claims) ||
          row.value("matched", false) || row.value("noMatch", false))
        continue;
      ready.push_back(row);
    }
    auto name = "adopted-" + uniqueId() + ".json";
    saveJson(directory / "queues" / name, ready);
    return {{"targets", ready}, {"path", utf8((directory / "queues" / name).wstring())}};
  }
  if (m == "tools.list") {
    auto hidden = fileJson(directory / "preferences.json").value("disabledTools", Json::array());
    Json out = Json::array();
    for (auto &t : descriptor.tools)
      out.push_back({{"id", t.id},
                     {"label", t.label},
                     {"readOnly", t.readOnly},
                     {"enabled", std::find(hidden.begin(), hidden.end(), t.id) == hidden.end()}});
    return out;
  }
  if (m == "tools.run") {
    auto &tool = descriptor.tool(a.at("tool"));
    if (!tool.readOnly && settings.portOnly)
      throw std::runtime_error("Mutating primary-checkout tools blocked in port-only mode");
    if (!tool.readOnly && fileJson(directory / "preferences.json").value("safeMode", false))
      throw std::runtime_error("User safe mode prohibits mutating tools");
    auto command = toolCommand(descriptor, tool, a.value("values", Json::object()), repository,
                               true, a.value("apply", false));
    command.environment = secrets;
    Runner r;
    auto log = directory / (uniqueId() + "-tool.log");
    auto result = r.run(command, {}, log);
    return {{"exit", result.code}, {"output", result.output}, {"log", utf8(log.wstring())}};
  }
  Runner runner;
  Repository repo(runner, repository, settings);
  if (m == "git.status")
    return {{"status", repo.status()}};
  if (m == "git.syncPreview")
    return {{"status", repo.status()},
            {"log", repo.git({"log", "--oneline", "--decorate", "-20"})},
            {"diff", repo.git({"diff", "--stat"})},
            {"notice", "Review local changes and make a backup before switching or synchronizing"}};
  if (m == "git.backup") {
    auto dest = directory / "backups" / uniqueId();
    fs::create_directories(dest);
    Json files = Json::array(), skipped = Json::array();
    for (auto &change :
         parseStatus(repo.git({"status", "--porcelain=v1", "-z", "--untracked-files=all"}))) {
      auto path = confinedPath(repository, change.path);
      auto reason = blockedPath(change.path, settings);
      if (change.path.rfind("src/", 0) == 0 && reason.find("src/") != std::string::npos)
        reason.clear();
      if (!reason.empty() || fs::is_symlink(fs::symlink_status(path))) {
        skipped.push_back(change.path);
        continue;
      }
      if (fs::is_regular_file(path)) {
        auto content = read(path);
        if (content.size() > 16 * 1024 * 1024 || !blockedBlob(content).empty()) {
          skipped.push_back(change.path);
          continue;
        }
        auto to = dest / fs::u8path(change.path);
        fs::create_directories(to.parent_path());
        write(to, content);
      }
      files.push_back(
          {{"path", change.path}, {"status", change.xy}, {"deleted", !fs::exists(path)}});
    }
    saveJson(dest / "manifest.json",
             {{"head", repo.git({"rev-parse", "HEAD"})}, {"files", files}, {"excluded", skipped}});
    return {{"backup", utf8(dest.wstring())}, {"files", files}, {"excluded", skipped}};
  }
  if (m == "git.discard") {
    Args args = {"git", "restore", "--source=HEAD", "--staged", "--worktree", "--"};
    for (auto &v : a.at("paths")) {
      auto path = v.get<std::string>();
      confinedPath(repository, path);
      if (!blockedPath(path, settings).empty())
        throw std::runtime_error("Protected discard path: " + path);
      args.push_back(path);
    }
    if (args.size() == 6)
      throw std::runtime_error("Explicit paths required");
    auto result = runner.run({args, repository});
    return {{"exit", result.code}, {"output", result.output}};
  }
  if (m == "git.action") {
    auto action = a.at("action").get<std::string>();
    Command command;
    if ((action == "branch.create" || action == "checkout" || action == "worktree.create") &&
        !validRef(a.at("branch")))
      throw std::runtime_error("Invalid branch ref");
    if (action == "branch.create")
      command = {{"git", "branch", a.at("branch")}, repository};
    else if (action == "worktree.create") {
      auto branch = a.at("branch").get<std::string>();
      if (branch.rfind("codex/", 0) != 0)
        throw std::runtime_error("New worktree branches must use codex/");
      command = {{"git", "worktree", "add", "-b", branch,
                  utf8((directory / "worktrees" / uniqueId()).wstring()), "HEAD"},
                 repository};
    } else if (action == "rebase.abort" || action == "rebase.continue")
      command = {{"git", "rebase", action == "rebase.abort" ? "--abort" : "--continue"},
                 repository};
    else if (action == "checkout") {
      if (!repo.git({"status", "--porcelain"}).empty())
        throw std::runtime_error("Checkout requires a clean tree");
      command = {{"git", "switch", a.at("branch")}, repository};
    } else
      command = repo.action(action, a.value("remote", std::string()), a.value("ref", std::string()),
                            a.value("text", std::string()));
    auto log = directory / (uniqueId() + "-git.log");
    auto result = runner.run(command, {}, log);
    return {{"exit", result.code}, {"output", result.output}, {"log", utf8(log.wstring())}};
  }
  if (m == "preflight") {
    Json checks = Json::array();
    for (auto &check : discoverChecks(repository, settings))
      checks.push_back({{"name", check.name},
                        {"available", check.available},
                        {"command", preview(check.command)}});
    auto requirements = descriptor.document.value("requirements", Json::object());
    auto packages = Json::array();
    for (auto &pkg : requirements.value("pythonPackages", Json::array())) {
      auto name = pkg.get<std::string>(), module = name;
      if (name == "pyelftools" || name == "py-elftools")
        module = "elftools";
      else if (name == "pillow")
        module = "PIL";
      else if (name == "pyyaml")
        module = "yaml";
      else
        std::replace(module.begin(), module.end(), '-', '_');
      auto result = runner.run({{descriptor.python, "-c",
                                 "import importlib.util,sys; sys.exit(0 if "
                                 "importlib.util.find_spec(sys.argv[1]) else 1)",
                                 module},
                                repository});
      packages.push_back(
          {{"package", name},
           {"available", result.code == 0},
           {"fix", "Install this package yourself using the repository's pinned requirements"}});
    }
    checks.push_back(
        {{"name", "Atlas data"},
         {"available", fs::exists(confinedPath(repository, descriptor.database))},
         {"fix",
          "Run the declared generation tool or configure your own published-data connection"}});
    auto python = runner.run({{descriptor.python, "--version"}, repository});
    auto gh = runner.run({{"gh", "auth", "status"}, repository});
    return {{"checks", checks},
            {"pythonPackages", packages},
            {"python", {{"available", python.code == 0}, {"detail", python.output}}},
            {"github", {{"authenticated", gh.code == 0}, {"detail", redact(gh.output, secrets)}}},
            {"instructions", repo.agentHandoff()}};
  }
  throw std::runtime_error("Unknown backend operation: " + m);
}
} // namespace lite
