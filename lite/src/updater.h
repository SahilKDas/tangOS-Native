#pragma once
#include "descriptor.h"
#include "network.h"
#include <functional>
namespace lite {
using UpdateFetch =
    std::function<HttpResponse(const std::string &, const std::string &, const std::string &,
                               const std::map<std::string, std::string> &)>;
Json stagePortableUpdate(const fs::path &data, const Json &release, const std::string &assetPrefix,
                         const fs::path &target, const UpdateFetch &fetch);
Json applyPortableUpdate(const fs::path &data, const fs::path &receipt);
void launchPortableUpdate(const fs::path &data, const fs::path &receipt, unsigned long parent,
                          bool restart);
int runPortableUpdateHelper(const fs::path &receipt, unsigned long parent, bool restart);
} // namespace lite
