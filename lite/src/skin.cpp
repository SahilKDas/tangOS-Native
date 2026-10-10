#include "skin.h"
#include "help.h"
#include <commctrl.h>
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <tuple>
#include <vector>
extern "C" void tangos_shape(unsigned char *, unsigned, unsigned, float, uint32_t, uint32_t);
extern "C" void tangos_frame(unsigned char *, unsigned, unsigned, float, uint32_t, uint32_t);
extern "C" void tangos_gradient_frame(unsigned char *, unsigned, unsigned, float, uint32_t,
                                      uint32_t, uint32_t);
extern "C" void tangos_image(unsigned char *, unsigned, unsigned, const unsigned char *, size_t);
extern "C" void tangos_mesh(unsigned char *, unsigned, unsigned, float, unsigned);
extern "C" void tangos_shadow(unsigned char *, unsigned, unsigned, unsigned, float, float, int, float);
extern "C" void tangos_icon(unsigned char *, unsigned, unsigned, unsigned, uint32_t);
extern "C" void tangos_glass(unsigned char *, unsigned, unsigned, uint32_t, uint32_t, uint32_t,
                             unsigned);
namespace skin {
namespace {
struct Color {
  uint32_t value;
  Color(int r, int g, int b) : Color(255, r, g, b) {}
  Color(int a, int r, int g, int b) : value((uint32_t(a) << 24) | (r << 16) | (g << 8) | b) {}
  int GetR() const { return (value >> 16) & 255; }
  int GetG() const { return (value >> 8) & 255; }
  int GetB() const { return value & 255; }
};
struct Palette {
  Color top, middle, bottom, primary, ink, muted, panel, field;
};
Palette colors{Color(143, 208, 248),      Color(126, 200, 240), Color(142, 200, 65),
               Color(0, 153, 224),        Color(13, 58, 92),    Color(72, 116, 156),
               Color(200, 234, 244, 253), Color(234, 244, 253)};
std::map<std::tuple<int, int, bool, bool>, HFONT> fonts;
HFONT uiFont(int size, bool bold, int weight = 0, bool italic = false, bool mono = false) {
  if (!weight)
    weight = bold ? FW_BOLD : FW_NORMAL;
  auto key = std::make_tuple(size, weight, italic, mono);
  auto found = fonts.find(key);
  if (found != fonts.end())
    return found->second;
  auto font = CreateFontW(-size, 0, 0, 0, weight, italic, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, 0, mono ? L"Consolas" : L"Nunito");
  fonts[key] = font;
  return font;
}
HANDLE fontResource = nullptr;
bool motionEnabled = true, systemMotion = true;
unsigned paletteIndex = 0;
float phase = 0;
ULONGLONG lastMotion = 0;
COLORREF rgb(Color c) { return RGB(c.GetR(), c.GetG(), c.GetB()); }
struct Surface {
  HDC target, dc;
  HBITMAP bitmap;
  HGDIOBJ previous;
  unsigned char *data = nullptr;
  int x, y, w, h;
  Surface(HDC dest, int xx, int yy, int ww, int hh) : target(dest), x(xx), y(yy), w(ww), h(hh) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    dc = CreateCompatibleDC(dest);
    bitmap = CreateDIBSection(dest, &info, DIB_RGB_COLORS, (void **)&data, nullptr, 0);
    previous = SelectObject(dc, bitmap);
    BitBlt(dc, 0, 0, w, h, dest, x, y, SRCCOPY);
    GdiFlush();
  }
  ~Surface() {
    BitBlt(target, x, y, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
  }
};
struct Backdrop {
  HDC dc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous = nullptr;
  int width = 0, height = 0;
  ULONGLONG rendered = 0;
  ~Backdrop() {
    if (dc) {
      SelectObject(dc, previous);
      DeleteObject(bitmap);
      DeleteDC(dc);
    }
  }
};
std::map<HWND, std::unique_ptr<Backdrop>> buttonBackdrops;
struct IconTooltip {
  HWND window = nullptr;
  std::wstring text;
  ~IconTooltip() {
    if (window)
      DestroyWindow(window);
  }
};
std::map<HWND, std::unique_ptr<IconTooltip>> iconTooltips;
void buttonBackground(const DRAWITEMSTRUCT &item) {
  HWND parent = GetParent(item.hwndItem);
  RECT parentBounds{}, bounds{};
  if (!parent || !GetClientRect(parent, &parentBounds) || !GetWindowRect(item.hwndItem, &bounds))
    return;
  MapWindowPoints(nullptr, parent, reinterpret_cast<POINT *>(&bounds), 2);
  auto &cached = buttonBackdrops[parent];
  if (!cached || cached->width != parentBounds.right || cached->height != parentBounds.bottom) {
    cached = std::make_unique<Backdrop>();
    cached->width = parentBounds.right;
    cached->height = parentBounds.bottom;
    cached->dc = CreateCompatibleDC(item.hDC);
    cached->bitmap = CreateCompatibleBitmap(item.hDC, cached->width, cached->height);
    cached->previous = SelectObject(cached->dc, cached->bitmap);
  }
  if (!cached->rendered) {
    SetPropW(parent, L"TangOSBackdropRendering", reinterpret_cast<HANDLE>(1));
    SendMessageW(parent, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(cached->dc), PRF_CLIENT);
    RemovePropW(parent, L"TangOSBackdropRendering");
    cached->rendered = 1;
  }
  BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right - item.rcItem.left,
         item.rcItem.bottom - item.rcItem.top, cached->dc, bounds.left, bounds.top, SRCCOPY);
  for (auto it = buttonBackdrops.begin(); it != buttonBackdrops.end();)
    if (!IsWindow(it->first))
      it = buttonBackdrops.erase(it);
    else
      ++it;
}
LRESULT CALLBACK hoverButton(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                             DWORD_PTR) {
  if (message == WM_MOUSEMOVE) {
    if (!GetPropW(window, L"TangOSHover")) {
      SetPropW(window, L"TangOSHover", reinterpret_cast<HANDLE>(1));
      TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
      TrackMouseEvent(&tracking);
      InvalidateRect(window, nullptr, FALSE);
    }
  } else if (message == WM_MOUSELEAVE) {
    RemovePropW(window, L"TangOSHover");
    InvalidateRect(window, nullptr, FALSE);
  } else if (message == WM_NCDESTROY) {
    iconTooltips.erase(window);
    RemovePropW(window, L"TangOSHover");
    RemovePropW(window, L"TangOSIcon");
    RemoveWindowSubclass(window, hoverButton, id);
  }
  return DefSubclassProc(window, message, w, l);
}
void paintCompactCombo(HWND window, HDC dc) {
  RECT bounds{};
  GetClientRect(window, &bounds);
  DRAWITEMSTRUCT background{};
  background.hwndItem = window;
  background.hDC = dc;
  background.rcItem = bounds;
  buttonBackground(background);
  static const Color gloss[] = {Color(234, 244, 253), Color(250, 208, 172), Color(20, 44, 70),
                                Color(252, 214, 240), Color(238, 255, 196)};
  static const Color borders[] = {Color(217, 255, 255, 255), Color(184, 255, 214, 182),
                                  Color(56, 120, 190, 230), Color(184, 255, 210, 238),
                                  Color(184, 214, 238, 168)};
  auto fill = gloss[paletteIndex];
  auto edge =
      GetPropW(window, L"TangOSNeedsRole") ? Color(255, 234, 179, 8) : borders[paletteIndex];
  Surface surface(dc, 0, 0, bounds.right, bounds.bottom);
  if (surface.data)
    tangos_frame(surface.data, bounds.right, bounds.bottom, 8,
                 Color(140, fill.GetR(), fill.GetG(), fill.GetB()).value, edge.value);
  std::wstring value(GetWindowTextLengthW(window) + 1, 0);
  GetWindowTextW(window, value.data(), int(value.size()));
  auto previous =
      SelectObject(surface.dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
  SetBkMode(surface.dc, TRANSPARENT);
  SetTextColor(surface.dc, rgb(IsWindowEnabled(window) ? colors.ink : colors.muted));
  RECT textBounds{6, 0, bounds.right - 21, bounds.bottom};
  DrawTextW(surface.dc, value.c_str(), -1, &textBounds,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  SelectObject(surface.dc, previous);
  auto pen = CreatePen(PS_SOLID, 1, rgb(colors.ink));
  auto oldPen = SelectObject(surface.dc, pen);
  MoveToEx(surface.dc, bounds.right - 14, bounds.bottom / 2 - 2, nullptr);
  LineTo(surface.dc, bounds.right - 10, bounds.bottom / 2 + 2);
  LineTo(surface.dc, bounds.right - 6, bounds.bottom / 2 - 2);
  SelectObject(surface.dc, oldPen);
  DeleteObject(pen);
}
LRESULT CALLBACK compactComboProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                  DWORD_PTR) {
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    auto dc = BeginPaint(window, &paint);
    paintCompactCombo(window, dc);
    EndPaint(window, &paint);
    return 0;
  }
  if (message == WM_PRINT || message == WM_PRINTCLIENT) {
    paintCompactCombo(window, reinterpret_cast<HDC>(w));
    return 0;
  }
  if (message == WM_ERASEBKGND)
    return 1;
  if (message == WM_NCDESTROY) {
    RemovePropW(window, L"TangOSCompactCombo");
    RemovePropW(window, L"TangOSNeedsRole");
    RemoveWindowSubclass(window, compactComboProc, id);
  }
  auto result = DefSubclassProc(window, message, w, l);
  if (message == CB_SETCURSEL || message == WM_ENABLE || message == WM_SETFOCUS ||
      message == WM_KILLFOCUS)
    InvalidateRect(window, nullptr, FALSE);
  return result;
}
void paintCompactEdit(HWND window, HDC dc) {
  RECT bounds{};
  GetWindowRect(window, &bounds);
  int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
  DRAWITEMSTRUCT background{};
  background.hwndItem = window;
  background.hDC = dc;
  background.rcItem = {0, 0, width, height};
  buttonBackground(background);
  auto tint = compactField();
  Surface surface(dc, 0, 0, width, height);
  if (surface.data)
    tangos_frame(surface.data, width, height, 8,
                 Color(166, GetRValue(tint), GetGValue(tint), GetBValue(tint)).value,
                 paletteIndex == 2 ? Color(56, 120, 190, 230).value
                                   : Color(217, 255, 255, 255).value);
}
LRESULT CALLBACK compactEditProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                 DWORD_PTR) {
  if (message == WM_NCCALCSIZE) {
    auto bounds =
        w ? &reinterpret_cast<NCCALCSIZE_PARAMS *>(l)->rgrc[0] : reinterpret_cast<RECT *>(l);
    auto dc = GetDC(window);
    auto previous =
        SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    ReleaseDC(window, dc);
    int height = bounds->bottom - bounds->top;
    int top = std::max(1, (height - int(metrics.tmHeight)) / 2);
    int bottom = std::max(1, height - int(metrics.tmHeight) - top);
    bounds->left += 6;
    bounds->right -= 6;
    bounds->top += top;
    bounds->bottom -= bottom;
    return 0;
  }
  if (message == WM_NCPAINT) {
    auto dc = GetWindowDC(window);
    RECT client{}, bounds{};
    GetClientRect(window, &client);
    GetWindowRect(window, &bounds);
    POINT origin{};
    ClientToScreen(window, &origin);
    int saved = SaveDC(dc);
    ExcludeClipRect(dc, origin.x - bounds.left, origin.y - bounds.top,
                    origin.x - bounds.left + client.right, origin.y - bounds.top + client.bottom);
    paintCompactEdit(window, dc);
    RestoreDC(dc, saved);
    ReleaseDC(window, dc);
    return 0;
  }
  if (message == WM_PRINT) {
    auto dc = reinterpret_cast<HDC>(w);
    paintCompactEdit(window, dc);
    RECT bounds{};
    GetWindowRect(window, &bounds);
    POINT origin{};
    ClientToScreen(window, &origin);
    auto saved = SaveDC(dc);
    OffsetViewportOrgEx(dc, origin.x - bounds.left, origin.y - bounds.top, nullptr);
    auto result = DefSubclassProc(window, WM_PRINTCLIENT, w, PRF_CLIENT);
    RestoreDC(dc, saved);
    return result;
  }
  if (message == WM_NCDESTROY) {
    RemovePropW(window, L"TangOSCompactEdit");
    RemoveWindowSubclass(window, compactEditProc, id);
  }
  return DefSubclassProc(window, message, w, l);
}
void shape(HDC dc, int x, int y, int w, int h, float radius, Color top, Color bottom) {
  if (w <= 0 || h <= 0)
    return;
  Surface s(dc, x, y, w, h);
  if (s.data)
    tangos_shape(s.data, w, h, radius, top.value, bottom.value);
}
} // namespace
void initialize() {
  BOOL enabled = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
  systemMotion = enabled;
  auto r = FindResourceW(nullptr, MAKEINTRESOURCEW(206), RT_RCDATA);
  if (r) {
    DWORD count = 0;
    fontResource = AddFontMemResourceEx(LockResource(LoadResource(nullptr, r)),
                                        SizeofResource(nullptr, r), nullptr, &count);
  }
}
void clearBackgroundFrames();
void shutdown() {
  iconTooltips.clear();
  buttonBackdrops.clear();
  clearBackgroundFrames();
  for (auto &font : fonts)
    DeleteObject(font.second);
  fonts.clear();
  if (fontResource)
    RemoveFontMemResourceEx(fontResource);
}
void theme(int i) {
  buttonBackdrops.clear();
  paletteIndex = std::clamp(i, 0, 4);
  switch (i) {
  case 1:
    colors = {Color(255, 217, 160),      Color(243, 128, 143), Color(168, 107, 201),
              Color(235, 94, 91),        Color(74, 30, 58),    Color(150, 84, 110),
              Color(220, 255, 234, 214), Color(255, 234, 214)};
    break;
  case 2:
    colors = {Color(6, 32, 47),      Color(4, 20, 31),     Color(2, 12, 21),
              Color(36, 211, 238),   Color(217, 242, 255), Color(122, 168, 196),
              Color(235, 7, 24, 42), Color(20, 44, 70)};
    break;
  case 3:
    colors = {Color(255, 214, 236),      Color(255, 174, 217), Color(195, 177, 247),
              Color(236, 72, 153),       Color(80, 22, 66),    Color(165, 88, 140),
              Color(215, 255, 233, 247), Color(255, 233, 247)};
    break;
  case 4:
    colors = {Color(214, 248, 151),      Color(160, 227, 189), Color(212, 242, 126),
              Color(90, 160, 0),         Color(44, 61, 6),     Color(111, 138, 56),
              Color(210, 238, 255, 196), Color(238, 255, 196)};
    break;
  default:
    colors = {Color(143, 208, 248),      Color(126, 200, 240), Color(142, 200, 65),
              Color(0, 153, 224),        Color(13, 58, 92),    Color(72, 116, 156),
              Color(200, 234, 244, 253), Color(234, 244, 253)};
  }
}
COLORREF text() { return rgb(colors.ink); }
COLORREF muted() { return rgb(colors.muted); }
COLORREF field() { return rgb(colors.field); }
void animate(bool enabled) { motionEnabled = enabled; }
bool animationEnabled() { return motionEnabled && systemMotion; }
void advance(bool visible) {
  auto now = GetTickCount64();
  if (visible && animationEnabled() && lastMotion)
    phase += std::min<ULONGLONG>(now - lastMotion, 250) / 1000.f;
  lastMotion = now;
}
static void drawBackground(HDC dc, int w, int h) {
  if (w > 0 && h > 0) {
    Surface surface(dc, 0, 0, w, h);
    if (surface.data)
      tangos_mesh(surface.data, w, h, animationEnabled() ? phase : 0, paletteIndex);
    return;
  }
}
namespace {
struct BackgroundFrame {
  HDC dc;
  HBITMAP bitmap;
  HGDIOBJ previous;
  int w, h;
  unsigned palette;
  float time;
  bool animated;
  BackgroundFrame(HDC target, int width, int height)
      : w(width), h(height), palette(~0u), time(-1), animated(false) {
    dc = CreateCompatibleDC(target);
    bitmap = CreateCompatibleBitmap(target, w, h);
    previous = SelectObject(dc, bitmap);
  }
  ~BackgroundFrame() {
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
  }
};
std::vector<std::unique_ptr<BackgroundFrame>> backgroundFrames;
} // namespace
void clearBackgroundFrames() { backgroundFrames.clear(); }
void background(HDC dc, int w, int h, int offsetY, int totalHeight) {
  if (w <= 0 || h <= 0)
    return;
  int sourceHeight = std::max(h + std::max(0, offsetY), totalHeight);
  BackgroundFrame *frame = nullptr;
  for (auto &item : backgroundFrames)
    if (item->w == w && item->h == sourceHeight) {
      frame = item.get();
      break;
    }
  if (!frame) {
    if (backgroundFrames.size() >= 4)
      backgroundFrames.erase(backgroundFrames.begin());
    backgroundFrames.push_back(std::make_unique<BackgroundFrame>(dc, w, sourceHeight));
    frame = backgroundFrames.back().get();
  }
  bool motion = animationEnabled();
  if (frame->palette != paletteIndex || frame->animated != motion ||
      (motion && frame->time != phase)) {
    drawBackground(frame->dc, w, sourceHeight);
    frame->palette = paletteIndex;
    frame->animated = motion;
    frame->time = phase;
  }
  BitBlt(dc, 0, 0, w, h, frame->dc, 0, std::max(0, offsetY), SRCCOPY);
}
void panel(HDC dc, int x, int y, int w, int h, bool solid, PanelStyle style) {
  if (w <= 0 || h <= 0)
    return;
  static const Color gloss[] = {Color(234, 244, 253), Color(250, 208, 172), Color(20, 44, 70),
                                Color(252, 214, 240), Color(238, 255, 196)};
  static const Color tint[] = {Color(158, 255, 255, 255), Color(128, 255, 234, 214),
                               Color(189, 7, 24, 42), Color(128, 255, 233, 247),
                               Color(128, 240, 250, 208)};
  static const Color edge[] = {Color(217, 255, 255, 255), Color(184, 255, 214, 182),
                               Color(56, 120, 190, 230), Color(184, 255, 210, 238),
                               Color(184, 214, 238, 168)};
  static const Color base[] = {Color(124, 196, 242), Color(255, 178, 128), Color(5, 22, 38),
                               Color(255, 179, 217), Color(212, 242, 126)};
  if (style != PanelStyle::task)
    shape(dc, x, y + 4, w, h, 18, Color(20, 0, 0, 0), Color(20, 0, 0, 0));
  if (solid)
    shape(dc, x, y, w, h, 18, base[paletteIndex], base[paletteIndex]);
  Surface surface(dc, x, y, w, h);
  if (surface.data)
    tangos_glass(surface.data, w, h, gloss[paletteIndex].value, tint[paletteIndex].value,
                 edge[paletteIndex].value, unsigned(style));
}
void scrim(HDC dc, int w, int h) {
  if (w <= 0 || h <= 0)
    return;
  Surface surface(dc, 0, 0, w, h);
  if (!surface.data)
    return;
  std::vector<unsigned char> horizontal(size_t(w) * h * 4);
  constexpr int radius = 3;
  for (int y = 0; y < h; ++y)
    for (int channel = 0; channel < 3; ++channel) {
      int sum = 0;
      for (int dx = -radius; dx <= radius; ++dx)
        sum += surface.data[(size_t(y) * w + std::clamp(dx, 0, w - 1)) * 4 + channel];
      for (int x = 0; x < w; ++x) {
        horizontal[(size_t(y) * w + x) * 4 + channel] = (unsigned char)(sum / 7);
        sum -= surface.data[(size_t(y) * w + std::clamp(x - radius, 0, w - 1)) * 4 + channel];
        sum += surface.data[(size_t(y) * w + std::clamp(x + radius + 1, 0, w - 1)) * 4 + channel];
      }
    }
  for (int x = 0; x < w; ++x)
    for (int channel = 0; channel < 3; ++channel) {
      int sum = 0;
      for (int dy = -radius; dy <= radius; ++dy)
        sum += horizontal[(size_t(std::clamp(dy, 0, h - 1)) * w + x) * 4 + channel];
      for (int y = 0; y < h; ++y) {
        surface.data[(size_t(y) * w + x) * 4 + channel] = (unsigned char)(sum * .68 / 7);
        sum -= horizontal[(size_t(std::clamp(y - radius, 0, h - 1)) * w + x) * 4 + channel];
        sum += horizontal[(size_t(std::clamp(y + radius + 1, 0, h - 1)) * w + x) * 4 + channel];
      }
    }
}
void agentCard(HDC dc, int x, int y, int w, int h, COLORREF tint, bool hovered) {
  {
    constexpr int padding = 40;
    Surface shadow(dc, x - padding, y - padding, w + padding * 2, h + padding * 2);
    if (shadow.data)
      tangos_shadow(shadow.data, shadow.w, shadow.h, padding, 14, hovered ? 20 : 12,
                     hovered ? 8 : 4, hovered ? .13f : .08f);
  }
  static const Color gloss[] = {Color(234, 244, 253), Color(250, 208, 172), Color(20, 44, 70),
                                Color(252, 214, 240), Color(238, 255, 196)};
  auto fill = gloss[paletteIndex];
  Surface surface(dc, x, y, w, h);
  if (surface.data)
    tangos_frame(surface.data, w, h, 14, Color(230, fill.GetR(), fill.GetG(), fill.GetB()).value,
                 Color(255, GetRValue(tint), GetGValue(tint), GetBValue(tint)).value);
}
void splashBubble(HDC dc, int x, int y, int size, double opacity) {
  if (size <= 0)
    return;
  Surface surface(dc, x, y, size, size);
  if (surface.data)
    tangos_frame(surface.data, size, size, size / 2.f,
                 Color(int(76 * opacity), 255, 255, 255).value,
                 Color(int(140 * opacity), 255, 255, 255).value);
}
COLORREF matched() {
  static const COLORREF values[] = {RGB(63, 196, 95), RGB(61, 186, 122), RGB(45, 224, 138),
                                    RGB(52, 199, 123), RGB(79, 176, 0)};
  return values[paletteIndex % 5];
}
void presenceDot(HDC dc, int x, int y, const std::string &state, int size) {
  auto tint = state == "stale"     ? RGB(234, 179, 8)
              : state == "offline" ? RGB(220, 76, 46)
                                   : matched();
  if (state == "online live") {
    int alpha = animationEnabled() ? int(50 + 30 * std::sin(phase * 6)) : 60;
    auto glow = Color(alpha, GetRValue(tint), GetGValue(tint), GetBValue(tint));
    shape(dc, x - 4, y - 4, size + 8, size + 8, (size + 8) / 2.f, glow, glow);
  }
  auto fill = Color(255, GetRValue(tint), GetGValue(tint), GetBValue(tint));
  shape(dc, x, y, size, size, size / 2.f, fill, fill);
}
void progressBar(HDC dc, int x, int y, int width, double percent, COLORREF tint) {
  Surface surface(dc, x, y, width, 6);
  if (!surface.data)
    return;
  tangos_frame(surface.data, width, 6, 3, Color(26, 0, 0, 0).value, 0);
  int filled = int(std::round(width * std::clamp(percent, 0., 100.) / 100.));
  if (filled > 0) {
    auto clip = SaveDC(surface.dc);
    IntersectClipRect(surface.dc, 0, 0, filled, 6);
    auto fill = Color(255, GetRValue(tint), GetGValue(tint), GetBValue(tint));
    shape(surface.dc, 0, 0, filled, 6, 3, fill, fill);
    RestoreDC(surface.dc, clip);
  }
}
void taskText(HDC dc, const std::wstring &value, int x, int y, int width, int height, bool live,
              bool secondary) {
  if (height <= 0)
    return;
  auto previous = SelectObject(dc, uiFont(live ? 11 : 13, false, live ? 400 : 600, false, live));
  SetTextColor(dc, rgb(secondary ? colors.muted : colors.ink));
  SetBkMode(dc, TRANSPARENT);
  auto saved = SaveDC(dc);
  IntersectClipRect(dc, x, y, x + width, y + height);
  RECT bounds{x, y, x + width, y + height};
  DrawTextW(dc, value.c_str(), int(value.size()), &bounds,
            DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  RestoreDC(dc, saved);
  SelectObject(dc, previous);
}
int taskNoteHeight(HDC dc, const std::wstring &value, int width) {
  auto previous = SelectObject(dc, uiFont(11, false, 600));
  RECT bounds{0, 0, std::max(1, width - 18), 0};
  DrawTextW(dc, value.c_str(), int(value.size()), &bounds,
            DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
  SelectObject(dc, previous);
  return std::max(25, int(bounds.bottom) + 10);
}
void taskNote(HDC dc, const std::wstring &value, int x, int y, int width) {
  int height = taskNoteHeight(dc, value, width);
  Surface surface(dc, x, y, width, height);
  if (surface.data)
    tangos_frame(surface.data, width, height, 8, Color(41, 234, 179, 8).value,
                 Color(115, 234, 179, 8).value);
  auto previous = SelectObject(surface.dc, uiFont(11, false, 600));
  SetBkMode(surface.dc, TRANSPARENT);
  SetTextColor(surface.dc, RGB(124, 74, 3));
  RECT bounds{9, 5, width - 9, height - 5};
  DrawTextW(surface.dc, value.c_str(), int(value.size()), &bounds, DT_WORDBREAK | DT_NOPREFIX);
  SelectObject(surface.dc, previous);
}
void label(HDC dc, const std::wstring &s, int x, int y, int w, int h, int size, bool bold,
           bool secondary, bool accent, COLORREF tint, bool italic, int weight) {
  auto font = uiFont(size, bold, weight, italic);
  auto prev = SelectObject(dc, font);
  SetTextColor(dc, tint == CLR_INVALID ? rgb(accent      ? colors.primary
                                             : secondary ? colors.muted
                                                         : colors.ink)
                                       : tint);
  SetBkMode(dc, TRANSPARENT);
  RECT r{x, y, x + w, y + h};
  DrawTextW(dc, s.c_str(), (int)s.size(), &r, DT_NOPREFIX | DT_END_ELLIPSIS);
  SelectObject(dc, prev);
}
void helperPanel(HDC dc, int x, int y, int width, int height) {
  Surface surface(dc, x, y, width, height);
  if (!surface.data)
    return;
  tangos_frame(surface.data, width, height, 15, Color(251, 255, 246).value,
               Color(94, 194, 46).value);
  auto saved = SaveDC(surface.dc);
  auto clip = CreateRoundRectRgn(0, 0, width, height, 30, 30);
  SelectClipRgn(surface.dc, clip);
  for (int row = 2; row < 35; ++row) {
    double t = double(row - 2) / 32;
    auto brush = CreateSolidBrush(
        RGB(int(134 + (94 - 134) * t), int(224 + (194 - 224) * t), int(90 + (46 - 90) * t)));
    RECT line{2, row, width - 2, row + 1};
    FillRect(surface.dc, &line, brush);
    DeleteObject(brush);
  }
  RestoreDC(surface.dc, saved);
  DeleteObject(clip);
}
void tourPanel(HDC dc, int x, int y, int width, int height) {
  Surface surface(dc, x, y, width, height);
  if (surface.data)
    tangos_frame(surface.data, width, height, 14, Color(251, 255, 246).value,
                 Color(94, 194, 46).value);
}
void tourShade(HDC dc, int width, int height, const RECT *spot) {
  auto saved = SaveDC(dc);
  HRGN outside = nullptr, hole = nullptr;
  if (spot) {
    outside = CreateRectRgn(0, 0, width, height);
    hole = CreateRoundRectRgn(spot->left, spot->top, spot->right, spot->bottom, 24, 24);
    CombineRgn(outside, outside, hole, RGN_DIFF);
    SelectClipRgn(dc, outside);
  }
  shape(dc, 0, 0, width, height, 0, Color(140, 10, 20, 30), Color(140, 10, 20, 30));
  RestoreDC(dc, saved);
  if (outside)
    DeleteObject(outside);
  if (hole)
    DeleteObject(hole);
  if (spot && spot->right > spot->left && spot->bottom > spot->top) {
    Surface border(dc, spot->left, spot->top, spot->right - spot->left, spot->bottom - spot->top);
    if (border.data)
      tangos_frame(border.data, border.w, border.h, 12, 0, Color(230, 255, 255, 255).value);
  }
}
namespace {
struct RichPart {
  std::wstring text;
  bool joke;
  int width = 0, x = 0, y = 0;
};
struct RichLayout {
  std::vector<RichPart> parts;
  int height = 0, lineHeight = 0;
};
RichLayout richLayout(HDC dc, const std::wstring &value, int width, int size, bool bold) {
  RichLayout layout;
  auto prior = SelectObject(dc, uiFont(size, bold));
  TEXTMETRICW metrics{};
  GetTextMetricsW(dc, &metrics);
  layout.lineHeight = metrics.tmHeight + metrics.tmExternalLeading;
  SIZE space{};
  GetTextExtentPoint32W(dc, L" ", 1, &space);
  std::vector<std::vector<RichPart>> words;
  std::vector<RichPart> word;
  for (const auto &run : lite::richTextRuns(lite::utf8(value))) {
    auto text = lite::wide(run.at("text").get<std::string>());
    bool joke = run.at("joke").get<bool>();
    for (auto character : text) {
      if (character == L' ' || character == L'\n' || character == L'\r' || character == L'\t' ||
          character == L'\f') {
        if (!word.empty()) {
          words.push_back(std::move(word));
          word.clear();
        }
      } else {
        if (word.empty() || word.back().joke != joke)
          word.push_back({L"", joke});
        word.back().text += character;
      }
    }
  }
  if (!word.empty())
    words.push_back(std::move(word));
  int x = 0, y = 0;
  for (auto &pieces : words) {
    int wordWidth = 0;
    for (auto &part : pieces) {
      SelectObject(dc, uiFont(size, part.joke ? false : bold, 0, part.joke));
      SIZE bounds{};
      GetTextExtentPoint32W(dc, part.text.c_str(), int(part.text.size()), &bounds);
      part.width = bounds.cx;
      wordWidth += part.width;
    }
    if (x && x + space.cx + wordWidth > width) {
      x = 0;
      y += layout.lineHeight;
    } else if (x)
      x += space.cx;
    for (auto &part : pieces) {
      part.x = x;
      part.y = y;
      x += part.width;
      layout.parts.push_back(std::move(part));
    }
  }
  layout.height = layout.parts.empty() ? 0 : y + layout.lineHeight;
  SelectObject(dc, prior);
  return layout;
}
void gradientText(HDC dc, const RichPart &part, int x, int y, int size, int height, int available) {
  if (part.width <= 0 || available <= 0)
    return;
  Surface mask(dc, x - 1, y, std::min(part.width, available) + 4, height);
  if (!mask.data)
    return;
  std::vector<unsigned char> background(mask.data, mask.data + mask.w * mask.h * 4);
  PatBlt(mask.dc, 0, 0, mask.w, mask.h, BLACKNESS);
  auto prior = SelectObject(mask.dc, uiFont(size, false, 0, true));
  SetTextColor(mask.dc, RGB(255, 255, 255));
  SetBkMode(mask.dc, TRANSPARENT);
  int fitted = 0;
  SIZE extent{};
  GetTextExtentExPointW(mask.dc, part.text.c_str(), int(std::min<size_t>(8192, part.text.size())),
                        mask.w, &fitted, nullptr, &extent);
  TextOutW(mask.dc, 1, 0, part.text.c_str(), int(std::min(part.text.size(), size_t(fitted + 1))));
  SelectObject(mask.dc, prior);
  GdiFlush();
  static const Color accents[] = {Color(127, 196, 0), Color(255, 179, 71), Color(45, 224, 138),
                                  Color(129, 140, 248), Color(230, 210, 0)};
  for (int column = 0; column < mask.w; ++column) {
    double u = std::fmod(
        column / double(2 * std::max(1, part.width)) + (animationEnabled() ? phase / 3.2 : 0), 1.0);
    double blend = u < .5 ? 2 * u : 2 * (1 - u);
    auto first = colors.primary, second = accents[paletteIndex];
    int tint[] = {int(first.GetB() + (second.GetB() - first.GetB()) * blend),
                  int(first.GetG() + (second.GetG() - first.GetG()) * blend),
                  int(first.GetR() + (second.GetR() - first.GetR()) * blend)};
    for (int row = 0; row < mask.h; ++row) {
      size_t pixel = (size_t(row) * mask.w + column) * 4;
      for (int channel = 0; channel < 3; ++channel) {
        int alpha = mask.data[pixel + channel];
        mask.data[pixel + channel] =
            (tint[channel] * alpha + background[pixel + channel] * (255 - alpha) + 127) / 255;
      }
      mask.data[pixel + 3] = 255;
    }
  }
}
} // namespace
void wrappedLabel(HDC dc, const std::wstring &value, int x, int y, int width, int height, int size,
                  bool bold, COLORREF tint) {
  auto previous = SelectObject(dc, uiFont(size, bold));
  auto saved = SaveDC(dc);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, tint);
  IntersectClipRect(dc, x, y, x + width, y + height);
  if (value.find(L":joke[") != std::wstring::npos) {
    auto layout = richLayout(dc, value, width, size, bold);
    for (const auto &part : layout.parts) {
      if (part.joke)
        gradientText(dc, part, x + part.x, y + part.y, size, layout.lineHeight, width - part.x);
      else {
        SelectObject(dc, uiFont(size, bold));
        TextOutW(dc, x + part.x, y + part.y, part.text.c_str(), int(part.text.size()));
      }
    }
    RestoreDC(dc, saved);
    SelectObject(dc, previous);
    return;
  }
  RECT bounds{x, y, x + width, y + height};
  DrawTextW(dc, value.c_str(), int(value.size()), &bounds, DT_WORDBREAK | DT_NOPREFIX);
  RestoreDC(dc, saved);
  SelectObject(dc, previous);
}
void invalidateBackdrop(HWND parent) {
  auto found = buttonBackdrops.find(parent);
  if (found != buttonBackdrops.end())
    found->second->rendered = 0;
}
int wrappedLabelHeight(HDC dc, const std::wstring &value, int width, int size, bool bold) {
  if (value.find(L":joke[") != std::wstring::npos)
    return richLayout(dc, value, width, size, bold).height;
  auto previous = SelectObject(dc, uiFont(size, bold));
  RECT bounds{0, 0, std::max(1, width), 0};
  DrawTextW(dc, value.c_str(), int(value.size()), &bounds,
            DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
  SelectObject(dc, previous);
  return bounds.bottom;
}
void compactCombo(HWND window, bool needsRole) {
  SetPropW(window, L"TangOSCompactCombo", reinterpret_cast<HANDLE>(1));
  if (needsRole)
    SetPropW(window, L"TangOSNeedsRole", reinterpret_cast<HANDLE>(1));
  SendMessageW(window, CB_SETITEMHEIGHT, WPARAM(-1), 20);
  RECT bounds{};
  GetClientRect(window, &bounds);
  auto region = CreateRoundRectRgn(0, 0, bounds.right + 1, bounds.bottom + 1, 16, 16);
  if (!SetWindowRgn(window, region, TRUE))
    DeleteObject(region);
  SetWindowSubclass(window, compactComboProc, 3, 0);
}
COLORREF compactField() {
  static const COLORREF colors[] = {RGB(234, 244, 253), RGB(250, 208, 172), RGB(20, 44, 70),
                                    RGB(252, 214, 240), RGB(238, 255, 196)};
  return colors[paletteIndex];
}
void compactEdit(HWND window) {
  SetPropW(window, L"TangOSCompactEdit", reinterpret_cast<HANDLE>(1));
  SetWindowSubclass(window, compactEditProc, 4, 0);
  SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) | WS_BORDER);
  SetWindowPos(window, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  RECT bounds{};
  GetWindowRect(window, &bounds);
  auto region = CreateRoundRectRgn(0, 0, bounds.right - bounds.left + 1,
                                   bounds.bottom - bounds.top + 1, 16, 16);
  if (!SetWindowRgn(window, region, TRUE))
    DeleteObject(region);
}
void iconButton(HWND window, Icon icon) {
  SetPropW(window, L"TangOSIcon", (HANDLE)(uintptr_t(unsigned(icon) + 1)));
  if (iconTooltips.count(window)) {
    InvalidateRect(window, nullptr, FALSE);
    return;
  }
  SetWindowSubclass(window, hoverButton, 1, 0);
  auto tooltip = std::make_unique<IconTooltip>();
  tooltip->text.resize(GetWindowTextLengthW(window) + 1);
  GetWindowTextW(window, tooltip->text.data(), int(tooltip->text.size()));
  tooltip->window = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, GetParent(window),
                                    nullptr, GetModuleHandleW(nullptr), nullptr);
  TOOLINFOW info{sizeof(info)};
  info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  info.hwnd = GetParent(window);
  info.uId = reinterpret_cast<UINT_PTR>(window);
  info.lpszText = tooltip->text.data();
  SendMessageW(tooltip->window, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
  iconTooltips[window] = std::move(tooltip);
}
void iconTextButton(HWND window, Icon icon) {
  iconButton(window, icon);
  SetPropW(window, L"TangOSIconText", reinterpret_cast<HANDLE>(1));
}
void policyButton(HWND window, unsigned state) {
  SetPropW(window, L"TangOSPolicy", reinterpret_cast<HANDLE>(uintptr_t(state + 1)));
  buttonFont(window, 12);
}
void roleChip(HWND window) {
  SetPropW(window, L"TangOSRoleChip", reinterpret_cast<HANDLE>(1));
  buttonFont(window, 11, 700);
}
void rule(HDC dc, int x, int y, int width) {
  static const COLORREF values[] = {RGB(255, 255, 255), RGB(255, 214, 182), RGB(120, 190, 230),
                                    RGB(255, 210, 238), RGB(214, 238, 168)};
  auto pen = CreatePen(PS_SOLID, 1, values[paletteIndex]);
  auto previous = SelectObject(dc, pen);
  MoveToEx(dc, x, y, nullptr);
  LineTo(dc, x + width, y);
  SelectObject(dc, previous);
  DeleteObject(pen);
}
void drawIcon(HDC dc, Icon icon, int x, int y, int size) {
  Surface surface(dc, x, y, size, size);
  if (surface.data)
    tangos_icon(surface.data, size, size, unsigned(icon), colors.ink.value);
}
void buttonFont(HWND window, int size, int weight) {
  SetPropW(window, L"TangOSFontSize", reinterpret_cast<HANDLE>(uintptr_t(std::clamp(size, 8, 24))));
  SetPropW(window, L"TangOSFontWeight",
           reinterpret_cast<HANDLE>(uintptr_t(std::clamp(weight, 100, 900))));
}
void controlFont(HWND window, int size, int weight) {
  SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont(size, false, weight)), TRUE);
}
int textWidth(HDC dc, const std::wstring &text, int size, int weight) {
  auto previous = SelectObject(dc, uiFont(size, false, weight));
  SIZE extent{};
  GetTextExtentPoint32W(dc, text.c_str(), int(text.size()), &extent);
  SelectObject(dc, previous);
  return extent.cx;
}
void badge(HDC dc, const std::wstring &text, int x, int y, int width, int height) {
  {
    Surface surface(dc, x, y, width, height);
    if (surface.data)
      tangos_frame(
          surface.data, width, height, height / 2.f,
          Color(36, colors.primary.GetR(), colors.primary.GetG(), colors.primary.GetB()).value,
          Color(77, colors.primary.GetR(), colors.primary.GetG(), colors.primary.GetB()).value);
  }
  auto previous = SelectObject(dc, uiFont(9, true, 800));
  SetTextColor(dc, rgb(colors.primary));
  SetBkMode(dc, TRANSPARENT);
  RECT bounds{x, y, x + width, y + height};
  DrawTextW(dc, text.c_str(), int(text.size()), &bounds,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
  SelectObject(dc, previous);
}
void button(const DRAWITEMSTRUCT &i, bool primary, bool danger) {
  buttonBackground(i);
  int x = i.rcItem.left + 1, y = i.rcItem.top + 1, w = i.rcItem.right - i.rcItem.left - 2,
      h = i.rcItem.bottom - i.rcItem.top - 2;
  Color base = danger ? Color(220, 76, 70) : primary ? colors.primary : colors.field;
  bool hover = GetPropW(i.hwndItem, L"TangOSHover") != nullptr;
  auto icon = uintptr_t(GetPropW(i.hwndItem, L"TangOSIcon"));
  bool iconText = GetPropW(i.hwndItem, L"TangOSIconText") != nullptr;
  auto policy = uintptr_t(GetPropW(i.hwndItem, L"TangOSPolicy"));
  danger = danger || (hover && icon == unsigned(Icon::close) + 1);
  if (danger)
    base = Color(225, 29, 72);
  bool stopControl = danger && iconText && icon == unsigned(Icon::stop) + 1;
  bool stopping = stopControl && (i.itemState & ODS_DISABLED);
  if (stopControl)
    base = stopping ? Color(176, 69, 63) : Color(239, 83, 80);
  bool flat = icon >= unsigned(Icon::minimize) + 1 && icon <= unsigned(Icon::close) + 1;
  if (policy) {
    static const Color gloss[] = {Color(234, 244, 253), Color(250, 208, 172), Color(20, 44, 70),
                                  Color(252, 214, 240), Color(238, 255, 196)};
    auto top = gloss[paletteIndex];
    auto bottom = policy == 3 ? Color(234, 179, 8) : policy == 2 ? colors.primary : top;
    auto border = policy == 1 ? Color(255, 255, 255) : bottom;
    Surface surface(i.hDC, x, y, w, h);
    if (surface.data)
      tangos_gradient_frame(
          surface.data, w, h, h / 2.f,
          Color(hover         ? 160
                : policy == 1 ? 128
                              : 115,
                top.GetR(), top.GetG(), top.GetB())
              .value,
          Color(policy == 1 ? 36 : 77, bottom.GetR(), bottom.GetG(), bottom.GetB()).value,
          Color(policy == 1 ? 217 : 153, border.GetR(), border.GetG(), border.GetB()).value);
  } else if (GetPropW(i.hwndItem, L"TangOSRoleChip")) {
    Surface surface(i.hDC, x, y, w, h);
    if (surface.data)
      tangos_frame(surface.data, w, h, h / 2.f, Color(hover ? 55 : 36, 59, 130, 246).value,
                   Color(102, 59, 130, 246).value);
  } else if (stopControl) {
    Surface surface(i.hDC, x, y, w, h);
    if (surface.data) {
      auto fill = Color(stopping ? 230 : 255, base.GetR(), base.GetG(), base.GetB());
      tangos_frame(surface.data, w, h, h / 2.f, fill.value, fill.value);
    }
  } else if ((i.itemState & ODS_DISABLED) && primary) {
    Surface surface(i.hDC, x, y, w, h);
    if (surface.data)
      tangos_frame(surface.data, w, h, h / 2.f,
                   Color(26, base.GetR(), base.GetG(), base.GetB()).value,
                   Color(56, base.GetR(), base.GetG(), base.GetB()).value);
  } else if (!flat || hover || (i.itemState & ODS_SELECTED)) {
    int alpha = primary || danger ? 255 : hover ? 140 : 80;
    shape(i.hDC, x, y, w, h, flat ? 8 : h / 2.f,
          Color(alpha, std::min(255, base.GetR() + 25), std::min(255, base.GetG() + 20),
                std::min(255, base.GetB() + 15)),
          Color(primary || danger ? 245 : 30, base.GetR(), base.GetG(), base.GetB()));
  }
  wchar_t title[256];
  GetWindowTextW(i.hwndItem, title, 256);
  if (icon && !iconText) {
    int size = 15;
    Surface surface(i.hDC, x + (w - size) / 2, y + (h - size) / 2, size, size);
    if (surface.data)
      tangos_icon(surface.data, size, size, unsigned(icon - 1),
                  (i.itemState & ODS_DISABLED) ? colors.muted.value
                  : danger || primary          ? 0xffffffff
                                               : colors.ink.value);
    if (i.itemState & ODS_FOCUS) {
      RECT focus{x + 3, y + 3, x + w - 3, y + h - 3};
      DrawFocusRect(i.hDC, &focus);
    }
    return;
  }
  auto fontSize = uintptr_t(GetPropW(i.hwndItem, L"TangOSFontSize"));
  auto fontWeight = uintptr_t(GetPropW(i.hwndItem, L"TangOSFontWeight"));
  auto font = uiFont(fontSize ? int(fontSize) : 13, true, int(fontWeight));
  auto prev = SelectObject(i.hDC, font);
  SetTextColor(i.hDC, (i.itemState & ODS_DISABLED) && !stopControl ? rgb(colors.muted)
                      : (primary || danger)                        ? RGB(255, 255, 255)
                                                                   : rgb(colors.ink));
  SetBkMode(i.hDC, TRANSPARENT);
  RECT r{x + 4, y, w + x - 4, y + h};
  if (icon && iconText) {
    auto textSize =
        textWidth(i.hDC, title, fontSize ? int(fontSize) : 13, fontWeight ? int(fontWeight) : 700);
    int left = x + std::max(4, (w - textSize - 21) / 2);
    Surface surface(i.hDC, left, y + (h - 14) / 2, 14, 14);
    if (surface.data)
      tangos_icon(surface.data, 14, 14, unsigned(icon - 1),
                  (i.itemState & ODS_DISABLED) && !stopControl ? colors.muted.value
                  : primary || danger                          ? 0xffffffff
                                                               : colors.ink.value);
    r.left = left + 21;
  }
  DrawTextW(i.hDC, title, -1, &r,
            DT_SINGLELINE | (iconText ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_END_ELLIPSIS |
                DT_NOPREFIX);
  if (i.itemState & ODS_FOCUS) {
    InflateRect(&r, -3, -3);
    DrawFocusRect(i.hDC, &r);
  }
  SelectObject(i.hDC, prev);
}
void mascot(HDC dc, int x, int y, int size, const std::string &emotion) {
  int id = emotion == "smile"      ? 214
           : emotion == "thinking" ? 215
           : emotion == "shy"      ? 216
           : emotion == "tongue"   ? 217
           : emotion == "handsup"  ? 218
                                   : 201;
  auto r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
  if (!r)
    return;
  int height = (size * 673 + 310) / 620; // Bundled frames share the original aspect ratio.
  Surface s(dc, x, y, size, height);
  if (s.data)
    tangos_image(s.data, size, height,
                 (const unsigned char *)LockResource(LoadResource(nullptr, r)),
                 SizeofResource(nullptr, r));
}
} // namespace skin
