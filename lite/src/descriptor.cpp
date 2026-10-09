#include "descriptor.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <regex>
#include <stdexcept>
namespace lite {
std::string githubSlug(const std::string &url) {
  auto value = trim(url);
  std::smatch match;
  if (!std::regex_match(
          value, match,
          std::regex(
              R"(^(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/|github\.com[:/]+)([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?/?$)",
              std::regex::icase)))
    return "";
  auto owner = match[1].str(), repo = match[2].str();
  if (owner == "." || owner == ".." || repo == "." || repo == "..")
    return "";
  auto slug = owner + "/" + repo;
  std::transform(slug.begin(), slug.end(), slug.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return slug;
}
fs::path confinedPath(const fs::path &root, const std::string &relative) {
  if (std::any_of(relative.begin(), relative.end(), [](unsigned char c) { return c < 32; }))
    throw std::runtime_error("Control character in repository path");
  auto colon = relative.find(':');
  if (colon != relative.npos && (colon != 1 || !std::isalpha((unsigned char)relative[0]) ||
                                 relative.find(':', colon + 1) != relative.npos))
    throw std::runtime_error("Alternate data streams are not repository paths");
  if (relative.rfind("\\\\?\\", 0) == 0 || relative.rfind("\\\\.\\", 0) == 0)
    throw std::runtime_error("Device paths are not repository paths");
  auto base = fs::weakly_canonical(root);
  auto candidate = (base / fs::u8path(relative)).lexically_normal();
  auto lexical = candidate.lexically_relative(base);
  if (lexical.empty() || lexical.is_absolute() || *lexical.begin() == "..")
    throw std::runtime_error("Path escapes repository: " + relative);
  auto result = fs::weakly_canonical(candidate);
  auto rel = result.lexically_relative(base);
  if (rel.empty() || rel.is_absolute() || (!rel.empty() && *rel.begin() == ".."))
    throw std::runtime_error("Path escapes repository: " + relative);
  return result;
}
const Tool &Descriptor::tool(const std::string &id) const {
  for (auto &t : tools)
    if (t.id == id)
      return t;
  throw std::runtime_error("Unknown descriptor tool: " + id);
}
const Tool *Descriptor::role(const std::string &name) const {
  auto it = roles.find(name);
  return it == roles.end() ? nullptr : &tool(it->second);
}
Descriptor parseDescriptor(const std::string &text) {
  if (text.size() > 8 * 1024 * 1024)
    throw std::runtime_error("Descriptor exceeds 8 MiB");
  Descriptor d;
  d.document = Json::parse(text);
  auto &j = d.document;
  if (j.value("tangosVersion", std::string()) != "1" || !j.contains("project") ||
      !j.contains("tools"))
    throw std::runtime_error("tangos.json requires version 1, project and tools");
  d.title = j.at("project").at("title").get<std::string>();
  d.tagline = j.at("project").value("tagline", std::string());
  auto runtime = j.value("runtime", Json::object());
  if (runtime.value("shell", false))
    throw std::runtime_error("Shell descriptors are unsupported; use explicit argv tools");
  d.python = runtime.value("python", std::string("python"));
  d.cwd = runtime.value("cwd", std::string("."));
  d.keys = runtime.value("envKeys", std::vector<std::string>{});
  d.database = j.value("data", Json::object()).value("dbPath", std::string("chaos-db.json"));
  std::set<std::string> ids;
  for (auto &row : j.at("tools")) {
    Tool t;
    t.id = row.at("id").get<std::string>();
    if (t.id.empty() ||
        t.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != t.id.npos ||
        !ids.insert(t.id).second)
      throw std::runtime_error("Invalid or duplicate tool id: " + t.id);
    t.command = row.at("command").get<std::string>();
    t.readOnly = row.at("readOnly").get<bool>();
    t.apply = row.value("apply", std::string());
    if (t.command.empty() || (!t.apply.empty() && t.readOnly))
      throw std::runtime_error("Invalid command/apply: " + t.id);
    t.label = row.value("label", t.id);
    t.category = row.value("category", std::string("other"));
    t.description = row.value("description", std::string());
    t.docs = row.value("docs", std::string());
    std::set<std::string> names;
    for (auto &a : row.value("args", Json::array())) {
      ToolArg arg;
      arg.name = a.at("name").get<std::string>();
      arg.type = a.at("type").get<std::string>();
      if (!names.insert(arg.name).second ||
          (arg.type != "string" && arg.type != "integer" && arg.type != "number" &&
           arg.type != "boolean" && arg.type != "enum"))
        throw std::runtime_error("Invalid argument in " + t.id);
      arg.flag = a.value("flag", std::string());
      arg.description = a.value("description", std::string());
      arg.required = a.value("required", false);
      arg.positional = a.value("positional", false);
      arg.value = a.value("default", Json());
      arg.choices = a.value("choices", Json::array());
      t.args.push_back(arg);
    }
    d.tools.push_back(t);
  }
  auto generation = j.value("data", Json::object()).value("generate", std::string());
  if (!generation.empty()) {
    Tool generator;
    generator.id = "generate_atlas_data";
    while (ids.count(generator.id))
      generator.id += "_native";
    generator.label = "Refresh Atlas data";
    generator.category = "reporting";
    generator.description = "Run data.generate from this repository's descriptor, then reload "
                            "the generated Atlas. Inspect the command and complete log.";
    generator.command = generation;
    ToolArg output;
    output.name = "out";
    output.type = "string";
    output.description = "Generated database path relative to the repository";
    output.value = d.database;
    output.required = true;
    generator.args.push_back(output);
    d.generatorId = generator.id;
    d.tools.push_back(std::move(generator));
  }
  auto roles = j.value("console", Json::object());
  for (auto it = roles.begin(); it != roles.end(); ++it) {
    auto id = it.value().get<std::string>();
    d.tool(id);
    d.roles[it.key()] = id;
  }
  return d;
}
Descriptor loadDescriptor(const fs::path &repo) {
  return parseDescriptor(read(repo / "tangos.json"));
}
void validateAtlasOutput(const Descriptor &descriptor, const Json &values, const fs::path &repo,
                         const Settings &settings) {
  auto path = confinedPath(repo, values.value("out", descriptor.database));
  auto relative = utf8(path.lexically_relative(fs::weakly_canonical(repo)).wstring());
  auto reason = blockedPath(relative, settings);
  if (!reason.empty())
    throw std::runtime_error("Atlas output is protected: " + relative + " — " + reason);
}
static std::string scalar(const Json &v) { return v.is_string() ? v.get<std::string>() : v.dump(); }
Command toolCommand(const Descriptor &d, const Tool &t, const Json &input, const fs::path &repo,
                    bool writes, bool apply) {
  if (!input.is_object())
    throw std::runtime_error("Arguments must be a JSON object");
  if (!t.readOnly && !writes)
    throw std::runtime_error("Writes are disabled: " + t.id);
  Json values = input;
  Args flags;
  for (auto &a : t.args) {
    Json v = values.contains(a.name) ? values[a.name] : a.value;
    bool missing = v.is_null() || (v.is_string() && v.get<std::string>().empty());
    if (missing) {
      if (a.required)
        throw std::runtime_error("Required argument: " + a.name);
      else
        continue;
    }
    if ((a.type == "integer" && !v.is_number_integer()) || (a.type == "number" && !v.is_number()) ||
        (a.type == "boolean" && !v.is_boolean()) || (a.type == "string" && !v.is_string()))
      throw std::runtime_error("Wrong argument type: " + a.name);
    if (a.type == "enum" && std::find(a.choices.begin(), a.choices.end(), v) == a.choices.end())
      throw std::runtime_error("Invalid choice: " + a.name);
    values[a.name] = v;
    if (a.positional || (a.flag.empty() && !v.is_boolean()))
      flags.push_back(scalar(v));
    else if (!a.flag.empty()) {
      if (v.is_boolean()) {
        if (v.get<bool>())
          flags.push_back(a.flag);
      } else {
        flags.push_back(a.flag);
        flags.push_back(scalar(v));
      }
    }
  }
  if (input.value("apply", false)) {
    if (!writes || !apply || t.apply.empty())
      throw std::runtime_error("Apply requires explicit approval");
    flags.push_back(t.apply);
  }
  Command c{{}, confinedPath(repo, d.cwd)};
  for (auto token : tokenize(t.command)) {
    if (token == "{python}")
      c.argv.push_back(d.python);
    else if (token == "{flags}")
      c.argv.insert(c.argv.end(), flags.begin(), flags.end());
    else if (token.size() > 2 && token.front() == '{' && token.back() == '}') {
      auto key = token.substr(1, token.size() - 2);
      if (values.contains(key) && !values[key].is_null())
        c.argv.push_back(scalar(values[key]));
      else
        throw std::runtime_error("Missing command placeholder: " + key);
    } else if (token.find('{') != token.npos || token.find('}') != token.npos)
      throw std::runtime_error("Use whole-token placeholders: " + token);
    else
      c.argv.push_back(token);
  }
  if (c.argv.empty())
    throw std::runtime_error("Empty tool command");
  return c;
}
bool exemptTarget(const Json &row) {
  return row.contains("noMatch") && !row["noMatch"].is_null() &&
         (!row["noMatch"].is_boolean() || row["noMatch"].get<bool>());
}
std::vector<AtlasFunction> parseAtlas(const std::string &text) {
  if (text.size() > 128 * 1024 * 1024)
    throw std::runtime_error("Atlas exceeds 128 MiB");
  auto j = Json::parse(text);
  std::vector<AtlasFunction> out;
  for (auto &row : j.at("functions")) {
    AtlasFunction f;
    f.id = row.contains("id") ? scalar(row["id"]) : row.value("name", std::string());
    f.name = row.value("name", f.id);
    f.module = row.value("module", std::string("unknown"));
    f.state =
        row.value("matched", false) ? "matched" : row.value("status", std::string("unmatched"));
    if (row.contains("size") && row["size"].is_number_unsigned())
      f.size = std::max<uint64_t>(1, row["size"].get<uint64_t>());
    if (f.state == "unmatched" && row.contains("div") && row["div"].is_number())
      f.state = "near_miss";
    f.row = row;
    out.push_back(f);
  }
  return out;
}
std::string redact(std::string text, const std::map<std::string, std::string> &secrets) {
  for (auto &entry : secrets)
    if (!entry.second.empty()) {
      size_t pos = 0;
      while ((pos = text.find(entry.second, pos)) != text.npos) {
        text.replace(pos, entry.second.size(), "[REDACTED]");
        pos += 10;
      }
    }
  return text;
}
} // namespace lite
