#pragma once
#include "mcp.h"
#include "backend.h"
#include "skin.h"
#include <functional>
namespace lite {
constexpr UINT CONSOLE_RELOAD = WM_APP + 40, CONSOLE_PICK_REPO = WM_APP + 41,
               CONSOLE_OPEN_REPO = WM_APP + 42, CONSOLE_UPDATE_INSTALL = WM_APP + 43,
               CONSOLE_UPDATE_STAGED = WM_APP + 44, CONSOLE_PICK_VIEWER = WM_APP + 45,
               CONSOLE_TAB_STATE = WM_APP + 46, CONSOLE_DEBUG_SNAPSHOT = WM_APP + 47;
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
  void show(bool visible, bool atlas = false, bool transition = false);
  void resize(int width, int height);
  bool running() const;
  void stop();
  void openPanel(const std::string &panel);
  Json debugState() const;
  void smokeDisplay(const fs::path &directory,
                    const std::function<void(const fs::path &)> &capture);
  void smokeRemote(const fs::path &directory, const std::function<void(const fs::path &)> &capture);
  void smokeScreens(const fs::path &directory,
                    const std::function<void(const fs::path &)> &capture);
};
} // namespace lite
