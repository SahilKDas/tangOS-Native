#include "tour_overlay.h"
#include <algorithm>
#include <stdexcept>
namespace lite {
namespace {
void renderUnderlying(HWND owner, HDC dc, HWND excluded) {
  SendMessageW(owner, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
  struct Context {
    HWND owner, excluded;
    HDC dc;
  } context{owner, excluded, dc};
  EnumChildWindows(
      owner,
      [](HWND child, LPARAM value) -> BOOL {
        auto &context = *reinterpret_cast<Context *>(value);
        for (auto ancestor = child; ancestor && ancestor != context.owner;
             ancestor = GetParent(ancestor))
          if (ancestor == context.excluded ||
              !(GetWindowLongPtrW(ancestor, GWL_STYLE) & WS_VISIBLE))
            return TRUE;
        RECT bounds{};
        GetWindowRect(child, &bounds);
        MapWindowPoints(nullptr, context.owner, reinterpret_cast<POINT *>(&bounds), 2);
        auto saved = SaveDC(context.dc);
        SetViewportOrgEx(context.dc, bounds.left, bounds.top, nullptr);
        IntersectClipRect(context.dc, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
        wchar_t name[32]{};
        GetClassNameW(child, name, 32);
        bool ownerButton = std::wstring(name) == L"Button" &&
                           (GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW;
        if (ownerButton) {
          DRAWITEMSTRUCT item{};
          item.CtlType = ODT_BUTTON;
          item.CtlID = GetDlgCtrlID(child);
          item.itemAction = ODA_DRAWENTIRE;
          item.itemState = IsWindowEnabled(child) ? 0 : ODS_DISABLED;
          item.hwndItem = child;
          item.hDC = context.dc;
          item.rcItem = {0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
          SendMessageW(GetParent(child), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
        } else {
          SendMessageW(child, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(context.dc), PRF_CLIENT);
        }
        RestoreDC(context.dc, saved);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&context));
}
} // namespace
struct TourOverlay::Impl {
  HWND owner, window = nullptr, priorFocus = nullptr;
  fs::path data;
  Target target;
  Json steps;
  size_t index = 0;
  HDC backdrop = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previousBitmap = nullptr;
  int width = 0, height = 0;
  int titleHeight = 20, bodyHeight = 57, boxHeight = 149;
  bool visible = true;
  RECT box{}, back{}, next{}, skip{}, spot{};
  bool hasSpot = false;
  Impl(HWND parent, fs::path directory, Target resolver)
      : owner(parent), data(std::move(directory)), target(std::move(resolver)),
        steps(readGuide(data, true)) {
    priorFocus = GetFocus();
    capture();
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TangOSLiteTour";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    window =
        CreateWindowExW(0, wc.lpszClassName, L"Tango's tour", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,
                        0, width, height, owner, nullptr, wc.hInstance, this);
    if (!window)
      throw std::runtime_error("Cannot create Tango tour overlay");
    layout();
    SetFocus(window);
  }
  ~Impl() {
    if (window)
      DestroyWindow(window);
    clearBackdrop();
  }
  void clearBackdrop() {
    if (!backdrop)
      return;
    SelectObject(backdrop, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(backdrop);
    backdrop = nullptr;
  }
  void capture() {
    clearBackdrop();
    RECT bounds{};
    GetClientRect(owner, &bounds);
    width = std::max(1L, bounds.right);
    height = std::max(1L, bounds.bottom);
    if (window)
      ShowWindow(window, SW_HIDE);
    RedrawWindow(owner, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    auto dc = GetDC(owner);
    backdrop = CreateCompatibleDC(dc);
    bitmap = CreateCompatibleBitmap(dc, width, height);
    previousBitmap = SelectObject(backdrop, bitmap);
    renderUnderlying(owner, backdrop, window);
    ReleaseDC(owner, dc);
    if (window && visible)
      ShowWindow(window, SW_SHOW);
  }
  void layout() {
    auto dc = GetDC(owner);
    titleHeight =
        std::max(20, skin::wrappedLabelHeight(
                         dc, wide(steps.at(index).value("title", std::string())), 252, 15, true));
    bodyHeight =
        std::max(19, skin::wrappedLabelHeight(
                         dc, wide(steps.at(index).value("body", std::string())), 252, 13, false));
    ReleaseDC(owner, dc);
    boxHeight = 28 + titleHeight + 5 + bodyHeight + 10 + 29;
    const int popHeight = std::max(171, boxHeight);
    auto selector = steps.at(index).value("target", std::string());
    auto rect = selector.empty() ? std::optional<RECT>{} : target(selector);
    hasSpot = rect.has_value();
    int left = (width - 448) / 2, top = (height - popHeight) / 2;
    if (rect) {
      spot = *rect;
      InflateRect(&spot, 6, 6);
      top = height - rect->bottom > 280 ? rect->bottom + 16 : std::max(14L, rect->top - 270);
      left = std::clamp(int(rect->left + (rect->right - rect->left) / 2 - 224), 14,
                        std::max(14, width - 462));
    }
    top = std::clamp(top, 14, std::max(14, height - popHeight - 14));
    box = {left + 164, top + popHeight - boxHeight, left + 448, top + popHeight};
    back = {box.right - 172, box.bottom - 42, box.right - 98, box.bottom - 12};
    next = {box.right - 90, box.bottom - 42, box.right - 14, box.bottom - 12};
    skip = {width - 100, 16, width - 18, 46};
  }
  void close() {
    auto path = data / "console-ui.json";
    auto preferences = fs::exists(path) ? Json::parse(read(path), nullptr, false) : Json::object();
    if (!preferences.is_object())
      preferences = Json::object();
    preferences["tourSeen"] = true;
    write(path, preferences.dump(2));
    visible = false;
    ShowWindow(window, SW_HIDE);
    if (IsWindow(priorFocus) && IsWindowVisible(priorFocus))
      SetFocus(priorFocus);
    else
      SetFocus(owner);
  }
  void advance(int direction) {
    if (direction > 0 && index + 1 == steps.size()) {
      close();
      return;
    }
    if (direction < 0 && !index)
      return;
    index = direction > 0 ? index + 1 : index - 1;
    layout();
    InvalidateRect(window, nullptr, FALSE);
  }
  void paint(HDC dc) {
    BitBlt(dc, 0, 0, width, height, backdrop, 0, 0, SRCCOPY);
    skin::tourShade(dc, width, height, hasSpot ? &spot : nullptr);
    skin::panel(dc, skip.left, skip.top, skip.right - skip.left, skip.bottom - skip.top);
    skin::label(dc, L"× Skip", skip.left + 10, skip.top + 6, 65, 20, 12, true, false, false,
                RGB(255, 255, 255));
    skin::tourPanel(dc, box.left, box.top, box.right - box.left, box.bottom - box.top);
    skin::wrappedLabel(dc, wide(steps.at(index).value("title", std::string())), box.left + 16,
                       box.top + 14, 252, titleHeight, 15, true, RGB(31, 61, 16));
    skin::wrappedLabel(dc, wide(steps.at(index).value("body", std::string())), box.left + 16,
                       box.top + 14 + titleHeight + 5, 252, bodyHeight, 13, false, RGB(44, 61, 34));
    skin::mascot(dc, box.left - 164, box.bottom - 171, 158,
                 steps.at(index).value("emotion", std::string("smile")));
    for (size_t dot = 0; dot < steps.size(); ++dot) {
      auto brush = CreateSolidBrush(dot == index ? RGB(79, 174, 46) : RGB(195, 215, 182));
      auto oldBrush = SelectObject(dc, brush);
      auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
      int x = box.left + 16 + int(dot) * 10;
      Ellipse(dc, x, box.bottom - 30, x + 6, box.bottom - 24);
      SelectObject(dc, oldPen);
      SelectObject(dc, oldBrush);
      DeleteObject(brush);
    }
    if (index) {
      skin::panel(dc, back.left, back.top, back.right - back.left, 30);
      skin::label(dc, L"Back", back.left + 16, back.top + 6, 50, 20, 13, true);
    }
    skin::panel(dc, next.left, next.top, next.right - next.left, 30);
    skin::label(dc, index + 1 == steps.size() ? L"Done" : L"Next", next.left + 16, next.top + 6, 50,
                20, 13, true);
  }
  static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Impl *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      self = static_cast<Impl *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
      self->window = h;
      SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
      return DefWindowProcW(h, msg, w, l);
    try {
      if (msg == WM_ERASEBKGND)
        return 1;
      if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
        PAINTSTRUCT ps{};
        auto dc = msg == WM_PAINT ? BeginPaint(h, &ps) : reinterpret_cast<HDC>(w);
        self->paint(dc);
        if (msg == WM_PAINT)
          EndPaint(h, &ps);
        return 0;
      }
      if (msg == WM_GETDLGCODE)
        return DLGC_WANTALLKEYS;
      if (msg == WM_KEYDOWN) {
        if (w == VK_ESCAPE)
          self->close();
        if (w == VK_RIGHT || w == VK_RETURN || w == VK_SPACE)
          self->advance(1);
        if (w == VK_LEFT || w == VK_BACK)
          self->advance(-1);
        return 0;
      }
      if (msg == WM_LBUTTONUP) {
        POINT point{short(LOWORD(l)), short(HIWORD(l))};
        if (PtInRect(&self->skip, point))
          self->close();
        else if (PtInRect(&self->next, point))
          self->advance(1);
        else if (self->index && PtInRect(&self->back, point))
          self->advance(-1);
        return 0;
      }
    } catch (const std::exception &error) {
      MessageBoxW(h, wide(error.what()).c_str(), L"Tango tour", MB_OK | MB_ICONERROR);
    }
    return DefWindowProcW(h, msg, w, l);
  }
};
TourOverlay::TourOverlay(HWND owner, fs::path data, Target target)
    : impl(std::make_unique<Impl>(owner, std::move(data), std::move(target))) {}
TourOverlay::~TourOverlay() = default;
void TourOverlay::close() { impl->close(); }
bool TourOverlay::open() const { return impl->visible; }
void TourOverlay::resize() {
  if (!open())
    return;
  impl->capture();
  impl->layout();
  SetWindowPos(impl->window, HWND_TOP, 0, 0, impl->width, impl->height, SWP_NOACTIVATE);
}
Json TourOverlay::snapshot() const {
  return {{"open", open()},
          {"index", impl->index},
          {"steps", impl->steps.size()},
          {"spotlight", impl->hasSpot},
          {"target", impl->steps.at(impl->index).value("target", std::string())},
          {"panel",
           {{"x", impl->box.left},
            {"y", impl->box.top},
            {"width", impl->box.right - impl->box.left},
            {"height", impl->boxHeight}}}};
}
void TourOverlay::smoke(const fs::path &directory,
                        const std::function<void(const fs::path &)> &capture) {
  if (!open() || impl->steps.size() < 10)
    throw std::runtime_error("Native tour did not open");
  capture(directory / "tour-overlay-centered.bmp");
  SendMessageW(impl->window, WM_KEYDOWN, VK_RIGHT, 0);
  if (!impl->hasSpot || impl->index != 1)
    throw std::runtime_error("Native tour toggle spotlight missing");
  write(directory / "tour-spotlight.json", snapshot().dump(2));
  capture(directory / "tour-overlay-spotlight.bmp");
  SendMessageW(impl->window, WM_KEYDOWN, VK_LEFT, 0);
  if (impl->index != 0 || impl->hasSpot)
    throw std::runtime_error("Native tour back did not recenter");
  for (size_t step = 0; step < impl->steps.size(); ++step) {
    impl->index = step;
    impl->layout();
    if (impl->box.bottom > impl->height - 14 || impl->box.top < 14 ||
        impl->boxHeight < impl->titleHeight + impl->bodyHeight + 72)
      throw std::runtime_error("Native tour text or panel clipped");
  }
  impl->index = 0;
  impl->layout();
}
} // namespace lite
