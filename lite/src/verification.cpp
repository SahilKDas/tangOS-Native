#include "verification.h"
#include <algorithm>
#include <cctype>
#include <regex>
namespace lite {
Json checkOutcome(const std::string &name, unsigned long exitCode, const std::string &output) {
  Json result{{"state", exitCode == 0 ? "passed" : "failed"}};
  if (exitCode == 1223) { // Windows ERROR_CANCELLED; portable classifier has no UI dependencies.
    result["state"] = "cancelled";
    return result;
  }
  if (exitCode != 0)
    return result;
  if (name == "Link checks" && output.find("nothing to verify") != output.npos)
    result["state"] = "skipped";
  if (name == "Declaration agreement") {
    std::smatch match;
    if (std::regex_search(output, match, std::regex("([0-9]+) disagreement\\(s\\)"))) {
      try {
        auto count = std::stoull(match[1]);
        if (count != 0) {
          result["state"] = "passed-with-baseline";
          result["existingDisagreements"] = count;
        }
      } catch (...) {
        result["state"] = "unverified: invalid baseline count";
      }
    }
  }
  if (name == "ROM data comparison") {
    Json counts = Json::object();
    for (auto label : {"VERIFIED", "PARTIAL", "DIFFERS", "UNNAMED"}) {
      std::smatch match;
      if (std::regex_search(output, match,
                            std::regex(std::string("(?:^|\\n)[ \\t]*") + label +
                                       "[ \\t]+([0-9,]+)(?:[ \\t\\r\\n]|$)"))) {
        auto number = match[1].str();
        number.erase(std::remove(number.begin(), number.end(), ','), number.end());
        try {
          std::string key(label);
          std::transform(key.begin(), key.end(), key.begin(),
                         [](unsigned char c) { return char(std::tolower(c)); });
          counts[key] = std::stoull(number);
        } catch (...) {
          // Oversized/malformed counts remain absent, hence unverified.
        }
      }
    }
    result["dataCounts"] = counts;
    if (counts.size() != 4)
      result["state"] = "unverified: incomplete data report";
    else if (counts["differs"] != 0)
      result["state"] = "completed-with-differences";
    else if (counts["partial"] != 0 || counts["unnamed"] != 0)
      result["state"] = "completed-with-partial-data";
    else if (counts["verified"] == 0)
      result["state"] = "skipped: no data verified";
  }
  return result;
}
std::string checkOutcomeText(const Json &outcome) {
  auto text = outcome.at("state").get<std::string>();
  if (outcome.contains("dataCounts"))
    text += " " + outcome.at("dataCounts").dump();
  if (outcome.contains("existingDisagreements"))
    text += " (" + outcome.at("existingDisagreements").dump() + " existing disagreements)";
  return "\n[VERIFICATION] " + text + "\n";
}
} // namespace lite
