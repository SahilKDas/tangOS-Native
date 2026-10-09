#pragma once
#include <windows.h>
namespace lite {
// Paint only this application's window tree, back to front, including owner drawing.
void renderWindowTree(HWND root, HDC target, HWND excluded = nullptr);
} // namespace lite
