#pragma once
#include "core.h"
#include <windows.h>
namespace lite {
void copyClipboardText(HWND owner, const std::string &text);
fs::path saveClipboardScreenshot(HWND owner, const fs::path &directory);
} // namespace lite
