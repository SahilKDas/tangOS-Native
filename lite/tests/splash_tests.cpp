#include "splash_overlay.h"
#include "descriptor.h"
#include "skin.h"
#include "window_capture.h"
#include <iostream>
#include <stdexcept>
using namespace lite;
namespace {
void expect(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
LRESULT CALLBACK fixture(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_PRINTCLIENT) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    skin::background(reinterpret_cast<HDC>(w), bounds.right, bounds.bottom);
    return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
void pumpUntil(ULONGLONG deadline) {
  while (GetTickCount64() < deadline) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(5);
  }
}
void capture(HWND window, const fs::path &path) {
  RECT bounds{};
  GetClientRect(window, &bounds);
  int width = bounds.right, height = bounds.bottom;
  auto screen = GetDC(window), dc = CreateCompatibleDC(screen);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void *pixels = nullptr;
  auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (!bitmap)
    throw std::runtime_error("Cannot capture native splash");
  auto previous = SelectObject(dc, bitmap);
  renderWindowTree(window, dc);
  BITMAPFILEHEADER header{};
  header.bfType = 0x4d42;
  header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
  header.bfSize = header.bfOffBits + width * height * 4;
  std::string bytes(reinterpret_cast<const char *>(&header), sizeof(header));
  bytes.append(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(info.bmiHeader));
  bytes.append(static_cast<const char *>(pixels), size_t(width) * height * 4);
  write(path, bytes);
  SelectObject(dc, previous);
  DeleteObject(bitmap);
  DeleteDC(dc);
  ReleaseDC(window, screen);
}
} // namespace
int main(int argc, char **argv) {
  HWND owner = nullptr;
  try {
    expect(argc == 2, "Supply an output directory");
    auto output = fs::u8path(argv[1]);
    fs::create_directories(output);
    skin::initialize();
    WNDCLASSW wc{};
    wc.lpfnWndProc = fixture;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TangOSLiteSplashFixture";
    RegisterClassW(&wc);
    owner = CreateWindowW(wc.lpszClassName, L"Disposable splash fixture", WS_POPUP, 0, 0, 1200, 800,
                          nullptr, nullptr, wc.hInstance, nullptr);
    expect(owner != nullptr, "Cannot create hidden GUI fixture");
    int swaps = 0;
    ULONGLONG elapsed = 0;
    auto start = GetTickCount64();
    {
      SplashOverlay splash(owner, "Chaos Viewer", [&] {
        ++swaps;
        elapsed = GetTickCount64() - start;
      });
      pumpUntil(start + 250);
      expect(swaps == 0, "View changed before opaque splash");
      pumpUntil(start + 700);
      expect(swaps == 1 && elapsed >= 450, "View did not swap exactly once after 450 ms");
      capture(owner, output / "splash-opaque.bmp");
      pumpUntil(start + 2000);
      auto child = FindWindowExW(owner, nullptr, L"TangOSLiteSplash", nullptr);
      expect(child && !(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE),
             "Expired splash still intercepts input");
      expect(swaps == 1, "View swap callback repeated");
    }
    int cancelled = 0, replayed = 0;
    {
      auto splash =
          std::make_unique<SplashOverlay>(owner, "Cancelled switch", [&] { ++cancelled; });
      pumpUntil(GetTickCount64() + 100);
      splash.reset();
      splash = std::make_unique<SplashOverlay>(owner, "Chaos Controller", [&] { ++replayed; });
      pumpUntil(GetTickCount64() + 2100);
      expect(cancelled == 0 && replayed == 1, "Repeated switch retained a stale timer or callback");
    }
    expect(!FindWindowExW(owner, nullptr, L"TangOSLiteSplash", nullptr),
           "Splash native window leaked");
    DestroyWindow(owner);
    owner = nullptr;
    skin::shutdown();
    write(output / "report.json", Json{{"state", "passed"},
                                       {"delayedSwap", true},
                                       {"expires", true},
                                       {"replayCancelsOldCallback", true},
                                       {"nativeWindowReleased", true}}
                                      .dump(2));
    std::cout << "PASS native splash: delayed swap, expiry, replay, window cleanup\n";
    return 0;
  } catch (const std::exception &error) {
    if (owner)
      DestroyWindow(owner);
    skin::shutdown();
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  }
}
