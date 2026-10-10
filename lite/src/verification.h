#pragma once
#include "descriptor.h"
namespace lite {
// A successful process exit is not always a complete verification result.
Json checkOutcome(const std::string &name, unsigned long exitCode, const std::string &output);
std::string checkOutcomeText(const Json &outcome);
} // namespace lite
