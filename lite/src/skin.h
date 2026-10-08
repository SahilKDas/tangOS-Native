#pragma once
#include <string>
#include <windows.h>
namespace skin {
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
void panel(HDC dc, int x, int y, int width, int height, bool solid = false);
void scrim(HDC dc, int width, int height);
void agentCard(HDC dc, int x, int y, int width, int height, COLORREF color);
void presenceDot(HDC dc, int x, int y, const std::string &state);
void label(HDC dc, const std::wstring &text, int x, int y, int width, int height, int size = 13,
           bool bold = false, bool secondary = false, bool accent = false);
void button(const DRAWITEMSTRUCT &item, bool primary = false, bool danger = false);
void mascot(HDC dc, int x, int y, int size, const std::string &emotion = "idle");
} // namespace skin
