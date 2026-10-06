#include "skin.h"
#include <objidl.h>
#include <propidl.h>
#include <algorithm>
#include <gdiplus.h>
#include <memory>
namespace skin {
using namespace Gdiplus;
namespace {
ULONG_PTR token = 0;
Image *tango = nullptr;
IStream *imageStream = nullptr;
struct Palette {
  Color top, middle, bottom, primary, ink, muted, panel, field;
};
Palette colors{Color(143, 208, 248),      Color(126, 200, 240), Color(142, 200, 65),
               Color(0, 153, 224),        Color(13, 58, 92),    Color(72, 116, 156),
               Color(200, 234, 244, 253), Color(234, 244, 253)};
std::unique_ptr<GraphicsPath> rounded(RectF r, float radius) {
  auto p = std::make_unique<GraphicsPath>();
  float d = radius * 2;
  p->AddArc(r.X, r.Y, d, d, 180, 90);
  p->AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
  p->AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
  p->AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
  p->CloseFigure();
  return p;
}
COLORREF rgb(Color c) { return RGB(c.GetR(), c.GetG(), c.GetB()); }
} // namespace
void initialize() {
  GdiplusStartupInput startup;
  GdiplusStartup(&token, &startup, nullptr);
  auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(201), RT_RCDATA);
  if (resource) {
    auto loaded = LoadResource(nullptr, resource);
    auto size = SizeofResource(nullptr, resource);
    auto data = LockResource(loaded);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (memory) {
      auto ptr = GlobalLock(memory);
      CopyMemory(ptr, data, size);
      GlobalUnlock(memory);
      if (SUCCEEDED(CreateStreamOnHGlobal(memory, TRUE, &imageStream)))
        tango = Image::FromStream(imageStream);
      else
        GlobalFree(memory);
    }
  }
}
void shutdown() {
  delete tango;
  if (imageStream)
    imageStream->Release();
  GdiplusShutdown(token);
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
  Graphics g(dc);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  LinearGradientBrush gradient(Point(0, 0), Point(0, h), colors.top, colors.bottom);
  Color stops[] = {colors.top, colors.middle,
                   Color(colors.bottom.GetR(), colors.bottom.GetG(), colors.bottom.GetB()),
                   colors.bottom};
  REAL positions[] = {0.0f, 0.58f, 0.84f, 1.0f};
  gradient.SetInterpolationColors(stops, positions, 4);
  g.FillRectangle(&gradient, 0, 0, w, h);
  GraphicsPath blob;
  blob.AddEllipse((REAL)(w * 0.1), (REAL)(-h * 0.55), (REAL)(w * 1.1), (REAL)(h * 1.1));
  PathGradientBrush glow(&blob);
  glow.SetCenterColor(Color(105, 255, 255, 255));
  Color edge(0, 255, 255, 255);
  int count = 1;
  glow.SetSurroundColors(&edge, &count);
  g.FillPath(&glow, &blob);
  SolidBrush top(Color(42, 234, 244, 253));
  g.FillRectangle(&top, 0, 0, w, 52);
  Pen rim(Color(120, 255, 255, 255));
  g.DrawLine(&rim, 0, 52, w, 52);
}
void panel(HDC dc, int x, int y, int w, int h, bool solid) {
  if (w < 1 || h < 1)
    return;
  Graphics g(dc);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  auto shadow = rounded(RectF((REAL)x, (REAL)(y + 4), (REAL)w, (REAL)h), 14);
  SolidBrush shade(Color(20, 0, 0, 0));
  g.FillPath(&shade, shadow.get());
  auto path = rounded(RectF((REAL)x, (REAL)y, (REAL)w, (REAL)h), 14);
  Color bottom = colors.panel;
  if (solid)
    bottom.SetValue(
        Color(244, colors.field.GetR(), colors.field.GetG(), colors.field.GetB()).GetValue());
  LinearGradientBrush fill(
      Point(x, y), Point(x, y + h),
      Color(solid ? 244 : 110, colors.field.GetR(), colors.field.GetG(), colors.field.GetB()),
      bottom);
  g.FillPath(&fill, path.get());
  Pen border(Color(solid ? 120 : 65, colors.field.GetR(), colors.field.GetG(), colors.field.GetB()),
             1);
  g.DrawPath(&border, path.get());
}
void label(HDC dc, const std::wstring &s, int x, int y, int w, int h, int size, bool bold,
           bool secondary, bool accent) {
  Graphics g(dc);
  g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
  FontFamily family(L"Segoe UI");
  Font font(&family, (REAL)size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
  SolidBrush brush(accent ? colors.primary : secondary ? colors.muted : colors.ink);
  StringFormat format;
  format.SetTrimming(StringTrimmingEllipsisCharacter);
  g.DrawString(s.c_str(), (int)s.size(), &font, RectF((REAL)x, (REAL)y, (REAL)w, (REAL)h), &format,
               &brush);
}
void button(const DRAWITEMSTRUCT &i, bool primary, bool danger) {
  Graphics g(i.hDC);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  RectF r((REAL)i.rcItem.left + 1, (REAL)i.rcItem.top + 1,
          (REAL)(i.rcItem.right - i.rcItem.left) - 2, (REAL)(i.rcItem.bottom - i.rcItem.top) - 2);
  auto path = rounded(r, std::min(10.0f, r.Height / 2));
  bool disabled = (i.itemState & ODS_DISABLED) != 0;
  bool down = (i.itemState & ODS_SELECTED) != 0;
  Color base = danger ? Color(220, 76, 70) : primary ? colors.primary : colors.field;
  Color a = primary || danger
                ? Color(disabled ? 135 : 255, (BYTE)std::min(255, (int)base.GetR() + 35),
                        (BYTE)std::min(255, (int)base.GetG() + 25),
                        (BYTE)std::min(255, (int)base.GetB() + 15))
                : Color(down ? 245 : 210, base.GetR(), base.GetG(), base.GetB());
  Color b = primary || danger ? base : Color(135, base.GetR(), base.GetG(), base.GetB());
  LinearGradientBrush fill(PointF(r.X, r.Y), PointF(r.X, r.GetBottom()), a, b);
  g.FillPath(&fill, path.get());
  Pen border(Color(140, base.GetR(), base.GetG(), base.GetB()));
  g.DrawPath(&border, path.get());
  wchar_t text[256];
  GetWindowTextW(i.hwndItem, text, 256);
  FontFamily family(L"Segoe UI");
  Font font(&family, 13, FontStyleBold, UnitPixel);
  SolidBrush ink(disabled ? colors.muted : (primary || danger ? Color(255, 255, 255) : colors.ink));
  StringFormat format;
  format.SetAlignment(StringAlignmentCenter);
  format.SetLineAlignment(StringAlignmentCenter);
  format.SetTrimming(StringTrimmingEllipsisCharacter);
  g.DrawString(text, -1, &font, r, &format, &ink);
  if (i.itemState & ODS_FOCUS) {
    Pen focus(colors.primary);
    auto rect = r;
    rect.Inflate(-3, -3);
    auto ring = rounded(rect, 7);
    g.DrawPath(&focus, ring.get());
  }
}
void mascot(HDC dc, int x, int y, int size) {
  if (tango && tango->GetLastStatus() == Ok) {
    Graphics g(dc);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(tango, x, y, size, size);
  }
}
} // namespace skin
