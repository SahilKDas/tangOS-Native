#pragma once
#include "mcp.h"
#include "backend.h"
#include "skin.h"
#include <functional>
namespace lite {
constexpr UINT CONSOLE_RELOAD = WM_APP + 40, CONSOLE_PICK_REPO = WM_APP + 41,
               CONSOLE_OPEN_REPO = WM_APP + 42;
class ConsoleUI {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  ConsoleUI(HWND parent, HFONT font, fs::path repository, fs::path data, Settings settings,
            std::function<void()> gitTools,
            std::function<void(const Settings &)> savePreferences = {}, bool viewerOnly = false,
            std::string module = {}, std::function<void(Json)> draftAdded = {},
            bool remoteOnly = false, Transport transport = requestHttp);
  ~ConsoleUI();
  void show(bool visible, bool atlas = false);
  void resize(int width, int height);
  bool running() const;
  void stop();
  void smokeRemote(const fs::path &directory, const std::function<void(const fs::path &)> &capture);
  void smokeScreens(const fs::path &directory,
                    const std::function<void(const fs::path &)> &capture);
};
} // namespace lite
