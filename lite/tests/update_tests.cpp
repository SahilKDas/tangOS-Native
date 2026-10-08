#include "updater.h"
#include "platform.h"
#include <iostream>
#include <stdexcept>
using namespace lite;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejected(F operation) {
  bool refused = false;
  try {
    operation();
  } catch (const std::exception &) {
    refused = true;
  }
  require(refused, "Unsafe update was accepted");
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "Pass the GUI executable fixture");
    auto root = fs::temp_directory_path() / fs::u8path("TangOS update " + uniqueId());
    fs::create_directories(root);
    auto target = root / "Portable application.exe";
    fs::copy_file(fs::u8path(argv[1]), target);
    auto original = sha256File(target);
    auto body = read(target) + "update-fixture";
    auto fixture = root / "release.exe";
    write(fixture, body);
    auto hash = sha256File(fixture);
    const std::string prefix = "https://example.test/releases/";
    Json release{
        {"version", "0.17.0"}, {"artifactUrl", prefix + "TangOSLite.exe"}, {"sha256", hash}};
    UpdateFetch fetch = [&](const auto &, const auto &, const auto &, const auto &headers) {
      require(headers.empty(), "Update forwarded credentials");
      return HttpResponse{200, body, "", ""};
    };
    auto stage = [&] {
      return stagePortableUpdate(root / "state", release, prefix, target, fetch);
    };
    auto staged = stage();
    require(sha256File(target) == original, "Staging modified the running target");
    auto installed =
        applyPortableUpdate(root / "state", fs::u8path(staged.at("receipt").get<std::string>()));
    require(sha256File(target) == hash, "Replacement did not install the verified executable");
    require(sha256File(fs::u8path(installed.at("backup").get<std::string>())) == original,
            "Rollback backup is missing");
    rejected([&] {
      applyPortableUpdate(root / "state", fs::u8path(staged.at("receipt").get<std::string>()));
    });
    auto pending = stage();
    write(target, read(target) + "changed");
    rejected([&] {
      applyPortableUpdate(root / "state", fs::u8path(pending.at("receipt").get<std::string>()));
    });
    release["sha256"] = std::string(64, '0');
    rejected(stage);
    release["sha256"] = hash;
    release["artifactUrl"] = "https://attacker.test/TangOSLite.exe";
    rejected(stage);
    release["artifactUrl"] = prefix + "TangOSLite.exe";
    rejected([&] { stagePortableUpdate(root / "state", release, "https:///", target, fetch); });
    UpdateFetch redirect = [&](const auto &, const auto &, const auto &, const auto &) {
      return HttpResponse{302, "", "", "https://attacker.test/release.exe"};
    };
    rejected([&] { stagePortableUpdate(root / "state", release, prefix, target, redirect); });
    write(root / "outside.json", "{}");
    rejected([&] { applyPortableUpdate(root / "state", root / "outside.json"); });
    auto invalid = std::string(512, 'x');
    write(fixture, invalid);
    release["sha256"] = sha256File(fixture);
    UpdateFetch invalidFetch = [&](const auto &, const auto &, const auto &, const auto &) {
      return HttpResponse{200, invalid, "", ""};
    };
    rejected([&] { stagePortableUpdate(root / "state", release, prefix, target, invalidFetch); });
    std::cout << "Portable update staging, replacement, rollback backup and safety tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
