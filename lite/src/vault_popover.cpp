#include "vault_popover.h"
#include "dpi.h"
#include "popover_motion.h"
#include "skin.h"
#include <algorithm>
#include <commctrl.h>
#include <shellapi.h>
namespace lite {
struct VaultPopover::Impl {
  HWND owner = nullptr, window = nullptr, input = nullptr;
  Vault &vault;
  std::vector<std::string> declared;
  Json help;
  std::string adding, error;
  std::map<int, std::string> addNames, removeNames;
  HBRUSH field = nullptr;
  int scroll = 0, contentHeight = 0, maximumHeight = 560;
  Impl(HWND parent, Vault &storage, std::vector<std::string> names, Json guidance)
      : owner(parent), vault(storage), declared(std::move(names)), help(std::move(guidance)) {
    field = CreateSolidBrush(skin::field());
  }
  ~Impl() {
    if (window)
      DestroyWindow(window);
    DeleteObject(field);
  }
  static std::string value(HWND edit) {
    int n = GetWindowTextLengthW(edit);
    std::wstring buffer(n + 1, L'\0');
    GetWindowTextW(edit, buffer.data(), n + 1);
    buffer.resize(n);
    auto result = utf8(buffer);
    if (!buffer.empty())
      SecureZeroMemory(buffer.data(), buffer.size() * sizeof(wchar_t));
    return result;
  }
  HWND control(const wchar_t *kind, const std::string &title, int id, int x, int y, int width,
               int height, DWORD style = 0) {
    auto child = dpi::createWindow(
        0, kind, wide(title).c_str(), WS_CHILD | WS_VISIBLE | style, x, y - scroll, width, height,
        window, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    skin::controlFont(child, 12);
    if (std::wstring(kind) == L"BUTTON")
      skin::buttonFont(child, 12);
    SetWindowSubclass(child, keyboard, 1, reinterpret_cast<DWORD_PTR>(this));
    return child;
  }
  void build() {
    while (auto child = GetWindow(window, GW_CHILD))
      DestroyWindow(child);
    input = nullptr;
    addNames.clear();
    removeNames.clear();
    control(L"STATIC", "API keys", 0, 16, 16, 290, 25);
    std::map<std::string, std::string> values;
    try {
      values = vault.values();
    } catch (...) {
      adding.clear();
      error.clear();
      control(L"STATIC",
              "Secure storage could not be read. Check local vault file permissions or restore a "
              "damaged encrypted key file.",
              0, 16, 52, 294, 90);
      control(L"BUTTON", "Retry", 5004, 16, 150, 100, 30, BS_OWNERDRAW | WS_TABSTOP);
      control(L"BUTTON", "Close", 5005, 124, 150, 100, 30, BS_OWNERDRAW | WS_TABSTOP);
      contentHeight = 196;
      scroll = 0;
      dpi::setWindowPos(window, nullptr, 0, 0, 340, 196,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      SCROLLINFO range{sizeof(range), SIF_RANGE | SIF_PAGE | SIF_POS, 0, 195, 196, 0, 0};
      SetScrollInfo(window, SB_VERT, &range, TRUE);
      InvalidateRect(window, nullptr, TRUE);
      SetFocus(GetDlgItem(window, 5004));
      return;
    }
    control(L"STATIC", "Encrypted at rest", 0, 16, 49, 190, 22);
    control(L"STATIC", std::to_string(values.size()) + " stored", 0, 229, 49, 80, 22, SS_RIGHT);
    int y = 79, id = 1000;
    for (auto &entry : values) {
      control(L"STATIC", entry.first, 0, 16, y, 222, 27, SS_ENDELLIPSIS);
      control(L"BUTTON", "Remove", id, 244, y, 66, 27, BS_OWNERDRAW | WS_TABSTOP);
      removeNames[id++] = entry.first;
      if (!entry.second.empty())
        SecureZeroMemory(entry.second.data(), entry.second.size());
      y += 34;
    }
    std::vector<std::string> missing;
    for (auto &name : declared)
      if (!values.count(name) && std::find(missing.begin(), missing.end(), name) == missing.end())
        missing.push_back(name);
    if (!missing.empty()) {
      control(L"STATIC", "This repo's tools expect", 0, 16, y + 4, 294, 24);
      y += 34;
      int x = 16;
      auto dc = dpi::getDC(window);
      for (auto &name : missing) {
        int width = std::clamp(skin::textWidth(dc, wide("+ " + name), 12, 700) + 20, 60, 294);
        if (x > 16 && x + width > 310) {
          x = 16;
          y += 34;
        }
        control(L"BUTTON", "+ " + name, id, x, y, width, 28, BS_OWNERDRAW | WS_TABSTOP);
        addNames[id++] = name;
        x += width + 6;
      }
      ReleaseDC(window, dc);
      y += 38;
    }
    if (!adding.empty()) {
      control(L"STATIC", adding, 0, 16, y, 294, 24, SS_ENDELLIPSIS);
      y += 28;
      if (help.is_object() && help.contains(adding) && help[adding].is_object()) {
        auto note = help[adding].value("note", std::string());
        if (!note.empty()) {
          control(L"STATIC", note, 0, 16, y, 294, 64);
          y += 70;
        }
        auto url = help[adding].value("url", std::string());
        if (!url.empty()) {
          control(L"BUTTON", "Create one ↗", 5003, 16, y, 150, 26, BS_OWNERDRAW | WS_TABSTOP);
          y += 32;
        }
      }
      input = control(L"EDIT", "", 5000, 16, y, 294, 30, WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL);
      SendMessageW(input, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"paste your key"));
      SendMessageW(input, EM_LIMITTEXT, 65536, 0);
      y += 38;
      if (!error.empty()) {
        control(L"STATIC", error, 0, 16, y, 294, 42);
        y += 48;
      }
      control(L"BUTTON", "Save", 5001, 16, y, 100, 30, BS_OWNERDRAW | WS_TABSTOP);
      control(L"BUTTON", "Cancel", 5002, 124, y, 100, 30, BS_OWNERDRAW | WS_TABSTOP);
      y += 42;
    }
    contentHeight = y + 10;
    dpi::setWindowPos(window, nullptr, 0, 0, 340, std::clamp(contentHeight, 120, maximumHeight),
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT bounds{};
    dpi::clientRect(window, &bounds);
    int desired = std::clamp(scroll, 0, std::max(0, contentHeight - int(bounds.bottom)));
    if (input)
      desired = std::max(0, contentHeight - int(bounds.bottom));
    if (desired != scroll) {
      for (auto child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT r{};
        dpi::relativeRect(child, window, &r);
        dpi::setWindowPos(child, nullptr, r.left, r.top + scroll - desired, 0, 0,
                          SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
      scroll = desired;
    }
    SCROLLINFO info{sizeof(info),
                    SIF_RANGE | SIF_PAGE | SIF_POS,
                    0,
                    contentHeight - 1,
                    UINT(bounds.bottom),
                    scroll,
                    0};
    SetScrollInfo(window, SB_VERT, &info, TRUE);
    InvalidateRect(window, nullptr, TRUE);
    SetFocus(input ? input : GetNextDlgTabItem(window, nullptr, FALSE));
  }
  void cancel() {
    adding.clear();
    error.clear();
    if (input)
      SetWindowTextW(input, L"");
    build();
  }
  void save() {
    auto secret = trim(value(input));
    if (secret.empty()) {
      error = "Paste a value";
      build();
      return;
    }
    try {
      vault.set(adding, secret);
    } catch (...) {
      SecureZeroMemory(secret.data(), secret.size());
      throw;
    }
    SecureZeroMemory(secret.data(), secret.size());
    cancel();
  }
  void command(int id) {
    if (addNames.count(id)) {
      adding = addNames.at(id);
      error.clear();
      build();
    } else if (removeNames.count(id)) {
      vault.remove(removeNames.at(id));
      build();
    } else if (id == 5001)
      save();
    else if (id == 5004)
      build();
    else if (id == 5005)
      PostMessageW(window, WM_CLOSE, 0, 0);
    else if (id == 5002)
      cancel();
    else if (id == 5003 && help.contains(adding)) {
      auto url = help[adding].value("url", std::string());
      if (url.rfind("https://", 0) != 0 || url.find_first_of("\r\n") != std::string::npos)
        throw std::runtime_error("Key guidance requires an HTTPS URL");
      ShellExecuteW(window, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
  }
  static LRESULT CALLBACK keyboard(HWND child, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                   DWORD_PTR context) {
    auto self = reinterpret_cast<Impl *>(context);
    if (message == WM_KEYDOWN) {
      if (w == VK_ESCAPE) {
        if (!self->adding.empty())
          self->cancel();
        else
          PostMessageW(self->window, WM_CLOSE, 0, 0);
        return 0;
      }
      if (w == VK_RETURN && child == self->input) {
        PostMessageW(self->window, WM_COMMAND, 5001, 0);
        return 0;
      }
      if (w == VK_TAB) {
        auto next = GetNextDlgTabItem(self->window, child, GetKeyState(VK_SHIFT) < 0);
        if (next)
          SetFocus(next);
        return 0;
      }
    }
    if (message == WM_NCDESTROY)
      RemoveWindowSubclass(child, keyboard, id);
    return DefSubclassProc(child, message, w, l);
  }
  static LRESULT CALLBACK proc(HWND h, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Impl *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<Impl *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
      SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->window = h;
    }
    if (!self)
      return DefWindowProcW(h, message, w, l);
    switch (message) {
    case WM_ACTIVATE:
      if (LOWORD(w) == WA_INACTIVE)
        PostMessageW(h, WM_CLOSE, 0, 0);
      break;
    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    case WM_NCDESTROY:
      self->window = nullptr;
      self->input = nullptr;
      self->adding.clear();
      self->error.clear();
      break;
    case WM_COMMAND:
      if (HIWORD(w) == BN_CLICKED)
        try {
          self->command(LOWORD(w));
        } catch (const std::exception &e) {
          // Never display backend exception text: a provider may echo input.
          self->error = "Could not update secure storage. Check local file permissions.";
          self->build();
        }
      return 0;
    case WM_DRAWITEM:
      skin::button(*reinterpret_cast<DRAWITEMSTRUCT *>(l));
      return TRUE;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
      SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
      SetTextColor(reinterpret_cast<HDC>(w), skin::text());
      return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_CTLCOLOREDIT:
      SetTextColor(reinterpret_cast<HDC>(w), skin::text());
      SetBkColor(reinterpret_cast<HDC>(w), skin::field());
      return reinterpret_cast<LRESULT>(self->field);
    case WM_MOUSEWHEEL:
    case WM_VSCROLL: {
      int next = self->scroll;
      if (message == WM_MOUSEWHEEL)
        next -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 54;
      else {
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
      }
      RECT bounds{};
      dpi::clientRect(h, &bounds);
      next = std::clamp(next, 0, std::max(0, self->contentHeight - int(bounds.bottom)));
      int shift = self->scroll - next;
      self->scroll = next;
      for (auto child = GetWindow(h, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT r{};
        dpi::relativeRect(child, h, &r);
        dpi::setWindowPos(child, nullptr, r.left, r.top + shift, 0, 0,
                          SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
      SetScrollPos(h, SB_VERT, next, TRUE);
      InvalidateRect(h, nullptr, TRUE);
      return 0;
    }
    case WM_PRINTCLIENT:
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      auto dc = message == WM_PAINT ? dpi::beginPaint(h, &ps) : reinterpret_cast<HDC>(w);
      RECT bounds{};
      dpi::clientRect(h, &bounds);
      skin::background(dc, bounds.right, bounds.bottom);
      skin::panel(dc, 0, 0, bounds.right, bounds.bottom, true);
      if (message == WM_PAINT)
        EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    }
    return DefWindowProcW(h, message, w, l);
  }
  void show() {
    if (window) {
      PostMessageW(window, WM_CLOSE, 0, 0);
      return;
    }
    WNDCLASSW cls{};
    cls.lpfnWndProc = proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"TangOSLiteVault";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dpi::registerClass(&cls);
    RECT bounds{};
    dpi::windowRect(owner, &bounds);
    scroll = 0;
    maximumHeight = std::max(220, std::min(560, int(bounds.bottom - bounds.top) - 82));
    window = dpi::createWindow(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, cls.lpszClassName,
                               L"API keys", WS_POPUP | WS_CLIPCHILDREN | WS_VSCROLL,
                               bounds.right - 500, bounds.top + 56, 340,
                               std::max(220, std::min(560, int(bounds.bottom - bounds.top) - 82)),
                               owner, nullptr, cls.hInstance, this);
    if (!window)
      throw std::runtime_error("Could not open secure key popup");
    try {
      build();
      animatePopover(window);
      ShowWindow(window, SW_SHOWNORMAL);
    } catch (...) {
      DestroyWindow(window);
      throw;
    }
  }
};
VaultPopover::VaultPopover(HWND owner, Vault &vault, std::vector<std::string> declared, Json help)
    : impl(std::make_unique<Impl>(owner, vault, std::move(declared), std::move(help))) {}
VaultPopover::~VaultPopover() = default;
void VaultPopover::show() { impl->show(); }
HWND VaultPopover::window() const { return impl->window; }
} // namespace lite