#pragma once
#include <string>
#include <windows.h>
namespace skin {
enum class Icon {
  report,
  refresh,
  settings,
  key,
  minimize,
  maximize,
  close,
  chart,
  document,
  shield,
  branch,
  pullRequest,
  github,
  play,
  stop,
  cart,
  image,
  sparkles
};
enum class PanelStyle { glass, controller, task };
void iconButton(HWND window, Icon icon);
void iconTextButton(HWND window, Icon icon);
void drawIcon(HDC dc, Icon icon, int x, int y, int size);
void policyButton(HWND window, unsigned state);
void roleChip(HWND window);
void compactCombo(HWND window, bool needsRole = false);
void compactEdit(HWND window);
COLORREF compactField();
void rule(HDC dc, int x, int y, int width);
void buttonFont(HWND window, int size, int weight = 700);
void controlFont(HWND window, int size, int weight = 400);
int textWidth(HDC dc, const std::wstring &text, int size, int weight = 400);
COLORREF matched();
void badge(HDC dc, const std::wstring &text, int x, int y, int width, int height);
void invalidateBackdrop(HWND parent);
void initialize();
void shutdown();
void theme(int index);
void animate(bool enabled);
bool animationEnabled();
void advance(bool visible);
COLORREF text();
COLORREF muted();
COLORREF field();
void background(HDC dc, int width, int height, int offsetY = 0, int totalHeight = 0);
void panel(HDC dc, int x, int y, int width, int height, bool solid = false,
           PanelStyle style = PanelStyle::glass);
void scrim(HDC dc, int width, int height);
void agentCard(HDC dc, int x, int y, int width, int height, COLORREF color);
void presenceDot(HDC dc, int x, int y, const std::string &state, int size = 12);
void progressBar(HDC dc, int x, int y, int width, double percent, COLORREF tint);
int taskNoteHeight(HDC dc, const std::wstring &value, int width);
void taskText(HDC dc, const std::wstring &value, int x, int y, int width, int height,
              bool live = false, bool secondary = false);
void taskNote(HDC dc, const std::wstring &value, int x, int y, int width);
void helperPanel(HDC dc, int x, int y, int width, int height);
void tourPanel(HDC dc, int x, int y, int width, int height);
void tourShade(HDC dc, int width, int height, const RECT *spot);
void wrappedLabel(HDC dc, const std::wstring &text, int x, int y, int width, int height, int size,
                  bool bold, COLORREF tint);
int wrappedLabelHeight(HDC dc, const std::wstring &text, int width, int size, bool bold);
void label(HDC dc, const std::wstring &text, int x, int y, int width, int height, int size = 13,
           bool bold = false, bool secondary = false, bool accent = false,
           COLORREF tint = CLR_INVALID, bool italic = false, int weight = 0);
void button(const DRAWITEMSTRUCT &item, bool primary = false, bool danger = false);
void mascot(HDC dc, int x, int y, int size, const std::string &emotion = "idle");
} // namespace skin
