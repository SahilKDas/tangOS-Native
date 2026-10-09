#include "clipboard.h"
#include "images.h"
#include "platform.h"
#include <cstring>
#include <stdexcept>
namespace lite {
namespace {
struct Clipboard {
  explicit Clipboard(HWND owner) {
    if (!OpenClipboard(owner))
      throw std::runtime_error("Clipboard is busy; try again");
  }
  ~Clipboard() { CloseClipboard(); }
};
} // namespace
void copyClipboardText(HWND owner, const std::string &text) {
  auto value = wide(text);
  auto memory = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
  if (!memory)
    throw std::runtime_error("Cannot allocate clipboard text");
  auto bytes = GlobalLock(memory);
  if (!bytes) {
    GlobalFree(memory);
    throw std::runtime_error("Cannot lock clipboard text");
  }
  std::memcpy(bytes, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
  GlobalUnlock(memory);
  try {
    Clipboard clipboard(owner);
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory))
      throw std::runtime_error("Cannot copy report to clipboard");
  } catch (...) {
    GlobalFree(memory);
    throw;
  }
}
fs::path saveClipboardScreenshot(HWND owner, const fs::path &directory) {
  std::string bytes;
  {
    Clipboard clipboard(owner);
    UINT format = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : CF_DIB;
    if (!IsClipboardFormatAvailable(format))
      return {};
    auto memory = GetClipboardData(format);
    auto length = GlobalSize(memory);
    if (!memory || length < sizeof(BITMAPINFOHEADER) || length > 160 * 1024 * 1024)
      throw std::runtime_error("Clipboard screenshot is invalid or exceeds 160 MiB");
    auto data = GlobalLock(memory);
    if (!data)
      throw std::runtime_error("Cannot read clipboard screenshot");
    bytes.assign(static_cast<const char *>(data), length);
    GlobalUnlock(memory);
  }
  auto bitmap = dibScreenshotPng(bytes);
  fs::create_directories(directory);
  auto path = directory / ("clipboard-" + uniqueId() + ".png");
  write(path, bitmap);
  try {
    inspectScreenshot(path);
  } catch (...) {
    fs::remove(path);
    throw;
  }
  return path;
}
} // namespace lite
