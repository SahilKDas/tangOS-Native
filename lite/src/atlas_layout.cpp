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
        out.push_back({scaled[i].first, cx, y, rw, rh, {}, -1});
        cx += rw;
      }
      y += rh;
      h = std::max(0., h - rh);
    } else {
      double rw = sum / h, cy = y;
      for (size_t i = start; i < end; i++) {
        double rh = scaled[i].second / rw;
        out.push_back({scaled[i].first, x, cy, rw, rh, {}, -1});
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
                              const std::string &mode,
                              const std::map<std::string, std::string> &aliases) {
  using Items = std::vector<std::pair<size_t, double>>;
  auto sortItems = [](Items &items) {
    std::stable_sort(items.begin(), items.end(),
                     [](auto a, auto b) { return a.second > b.second; });
  };
  if (mode == "size") {
    Items items;
    for (auto i : indices)
      items.push_back({i, double(functions.at(i).size)});
    sortItems(items);
    auto out = squarify(items, 0, 0, w, h);
    for (auto &tile : out)
      tile.groupArea = 0;
    return out;
  }
  struct Group {
    std::string key;
    double size = 0;
    Items items;
  };
  std::vector<Group> groups;
  for (auto i : indices) {
    const auto &f = functions.at(i);
    std::string key = f.module;
    if (mode == "match") {
      key = f.state == "matched"  ? "matched"
            : exemptTarget(f.row) ? "no match needed"
            : ((f.row.contains("div") && f.row["div"].is_number()) ||
               (f.row.contains("srcPath") && f.row["srcPath"].is_string() &&
                !f.row["srcPath"].get<std::string>().empty()))
                ? "draft"
                : "unmatched";
    } else if ((mode == "author" || mode == "contributor")) {
      key = f.state == "matched" && f.row.contains("author") && f.row["author"].is_string()
                ? f.row["author"].get<std::string>()
                : std::string();
      if (key.empty())
        key = "unmatched";
      else if (aliases.count(key))
        key = aliases.at(key);
    }
    auto g =
        std::find_if(groups.begin(), groups.end(), [&](const Group &g) { return g.key == key; });
    if (g == groups.end()) {
      groups.push_back({key, 0, {}});
      g = std::prev(groups.end());
    }
    g->size += f.size;
    g->items.push_back({i, double(f.size)});
  }
  if (mode == "match") {
    std::vector<std::string> order = {"unmatched", "draft", "no match needed", "matched"};
    std::stable_sort(groups.begin(), groups.end(), [&](const Group &a, const Group &b) {
      return std::find(order.begin(), order.end(), a.key) <
             std::find(order.begin(), order.end(), b.key);
    });
  } else
    std::stable_sort(groups.begin(), groups.end(),
                     [](const Group &a, const Group &b) { return a.size > b.size; });
  Items sizes;
  for (size_t i = 0; i < groups.size(); ++i) {
    sortItems(groups[i].items);
    sizes.push_back({i, groups[i].size});
  }
  std::vector<Tile> out;
  for (auto group : squarify(sizes, 0, 0, w, h)) {
    auto tiles = squarify(groups[group.index].items, group.x + 1, group.y + 1,
                          std::max(0., group.width - 2), std::max(0., group.height - 2));
    for (auto &tile : tiles) {
      tile.group = groups[group.index].key;
      tile.groupArea = group.width * group.height;
    }
    out.insert(out.end(), tiles.begin(), tiles.end());
  }
  return out;
}
} // namespace lite
