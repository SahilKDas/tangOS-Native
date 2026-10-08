#pragma once
#include "descriptor.h"
namespace lite {
std::vector<size_t> atlasOrder(const std::vector<AtlasFunction> &functions,
                               std::vector<size_t> indices, const std::string &sort);
struct Tile {
  size_t index;
  double x, y, width, height;
  std::string group;
  double groupArea = -1;
};
std::vector<Tile> squarify(const std::vector<std::pair<size_t, double>> &items, double x, double y,
                           double width, double height);
std::vector<Tile> atlasLayout(const std::vector<AtlasFunction> &functions,
                              const std::vector<size_t> &indices, double width, double height,
                              const std::string &mode,
                              const std::map<std::string, std::string> &aliases = {});
} // namespace lite
