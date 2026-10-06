#pragma once
#include "platform.h"
namespace lite {
class Repository {
  Runner &runner;

public:
  fs::path root;
  Settings settings;
  Repository(Runner &r, const fs::path &selected, const Settings &s);
  std::string git(const Args &args);
  std::string status();
  std::string safetyIndex();
  std::string commitPreview();
  std::string pushPreview(const std::string &remote, const std::string &branch);
  std::string agentHandoff();
  Command action(const std::string &name, const std::string &remote, const std::string &ref,
                 const std::string &text);
  Command check(size_t index);
};
} // namespace lite
