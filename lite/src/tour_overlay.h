#pragma once
#include "help.h"
#include "skin.h"
#include <functional>
#include <memory>
#include <optional>
namespace lite {
class TourOverlay {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  using Target = std::function<std::optional<RECT>(const std::string &)>;
  TourOverlay(HWND owner, fs::path data, Target target);
  ~TourOverlay();
  void close();
  bool open() const;
  void resize();
  Json snapshot() const;
  void smoke(const fs::path &directory, const std::function<void(const fs::path &)> &capture);
};
} // namespace lite
