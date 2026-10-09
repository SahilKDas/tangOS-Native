#include "help.h"
#include <regex>
#include <algorithm>
#include <stdexcept>
#include <windows.h>
namespace lite {
std::string supportResultText(const Json &result) {
  if (result.empty())
    return "Check for updates, read the complete reference, or describe a bug to prepare a local "
           "report.";
  if (result.contains("error"))
    return "Operation failed:\n" + result.at("error").dump() +
           "\n\nReview your Connections and local requirements, then retry.";
  if (result.value("requiresConfirmation", false))
    return "Review this preview, then confirm to proceed:\n\n" +
           result.value("details", Json::object()).dump(2);
  if (result.value("state", std::string()) == "downloaded")
    return "Update " + result.value("version", std::string()) +
           " is downloaded and checksum-verified.\nRestart and update to install. "
           "A recovery copy is retained.";
  if (result.contains("update")) {
    const auto &update = result.at("update");
    if (update.value("state", std::string()) == "none")
      return "You are up to date. Running version: " +
             update.value("currentVersion", std::string());
    if (update.value("state", std::string()) == "available")
      return "Version " + update.value("version", std::string()) +
             " is available.\nOpen the release page or preview a download. "
             "Downloads require your trusted publisher configuration and a published SHA256.";
  }
  if (result.contains("markdown"))
    return result.at("markdown").get<std::string>();
  return result.dump(2);
}
Json updateStatus(const std::string &current, const Json &release) {
  if (!release.is_object())
    throw std::runtime_error("Update endpoint must return a JSON release object");
  auto version = release.value("version", release.value("tag_name", std::string()));
  auto numbers = [](std::string value) {
    if (!value.empty() && value[0] == 'v')
      value.erase(0, 1);
    if (!std::regex_match(value, std::regex("[0-9]+(\\.[0-9]+){1,3}")))
      throw std::runtime_error("Update version must be numeric x.y.z");
    std::vector<uint64_t> out;
    for (auto &part : split(value, '.'))
      out.push_back(std::stoull(part));
    out.resize(4);
    return out;
  };
  auto newer = numbers(version) > numbers(current);
  auto url = release.value("html_url", release.value("url", std::string()));
  if (!url.empty() && (url.rfind("https://", 0) != 0 || url.substr(8).find('@') != url.npos ||
                       url.find_first_of("\r\n ") != url.npos))
    throw std::runtime_error("Release URL must be credential-free HTTPS");
  return {{"state", newer ? "available" : "none"},
          {"currentVersion", current},
          {"version", version},
          {"releaseUrl", url},
          {"installation", "Portable release: configure trusted update downloads, verify the "
                           "published SHA256, then restart to install"}};
}
Json parseGuide(std::string text, bool tour) {
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  std::regex blocks("\\n\\s*\\n"), emotion("\\[(\\w[\\w-]*)\\]"), target("@([\\w-]+)");
  Json rows = Json::array();
  for (std::sregex_token_iterator it(text.begin(), text.end(), blocks, -1), end; it != end; ++it) {
    std::vector<std::string> lines;
    for (auto line : split(it->str(), '\n')) {
      auto t = trim(line);
      if (t.rfind("#", 0) == 0 || (tour && t.empty()))
        continue;
      lines.push_back(line);
    }
    if (lines.empty())
      continue;
    Json row = Json::object();
    std::smatch em, tg;
    auto first = trim(lines.front());
    if (tour) {
      row["emotion"] = "smile";
      bool hasEm = std::regex_search(first, em, emotion),
           hasTarget = std::regex_search(first, tg, target);
      auto leftover = trim(std::regex_replace(std::regex_replace(first, emotion, ""), target, ""));
      if ((hasEm || hasTarget) && leftover.empty()) {
        if (hasEm)
          row["emotion"] = em[1].str();
        if (hasTarget)
          row["target"] = "[data-tour=\"" + tg[1].str() + "\"]";
        lines.erase(lines.begin());
      }
      if (lines.empty())
        continue;
      row["title"] = trim(lines.front());
    } else {
      std::regex prefix("^\\[(\\w[\\w-]*)\\]\\s*(.*)$");
      if (std::regex_match(first, em, prefix)) {
        row["emotion"] = em[1].str();
        first = trim(em[2].str());
      }
      row["title"] = first;
    }
    std::string body;
    for (size_t i = 1; i < lines.size(); ++i) {
      if (i > 1)
        body += ' ';
      body += lines[i];
    }
    row["body"] = trim(body);
    if (!row["title"].get<std::string>().empty())
      rows.push_back(row);
  }
  return rows;
}
Json readGuide(const fs::path &directory, bool tour) {
  auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(tour ? 212 : 213), RT_RCDATA);
  if (!resource)
    throw std::runtime_error("Embedded help resource missing");
  std::string defaults((const char *)LockResource(LoadResource(nullptr, resource)),
                       SizeofResource(nullptr, resource));
  auto path = directory / (tour ? "tango-tour.txt" : "tango-tips.txt");
  if (!fs::exists(path))
    write(path, defaults);
  if (fs::file_size(path) > 1024 * 1024)
    throw std::runtime_error("Help file exceeds 1 MiB; use a smaller local guide");
  auto rows = parseGuide(read(path), tour);
  return rows.empty() ? parseGuide(defaults, tour) : rows;
}
} // namespace lite
