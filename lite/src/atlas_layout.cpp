#include "atlas_layout.h"
#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
namespace lite {
std::vector<Tile> squarify(const std::vector<std::pair<size_t, double>> &items, double x, double y,
                           double w, double h) {
  if (w <= 0 || h <= 0)
    return {};
  double total = 0;
  for (auto &i : items)
    if (i.second > 0)
      total += i.second;
  if (total <= 0)
    return {};
  std::vector<std::pair<size_t, double>> scaled;
  for (auto &i : items)
    if (i.second > 0)
      scaled.push_back({i.first, i.second * w * h / total});
  std::vector<Tile> out;
  size_t start = 0;
  auto worst = [&](size_t begin, size_t end, double shortSide) {
    double sum = 0, mn = std::numeric_limits<double>::infinity(), mx = 0;
    for (size_t i = begin; i < end; i++) {
      sum += scaled[i].second;
      mn = std::min(mn, scaled[i].second);
      mx = std::max(mx, scaled[i].second);
    }
    return std::max(shortSide * shortSide * mx / (sum * sum),
                    sum * sum / (shortSide * shortSide * mn));
  };
  while (start < scaled.size()) {
    double side = std::min(w, h);
    if (side <= 1e-12)
      break;
    size_t end = start + 1;
    while (end < scaled.size() && worst(start, end + 1, side) <= worst(start, end, side))
      ++end;
    double sum = 0;
    for (size_t i = start; i < end; i++)
      sum += scaled[i].second;
    if (w <= h) {
      double rh = sum / w, cx = x;
      for (size_t i = start; i < end; i++) {
        double rw = scaled[i].second / rh;
        out.push_back({scaled[i].first, cx, y, rw, rh});
        cx += rw;
      }
      y += rh;
      h = std::max(0., h - rh);
    } else {
      double rw = sum / h, cy = y;
      for (size_t i = start; i < end; i++) {
        double rh = scaled[i].second / rw;
        out.push_back({scaled[i].first, x, cy, rw, rh});
        cy += rh;
      }
      x += rw;
      w = std::max(0., w - rw);
    }
    start = end;
  }
  return out;
}
std::vector<Tile> atlasLayout(const std::vector<AtlasFunction> &functions,
                              const std::vector<size_t> &indices, double w, double h,
                              const std::string &mode) {
  std::map<std::string, std::vector<std::pair<size_t, double>>> groups;
  for (auto i : indices) {
    auto &f = functions.at(i);
    auto key = mode == "size"     ? "all"
               : mode == "match"  ? f.state
               : mode == "author" ? f.row.value("author", std::string("unattributed"))
                                  : f.module;
    groups[key].push_back({i, (double)f.size});
  }
  std::vector<std::pair<size_t, double>> sizes;
  std::vector<std::vector<std::pair<size_t, double>>> contents;
  for (auto &g : groups) {
    std::sort(g.second.begin(), g.second.end(),
              [](auto &a, auto &b) { return a.second > b.second; });
    double total = 0;
    for (auto &f : g.second)
      total += f.second;
    sizes.push_back({contents.size(), total});
    contents.push_back(g.second);
  }
  std::sort(sizes.begin(), sizes.end(), [](auto &a, auto &b) { return a.second > b.second; });
  std::vector<Tile> out;
  for (auto &group : squarify(sizes, 0, 0, w, h)) {
    auto tiles = squarify(contents[group.index], group.x + 1, group.y + 1,
                          std::max(0., group.width - 2), std::max(0., group.height - 2));
    out.insert(out.end(), tiles.begin(), tiles.end());
  }
  return out;
}
} // namespace lite
