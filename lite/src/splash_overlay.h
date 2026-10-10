#pragma once
#include "core.h"
#include <functional>
#include <windows.h>
namespace lite {
struct SplashFrame {
  double opacity, scale, translateY, centerOpacity;
  bool swap, finished;
};
SplashFrame splashFrame(double milliseconds);
class SplashOverlay {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  SplashOverlay(HWND parent, std::string label, std::function<void()> swap);
  ~SplashOverlay();
  bool open() const;
};
} // namespace lite
