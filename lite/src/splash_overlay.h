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
struct SplashBubbleFrame {
  double rise, opacity;
  bool visible;
};
SplashBubbleFrame splashBubbleFrame(double progress);
class SplashOverlay {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  SplashOverlay(HWND parent, std::string label, std::function<void()> swap, bool hold = false);
  ~SplashOverlay();
  bool open() const;
  void release();
};
} // namespace lite
