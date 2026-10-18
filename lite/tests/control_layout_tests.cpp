#include "skin.h"
#include "project_popup.h"
#include "popover_motion.h"
#include "vault_popover.h"
#include "mcp_popover.h"
#include "window_capture.h"
#include "dpi.h"
#include <thread>
#include <fstream>
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void capturePopover(HWND window, const lite::fs::path &path) {
  RECT bounds{};
  GetClientRect(window, &bounds);
  auto screen = GetDC(window), dc = CreateCompatibleDC(screen);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = bounds.right;
  info.bmiHeader.biHeight = -bounds.bottom;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  void *pixels = nullptr;
  auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  auto old = SelectObject(dc, bitmap);
  dpi::configureDC(dc, dpi::forWindow(window));
  lite::renderWindowTree(window, dc);
  GdiFlush();
  BITMAPFILEHEADER header{};
  header.bfType = 0x4d42;
  header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
  header.bfSize = header.bfOffBits + bounds.right * bounds.bottom * 4;
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<char *>(&header), sizeof(header));
  file.write(reinterpret_cast<char *>(&info.bmiHeader), sizeof(info.bmiHeader));
  file.write(static_cast<char *>(pixels), bounds.right * bounds.bottom * 4);
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  ReleaseDC(window, screen);
}
int main(int argc, char **argv) {
  std::string capturePath = argc == 3 && std::string(argv[1]) == "--capture" ? argv[2] : "";
  try {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    skin::initialize();
    auto parent = CreateWindowExW(0, L"STATIC", L"Control layout fixture", WS_OVERLAPPED, 0, 0,
                                  1000, 800, nullptr, nullptr, nullptr, nullptr);
    expect(parent != nullptr, "fixture creation failed");
    for (bool edit : {false, true}) {
      auto child = CreateWindowExW(0, edit ? L"EDIT" : L"COMBOBOX", L"Nunito field",
                                   WS_CHILD | (edit ? ES_AUTOHSCROLL : CBS_DROPDOWNLIST), 0, 0, 100,
                                   32, parent, nullptr, nullptr, nullptr);
      expect(child != nullptr, "control creation failed");
      if (edit)
        skin::compactEdit(child);
      else
        skin::compactCombo(child);
      // Exercise grow, shrink and fractional-scale-equivalent dimensions.
      // These are layout stress cases, not physical monitor DPI validation.
      for (int width : {100, 350, 175, 625, 125}) {
        SetWindowPos(child, nullptr, 0, 0, width, 56, SWP_NOZORDER);
        RECT outer{};
        GetWindowRect(child, &outer);
        auto region = CreateRectRgn(0, 0, 0, 0);
        expect(GetWindowRgn(child, region) != ERROR, "missing rounded region");
        expect(PtInRegion(region, width - 3, (outer.bottom - outer.top) / 2),
               "resizing left the field clipped at its original width");
        expect(!PtInRegion(region, width + 2, (outer.bottom - outer.top) / 2),
               "shrinking left an oversized clipping region");
        DeleteObject(region);
      }
      if (edit) {
        skin::controlFont(child, 13);
        RECT small{};
        GetClientRect(child, &small);
        skin::controlFont(child, 24);
        RECT large{};
        GetClientRect(child, &large);
        expect(large.bottom > small.bottom, "larger font did not refresh edit centering");
      }
      DestroyWindow(child);
    }
    auto screen = GetDC(parent), dc = CreateCompatibleDC(screen);
    auto bitmap = CreateCompatibleBitmap(screen, 400, 60);
    auto previous = SelectObject(dc, bitmap);
    RECT row{0, 0, 360, 36};
    for (int theme = 0; theme < 5; ++theme) {
      skin::theme(theme);
      skin::projectMenuRow(dc, row, L"Long project title that must fit beside status", L"DX", true,
                           false, false);
      auto idle = GetPixel(dc, 12, 18);
      skin::projectMenuRow(dc, row, L"Project", L"DX", true, false, true);
      auto hover = GetPixel(dc, 12, 18);
      skin::projectMenuRow(dc, row, L"Project", L"DX", true, true, false);
      auto active = GetPixel(dc, 12, 18);
      expect(idle != hover && hover != active, "menu hover/active states are indistinguishable");
    }
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(parent, screen);
    auto frame = lite::projectPopupFrame(100, false);
    expect(frame.scale > 1 && frame.opacity > .9, "popup reference overshoot/ease missing");
    expect(lite::projectPopupFrame(200, false).scale == 1, "popup opening endpoint wrong");
    expect(lite::projectPopupFrame(160, true).opacity == 0, "popup closing endpoint wrong");
    expect(lite::projectPopupFrame(0, false, false).finished, "reduced motion not instant");
    skin::animate(false);
    ShowWindow(parent, SW_SHOW);
    std::vector<lite::ProjectPopupRow> projects;
    for (unsigned i = 1; i <= 64; ++i)
      projects.push_back({i, L"Project " + std::to_wstring(i), L"DX", i % 2 == 0, i == 1});
    auto popupTest = [&](unsigned key, unsigned expected) {
      auto uiThread = GetCurrentThreadId();
      auto clickDpi = dpi::forWindow(parent);
      std::thread driver([key, uiThread, clickDpi, &capturePath, &projects] {
        HWND popup = nullptr;
        auto start = GetTickCount64();
        while (!popup && GetTickCount64() - start < 5000) {
          EnumThreadWindows(
              uiThread,
              [](HWND window, LPARAM context) -> BOOL {
                wchar_t name[128]{};
                GetClassNameW(window, name, 128);
                if (std::wstring(name) == L"TangOSProjectPopup") {
                  *reinterpret_cast<HWND *>(context) = window;
                  return FALSE;
                }
                return TRUE;
              },
              reinterpret_cast<LPARAM>(&popup));
          Sleep(10);
        }
        if (popup && projects.size() == 3 && !capturePath.empty()) {
          Sleep(230);
          RECT r{};
          GetClientRect(popup, &r);
          auto screen = GetDC(popup), dc = CreateCompatibleDC(screen);
          BITMAPINFO info{};
          info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
          info.bmiHeader.biWidth = r.right;
          info.bmiHeader.biHeight = -r.bottom;
          info.bmiHeader.biPlanes = 1;
          info.bmiHeader.biBitCount = 32;
          void *pixels = nullptr;
          auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
          auto previous = SelectObject(dc, bitmap);
          SendMessageW(popup, WM_PRINTCLIENT, WPARAM(dc), PRF_CLIENT);
          GdiFlush();
          BITMAPFILEHEADER file{};
          file.bfType = 0x4d42;
          file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader);
          file.bfSize = file.bfOffBits + r.right * r.bottom * 4;
          std::ofstream output(capturePath, std::ios::binary);
          output.write(reinterpret_cast<char *>(&file), sizeof(file));
          output.write(reinterpret_cast<char *>(&info.bmiHeader), sizeof(info.bmiHeader));
          output.write(static_cast<char *>(pixels), r.right * r.bottom * 4);
          SelectObject(dc, previous);
          DeleteObject(bitmap);
          DeleteDC(dc);
          ReleaseDC(popup, screen);
        }
        if (popup) {
          if (key == VK_LBUTTON)
            PostMessageW(popup, WM_LBUTTONUP, 0,
                         MAKELPARAM(dpi::pixels(30, clickDpi), dpi::pixels(24, clickDpi)));
          else if (key == VK_RBUTTON)
            PostMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(-10, -10));
          else {
            PostMessageW(popup, WM_KEYDOWN, key, 0);
            if (key != VK_ESCAPE)
              PostMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
          }
        }
      });
      auto result = lite::projectPopup(parent, parent, projects);
      driver.join();
      expect(result == expected, "native popup keyboard/scroll selection failed");
      expect(FindWindowW(L"TangOSProjectPopup", nullptr) == nullptr, "popup leaked its window");
      expect(GetCapture() == nullptr, "popup leaked input capture");
    };
    popupTest(VK_END, 64);
    popupTest(VK_DOWN, 1);
    popupTest(VK_ESCAPE, 0);
    for (UINT scale : {96, 106, 120, 132, 192, 216, 384}) {
      dpi::testing::simulateWindowDpi(parent, scale);
      popupTest(VK_LBUTTON, 1);
      popupTest(VK_END, 64);
      popupTest(VK_RBUTTON, 0);
    }
    dpi::testing::simulateWindowDpi(parent, 96);
    skin::animate(true);
    projects.resize(3);
    popupTest(VK_ESCAPE, 0);
    auto pump = [](unsigned duration) {
      auto start = GetTickCount64();
      while (GetTickCount64() - start < duration) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
        Sleep(2);
      }
    };
    auto newPopover = [&] {
      auto popup = CreateWindowExW(0, L"STATIC", L"Popover motion fixture", WS_POPUP, 50, 50, 260,
                                   180, parent, nullptr, nullptr, nullptr);
      expect(popup != nullptr, "popover fixture creation failed");
      CreateWindowExW(0, L"BUTTON", L"Live child", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 30,
                      100, 30, popup, nullptr, nullptr, nullptr);
      lite::animatePopover(popup);
      ShowWindow(popup, SW_SHOW);
      return popup;
    };
    expect(lite::popoverFrame(0, false).scale == .8, "popover initial scale differs");
    expect(lite::popoverFrame(240, false).scale == 1, "popover opening duration differs");
    expect(lite::popoverFrame(190, true).finished, "popover closing duration differs");
    auto pop = newPopover();
    pump(300);
    expect(IsWindow(pop) && GetPropW(pop, L"TangOSPopoverMotion"), "popover lost its live window");
    SendMessageW(pop, WM_CLOSE, 0, 0);
    pump(250);
    expect(!IsWindow(pop), "popover close did not release native window");
    pop = newPopover();
    pump(40);
    SendMessageW(pop, WM_CLOSE, 0, 0);
    pump(240);
    expect(!IsWindow(pop), "interrupted popover open did not reverse and close");
    pop = newPopover();
    pump(20);
    DestroyWindow(pop);
    pump(30);
    expect(!FindWindowW(L"TangOSPopoverAnimation", nullptr),
           "popover compositor leaked after direct destruction");
    skin::animate(false);
    pop = newPopover();
    expect(!GetPropW(pop, L"TangOSPopoverMotion"), "reduced motion still installed compositor");
    DestroyWindow(pop);
    skin::animate(true);
    skin::animate(false);
    auto vaultDir =
        lite::fs::temp_directory_path() / lite::fs::u8path("tangos-vault-ui-" + lite::uniqueId());
    lite::Vault storage(vaultDir);
    {
      lite::VaultPopover vault(parent, storage, {"TEST_API_KEY"}, lite::Json::object());
      vault.show();
      auto popup = vault.window();
      if (!capturePath.empty()) {
        auto path = lite::fs::u8path(capturePath);
        capturePopover(popup, path.parent_path() / (path.stem().wstring() + L"-vault.bmp"));
      }
      expect(popup && IsWindowVisible(parent), "vault replaced its Controller owner");
      RECT initialBounds{};
      GetWindowRect(popup, &initialBounds);
      expect(initialBounds.bottom - initialBounds.top < 300, "empty vault did not size to content");
      lite::write(vaultDir / "BROKEN_KEY.dpapi", "not encrypted");
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(1000, BN_CLICKED), 0);
      expect(vault.window() && GetDlgItem(popup, 5004) && !GetDlgItem(popup, 5000),
             "corrupt secure storage did not produce a recoverable screen");
      lite::fs::remove(vaultDir / "BROKEN_KEY.dpapi");
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(5004, BN_CLICKED), 0);
      expect(GetDlgItem(popup, 1000), "vault retry did not recover after repairing storage");
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(1000, BN_CLICKED), 0);
      auto input = GetDlgItem(popup, 5000);
      expect(input && (GetWindowLongPtrW(input, GWL_STYLE) & ES_PASSWORD),
             "vault input is not masked");
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(5001, BN_CLICKED), 0);
      expect(storage.values().empty() && GetDlgItem(popup, 5000), "blank vault save accepted");
      SetWindowTextW(GetDlgItem(popup, 5000), L"  fixture-vault-ui-secret  ");
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(5001, BN_CLICKED), 0);
      expect(storage.values().at("TEST_API_KEY") == "fixture-vault-ui-secret",
             "popup failed encrypted save/trim");
      expect(!GetDlgItem(popup, 5000), "vault retained its secret input after save");
      expect(lite::read(vaultDir / "TEST_API_KEY.dpapi").find("fixture-vault-ui-secret") ==
                 std::string::npos,
             "vault popup persisted plaintext");
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(1000, BN_CLICKED), 0);
      expect(storage.values().empty(), "popup remove did not delete stored key");
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(1000, BN_CLICKED), 0);
      SetWindowTextW(GetDlgItem(popup, 5000), L"cancelled-secret");
      SendMessageW(GetDlgItem(popup, 5000), WM_KEYDOWN, VK_ESCAPE, 0);
      expect(vault.window() && !GetDlgItem(popup, 5000) && storage.values().empty(),
             "Escape did not cancel key entry without closing vault");
      SendMessageW(popup, WM_CLOSE, 0, 0);
      expect(!vault.window(), "vault native cleanup failed");
      vault.show();
      expect(vault.window() && !GetDlgItem(vault.window(), 5000),
             "reopened vault retained secret entry");
    }
    {
      std::vector<std::string> names;
      for (int i = 0; i < 64; ++i)
        names.push_back("FIXTURE_KEY_" + std::to_string(i));
      lite::VaultPopover vault(parent, storage, names, lite::Json::object());
      vault.show();
      auto popup = vault.window();
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(1063, BN_CLICKED), 0);
      auto input = GetDlgItem(popup, 5000);
      RECT field{}, client{};
      dpi::relativeRect(input, popup, &field);
      dpi::clientRect(popup, &client);
      expect(input && field.top >= 0 && field.bottom <= client.bottom,
             "long vault list hides secret entry");
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(5002, BN_CLICKED), 0);
      expect(!GetDlgItem(popup, 5000) && storage.values().empty(),
             "long-list cancel stored a credential");
    }
    {
      lite::VaultPopover vault(parent, storage, {"TEST_API_KEY"}, lite::Json::object());
      vault.show();
      auto popup = vault.window();
      dpi::testing::simulateWindowDpi(popup, 144);
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(1000, BN_CLICKED), 0);
      RECT before{}, after{};
      GetWindowRect(popup, &before);
      SendMessageW(GetDlgItem(popup, 5000), WM_KEYDOWN, VK_ESCAPE, 0);
      GetWindowRect(popup, &after);
      expect(before.right - before.left == after.right - after.left &&
                 after.right - after.left == dpi::pixels(340, 144),
             "vault keyboard cancel rebuilt controls using the caller's DPI");
      dpi::testing::simulateWindowDpi(popup, 0);
    }
    {
      expect(lite::mcpContactAge(59499, 0) == "59s ago" &&
                 lite::mcpContactAge(59500, 0) == "1m ago" &&
                 lite::mcpContactAge(3599500, 0) == "1h ago" &&
                 lite::mcpContactAge(1, 5000) == "0s ago",
             "MCP relative contact time differs from the reference");
      lite::Json state{{"running", false},
                       {"busy", false},
                       {"promptPreview", "Follow AGENTS.md. Public fixture only."}};
      std::vector<lite::McpPopoverAction> actions;
      lite::McpPopover mcp(
          parent, [&] { return state; },
          [&](lite::McpPopoverAction action) {
            actions.push_back(action);
            if (action == lite::McpPopoverAction::toggle)
              state["running"] = !state.value("running", false);
          });
      auto label = [](HWND control) {
        std::wstring value(GetWindowTextLengthW(control) + 1, L'\0');
        GetWindowTextW(control, value.data(), int(value.size()));
        value.resize(value.size() - 1);
        return value;
      };
      mcp.show(nullptr);
      auto popup = mcp.window();
      expect(popup && actions.empty() && label(GetDlgItem(popup, 8201)) == L"stopped",
             "MCP popup performs an action merely by opening");
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(8101, BN_CLICKED), 0);
      expect(actions.size() == 1 && label(GetDlgItem(popup, 8201)) == L"running",
             "MCP popup toggle/state refresh failed");
      state["connectedClients"] = 2;
      state["requestsSeen"] = 7;
      state["url"] = "http://127.0.0.1:12345/mcp";
      SendMessageW(popup, WM_TIMER, 1, 0);
      expect(label(GetDlgItem(popup, 8202)) == L"2" &&
                 label(GetDlgItem(popup, 8203)).find(L"7 req") != std::wstring::npos,
             "MCP popup failed live client/traffic refresh");
      if (!capturePath.empty()) {
        auto path = lite::fs::u8path(capturePath);
        capturePopover(popup, path.parent_path() / (path.stem().wstring() + L"-mcp.bmp"));
      }
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(8104, BN_CLICKED), 0);
      expect(actions.back() == lite::McpPopoverAction::copyPrompt &&
                 label(GetDlgItem(popup, 8104)) == L"Copied",
             "MCP popup copy feedback failed");
      pump(1700);
      expect(label(GetDlgItem(popup, 8104)) == L"Copy prompt", "MCP copy feedback did not expire");
      SendMessageW(popup, WM_COMMAND, MAKEWPARAM(8105, BN_CLICKED), 0);
      pump(30);
      expect(actions.back() == lite::McpPopoverAction::advanced && !mcp.window(),
             "MCP advanced action leaked its popup");
    }
    {
      dpi::Scope scale(96);
      auto button =
          CreateWindowExW(0, L"BUTTON", L"Hover fixture", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20,
                          40, 128, 36, parent, nullptr, nullptr, nullptr);
      skin::controlFont(button, 12);
      skin::buttonFont(button, 12);
      auto screen = GetDC(parent), dc = CreateCompatibleDC(screen);
      BITMAPINFO info{};
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = 128;
      info.bmiHeader.biHeight = -36;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      void *raw = nullptr;
      auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
      expect(dc && bitmap && raw, "hover pixel fixture allocation failed");
      auto previous = SelectObject(dc, bitmap);
      dpi::configureDC(dc, 96);
      DRAWITEMSTRUCT item{};
      item.CtlType = ODT_BUTTON;
      item.hwndItem = button;
      item.hDC = dc;
      item.rcItem = {0, 0, 128, 36};
      skin::button(item);
      GdiFlush();
      auto pixels = static_cast<unsigned char *>(raw);
      std::vector<unsigned char> resting(pixels, pixels + 128 * 36 * 4);
      SendMessageW(button, WM_MOUSEMOVE, 0, MAKELPARAM(12, 12));
      skin::button(item);
      GdiFlush();
      expect(!std::equal(resting.begin(), resting.end(), pixels),
             "plain popup button has no visible hover state");
      SendMessageW(button, WM_MOUSELEAVE, 0, 0);
      skin::button(item);
      GdiFlush();
      expect(std::equal(resting.begin(), resting.end(), pixels),
             "popup button hover remains after pointer leave");
      SelectObject(dc, previous);
      DeleteObject(bitmap);
      DeleteDC(dc);
      ReleaseDC(parent, screen);
      DestroyWindow(button);
    }
    lite::fs::remove_all(vaultDir);
    skin::animate(true);
    DestroyWindow(parent);
    skin::shutdown();
    std::cout << "Native control grow/shrink and font reflow passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
