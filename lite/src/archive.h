#pragma once
#include "descriptor.h"
namespace lite {
// Validate the entire archive before creating any destination files.
Json inspectProjectZip(const fs::path &archive, const Settings &settings);
Json extractProjectZip(const fs::path &archive, const fs::path &destination,
                       const Settings &settings);
} // namespace lite
