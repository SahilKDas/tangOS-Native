#include "client_setup.h"
#include "mcp.h"
#include <cctype>
#include <windows.h>
namespace lite {
namespace {
Json vscodeConfig(std::string text) {
  bool quoted = false, escaped = false;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
      continue;
    }
    if (c == '"') {
      quoted = true;
      continue;
    }
    if (c != '/' || i + 1 == text.size())
      continue;
    if (text[i + 1] == '/') {
      while (i < text.size() && text[i] != '\n' && text[i] != '\r')
        text[i++] = ' ';
      if (i == text.size())
        break;
    } else if (text[i + 1] == '*') {
      text[i++] = ' ';
      text[i++] = ' ';
      while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
        if (text[i] != '\n' && text[i] != '\r')
          text[i] = ' ';
        ++i;
      }
      if (i + 1 >= text.size())
        throw std::runtime_error("Unterminated client configuration comment");
      text[i++] = ' ';
      text[i] = ' ';
    }
  }
  quoted = escaped = false;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == ',') {
      size_t next = i + 1;
      while (next < text.size() && std::isspace(static_cast<unsigned char>(text[next])))
        ++next;
      if (next < text.size() && (text[next] == '}' || text[next] == ']'))
        text[i] = ' ';
    }
  }
  return Json::parse(text);
}
fs::path environmentPath(const wchar_t *name) {
  wchar_t buffer[32768];
  auto n = GetEnvironmentVariableW(name, buffer, 32768);
  if (!n || n >= 32768)
    throw std::runtime_error("Client configuration environment path unavailable");
  return fs::path(buffer);
}
std::string baseline(const fs::path &path) {
  if (!fs::exists(path))
    return "missing";
  if (!fs::is_regular_file(path) || fs::file_size(path) > 2 * 1024 * 1024)
    throw std::runtime_error("Client config must be a regular JSON file under 2 MiB");
  auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    throw std::runtime_error("Client config reparse points are refused");
  return sha256File(path);
}
} // namespace
fs::path clientConfigPath(const std::string &client) {
  if (client == "Claude Code")
    return environmentPath(L"USERPROFILE") / ".claude.json";
  if (client == "Claude Desktop")
    return environmentPath(L"APPDATA") / "Claude/claude_desktop_config.json";
  if (client == "Cursor")
    return environmentPath(L"USERPROFILE") / ".cursor/mcp.json";
  if (client == "VS Code")
    return environmentPath(L"APPDATA") / "Code/User/mcp.json";
  throw std::runtime_error("Choose a supported MCP client or export its generic setup");
}
ClientSetup previewClientSetup(const std::string &client, const fs::path &executable,
                               const fs::path &connection, const std::string &agent,
                               const fs::path &destination) {
  ClientSetup plan;
  plan.path = destination.empty() ? clientConfigPath(client) : destination;
  if (!plan.path.is_absolute() || plan.path.extension() != ".json")
    throw std::runtime_error("Client setup requires an absolute JSON config path");
  auto entry = mcpClientConfiguration(client, executable, connection, agent);
  auto section = client == "VS Code" ? "servers" : "mcpServers";
  plan.baseline = baseline(plan.path);
  plan.merged = plan.baseline == "missing" ? Json::object()
                : client == "VS Code"      ? vscodeConfig(read(plan.path))
                                           : Json::parse(read(plan.path));
  if (!plan.merged.is_object() ||
      (plan.merged.contains(section) && !plan.merged[section].is_object()))
    throw std::runtime_error(
        "Client config has an invalid server section; repair it before connecting");
  auto old = plan.merged.value(section, Json::object()).value("tangos-lite", Json());
  auto next = entry.at(section).at("tangos-lite");
  auto action = old == next ? "unchanged" : old.is_null() ? "added" : "updated";
  plan.merged[section]["tangos-lite"] = next;
  // Do not expose unrelated settings or credentials in the preview/log.
  plan.outcome = {{"target", client},
                  {"path", utf8(plan.path.wstring())},
                  {"action", action},
                  {"server", next},
                  {"notice", "Preserve all other settings; retain a local backup; restart or "
                             "reload your client afterward."}};
  if (client == "VS Code")
    plan.outcome["notice"] = "Preserve all other settings; save a backup including original "
                             "comments. Write normalized JSON and reload VS Code.";
  return plan;
}
Json installClientSetup(const ClientSetup &plan) {
  if (baseline(plan.path) != plan.baseline)
    throw std::runtime_error("Client configuration changed after preview; review it again");
  if (plan.outcome.at("action") == "unchanged")
    return plan.outcome;
  fs::create_directories(plan.path.parent_path());
  auto backup = plan.path;
  backup += ".tangos-lite-backup-" + uniqueId();
  if (plan.baseline != "missing" && !CopyFileW(plan.path.c_str(), backup.c_str(), TRUE))
    throw std::runtime_error("Cannot back up client configuration; nothing was installed");
  auto temp = plan.path;
  temp += ".tangos-lite-" + uniqueId() + ".tmp";
  write(temp, plan.merged.dump(2) + "\n");
  try {
    if (baseline(plan.path) != plan.baseline)
      throw std::runtime_error(
          "Client configuration changed while preparing the install; review again");
    bool saved = false;
    DWORD error = 0;
    for (int i = 0; i < 20; ++i) {
      if (MoveFileExW(temp.c_str(), plan.path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        saved = true;
        break;
      }
      error = GetLastError();
      if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION &&
          error != ERROR_ACCESS_DENIED)
        break;
      Sleep(50);
    }
    if (!saved)
      throw std::runtime_error("Client config replacement failed (Windows " +
                               std::to_string(error) + "); backup retained");
  } catch (...) {
    std::error_code ignored;
    fs::remove(temp, ignored);
    throw;
  }
  auto result = plan.outcome;
  if (plan.baseline != "missing")
    result["backup"] = utf8(backup.wstring());
  result["installed"] = true;
  return result;
}
} // namespace lite
