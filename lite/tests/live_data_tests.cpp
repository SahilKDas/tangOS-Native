#include "backend.h"
#include <windows.h>
#include "live_data.h"
#include "platform.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace lite;
int main(int argc, char **argv) {
  if (argc == 6 && std::string(argv[1]) == "--watch") {
    try {
      Backend backend(fs::u8path(argv[2]), fs::u8path(argv[3]), Settings{},
                      {{"FIXTURE_KEY", "fixture-only"}});
      std::atomic<bool> stop{false};
      Json events = Json::array();
      auto start = GetTickCount64();
      std::thread worker([&] {
        backend.streamAtlasEvents(stop, [&](Json e) { events.push_back(std::move(e)); });
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[5])));
      auto cancel = GetTickCount64();
      stop = true;
      worker.join();
      write(fs::u8path(argv[4]), Json{{"events", events},
                                      {"cancelMs", GetTickCount64() - cancel},
                                      {"elapsedMs", GetTickCount64() - start}}
                                     .dump());
      return 0;
    } catch (const std::exception &e) {
      std::cerr << e.what();
      return 1;
    }
  }
  auto root = fs::temp_directory_path() / ("tangos-live-" + uniqueId());
  int assertions = 0;
  auto check = [&](bool value, const char *why) {
    ++assertions;
    if (!value)
      throw std::runtime_error(why);
  };
  try {
    SseDecoder decoder;
    std::string wire =
        ": heartbeat\r\n\r\nevent: counts\r\ndata: {\"totals\":{\"日本語\":2}}\r\n\r\ndata: "
        "broken\n\nevent: progress\ndata: {\n"
        "data: \"ready\":false}\n\n";
    Json events = Json::array();
    for (char c : wire)
      for (auto &e : decoder.feed(std::string(1, c)))
        events.push_back(e);
    check(events.size() == 2 && events[0]["event"] == "counts" &&
              events[1]["data"]["ready"] == false,
          "fragmented UTF8/CRLF/multiline/heartbeat parsing");
    bool rejected = false;
    try {
      decoder.feed(std::string(2 * 1024 * 1024 + 1, 'x'));
    } catch (...) {
      rejected = true;
    }
    check(rejected, "unframed SSE has a memory bound");
    check(reuseLiveAtlas(1000, 20999, true) && !reuseLiveAtlas(1000, 21000, true) &&
              reuseLiveAtlas(1000, 60999, false) && !reuseLiveAtlas(1000, 61000, false) &&
              !reuseLiveAtlas(1000, 999, false) && !reuseLiveAtlas(0, 1000, false),
          "force/passive TTL boundaries");
    auto repo = root / "project A", data = root / "data";
    fs::create_directories(repo / "src");
    Json desc{{"tangosVersion", "1"},
              {"project", {{"name", "a"}, {"title", "A"}}},
              {"tools", Json::array()}};
    write(repo / "tangos.json", desc.dump());
    Json local{
        {"functions",
         Json::array({{{"name", "f"}, {"id", "arm9:1"}, {"matched", true}, {"custom", 7}}})}};
    write(repo / "chaos-db.json", local.dump());
    int requests = 0;
    bool offline = false;
    auto http = [&](const std::string &url, const std::string &, const std::string &,
                    const std::map<std::string, std::string> &headers) {
      ++requests;
      check(headers.at("Authorization") == "Bearer fixture-only", "only supplied user key is used");
      if (offline)
        throw std::runtime_error("fixture offline");
      if (url.find("contributors") != url.npos)
        return HttpResponse{200, "[{\"login\":\"alice\",\"contributions\":2},{\"login\":\"ci[bot]"
                                 "\",\"contributions\":9}]"};
      if (url.find("commits") != url.npos)
        return HttpResponse{200, "[{\"author\":{\"login\":\"alice\"},\"commit\":{\"author\":{"
                                 "\"email\":\"42+Alice@users.noreply.github.com\"}}}]"};
      if (url.find("pulls") != url.npos)
        return HttpResponse{200,
                            "[{\"user\":{\"login\":\"alice\"}},{\"user\":{\"login\":\"alice\"}}]"};
      return HttpResponse{
          200,
          Json{{"ready", true},
               {"functions",
                Json::array(
                    {{{"name", "f"}, {"id", "arm9:1"}, {"matched", false}, {"noMatch", true}}})}}
              .dump()};
    };
    Backend backend(repo, data, Settings{}, {{"FIXTURE_KEY", "fixture-only"}}, http);
    auto confirm = [&](const std::string &method, Json args) {
      auto preview = backend.invoke(method, args);
      args["confirmation"] = preview.at("confirmation");
      return backend.invoke(method, args);
    };
    check(backend.invoke("atlas.current") == local && requests == 0,
          "current loads local data without connecting");
    Json profile{
        {"url", "https://fixture.invalid/live"}, {"enabled", true}, {"keyEnv", "FIXTURE_KEY"}};
    confirm("connections.set", {{"atlas.live", profile}});
    auto live = backend.invoke("atlas.live");
    check(live["data"]["functions"][0]["noMatch"] == true && requests == 1, "live database fetch");
    check(backend.invoke("atlas.load")["functions"][0]["matched"] == true &&
              backend.invoke("atlas.load")["functions"][0]["custom"] == 7,
          "overlay preserves local matches and metadata");
    check(backend.invoke("atlas.live", {{"force", true}})["cached"] == true && requests == 1,
          "force observes short throttle");
    confirm("atlas.acceptEvent", {{"event", "published"}, {"data", Json::object()}});
    check(backend.invoke("atlas.live")["cached"] == false && requests == 2,
          "publication invalidates TTL immediately");
    confirm("atlas.acceptEvent",
            {{"event", "progress"},
             {"data",
              {{"ready", true}, {"stats", Json::object()}, {"matched", Json::array({"arm9:1"})}}}});
    check(backend.invoke("atlas.state")["extras"]["atlas.progress"]["ready"] == true,
          "SSE progress persisted across consumers");
    confirm("atlas.acceptEvent", {{"event", "published"}, {"data", Json::object()}});
    offline = true;
    check(backend.invoke("atlas.live")["stale"] == true, "offline last-good fallback");
    offline = false;
    Backend another(repo, data, Settings{}, {{"FIXTURE_KEY", "fixture-only"}}, http);
    check(another.invoke("atlas.state")["hasLive"] == true,
          "durable cache across backend instances");
    Backend rotated(repo, data, Settings{}, {{"FIXTURE_KEY", "different"}}, http);
    check(rotated.invoke("atlas.state")["hasLive"] == false,
          "credential rotation isolates cached data");
    auto other = root / "project B";
    fs::create_directories(other);
    write(other / "tangos.json", desc.dump());
    Backend otherBackend(other, data, Settings{}, {{"FIXTURE_KEY", "fixture-only"}}, http);
    check(otherBackend.invoke("atlas.state")["hasLive"] == false,
          "project isolation despite same descriptor");
    profile["url"] = "https://fixture.invalid/repos/user/project";
    profile["mode"] = "github-api";
    confirm("connections.set", {{"atlas.live", profile}, {"github.credits", profile}});
    auto credits = backend.invoke("github.credits");
    auto before = requests;
    check(credits["data"]["keyToLogin"]["Alice"] == "alice" &&
              credits["data"]["logins"].size() == 1 && credits["data"]["prAuthors"].size() == 1,
          "GitHub aliases, bot filter and PR deduplication");
    check(backend.invoke("github.credits")["cached"] == true && requests == before,
          "credits reused until forced");
    offline = true;
    check(backend.invoke("github.credits", {{"force", true}})["warnings"].size() == 3,
          "partial API failures are reported");
    offline = false;
    check(backend.invoke("atlas.refresh")["authorized"] == false,
          "unapproved generator never executes");
    write(
        repo / "generator.py",
        "import json,pathlib\npathlib.Path('chaos-db.json').write_text(json.dumps({'functions':[], "
        "'generated':True}))\n");
    desc["data"] = {{"generate", "python generator.py"}};
    write(repo / "tangos.json", desc.dump());
    fs::last_write_time(repo / "chaos-db.json",
                        fs::file_time_type::clock::now() - std::chrono::hours(1));
    write(repo / "src/input.cpp", "fixture\n");
    check(backend.invoke("atlas.refreshStatus")["stale"] == true,
          "existing source changes make data stale");
    confirm("atlas.authorizeRefresh", Json::object());
    check(backend.invoke("atlas.refresh")["refreshed"] == true &&
              backend.invoke("atlas.load")["generated"] == true,
          "approved stale generator executes and invalidates local cache");
    write(repo / "generator.py", "raise RuntimeError('changed command')\n");
    check(backend.invoke("atlas.refreshStatus")["authorized"] == false,
          "changed generator requires new approval");
    confirm("atlas.authorizeRefresh", {{"enabled", false}});
    check(backend.invoke("atlas.refresh")["authorized"] == false, "approval can be revoked");
    fs::remove_all(root);
    std::cout << assertions << " live-state assertions passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << "; fixture=" << utf8(root.wstring()) << '\n';
    return 1;
  }
}