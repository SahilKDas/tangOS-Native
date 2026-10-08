#pragma once
#include "fleet.h"
#include <functional>
namespace lite {
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
