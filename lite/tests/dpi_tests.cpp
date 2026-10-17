#include "dpi.h"
#include "skin.h"
#include <iostream>
#include <stdexcept>
#include <vector>

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
POINT lastSize{}, lastClick{};
LRESULT CALLBACK fixture(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_PRINTCLIENT) {
    RECT bounds{};
    dpi::clientRect(window, &bounds);
    skin::background(reinterpret_cast<HDC>(w), bounds.right, bounds.bottom);
    return 0;
  }
  if (message == WM_SIZE)
    lastSize = {LOWORD(l), HIWORD(l)};
  if (message == WM_LBUTTONUP)
    lastClick = {short(LOWORD(l)), short(HIWORD(l))};
  return DefWindowProcW(window, message, w, l);
}
int main() {
  try {
    dpi::enable();
    skin::initialize();
    // Deterministic renderer matrix; these are synthetic DC scales, not changes
    // to the user's display configuration.
    for (UINT value : {96u, 106u, 120u, 132u, 144u, 168u, 192u, 216u, 240u, 288u, 384u}) {
      dpi::Scope scope(value);
      for (int coordinate : {-2000, -33, -1, 0, 1, 33, 2000})
        expect(std::abs(dpi::logical(dpi::pixels(coordinate, value), value) - coordinate) <= 1,
               "coordinate round trip drifted");
      auto screen = ::GetDC(nullptr), dc = dpi::compatibleDC(nullptr);
      auto bitmap = dpi::compatibleBitmap(dc, 320, 200);
      auto previous = SelectObject(dc, bitmap);
      BITMAP dimensions{};
      GetObjectW(bitmap, sizeof(dimensions), &dimensions);
      expect(dimensions.bmWidth == dpi::pixels(320, value) &&
                 dimensions.bmHeight == dpi::pixels(200, value),
             "bitmap remained at 96 DPI");
      RECT background{0, 0, 320, 200};
      FillRect(dc, &background, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
      skin::panel(dc, 20, 20, 280, 160, true);
      skin::label(dc, L"Nunito at physical resolution", 40, 60, 240, 40, 16, true);
      expect(GetPixel(dc, 160, 120) != RGB(255, 255, 255), "scaled raster was not rendered");
      // Same logical dimensions at another DPI must invalidate the background cache.
      skin::background(dc, 320, 200);
      expect(GetPixel(dc, 160, 100) != CLR_INVALID, "scaled background cache lost its surface");
      SelectObject(dc, previous);
      DeleteObject(bitmap);
      DeleteDC(dc);
      ::ReleaseDC(nullptr, screen);
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = fixture;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TangOSDpiFixture";
    dpi::registerClass(&wc);
    auto owner = dpi::createWindow(0, wc.lpszClassName, L"DPI fixture", WS_POPUP, 0, 0, 320, 200,
                                   nullptr, nullptr, wc.hInstance, nullptr);
    expect(owner != nullptr, "native fixture could not be created");
    expect(AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(owner),
                                       DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
           "native window did not enter per-monitor v2 awareness");
    auto field = dpi::createWindow(0, L"EDIT", L"Scaled Nunito", WS_CHILD, 12, 15, 100, 32, owner,
                                   nullptr, wc.hInstance, nullptr);
    skin::controlFont(field, 16);
    skin::compactEdit(field);
    for (UINT value : {96u, 168u, 240u, 120u, 288u, 96u}) {
      dpi::testing::simulateWindowDpi(owner, value);
      RECT suggestion{20, 20, 20 + dpi::pixels(320, value), 20 + dpi::pixels(200, value)};
      ::SendMessageW(owner, WM_DPICHANGED, MAKELONG(value, value), LPARAM(&suggestion));
      dpi::Scope scope(value);
      expect(lastSize.x == 320 && lastSize.y == 200, "DPI change moved logical layout dimensions");
      RECT bounds{};
      dpi::relativeRect(field, owner, &bounds);
      expect(bounds.left == 12 && bounds.top == 15 && bounds.right - bounds.left == 100,
             "DPI change moved the native field away from logical geometry");
      auto font = reinterpret_cast<HFONT>(::SendMessageW(field, WM_GETFONT, 0, 0));
      LOGFONTW definition{};
      GetObjectW(font, sizeof(definition), &definition);
      expect(definition.lfHeight == -dpi::pixels(16, value),
             "native field font did not follow DPI");
      dpi::sendMessage(owner, WM_LBUTTONUP, 0, MAKELPARAM(123, 87));
      expect(lastClick.x == 123 && lastClick.y == 87, "DPI change desynchronized mouse targets");
    }
    dpi::testing::simulateWindowDpi(owner, 0);
    DestroyWindow(field);
    {
      dpi::Scope scope(dpi::forWindow(owner));
      dpi::moveWindow(owner, 0, 0, 320, 200, FALSE);
      RECT physical{}, layout{};
      ::GetClientRect(owner, &physical);
      dpi::clientRect(owner, &layout);
      expect(layout.right == 320 && layout.bottom == 200, "native layout changed logical size");
      expect(physical.right == dpi::pixels(320, dpi::forWindow(owner)), "native size is unscaled");
      expect(lastSize.x == 320 && lastSize.y == 200,
             "WM_SIZE passed physical coordinates to layout");
      dpi::sendMessage(owner, WM_LBUTTONUP, 0, MAKELPARAM(123, 87));
      expect(lastClick.x == 123 && lastClick.y == 87, "mouse target diverged from layout");
      auto label =
          dpi::createWindow(0, L"STATIC", L"Long previous status that must disappear", WS_CHILD, 10,
                            10, 280, 30, owner, nullptr, wc.hInstance, nullptr);
      skin::controlFont(label, 16);
      skin::backgroundLabel(label);
      auto dc = dpi::compatibleDC(nullptr);
      auto bitmap = dpi::compatibleBitmap(dc, 280, 30);
      auto previous = SelectObject(dc, bitmap);
      dpi::sendMessage(label, WM_PRINT, WPARAM(dc), PRF_CLIENT);
      dpi::sendMessage(label, WM_SETTEXT, 0, LPARAM(L"Ready"));
      dpi::sendMessage(label, WM_PRINT, WPARAM(dc), PRF_CLIENT);
      auto readPixels = [&](HBITMAP image) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = dpi::pixels(280, dpi::current());
        info.bmiHeader.biHeight = dpi::pixels(30, dpi::current());
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        std::vector<unsigned char> pixels(size_t(info.bmiHeader.biWidth) * info.bmiHeader.biHeight *
                                          4);
        expect(GetDIBits(dc, image, 0, info.bmiHeader.biHeight, pixels.data(), &info,
                         DIB_RGB_COLORS) != 0,
               "status capture failed");
        return pixels;
      };
      SelectObject(dc, previous);
      auto changed = readPixels(bitmap);
      auto clean = dpi::compatibleBitmap(dc, 280, 30);
      SelectObject(dc, clean);
      dpi::sendMessage(label, WM_PRINT, WPARAM(dc), PRF_CLIENT);
      SelectObject(dc, previous);
      expect(changed == readPixels(clean), "short status left stale text over the backdrop");
      DeleteObject(bitmap);
      DeleteObject(clean);
      DeleteDC(dc);
      DestroyWindow(label);
    }
    std::vector<RECT> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT rect, LPARAM data) -> BOOL {
          reinterpret_cast<std::vector<RECT> *>(data)->push_back(*rect);
          return TRUE;
        },
        LPARAM(&monitors));
    for (auto monitor : monitors) {
      ::SetWindowPos(owner, nullptr, monitor.left + 20, monitor.top + 20, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      dpi::Scope scope(dpi::forWindow(owner));
      dpi::moveWindow(owner, dpi::logical(monitor.left + 20, dpi::current()),
                      dpi::logical(monitor.top + 20, dpi::current()), 320, 200, FALSE);
      RECT layout{};
      dpi::clientRect(owner, &layout);
      expect(layout.right == 320 && layout.bottom == 200,
             "monitor transition changed logical size");
      std::cout << "Actual attached monitor: window DPI " << dpi::forWindow(owner) << '\n';
    }
    DestroyWindow(owner);
    skin::shutdown();
    dpi::shutdown();
    std::cout << "PASS DPI: 96/106/120/132/144/168/192/216/240/288/384 DPI raster matrix, native size/input, "
                 "attached monitors\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
