#pragma once
#include "backend.h"
#include <windows.h>
namespace lite {
class ReportDialog {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  ReportDialog(HWND owner, HFONT font, fs::path repository, fs::path data, Settings settings,
               std::map<std::string, std::string> secrets, bool showExports = true);
  ~ReportDialog();
  void show();
  void close();
  void tick();
  bool isOpen() const;
  bool running() const;
  void stop();
  HWND window() const;
  void setDescription(const std::string &description);
  void attach(const fs::path &path);
  void prepare();
  Json result() const;
  void saveSnapshot(const fs::path &path) const;
};
} // namespace lite
