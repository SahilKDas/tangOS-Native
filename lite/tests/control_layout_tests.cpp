#include "skin.h"
#include "project_popup.h"
#include "dpi.h"
#include <thread>
#include <fstream>
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  std::string capturePath = argc == 3 && std::string(argv[1]) == "--capture" ? argv[2] : "";
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
    auto frame = lite::projectPopupFrame(100, false);
    expect(frame.scale > 1 && frame.opacity > .9, "popup reference overshoot/ease missing");
    expect(lite::projectPopupFrame(200, false).scale == 1, "popup opening endpoint wrong");
    expect(lite::projectPopupFrame(160, true).opacity == 0, "popup closing endpoint wrong");
    expect(lite::projectPopupFrame(0, false, false).finished, "reduced motion not instant");
    skin::animate(false);
    ShowWindow(parent, SW_SHOW);
    std::vector<lite::ProjectPopupRow> projects;
    for (unsigned i = 1; i <= 64; ++i)
      projects.push_back({i, L"Project " + std::to_wstring(i), L"DX", i % 2 == 0, i == 1});
    auto popupTest = [&](unsigned key, unsigned expected) {
      auto uiThread = GetCurrentThreadId();
      auto clickDpi = dpi::forWindow(parent);
      std::thread driver([key, uiThread, clickDpi, &capturePath, &projects] {
        HWND popup = nullptr;
        auto start = GetTickCount64();
        while (!popup && GetTickCount64() - start < 5000) {
          EnumThreadWindows(
              uiThread,
              [](HWND window, LPARAM context) -> BOOL {
                wchar_t name[128]{};
                GetClassNameW(window, name, 128);
                if (std::wstring(name) == L"TangOSProjectPopup") {
                  *reinterpret_cast<HWND *>(context) = window;
                  return FALSE;
                }
                return TRUE;
              },
              reinterpret_cast<LPARAM>(&popup));
          Sleep(10);
        }
        if (popup && projects.size() == 3 && !capturePath.empty()) {
          Sleep(230);
          RECT r{};
          GetClientRect(popup, &r);
          auto screen = GetDC(popup), dc = CreateCompatibleDC(screen);
          BITMAPINFO info{};
          info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
          info.bmiHeader.biWidth = r.right;
          info.bmiHeader.biHeight = -r.bottom;
          info.bmiHeader.biPlanes = 1;
          info.bmiHeader.biBitCount = 32;
          void *pixels = nullptr;
          auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
          auto previous = SelectObject(dc, bitmap);
          SendMessageW(popup, WM_PRINTCLIENT, WPARAM(dc), PRF_CLIENT);
          GdiFlush();
          BITMAPFILEHEADER file{};
          file.bfType = 0x4d42;
          file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader);
          file.bfSize = file.bfOffBits + r.right * r.bottom * 4;
          std::ofstream output(capturePath, std::ios::binary);
          output.write(reinterpret_cast<char *>(&file), sizeof(file));
          output.write(reinterpret_cast<char *>(&info.bmiHeader), sizeof(info.bmiHeader));
          output.write(static_cast<char *>(pixels), r.right * r.bottom * 4);
          SelectObject(dc, previous);
          DeleteObject(bitmap);
          DeleteDC(dc);
          ReleaseDC(popup, screen);
        }
        if (popup) {
          if (key == VK_LBUTTON)
            PostMessageW(popup, WM_LBUTTONUP, 0,
                         MAKELPARAM(dpi::pixels(30, clickDpi), dpi::pixels(24, clickDpi)));
          else if (key == VK_RBUTTON)
            PostMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(-10, -10));
          else {
            PostMessageW(popup, WM_KEYDOWN, key, 0);
            if (key != VK_ESCAPE)
              PostMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
          }
        }
      });
      auto result = lite::projectPopup(parent, parent, projects);
      driver.join();
      expect(result == expected, "native popup keyboard/scroll selection failed");
      expect(FindWindowW(L"TangOSProjectPopup", nullptr) == nullptr, "popup leaked its window");
      expect(GetCapture() == nullptr, "popup leaked input capture");
    };
    popupTest(VK_END, 64);
    popupTest(VK_DOWN, 1);
    popupTest(VK_ESCAPE, 0);
    for (UINT scale : {96, 106, 120, 132, 192, 216, 384}) {
      dpi::testing::simulateWindowDpi(parent, scale);
      popupTest(VK_LBUTTON, 1);
      popupTest(VK_END, 64);
      popupTest(VK_RBUTTON, 0);
    }
    dpi::testing::simulateWindowDpi(parent, 96);
    skin::animate(true);
    projects.resize(3);
    popupTest(VK_ESCAPE, 0);
    DestroyWindow(parent);
    skin::shutdown();
    std::cout << "Native control grow/shrink and font reflow passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
