#include "mcp_popover.h"
#include "dpi.h"
#include "popover_motion.h"
#include "skin.h"
#include <algorithm>
#include <cmath>
#include <commctrl.h>
namespace lite {
std::string mcpContactAge(int64_t now, int64_t timestamp) {
  auto seconds =
      std::max<int64_t>(0, std::llround((static_cast<long double>(now) - timestamp) / 1000.L));
  if (seconds < 60)
    return std::to_string(seconds) + "s ago";
  if (seconds < 3600)
    return std::to_string(std::llround(seconds / 60.)) + "m ago";
  return std::to_string(std::llround(seconds / 3600.)) + "h ago";
}
struct McpPopover::Impl {
  HWND owner = nullptr, window = nullptr, status = nullptr, clients = nullptr, traffic = nullptr;
  HBRUSH field = nullptr, compactField = nullptr;
  std::function<Json()> snapshot;
  std::function<void(McpPopoverAction)> action;
  Json model = Json::object();
  std::string shape, localError;
  int scroll = 0, contentHeight = 0, maximumHeight = 600;
  ULONGLONG copiedUntil = 0;
  bool performingAction = false, manualExpanded = false;
  Impl(HWND root, std::function<Json()> read, std::function<void(McpPopoverAction)> perform)
      : owner(root), snapshot(std::move(read)), action(std::move(perform)) {
    field = CreateSolidBrush(skin::field());
    compactField = CreateSolidBrush(skin::compactField());
  }
  ~Impl() {
    if (window)
      DestroyWindow(window);
    DeleteObject(field);
    DeleteObject(compactField);
  }
  HWND control(const wchar_t *kind, const std::string &title, int id, int x, int y, int width,
               int height, DWORD style = 0) {
    auto child = dpi::createWindow(
        0, kind, wide(title).c_str(), WS_CHILD | WS_VISIBLE | style, x, y - scroll, width, height,
        window, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    skin::controlFont(child, 12);
    if (std::wstring(kind) == L"EDIT" && !(style & ES_MULTILINE))
      skin::compactEdit(child);
    if (std::wstring(kind) == L"BUTTON")
      skin::buttonFont(child, 12);
    SetWindowSubclass(child, keyboard, 1, reinterpret_cast<DWORD_PTR>(this));
    return child;
  }
  LRESULT editColors(HDC dc, HWND child) {
    auto compact = GetPropW(child, L"TangOSCompactEdit") != nullptr;
    SetTextColor(dc, skin::text());
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, compact ? skin::compactField() : skin::field());
    return reinterpret_cast<LRESULT>(compact ? compactField : field);
  }
  static std::string prompt(const Json &state) {
    return state.value("promptPreview",
                       std::string("Start the server, then copy the connection prompt. Accounts "
                                   "and providers stay user-configured."));
  }
  void scrollTo(int desired) {
    RECT bounds{};
    dpi::clientRect(window, &bounds);
    desired = std::clamp(desired, 0, std::max(0, contentHeight - int(bounds.bottom)));
    if (desired == scroll)
      return;
    for (auto child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
      RECT r{};
      dpi::relativeRect(child, window, &r);
      dpi::setWindowPos(child, nullptr, r.left, r.top + scroll - desired, 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    scroll = desired;
    SetScrollPos(window, SB_VERT, scroll, TRUE);
    InvalidateRect(window, nullptr, TRUE);
  }
  void reveal(HWND child) {
    RECT bounds{}, fieldBounds{};
    dpi::clientRect(window, &bounds);
    dpi::relativeRect(child, window, &fieldBounds);
    if (fieldBounds.top < 0)
      scrollTo(scroll + fieldBounds.top - 8);
    else if (fieldBounds.bottom > bounds.bottom)
      scrollTo(scroll + fieldBounds.bottom - bounds.bottom + 8);
  }
  void build() {
    while (auto child = GetWindow(window, GW_CHILD))
      DestroyWindow(child);
    status = clients = traffic = nullptr;
    auto running = model.value("running", false), busy = model.value("busy", false);
    skin::controlFont(control(L"STATIC", "MCP server", 0, 16, 16, 294, 25), 15, 700);
    control(L"STATIC",
            "The endpoint an AI connects to. Every call it makes streams into the viewer.", 0, 16,
            46, 294, 44);
    control(L"STATIC", "Status", 0, 16, 100, 100, 22);
    status = control(L"STATIC",
                     busy      ? "changing…"
                     : running ? "running"
                               : "stopped",
                     8201, 132, 100, 178, 22, SS_RIGHT);
    int y = 130;
    if (running) {
      control(L"STATIC", "Clients", 0, 16, y, 100, 22);
      clients = control(L"STATIC", "", 8202, 132, y, 178, 22, SS_RIGHT);
      y += 28;
      control(L"STATIC", "Traffic", 0, 16, y, 80, 22);
      traffic = control(L"STATIC", "", 8203, 96, y, 214, 22, SS_RIGHT);
      y += 32;
      if (model.value("connectedClients", 0) == 0 && model.value("requestsSeen", uint64_t(0)) > 0) {
        control(L"STATIC",
                "Traffic arrived but no MCP session formed. Use the stdio fallback in the copied "
                "connection prompt if your client cannot speak Streamable HTTP.",
                0, 16, y, 294, 80);
        y += 88;
      }
      control(L"STATIC", "URL", 0, 16, y, 294, 22);
      y += 24;
      control(L"EDIT", model.value("url", std::string()), 8205, 16, y, 294, 28,
              ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP);
      y += 40;
    }
    auto error = localError.empty() ? model.value("error", std::string()) : localError;
    if (!error.empty()) {
      control(L"STATIC", error, 0, 16, y, 294, 64);
      y += 72;
    }
    auto toggle = control(L"BUTTON",
                          busy      ? "Working…"
                          : running ? "Stop"
                                    : "Start server",
                          8101, 16, y, 112, 30, BS_OWNERDRAW | WS_TABSTOP);
    EnableWindow(toggle, !busy);
    if (running) {
      auto copyUrl =
          control(L"BUTTON", "Copy URL", 8106, 136, y, 100, 30, BS_OWNERDRAW | WS_TABSTOP);
      EnableWindow(copyUrl, !busy);
      auto connect =
          control(L"BUTTON", "Add to Claude", 8103, 16, y + 38, 152, 30, BS_OWNERDRAW | WS_TABSTOP);
      EnableWindow(connect, !busy);
      y += 38;
    }
    y += 44;
    auto outcome = model.value("outcome", std::string());
    if (!outcome.empty()) {
      control(L"EDIT", outcome, 8207, 16, y, 294, 96,
              ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP);
      y += 104;
    }
    if (running) {
      auto config =
          control(L"BUTTON", "Copy configuration", 8102, 16, y, 190, 30, BS_OWNERDRAW | WS_TABSTOP);
      EnableWindow(config, !busy);
      control(L"BUTTON", manualExpanded ? "Hide setup" : "Manual setup", 8108, 214, y, 96, 30,
              BS_OWNERDRAW | WS_TABSTOP);
      y += 42;
      if (manualExpanded) {
        control(L"STATIC", "Or add it yourself", 0, 16, y, 294, 24);
        y += 28;
        control(L"STATIC",
                "Create an MCP agent named Claude Code in Controller. Add this server entry to "
                "your client settings, then reload the client. Accounts remain user-configured.",
                0, 16, y, 294, 84);
        y += 90;
        control(L"EDIT", model.value("manualSetup", std::string()), 8206, 16, y, 294, 112,
                ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP);
        y += 120;
        auto manual = control(L"BUTTON", "Copy manual setup", 8107, 16, y, 180, 30,
                              BS_OWNERDRAW | WS_TABSTOP);
        EnableWindow(manual, !busy);
        y += 40;
      }
    }
    control(L"STATIC", "Prompt for your AI", 0, 16, y, 294, 24);
    y += 28;
    control(L"STATIC", "Paste this to your AI agent so it knows how to connect and start working.",
            0, 16, y, 294, 40);
    y += 46;
    control(L"EDIT", prompt(model), 8204, 16, y, 294, 190,
            ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP);
    y += 198;
    auto copy = control(L"BUTTON", "Copy prompt", 8104, 16, y, 142, 30, BS_OWNERDRAW | WS_TABSTOP);
    EnableWindow(copy, !busy);
    y += 42;
    control(L"STATIC", "Connected agents appear in Controller. Assign their role and work there.",
            0, 16, y, 294, 42);
    y += 50;
    control(L"BUTTON", "Full connection details", 8105, 16, y, 210, 30, BS_OWNERDRAW | WS_TABSTOP);
    y += 42;
    contentHeight = y + 8;
    dpi::setWindowPos(window, nullptr, 0, 0, 340, std::clamp(contentHeight, 220, maximumHeight),
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT bounds{};
    dpi::clientRect(window, &bounds);
    SCROLLINFO range{sizeof(range),
                     SIF_RANGE | SIF_PAGE | SIF_POS,
                     0,
                     contentHeight - 1,
                     UINT(bounds.bottom),
                     scroll,
                     0};
    SetScrollInfo(window, SB_VERT, &range, TRUE);
    scrollTo(scroll);
    InvalidateRect(window, nullptr, TRUE);
  }
  void refresh(bool force = false) {
    if (!window)
      return;
    try {
      model = snapshot();
      if (!model.is_object())
        throw std::runtime_error("Invalid local status");
    } catch (...) {
      model = {{"running", false},
               {"error", "Connection status is unavailable. Open full connection details."}};
    }
    auto next = Json::array({model.value("running", false), model.value("busy", false),
                             model.value("connectedClients", 0) == 0 &&
                                 model.value("requestsSeen", uint64_t(0)) > 0,
                             model.value("error", std::string()),
                             model.value("outcome", std::string()), localError})
                    .dump();
    if (force || next != shape) {
      auto focused = GetFocus();
      int focusId = IsChild(window, focused) ? GetDlgCtrlID(focused) : 0;
      shape = next;
      build();
      if (focusId && IsWindowEnabled(GetDlgItem(window, focusId)))
        SetFocus(GetDlgItem(window, focusId));
    }
    // Update changing endpoint/prompt text without rebuilding controls or resetting
    // selection and scroll position when the public snapshot is unchanged.
    auto update = [&](int id, const std::string &text) {
      auto child = GetDlgItem(window, id);
      if (!child)
        return;
      std::wstring current(GetWindowTextLengthW(child) + 1, L'\0');
      auto length = GetWindowTextW(child, current.data(), int(current.size()));
      current.resize(length);
      auto next = wide(text);
      if (current != next)
        SetWindowTextW(child, next.c_str());
    };
    update(8205, model.value("url", std::string()));
    update(8204, prompt(model));
    update(8206, model.value("manualSetup", std::string()));
    if (clients)
      SetWindowTextW(clients, wide(std::to_string(model.value("connectedClients", 0))).c_str());
    if (traffic) {
      auto last = model.value("lastContactAt", int64_t(0));
      FILETIME time{};
      GetSystemTimeAsFileTime(&time);
      ULARGE_INTEGER value{};
      value.LowPart = time.dwLowDateTime;
      value.HighPart = time.dwHighDateTime;
      int64_t now = int64_t(value.QuadPart / 10000ULL - 11644473600000ULL);
      auto text = std::to_string(model.value("requestsSeen", uint64_t(0))) + " req · " +
                  (last ? "last " + mcpContactAge(now, last) : "none yet");
      SetWindowTextW(traffic, wide(text).c_str());
    }
    auto copy = GetDlgItem(window, 8104);
    if (copy)
      SetWindowTextW(copy, GetTickCount64() < copiedUntil ? L"Copied" : L"Copy prompt");
  }
  static LRESULT CALLBACK keyboard(HWND child, UINT msg, WPARAM w, LPARAM l, UINT_PTR id,
                                   DWORD_PTR data) {
    auto self = reinterpret_cast<Impl *>(data);
    dpi::Scope scale(dpi::forWindow(self->window));
    if (msg == WM_SETFOCUS)
      self->reveal(child);
    if (msg == WM_KEYDOWN) {
      if (w == VK_ESCAPE) {
        PostMessageW(self->window, WM_CLOSE, 0, 0);
        return 0;
      }
      if (w == VK_TAB) {
        auto next = GetNextDlgTabItem(self->window, child, GetKeyState(VK_SHIFT) < 0);
        if (next)
          SetFocus(next);
        return 0;
      }
    }
    if (msg == WM_NCDESTROY)
      RemoveWindowSubclass(child, keyboard, id);
    return DefSubclassProc(child, msg, w, l);
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
    switch (msg) {
    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    case WM_NCDESTROY:
      self->window = nullptr;
      self->status = self->clients = self->traffic = nullptr;
      break;
    case WM_ACTIVATE:
      if (LOWORD(w) == WA_INACTIVE && !self->performingAction)
        PostMessageW(h, WM_CLOSE, 0, 0);
      break;
    case WM_TIMER:
      if (w == 2)
        KillTimer(h, 2);
      if (w == 1 || w == 2)
        self->refresh();
      return 0;
    case WM_COMMAND:
      if (HIWORD(w) == BN_CLICKED && LOWORD(w) == 8108) {
        self->manualExpanded = !self->manualExpanded;
        self->refresh(true);
        SetFocus(GetDlgItem(h, 8108));
        return 0;
      }
      if (HIWORD(w) == BN_CLICKED && LOWORD(w) >= 8101 && LOWORD(w) <= 8107) {
        auto action = static_cast<McpPopoverAction>(LOWORD(w));
        if (action == McpPopoverAction::advanced)
          PostMessageW(h, WM_CLOSE, 0, 0);
        struct ActionGuard {
          bool &flag;
          ActionGuard(bool &value) : flag(value) { flag = true; }
          ~ActionGuard() { flag = false; }
        } actionGuard(self->performingAction);
        try {
          {
            dpi::Scope scale(dpi::forWindow(self->owner));
            self->action(action);
          }
          self->localError.clear();
          if (action == McpPopoverAction::copyPrompt) {
            self->copiedUntil = GetTickCount64() + 1600;
            SetTimer(h, 2, 1600, nullptr);
          }
        } catch (...) {
          self->localError = "Could not complete this action. Check full connection details and "
                             "local client permissions.";
          // Keep recovery guidance inline so setup errors do not dismiss the popup.
        }
        self->refresh();
        if (action != McpPopoverAction::advanced && self->window)
          SetForegroundWindow(self->window);
        return 0;
      }
      break;
    case WM_DRAWITEM:
      skin::button(*reinterpret_cast<DRAWITEMSTRUCT *>(l));
      return TRUE;
    case WM_CTLCOLORSTATIC: {
      wchar_t name[32]{};
      GetClassNameW(reinterpret_cast<HWND>(l), name, 32);
      // Windows sends read-only EDIT colors through WM_CTLCOLORSTATIC.
      if (std::wstring(name) == L"Edit")
        return self->editColors(reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l));
    }
      [[fallthrough]];
    case WM_CTLCOLORBTN:
      SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
      SetTextColor(reinterpret_cast<HDC>(w),
                   reinterpret_cast<HWND>(l) == self->status
                       ? self->model.value("running", false) ? skin::matched() : skin::muted()
                       : skin::text());
      return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_CTLCOLOREDIT:
      return self->editColors(reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l));
    case WM_MOUSEWHEEL:
      self->scrollTo(self->scroll - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 54);
      return 0;
    case WM_VSCROLL: {
      auto next = self->scroll;
      SCROLLINFO info{sizeof(info), SIF_TRACKPOS};
      GetScrollInfo(h, SB_VERT, &info);
      switch (LOWORD(w)) {
      case SB_LINEUP:
        next -= 27;
        break;
      case SB_LINEDOWN:
        next += 27;
        break;
      case SB_PAGEUP:
        next -= 300;
        break;
      case SB_PAGEDOWN:
        next += 300;
        break;
      case SB_THUMBTRACK:
        next = info.nTrackPos;
        break;
      }
      self->scrollTo(next);
      return 0;
    }
    case WM_KEYDOWN:
      if (w == VK_ESCAPE) {
        PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
      }
      break;
    case WM_PRINTCLIENT:
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      auto dc = msg == WM_PAINT ? dpi::beginPaint(h, &ps) : reinterpret_cast<HDC>(w);
      RECT r{};
      dpi::clientRect(h, &r);
      skin::background(dc, r.right, r.bottom);
      skin::panel(dc, 0, 0, r.right, r.bottom, true);
      if (msg == WM_PAINT)
        EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    }
    return DefWindowProcW(h, msg, w, l);
  }
  void show(HWND anchor) {
    if (window) {
      PostMessageW(window, WM_CLOSE, 0, 0);
      return;
    }
    dpi::Scope scale(dpi::forWindow(owner));
    WNDCLASSW cls{};
    cls.lpfnWndProc = proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"TangOSLiteMcpPopover";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dpi::registerClass(&cls);
    RECT bounds{};
    dpi::windowRect(owner, &bounds);
    RECT button{};
    dpi::windowRect(IsWindow(anchor) ? anchor : owner, &button);
    int top = IsWindow(anchor) ? button.bottom + 12 : bounds.top + 72;
    int left =
        std::max(int(bounds.left + 16), std::min(int(button.right - 340), int(bounds.right - 356)));
    maximumHeight = std::max(220, std::min(620, int(bounds.bottom - top - 16)));
    scroll = 0;
    localError.clear();
    window = dpi::createWindow(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, cls.lpszClassName,
                               L"MCP server", WS_POPUP | WS_CLIPCHILDREN | WS_VSCROLL, left, top,
                               340, maximumHeight, owner, nullptr, cls.hInstance, this);
    if (!window)
      throw std::runtime_error("Could not open MCP popup");
    try {
      dpi::Scope popupScale(dpi::forWindow(window));
      RECT ownerPixels{}, popupPixels{};
      GetWindowRect(owner, &ownerPixels);
      GetWindowRect(window, &popupPixels);
      maximumHeight = std::max(
          220,
          std::min(620, dpi::logical(ownerPixels.bottom - popupPixels.top, dpi::current()) - 16));
      refresh(true);
      animatePopover(window);
      SetTimer(window, 1, 500, nullptr);
      ShowWindow(window, SW_SHOWNORMAL);
      SetFocus(GetNextDlgTabItem(window, nullptr, FALSE));
    } catch (...) {
      DestroyWindow(window);
      throw;
    }
  }
};
McpPopover::McpPopover(HWND owner, std::function<Json()> snapshot,
                       std::function<void(McpPopoverAction)> action)
    : impl(std::make_unique<Impl>(owner, std::move(snapshot), std::move(action))) {}
McpPopover::~McpPopover() = default;
void McpPopover::show(HWND anchor) { impl->show(anchor); }
HWND McpPopover::window() const { return impl->window; }
} // namespace lite