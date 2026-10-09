#include "helper_overlay.h"
#include "window_capture.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace lite {
namespace {
Json preferences(const fs::path &data) {
  auto path = data / "console-ui.json";
  auto result = fs::exists(path) ? Json::parse(read(path), nullptr, false) : Json::object();
  return result.is_object() ? result : Json::object();
}
} // namespace
struct HelperOverlay::Impl {
  HWND parent, backdropWindow, window = nullptr;
  fs::path data;
  HelperState state;
  ULONGLONG bounce = 0;
  ULONGLONG lastRefresh = 0;
  int x = 0, y = 0;
  int boxHeight = 204;
  int titleHeight = 19, bodyHeight = 0, bodyScroll = 0;
  bool desiredVisible = false;
  Impl(HWND owner, HWND backdrop, fs::path directory, bool)
      : parent(owner), backdropWindow(backdrop), data(std::move(directory)),
        state(preferences(data), readGuide(data, false), currentAnnouncement(), false) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TangOSLiteHelper";
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    RegisterClassW(&wc);
    window = CreateWindowExW(0, wc.lpszClassName, L"Tango tips", WS_CHILD | WS_TABSTOP, 0, 0, 232,
                             443, parent, nullptr, wc.hInstance, this);
    if (!window)
      throw std::runtime_error("Cannot create Tango helper");
    region();
  }
  ~Impl() {
    if (window)
      DestroyWindow(window);
  }
  void region() {
    auto shape = CreateRectRgn(42, 270, 190, 443);
    if (state.open) {
      auto messages = state.messages();
      auto row = messages.empty() ? Json::object() : messages.at(state.index);
      auto dc = GetDC(window);
      titleHeight = std::max(
          19, skin::wrappedLabelHeight(dc, wide(row.value("title", std::string())), 180, 14, true));
      bodyHeight =
          skin::wrappedLabelHeight(dc, wide(row.value("body", std::string())), 180, 13, false);
      ReleaseDC(window, dc);
      boxHeight = std::min(276, 38 + std::max(132, titleHeight + 4 + bodyHeight + 24) +
                                    (messages.size() > 1 ? 34 : 0));
      auto box = CreateRoundRectRgn(10, 276 - boxHeight, 222, 276, 30, 30);
      CombineRgn(shape, shape, box, RGN_OR);
      DeleteObject(box);
    }
    if (!SetWindowRgn(window, shape, TRUE))
      DeleteObject(shape);
  }
  void change(int action) {
    auto prefs = preferences(data);
    if (action == 0) {
      state.toggle(prefs);
      bounce = GetTickCount64();
    }
    if (action == 1)
      state.close(prefs);
    if (action == 2)
      state.next(-1);
    if (action == 3)
      state.next(1);
    if (action == 2 || action == 3)
      bodyScroll = 0;
    write(data / "console-ui.json", prefs.dump(2));
    region();
    InvalidateRect(window, nullptr, FALSE);
  }
  void paint(HDC dc) {
    RECT bounds{};
    GetClientRect(parent, &bounds);
    auto backdrop = CreateCompatibleDC(dc);
    auto bitmap =
        CreateCompatibleBitmap(dc, std::max(1L, bounds.right), std::max(1L, bounds.bottom));
    auto previousBitmap = SelectObject(backdrop, bitmap);
    renderWindowTree(parent, backdrop, window);
    BitBlt(dc, 0, 0, 232, 443, backdrop, x, y, SRCCOPY);
    SelectObject(backdrop, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(backdrop);
    if (state.open) {
      int top = 276 - boxHeight;
      skin::helperPanel(dc, 10, top, 212, boxHeight);
      auto messages = state.messages();
      auto row = messages.empty() ? Json::object() : messages.at(state.index);
      skin::label(dc, L"Tango says,", 21, top + 8, 114, 22, 13, true, false, false,
                  RGB(20, 52, 10));
      skin::label(dc,
                  wide(std::to_string(messages.empty() ? 0 : state.index + 1) + "/" +
                       std::to_string(messages.size())),
                  145, top + 9, 42, 20, 11, true);
      skin::label(dc, L"×", 193, top + 6, 22, 22, 18, true);
      skin::wrappedLabel(dc, wide(row.value("title", std::string("Loading…"))), 26, top + 48, 180,
                         titleHeight, 14, true, RGB(31, 61, 16));
      int bodyTop = top + 48 + titleHeight + 4;
      int bodyBottom = 276 - (messages.size() > 1 ? 34 : 0) - 12;
      auto clip = SaveDC(dc);
      IntersectClipRect(dc, 26, bodyTop, 206, bodyBottom);
      skin::wrappedLabel(dc, wide(row.value("body", std::string())), 26, bodyTop - bodyScroll, 180,
                         std::max(bodyHeight, bodyBottom - bodyTop), 13, false, RGB(44, 61, 34));
      RestoreDC(dc, clip);
      if (messages.size() > 1) {
        skin::label(dc, L"‹", 24, 240, 26, 24, 20, true, false, false, RGB(79, 174, 46));
        skin::label(dc, L"›", 184, 240, 26, 24, 20, true, false, false, RGB(79, 174, 46));
        int dotsWidth = int(messages.size()) * 11 - 5;
        if (dotsWidth <= 130) {
          for (size_t i = 0; i < messages.size(); ++i) {
            auto brush = CreateSolidBrush(i == state.index ? RGB(94, 194, 46) : RGB(210, 218, 205));
            auto previous = SelectObject(dc, brush);
            auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
            int dx = 116 - dotsWidth / 2 + int(i) * 11;
            Ellipse(dc, dx, 251, dx + 6, 257);
            SelectObject(dc, pen);
            SelectObject(dc, previous);
            DeleteObject(brush);
          }
        }
      }
    }
    auto elapsed = GetTickCount64() - bounce;
    int rise = bounce && elapsed < 450 && skin::animationEnabled()
                   ? int(std::sin(double(elapsed) / 450 * 3.141592653589793) * 12)
                   : 0;
    skin::mascot(dc, 42, 282 - rise, 148,
                 state.open     ? "handsup"
                 : state.unread ? "thinking"
                                : "idle");
    if (!state.open && state.unread) {
      auto brush = CreateSolidBrush(RGB(255, 77, 79));
      auto previous = SelectObject(dc, brush);
      auto pen = SelectObject(dc, GetStockObject(WHITE_PEN));
      Ellipse(dc, 145, 286, 160, 301);
      SelectObject(dc, pen);
      SelectObject(dc, previous);
      DeleteObject(brush);
    }
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
      if (msg == WM_LBUTTONUP) {
        SetFocus(h);
        int px = short(LOWORD(l)), py = short(HIWORD(l));
        if (py >= 276)
          self->change(0);
        else if (py < 276 - self->boxHeight + 34 && px > 189)
          self->change(1);
        else if (py >= 235 && px < 57)
          self->change(2);
        else if (py >= 235 && px > 175)
          self->change(3);
        return 0;
      }
      if (msg == WM_KEYDOWN) {
        if (w == VK_ESCAPE)
          self->change(1);
        else if (w == VK_LEFT)
          self->change(2);
        else if (w == VK_RIGHT)
          self->change(3);
        else if (w == VK_SPACE || w == VK_RETURN)
          self->change(0);
        return 0;
      }
      if (msg == WM_GETDLGCODE)
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
      if (msg == WM_MOUSEWHEEL && self->state.open) {
        int available = self->boxHeight - 48 - self->titleHeight - 4 -
                        (self->state.messages().size() > 1 ? 34 : 0) - 12;
        self->bodyScroll = std::clamp(self->bodyScroll - short(HIWORD(w)) / WHEEL_DELTA * 48, 0,
                                      std::max(0, self->bodyHeight - available));
        InvalidateRect(h, nullptr, FALSE);
        return 0;
      }
    } catch (const std::exception &error) {
      MessageBoxW(h, wide(error.what()).c_str(), L"Tango helper", MB_OK | MB_ICONERROR);
    }
    return DefWindowProcW(h, msg, w, l);
  }
};
HelperOverlay::HelperOverlay(HWND owner, HWND backdrop, fs::path data, bool viewerOnly)
    : impl(std::make_unique<Impl>(owner, backdrop, std::move(data), viewerOnly)) {}
