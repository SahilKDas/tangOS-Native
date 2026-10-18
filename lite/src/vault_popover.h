#pragma once
#include "descriptor.h"
#include "fleet.h"
#include <memory>
#include <windows.h>
namespace lite {
class VaultPopover {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  VaultPopover(HWND owner, Vault &vault, std::vector<std::string> declared, Json help);
  ~VaultPopover();
  void show();
  HWND window() const;
};
} // namespace lite