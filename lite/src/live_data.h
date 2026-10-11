#pragma once
#include "descriptor.h"
#include <functional>
namespace lite {
class SseDecoder {
  std::string pending;

public:
  std::vector<Json> feed(const std::string &chunk);
};
Json normalizeAtlasFeed(const std::string &method, const Json &raw);
Json overlayLiveAtlas(Json local, const Json &live);
bool reuseLiveAtlas(int64_t savedAt, int64_t now, bool force);
class GithubCredits {
  Json result = {
      {"logins", Json::array()}, {"keyToLogin", Json::object()}, {"prAuthors", Json::array()}};

public:
  void contributors(const Json &rows);
  void commits(const Json &rows);
  void pulls(const Json &rows);
  Json value() const { return result; }
};
} // namespace lite