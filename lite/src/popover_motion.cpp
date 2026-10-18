#include "popover_motion.h"
#include "dpi.h"
#include "skin.h"
#include "window_capture.h"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace lite {
namespace {
double easing(double x, double a, double b, double c, double d) {
  x = std::clamp(x, 0., 1.);
  if (x == 0 || x == 1)
    return x;
  auto f = [](double t, double p, double q) {
    return 3 * (1 - t) * (1 - t) * t * p + 3 * (1 - t) * t * t * q + t * t * t;
  };
  double lo = 0, hi = 1;
  for (int i = 0; i < 40; ++i) {
    auto t = (lo + hi) / 2;
    if (f(t, a, c) < x)
      lo = t;
    else
      hi = t;
  }
  return f((lo + hi) / 2, b, d);
}
void roundPopover(HWND window) {
  RECT bounds{};
  if (!GetWindowRect(window, &bounds))
    return;
  auto diameter = dpi::pixels(36, dpi::forWindow(window));
  auto region = CreateRoundRectRgn(0, 0, bounds.right - bounds.left + 1,
                                   bounds.bottom - bounds.top + 1, diameter, diameter);
  if (region && !SetWindowRgn(window, region, TRUE))
    DeleteObject(region);
}
constexpr UINT_PTR subclassId = 0x5447504f;
struct Motion {
  HWND target = nullptr, proxy = nullptr;
  HDC snapshot = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous = nullptr;
  int width = 0, height = 0;
  UINT dpi = 96;
  RECT bounds{};
  ULONGLONG started = GetTickCount64();
  enum Phase { opening, idle, closing } phase = opening;
  PopoverFrame from{1, 1, 0, false}, shown{.8, 0, -10, false};
  ~Motion() {
    if (proxy) {
      SetWindowLongPtrW(proxy, GWLP_USERDATA, 0);
      KillTimer(proxy, 1);
      DestroyWindow(proxy);
    }
    release();
  }
  void release() {
    if (snapshot) {
      SelectObject(snapshot, previous);
      DeleteObject(bitmap);
      DeleteDC(snapshot);
    }
    snapshot = nullptr;
    bitmap = nullptr;
    previous = nullptr;
  }
  void capture() {
    release();
    GetWindowRect(target, &bounds);
    width = bounds.right - bounds.left;
    height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
      throw std::runtime_error("Invalid popover bounds");
    auto screen = GetDC(target);
    snapshot = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *pixels = nullptr;
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    ReleaseDC(target, screen);
    if (!snapshot || !bitmap || !pixels)
      throw std::runtime_error("Cannot capture native popover");
    previous = SelectObject(snapshot, bitmap);
    auto brush = CreateSolidBrush(skin::field());
    RECT r{0, 0, width, height};
    FillRect(snapshot, &r, brush);
    DeleteObject(brush);
    dpi::configureDC(snapshot, dpi);
    renderWindowTree(target, snapshot);
    SetMapMode(snapshot, MM_TEXT);
    SetViewportOrgEx(snapshot, 0, 0, nullptr);
    GdiFlush();
  }
  void makeProxy();
  void beginClose() {
    if (phase == closing)
      return;
    if (phase == opening)
      shown = popoverFrame(double(GetTickCount64() - started), false);
    else
      shown = {1, 1, 0, false};
    from = shown;
    phase = closing;
    started = GetTickCount64();
    capture();
    SetLayeredWindowAttributes(target, 0, 0, LWA_ALPHA);
    if (!proxy)
      makeProxy();
    SetTimer(proxy, 1, 10, nullptr);
    tick();
  }
  void tick() {
    if (!target || !IsWindow(target))
      return;
    auto frame = popoverFrame(double(GetTickCount64() - started), phase == closing);
    shown = frame;
    if (phase == closing) {
      double p = (1 - frame.scale) / .2;
      shown.scale = from.scale + (.8 - from.scale) * p;
      shown.translateY = from.translateY + (-10 - from.translateY) * p;
      shown.opacity = from.opacity * frame.opacity;
    }
    if (frame.finished) {
      if (phase == closing) {
        auto window = target;
        RemoveWindowSubclass(window, subclass, subclassId);
        RemovePropW(window, L"TangOSPopoverMotion");
        target = nullptr;
        delete this;
        DestroyWindow(window);
        return;
      }
      phase = idle;
      SetLayeredWindowAttributes(target, 0, 255, LWA_ALPHA);
      auto window = proxy;
      proxy = nullptr;
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      KillTimer(window, 1);
      DestroyWindow(window);
      release();
      return;
    }
    int w = std::max(1, int(std::lround(width * shown.scale)));
    int h = std::max(1, int(std::lround(height * shown.scale)));
    auto screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *raw = nullptr;
    auto image = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
    if (!dc || !image || !raw) {
      if (image)
        DeleteObject(image);
      if (dc)
        DeleteDC(dc);
      ReleaseDC(nullptr, screen);
      throw std::runtime_error("Cannot compose popover frame");
    }
    auto old = SelectObject(dc, image);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc, 0, 0, w, h, snapshot, 0, 0, width, height, SRCCOPY);
    GdiFlush();
    auto bytes = static_cast<unsigned char *>(raw);
    float radius = float(18 * dpi / 96. * shown.scale);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        float qx = std::abs(x + .5f - w / 2.f) - (w / 2.f - radius);
        float qy = std::abs(y + .5f - h / 2.f) - (h / 2.f - radius);
        float distance = std::hypot(std::max(0.f, qx), std::max(0.f, qy)) +
                         std::min(std::max(qx, qy), 0.f) - radius;
        float a = std::clamp(.5f - distance, 0.f, 1.f);
        auto p = bytes + (size_t(y) * w + x) * 4;
        for (int c = 0; c < 3; ++c)
          p[c] = BYTE(std::lround(p[c] * a));
        p[3] = BYTE(std::lround(255 * a));
      }
    POINT location{bounds.right - w,
                   bounds.top + int(std::lround(shown.translateY * shown.scale * dpi / 96.))};
    POINT origin{};
    SIZE size{w, h};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, BYTE(std::lround(std::clamp(shown.opacity, 0., 1.) * 255)),
                        AC_SRC_ALPHA};
    UpdateLayeredWindow(proxy, screen, &location, &size, dc, &origin, 0, &blend, ULW_ALPHA);
    SelectObject(dc, old);
    DeleteObject(image);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
  }
  static LRESULT CALLBACK subclass(HWND window, UINT msg, WPARAM w, LPARAM l, UINT_PTR,
                                   DWORD_PTR context) {
    auto self = reinterpret_cast<Motion *>(context);
    if (msg == WM_CLOSE && skin::animationEnabled()) {
      try {
        self->beginClose();
        return 0;
      } catch (...) {
        SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
      }
    }
    if ((msg == WM_DPICHANGED || msg == WM_SIZE) && self->proxy) {
      SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
      auto proxy = self->proxy;
      self->proxy = nullptr;
      SetWindowLongPtrW(proxy, GWLP_USERDATA, 0);
      KillTimer(proxy, 1);
      DestroyWindow(proxy);
      self->phase = idle;
      self->release();
    }
    if (msg == WM_SIZE)
      roundPopover(window);
    if (msg == WM_NCDESTROY) {
      RemoveWindowSubclass(window, subclass, subclassId);
      RemovePropW(window, L"TangOSPopoverMotion");
      self->target = nullptr;
      delete self;
    }
    return DefSubclassProc(window, msg, w, l);
  }
  static LRESULT CALLBACK proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Motion *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      self = static_cast<Motion *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, LONG_PTR(self));
    }
    if (!self)
      return DefWindowProcW(window, msg, w, l);
    if (msg == WM_TIMER) {
      try {
        self->tick();
      } catch (...) {
        auto target = self->target;
        if (target) {
          RemoveWindowSubclass(target, subclass, subclassId);
          RemovePropW(target, L"TangOSPopoverMotion");
          SetLayeredWindowAttributes(target, 0, 255, LWA_ALPHA);
        }
        delete self;
      }
      return 0;
    }
    if (msg == WM_MOUSEACTIVATE)
      return MA_NOACTIVATE;
    if (msg == WM_ERASEBKGND)
      return 1;
    if ((msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) && self->target) {
      SendMessageW(self->target, msg, w, l);
      return 0;
    }
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && self->target) {
      POINT p{int(short(LOWORD(l)) / self->shown.scale), int(short(HIWORD(l)) / self->shown.scale)};
      HWND child = self->target;
      for (;;) {
        auto next = ChildWindowFromPointEx(child, p, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
        if (!next || next == child)
          break;
        MapWindowPoints(child, next, &p, 1);
        child = next;
      }
      SendMessageW(child, msg, w, MAKELPARAM(p.x, p.y));
      return 0;
    }
    return DefWindowProcW(window, msg, w, l);
  }
};
void Motion::makeProxy() {
  WNDCLASSW cls{};
  cls.lpfnWndProc = proc;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"TangOSPopoverAnimation";
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassW(&cls);
  proxy = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls.lpszClassName,
                          L"", WS_POPUP, bounds.left, bounds.top, width, height,
                          GetWindow(target, GW_OWNER), nullptr, cls.hInstance, this);
  if (!proxy)
    throw std::runtime_error("Cannot create popover compositor");
  ShowWindow(proxy, SW_SHOWNOACTIVATE);
}
} // namespace
PopoverFrame popoverFrame(double milliseconds, bool closing, bool motion) {
  if (!std::isfinite(milliseconds))
    milliseconds = 240;
  if (!motion)
    return {closing ? .8 : 1, closing ? 0. : 1., closing ? -10. : 0., true};
  auto t = std::max(0., milliseconds);
  double p = closing ? easing(t / 190, .2, 1.1, .4, 1) : easing(t / 240, .2, 1.5, .35, 1);
  double alpha = easing(t / (closing ? 150 : 140), .25, .1, .25, 1);
  return {closing ? 1 - .2 * p : .8 + .2 * p, closing ? 1 - alpha : alpha,
          closing ? -10 * p : -10 * (1 - p), t >= (closing ? 190 : 240)};
}
void animatePopover(HWND target) {
  if (IsWindow(target))
    roundPopover(target);
  if (!skin::animationEnabled() || !IsWindow(target) || GetPropW(target, L"TangOSPopoverMotion"))
    return;
  auto state = new Motion;
  state->target = target;
  state->dpi = dpi::forWindow(target);
  try {
    state->capture();
    SetWindowLongPtrW(target, GWL_EXSTYLE, GetWindowLongPtrW(target, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(target, 0, 0, LWA_ALPHA);
    if (!SetWindowSubclass(target, Motion::subclass, subclassId,
                           reinterpret_cast<DWORD_PTR>(state)))
      throw std::runtime_error("Cannot attach popover motion");
    SetPropW(target, L"TangOSPopoverMotion", state);
    state->makeProxy();
    state->started = GetTickCount64();
    state->tick();
    SetTimer(state->proxy, 1, 10, nullptr);
  } catch (...) {
    RemoveWindowSubclass(target, Motion::subclass, subclassId);
    RemovePropW(target, L"TangOSPopoverMotion");
    SetLayeredWindowAttributes(target, 0, 255, LWA_ALPHA);
    delete state;
    return;
  }
}
} // namespace lite