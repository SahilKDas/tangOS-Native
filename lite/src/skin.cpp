#include "skin.h"
#include <commctrl.h>
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <vector>
extern "C" void tangos_shape(unsigned char *, unsigned, unsigned, float, uint32_t, uint32_t);
extern "C" void tangos_image(unsigned char *, unsigned, unsigned, const unsigned char *, size_t);
extern "C" void tangos_mesh(unsigned char *, unsigned, unsigned, float, unsigned);
extern "C" void tangos_icon(unsigned char *, unsigned, unsigned, unsigned, uint32_t);
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
std::map<std::pair<int, bool>, HFONT> fonts;
HFONT uiFont(int size, bool bold) {
  auto key = std::make_pair(size, bold);
  auto found = fonts.find(key);
  if (found != fonts.end())
    return found->second;
  auto font = CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Nunito");
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
  if (animationEnabled() && w > 0 && h > 0) {
    Surface surface(dc, 0, 0, w, h);
    if (surface.data)
      tangos_mesh(surface.data, w, h, phase, paletteIndex);
    return;
  }
  int split = h * 58 / 100;
  shape(dc, 0, 0, w, split, 0, colors.top, colors.middle);
  shape(dc, 0, split, w, h - split, 0, colors.middle, colors.bottom);
  shape(dc, 0, 0, w, 52, 0, Color(42, 234, 244, 253), Color(42, 234, 244, 253));
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
void panel(HDC dc, int x, int y, int w, int h, bool solid) {
  shape(dc, x, y + 4, w, h, 14, Color(20, 0, 0, 0), Color(20, 0, 0, 0));
  shape(dc, x, y, w, h, 14,
        Color(solid ? 244 : 110, colors.field.GetR(), colors.field.GetG(), colors.field.GetB()),
        solid ? Color(244, colors.field.GetR(), colors.field.GetG(), colors.field.GetB())
              : colors.panel);
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
void agentCard(HDC dc, int x, int y, int w, int h, COLORREF tint) {
  shape(dc, x, y + 4, w, h, 14, Color(18, 0, 0, 0), Color(18, 0, 0, 0));
  auto edge = Color(220, GetRValue(tint), GetGValue(tint), GetBValue(tint));
  shape(dc, x, y, w, h, 14, edge, edge);
  auto fill = Color(230, colors.field.GetR(), colors.field.GetG(), colors.field.GetB());
  shape(dc, x + 1, y + 1, w - 2, h - 2, 13, fill, fill);
}
void presenceDot(HDC dc, int x, int y, const std::string &state) {
  auto tint = state == "stale"     ? RGB(230, 170, 20)
              : state == "offline" ? RGB(215, 63, 67)
                                   : RGB(30, 165, 87);
  if (state == "online live") {
    int alpha = animationEnabled() ? int(50 + 30 * std::sin(phase * 6)) : 60;
    auto glow = Color(alpha, GetRValue(tint), GetGValue(tint), GetBValue(tint));
    shape(dc, x - 4, y - 4, 20, 20, 10, glow, glow);
  }
  auto fill = Color(255, GetRValue(tint), GetGValue(tint), GetBValue(tint));
  shape(dc, x, y, 12, 12, 6, fill, fill);
}
void label(HDC dc, const std::wstring &s, int x, int y, int w, int h, int size, bool bold,
           bool secondary, bool accent, COLORREF tint) {
  auto font = uiFont(size, bold);
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
void invalidateBackdrop(HWND parent) {
  auto found = buttonBackdrops.find(parent);
  if (found != buttonBackdrops.end())
    found->second->rendered = 0;
}
void iconButton(HWND window, Icon icon) {
  SetPropW(window, L"TangOSIcon", (HANDLE)(uintptr_t(unsigned(icon) + 1)));
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
void button(const DRAWITEMSTRUCT &i, bool primary, bool danger) {
  buttonBackground(i);
  int x = i.rcItem.left + 1, y = i.rcItem.top + 1, w = i.rcItem.right - i.rcItem.left - 2,
      h = i.rcItem.bottom - i.rcItem.top - 2;
  Color base = danger ? Color(220, 76, 70) : primary ? colors.primary : colors.field;
  bool hover = GetPropW(i.hwndItem, L"TangOSHover") != nullptr;
  auto icon = uintptr_t(GetPropW(i.hwndItem, L"TangOSIcon"));
  danger = danger || (hover && icon == unsigned(Icon::close) + 1);
  if (danger)
    base = Color(225, 29, 72);
  bool flat = icon >= unsigned(Icon::minimize) + 1 && icon <= unsigned(Icon::close) + 1;
  if (!flat || hover || (i.itemState & ODS_SELECTED)) {
    int alpha = primary || danger ? 255 : hover ? 140 : 80;
    shape(i.hDC, x, y, w, h, flat ? 8 : h / 2.f,
          Color(alpha, std::min(255, base.GetR() + 25), std::min(255, base.GetG() + 20),
                std::min(255, base.GetB() + 15)),
          Color(primary || danger ? 245 : 30, base.GetR(), base.GetG(), base.GetB()));
  }
  wchar_t title[256];
  GetWindowTextW(i.hwndItem, title, 256);
  if (icon) {
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
  auto font = uiFont(13, true);
  auto prev = SelectObject(i.hDC, font);
  SetTextColor(i.hDC, (i.itemState & ODS_DISABLED) ? rgb(colors.muted)
                      : (primary || danger)        ? RGB(255, 255, 255)
                                                   : rgb(colors.ink));
  SetBkMode(i.hDC, TRANSPARENT);
  RECT r{x + 4, y, w + x - 4, y + h};
  DrawTextW(i.hDC, title, -1, &r,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
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
