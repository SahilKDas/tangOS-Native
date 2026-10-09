#pragma once
#include "descriptor.h"
namespace lite {
Json updateStatus(const std::string &current, const Json &release);
std::string supportResultText(const Json &result);
Json updatePresentation(const Json &downloaded, const Json &current);
Json parseGuide(std::string text, bool tour);
Json readGuide(const fs::path &directory, bool tour);
} // namespace lite
