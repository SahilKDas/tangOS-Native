#pragma once
#include "descriptor.h"
namespace lite {
struct ClientSetup {
  fs::path path;
  std::string baseline;
  Json merged, outcome;
};
fs::path clientConfigPath(const std::string &client);
ClientSetup previewClientSetup(const std::string &client, const fs::path &executable,
                               const fs::path &connection, const std::string &agent,
                               const fs::path &path = {});
Json installClientSetup(const ClientSetup &plan);
} // namespace lite
