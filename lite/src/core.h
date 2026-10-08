#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace lite {
namespace fs = std::filesystem;
using Args = std::vector<std::string>;
struct Command {
  Args argv;
  fs::path cwd;
  std::map<std::string, std::string> environment;
  Command() = default;
  Command(Args argv, fs::path cwd, std::map<std::string, std::string> environment = {})
      : argv(std::move(argv)), cwd(std::move(cwd)), environment(std::move(environment)) {}
};
struct Settings {
  std::string repository, activeProject,
      python = "python", exclusions = "local-assets;private;extracted;baserom;roms;assets;nintendo";
  bool portOnly = true;
  int themeIndex = 0;
  std::string romPath, romSha256;
  std::map<std::string, Args> checks;
};
struct Change {
  std::string xy, path, original;
  bool conflict() const;
};
struct Check {
  std::string name, requirement;
  Command command;
  bool available;
};
std::wstring wide(const std::string &s);
std::string utf8(const std::wstring &s);
std::string trim(std::string s);
std::vector<std::string> split(const std::string &s, char delimiter);
std::string read(const fs::path &p);
void write(const fs::path &p, const std::string &text);
std::wstring quoteWindows(const std::wstring &s);
std::wstring commandLine(const Args &args);
Args tokenize(const std::string &text);
std::string preview(const Command &c);
Settings loadSettings(const fs::path &p);
void saveSettings(const fs::path &p, const Settings &s);
std::vector<Change> parseStatus(const std::string &porcelain);
std::string blockedPath(std::string path, const Settings &s);
std::string blockedBlob(const std::string &blob);
bool validRef(const std::string &ref);
std::vector<Check> discoverChecks(const fs::path &repo, const Settings &settings);
} // namespace lite
