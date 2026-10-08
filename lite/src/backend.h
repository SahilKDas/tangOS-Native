#pragma once
#include "descriptor.h"
#include "network.h"
#include <functional>
#include <memory>
namespace lite {
class RemoteLease;
using Transport =
    std::function<HttpResponse(const std::string &, const std::string &, const std::string &,
                               const std::map<std::string, std::string> &)>;
Json parseClaimRows(const std::string &markdown);
bool heldTarget(const Json &row, const Json &claims);
bool enabledTool(const Json &preferences, const std::string &id);
std::string classifySource(const std::string &source);
Json poolDifficulty(const std::vector<AtlasFunction> &functions);
std::string adaptiveRole(const std::string &role, int attempts, int matches, const Json &pool);
Json driverUsage(const std::string &output, bool productive, uint64_t elapsedMs, int streak);
bool productiveDriver(const Json &result);
Json driverResultRows(const Json &result);
Json updateAgentStats(Json entry, const Json &rows, Json &best);
class Backend {
  friend class RemoteLease;
  fs::path repository, directory;
  Settings settings;
  Transport transport;
  std::map<std::string, std::string> secrets;
  Json execute(const std::string &method, const Json &args);

public:
  Backend(fs::path repo, fs::path data, Settings prefs,
          std::map<std::string, std::string> keys = {}, Transport http = requestHttp);
  static bool mutation(const std::string &method, const Json &args);
  static Json catalog();
  Json invoke(const std::string &method, Json args = Json::object());
  void resetRecent(const std::string &id);
  void recordAgent(const std::string &id, const fs::path &results, const std::string &phase,
                   int verifiedGates, const std::string &adaptive = "");
  std::unique_ptr<RemoteLease> reserve(const Json &targets, const std::string &agent,
                                       std::function<void()> cancel);
};
class RemoteLease {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  RemoteLease(Backend backend, Json targets, std::string agent, std::function<void()> cancel);
  ~RemoteLease();
  void check();
};
} // namespace lite
