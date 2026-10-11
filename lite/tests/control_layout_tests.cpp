#include "skin.h"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main() {
  try {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    skin::initialize();
    auto parent = CreateWindowExW(0, L"STATIC", L"Control layout fixture", WS_OVERLAPPED, 0, 0,
                                  1000, 800, nullptr, nullptr, nullptr, nullptr);
    expect(parent != nullptr, "fixture creation failed");
    for (bool edit : {false, true}) {
      auto child = CreateWindowExW(0, edit ? L"EDIT" : L"COMBOBOX", L"Nunito field",
                                   WS_CHILD | (edit ? ES_AUTOHSCROLL : CBS_DROPDOWNLIST), 0, 0, 100,
                                   32, parent, nullptr, nullptr, nullptr);
      expect(child != nullptr, "control creation failed");
      if (edit)
        skin::compactEdit(child);
      else
        skin::compactCombo(child);
      // Exercise grow, shrink and fractional-scale-equivalent dimensions.
      // These are layout stress cases, not physical monitor DPI validation.
      for (int width : {100, 350, 175, 625, 125}) {
        SetWindowPos(child, nullptr, 0, 0, width, 56, SWP_NOZORDER);
        RECT outer{};
        GetWindowRect(child, &outer);
        auto region = CreateRectRgn(0, 0, 0, 0);
        expect(GetWindowRgn(child, region) != ERROR, "missing rounded region");
        expect(PtInRegion(region, width - 3, (outer.bottom - outer.top) / 2),
               "resizing left the field clipped at its original width");
        expect(!PtInRegion(region, width + 2, (outer.bottom - outer.top) / 2),
               "shrinking left an oversized clipping region");
        DeleteObject(region);
      }
      if (edit) {
        skin::controlFont(child, 13);
        RECT small{};
        GetClientRect(child, &small);
        skin::controlFont(child, 24);
        RECT large{};
        GetClientRect(child, &large);
        expect(large.bottom > small.bottom, "larger font did not refresh edit centering");
      }
      DestroyWindow(child);
    }
    auto screen = GetDC(parent), dc = CreateCompatibleDC(screen);
    auto bitmap = CreateCompatibleBitmap(screen, 400, 60);
    auto previous = SelectObject(dc, bitmap);
    RECT row{0, 0, 360, 36};
    for (int theme = 0; theme < 5; ++theme) {
      skin::theme(theme);
      skin::projectMenuRow(dc, row, L"Long project title that must fit beside status", L"DX", true,
                           false, false);
      auto idle = GetPixel(dc, 12, 18);
      skin::projectMenuRow(dc, row, L"Project", L"DX", true, false, true);
      auto hover = GetPixel(dc, 12, 18);
      skin::projectMenuRow(dc, row, L"Project", L"DX", true, true, false);
      auto active = GetPixel(dc, 12, 18);
      expect(idle != hover && hover != active, "menu hover/active states are indistinguishable");
    }
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(parent, screen);
    DestroyWindow(parent);
    skin::shutdown();
    std::cout << "Native control grow/shrink and font reflow passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
