#include "updater.h"
#include "platform.h"
#include <windows.h>
#include <algorithm>
#include <regex>
#include <stdexcept>
namespace lite {
namespace {
bool https(const std::string &url) {
  if (url.rfind("https://", 0) != 0 || url.find_first_of("\r\n\t ") != url.npos)
    return false;
  auto host = url.substr(8, url.find('/', 8) - 8);
  return !host.empty() && host.find_first_of("@?#\\") == host.npos;
}
void executable(const fs::path &path) {
  if (!fs::is_regular_file(path) || fs::file_size(path) >= 50000000 || fs::file_size(path) < 256)
    throw std::runtime_error("Update must be a Windows x64 executable under 50 MB");
  auto bytes = read(path);
  auto word = [&](size_t p) -> unsigned {
    if (p > bytes.size() || bytes.size() - p < 2)
      throw std::runtime_error("Truncated update executable");
    return uint8_t(bytes[p]) | (unsigned(uint8_t(bytes[p + 1])) << 8);
  };
  if (word(0) != 0x5a4d)
    throw std::runtime_error("Update has no Windows executable header");
  size_t pe = word(60) | (size_t(word(62)) << 16);
  if (pe > bytes.size() || bytes.size() - pe < 92 || word(pe) != 0x4550 || word(pe + 2) ||
      word(pe + 4) != 0x8664 || word(pe + 24) != 0x20b || (word(pe + 22) & 0x2000) ||
      !(word(pe + 22) & 0x0002) || word(pe + 92) != 2)
    throw std::runtime_error("Update must be an x64 GUI executable, not a DLL or script");
}
Json readReceipt(const fs::path &data, const fs::path &receipt) {
  auto root = fs::weakly_canonical(data / "updates");
  auto path = fs::weakly_canonical(receipt);
  if (path.parent_path() != root || !fs::is_regular_file(path) || fs::file_size(path) > 65536)
    throw std::runtime_error("Update receipt is outside the local update store");
  auto value = Json::parse(read(path));
  auto candidate = fs::weakly_canonical(fs::u8path(value.at("candidate").get<std::string>()));
  if (candidate.parent_path() != root || candidate == path)
    throw std::runtime_error("Update candidate is outside the local update store");
  return value;
}
void spawn(const fs::path &program, const Args &args, bool visible = false) {
  Args command{utf8(program.wstring())};
  command.insert(command.end(), args.begin(), args.end());
  auto line = commandLine(command);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = visible ? SW_SHOW : SW_HIDE;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, program.parent_path().c_str(), &startup, &process))
    throw std::runtime_error("Cannot launch portable update process");
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
}
} // namespace
Json stagePortableUpdate(const fs::path &data, const Json &release, const std::string &assetPrefix,
                         const fs::path &target, const UpdateFetch &fetch) {
  if (!https(assetPrefix) || assetPrefix.back() != '/')
    throw std::runtime_error(
        "Set an explicit trusted HTTPS assetPrefix ending in / in update.check");
  auto url = release.value("artifactUrl", std::string());
  auto expected = release.value("sha256", std::string());
  if (release.contains("assets"))
    for (auto &asset : release.at("assets"))
      if (asset.value("name", std::string()) == "TangOSLite.exe") {
        url = asset.at("browser_download_url").get<std::string>();
        expected = asset.value("digest", std::string());
        if (expected.rfind("sha256:", 0) == 0)
          expected.erase(0, 7);
        break;
      }
  if (!https(url) || url.rfind(assetPrefix, 0) != 0 ||
      !std::regex_match(expected, std::regex("[0-9a-fA-F]{64}")))
    throw std::runtime_error(
        "Release needs a trusted asset URL and published SHA256; no unchecked update is allowed");
  std::transform(expected.begin(), expected.end(), expected.begin(), ::tolower);
  executable(target);
  HttpResponse response{};
  for (int redirects = 0; redirects < 4; ++redirects) {
    response = fetch(url, "GET", "", {}); // Never forward provider/registry credentials.
    if (response.status >= 300 && response.status < 400) {
      url = response.location;
      if (!https(url) || (url.rfind(assetPrefix, 0) != 0 &&
                          url.rfind("https://release-assets.githubusercontent.com/", 0) != 0 &&
                          url.rfind("https://objects.githubusercontent.com/", 0) != 0))
        throw std::runtime_error("Update redirect left the trusted release hosts");
      continue;
    }
    break;
  }
  if (response.status != 200 || response.body.size() >= 50000000)
    throw std::runtime_error("Portable update download failed or exceeds 50 MB");
  auto store = data / "updates";
  fs::create_directories(store);
  auto id = uniqueId();
  auto candidate = store / (id + ".exe");
  write(candidate, response.body);
  try {
    if (sha256File(candidate) != expected)
      throw std::runtime_error("Update checksum mismatch; current executable is unchanged");
    executable(candidate);
  } catch (...) {
    fs::remove(candidate);
    throw;
  }
  auto receipt = store / (id + ".json");
  Json value{{"candidate", utf8(fs::absolute(candidate).wstring())},
             {"target", utf8(fs::absolute(target).wstring())},
             {"sha256", expected},
             {"targetSha256", sha256File(target)},
             {"version", release.value("version", release.value("tag_name", std::string()))}};
  write(receipt, value.dump(2));
  return {{"state", "downloaded"},
          {"version", value.at("version")},
          {"receipt", utf8(fs::absolute(receipt).wstring())},
          {"target", value.at("target")},
          {"sha256", expected},
          {"bytes", response.body.size()}};
}
Json applyPortableUpdate(const fs::path &data, const fs::path &receipt) {
  auto value = readReceipt(data, receipt);
  auto target = fs::u8path(value.at("target").get<std::string>());
  auto candidate = fs::u8path(value.at("candidate").get<std::string>());
  if (!target.is_absolute() || !fs::is_regular_file(target) ||
      sha256File(target) != value.at("targetSha256") || sha256File(candidate) != value.at("sha256"))
    throw std::runtime_error("Executable changed after update staging; download and review again");
  executable(candidate);
  auto replacement = target.parent_path() / fs::u8path(".TangOSLite-update-" + uniqueId() + ".exe");
  auto backup =
      target.parent_path() / fs::u8path(target.filename().u8string() + ".previous-" + uniqueId());
  fs::copy_file(candidate, replacement, fs::copy_options::none);
  if (sha256File(replacement) != value.at("sha256"))
    throw std::runtime_error("Copied update checksum mismatch");
  if (!ReplaceFileW(target.c_str(), replacement.c_str(), backup.c_str(), 0, nullptr, nullptr)) {
    auto error = GetLastError();
    // ReplaceFile can move the old file to the backup before a later step fails.
    if (!fs::exists(target) && fs::is_regular_file(backup) &&
        sha256File(backup) == value.at("targetSha256")) {
      std::error_code restoreError;
      fs::rename(backup, target, restoreError);
      if (restoreError)
        throw std::runtime_error("Update replacement failed; restore the retained backup: " +
                                 utf8(backup.wstring()));
    }
    std::error_code cleanupError;
    fs::remove(replacement, cleanupError);
    throw std::runtime_error("Portable update replacement failed (Windows error " +
                             std::to_string(error) +
                             "); close other copies and inspect the retained candidate");
  }
  fs::remove(receipt);
  return {
      {"state", "installed"}, {"target", value.at("target")}, {"backup", utf8(backup.wstring())}};
}
void launchPortableUpdate(const fs::path &data, const fs::path &receipt, unsigned long parent,
                          bool restart) {
  readReceipt(data, receipt);
  auto helper = data / "updates" / fs::u8path("helper-" + uniqueId() + ".exe");
  fs::copy_file(fs::u8path(selfExecutable()), helper, fs::copy_options::none);
  spawn(helper, {"--apply-portable-update", utf8(receipt.wstring()), std::to_string(parent),
                 restart ? "restart" : "quit"});
}
int runPortableUpdateHelper(const fs::path &receipt, unsigned long parent, bool restart) {
  auto process = OpenProcess(SYNCHRONIZE, FALSE, parent);
  if (process) {
    auto status = WaitForSingleObject(process, 60000);
    CloseHandle(process);
    if (status != WAIT_OBJECT_0)
      return 1;
  }
  auto data = localData();
  try {
    auto result = applyPortableUpdate(data, receipt);
    write(data / "updates" / "last-result.json", result.dump(2));
    if (restart)
      spawn(fs::u8path(result.at("target").get<std::string>()), {}, true);
    return 0;
  } catch (const std::exception &error) {
    write(data / "updates" / "last-result.json",
          Json{{"state", "error"}, {"message", error.what()}}.dump(2));
    return 1;
  }
}
} // namespace lite
