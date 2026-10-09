#include "report_dialog.h"
#include "skin.h"
#include "images.h"
#include "clipboard.h"
#include "platform.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <stdexcept>
namespace lite {
namespace {
constexpr int Description = 7200, Attach = 7201, Cancel = 7202, Prepare = 7203, Close = 7204,
              Chip = 7300;
std::string contents(HWND control) {
  std::wstring value(GetWindowTextLengthW(control) + 1, 0);
  GetWindowTextW(control, value.data(), int(value.size()));
  value.resize(wcslen(value.c_str()));
  return utf8(value);
}
} // namespace
struct ReportDialog::Impl {
  HWND owner, window = nullptr, descriptionField = nullptr;
  HFONT font;
  fs::path repository, data;
  Settings settings;
  std::map<std::string, std::string> secrets;
  bool showExports, building = false, disabledOwner = false;
  HDC backdrop = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previousBitmap = nullptr;
  HBRUSH field = nullptr;
  int width = 0, height = 0;
  RECT panel{};
  std::vector<HWND> controls;
  std::vector<fs::path> screenshots;
  std::string description, error;
  std::thread worker;
  Runner runner;
  std::atomic<bool> busy{false}, ready{false};
  Json pending, completed = Json::object();
  Impl(HWND owner, HFONT font, fs::path repository, fs::path data, Settings settings,
       std::map<std::string, std::string> secrets, bool showExports)
      : owner(owner), font(font), repository(std::move(repository)), data(std::move(data)),
        settings(std::move(settings)), secrets(std::move(secrets)), showExports(showExports) {
    field = CreateSolidBrush(skin::field());
  }
  ~Impl() {
    runner.cancel();
    if (worker.joinable())
      worker.join();
    close();
    if (field)
      DeleteObject(field);
    if (backdrop) {
      SelectObject(backdrop, previousBitmap);
      DeleteObject(bitmap);
      DeleteDC(backdrop);
    }
  }
  void close() {
    if (window)
      DestroyWindow(window);
    if (disabledOwner && IsWindow(owner))
      EnableWindow(owner, TRUE);
    disabledOwner = false;
  }
  HWND control(const wchar_t *kind, const std::string &text, int id, int x, int y, int w, int h,
               DWORD style = 0) {
    if (std::wstring(kind) == L"BUTTON")
      style |= BS_OWNERDRAW;
    auto child = CreateWindowExW(
        0, kind, wide(text).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, x, y, w, h, window,
        reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    skin::controlFont(child, 13);
    SetWindowSubclass(child, keyboard, 4, reinterpret_cast<DWORD_PTR>(this));
    controls.push_back(child);
    return child;
  }
  void build() {
    if (!window)
      return;
    DWORD selectionStart = 0, selectionEnd = 0;
    int firstLine = 0;
    bool restoreFocus = descriptionField && GetFocus() == descriptionField;
    if (descriptionField) {
      SendMessageW(descriptionField, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart),
                   reinterpret_cast<LPARAM>(&selectionEnd));
      firstLine = int(SendMessageW(descriptionField, EM_GETFIRSTVISIBLELINE, 0, 0));
    }
    if (descriptionField)
      description = contents(descriptionField);
    building = true;
    for (auto child : controls)
      DestroyWindow(child);
    controls.clear();
    descriptionField = nullptr;
    int rows = screenshots.empty() ? 1 : 1 + int((screenshots.size() + 1) / 2);
    int panelHeight = 326 + (rows - 1) * 28;
    panel = {(width - 520) / 2, (height - panelHeight) / 2, (width + 520) / 2,
             (height + panelHeight) / 2};
    int x = panel.left + 19, y = panel.top;
    auto closeButton = control(L"BUTTON", "Close report", Close, panel.right - 45, y + 17, 26, 26);
    skin::iconButton(closeButton, skin::Icon::close);
    if (completed.contains("folder")) {
      control(L"EDIT", completed.at("folder"), 0, x, y + 190, 482, 54, ES_MULTILINE | ES_READONLY);
      auto done = control(L"BUTTON", "Done", Close, panel.left + 213, y + 266, 94, 30);
      skin::buttonFont(done, 13);
    } else {
      descriptionField = control(L"EDIT", description, Description, x, y + 115, 482, 120,
                                 ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
      SendMessageW(descriptionField, EM_SETLIMITTEXT, 65536, 0);
      RECT textBounds{11, 9, 459, 109};
      SendMessageW(descriptionField, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&textBounds));
      SetWindowRgn(descriptionField, CreateRoundRectRgn(0, 0, 483, 121, 20, 20), TRUE);
      auto attachButton = control(L"BUTTON", "Attach screenshots", Attach, x, y + 245, 153, 26);
      skin::iconTextButton(attachButton, skin::Icon::image);
      skin::buttonFont(attachButton, 12);
      for (size_t i = 0; i < screenshots.size(); ++i) {
        int column = int(i % 2), row = 1 + int(i / 2);
        auto chip = control(L"BUTTON", utf8(screenshots[i].filename().wstring()) + " ×",
                            Chip + int(i), x + column * 242, y + 245 + row * 28, 236, 26);
        skin::buttonFont(chip, 11, 600);
      }
      auto cancel =
          control(L"BUTTON", "Cancel", Cancel, panel.right - 233, panel.bottom - 43, 72, 26);
      auto prepare = control(L"BUTTON", busy ? "Preparing…" : "Prepare report", Prepare,
                             panel.right - 153, panel.bottom - 43, 134, 26);
      skin::buttonFont(cancel, 12);
      skin::buttonFont(prepare, 12);
      EnableWindow(prepare, !busy && !trim(description).empty());
      for (auto child : controls)
        if (GetDlgCtrlID(child) != Close && GetDlgCtrlID(child) != Cancel &&
            GetDlgCtrlID(child) != Prepare)
          EnableWindow(child, !busy);
    }
    building = false;
    if (descriptionField) {
      SendMessageW(descriptionField, EM_SETSEL, selectionStart, selectionEnd);
      SendMessageW(descriptionField, EM_LINESCROLL, 0, firstLine);
      if (restoreFocus)
        SetFocus(descriptionField);
    }
    skin::invalidateBackdrop(window);
    InvalidateRect(window, nullptr, FALSE);
  }
  void show() {
    if (window) {
      SetForegroundWindow(window);
      return;
    }
    RECT bounds{};
    GetClientRect(owner, &bounds);
    width = bounds.right;
    height = bounds.bottom;
    POINT origin{};
    ClientToScreen(owner, &origin);
    auto source = GetDC(owner);
    if (backdrop) {
      SelectObject(backdrop, previousBitmap);
      DeleteObject(bitmap);
      DeleteDC(backdrop);
    }
    backdrop = CreateCompatibleDC(source);
    bitmap = CreateCompatibleBitmap(source, width, height);
    previousBitmap = SelectObject(backdrop, bitmap);
    SendMessageW(owner, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(backdrop), PRF_CLIENT);
    struct Capture {
      HWND root;
      HDC dc;
    } capture{owner, backdrop};
    EnumChildWindows(
        owner,
        [](HWND child, LPARAM value) -> BOOL {
          auto &capture = *reinterpret_cast<Capture *>(value);
          for (auto ancestor = child; ancestor && ancestor != capture.root;
               ancestor = GetParent(ancestor))
            if (!(GetWindowLongW(ancestor, GWL_STYLE) & WS_VISIBLE))
              return TRUE;
          RECT bounds{};
          GetWindowRect(child, &bounds);
          MapWindowPoints(nullptr, capture.root, reinterpret_cast<POINT *>(&bounds), 2);
          auto state = SaveDC(capture.dc);
          SetViewportOrgEx(capture.dc, bounds.left, bounds.top, nullptr);
          IntersectClipRect(capture.dc, 0, 0, bounds.right - bounds.left,
                            bounds.bottom - bounds.top);
          wchar_t name[32]{};
          GetClassNameW(child, name, 32);
          if (std::wstring(name) == L"Button" &&
              (GetWindowLongW(child, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW) {
            DRAWITEMSTRUCT item{};
            item.CtlType = ODT_BUTTON;
            item.CtlID = GetDlgCtrlID(child);
            item.itemAction = ODA_DRAWENTIRE;
            item.itemState = IsWindowEnabled(child) ? 0 : ODS_DISABLED;
            item.hwndItem = child;
            item.hDC = capture.dc;
            item.rcItem = {0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
            SendMessageW(GetParent(child), WM_DRAWITEM, item.CtlID,
                         reinterpret_cast<LPARAM>(&item));
          } else
            SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(capture.dc),
                         PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
          RestoreDC(capture.dc, state);
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&capture));
    ReleaseDC(owner, source);
    WNDCLASSW type{};
    type.lpfnWndProc = proc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"TangOSLiteBugReport";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    window = CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"Report a bug",
                             WS_POPUP | WS_CLIPCHILDREN, origin.x, origin.y, width, height, owner,
                             nullptr, type.hInstance, this);
    if (!window)
      throw std::runtime_error("Cannot open the native bug-report overlay");
    SetWindowRgn(window, CreateRoundRectRgn(0, 0, width + 1, height + 1, 32, 32), FALSE);
    disabledOwner = IsWindowEnabled(owner);
    if (disabledOwner)
      EnableWindow(owner, FALSE);
    build();
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    if (descriptionField)
      SetFocus(descriptionField);
  }
  void attach(const fs::path &path) {
    if (busy)
      throw std::runtime_error("Wait for report preparation");
    inspectScreenshot(path);
    if (std::find(screenshots.begin(), screenshots.end(), path) == screenshots.end()) {
      if (screenshots.size() >= 16)
        throw std::runtime_error("Choose at most 16 screenshots");
      screenshots.push_back(path);
    }
    build();
  }
  void pickScreenshots() {
    IFileOpenDialog *dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
      throw std::runtime_error("Cannot open screenshot picker");
    struct Guard {
      IFileOpenDialog *value;
      ~Guard() { value->Release(); }
    } guard{dialog};
    DWORD flags = 0;
    dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    COMDLG_FILTERSPEC filter{L"Screenshots", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif"};
    dialog->SetFileTypes(1, &filter);
    dialog->SetTitle(L"Attach screenshots");
    if (FAILED(dialog->Show(window)))
      return;
    IShellItemArray *items = nullptr;
    if (FAILED(dialog->GetResults(&items)))
      return;
    struct Items {
      IShellItemArray *value;
      ~Items() { value->Release(); }
    } itemGuard{items};
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; ++i) {
      IShellItem *item = nullptr;
      if (SUCCEEDED(items->GetItemAt(i, &item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          auto value = fs::path(path);
          CoTaskMemFree(path);
          item->Release();
          attach(value);
        } else
          item->Release();
      }
    }
  }
  void prepare() {
    if (busy)
      return;
    if (descriptionField)
      description = contents(descriptionField);
    if (trim(description).empty())
      throw std::runtime_error("Describe what went wrong first");
    if (worker.joinable())
      worker.join();
    runner.reset();
    ready = false;
    busy = true;
    error.clear();
    Json args = {{"description", description}, {"screenshots", Json::array()}};
    for (auto &path : screenshots)
      args["screenshots"].push_back(utf8(path.wstring()));
    worker = std::thread([this, args]() mutable {
      try {
        Backend service(repository, data, settings, secrets, requestHttp, &runner);
        auto preview = service.invoke("bug.report", args, 1000);
        if (runner.isCancelled())
          throw std::runtime_error("Report preparation cancelled");
        args["confirmation"] = preview.at("confirmation");
        pending = service.invoke("bug.report", args, 1000);
      } catch (const std::exception &e) {
        pending = {{"error", e.what()}};
      }
      ready = true;
      busy = false;
    });
    build();
  }
  void tick() {
    if (!ready.exchange(false))
      return;
    if (worker.joinable())
      worker.join();
    if (pending.contains("error"))
      error = pending.at("error");
    else {
      completed = pending;
      if (window && showExports && !runner.isCancelled()) {
        copyClipboardText(window, completed.at("markdown").get<std::string>());
        ShellExecuteW(window, L"open", wide(completed.at("folder").get<std::string>()).c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
      }
    }
    build();
    if (window && !error.empty() && !runner.isCancelled())
      MessageBoxW(window, wide(error).c_str(), L"Report could not be prepared",
                  MB_OK | MB_ICONERROR);
  }
  void paint(HDC dc) {
    BitBlt(dc, 0, 0, width, height, backdrop, 0, 0, SRCCOPY);
    skin::scrim(dc, width, height);
    skin::panel(dc, panel.left, panel.top, panel.right - panel.left, panel.bottom - panel.top,
                true);
    int x = panel.left + 19, y = panel.top;
    skin::label(dc, L"Report a bug", x + 24, y + 17, 390, 26, 16, true);
    skin::drawIcon(dc, skin::Icon::report, x, y + 22, 16);
    if (completed.contains("folder")) {
      skin::label(dc, L"✓", panel.left + 244, y + 66, 40, 38, 30, true, false, false,
                  skin::matched());
      skin::label(dc,
                  showExports ? L"Report ready. The details are copied to your clipboard.\n"
                                L"The screenshot folder is open."
                              : L"Report ready. The local report and screenshots\n"
                                L"have been saved.",
                  x + 8, y + 113, 466, 66, 13);
    } else {
      skin::label(dc,
                  L"What went wrong? We attach app version, OS and recent activity.\n"
                  L"No API keys in diagnostics. Ctrl+V to paste a screenshot.",
                  x, y + 53, 482, 44, 13, false, true);
    }
  }
  static LRESULT CALLBACK keyboard(HWND child, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                   DWORD_PTR context) {
    auto self = reinterpret_cast<Impl *>(context);
    if ((message == WM_PAINT || message == WM_PRINT) && GetDlgCtrlID(child) == Description) {
      auto result = DefSubclassProc(child, message, w, l);
      if (contents(child).empty()) {
        auto dc = message == WM_PRINT ? reinterpret_cast<HDC>(w) : GetDC(child);
        skin::label(dc, L"Describe what happened and how to reproduce it…", 11, 9, 449, 38, 13,
                    false, true);
        if (message == WM_PAINT)
          ReleaseDC(child, dc);
      }
      return result;
    }
    if (message == WM_PASTE && GetDlgCtrlID(child) == Description && !self->busy &&
        (IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5))) {
      try {
        auto path = saveClipboardScreenshot(self->window, self->data / "screenshots");
        if (!path.empty())
          self->attach(path);
      } catch (const std::exception &e) {
        MessageBoxW(self->window, wide(e.what()).c_str(), L"Screenshot", MB_OK | MB_ICONERROR);
      }
      return 0;
    }
    if (message == WM_KEYDOWN && w == VK_ESCAPE) {
      self->runner.cancel();
      self->close();
      return 0;
    }
    if (message == WM_KEYDOWN && w == VK_TAB) {
      SetFocus(GetNextDlgTabItem(self->window, child, GetKeyState(VK_SHIFT) < 0));
      return 0;
    }
    if (message == WM_KEYDOWN && w == VK_RETURN && GetDlgCtrlID(child) != Description) {
      SendMessageW(self->window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(child), BN_CLICKED),
                   reinterpret_cast<LPARAM>(child));
      return 0;
    }
    if (message == WM_NCDESTROY)
      RemoveWindowSubclass(child, keyboard, id);
    return DefSubclassProc(child, message, w, l);
  }
  static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Impl *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = reinterpret_cast<Impl *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
      self->window = window;
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
      return DefWindowProcW(window, message, w, l);
    try {
      if (message == WM_CLOSE) {
        self->runner.cancel();
        self->close();
        return 0;
      }
      if (message == WM_NCDESTROY) {
        self->window = nullptr;
        self->descriptionField = nullptr;
        self->controls.clear();
        if (self->disabledOwner && IsWindow(self->owner))
          EnableWindow(self->owner, TRUE);
        self->disabledOwner = false;
        return DefWindowProcW(window, message, w, l);
      }
      if (message == WM_ERASEBKGND)
        return 1;
      if (message == WM_PRINTCLIENT) {
        self->paint(reinterpret_cast<HDC>(w));
        return 0;
      }
      if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        auto dc = BeginPaint(window, &paint);
        self->paint(dc);
        EndPaint(window, &paint);
        return 0;
      }
      if (message == WM_DRAWITEM) {
        auto item = reinterpret_cast<DRAWITEMSTRUCT *>(l);
        skin::button(*item, item->CtlID == Prepare ||
                                (item->CtlID == Close && contents(item->hwndItem) == "Done"));
        return TRUE;
      }
      if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORSTATIC) {
        auto dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, skin::text());
        SetBkColor(dc, skin::field());
        return reinterpret_cast<LRESULT>(self->field);
      }
      if (message == WM_LBUTTONUP) {
        POINT point{short(LOWORD(l)), short(HIWORD(l))};
        if (!PtInRect(&self->panel, point)) {
          self->runner.cancel();
          self->close();
        }
        return 0;
      }
      if (message == WM_COMMAND && !self->building) {
        int id = LOWORD(w);
        if (id == Description && HIWORD(w) == EN_CHANGE) {
          self->description = contents(self->descriptionField);
          EnableWindow(GetDlgItem(window, Prepare),
                       !self->busy && !trim(self->description).empty());
        } else if (HIWORD(w) == BN_CLICKED) {
          if (id == Close || id == Cancel) {
            self->runner.cancel();
            self->close();
          } else if (id == Attach)
            self->pickScreenshots();
          else if (id == Prepare)
            self->prepare();
          else if (id >= Chip && size_t(id - Chip) < self->screenshots.size() && !self->busy) {
            self->screenshots.erase(self->screenshots.begin() + id - Chip);
            self->build();
          }
        }
        return 0;
      }
    } catch (const std::exception &e) {
      MessageBoxW(window, wide(e.what()).c_str(), L"Report", MB_OK | MB_ICONERROR);
    }
    return DefWindowProcW(window, message, w, l);
  }
};
ReportDialog::ReportDialog(HWND owner, HFONT font, fs::path repository, fs::path data,
                           Settings settings, std::map<std::string, std::string> secrets,
                           bool showExports)
    : impl(std::make_unique<Impl>(owner, font, std::move(repository), std::move(data),
                                  std::move(settings), std::move(secrets), showExports)) {}
