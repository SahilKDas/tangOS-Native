#include "core.h"
#include "platform.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <windows.h>
namespace lite {
std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
  if (!n)
    throw std::runtime_error("Invalid UTF-8");
  std::wstring r(n, 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), r.data(), n);
  return r;
}
std::string utf8(const std::wstring &s) {
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string r(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
  return r;
}
std::string trim(std::string s) {
  auto a = s.find_first_not_of(" \r\n\t");
  if (a == s.npos)
    return {};
  return s.substr(a, s.find_last_not_of(" \r\n\t") - a + 1);
}
std::vector<std::string> split(const std::string &s, char d) {
  std::vector<std::string> v;
  size_t a = 0, b;
  while ((b = s.find(d, a)) != s.npos) {
    v.push_back(s.substr(a, b - a));
    a = b + 1;
  }
  if (a < s.size())
    v.push_back(s.substr(a));
  return v;
}
std::string read(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    return {};
  return {std::istreambuf_iterator<char>(f), {}};
}
void write(const fs::path &p, const std::string &s) {
  if (!p.parent_path().empty())
    fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f || !(f << s))
    throw std::runtime_error("Cannot write " + utf8(p.wstring()));
}
std::wstring quoteWindows(const std::wstring &s) {
  std::wstring r = L"\"";
  size_t back = 0;
  for (auto c : s) {
    if (c == L'\\') {
      ++back;
      continue;
    }
    if (c == L'\"')
      r.append(back * 2 + 1, L'\\');
    else
      r.append(back, L'\\');
    back = 0;
    r += c;
  }
  r.append(back * 2, L'\\');
  return r + L"\"";
}
std::wstring commandLine(const Args &a) {
  std::wstring r;
  for (auto &s : a) {
    if (!r.empty())
      r += L" ";
    r += quoteWindows(wide(s));
  }
  return r;
}
// INI commands are shell-free argv. Quotes group whitespace; backslash is
// literal.
Args tokenize(const std::string &s) {
  Args r;
  std::string t;
  bool q = false, started = false;
  for (char c : s) {
    if (c == '"') {
      q = !q;
      started = true;
    } else if (isspace((unsigned char)c) && !q) {
      if (started) {
        r.push_back(t);
        t.clear();
        started = false;
      }
    } else {
      t += c;
      started = true;
    }
  }
  if (q)
    throw std::runtime_error("Unclosed command quote");
  if (started)
    r.push_back(t);
  return r;
}
std::string preview(const Command &c) { return utf8(commandLine(c.argv)); }
Settings loadSettings(const fs::path &p) {
  Settings s;
  std::string section;
  for (auto &l : split(read(p), '\n')) {
    l = trim(l);
    if (l.empty() || l[0] == '#' || l[0] == ';')
      continue;
    if (l[0] == '[') {
      if (l.back() != ']')
        throw std::runtime_error("Bad INI section");
      section = l.substr(1, l.size() - 2);
      continue;
    }
    auto eq = l.find('=');
    if (eq == l.npos)
      throw std::runtime_error("Bad INI line: " + l);
    auto k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
    if (section == "checks") {
      auto a = tokenize(v);
      if (a.empty())
        throw std::runtime_error("Empty check: " + k);
      s.checks[k] = a;
    } else if (k == "repository")
      s.repository = v;
    else if (k == "theme") {
      if (v.size() != 1 || v[0] < '0' || v[0] > '4')
        throw std::runtime_error("Theme must be 0..4");
      s.themeIndex = v[0] - '0';
    } else if (k == "rom_path")
      s.romPath = v;
    else if (k == "rom_sha256")
      s.romSha256 = v;
    else if (k == "python")
      s.python = v;
    else if (k == "exclusions")
      s.exclusions = v;
    else if (k == "port_only") {
      if (v != "true" && v != "false")
        throw std::runtime_error("port_only must be true or false");
      s.portOnly = v == "true";
    }
  }
  return s;
}
void saveSettings(const fs::path &p, const Settings &s) {
  std::string t = "# TangOS Lite - local settings, no "
                  "credentials\n[settings]\nrepository=" +
                  s.repository + "\npython=" + s.python +
                  "\nport_only=" + (s.portOnly ? std::string("true") : "false") +
                  "\ntheme=" + std::to_string(s.themeIndex) + "\nrom_path=" + s.romPath +
                  "\nrom_sha256=" + s.romSha256 + "\nexclusions=" + s.exclusions + "\n\n[checks]\n";
  for (auto &[k, a] : s.checks) {
    t += k + "=";
    for (auto &v : a) {
      if (v.find('"') != v.npos || v.find('\n') != v.npos)
        throw std::runtime_error("INI argv cannot contain quotes/newlines");
      t += '"' + v + "\" ";
    }
    t += '\n';
  }
  auto temporary = p;
  temporary += fs::u8path(".tmp-" + uniqueId());
  write(temporary, t);
  if (!MoveFileExW(temporary.c_str(), p.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    fs::remove(temporary);
    throw std::runtime_error("Cannot replace settings atomically");
  }
}
bool Change::conflict() const {
  return xy == "DD" || xy == "AU" || xy == "UD" || xy == "UA" || xy == "DU" || xy == "AA" ||
         xy == "UU";
}
std::vector<Change> parseStatus(const std::string &s) {
  auto ts = split(s, '\0');
  std::vector<Change> out;
  for (size_t i = 0; i < ts.size(); ++i) {
    auto &t = ts[i];
    if (t.size() < 4)
      continue;
    Change c{t.substr(0, 2), t.substr(3), {}};
    if (c.xy.find_first_of("RC") != c.xy.npos) {
      if (i + 1 >= ts.size())
        throw std::runtime_error("Truncated rename status");
      c.original = ts[++i];
    }
    out.push_back(c);
  }
  return out;
}
std::string blockedPath(std::string p, const Settings &s) {
  std::replace(p.begin(), p.end(), '\\', '/');
  std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return (char)tolower(c); });
  if (p.empty() || p[0] == '/' || p.find(':') != p.npos)
    return "invalid repository-relative path";
  for (auto &c : split(p, '/'))
    if (c == ".." || c == ".")
      return "path traversal";
  if (s.portOnly && (p == "src" || p.rfind("src/", 0) == 0))
    return "port-only mode excludes src/";
  auto ext = fs::path(p).extension().string();
  if (ext == ".nds" || ext == ".srl" || ext == ".rom" || ext == ".bin" || ext == ".narc" ||
      ext == ".sdat" || ext == ".bmd" || ext == ".bca" || ext == ".pem" || ext == ".key" ||
      ext == ".p12" || ext == ".pyc")
    return "ROM, extracted asset, or credential extension";
  for (auto &part : split(p, '/'))
    if (part == ".git" || part == "__pycache__" || part == "extracted" || part == "roms" ||
        part == "baserom" || part == "nintendo" || part == ".env" || part.rfind(".env.", 0) == 0 ||
        part == "credentials" || part == "id_rsa" || part == "id_ed25519")
      return "protected asset or credential path";
  for (auto x : split(s.exclusions, ';')) {
    x = trim(x);
    std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return (char)tolower(c); });
    while (!x.empty() && x.back() == '/')
      x.pop_back();
    if (!x.empty() && (p == x || p.rfind(x + "/", 0) == 0))
      return "explicit local exclusion";
  }
  return {};
}
std::string blockedBlob(const std::string &b) {
  for (const char *x : {"-----BEGIN PRIVATE KEY-----", "-----BEGIN RSA PRIVATE KEY-----",
                        "-----BEGIN OPENSSH PRIVATE KEY-----", "github_pat_", "ghp_", "AKIA",
                        "-----BEGIN EC PRIVATE KEY-----"})
    if (b.find(x) != b.npos)
      return "recognizable credential in content";
  if (b.size() > 0x160 && b.compare(0xc0, 6, "\x24\xff\xae\x51\x69\x9a", 6) == 0)
    return "Nintendo DS ROM header";
  if (b.size() > 16 * 1024 * 1024)
    return "large blob requires external review (over 16 MiB)";
  return {};
}
bool validRef(const std::string &r) {
  if (r.empty() || r[0] == '-' || r.find("..") != r.npos || r.back() == '/' || r.back() == '.' ||
      r.find("@{") != r.npos || r.find("//") != r.npos || r.find(".lock") != r.npos)
    return false;
  for (unsigned char c : r)
    if (c <= 32 || c == 127 || std::string("~^:?*[\\").find(c) != std::string::npos)
      return false;
  return true;
}
std::vector<Check> discoverChecks(const fs::path &r, const Settings &s) {
  std::vector<Check> v;
  auto py = [&](std::string name, std::string path, Args rest, std::string need) {
    Args a{s.python, path};
    a.insert(a.end(), rest.begin(), rest.end());
    v.push_back({name, need, {a, r}, fs::is_regular_file(r / fs::u8path(path))});
  };
  v.push_back({"ROM verification",
               "Set rom_path and independently trusted rom_sha256 in settings.ini",
               {{selfExecutable(), "--verify-rom", s.romPath, s.romSha256}, r},
               !s.romPath.empty() && s.romSha256.size() == 64});
  py("Byte matching", "tools/rombuild.py", {"--no-rom"},
     "Pinned compiler and user-owned extracted ROM; no ROM output");
  py("Port references", "tools/port_refcheck.py", {}, "Python only");
  py("Declaration agreement", "tools/check_decl_agreement.py", {}, "Python only; baseline ratchet");
  py("Dead references", "tools/check_dead_references.py", {}, "Python only; no --update flags");
  py("Link checks", "tools/prepush_linkcheck.py", {"--range", "origin/main..HEAD"},
     "Compiler and origin/main; configure override for other base");
  v.push_back({"Port build + smoke",
               "MSVC x86, CMake, Ninja, user-owned assets",
               {{"cmd.exe", "/d", "/s", "/c", "port\\build-port.cmd"}, r},
               fs::is_regular_file(r / "port/build-port.cmd")});
  v.push_back({"Smoke tests (built port)",
               "Previously configured build/port",
               {{"ctest", "--test-dir", "build/port", "--output-on-failure"}, r},
               fs::exists(r / "build/port/CTestTestfile.cmake")});
  py("ROM data comparison", "tools/romdata_check.py", {},
     "Compiled data comparison, not ROM identity; needs compiler and local "
     "extraction");
  for (auto &[name, a] : s.checks) {
    auto it = std::find_if(v.begin(), v.end(), [&](auto &c) { return c.name == name; });
    Check c{name, "User-configured argv; review before execution", {a, r}, true};
    if (it != v.end())
      *it = c;
    else
      v.push_back(c);
  }
  return v;
}
} // namespace lite
