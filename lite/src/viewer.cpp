#include "viewer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
namespace lite {
Json sourceEnvelope(const std::string &source, const std::string &kind, const std::string &path) {
  Json lines = Json::array();
  bool truncated = false;
  size_t begin = 0;
  while (begin <= source.size()) {
    if (lines.size() == 400) {
      truncated = true;
      break;
    }
    auto end = source.find('\n', begin);
    auto line = source.substr(begin, end == source.npos ? source.size() - begin : end - begin);
    if (end != source.npos && !line.empty() && line.back() == '\r')
      line.pop_back();
    lines.push_back(line);
    if (end == source.npos)
      break;
    begin = end + 1;
  }
  Json result = {{"lines", lines}, {"truncated", truncated}, {"kind", kind}};
  if (kind == "src")
    result["path"] = path;
  return result;
}
bool claimedTarget(const Json &row) {
  if (!row.contains("claim") || row["claim"].is_null())
    return false;
  const auto &claim = row["claim"];
  if (claim.is_boolean())
    return claim.get<bool>();
  if (claim.is_number())
    return claim.get<double>() != 0;
  if (claim.is_string())
    return !claim.get<std::string>().empty();
  return true;
}
std::string atlasColor(const Json &row, bool authors, bool nearMiss,
                       const std::map<std::string, std::string> &aliases,
                       const std::map<std::string, std::string> &colors) {
  if (exemptTarget(row))
    return "#a8324a";
  bool matched = row.contains("matched") && row["matched"] == true;
  if (authors) {
    if (!matched)
      return "#b9cadb";
    auto who = row.contains("author") && row["author"].is_string()
                   ? row["author"].get<std::string>()
                   : std::string();
    if (aliases.count(who))
      who = aliases.at(who);
    return !who.empty() && colors.count(who) ? colors.at(who) : "#9aa7b5";
  }
  if (matched)
    return "#3fc45f";
  bool draft = (row.contains("div") && row["div"].is_number()) ||
               (row.contains("srcPath") && row["srcPath"].is_string() &&
                !row["srcPath"].get<std::string>().empty());
  return draft && nearMiss ? "#eab308" : "#b9cadb";
}
void AtlasCamera::clamp(double width, double height) {
  zoom = std::isfinite(zoom) ? std::clamp(zoom, 1., 4096.) : 1.;
  x = std::isfinite(x) ? std::clamp(x, width * (1 - zoom), 0.) : 0.;
  y = std::isfinite(y) ? std::clamp(y, height * (1 - zoom), 0.) : 0.;
}
void AtlasCamera::zoomAt(double factor, double px, double py, double width, double height) {
  const double before = zoom;
  zoom = std::clamp(zoom * factor, 1., 4096.);
  x = px - (px - x) * zoom / before;
  y = py - (py - y) * zoom / before;
  clamp(width, height);
}
void AtlasCamera::center(double px, double py, double width, double height) {
  x = width / 2 - px * zoom;
  y = height / 2 - py * zoom;
  clamp(width, height);
}
ViewRect AtlasCamera::visible(double width, double height) const {
  return {-x / zoom, -y / zoom, width / zoom, height / zoom};
}
std::vector<size_t> marqueeTiles(const std::vector<Tile> &tiles, ViewRect s) {
  if (s.width < 0) {
    s.x += s.width;
    s.width = -s.width;
  }
  if (s.height < 0) {
    s.y += s.height;
    s.height = -s.height;
  }
  std::vector<size_t> selected;
  for (auto t : tiles)
    if (t.width > 0 && t.height > 0 && t.x < s.x + s.width && t.x + t.width > s.x &&
        t.y < s.y + s.height && t.y + t.height > s.y)
      selected.push_back(t.index);
  return selected;
}
void AtlasLod::compute(const std::vector<AtlasFunction> &rows, const std::vector<Tile> &tiles,
                       double width, double height) {
  std::vector<double> areas;
  std::map<std::string, double> grouped;
  for (auto t : tiles) {
    areas.push_back(t.width * t.height);
    if (t.groupArea > 0)
      grouped[t.group] = t.groupArea;
    else if (t.groupArea < 0)
      grouped[rows.at(t.index).module] += t.width * t.height;
  }
  auto median = [](std::vector<double> values) {
    if (values.empty())
      return 1.;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
  };
  std::vector<double> moduleAreas;
  for (auto &m : grouped)
    moduleAreas.push_back(m.second);
  modules = std::max(std::sqrt(width * height / (1.8 * std::max(1., median(moduleAreas)))), 1.6);
  functions =
      std::max(std::sqrt(width * height / (5.5 * std::max(1., median(areas)))), modules * 1.8);
  band = 1;
}
int AtlasLod::update(double z) {
  if (band == 1) {
    if (z >= functions)
      band = 3;
    else if (z >= modules)
      band = 2;
  } else if (band == 2) {
    if (z >= functions)
      band = 3;
    else if (z < modules / 1.15)
      band = 1;
  } else {
    if (z < modules / 1.15)
      band = 1;
    else if (z < functions / 1.15)
      band = 2;
  }
  return band;
}
size_t atlasNeighbor(const std::vector<AtlasFunction> &rows, const std::vector<Tile> &tiles,
                     size_t selected, int dx, int dy) {
  auto current =
      std::find_if(tiles.begin(), tiles.end(), [&](auto t) { return t.index == selected; });
  if (current == tiles.end())
    return selected;
  auto pick = [&](bool sameModule) {
    size_t best = selected;
    double score = 1e100;
    for (auto tile : tiles) {
      if (tile.index == selected ||
          (sameModule && rows[tile.index].module != rows[selected].module))
        continue;
      double vx = tile.x + tile.width / 2 - current->x - current->width / 2,
             vy = tile.y + tile.height / 2 - current->y - current->height / 2;
      double dot = vx * dx + vy * dy, dist = std::hypot(vx, vy);
      if (dot <= 0 || dist < 1e-6)
        continue;
      double candidate = dist * (1 + 2 * (1 - dot / dist));
      if (candidate < score) {
        score = candidate;
        best = tile.index;
      }
    }
    return best;
  };
  auto same = pick(true);
  return same != selected ? same : pick(false);
}
std::string numberedSource(const std::string &source) {
  std::istringstream input(source);
  std::ostringstream out;
  std::string line;
  size_t n = 1;
  while (std::getline(input, line))
    out << n++ << "  " << line << "\n";
  return out.str();
}
} // namespace lite
