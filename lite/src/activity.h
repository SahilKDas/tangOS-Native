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
  void clear();
};
ActivityBus &activityBus();
int64_t activityNow();
Json activityStreams(const std::string &output);
std::string sizeRecommendation(const Json &bySize);
Json measuredRole(const Json &stats);
} // namespace lite
