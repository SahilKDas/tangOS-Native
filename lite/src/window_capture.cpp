#include "window_capture.h"
#include <functional>
#include <string>
#include <vector>
namespace lite {
void renderWindowTree(HWND root, HDC target, HWND excluded) {
  auto initial = SaveDC(target);
  SendMessageW(root, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(target), PRF_CLIENT);
  RestoreDC(target, initial);
  std::function<void(HWND)> children = [&](HWND parent) {
    std::vector<HWND> siblings;
    for (auto child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
      siblings.push_back(child);
    for (auto it = siblings.rbegin(); it != siblings.rend(); ++it) {
      auto child = *it;
      if (child == excluded || !(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE))
        continue;
      RECT bounds{};
      GetWindowRect(child, &bounds);
      MapWindowPoints(nullptr, root, reinterpret_cast<POINT *>(&bounds), 2);
      auto saved = SaveDC(target);
      SetViewportOrgEx(target, bounds.left, bounds.top, nullptr);
      IntersectClipRect(target, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
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
        item.hDC = target;
        item.rcItem = {0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
        SendMessageW(parent, WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
      } else
        SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(target),
                     PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
      if (std::wstring(name) == L"ComboBox" && !GetPropW(child, L"TangOSCompactCombo")) {
        DRAWITEMSTRUCT selection{};
        selection.CtlType = ODT_COMBOBOX;
        selection.CtlID = GetDlgCtrlID(child);
        selection.itemID = UINT(SendMessageW(child, CB_GETCURSEL, 0, 0));
        selection.hwndItem = child;
        selection.hDC = target;
        selection.rcItem = {1, 1, bounds.right - bounds.left - 24, 29};
        SendMessageW(parent, WM_DRAWITEM, selection.CtlID, reinterpret_cast<LPARAM>(&selection));
      }
      RestoreDC(target, saved);
      children(child);
    }
  };
  children(root);
}
} // namespace lite
