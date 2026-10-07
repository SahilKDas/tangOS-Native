#include "skin.h"
#include <algorithm>
#include <cstdint>
extern "C" void tangos_shape(unsigned char *, unsigned, unsigned, float, uint32_t, uint32_t);
extern "C" void tangos_image(unsigned char *, unsigned, unsigned, const unsigned char *, size_t);
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
HANDLE fontResource = nullptr;
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
void shape(HDC dc, int x, int y, int w, int h, float radius, Color top, Color bottom) {
  if (w <= 0 || h <= 0)
    return;
  Surface s(dc, x, y, w, h);
  if (s.data)
    tangos_shape(s.data, w, h, radius, top.value, bottom.value);
}
} // namespace
void initialize() {
  auto r = FindResourceW(nullptr, MAKEINTRESOURCEW(206), RT_RCDATA);
  if (r) {
    DWORD count = 0;
    fontResource = AddFontMemResourceEx(LockResource(LoadResource(nullptr, r)),
                                        SizeofResource(nullptr, r), nullptr, &count);
  }
}
void shutdown() {
  if (fontResource)
    RemoveFontMemResourceEx(fontResource);
}
void theme(int i) {
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
void background(HDC dc, int w, int h) {
  int split = h * 58 / 100;
  shape(dc, 0, 0, w, split, 0, colors.top, colors.middle);
  shape(dc, 0, split, w, h - split, 0, colors.middle, colors.bottom);
  shape(dc, 0, 0, w, 52, 0, Color(42, 234, 244, 253), Color(42, 234, 244, 253));
}
void panel(HDC dc, int x, int y, int w, int h, bool solid) {
  shape(dc, x, y + 4, w, h, 14, Color(20, 0, 0, 0), Color(20, 0, 0, 0));
  shape(dc, x, y, w, h, 14,
        Color(solid ? 244 : 110, colors.field.GetR(), colors.field.GetG(), colors.field.GetB()),
        solid ? Color(244, colors.field.GetR(), colors.field.GetG(), colors.field.GetB())
              : colors.panel);
}
void label(HDC dc, const std::wstring &s, int x, int y, int w, int h, int size, bool bold,
           bool secondary, bool accent) {
  auto font = CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Nunito");
  auto prev = SelectObject(dc, font);
  SetTextColor(dc, rgb(accent ? colors.primary : secondary ? colors.muted : colors.ink));
  SetBkMode(dc, TRANSPARENT);
  RECT r{x, y, x + w, y + h};
  DrawTextW(dc, s.c_str(), (int)s.size(), &r, DT_NOPREFIX | DT_END_ELLIPSIS);
  SelectObject(dc, prev);
  DeleteObject(font);
}
void button(const DRAWITEMSTRUCT &i, bool primary, bool danger) {
  int x = i.rcItem.left + 1, y = i.rcItem.top + 1, w = i.rcItem.right - i.rcItem.left - 2,
      h = i.rcItem.bottom - i.rcItem.top - 2;
  Color base = danger ? Color(220, 76, 70) : primary ? colors.primary : colors.field;
  shape(i.hDC, x, y, w, h, 10,
        Color(245, std::min(255, base.GetR() + 25), std::min(255, base.GetG() + 20),
              std::min(255, base.GetB() + 15)),
        base);
  wchar_t title[256];
  GetWindowTextW(i.hwndItem, title, 256);
  auto font = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, 0, L"Nunito");
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
  DeleteObject(font);
}
void mascot(HDC dc, int x, int y, int size) {
  auto r = FindResourceW(nullptr, MAKEINTRESOURCEW(201), RT_RCDATA);
  if (!r)
    return;
  Surface s(dc, x, y, size, size);
  if (s.data)
    tangos_image(s.data, size, size, (const unsigned char *)LockResource(LoadResource(nullptr, r)),
                 SizeofResource(nullptr, r));
}
} // namespace skin
