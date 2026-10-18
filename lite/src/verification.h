#pragma once
#include "descriptor.h"
namespace lite {
// A successful process exit is not always a complete verification result.
Json checkOutcome(const std::string &name, unsigned long exitCode, const std::string &output);
// Read only bounded semantic summary lines; complete process logs remain on disk.
Json checkOutcomeLog(const std::string &name, unsigned long exitCode, const fs::path &log);
std::string checkOutcomeText(const Json &outcome);
} // namespace lite
