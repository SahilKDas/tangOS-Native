#pragma once
#include "descriptor.h"
namespace lite {
struct Tile {
  size_t index;
  double x, y, width, height;
};
std::vector<Tile> squarify(const std::vector<std::pair<size_t, double>> &items, double x, double y,
                           double width, double height);
std::vector<Tile> atlasLayout(const std::vector<AtlasFunction> &functions,
                              const std::vector<size_t> &indices, double width, double height,
                              const std::string &mode);
} // namespace lite
