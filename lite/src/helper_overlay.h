#pragma once
#include "help.h"
#include "skin.h"
#include <memory>
namespace lite {
class HelperOverlay {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  HelperOverlay(HWND owner, HWND backdrop, fs::path data, bool viewerOnly);
  ~HelperOverlay();
  void position(int width, int height, bool visible);
  void tick();
  Json snapshot() const;
  void smoke(const fs::path &directory);
};
} // namespace lite
