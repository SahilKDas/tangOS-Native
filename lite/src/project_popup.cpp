#include "project_popup.h"
#include "dpi.h"
#include "skin.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace lite {
namespace {
double ease(double x, double a, double b, double c, double d) {
  x = std::clamp(x, 0., 1.);
  if (x == 0 || x == 1)
    return x;
  auto curve = [](double t, double p, double q) {
    return 3 * (1 - t) * (1 - t) * t * p + 3 * (1 - t) * t * t * q + t * t * t;
  };
  double low = 0, high = 1;
  for (int i = 0; i < 40; ++i) {
    double t = (low + high) / 2;
    if (curve(t, a, c) < x)
      low = t;
    else
      high = t;
  }
  return curve((low + high) / 2, b, d);
}
struct Popup {
  const std::vector<ProjectPopupRow> &rows;
  HWND window = nullptr, anchor = nullptr;
  UINT dpi = 96;
  int width = 190, height = 0, first = 0, visible = 0, hover = -1;
  int x = 0, y = 0;
  bool closing = false, done = false, motion;
  unsigned selection = 0;
  ULONGLONG started = GetTickCount64();
  double closingScale = 1, closingOpacity = 1, scale = .9;
  void close(unsigned id = 0) {
    if (closing)
      return;
    auto current = projectPopupFrame(double(GetTickCount64() - started), false, motion);
    closingScale = current.scale;
    closingOpacity = current.opacity;
    skin::projectPickerOpen(anchor, false);
    selection = id;
    closing = true;
    started = GetTickCount64();
    if (!motion)
      done = true;
  }
  void select(int index) {
    hover = std::clamp(index, 0, int(rows.size()) - 1);
    first = std::clamp(first, 0, std::max(0, int(rows.size()) - visible));
    if (hover < first)
      first = hover;
    if (hover >= first + visible)
      first = hover - visible + 1;
    InvalidateRect(window, nullptr, FALSE);
  }
  int hit(LPARAM point) {
    int px = short(LOWORD(point)), py = short(HIWORD(point));
    double ratio = dpi / 96. * scale;
    int lx = int(px / ratio), ly = int(py / ratio);
    if (lx < 6 || lx >= width - 6 || ly < 6 || ly >= height - 6)
      return -1;
    int row = (ly - 6) / 38;
    if ((ly - 6) % 38 >= 36 || row >= visible || first + row >= int(rows.size()))
      return -1;
    return first + row;
  }
  void animate() {
    auto frame = projectPopupFrame(double(GetTickCount64() - started), closing, motion);
    scale = closing ? closingScale * frame.scale : frame.scale;
    if (closing)
      scale = closingScale + (.9 - closingScale) * (1 - frame.scale) / .1;
    int w = std::max(1, int(std::lround(dpi::pixels(width, dpi) * scale)));
    int h = std::max(1, int(std::lround(dpi::pixels(height, dpi) * scale)));
    SetWindowPos(window, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
    int radius = std::max(1, int(std::lround(dpi::pixels(36, dpi) * scale)));
    SetWindowRgn(window, CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius), TRUE);
    auto alpha = std::clamp(frame.opacity * (closing ? closingOpacity : 1), 0., 1.);
    SetLayeredWindowAttributes(window, 0, BYTE(std::lround(alpha * 255)), LWA_ALPHA);
    InvalidateRect(window, nullptr, FALSE);
    if (closing && frame.finished)
      done = true;
  }
  void paint(HDC target) {
    dpi::Scope scope(dpi);
    int pw = dpi::pixels(width, dpi), ph = dpi::pixels(height, dpi);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, pw, ph);
    auto old = SelectObject(dc, bitmap);
    dpi::configureDC(dc, dpi);
    RECT canvas{0, 0, width, height};
    auto background = CreateSolidBrush(skin::field());
    FillRect(dc, &canvas, background);
    DeleteObject(background);
    skin::panel(dc, 0, 0, width, height, true);
    for (int i = 0; i < visible && first + i < int(rows.size()); ++i) {
      auto &row = rows[first + i];
      RECT bounds{0, 6 + i * 38, width, 42 + i * 38};
      skin::projectMenuRow(dc, bounds, row.title, row.glyph, row.viewerOnly, row.active,
                           hover == first + i, true);
    }
    SetMapMode(dc, MM_TEXT);
    SetViewportOrgEx(dc, 0, 0, nullptr);
    RECT client{};
    GetClientRect(window, &client);
    SetStretchBltMode(target, HALFTONE);
    SetBrushOrgEx(target, 0, 0, nullptr);
    StretchBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, pw, ph, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
  }
};
LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
  auto *p = reinterpret_cast<Popup *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    p = static_cast<Popup *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
    p->window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, LONG_PTR(p));
  }
  if (!p)
    return DefWindowProcW(window, message, w, l);
  switch (message) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps{};
    auto dc = BeginPaint(window, &ps);
    p->paint(dc);
    EndPaint(window, &ps);
    return 0;
  }
  case WM_PRINTCLIENT:
    p->paint(reinterpret_cast<HDC>(w));
    return 0;
  case WM_TIMER:
    p->animate();
    return 0;
  case WM_MOUSEMOVE:
    if (!p->closing) {
      int hit = p->hit(l);
      if (hit != p->hover) {
        p->hover = hit;
        InvalidateRect(window, nullptr, FALSE);
      }
    }
    return 0;
  case WM_LBUTTONUP:
    if (!p->closing) {
      auto hit = p->hit(l);
      if (hit >= 0)
        p->close(p->rows[hit].id);
      else
        p->close();
    }
    return 0;
  case WM_RBUTTONDOWN:
    p->close();
    return 0;
  case WM_MOUSEWHEEL:
    if (!p->closing) {
      p->first = std::clamp(p->first - (short(HIWORD(w)) > 0 ? 3 : -3), 0,
                            std::max(0, int(p->rows.size()) - p->visible));
      p->hover = -1;
      InvalidateRect(window, nullptr, FALSE);
    }
    return 0;
  case WM_KEYDOWN:
    if (p->closing)
      return 0;
    if (w == VK_ESCAPE || w == VK_TAB)
      p->close();
    else if (w == VK_DOWN)
      p->select(p->hover < 0 ? 0 : (p->hover + 1) % p->rows.size());
    else if (w == VK_UP)
      p->select(p->hover < 0 ? int(p->rows.size()) - 1
                             : (p->hover + int(p->rows.size()) - 1) % p->rows.size());
    else if (w == VK_HOME)
      p->select(0);
    else if (w == VK_END)
      p->select(int(p->rows.size()) - 1);
    else if (w == VK_NEXT)
      p->select(std::max(0, p->hover) + p->visible);
    else if (w == VK_PRIOR)
      p->select(std::max(0, p->hover) - p->visible);
    else if ((w == VK_RETURN || w == VK_SPACE) && p->hover >= 0)
      p->close(p->rows[p->hover].id);
    return 0;
  case WM_ACTIVATE:
    if (LOWORD(w) == WA_INACTIVE)
      p->close();
    return 0;
  case WM_CAPTURECHANGED:
    if (reinterpret_cast<HWND>(l) != window)
      p->close();
    return 0;
  case WM_CLOSE:
  case WM_CANCELMODE:
    p->close();
    return 0;
  case WM_DPICHANGED:
    p->close();
    return 0; // Reopen on the new monitor rather than mixing scales.
  }
  return DefWindowProcW(window, message, w, l);
}
} // namespace
PopupFrame projectPopupFrame(double milliseconds, bool closing, bool motion) {
  if (!std::isfinite(milliseconds))
    milliseconds = 200;
  if (!motion)
    return {closing ? .9 : 1, closing ? 0. : 1., true};
  double time = std::max(0., milliseconds);
  double scale = closing ? 1 - .1 * ease(time / 160, .2, 1.3, .4, 1)
                         : .9 + .1 * ease(time / 200, .2, 1.4, .4, 1);
  double opacity = ease(time / 120, .25, .1, .25, 1);
  return {scale, closing ? 1 - opacity : opacity, time >= (closing ? 160 : 200)};
}
unsigned projectPopup(HWND owner, HWND anchor, const std::vector<ProjectPopupRow> &rows) {
  if (rows.empty())
    return 0;
  Popup popup{rows};
  popup.anchor = anchor;
  popup.motion = skin::animationEnabled();
  popup.dpi = dpi::forWindow(anchor);
  dpi::Scope scope(popup.dpi);
  auto dc = dpi::getDC(anchor);
  for (auto &row : rows)
    popup.width =
        std::max(popup.width, skin::textWidth(dc, row.title, 13, 600) +
                                  (row.glyph.empty() ? 0 : 28) + (row.viewerOnly ? 82 : 0) + 52);
  ReleaseDC(anchor, dc);
  RECT bounds{};
  GetWindowRect(anchor, &bounds);
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST), &monitor);
  popup.width = std::clamp(
      popup.width, 190,
      std::max(190, dpi::logical(monitor.rcWork.right - monitor.rcWork.left, popup.dpi) - 16));
  popup.visible = std::min(
      int(rows.size()),
      std::max(1, (dpi::logical(monitor.rcWork.bottom - monitor.rcWork.top, popup.dpi) - 28) / 38));
  popup.height = 12 + popup.visible * 38 - 2;
  popup.x = std::clamp(int(bounds.left), int(monitor.rcWork.left),
                       int(monitor.rcWork.right) - dpi::pixels(popup.width, popup.dpi));
  popup.y = bounds.bottom + dpi::pixels(8, popup.dpi);
  if (popup.y + dpi::pixels(popup.height, popup.dpi) > monitor.rcWork.bottom)
    popup.y = std::max(int(monitor.rcWork.top),
                       int(bounds.top) - dpi::pixels(popup.height + 8, popup.dpi));
  WNDCLASSW cls{};
  cls.lpfnWndProc = proc;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"TangOSProjectPopup";
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassW(&cls);
  auto priorFocus = GetFocus();
  auto window =
      CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, cls.lpszClassName, L"Choose a project",
                      WS_POPUP, popup.x, popup.y, dpi::pixels(popup.width, popup.dpi),
                      dpi::pixels(popup.height, popup.dpi), owner, nullptr, cls.hInstance, &popup);
  if (!window)
    throw std::runtime_error("Cannot create native project popup");
  popup.started = GetTickCount64();
  popup.animate();
  ShowWindow(window, SW_SHOW);
  SetFocus(window);
  SetCapture(window);
  SetTimer(window, 1, 10, nullptr);
  MSG message{};
  bool quit = false;
  int quitCode = 0;
  while (!popup.done && IsWindow(owner)) {
    auto status = GetMessageW(&message, nullptr, 0, 0);
    if (status <= 0) {
      quit = status == 0;
      quitCode = int(message.wParam);
      break;
    }
    if (message.message == WM_KEYDOWN && GetAncestor(message.hwnd, GA_ROOT) == owner)
      SendMessageW(window, message.message, message.wParam, message.lParam);
    else {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  if (GetCapture() == window)
    ReleaseCapture();
  KillTimer(window, 1);
  DestroyWindow(window);
  if (IsWindow(priorFocus))
    SetFocus(priorFocus);
  if (quit)
    PostQuitMessage(quitCode);
  return popup.selection;
}
} // namespace lite