HelperOverlay::~HelperOverlay() = default;
void HelperOverlay::position(int width, int height, bool visible) {
  RECT owner{};
  GetClientRect(impl->parent, &owner);
  width = owner.right;
  height = owner.bottom;
  impl->desiredVisible = visible;
  impl->x = std::max(0, width - 230);
  impl->y = std::max(0, height - 495);
  SetWindowPos(impl->window, HWND_TOP, impl->x, impl->y, 232, 443,
               SWP_NOACTIVATE |
                   ((visible && (GetWindowLongPtrW(impl->backdropWindow, GWL_STYLE) & WS_VISIBLE))
                        ? SWP_SHOWWINDOW
                        : SWP_HIDEWINDOW));
}
void HelperOverlay::tick() {
  bool visible =
      impl->desiredVisible && (GetWindowLongPtrW(impl->backdropWindow, GWL_STYLE) & WS_VISIBLE);
  if (bool(GetWindowLongPtrW(impl->window, GWL_STYLE) & WS_VISIBLE) != visible)
    ShowWindow(impl->window, visible ? SW_SHOW : SW_HIDE);
  auto now = GetTickCount64();
  bool bouncing = impl->bounce && now - impl->bounce < 450;
  if (IsWindowVisible(impl->window) &&
      (bouncing || (skin::animationEnabled() && now - impl->lastRefresh >= 250))) {
    impl->lastRefresh = now;
    InvalidateRect(impl->window, nullptr, FALSE);
  }
}
Json HelperOverlay::snapshot() const {
  return {{"open", impl->state.open},
          {"unread", impl->state.unread},
          {"index", impl->state.index},
          {"messages", impl->state.messages().size()},
          {"panel",
           {{"x", impl->x + 10},
            {"y", impl->y + 276 - impl->boxHeight},
            {"width", 212},
            {"height", impl->boxHeight}}},
          {"mascot", {{"x", impl->x + 42}, {"y", impl->y + 282}, {"width", 148}, {"height", 161}}}};
}
void HelperOverlay::smoke(const fs::path &directory) {
  impl->change(1);
  SendMessageW(impl->window, WM_KEYDOWN, VK_RETURN, 0);
  if (!impl->state.open || impl->state.unread)
    throw std::runtime_error("Helper keyboard toggle did not open and mark messages read");
  auto before = impl->state.index;
  SendMessageW(impl->window, WM_KEYDOWN, VK_RIGHT, 0);
  SendMessageW(impl->window, WM_KEYDOWN, VK_LEFT, 0);
  if (impl->state.index != before)
    throw std::runtime_error("Helper circular navigation failed");
  if (preferences(impl->data).value("updateNoteSeen", std::string()) !=
      currentAnnouncement().at("id"))
    throw std::runtime_error("Helper announcement read state was not persisted");
  SendMessageW(impl->window, WM_LBUTTONUP, 0, MAKELPARAM(200, 276 - impl->boxHeight + 12));
  if (impl->state.open)
    throw std::runtime_error("Helper mouse close failed");
  SendMessageW(impl->window, WM_LBUTTONUP, 0, MAKELPARAM(100, 330));
  if (!impl->state.open)
    throw std::runtime_error("Helper mascot mouse toggle failed");
  impl->state.tips.push_back(
      {{"title", "A :joke[styled] tip"},
       {"body", "Keep :joke[λ food pellets] italic and colorful while "
                "this longer message wraps inside Tango's floating panel."}});
  impl->state.index = impl->state.messages().size() - 1;
  impl->region();
  write(directory / "helper-overlay.json", snapshot().dump(2));
}
} // namespace lite
