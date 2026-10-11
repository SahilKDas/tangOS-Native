#include "dpi.h"
#include <commctrl.h>
#include <algorithm>
#include <map>
#include <string>
#include <tuple>

namespace dpi {
namespace {
thread_local UINT active = 96;
std::map<std::wstring, WNDPROC> procedures;
std::map<std::pair<HFONT, UINT>, HFONT> fonts;
struct Geometry {
  int x, y, width, height;
};
std::map<HWND, Geometry> geometry;
std::map<HWND, UINT> simulated;
UINT valid(UINT value) { return value >= 48 && value <= 768 ? value : 96; }
HFONT scaledFont(HFONT original, UINT value) {
  if (!original || value == 96)
    return original;
  auto key = std::make_pair(original, value);
  auto found = fonts.find(key);
  if (found != fonts.end())
    return found->second;
  LOGFONTW definition{};
  if (!GetObjectW(original, sizeof(definition), &definition))
    return original;
  definition.lfHeight = pixels(definition.lfHeight, value);
  definition.lfWidth = pixels(definition.lfWidth, value);
  auto result = CreateFontIndirectW(&definition);
  if (result)
    fonts[key] = result;
  return result ? result : original;
}
void refreshTree(HWND root, UINT value) {
  EnumChildWindows(
      root,
      [](HWND child, LPARAM context) -> BOOL {
        auto value = UINT(context);
        auto font = reinterpret_cast<HFONT>(GetPropW(child, L"TangOSLogicalFont"));
        if (font)
          ::SendMessageW(child, WM_SETFONT, WPARAM(scaledFont(font, value)), TRUE);
        auto found = geometry.find(child);
        if (found != geometry.end()) {
          auto g = found->second;
          ::SetWindowPos(child, nullptr, pixels(g.x, value), pixels(g.y, value),
                         pixels(g.width, value), pixels(g.height, value),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        auto comboHeight = reinterpret_cast<UINT_PTR>(GetPropW(child, L"TangOSComboHeight"));
        if (comboHeight) {
          Scope scale(value);
          compactComboHeight(child, int(comboHeight));
        }
        InvalidateRect(child, nullptr, FALSE);
        return TRUE;
      },
      LPARAM(value));
}
LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM w, LPARAM l) {
  wchar_t name[128]{};
  GetClassNameW(window, name, 128);
  auto found = procedures.find(name);
  if (found == procedures.end())
    return DefWindowProcW(window, message, w, l);
  Scope scope(message == WM_DPICHANGED ? LOWORD(w) : forWindow(window));
  auto call = [&](WPARAM a, LPARAM b) { return found->second(window, message, a, b); };
  if (message == WM_DPICHANGED) {
    refreshTree(window, current());
    if (l)
      applySuggestedRect(window, *reinterpret_cast<RECT *>(l));
    auto result = call(w, l);
    InvalidateRect(window, nullptr, FALSE);
    return result;
  }
  if (message == WM_SIZE)
    return call(w, MAKELPARAM(logical(LOWORD(l), current()), logical(HIWORD(l), current())));
  if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
    return call(
        w, MAKELPARAM(logical(short(LOWORD(l)), current()), logical(short(HIWORD(l)), current())));
  if (message == WM_NCHITTEST)
    return call(
        w, MAKELPARAM(logical(short(LOWORD(l)), current()), logical(short(HIWORD(l)), current())));
  if (message == WM_DRAWITEM && l) {
    auto item = *reinterpret_cast<DRAWITEMSTRUCT *>(l);
    item.rcItem = {logical(item.rcItem.left, current()), logical(item.rcItem.top, current()),
                   logical(item.rcItem.right, current()), logical(item.rcItem.bottom, current())};
    auto saved = SaveDC(item.hDC);
    configureDC(item.hDC, current());
    auto result = call(w, LPARAM(&item));
    RestoreDC(item.hDC, saved);
    return result;
  }
  if (message == WM_MEASUREITEM && l) {
    auto result = call(w, l);
    auto item = reinterpret_cast<MEASUREITEMSTRUCT *>(l);
    item->itemWidth = pixels(item->itemWidth, current());
    item->itemHeight = pixels(item->itemHeight, current());
    return result;
  }
  if (message == WM_GETMINMAXINFO && l) {
    auto result = call(w, l);
    auto info = reinterpret_cast<MINMAXINFO *>(l);
    info->ptMinTrackSize.x = pixels(info->ptMinTrackSize.x, current());
    info->ptMinTrackSize.y = pixels(info->ptMinTrackSize.y, current());
    return result;
  }
  if (message == WM_PRINTCLIENT && w) {
    auto dc = reinterpret_cast<HDC>(w);
    auto saved = SaveDC(dc);
    configureDC(dc, current());
    auto result = call(w, l);
    RestoreDC(dc, saved);
    return result;
  }
  auto result = call(w, l);
  if (message == WM_NCDESTROY)
    geometry.erase(window);
  return result;
}
} // namespace
UINT current() { return active; }
UINT forWindow(HWND window) {
  auto found = simulated.find(GetAncestor(window, GA_ROOT));
  if (found != simulated.end())
    return found->second;
  return valid(window ? GetDpiForWindow(window) : current());
}
int pixels(int value, UINT dpi) { return MulDiv(value, valid(dpi), 96); }
int logical(int value, UINT dpi) { return MulDiv(value, 96, valid(dpi)); }
Scope::Scope(UINT value) : previous(active) { active = valid(value); }
Scope::~Scope() { active = previous; }
void enable() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  active = valid(GetDpiForSystem());
}
void shutdown() {
  for (auto &entry : fonts)
    DeleteObject(entry.second);
  fonts.clear();
  geometry.clear();
  simulated.clear();
}
UINT forDC(HDC dc) {
  SIZE window{}, viewport{};
  GetWindowExtEx(dc, &window);
  GetViewportExtEx(dc, &viewport);
  return window.cx ? valid(UINT(MulDiv(viewport.cx, 96, window.cx))) : 96;
}
void configureDC(HDC dc, UINT value) {
  SetMapMode(dc, MM_ANISOTROPIC);
  SetWindowExtEx(dc, 96, 96, nullptr);
  SetViewportExtEx(dc, valid(value), valid(value), nullptr);
}
ATOM registerClass(const WNDCLASSW *value) {
  auto copy = *value;
  procedures[value->lpszClassName] = value->lpfnWndProc;
  copy.lpfnWndProc = dispatch;
  return ::RegisterClassW(&copy);
}
HWND createWindow(DWORD ex, LPCWSTR kind, LPCWSTR title, DWORD style, int x, int y, int width,
                  int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID data) {
  auto scale = current();
  auto coordinate = [&](int value) {
    return value == CW_USEDEFAULT ? value : pixels(value, scale);
  };
  auto result =
      ::CreateWindowExW(ex, kind, title, style, coordinate(x), coordinate(y), coordinate(width),
                        coordinate(height), parent, menu, instance, data);
  if (result && !(style & WS_CHILD) && width != CW_USEDEFAULT && height != CW_USEDEFAULT &&
      forWindow(result) != scale)
    ::SetWindowPos(result, nullptr, 0, 0, pixels(width, forWindow(result)),
                   pixels(height, forWindow(result)), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  if (result && (style & WS_CHILD))
    geometry[result] = {x, y, width, height};
  return result;
}
BOOL clientRect(HWND window, LPRECT bounds) {
  if (!::GetClientRect(window, bounds))
    return FALSE;
  bounds->right = logical(bounds->right, current());
  bounds->bottom = logical(bounds->bottom, current());
  return TRUE;
}
BOOL windowRect(HWND window, LPRECT bounds) {
  if (!::GetWindowRect(window, bounds))
    return FALSE;
  int width = logical(bounds->right - bounds->left, current());
  int height = logical(bounds->bottom - bounds->top, current());
  bounds->left = logical(bounds->left, current());
  bounds->top = logical(bounds->top, current());
  bounds->right = bounds->left + width;
  bounds->bottom = bounds->top + height;
  return TRUE;
}
BOOL relativeRect(HWND window, HWND parent, LPRECT bounds) {
  // Convert only after mapping in physical pixels. A screen-space round trip
  // at fractional DPI can otherwise move an edge by a whole logical pixel.
  if (!::GetWindowRect(window, bounds))
    return FALSE;
  ::MapWindowPoints(nullptr, parent, reinterpret_cast<POINT *>(bounds), 2);
  int width = logical(bounds->right - bounds->left, current());
  int height = logical(bounds->bottom - bounds->top, current());
  bounds->left = logical(bounds->left, current());
  bounds->top = logical(bounds->top, current());
  bounds->right = bounds->left + width;
  bounds->bottom = bounds->top + height;
  return TRUE;
}
BOOL moveWindow(HWND window, int x, int y, int width, int height, BOOL repaint) {
  if (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)
    geometry[window] = {x, y, width, height};
  return ::MoveWindow(window, pixels(x, current()), pixels(y, current()), pixels(width, current()),
                      pixels(height, current()), repaint);
}
BOOL setWindowPos(HWND window, HWND order, int x, int y, int width, int height, UINT flags) {
  if (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) {
    auto &g = geometry[window];
    if (!(flags & SWP_NOMOVE)) {
      g.x = x;
      g.y = y;
    }
    if (!(flags & SWP_NOSIZE)) {
      g.width = width;
      g.height = height;
    }
  }
  return ::SetWindowPos(window, order, pixels(x, current()), pixels(y, current()),
                        pixels(width, current()), pixels(height, current()), flags);
}
int mapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count) {
  for (UINT i = 0; i < count; ++i) {
    points[i].x = pixels(points[i].x, current());
    points[i].y = pixels(points[i].y, current());
  }
  auto result = ::MapWindowPoints(from, to, points, count);
  for (UINT i = 0; i < count; ++i) {
    points[i].x = logical(points[i].x, current());
    points[i].y = logical(points[i].y, current());
  }
  return result;
}
BOOL screenToClient(HWND window, LPPOINT point) {
  point->x = pixels(point->x, current());
  point->y = pixels(point->y, current());
  auto result = ::ScreenToClient(window, point);
  point->x = logical(point->x, current());
  point->y = logical(point->y, current());
  return result;
}
BOOL clientToScreen(HWND window, LPPOINT point) {
  point->x = pixels(point->x, current());
  point->y = pixels(point->y, current());
  auto result = ::ClientToScreen(window, point);
  point->x = logical(point->x, current());
  point->y = logical(point->y, current());
  return result;
}
BOOL cursorPos(LPPOINT point) {
  auto result = ::GetCursorPos(point);
  point->x = logical(point->x, current());
  point->y = logical(point->y, current());
  return result;
}
HDC beginPaint(HWND window, LPPAINTSTRUCT paint) {
  auto dc = ::BeginPaint(window, paint);
  configureDC(dc, current());
  return dc;
}
HDC getDC(HWND window) {
  auto dc = ::GetDC(window);
  configureDC(dc, current());
  return dc;
}
HDC getWindowDC(HWND window) {
  auto dc = ::GetWindowDC(window);
  configureDC(dc, current());
  return dc;
}
HDC compatibleDC(HDC source) {
  auto dc = ::CreateCompatibleDC(source);
  configureDC(dc, source ? forDC(source) : current());
  return dc;
}
HBITMAP compatibleBitmap(HDC source, int width, int height) {
  return ::CreateCompatibleBitmap(source, pixels(width, forDC(source)),
                                  pixels(height, forDC(source)));
}
BOOL viewportOrigin(HDC dc, int x, int y, LPPOINT previous) {
  return ::SetViewportOrgEx(dc, pixels(x, forDC(dc)), pixels(y, forDC(dc)), previous);
}
BOOL offsetViewportOrigin(HDC dc, int x, int y, LPPOINT previous) {
  return ::OffsetViewportOrgEx(dc, pixels(x, forDC(dc)), pixels(y, forDC(dc)), previous);
}
HRGN roundRegion(int left, int top, int right, int bottom, int ew, int eh) {
  return ::CreateRoundRectRgn(pixels(left, current()), pixels(top, current()),
                              pixels(right, current()), pixels(bottom, current()),
                              pixels(ew, current()), pixels(eh, current()));
}
HRGN rectRegion(int left, int top, int right, int bottom) {
  return ::CreateRectRgn(pixels(left, current()), pixels(top, current()), pixels(right, current()),
                         pixels(bottom, current()));
}
HFONT paintFont(HWND window) {
  auto font = reinterpret_cast<HFONT>(GetPropW(window, L"TangOSLogicalFont"));
  return font ? font : reinterpret_cast<HFONT>(::SendMessageW(window, WM_GETFONT, 0, 0));
}
void compactComboHeight(HWND window, int height) {
  SetPropW(window, L"TangOSComboHeight", reinterpret_cast<HANDLE>(UINT_PTR(height)));
  RECT bounds{};
  ::GetWindowRect(window, &bounds);
  int item = int(::SendMessageW(window, CB_GETITEMHEIGHT, WPARAM(-1), 0));
  int border = bounds.bottom - bounds.top - item;
  if (item >= 0 && border >= 0)
    ::SendMessageW(window, CB_SETITEMHEIGHT, WPARAM(-1),
                   std::max(1, pixels(height, current()) - border));
}
LRESULT sendMessage(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == EM_SETRECT || message == EM_SETRECTNP) {
    if (!l)
      return ::SendMessageW(window, message, w, l);
    auto bounds = *reinterpret_cast<RECT *>(l);
    bounds = {pixels(bounds.left, current()), pixels(bounds.top, current()),
              pixels(bounds.right, current()), pixels(bounds.bottom, current())};
    return ::SendMessageW(window, message, w, LPARAM(&bounds));
  }
  if (message == CB_SETITEMHEIGHT || message == LB_SETITEMHEIGHT)
    return ::SendMessageW(window, message, w, pixels(int(l), current()));
  if (message == WM_SETFONT) {
    SetPropW(window, L"TangOSLogicalFont", reinterpret_cast<HANDLE>(w));
    return ::SendMessageW(window, message,
                          WPARAM(scaledFont(reinterpret_cast<HFONT>(w), forWindow(window))), l);
  }
  if (message == WM_GETFONT)
    return LRESULT(paintFont(window));
  if (message == WM_DRAWITEM && l) {
    auto item = *reinterpret_cast<DRAWITEMSTRUCT *>(l);
    item.rcItem = {pixels(item.rcItem.left, current()), pixels(item.rcItem.top, current()),
                   pixels(item.rcItem.right, current()), pixels(item.rcItem.bottom, current())};
    return ::SendMessageW(window, message, w, LPARAM(&item));
  }
  if (message == WM_PRINT && w &&
      !(GetClassLongPtrW(window, GCLP_WNDPROC) == ULONG_PTR(dispatch))) {
    auto dc = reinterpret_cast<HDC>(w);
    auto saved = SaveDC(dc);
    SetMapMode(dc, MM_TEXT);
    auto result = ::SendMessageW(window, message, w, l);
    RestoreDC(dc, saved);
    return result;
  }
  if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
    l = MAKELPARAM(pixels(short(LOWORD(l)), current()), pixels(short(HIWORD(l)), current()));
  return ::SendMessageW(window, message, w, l);
}
BOOL trackPopup(HMENU menu, UINT flags, int x, int y, int reserved, HWND owner,
                const RECT *bounds) {
  return ::TrackPopupMenu(menu, flags, pixels(x, current()), pixels(y, current()), reserved, owner,
                          bounds);
}
void applySuggestedRect(HWND window, const RECT &bounds) {
  ::SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                 bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
}
namespace testing {
void simulateWindowDpi(HWND window, UINT value) {
  if (value)
    simulated[GetAncestor(window, GA_ROOT)] = valid(value);
  else
    simulated.erase(GetAncestor(window, GA_ROOT));
}
} // namespace testing
} // namespace dpi
