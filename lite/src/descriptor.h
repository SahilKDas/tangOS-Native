#pragma once
#include "core.h"
#include "../vendor/json.hpp"
namespace lite {
using Json = nlohmann::json;
struct ToolArg {
  std::string name, type, flag, description;
  bool required = false, positional = false;
  Json value, choices;
};
struct Tool {
  std::string id, label, category, description, docs, command, apply;
  bool readOnly = true;
  std::vector<ToolArg> args;
};
struct Descriptor {
  std::string title, tagline, python = "python", cwd = ".", database = "chaos-db.json";
  Json document;
  std::vector<Tool> tools;
  std::map<std::string, std::string> roles;
  std::vector<std::string> keys;
  const Tool &tool(const std::string &id) const;
  const Tool *role(const std::string &name) const;
};
Descriptor parseDescriptor(const std::string &text);
Descriptor loadDescriptor(const fs::path &repo);
Command toolCommand(const Descriptor &descriptor, const Tool &tool, const Json &values,
                    const fs::path &repo, bool allowWrites, bool allowApply = false);
fs::path confinedPath(const fs::path &root, const std::string &relative);
struct AtlasFunction {
  std::string id, name, module, state;
  uint64_t size = 1;
  Json row;
};
bool exemptTarget(const Json &row);
std::vector<AtlasFunction> parseAtlas(const std::string &text);
std::string redact(std::string text, const std::map<std::string, std::string> &secrets);
} // namespace lite
