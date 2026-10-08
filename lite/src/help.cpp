#include "help.h"
#include <regex>
#include <algorithm>
#include <stdexcept>
#include <windows.h>
namespace lite {
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
