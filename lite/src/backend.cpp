#include "backend.h"
#include "controller_view.h"
#include "help.h"
#include "atlas_layout.h"
#include "viewer.h"
#include "batches.h"
#include "archive.h"
#include "updater.h"
#include "activity.h"
#include "images.h"
#include <numeric>
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
std::mutex sessionStatsMutex;
std::map<std::string, Json> sessionStats;
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
Json redactMetadata(Json value, const std::map<std::string, std::string> &secrets) {
  if (value.is_string())
    return redact(value.get<std::string>(), secrets);
  if (value.is_object() || value.is_array())
    for (auto &item : value)
      item = redactMetadata(item, secrets);
  return value;
}
std::string screenshotBytes(const fs::path &path) {
  auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot read the screenshot; close programs currently writing it");
  struct Guard {
    HANDLE handle;
    ~Guard() { CloseHandle(handle); }
  } guard{handle};
  LARGE_INTEGER size{};
  if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileSizeEx(handle, &size) ||
      size.QuadPart <= 0 || size.QuadPart > 16 * 1024 * 1024)
    throw std::runtime_error("Each screenshot must be a regular file under 16 MiB");
  std::string bytes(size_t(size.QuadPart), '\0');
  DWORD received = 0;
  if (!ReadFile(handle, bytes.data(), DWORD(bytes.size()), &received, nullptr) ||
      received != bytes.size())
    throw std::runtime_error("Screenshot changed or could not be read completely");
  return bytes;
}
Json reportAttachments(const Json &args) {
  auto paths = args.value("screenshots", Json::array());
  if (!paths.is_array() || paths.size() > 16)
    throw std::runtime_error("Choose at most 16 screenshots");
  Json result = Json::array();
  std::set<fs::path> seen;
  uint64_t total = 0;
  for (auto &value : paths) {
    if (!value.is_string())
      throw std::runtime_error("Screenshot paths must be strings");
    auto path = fs::u8path(value.get<std::string>());
    auto info = inspectScreenshot(path);
    path = fs::canonical(path);
    if (!seen.insert(path).second)
      continue;
    total += info.bytes;
    if (total > 64 * 1024 * 1024)
      throw std::runtime_error("Screenshot attachments exceed 64 MiB total");
    auto bytes = screenshotBytes(path);
    if (bytes.size() != info.bytes)
      throw std::runtime_error("Screenshot changed during inspection; attach it again");
    result.push_back(
        {{"path", utf8(path.wstring())},
         {"file", "screenshot-" + std::to_string(result.size() + 1) + "." + info.format},
         {"bytes", bytes.size()},
         {"width", info.width},
         {"height", info.height},
         {"format", info.format},
         {"sha256", fingerprint(bytes)}});
  }
  return result;
}
Json fileJson(const fs::path &p, Json fallback = Json::object()) {
  if (!fs::exists(p))
    return fallback;
  if (fs::file_size(p) > 128 * 1024 * 1024)
    throw std::runtime_error("Data file exceeds 128 MiB");
  return p.filename() == "stats.json" ? parseStatisticsJson(read(p)) : Json::parse(read(p));
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
Json historyString(const Json &row, const std::string &key) {
  if (row.contains(key) && row[key].is_string() && !trim(row[key].get<std::string>()).empty())
    return trim(row[key].get<std::string>());
  return nullptr;
}
Json historyNumber(const Json &row, const std::string &key) {
  if (!row.contains(key))
    return nullptr;
  if (row[key].is_number())
    return row[key];
  if (row[key].is_string())
    try {
      size_t used = 0;
      auto value = std::stod(row[key].get<std::string>(), &used);
      if (used == row[key].get<std::string>().size() && std::isfinite(value))
        return value;
    } catch (...) {
    }
  return nullptr;
}
Json historySummary(const Json &row, size_t index) {
  Json r = Json::object();
  r["attemptId"] = historyString(row, "attemptId");
  if (r["attemptId"].is_null())
    r["attemptId"] = historyString(row, "id");
  if (r["attemptId"].is_null())
    r["attemptId"] = "anon-" + std::to_string(index);
  for (auto key : {"parentAttemptId", "model", "harness", "reasoning", "note", "baseKind"})
    r[key] = historyString(row, key);
  for (auto key : {"model", "harness", "reasoning"})
    if (r[key].is_null() && row.contains("matchProvenance") && row["matchProvenance"].is_object())
      r[key] = historyString(row["matchProvenance"], key);
  if (row.contains("base") && row["base"].is_object())
    r["baseKind"] = historyString(row["base"], "kind");
  r["status"] = historyString(row, "status");
  if (r["status"].is_null())
    r["status"] = "unknown";
  r["divergences"] = historyNumber(row, "divergences");
  r["improvedNearMiss"] = row.contains("improvedNearMiss") &&
                          row["improvedNearMiss"].is_boolean() &&
                          row["improvedNearMiss"].get<bool>();
  for (auto key : {"usedNearMissDraft", "usedGhidraDraft"})
    r[key] = row.contains(key) && row[key].is_boolean() ? row[key] : Json(nullptr);
  return r;
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
    auto parent =
        pair.second.contains("parentAttemptId") && pair.second["parentAttemptId"].is_string()
            ? pair.second["parentAttemptId"].get<std::string>()
            : std::string();
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
  auto fid = historyString(q, "functionId");
  if (fid.is_null())
    fid = historyString(q, "id");
  auto rid = historyString(r, "functionId");
  if (rid.is_null())
    rid = historyString(r, "id");
  if (!fid.is_null() && fid == rid)
    return true;
  auto mod = historyString(q, "module");
  auto rm = historyString(r, "module");
  if (q.contains("addr"))
    try {
      auto addr = number(q["addr"]);
      std::ostringstream hex;
      hex << std::hex << addr;
      if (!mod.is_null() && rid.is_string() &&
          (rid == mod.get<std::string>() + ":" + hex.str() ||
           rid == mod.get<std::string>() + ":0x" + hex.str()))
        return true;
      if (r.contains("addr") && rm == mod && number(r["addr"]) == addr)
        return true;
    } catch (...) {
    }
  auto name = historyString(q, "name");
  return !name.is_null() && historyString(r, "name") == name && (rm.is_null() || rm == mod);
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
    if (f.state == "matched" || f.state == "no_match" || exemptTarget(f.row))
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
                 std::map<std::string, std::string> keys, Transport http, Runner *process,
                 std::function<void(const std::string &)> progress)
    : repository(std::move(repo)), directory(std::move(data)), settings(std::move(prefs)),
      transport(std::move(http)), processRunner(process), progressSink(std::move(progress)),
      secrets(std::move(keys)) {
  fs::create_directories(directory);
}
bool Backend::mutation(const std::string &m, const Json &) {
  return m == "update.stage" || m == "projects.importZip" || m == "projects.discover" ||
         m == "projects.download" || m == "projects.open" || m == "projects.register" ||
         m == "descriptor.write" || m == "preferences.set" || m == "connections.set" ||
         m == "git.action" || m == "checks.run" || m == "tools.run" || m == "atlas.generate" ||
         m == "reports.export" || m == "bug.report" || m == "stats.clear" || m == "network.write" ||
         m == "queue.adopt" || m == "git.clone" || m == "git.backup" || m == "git.discard" ||
         m == "git.sync";
}
bool enabledTool(const Json &prefs, const std::string &id) {
  auto hidden = prefs.value("disabledTools", Json::array());
  return std::find(hidden.begin(), hidden.end(), id) == hidden.end() &&
         (prefs.value("allowNearMiss", true) || id.rfind("nearmiss_", 0) != 0);
}
Json Backend::catalog() {
  return Json::array(
      {"projects.importZip", "projects.discover",    "projects.download",  "projects.list",
       "projects.register",  "projects.open",        "descriptor.preview", "descriptor.write",
       "preferences.get",    "preferences.set",      "connections.get",    "connections.set",
       "network.read",       "network.write",        "atlas.load",         "atlas.generate",
       "atlas.source",       "atlas.history",        "claims.read",        "preflight",
       "git.status",         "git.syncPreview",      "git.sync",           "git.action",
       "git.clone",          "git.backup",           "git.discard",        "tools.list",
       "tools.run",          "checks.list",          "checks.run",         "policy.presence",
       "stats.get",          "stats.session",        "stats.clear",        "reports.list",
       "reports.export",     "queue.adopt",          "policy.classify",    "policy.adaptive",
       "policy.pool",        "policy.statistics",    "policy.layout",      "policy.color",
       "policy.batches",     "policy.source",        "policy.usage",       "guide.parse",
       "guide.tour",         "guide.tips",           "guide.richText",     "projects.get",
       "github.credits",     "atlas.cosmetics",      "atlas.counts",       "atlas.progress",
       "atlas.live",         "update.check",         "update.stage",       "bug.report",
       "harvest.list",       "activity.snapshot",    "policy.activity",    "policy.detail",
       "policy.match",       "policy.role",          "policy.autoRole",    "policy.effort",
       "policy.drive",       "policy.controllerView"});
}
Json Backend::invoke(const std::string &m, Json a, unsigned lockWaitMs) {
  if (m == "bug.report" && a.contains("description") && a["description"].is_string())
    a["description"] = redact(a["description"].get<std::string>(), secrets);
  const auto deadline = GetTickCount64() + std::min(lockWaitMs, 1000u);
  HANDLE lock;
  do {
    lock = CreateFileW((directory / "backend.lock").c_str(),
                       mutation(m, a) ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                       mutation(m, a) ? 0 : FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION ||
        GetTickCount64() >= deadline)
      break;
    Sleep(10);
  } while (true);
  if (lock == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Another backend operation is active");
  struct Guard {
    HANDLE h;
    ~Guard() { CloseHandle(h); }
  } guard{lock};
  if (m == "catalog")
    return catalog();
  if (m == "activity.snapshot")
    return activityBus().snapshot(utf8(repository.wstring()));
  if (m == "policy.activity") {
    ActivityBus bus;
    for (auto &event : a.at("events"))
      bus.publish(event);
    return bus.snapshot();
  }
  if (m == "policy.detail")
    return {{"streams", activityStreams(a.value("output", std::string()))},
            {"recommendation", sizeRecommendation(a.value("bySize", Json::object()),
                                                  a.value("bySizeOrder", Json::array()))}};
  if (m == "policy.role")
    return measuredRole(a.value("stats", Json::object()));
  if (m == "policy.autoRole") {
    Json out = Json::array();
    for (auto &agent : a.at("agents"))
      out.push_back(automaticRole(agent));
    return out;
  }
  if (m == "policy.effort") {
    Json out = Json::array();
    for (auto &agent : a.at("agents"))
      out.push_back(effortPolicy(agent));
    return out;
  }
  if (m == "policy.match")
    return matchObservation(a.value("values", Json::object()), a.value("output", std::string()),
                            a.value("exit", 0UL), a.value("source", std::string()));
  if (m == "policy.drive") {
    Json out = Json::array();
    for (auto &entry : a.at("cases")) {
      auto targets = entry.at("targets");
      if (!targets.is_number_integer() || targets < 0 || targets > 1000000000)
        throw std::runtime_error("targets must be an integer from 0 to 1000000000");
      out.push_back(driverPolicy(entry.at("agent"), entry.at("preferences"), entry.at("targets")));
    }
    return out;
  }
  Json attachments;
  if (mutation(m, a)) {
    noCredentials(a);
    auto ticket = a.value("confirmation", std::string());
    a.erase("confirmation");
    std::string baseline;
    if (m != "bug.report" && !repository.empty() && fs::exists(repository / ".git")) {
      Runner r;
      Repository repo(r, repository, settings);
      baseline = repo.git({"show-ref"}) + repo.git({"rev-parse", "HEAD"}) +
                 repo.git({"status", "--porcelain=v1", "-z", "--untracked-files=all"}) +
                 repo.git({"diff", "--binary", "HEAD"});
      for (auto &change :
           parseStatus(repo.git({"status", "--porcelain=v1", "-z", "--untracked-files=all"}))) {
        auto path = confinedPath(repository, change.path);
        if (fs::is_regular_file(path) && blockedPath(change.path, settings).empty())
          baseline += utf8(path.wstring()) + sha256File(path);
      }
    }
    for (auto name : {"connections.json", "preferences.json", "projects.json"})
      if (fs::exists(directory / name))
        baseline += read(directory / name);
    if (m == "projects.importZip") {
      auto archive = fs::u8path(a.at("archive").get<std::string>());
      auto destination = fs::u8path(a.at("destination").get<std::string>());
      if (!fs::is_regular_file(archive) || fs::file_size(archive) > 128 * 1024 * 1024 ||
          !destination.is_absolute() || fs::exists(destination))
        throw std::runtime_error(
            "Choose a ZIP under 128 MiB and an absolute new destination folder");
      baseline += sha256File(archive);
    }
    if (m == "bug.report") {
      auto description = a.value("description", std::string());
      if (trim(description).empty() || description.size() > 65536 ||
          !blockedBlob(description).empty())
        throw std::runtime_error("Describe the bug without protected content (maximum 64 KiB)");
      attachments = reportAttachments(a);
      baseline += attachments.dump();
    }
    baseline = fingerprint(baseline);
    if (ticket.empty()) {
      Json details = Json::object();
      if (m == "bug.report")
        details = {{"screenshots", attachments},
                   {"description", redact(a.at("description").get<std::string>(), secrets)},
                   {"notice", "Prepare a local folder only; screenshots retain their visible "
                              "content and no upload is performed"}};
      if (m == "update.stage") {
        auto profiles = fileJson(directory / "connections.json");
        if (!profiles.contains("update.check"))
          throw std::runtime_error("Configure your update.check connection first");
        auto profile = profiles.at("update.check");
        details = {
            {"target", selfExecutable()},
            {"endpoint", profile.at("url")},
            {"trustedAssetPrefix", profile.value("assetPrefix", std::string())},
            {"notice",
             "Download only; SHA256 and native executable checks must pass before installation"}};
      }
      if (m == "checks.run") {
        bool found = false;
        for (auto &check : discoverChecks(repository, settings))
          if (check.name == a.at("name")) {
            if (!check.available)
              throw std::runtime_error(check.requirement);
            details["command"] = preview(check.command);
            details["requirement"] = check.requirement;
            found = true;
          }
        if (!found)
          throw std::runtime_error("Unknown discovered check");
      }
      if (m == "projects.importZip")
        details = inspectProjectZip(fs::u8path(a.at("archive").get<std::string>()), settings);
      if (m == "git.action") {
        Runner r;
        Repository repo(r, repository, settings);
        auto action = a.at("action").get<std::string>();
        if (action == "Commit staged")
          details["diff"] = repo.commitPreview();
        if (action == "Push reviewed")
          details["commits"] = repo.pushPreview(a.at("remote"), a.at("ref"));
      }
      if (m == "git.sync")
        details = execute("git.syncPreview", a);
      if (m == "network.write") {
        auto c = fileJson(directory / "connections.json").at(a.at("connection").get<std::string>());
        details = {{"url", c.at("url")},
                   {"method", c.value("method", std::string("GET"))},
                   {"keyEnv", c.value("keyEnv", std::string())}};
      }
      if (m == "tools.run" || m == "atlas.generate") {
        auto d = loadDescriptor(repository);
        if (m == "atlas.generate" && d.generatorId.empty())
          throw std::runtime_error(
              "Configure data.generate in tangos.json before regenerating Atlas data");
        auto toolId = m == "atlas.generate" ? d.generatorId : a.at("tool").get<std::string>();
        auto values =
            m == "atlas.generate" ? Json{{"out", d.database}} : a.value("values", Json::object());
        if (!d.generatorId.empty() && toolId == d.generatorId)
          validateAtlasOutput(d, values, repository, settings);
        details["command"] = preview(toolCommand(
            d, d.tool(m == "atlas.generate" ? d.generatorId : a.at("tool").get<std::string>()),
            m == "atlas.generate" ? Json{{"out", d.database}} : a.value("values", Json::object()),
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
  if (m == "bug.report")
    a["_validatedAttachments"] = attachments;
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

Json driverUsage(const std::string &output, bool productive, uint64_t elapsedMs, int streak) {
  static const std::regex exhausted(
      R"(\b402\b|payment required|insufficient|out of (credit|quota|balance)|quota (exceeded|exhausted)|billing|no credit)",
      std::regex::icase);
  std::string reason;
  if (std::regex_search(output, exhausted)) {
    streak = 0;
    reason = "API reported out of usage (credit/quota exhausted)";
  } else if (!productive && elapsedMs < 20000) {
    ++streak;
    if (streak >= 5) {
      reason = std::to_string(streak) + " fast empty runs in a row (key likely out of usage)";
      streak = 0;
    }
  } else {
    streak = 0;
  }
  return {{"streak", streak}, {"reason", reason}, {"stopped", !reason.empty()}};
}
bool productiveDriver(const Json &result) {
  if (!result.is_object())
    return false;
  Json names = Json::array();
  for (auto field : {"landedNames", "landed", "matches"})
    if (result.contains(field) && !result[field].is_null()) {
      names = result[field];
      break;
    }
  auto sources = result.value("sources", Json::object());
  if (names.is_array())
    for (auto &entry : names) {
      std::string name = entry.is_string() ? entry.get<std::string>()
                         : entry.is_object() && entry.contains("name") && entry["name"].is_string()
                             ? entry["name"].get<std::string>()
                             : "";
      if (!name.empty() &&
          (!sources.is_object() || !sources.contains(name) || !sources[name].is_string() ||
           classifySource(sources[name].get<std::string>()) != "transcribed"))
        return true;
    }
  auto nearMisses = result.value("nearMisses", Json::array());
  if (nearMisses.is_array())
    for (auto &entry : nearMisses)
      if (entry.is_object() && entry.contains("name") && entry["name"].is_string() &&
          !entry["name"].get<std::string>().empty())
        return true;
  return false;
}
Json driverResultRows(const Json &result) {
  if (result.is_array())
    return result;
  if (!result.is_object())
    return Json::array();
  Json names = Json::array();
  for (auto field : {"landedNames", "landed", "matches"})
    if (result.contains(field) && !result[field].is_null()) {
      names = result[field];
      break;
    }
  if (!result.contains("results") && !result.contains("sources") && !result.contains("landed") &&
      !result.contains("landedNames") && !result.contains("matches") &&
      !result.contains("nearMisses")) {
    auto row = result;
    for (auto pair : {std::pair{"tokensIn", "inputTokens"}, std::pair{"tokensOut", "outputTokens"}})
      if ((!row.contains(pair.first) || row[pair.first].is_null()) && row.contains(pair.second))
        row[pair.first] = row[pair.second];
    return Json::array({row});
  }
  Json rows = result.value("results", Json::array());
  if (!rows.is_array())
    rows = Json::array();
  if (!result.contains("results")) {
    if (names.is_array())
      for (auto &entry : names) {
        if (entry.is_string() && !entry.get<std::string>().empty())
          rows.push_back({{"name", entry}, {"matched", true}});
        else if (entry.is_object() && entry.contains("name") && entry["name"].is_string()) {
          auto row = entry;
          row["matched"] = true;
          rows.push_back(row);
        }
      }
  }
  auto sources = result.value("sources", Json::object());
  for (auto &row : rows)
    if (row.is_object() && row.contains("name") && row["name"].is_string() && sources.is_object() &&
        sources.contains(row["name"].get<std::string>()) &&
        sources[row["name"].get<std::string>()].is_string())
      row["c_source"] = sources[row["name"].get<std::string>()];
  auto tokens = [&](const char *primary, const char *alias) -> int64_t {
    auto value = result.contains(primary) && !result[primary].is_null()
                     ? result[primary]
                     : result.value(alias, Json(0));
    if (!value.is_number())
      return 0;
    auto n = value.get<double>();
    return std::isfinite(n) && n >= 0 && n < 9e18 ? static_cast<int64_t>(n) : 0;
  };
  auto input = tokens("tokensIn", "inputTokens"), output = tokens("tokensOut", "outputTokens");
  if (result.value("tokensOut", Json()).is_null() &&
      result.value("outputTokens", Json()).is_null() && result.contains("tokensPerLanded") &&
      result["tokensPerLanded"].is_number()) {
    auto n = result["tokensPerLanded"].get<double>();
    size_t landed = 0;
    if (names.is_array())
      for (auto &entry : names) {
        auto name = entry.is_string() ? entry.get<std::string>()
                    : entry.is_object() && entry.contains("name") && entry["name"].is_string()
                        ? entry["name"].get<std::string>()
                        : std::string();
        if (!name.empty() &&
            (!sources.is_object() || !sources.contains(name) || !sources[name].is_string() ||
             classifySource(sources[name].get<std::string>()) != "transcribed"))
          ++landed;
      }
    auto total = n * landed;
    if (std::isfinite(total) && total >= 0 && total < 9e18)
      output = static_cast<int64_t>(total);
  }
  if (input || output)
    rows.push_back({{"tokensIn", input}, {"tokensOut", output}});
  return rows;
}
Json updateAgentStats(Json entry, const Json &rows, Json &best) {
  if (!entry.contains("attemptedFuncs")) {
    entry["attempts"] = 0;
    entry["declaredMatches"] = 0;
    entry["nearMisses"] = 0;
    entry["recent"] = Json::array();
    entry["bySize"] = Json::object();
    entry["bySizeOrder"] = Json::array();
  }
  for (auto field : {"attemptedFuncs", "matchedFuncs", "nearMissFuncs", "recent"})
    if (!entry.contains(field) || !entry[field].is_array())
      entry[field] = Json::array();
  auto seen = [](const Json &set, const std::string &key) {
    return !key.empty() && std::find(set.begin(), set.end(), Json(key)) != set.end();
  };
  for (const auto &row : rows) {
    if (!row.is_object())
      continue;
    std::string key;
    for (auto field : {"functionId", "name", "id"})
      if (row.contains(field) && row[field].is_string()) {
        key = row[field];
        break;
      }
    auto source = row.contains("c_source") && row["c_source"].is_string()
                      ? row["c_source"].get<std::string>()
                      : "";
    bool matched = row.contains("matched") && row["matched"] == true &&
                   classifySource(source) != "transcribed";
    double div = -1;
    if (row.contains("divergences")) {
      if (row["divergences"].is_number())
        div = row["divergences"].get<double>();
      else if (row["divergences"].is_string()) {
        try {
          auto text = row["divergences"].get<std::string>();
          size_t end = 0;
          div = std::stod(text, &end);
          if (end != text.size())
            div = -1;
        } catch (...) {
        }
      }
    }
    if (div > 0)
      matched = false;
    bool firstAttempt = !key.empty() && !seen(entry["attemptedFuncs"], key),
         firstMatch = matched && (key.empty() || !seen(entry["matchedFuncs"], key));
    if (key.empty() && row.contains("matched") && row["matched"].is_boolean())
      firstAttempt = true;
    if (firstAttempt) {
      entry["attempts"] = entry.value("attempts", 0) + 1;
      if (!key.empty())
        entry["attemptedFuncs"].push_back(key);
    }
    if (firstMatch) {
      entry["declaredMatches"] = entry.value("declaredMatches", 0) + 1;
      if (!key.empty())
        entry["matchedFuncs"].push_back(key);
    }
    double size = row.contains("size") && row["size"].is_number() ? row["size"].get<double>() : -1;
    if (row.contains("size") && row["size"].is_number() && (firstAttempt || firstMatch)) {
      std::string bucket = size <= 64     ? "<=0x40"
                           : size <= 512  ? "0x40-0x200"
                           : size <= 2048 ? "0x200-0x800"
                                          : ">0x800";
      if (!entry.contains("bySize"))
        entry["bySize"] = Json::object();
      if (!entry.contains("bySizeOrder") || !entry["bySizeOrder"].is_array()) {
        entry["bySizeOrder"] = Json::array();
        for (auto it = entry["bySize"].begin(); it != entry["bySize"].end(); ++it)
          entry["bySizeOrder"].push_back(it.key());
      }
      auto &order = entry["bySizeOrder"];
      if (std::find(order.begin(), order.end(), Json(bucket)) == order.end())
        order.push_back(bucket);
      auto &b = entry["bySize"][bucket];
      if (!b.is_object())
        b = Json::object();
      b["attempts"] = b.value("attempts", 0) + (firstAttempt ? 1 : 0);
      b["matches"] = b.value("matches", 0) + (firstMatch ? 1 : 0);
    }
    if (firstAttempt || firstMatch) {
      entry["recent"].push_back(firstMatch);
      while (entry["recent"].size() > 16)
        entry["recent"].erase(entry["recent"].begin());
    }
    if (matched && !key.empty())
      best[key] = 0;
    if (!key.empty() && div >= 1 && div < 999 &&
        (!best.contains(key) || div < best[key].get<double>())) {
      best[key] = div;
      if ((size <= 0 || div / (size / 4) < 0.34) && !seen(entry["nearMissFuncs"], key)) {
        entry["nearMissFuncs"].push_back(key);
        entry["nearMisses"] = entry.value("nearMisses", 0) + 1;
      }
    }
    for (auto field : {"tokensIn", "tokensOut"})
      if (row.contains(field) && row[field].is_number_integer() && row[field].get<long long>() >= 0)
        entry[field] = entry.value(field, 0LL) + row[field].get<long long>();
  }
  entry["hitRate"] = entry.value("attempts", 0)
                         ? double(entry.value("declaredMatches", 0)) / entry["attempts"].get<int>()
                         : 0.0;
  auto tokens = entry.value("tokensIn", 0LL) + entry.value("tokensOut", 0LL);
  if (tokens && entry.value("declaredMatches", 0))
    entry["tokensPerMatch"] = static_cast<long long>(
        std::floor(double(tokens) / entry["declaredMatches"].get<int>() + 0.5));
  return entry;
}
Json matchObservation(const Json &values, const std::string &output, unsigned long exitCode,
                      const std::string &source) {
  bool matched = exitCode == 0 &&
                 std::regex_search(output, std::regex("MATCHING VERSIONS:\\s*(?!none\\b)\\S",
                                                      std::regex::icase)) &&
                 classifySource(source) != "transcribed";
  Json row{{"matched", matched}};
  if (values.contains("func") && values["func"].is_string())
    row["name"] = values["func"];
  if (values.contains("size")) {
    if (values["size"].is_number())
      row["size"] = values["size"];
    else if (values["size"].is_string()) {
      try {
        auto s = trim(values["size"].get<std::string>());
        row["size"] = std::stoll(
            s, nullptr, s.size() > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? 16 : 10);
      } catch (...) {
      }
    }
  }
  if (!matched) {
    std::regex div("(\\d+)\\s+word\\(s\\)\\s+differ|divergences?\\s*=\\s*(\\d+)",
                   std::regex::icase);
    int64_t smallest = INT64_MAX;
    for (auto it = std::sregex_iterator(output.begin(), output.end(), div);
         it != std::sregex_iterator(); ++it) {
      try {
        smallest = std::min<int64_t>(
            smallest, std::stoll((*it)[1].matched ? (*it)[1].str() : (*it)[2].str()));
      } catch (...) {
      }
    }
    if (smallest != INT64_MAX)
      row["divergences"] = smallest;
  }
  if (classifySource(source) == "transcribed")
    row["c_source"] = "dcd 0x00000000";
  return row;
}
void Backend::resetRecent(const std::string &id) {
  HANDLE lock = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 100 && lock == INVALID_HANDLE_VALUE; ++attempt) {
    lock = CreateFileW((directory / "backend.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE)
      Sleep(10);
  }
  if (lock == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Statistics store busy; adaptive role unchanged");
  struct Guard {
    HANDLE h;
    ~Guard() { CloseHandle(h); }
  } guard{lock};
  auto stats = fileJson(directory / "stats.json");
  if (stats.contains(id)) {
    stats[id]["recent"] = Json::array();
    saveJson(directory / "stats.json", stats);
  }
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
      rows = driverResultRows(parsed);
    } else
      rows = jsonLines(results);
  }
  auto best = fileJson(directory / "stats-best.json");
  {
    std::lock_guard<std::mutex> sessionLock(sessionStatsMutex);
    auto &session = sessionStats[utf8(directory.wstring())];
    if (!session.is_object())
      session = Json::object();
    auto sessionBest = best;
    session[id] = updateAgentStats(session.value(id, Json::object()), rows, sessionBest);
  }
  auto updated = updateAgentStats(entry, rows, best);
  int attempts = updated.value("attempts", 0) - entry.value("attempts", 0);
  int claims = updated.value("declaredMatches", 0) - entry.value("declaredMatches", 0);
  entry = updated;
  Json harvest = Json::array();
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
    if (matched)
      harvest.push_back({{"name", row.value("name", std::string())},
                         {"sourceClassification", transcribed ? "transcribed" : "ok"},
                         {"declaredMatch", true},
                         {"verifiedGates", gates}});
  }
  if (!adaptive.empty())
    entry["adaptiveRole"] = adaptive;
  entry["phase"] = phase;
  entry["updated"] = std::time(nullptr);
  entry["verifiedGates"] = gates;
  stats[id] = entry;
  saveJson(directory / "stats.json", stats);
  saveJson(directory / "stats-best.json", best);
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
  if (m == "guide.parse")
    return parseGuide(a.at("text").get<std::string>(), a.value("tour", true));
  if (m == "guide.richText")
    return richTextRuns(a.value("text", std::string()));
  if (m == "guide.tour" || m == "guide.tips")
    return readGuide(directory, m == "guide.tour");
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
    for (auto key : {"reports", "safeMode", "useAgents", "autoLand", "allowNearMiss", "allowGhidra",
                     "animateBackground", "liveRefresh"})
      if (a.contains(key) && !a[key].is_boolean())
        throw std::runtime_error("Policy must be boolean: " + std::string(key));
    if (a.contains("agentFanout")) {
      if (!a["agentFanout"].is_number())
        throw std::runtime_error("Functions per sub-agent must be numeric");
    }
    if (a.contains("disabledTools")) {
      if (!a["disabledTools"].is_array())
        throw std::runtime_error("disabledTools must be an array");
      for (auto &id : a["disabledTools"])
        if (!id.is_string())
          throw std::runtime_error("Disabled tool identifiers must be strings");
    }
    auto prefs = fileJson(directory / "preferences.json");
    prefs.merge_patch(a);
    if (a.contains("agentFanout"))
      prefs["agentFanout"] = driverPolicy(Json::object(), a, 0).at("functionsPerAgent");
    saveJson(directory / "preferences.json", prefs);
    return prefs;
  }
  if (m == "update.stage") {
    auto profile = fileJson(directory / "connections.json").at("update.check");
    if (!profile.value("enabled", false) || !profile.value("allowUpdateDownloads", false))
      throw std::runtime_error(
          "Enable update.check and allowUpdateDownloads in your local Connections first");
    auto result = execute("update.check", Json::object());
    if (!result.value("ok", false))
      throw std::runtime_error("Update endpoint unavailable");
    if (result.at("update").at("state") != "available")
      return result.at("update");
    auto fetch = [&](const std::string &url, const std::string &method, const std::string &body,
                     const std::map<std::string, std::string> &headers) {
      if (processRunner && processRunner->isCancelled())
        throw std::runtime_error("Update cancelled");
      auto response = transport(url, method, body, headers);
      if (processRunner && processRunner->isCancelled())
        throw std::runtime_error("Update cancelled");
      return response;
    };
    return stagePortableUpdate(directory, result.at("data"),
                               profile.value("assetPrefix", std::string()),
                               fs::u8path(selfExecutable()), fetch);
  }
  if (m == "projects.importZip") {
    auto destination = fs::u8path(a.at("destination").get<std::string>());
    auto result =
        extractProjectZip(fs::u8path(a.at("archive").get<std::string>()), destination, settings);
    Runner local;
    auto &runner = processRunner ? *processRunner : local;
    auto log = directory / "logs" / ("zip-import-" + uniqueId() + ".log");
    auto init = runner.run({{"git", "-c", "core.hooksPath=", "init", "-b", "main"}, destination},
                           progressSink, log);
    if (init.code != 0)
      throw std::runtime_error("ZIP extracted but Git initialization failed; inspect " +
                               utf8(log.wstring()));
    auto descriptor = loadDescriptor(destination);
    auto id = "local:" + fingerprint(utf8(fs::weakly_canonical(destination).wstring()));
    execute("projects.register", {{"id", id},
                                  {"repository", utf8(destination.wstring())},
                                  {"descriptor", descriptor.document}});
    result["path"] = utf8(destination.wstring());
    result["id"] = id;
    result["log"] = utf8(log.wstring());
    result["notice"] =
        "Imported files are untracked. Preview and review your initial commit before publishing.";
    return result;
  }
  if (m == "projects.discover") {
    auto result = execute("network.read", {{"connection", "projects.registry"}});
    if (!result.value("ok", false))
      throw std::runtime_error("Registry unavailable; cached projects remain available");
    auto payload = result.at("data");
    auto rows = payload.is_array() ? payload : payload.value("projects", Json::array());
    if (!rows.is_array() || rows.size() > 1000)
      throw std::runtime_error("Registry must contain at most 1000 projects");
    auto list = fileJson(directory / "projects.json", Json::array());
    std::set<std::string> ids;
    for (auto &row : rows) {
      if (!row.is_object())
        throw std::runtime_error("Invalid registry row");
      auto id = row.value("id", std::string());
      if (id.empty())
        continue;
      if (id.size() > 4096 || id.find_first_of("\r\n") != id.npos || id.find('\0') != id.npos ||
          !ids.insert(id).second)
        throw std::runtime_error("Invalid or duplicate registry id");
      Json entry{{"id", id}};
      for (auto &known : list)
        if (known.at("id") == id)
          entry = known;
      for (auto key : {"title", "github", "tagline", "glyph"})
        if (row.contains(key) && !row[key].is_null()) {
          auto value = row.at(key).get<std::string>();
          if (!value.empty())
            entry[key] = value;
        }
      if (row.contains("descriptor")) {
        if (row["descriptor"].dump().size() > 1024 * 1024)
          throw std::runtime_error("Registry descriptor exceeds 1 MiB");
        auto parsed = parseDescriptor(row["descriptor"].dump());
        entry["descriptor"] = parsed.document;
        entry["title"] = parsed.title;
        entry["descriptorAt"] = std::time(nullptr);
      }
      bool found = false;
      for (auto &known : list)
        if (known.at("id") == id) {
          known = entry;
          found = true;
        }
      if (!found)
        list.push_back(entry);
    }
    noCredentials(list);
    saveJson(directory / "projects.json", list);
    return {{"discovered", ids.size()}, {"projects", execute("projects.list", {})}};
  }
  if (m == "projects.download") {
    auto profiles = fileJson(directory / "connections.json");
    if (!profiles.contains("projects.registry") ||
        !profiles["projects.registry"].value("enabled", false) ||
        !profiles["projects.registry"].value("allowDescriptorDownloads", false))
      throw std::runtime_error(
          "Enable projects.registry and allowDescriptorDownloads locally first");
    auto list = fileJson(directory / "projects.json", Json::array());
    for (auto &entry : list) {
      if (entry.at("id") != a.at("id"))
        continue;
      auto now = std::time(nullptr);
      auto at = entry.value("descriptorAt", int64_t(0));
      if (entry.contains("descriptor") && !a.value("force", false) && at <= now && now - at < 86400)
        return {{"cached", true}, {"project", entry}};
      try {
        auto github = entry.value("github", std::string());
        auto slug = githubSlug(github);
        if (slug.empty())
          throw std::runtime_error(
              "Descriptor needs a credential-free GitHub repository URL (HTTPS or Git SSH)");
        auto url = "https://raw.githubusercontent.com/" + slug + "/HEAD/tangos.json";
        auto response = transport(url, "GET", "", {});
        if (response.status != 200 || response.body.size() > 1024 * 1024)
          throw std::runtime_error("Descriptor download failed or exceeds 1 MiB");
        auto parsed = parseDescriptor(response.body);
        noCredentials(parsed.document);
        entry["descriptor"] = parsed.document;
        entry["title"] = parsed.title;
        entry["descriptorAt"] = now;
        saveJson(directory / "projects.json", list);
        return {{"cached", false}, {"project", entry}};
      } catch (const std::exception &error) {
        if (!entry.contains("descriptor"))
          throw;
        return {{"cached", true}, {"stale", true}, {"warning", error.what()}, {"project", entry}};
      }
    }
    throw std::runtime_error("Unknown registered project");
  }
  if (m == "projects.list") {
    auto rows = fileJson(directory / "projects.json", Json::array());
    for (auto &entry : rows) {
      auto path = entry.value("repository", std::string());
      bool cloned = !path.empty() && fs::exists(fs::u8path(path) / ".git");
      entry["cloned"] = cloned;
      entry["path"] = cloned ? Json(path) : Json();
      entry["active"] = settings.activeProject.empty() ? cloned && path == settings.repository
                                                       : entry.at("id") == settings.activeProject;
      if (!entry.contains("title"))
        entry["title"] = entry.contains("descriptor")
                             ? parseDescriptor(entry["descriptor"].dump()).title
                             : entry.at("id").get<std::string>();
      if (!entry.contains("glyph")) {
        auto title = wide(entry.at("title").get<std::string>());
        title.resize(std::min(size_t(2), title.size()));
        if (!title.empty())
          CharUpperBuffW(title.data(), static_cast<DWORD>(title.size()));
        entry["glyph"] = title.empty() ? "??" : utf8(title);
      }
    }
    return rows;
  }
  if (m == "projects.open") {
    for (auto &entry : fileJson(directory / "projects.json", Json::array())) {
      if (entry.at("id") != a.at("id"))
        continue;
      auto path = entry.value("repository", std::string());
      if (!path.empty() && fs::exists(fs::u8path(path) / ".git")) {
        Runner runner;
        Repository local(runner, fs::u8path(path), settings);
        return {{"id", entry.at("id")}, {"path", utf8(local.root.wstring())}, {"cloned", true}};
      }
      if (!entry.contains("descriptor"))
        throw std::runtime_error("This local project moved; choose its new folder");
      auto descriptor = parseDescriptor(entry["descriptor"].dump());
      uint64_t hash = 1469598103934665603ULL;
      for (unsigned char c : entry.at("id").get<std::string>()) {
        hash ^= c;
        hash *= 1099511628211ULL;
      }
      auto view = confinedPath(directory, "project-views/" + std::to_string(hash));
      fs::create_directories(view);
      write(confinedPath(view, "tangos.json"), descriptor.document.dump(2));
      write(confinedPath(view, ".tangos-lite-viewer-only"),
            "Managed metadata cache; not a Git checkout\n");
      return {{"id", entry.at("id")},
              {"path", utf8(view.wstring())},
              {"cloned", false},
              {"title", descriptor.title},
              {"descriptor", descriptor.document}};
    }
    throw std::runtime_error("Unknown registered project");
  }
  if (m == "projects.register") {
    noCredentials(a);
    auto list = fileJson(directory / "projects.json", Json::array());
    auto entry = a;
    auto id = entry.at("id").get<std::string>();
    if (id.empty() || id.size() > 4096 || id.find_first_of("\r\n") != std::string::npos ||
        id.find('\0') != std::string::npos)
      throw std::runtime_error("Project id must be a single nonempty line under 4096 bytes");
    if (entry.contains("repository")) {
      Runner r;
      Repository repo(r, fs::u8path(entry["repository"].get<std::string>()), settings);
      entry["repository"] = utf8(repo.root.wstring());
    } else if (!entry.contains("descriptor"))
      throw std::runtime_error("A remote project needs a local descriptor; fetching it requires "
                               "your enabled connection");
    if (entry.contains("descriptor")) {
      auto parsed = parseDescriptor(entry["descriptor"].dump());
      entry["descriptor"] = parsed.document;
      entry["title"] = parsed.title;
    }
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
    auto result = execute("network.read", args);
    if (m == "update.check" && result.value("ok", false))
      result["update"] = updateStatus("0.25.0", result.at("data"));
    return result;
  }
  if (m == "git.clone") {
    auto url = a.at("url").get<std::string>();
    if (url.empty() || url[0] == '-' || url.find('?') != url.npos || url.find('#') != url.npos ||
        (url.find("https://") == 0 && url.substr(8).find('@') != url.npos))
      throw std::runtime_error("Use a credential-free Git URL or a local repository path; "
                               "authentication belongs in your Git credential helper");
    auto dest = a.contains("destination") ? fs::u8path(a.at("destination").get<std::string>())
                                          : directory / "clones" / uniqueId();
    if (!dest.is_absolute())
      throw std::runtime_error("Clone destination must be an absolute new folder");
    if (fs::exists(dest))
      throw std::runtime_error("Clone destination already exists; choose a new folder");
    if (url.find("://") != url.npos && url.rfind("https://", 0) != 0 && url.rfind("ssh://", 0) != 0)
      throw std::runtime_error("Clone supports HTTPS, SSH or a local repository");
    if (url.find("::") != url.npos)
      throw std::runtime_error("External Git helpers are not allowed");
    fs::create_directories(dest.parent_path());
    Runner r;
    auto log = directory / (uniqueId() + "-clone.log");
    auto &cloneRunner = processRunner ? *processRunner : r;
    auto result = cloneRunner.run(
        {{"git", "clone", "--progress", "--", url, utf8(dest.wstring())}, directory}, progressSink,
        log);
    return {{"exit", result.code},
            {"cancelled", cloneRunner.isCancelled()},
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
  if (m == "stats.session") {
    std::lock_guard<std::mutex> lock(sessionStatsMutex);
    auto found = sessionStats.find(utf8(directory.wstring()));
    return found == sessionStats.end() ? Json::object() : found->second;
  }
  if (m == "stats.clear") {
    saveJson(directory / "stats.json", Json::object());
    saveJson(directory / "stats-best.json", Json::object());
    std::lock_guard<std::mutex> lock(sessionStatsMutex);
    sessionStats.erase(utf8(directory.wstring()));
    return {{"cleared", true}};
  }
  if (m == "bug.report") {
    if (processRunner && processRunner->isCancelled())
      throw std::runtime_error("Report preparation cancelled");
    auto description = redact(a.value("description", std::string()), secrets);
    if (description.size() > 65536 || !blockedBlob(description).empty())
      throw std::runtime_error("Report description contains protected content or exceeds 64 KiB");
    std::vector<std::pair<std::string, std::string>> images;
    Json exportedShots = Json::array();
    for (auto shot : a.value("_validatedAttachments", Json::array())) {
      auto bytes = screenshotBytes(fs::u8path(shot.at("path").get<std::string>()));
      if (fingerprint(bytes) != shot.at("sha256"))
        throw std::runtime_error("Screenshot changed after preview; preview the report again");
      images.emplace_back(shot.at("file").get<std::string>(), std::move(bytes));
      shot.erase("path");
      exportedShots.push_back(shot);
    }
    if (processRunner && processRunner->isCancelled())
      throw std::runtime_error("Report preparation cancelled before export");
    auto folder = directory / "exports" / ("bug-report-" + uniqueId());
    fs::create_directories(folder);
    Json debug = {{"app", "TangOS Lite"},
                  {"version", "0.25.0"},
                  {"portOnly", settings.portOnly},
                  {"project", settings.activeProject},
                  {"connections", Json::array()},
                  {"screenshots", exportedShots},
                  {"os", "Windows"},
                  {"architecture", "x64"},
                  {"recentActivity", Json::array()}};
    auto activity = activityBus().snapshot(utf8(repository.wstring()));
    for (size_t i = activity.size() > 20 ? activity.size() - 20 : 0; i < activity.size(); ++i) {
      Json run = Json::object();
      for (auto field : {"toolId", "label", "status", "exitCode", "startedAt", "finishedAt"})
        if (activity[i].contains(field))
          run[field] = activity[i][field];
      debug["recentActivity"].push_back(run);
    }
    for (auto &name : {"connections.json"}) {
      auto profiles = fileJson(directory / name);
      for (auto it = profiles.begin(); it != profiles.end(); ++it)
        debug["connections"].push_back(
            {{"name", it.key()}, {"enabled", it.value().value("enabled", false)}});
    }
    debug = redactMetadata(debug, secrets);
    auto markdown =
        "# TangOS Lite bug report\n\n" + description + "\n\n```json\n" + debug.dump(2) + "\n```\n";
    write(folder / "bug-report.md", markdown);
    saveJson(folder / "debug.json", debug);
    for (auto &image : images)
      write(folder / fs::u8path(image.first), image.second);
    return {{"folder", utf8(folder.wstring())},
            {"screenshots", exportedShots},
            {"markdown", markdown},
            {"notice", "Review the report before sharing; no message was sent"}};
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
  if (m == "policy.usage")
    return driverUsage(a.value("output", std::string()), a.value("productive", false),
                       a.value("elapsedMs", uint64_t(0)), a.value("streak", 0));
  if (m == "policy.presence")
    return agentPresence(a.at("kind").get<std::string>(), a.value("lastSeen", int64_t(0)),
                         a.value("live", false), a.at("now").get<int64_t>());
  if (m == "policy.controllerView")
    return controllerView(a.at("agent"), a.value("batches", Json::array()),
                          a.value("runs", Json::array()));
  if (m == "policy.classify")
    return {{"classification", classifySource(a.at("source").get<std::string>())}};
  if (m == "policy.statistics") {
    auto best = a.value("best", Json::object());
    return {{"entry", updateAgentStats(a.value("entry", Json::object()), a.at("rows"), best)},
            {"best", best}};
  }
  if (m == "policy.source")
    return sourceEnvelope(a.at("source").get<std::string>(), a.value("kind", std::string("src")),
                          a.value("path", std::string()));
  if (m == "atlas.source" && ((!a.contains("id") && !a.contains("path")) ||
                              (a.contains("id") && (!a["id"].is_string() || repository.empty()))))
    return nullptr;
  if (m == "policy.batches") {
    BatchBook book;
    book.restore({{"batches", a.at("batches")}});
    auto action = a.at("action").get<std::string>();
    if (action == "remove")
      book.remove(a.value("id", std::string()));
    else if (action == "reorder")
      book.reorder(a.value("id", std::string()), a.value("direction", 1));
    else if (action == "clearDone")
      book.clearDone();
    else if (action == "prune")
      book.complete("__none__", Json::array());
    else
      throw std::runtime_error("Unknown batch policy operation");
    return book.snapshot();
  }
  if (m == "policy.color")
    return {{"color", atlasColor(a.at("row"), a.value("authors", false), a.value("nearMiss", true),
                                 a.value("aliases", std::map<std::string, std::string>{}),
                                 a.value("colors", std::map<std::string, std::string>{}))}};
  if (m == "policy.driverTokens") {
    Json best = Json::object();
    auto stats = updateAgentStats(Json::object(), driverResultRows(a), best);
    return {{"tokensIn", stats.value("tokensIn", 0LL)},
            {"tokensOut", stats.value("tokensOut", 0LL)}};
  }
  if (m == "policy.sort") {
    auto rows = parseAtlas(Json{{"functions", a.at("functions")}}.dump());
    std::vector<size_t> indices(rows.size());
    std::iota(indices.begin(), indices.end(), 0);
    Json result = Json::array();
    for (auto index : atlasOrder(rows, indices, a.value("sort", std::string("unmatched"))))
      result.push_back(rows[index].id);
    return result;
  }
  if (m == "policy.layout") {
    auto rows = parseAtlas(Json{{"functions", a.at("functions")}}.dump());
    std::vector<size_t> indices(rows.size());
    std::iota(indices.begin(), indices.end(), 0);
    auto tiles = atlasLayout(rows, indices, a.at("width").get<double>(),
                             a.at("height").get<double>(), a.value("mode", std::string("ov")),
                             a.value("aliases", std::map<std::string, std::string>{}));
    Json result = Json::array();
    for (auto &tile : tiles)
      result.push_back({{"id", rows[tile.index].id},
                        {"x", tile.x},
                        {"y", tile.y},
                        {"width", tile.width},
                        {"height", tile.height}});
    return result;
  }
  if (m == "policy.pool") {
    auto rows = parseAtlas(Json{{"functions", a.at("functions")}}.dump());
    auto live = a.value("liveMatched", Json::array());
    for (auto &row : rows)
      if (std::find(live.begin(), live.end(), row.name) != live.end())
        row.state = "matched";
    return poolDifficulty(rows);
  }
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
  if (m == "atlas.source" && a.contains("id")) {
    if (a.contains("srcPath") && a["srcPath"].is_string() &&
        !a["srcPath"].get<std::string>().empty()) {
      try {
        auto result = execute("atlas.source", {{"path", a["srcPath"]}});
        return sourceEnvelope(result.at("source").get<std::string>(), "src", a["srcPath"]);
      } catch (
          const std::exception &) { /* best-effort fallback; privacy/path guards stay enforced */
      }
    }
    try {
      auto db = execute("atlas.load", Json::object());
      for (auto &row : db.value("functions", Json::array()))
        if (row.contains("id") && row["id"] == a["id"] && row.contains("disasm") &&
            row["disasm"].is_string() && !row["disasm"].get<std::string>().empty()) {
          auto text = row["disasm"].get<std::string>();
          if (!blockedBlob(text).empty())
            return nullptr;
          return sourceEnvelope(text, "disasm");
        }
    } catch (const std::exception &) {
    }
    return nullptr;
  }
  if (m == "atlas.source") {
    auto path = confinedPath(repository, a.at("path"));
    auto readSettings = settings;
    readSettings.portOnly = false; // Reading public source does not authorize editing it.
    auto relative = path.lexically_relative(fs::weakly_canonical(repository)).generic_u8string();
    auto why = blockedPath(relative, readSettings);
    if (!why.empty())
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
        attempts.push_back(historySummary(r, attempts.size()));
    for (auto &r : jsonLines(confinedPath(
             repository, conventions.value("nearMissDb", std::string("nearmiss/db.jsonl")))))
      if (sameFunction(r, a))
        tips.push_back(r);
    Json best = nullptr;
    double score = 1e100;
    for (auto &tip : tips) {
      auto div = historyNumber(tip, "divergences");
      double candidate = div.is_null() ? 9999. : div.get<double>();
      if (candidate < score) {
        score = candidate;
        best = {{"divergences", div},
                {"source", historyString(tip, "source")},
                {"srcPath", historyString(tip, "srcPath")},
                {"hasCSource", tip.contains("c_source") && tip["c_source"].is_string() &&
                                   !tip["c_source"].get<std::string>().empty()}};
        if (best["source"].is_null())
          best["source"] = historyString(tip, "label");
      }
    }
    auto attemptsPath =
        conventions.value("attemptsPath", std::string("config/match_attempts.jsonl"));
    auto nearMissPath = conventions.value("nearMissDb", std::string("nearmiss/db.jsonl"));
    Json note = nullptr;
    if (!fs::exists(confinedPath(repository, attemptsPath)) &&
        !fs::exists(confinedPath(repository, nearMissPath)))
      note = "No attempt log or near-miss DB in this repo yet.";
    else if (attempts.empty() && best.is_null())
      note = "Nothing logged for this function yet — open field for a first try.";
    std::ostringstream generatedId;
    generatedId << a.value("module", std::string()) << ":0x" << std::hex
                << (a.contains("addr") ? number(a["addr"]) : 0);
    return {{"functionId", a.value("functionId", a.value("id", generatedId.str()))},
            {"name", a.value("name", std::string())},
            {"attempts", orderHistory(attempts)},
            {"tip", best},
            {"attemptsPath", utf8(confinedPath(repository, attemptsPath).wstring())},
            {"nearMissPath", utf8(confinedPath(repository, nearMissPath).wstring())},
            {"note", note}};
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
          row.value("matched", false) || exemptTarget(row))
        continue;
      ready.push_back(row);
    }
    auto name = "adopted-" + uniqueId() + ".json";
    saveJson(directory / "queues" / name, ready);
    return {{"targets", ready}, {"path", utf8((directory / "queues" / name).wstring())}};
  }
  if (m == "checks.list" || m == "checks.run") {
    Json rows = Json::array();
    for (auto &check : discoverChecks(repository, settings)) {
      if (m == "checks.list")
        rows.push_back({{"name", check.name},
                        {"available", check.available},
                        {"requirement", check.requirement},
                        {"command", preview(check.command)}});
      else if (check.name == a.at("name")) {
        if (!check.available)
          throw std::runtime_error(check.requirement);
        check.command.environment = secrets;
        check.command.activityTool = check.name;
        check.command.activityLabel = check.name;
        check.command.activityReadOnly = true;
        check.command.activityRepository = repository;
        Runner local;
        auto &runner = processRunner ? *processRunner : local;
        auto log = directory / "logs" / ("check-" + uniqueId() + ".log");
        auto result = runner.run(check.command, progressSink, log);
        return {{"exit", result.code},
                {"output", result.output},
                {"log", utf8(log.wstring())},
                {"cancelled", result.code == ERROR_CANCELLED}};
      }
    }
    if (m == "checks.list")
      return rows;
    throw std::runtime_error("Unknown discovered check");
  }
  if (m == "tools.list") {
    auto prefs = fileJson(directory / "preferences.json");
    Json out = Json::array();
    for (auto &t : descriptor.tools)
      out.push_back({{"id", t.id},
                     {"label", t.label},
                     {"readOnly", t.readOnly},
                     {"enabled", enabledTool(prefs, t.id)}});
    return out;
  }
  if (m == "atlas.generate") {
    if (descriptor.generatorId.empty())
      throw std::runtime_error(
          "Configure data.generate in tangos.json before regenerating Atlas data");
    auto result = execute("tools.run", {{"tool", descriptor.generatorId},
                                        {"values", {{"out", descriptor.database}}}});
    if (result.value("exit", 1) == 0 && !result.value("cancelled", false))
      result["atlas"] = execute("atlas.load", Json::object());
    return result;
  }
  if (m == "tools.run") {
    auto &tool = descriptor.tool(a.at("tool"));
    if (!descriptor.generatorId.empty() && tool.id == descriptor.generatorId)
      validateAtlasOutput(descriptor, a.value("values", Json::object()), repository, settings);
    if (!enabledTool(fileJson(directory / "preferences.json"), tool.id))
      throw std::runtime_error("Tool disabled by user policy");
    if (!tool.readOnly && settings.portOnly)
      throw std::runtime_error("Mutating primary-checkout tools blocked in port-only mode");
    if (!tool.readOnly && fileJson(directory / "preferences.json").value("safeMode", false))
      throw std::runtime_error("User safe mode prohibits mutating tools");
    auto command = toolCommand(descriptor, tool, a.value("values", Json::object()), repository,
                               true, a.value("apply", false));
    command.environment = secrets;
    command.activityTool = tool.id;
    command.activityArguments = a.value("values", Json::object()).dump();
    command.activityLabel = tool.label;
    command.activityReadOnly = tool.readOnly;
    command.activityRepository = repository;
    Runner local;
    auto &r = processRunner ? *processRunner : local;
    auto log = directory / (uniqueId() + "-tool.log");
    auto result = r.run(command, progressSink, log);
    return {{"exit", result.code},
            {"output", result.output},
            {"log", utf8(log.wstring())},
            {"cancelled", result.code == ERROR_CANCELLED}};
  }
  Runner runner;
  Repository repo(runner, repository, settings);
  if (m == "git.status")
    return {{"status", repo.status()}};
  if (m == "git.syncPreview" || m == "git.sync") {
    auto ref = a.value("ref", std::string());
    if (ref.empty())
      ref = trim(repo.git({"rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{upstream}"}));
    if (!validRef(ref))
      throw std::runtime_error("Choose a valid existing upstream ref; fetch it before previewing");
    auto target = trim(repo.git({"rev-parse", "--verify", ref + "^{commit}"}));
    if (!std::regex_match(target, std::regex("[0-9a-f]{40,64}")))
      throw std::runtime_error("Cannot resolve synchronization target");
    Json remove = Json::array(), kept = Json::array(), blocked = Json::array();
    for (auto &path : split(repo.git({"diff", "--name-only", "-z", "HEAD", target}), '\0'))
      if (!path.empty() && !blockedPath(path, settings).empty())
        blocked.push_back(path);
    for (auto &change :
         parseStatus(repo.git({"status", "--porcelain=v1", "-z", "--untracked-files=all"}))) {
      auto reason = blockedPath(change.path, settings);
      if (change.xy == "??") {
        if (!reason.empty())
          kept.push_back(change.path);
        else {
          confinedPath(repository, change.path);
          remove.push_back(change.path);
        }
      } else if (!reason.empty())
        blocked.push_back(change.path);
    }
    auto preview =
        Json{{"ref", ref},
             {"target", target},
             {"status", repo.status()},
             {"diff", repo.git({"diff", "--binary", "HEAD", target})},
             {"unpushed", repo.git({"log", "--oneline", target + "..HEAD"})},
             {"remove", remove},
             {"preserved", kept},
             {"blocked", blocked},
             {"notice", "Confirmed sync backs up allowed local changes, pins current history for "
                        "recovery, resets to this exact commit, and removes only listed allowed "
                        "untracked files. Ignored/protected assets are preserved."}};
    if (m == "git.syncPreview")
      return preview;
    if (!blocked.empty())
      throw std::runtime_error(
          "Synchronization would modify protected paths; inspect git.syncPreview blocked list");
    auto backup = execute("git.backup", Json::object());
    auto log = directory / (uniqueId() + "-sync.log");
    auto result = runner.run({{"git", "reset", "--hard", target}, repository}, {}, log);
    if (result.code != 0)
      return {{"exit", result.code},
              {"output", result.output},
              {"backup", backup},
              {"log", utf8(log.wstring())}};
    for (auto &name : remove) {
      auto path = confinedPath(repository, name.get<std::string>());
      // Only remove the concrete files shown in the preview, never recursively clean a directory.
      if (fs::is_regular_file(path) && !fs::is_symlink(fs::symlink_status(path)))
        fs::remove(path);
    }
    return {{"exit", 0},
            {"target", target},
            {"output", result.output},
            {"backup", backup},
            {"log", utf8(log.wstring())}};
  }
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
    auto recoveryRef = "refs/tangos/backups/" + utf8(dest.filename().wstring());
    repo.git({"update-ref", recoveryRef, "HEAD"});
    saveJson(dest / "manifest.json", {{"recoveryRef", recoveryRef},
                                      {"head", repo.git({"rev-parse", "HEAD"})},
                                      {"files", files},
                                      {"excluded", skipped}});
    return {{"backup", utf8(dest.wstring())},
            {"recoveryRef", recoveryRef},
            {"files", files},
            {"excluded", skipped}};
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
    if (requirements.contains("compiler") && requirements["compiler"].is_string()) {
      auto name = requirements["compiler"].get<std::string>();
      bool found = false;
      std::string detail;
      for (auto candidate : {"tools/" + name, "tools/" + name + ".exe", name, name + ".exe"}) {
        auto path = confinedPath(repository, candidate);
        if (fs::exists(path)) {
          found = true;
          detail = candidate;
          break;
        }
      }
      if (!found) {
        auto result = runner.run({{"where.exe", name}, repository});
        found = result.code == 0;
        if (found)
          detail = result.output;
      }
      checks.push_back(
          {{"name", "Compiler (" + name + ")"},
           {"available", found},
           {"detail", detail},
           {"fix", "Put your own compiler and any required license in tools/" + name +
                       ". Follow the repository setup notes; no compiler is downloaded."}});
    }
    if (requirements.value("rom", false)) {
      bool found = false;
      std::string detail;
      for (auto relative : {"extracted", "orig", "baserom", "build/extracted", "expected"}) {
        if (fs::exists(confinedPath(repository, relative))) {
          found = true;
          detail = relative;
          break;
        }
      }
      checks.push_back({{"name", "Extracted ROM"},
                        {"available", found},
                        {"detail", detail},
                        {"fix", "Extract your own locally dumped ROM using the repository setup "
                                "instructions. Keep its output excluded from Git."}});
    }

    for (auto &pkg : requirements.value("pythonPackages", Json::array())) {
      auto name = pkg.get<std::string>(), module = name, folded = name;
      std::transform(folded.begin(), folded.end(), folded.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (folded == "pyelftools" || folded == "py-elftools")
        module = "elftools";
      else if (folded == "pillow")
        module = "PIL";
      else if (folded == "pyyaml")
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
