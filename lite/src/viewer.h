#pragma once
#include "atlas_layout.h"
namespace lite {
std::string agentPresence(const std::string &kind, int64_t lastSeen, bool live, int64_t now);
struct ViewRect {
  double x, y, width, height;
};
struct AtlasCamera {
  double zoom = 1, x = 0, y = 0;
  void zoomAt(double factor, double px, double py, double width, double height);
  void clamp(double width, double height);
  void center(double px, double py, double width, double height);
  ViewRect visible(double width, double height) const;
};
std::vector<size_t> marqueeTiles(const std::vector<Tile> &tiles, ViewRect selection);
class AtlasLod {
  double modules = 1.6, functions = 2.88;
  int band = 1;

public:
  void compute(const std::vector<AtlasFunction> &rows, const std::vector<Tile> &tiles, double width,
               double height);
  int update(double zoom);
  double maximum() const { return functions * 12; }
};
size_t atlasNeighbor(const std::vector<AtlasFunction> &rows, const std::vector<Tile> &tiles,
                     size_t selected, int dx, int dy);
std::string atlasColor(const Json &row, bool authors, bool nearMiss,
                       const std::map<std::string, std::string> &aliases = {},
                       const std::map<std::string, std::string> &colors = {});
Json sourceEnvelope(const std::string &source, const std::string &kind,
                    const std::string &path = {});
Json readAtlasCache(const fs::path &path, const std::string &key, int64_t now, int64_t maxAge);
void writeAtlasCache(const fs::path &path, const std::string &key, const Json &database,
                     const Json &extras, int64_t now);
bool claimedTarget(const Json &row);
std::string numberedSource(const std::string &source);
} // namespace lite
