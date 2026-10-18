#pragma once
#include <windows.h>
namespace lite {
struct PopoverFrame {
  double scale, opacity, translateY;
  bool finished;
};
PopoverFrame popoverFrame(double milliseconds, bool closing, bool motion = true);
// Call after creating child controls and before showing the native popup.
void animatePopover(HWND target);
} // namespace lite