ReportDialog::~ReportDialog() = default;
void ReportDialog::show() { impl->show(); }
void ReportDialog::close() { impl->close(); }
void ReportDialog::tick() { impl->tick(); }
bool ReportDialog::isOpen() const { return impl->window != nullptr; }
bool ReportDialog::running() const { return impl->busy || impl->ready; }
void ReportDialog::stop() { impl->runner.cancel(); }
HWND ReportDialog::window() const { return impl->window; }
void ReportDialog::setDescription(const std::string &value) {
  impl->description = value;
  if (impl->descriptionField) {
    SetWindowTextW(impl->descriptionField, wide(value).c_str());
    EnableWindow(GetDlgItem(impl->window, Prepare), !impl->busy && !trim(value).empty());
  }
}
void ReportDialog::attach(const fs::path &path) { impl->attach(path); }
void ReportDialog::prepare() { impl->prepare(); }
Json ReportDialog::result() const { return impl->completed; }
void ReportDialog::saveSnapshot(const fs::path &path) const {
  if (!impl->window)
    throw std::runtime_error("Open the report before capturing its layout");
  auto dc = GetDC(impl->window);
  auto memory = CreateCompatibleDC(dc);
  auto bitmap = CreateCompatibleBitmap(dc, impl->width, impl->height);
  auto old = SelectObject(memory, bitmap);
  impl->paint(memory);
  for (auto child : impl->controls) {
    RECT bounds{};
    GetWindowRect(child, &bounds);
    MapWindowPoints(nullptr, impl->window, reinterpret_cast<POINT *>(&bounds), 2);
    int state = SaveDC(memory);
    SetViewportOrgEx(memory, bounds.left, bounds.top, nullptr);
    IntersectClipRect(memory, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
    if ((GetWindowLongW(child, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW &&
        GetDlgCtrlID(child) != Description && GetDlgCtrlID(child) != 0) {
      DRAWITEMSTRUCT item{};
      item.CtlType = ODT_BUTTON;
      item.CtlID = GetDlgCtrlID(child);
      item.itemAction = ODA_DRAWENTIRE;
      item.itemState = IsWindowEnabled(child) ? 0 : ODS_DISABLED;
      item.hwndItem = child;
      item.hDC = memory;
      item.rcItem = {0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
      SendMessageW(impl->window, WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
    } else
      SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(memory),
                   PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
    RestoreDC(memory, state);
  }
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = impl->width;
  info.bmiHeader.biHeight = impl->height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  std::string pixels(size_t(impl->width) * impl->height * 4, '\0');
  SelectObject(memory, old);
  int rows = GetDIBits(dc, bitmap, 0, impl->height, pixels.data(), &info, DIB_RGB_COLORS);
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(impl->window, dc);
  if (rows != impl->height)
    throw std::runtime_error("Cannot capture native report pixels");
  std::string dib(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(info.bmiHeader));
  dib += pixels;
  write(path, dibScreenshotBitmap(dib));
}
} // namespace lite
