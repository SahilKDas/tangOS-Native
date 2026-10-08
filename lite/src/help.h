#pragma once
#include "descriptor.h"
namespace lite {
Json parseGuide(std::string text, bool tour);
Json readGuide(const fs::path &directory, bool tour);
} // namespace lite
