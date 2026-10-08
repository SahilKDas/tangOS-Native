#pragma once
#include "fleet.h"
#include <functional>
namespace lite {
Json mcpClientConfiguration(const std::string &client, const fs::path &executable,
                            const fs::path &connection, const std::string &agent);
int runMcpStdio(const fs::path &connection, const std::string &agent);
class McpServer {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  McpServer(Fleet &fleet, Descriptor descriptor, fs::path config, unsigned short port = 0);
  ~McpServer();
  unsigned short port() const;
  std::string configuration() const;
  Json state() const;
};
} // namespace lite
