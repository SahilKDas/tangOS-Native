#include "verification.h"
#include <algorithm>
#include <cctype>
#include <optional>
#include <limits>
#include <fstream>
#include <array>
namespace lite {
namespace {
std::optional<uint64_t> unsignedCount(const std::string &text, size_t start, size_t end,
                                      bool commas) {
  uint64_t value = 0;
  bool digitSeen = false;
  for (size_t i = start; i < end; ++i) {
    if (commas && text[i] == ',')
      continue;
    if (text[i] < '0' || text[i] > '9')
      return {};
    auto digit = unsigned(text[i] - '0');
    if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
      return {};
    value = value * 10 + digit;
    digitSeen = true;
  }
  return digitSeen ? std::optional<uint64_t>(value) : std::nullopt;
}
std::optional<uint64_t> dataCount(const std::string &text, const std::string &label) {
  for (size_t line = 0; line <= text.size();) {
    auto end = text.find('\n', line);
    if (end == text.npos)
      end = text.size();
    size_t pos = line;
    while (pos < end && (text[pos] == ' ' || text[pos] == '\t'))
      ++pos;
    if (pos + label.size() <= end && text.compare(pos, label.size(), label) == 0) {
      pos += label.size();
      if (pos < end && (text[pos] == ' ' || text[pos] == '\t')) {
        while (pos < end && (text[pos] == ' ' || text[pos] == '\t'))
          ++pos;
        auto start = pos;
        while (pos < end && ((text[pos] >= '0' && text[pos] <= '9') || text[pos] == ','))
          ++pos;
        if (pos > start &&
            (pos == end || text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r'))
          return unsignedCount(text, start, pos, true);
      }
    }
    if (end == text.size())
      break;
    line = end + 1;
  }
  return {};
}
} // namespace
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
    const std::string marker = " disagreement(s)";
    for (size_t at = output.find(marker); at != output.npos;
         at = output.find(marker, at + marker.size())) {
      auto start = at;
      while (start && output[start - 1] >= '0' && output[start - 1] <= '9')
        --start;
      if (start == at)
        continue;
      auto count = unsignedCount(output, start, at, false);
      if (!count)
        result["state"] = "unverified: invalid baseline count";
      else if (*count != 0) {
        result["state"] = "passed-with-baseline";
        result["existingDisagreements"] = *count;
      }
      break;
    }
  }
  if (name == "ROM data comparison") {
    Json counts = Json::object();
    for (auto label : {"VERIFIED", "PARTIAL", "DIFFERS", "UNNAMED"}) {
      if (auto count = dataCount(output, label)) {
        std::string key(label);
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        counts[key] = *count;
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
Json checkOutcomeLog(const std::string &name, unsigned long exitCode, const fs::path &log) {
  if (exitCode != 0)
    return checkOutcome(name, exitCode, "");
  std::ifstream stream(log, std::ios::binary);
  if (!stream)
    return {{"state", "unverified: verification log unavailable"}};
  if (name != "Link checks" && name != "Declaration agreement" && name != "ROM data comparison")
    return checkOutcome(name, exitCode, "");
  std::string line, evidence;
  bool excessive = false;
  auto consume = [&] {
    bool relevant = false;
    if (name == "Link checks")
      relevant = line.find("nothing to verify") != std::string::npos;
    else if (name == "Declaration agreement")
      relevant = line.find("disagreement(s)") != std::string::npos;
    else {
      auto first = line.find_first_not_of(" \t");
      if (first != line.npos)
        for (auto label : {"VERIFIED", "PARTIAL", "DIFFERS", "UNNAMED"})
          if (line.compare(first, std::char_traits<char>::length(label), label) == 0)
            relevant = true;
    }
    if (relevant) {
      if (evidence.size() + line.size() + 1 > 256 * 1024)
        excessive = true;
      else {
        evidence += line;
        evidence += '\n';
      }
    }
    line.clear();
  };
  std::array<char, 8192> buffer;
  while (stream) {
    stream.read(buffer.data(), buffer.size());
    for (std::streamsize i = 0; i < stream.gcount(); ++i) {
      if (buffer[size_t(i)] == '\n')
        consume();
      else if (line.size() < 64 * 1024)
        line += buffer[size_t(i)];
      else
        return {{"state", "unverified: oversized verification line"}};
      if (excessive)
        return {{"state", "unverified: excessive verification summary"}};
    }
  }
  if (stream.bad())
    return {{"state", "unverified: verification log read failed"}};
  if (!line.empty())
    consume();
  if (excessive)
    return {{"state", "unverified: excessive verification summary"}};
  return checkOutcome(name, exitCode, evidence);
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
