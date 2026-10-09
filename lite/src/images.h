#pragma once
#include "core.h"
#include <cstdint>
namespace lite {
struct ScreenshotInfo {
  uint32_t width = 0, height = 0;
  uint64_t bytes = 0;
  std::string format;
};
ScreenshotInfo inspectScreenshot(const fs::path &path);
std::string dibScreenshotBitmap(const std::string &dib);
std::string dibScreenshotPng(const std::string &dib);
} // namespace lite
