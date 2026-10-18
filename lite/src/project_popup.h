#pragma once
#include <windows.h>
#include <string>
#include <vector>
namespace lite {
struct ProjectPopupRow {
  unsigned id;
  std::wstring title, glyph;
  bool viewerOnly = false, active = false;
};
struct PopupFrame {
  double scale, opacity;
  bool finished;
};
PopupFrame projectPopupFrame(double milliseconds, bool closing, bool motion = true);
unsigned projectPopup(HWND owner, HWND anchor, const std::vector<ProjectPopupRow> &rows);
} // namespace lite