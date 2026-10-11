#pragma once
#include <windows.h>

// Layout uses 96-DPI units. Only this boundary converts Win32 geometry and DCs.
namespace dpi {
UINT current();
UINT forWindow(HWND window);
int pixels(int logical, UINT dpi);
int logical(int pixels, UINT dpi);
class Scope {
  UINT previous;

public:
  explicit Scope(UINT value);
  ~Scope();
};
void enable();
void shutdown();
void configureDC(HDC dc, UINT value);
UINT forDC(HDC dc);
ATOM registerClass(const WNDCLASSW *value);
HWND createWindow(DWORD ex, LPCWSTR kind, LPCWSTR title, DWORD style, int x, int y, int width,
                  int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID data);
BOOL clientRect(HWND window, LPRECT bounds);
BOOL windowRect(HWND window, LPRECT bounds);
BOOL relativeRect(HWND window, HWND parent, LPRECT bounds);
BOOL moveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
BOOL setWindowPos(HWND window, HWND order, int x, int y, int width, int height, UINT flags);
int mapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count);
BOOL screenToClient(HWND window, LPPOINT point);
BOOL clientToScreen(HWND window, LPPOINT point);
BOOL cursorPos(LPPOINT point);
HDC beginPaint(HWND window, LPPAINTSTRUCT paint);
HDC getDC(HWND window);
HDC getWindowDC(HWND window);
HDC compatibleDC(HDC source);
HBITMAP compatibleBitmap(HDC source, int width, int height);
BOOL viewportOrigin(HDC dc, int x, int y, LPPOINT previous);
BOOL offsetViewportOrigin(HDC dc, int x, int y, LPPOINT previous);
HRGN roundRegion(int left, int top, int right, int bottom, int ellipseWidth, int ellipseHeight);
HRGN rectRegion(int left, int top, int right, int bottom);
LRESULT sendMessage(HWND window, UINT message, WPARAM w, LPARAM l);
HFONT paintFont(HWND window);
void compactComboHeight(HWND window, int logicalHeight);
BOOL trackPopup(HMENU menu, UINT flags, int x, int y, int reserved, HWND owner, const RECT *bounds);
void applySuggestedRect(HWND window, const RECT &bounds);
namespace testing {
// Deterministic platform seam; never changes Windows display configuration.
void simulateWindowDpi(HWND window, UINT value);
} // namespace testing
} // namespace dpi
