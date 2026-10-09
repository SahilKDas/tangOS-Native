#pragma once
#include "descriptor.h"
namespace lite {
struct HelperState {
  bool open = false, unread = false;
  size_t index = 0;
  Json tips = Json::array(), note = Json::object();
  HelperState(const Json &preferences, Json messages, Json announcement, bool firstRun);
  Json messages() const;
  void markRead(Json &preferences);
  void toggle(Json &preferences);
  void close(Json &preferences);
  void next(int direction);
};
Json currentAnnouncement();
Json richTextRuns(const std::string &text);
Json updateStatus(const std::string &current, const Json &release);
std::string supportResultText(const Json &result);
Json updatePresentation(const Json &downloaded, const Json &current);
Json parseGuide(std::string text, bool tour);
Json readGuide(const fs::path &directory, bool tour);
} // namespace lite
