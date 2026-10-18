#pragma once
#include "core.h"
#include <atomic>
#include <functional>
namespace lite {
struct Result {
  unsigned long code;
  std::string output;
};
using Sink = std::function<void(const std::string &)>;
class Runner {
  std::atomic<bool> cancelled{false};

public:
  void cancel() { cancelled = true; }
  void reset() { cancelled = false; }
  bool isCancelled() const { return cancelled; }
  // With a sink, output is streamed rather than copied into Result.output. Tee a bounded
  // semantic response or inspect the durable log when parsing streamed command results.
  Result run(const Command &c, const Sink &sink = {}, const fs::path &log = {});
};
void atomicWrite(const fs::path &path, const std::string &content);
fs::path localData();
std::string uniqueId();
std::string sha256File(const fs::path &path);
std::string selfExecutable();
} // namespace lite
