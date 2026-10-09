#pragma once
#include "descriptor.h"
#include <mutex>
namespace lite {
// Process-local, bounded activity; complete output remains in the Runner's log.
class ActivityBus {
  mutable std::mutex mutex;
  std::map<std::string, Json> runs;
  std::vector<std::string> order;

public:
  void publish(const Json &event);
  Json snapshot(const std::string &repository = {}) const;
  Json controllerSnapshot(const std::string &repository) const;
  void clear();
};
ActivityBus &activityBus();
int64_t activityNow();
Json activityStreams(const std::string &output);
Json parseStatisticsJson(const std::string &source);
std::string sizeRecommendation(const Json &bySize, const Json &order = Json::array());
Json measuredRole(const Json &stats);
Json automaticRole(const Json &agent);
Json effortPolicy(const Json &agent);
Json driverPolicy(const Json &agent, const Json &preferences, size_t targets);
} // namespace lite
