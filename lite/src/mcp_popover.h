#pragma once
#include "descriptor.h"
#include <functional>
#include <memory>
#include <windows.h>
namespace lite {
enum class McpPopoverAction {
  toggle = 8101,
  copyConfiguration,
  connectClaude,
  copyPrompt,
  advanced
};
std::string mcpContactAge(int64_t now, int64_t timestamp);
class McpPopover {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  // Snapshot must contain public status only, never bearer tokens or provider keys.
  McpPopover(HWND owner, std::function<Json()> snapshot,
             std::function<void(McpPopoverAction)> action);
  ~McpPopover();
  void show(HWND anchor);
  HWND window() const;
};
} // namespace lite