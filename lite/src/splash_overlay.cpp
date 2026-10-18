#include "dpi.h"
#include "splash_overlay.h"
#include "skin.h"
#include "window_capture.h"
#include <algorithm>
#include <array>
#include <random>
#include <stdexcept>
namespace lite {
struct SplashOverlay::Impl {
  HWND owner, window = nullptr, priorFocus;
  std::string label, pose;
  std::function<void()> swap;
  ULONGLONG started = GetTickCount64();
  bool swapped = false;
  bool holding = false;
  ULONGLONG resumed = 0;
  double resumeFrom = 0;
  double elapsed() const {
    auto now = GetTickCount64();
    auto time = double(now - started);
    return resumed ? resumeFrom + double(now - resumed) : holding ? std::min(time, 1225.) : time;
  }
  void release() {
    if (!holding)
      return;
    resumeFrom = elapsed();
    resumed = GetTickCount64();
    holding = false;
  }
  struct Bubble {
    double left, size, delay, duration;
  };
  std::array<Bubble, 18> bubbles;
  Impl(HWND parent, std::string text, std::function<void()> action, bool hold)
      : owner(parent), priorFocus(GetFocus()), label(std::move(text)), swap(std::move(action)),
        holding(hold) {
    std::mt19937 random(static_cast<unsigned>(started));
    std::uniform_real_distribution<double> unit(0, 1);
    for (auto &b : bubbles)
      b = {unit(random), 12 + 52 * unit(random), 350 * unit(random), 900 + 700 * unit(random)};
    const char *poses[] = {"idle", "smile", "thinking", "shy", "tongue", "handsup"};
    pose = poses[random() % 6];
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TangOSLiteSplash";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dpi::registerClass(&wc);
    RECT bounds{};
    dpi::clientRect(owner, &bounds);
    window = dpi::createWindow(0, wc.lpszClassName, L"Switching application",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, bounds.right,
                               bounds.bottom, owner, nullptr, wc.hInstance, this);
    if (!window)
      throw std::runtime_error("Cannot create app-switch overlay");
    dpi::setWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
    SetFocus(window);
    SetTimer(window, 1, 33, nullptr);
  }
  ~Impl() {
    if (window)
      DestroyWindow(window);
  }
  void advance() {
    auto frame = splashFrame(elapsed());
    if (frame.swap && !swapped) {
      swapped = true;
      swap();
    }
    if (frame.finished) {
      KillTimer(window, 1);
      ShowWindow(window, SW_HIDE);
      if (IsWindow(priorFocus) && IsWindowVisible(priorFocus))
        SetFocus(priorFocus);
      else if (IsWindow(owner))
        SetFocus(owner);
      return;
    }
    RECT bounds{};
    dpi::clientRect(owner, &bounds);
    dpi::setWindowPos(window, HWND_TOP, 0, 0, bounds.right, bounds.bottom, SWP_NOACTIVATE);
    auto shape = dpi::roundRegion(0, 0, bounds.right + 1, bounds.bottom + 1, 32, 32);
    if (!SetWindowRgn(window, shape, FALSE))
      DeleteObject(shape);
    InvalidateRect(window, nullptr, FALSE);
  }
  void paint(HDC dc) {
    RECT r{};
    dpi::clientRect(window, &r);
    int w = r.right, h = r.bottom;
    if (w <= 0 || h <= 0)
      return;
    auto base = dpi::compatibleDC(dc), layer = dpi::compatibleDC(dc);
    auto bitmap = dpi::compatibleBitmap(dc, w, h), foreground = dpi::compatibleBitmap(dc, w, h);
    auto oldBase = SelectObject(base, bitmap), oldLayer = SelectObject(layer, foreground);
    renderWindowTree(owner, base, window);
    skin::background(layer, w, h);
    auto time = elapsed();
    auto frame = splashFrame(time);
    for (const auto &b : bubbles) {
      auto progress = (time - b.delay) / b.duration;
      auto bubble = splashBubbleFrame(progress);
      if (!bubble.visible)
        continue;
      int size = int(b.size), x = int(b.left * w),
          y = int(h + 70 - b.size - bubble.rise * 1.12 * h);
      skin::splashBubble(layer, x, y, size, bubble.opacity);
    }
    auto center = dpi::compatibleDC(dc);
    auto centerBitmap = dpi::compatibleBitmap(dc, w, h);
    auto oldCenter = SelectObject(center, centerBitmap);
    BitBlt(center, 0, 0, w, h, layer, 0, 0, SRCCOPY);
    int mascotHeight = int(188 * frame.scale), mascotWidth = (mascotHeight * 620 + 336) / 673;
    int font = int(78 * frame.scale), gap = int(30 * frame.scale);
    int wordWidth = skin::textWidth(layer, L"tangOS", font, 800);
    int labelWidth = skin::textWidth(layer, wide(label), int(22 * frame.scale), 600);
    int textWidth = std::max(wordWidth, labelWidth), total = textWidth + gap + mascotWidth;
    int left = (w - total) / 2, top = (h - mascotHeight) / 2 + int(frame.translateY);
    int wordLeft = left + (textWidth - wordWidth) / 2;
    int tangWidth = skin::textWidth(layer, L"tang", font, 800);
    skin::label(center, L"tang", wordLeft, top + 30, textWidth, 95, font, true, false, false,
                RGB(13, 58, 92), false, 800);
    skin::label(center, L"OS", wordLeft + tangWidth, top + 30, textWidth - tangWidth, 95, font,
                true, false, false, RGB(0, 153, 224), false, 800);
    skin::label(center, wide(label), left + (textWidth - labelWidth) / 2, top + 125, textWidth, 40,
                int(22 * frame.scale), false, false, false, RGB(13, 58, 92), false, 600);
    skin::mascot(center, left + textWidth + gap, top, mascotWidth, pose);
    BLENDFUNCTION centerBlend{AC_SRC_OVER, 0,
                              BYTE(std::clamp(frame.centerOpacity * 255., 0., 255.)), 0};
    AlphaBlend(layer, 0, 0, w, h, center, 0, 0, w, h, centerBlend);
    SelectObject(center, oldCenter);
    DeleteObject(centerBitmap);
    DeleteDC(center);
    // No sleeps or nested message loop; the underlying view swaps at 450 ms.
    BLENDFUNCTION blend{AC_SRC_OVER, 0, BYTE(std::clamp(frame.opacity * 255., 0., 255.)), 0};
    AlphaBlend(base, 0, 0, w, h, layer, 0, 0, w, h, blend);
    BitBlt(dc, 0, 0, w, h, base, 0, 0, SRCCOPY);
    SelectObject(base, oldBase);
    SelectObject(layer, oldLayer);
    DeleteObject(bitmap);
    DeleteObject(foreground);
    DeleteDC(base);
    DeleteDC(layer);
  }
  static LRESULT CALLBACK proc(HWND h, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Impl *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<Impl *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
      self->window = h;
      SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self)
      switch (message) {
      case WM_TIMER:
        self->advance();
        return 0;
      case WM_ERASEBKGND:
        return 1;
      case WM_PAINT: {
        PAINTSTRUCT ps;
        auto dc = dpi::beginPaint(h, &ps);
        self->paint(dc);
        EndPaint(h, &ps);
        return 0;
      }
      case WM_PRINTCLIENT:
        self->paint(reinterpret_cast<HDC>(w));
        return 0;
      case WM_KEYDOWN:
      case WM_LBUTTONDOWN:
      case WM_MOUSEWHEEL:
        return 0;
      case WM_NCDESTROY:
        KillTimer(h, 1);
        self->window = nullptr;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
      }
    return DefWindowProcW(h, message, w, l);
  }
};
SplashOverlay::SplashOverlay(HWND parent, std::string label, std::function<void()> swap, bool hold)
    : impl(std::make_unique<Impl>(parent, std::move(label), std::move(swap), hold)) {}
SplashOverlay::~SplashOverlay() = default;
bool SplashOverlay::open() const { return impl->window && IsWindowVisible(impl->window); }
void SplashOverlay::release() { impl->release(); }
} // namespace lite
