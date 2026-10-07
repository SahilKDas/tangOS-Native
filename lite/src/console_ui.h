#pragma once
#include "mcp.h"
#include "skin.h"
#include <functional>
namespace lite {
class ConsoleUI {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  ConsoleUI(HWND parent, HFONT font, fs::path repository, fs::path data, Settings settings,
            std::function<void()> gitTools,
            std::function<void(const Settings &)> savePreferences = {});
  ~ConsoleUI();
  void show(bool visible, bool atlas = false);
  void resize(int width, int height);
  bool running() const;
  void stop();
  void smokeScreens(const fs::path &directory,
                    const std::function<void(const fs::path &)> &capture);
};
} // namespace lite
