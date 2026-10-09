#include "backend.h"
#include "images.h"
#include "activity.h"
#include "controller_view.h"
#include "help.h"
#include <windows.h>
#include "repository.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
using namespace lite;
int main() {
  auto dir = fs::temp_directory_path() / fs::u8path("lite-backend-" + uniqueId());
  fs::create_directories(dir / "repo/tools");
  auto repo = dir / "repo", data = dir / "data";
  int count = 0;
  auto expect = [&](bool ok, const std::string &msg) {
    ++count;
    if (!ok)
      throw std::runtime_error(msg);
  };
  auto reject = [&](auto action, const std::string &msg) {
    bool threw = false;
    try {
      action();
    } catch (...) {
      threw = true;
    }
    expect(threw, msg);
  };
  try {
    expect(controllerProgress(0, 100, 0) == 0 && controllerProgress(0, 100, 300) == 100,
           "Controller CSS progress endpoints");
    expect(std::abs(controllerProgress(0, 100, 150) - 80.2403) < .001,
           "Controller CSS ease midpoint matches cubic-bezier");
    expect(controllerProgress(80, 20, -1) == 80 && controllerProgress(80, 20, 301) == 20,
           "progress reset and clamped transition duration");
    {
      Json prefs = Json::object();
      auto tips = Json::array(
          {{{"title", "One"}, {"body", "First"}}, {{"title", "Two"}, {"body", "Second"}}});
      HelperState helper(prefs, tips, currentAnnouncement(), true);
      expect(helper.open && helper.unread && helper.messages().size() == 3,
             "first-run helper opens with announcement before tips");
      helper.close(prefs);
      expect(!helper.open && !helper.unread && prefs.value("tourSeen", false),
             "closing marks first-run helper read");
      expect(prefs.at("updateNoteSeen") == currentAnnouncement().at("id"),
             "announcement read ID persists");
      expect(helper.messages().size() == 3,
             "read announcement remains available during same session");
      helper.next(-1);
      expect(helper.index == 2, "helper previous wraps to final tip");
      helper.next(1);
      expect(helper.index == 0, "helper next wraps to announcement");
      helper.toggle(prefs);
      expect(helper.open && !helper.unread, "reopening preserves read state");
      HelperState restarted(prefs, tips, currentAnnouncement(), false);
      expect(!restarted.open && !restarted.unread && restarted.messages().size() == 2,
             "read announcement does not nag after restart");
      auto newNote = currentAnnouncement();
      newNote["id"] = "future-release";
      HelperState updated(prefs, tips, newNote, false);
      expect(!updated.open && updated.unread && updated.messages().size() == 3,
             "new release restores unread badge without opening overlay");
      HelperState empty(Json::object(), Json::array(), Json::object(), false);
      empty.next(-1);
      empty.next(1);
      expect(empty.index == 0 && empty.messages().empty(), "empty tips navigation is safe");
    }
    auto downloaded = Json{{"state", "downloaded"}, {"version", "2.0.0"}, {"receipt", "fixture"}};
    auto available = Json{{"update", {{"state", "available"}, {"version", "2.0.0"}}}};
    expect(updatePresentation(downloaded, available) == downloaded,
           "checking same version preserves downloaded restart presentation");
    available["update"]["version"] = "2.0.1";
    expect(updatePresentation(downloaded, available) == available,
           "newer release is not masked by older download");
    auto updateError = Json{{"error", "offline"}};
    expect(updatePresentation(downloaded, updateError) == updateError,
           "download receipt does not hide failed update checks");
    expect(supportResultText({{"update", {{"state", "none"}, {"currentVersion", "1.2.3"}}}})
                   .find("up to date") != std::string::npos,
           "verified current update is shown as up to date");
    expect(supportResultText({{"error", "offline"}}).find("up to date") == std::string::npos,
           "failed checks never appear up to date");
    expect(supportResultText({{"update", {{"state", "available"}, {"version", "2.0.0"}}}})
                   .find("published SHA256") != std::string::npos,
           "available update explains required download trust");
    expect(supportResultText({{"state", "downloaded"}, {"version", "2.0.0"}})
                   .find("Restart and update") != std::string::npos,
           "downloaded update has an actionable restart state");
    {
      ActivityBus bus;
      std::string largeOutput(190000, 'x');
      largeOutput += "\nport/fixture.cpp:42: latest diagnostic\n";
      for (int i = 0; i < 50; ++i)
        bus.publish({{"kind", "run-started"},
                     {"run",
                      {{"runId", std::to_string(i)},
                       {"repository", utf8(repo.wstring())},
                       {"source", "ai"},
                       {"client", {{"name", "Fixture"}}},
                       {"startedAt", i / 2},
                       {"label", std::to_string(i)},
                       {"status", "finished"},
                       {"output", largeOutput}}}});
      bus.publish({{"kind", "run-started"},
                   {"run",
                    {{"runId", "other"},
                     {"repository", utf8((dir / "other").wstring())},
                     {"source", "ai"},
                     {"client", {{"name", "Fixture"}}},
                     {"startedAt", 100},
                     {"status", "running"},
                     {"output", "wrong repository"}}}});
      auto compact = bus.controllerSnapshot(utf8(repo.wstring()));
      expect(compact.size() == 1 && compact[0].at("label") == "49" &&
                 compact[0].at("output").get<std::string>().size() <= 1600,
             "Controller telemetry selects latest tied run and bounds output copying");
      auto agent = Json{{"name", "Fixture"}, {"stats", Json::object()}};
      expect(controllerView(agent, Json::array(), compact) ==
                 controllerView(agent, Json::array(), bus.snapshot(utf8(repo.wstring()))),
             "bounded Controller cache preserves original latest-line semantics");
      expect(bus.snapshot(utf8(repo.wstring()))[0].at("output") == largeOutput,
             "Controller cache does not truncate retained complete activity");
    }
    Runner git;
    expect(git.run({{"git", "init", "-b", "main"}, repo}).code == 0, "init");
    write(repo / "README.md", "backend fixture\n");
    git.run({{"git", "add", "README.md"}, repo});
    expect(git.run({{"git", "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                     "commit", "-m", "initial"},
                    repo})
                   .code == 0,
           "initial commit");
    Json desc = {{"tangosVersion", "1"},
                 {"project", {{"name", "fixture"}, {"title", "Backend fixture"}}},
                 {"tools", Json::array()}};
    write(repo / "tangos.json", desc.dump());
    Settings settings;
    int requests = 0;
    Backend backend(repo, data, settings, {{"USER_KEY", "local-fixture-secret"}},
                    [&](auto &url, auto &method, auto &body, auto &headers) {
                      ++requests;
                      expect(headers.at("Authorization") == "Bearer local-fixture-secret",
                             "local credentials supplied");
                      return HttpResponse{200,
                                          "{\"claims\":[{\"module\":\"arm9\",\"start\":33554432,"
                                          "\"end\":33554448}],\"echo\":\"local-fixture-secret\"}"};
                    });
    // Agent policy reads may overlap another agent's short statistics transaction.
    auto heldLock = CreateFileW((data / "backend.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    expect(heldLock != INVALID_HANDLE_VALUE, "hold backend transaction fixture");
    reject([&] { backend.invoke("preferences.get"); }, "external operations remain fail-fast");
    reject([&] { backend.invoke("preferences.get", Json::object(), 20); },
           "internal contention wait is bounded");
    std::thread releaseLock([heldLock] {
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
      CloseHandle(heldLock);
    });
    Json contendedRead;
    try {
      contendedRead = backend.invoke("preferences.get", Json::object(), 1000);
    } catch (...) {
      releaseLock.join();
      throw;
    }
    releaseLock.join();
    expect(contendedRead.is_object(), "fleet policy read waits for completion transaction");
    auto confirmed = [&](const std::string &method, Json args) {
      auto preview = backend.invoke(method, args);
      expect(preview.value("requiresConfirmation", false), "write preview");
      args["confirmation"] = preview.at("confirmation");
      return backend.invoke(method, args);
    };
    expect(!enabledTool({{"allowNearMiss", false}}, "nearmiss_stats"),
           "near-miss tool policy denied");
    expect(enabledTool({{"allowNearMiss", false}}, "check"),
           "ordinary tools unaffected by near-miss policy");
    expect(!enabledTool({{"disabledTools", Json::array({"check"})}}, "check"),
           "explicit tool disable");
    reject([&] { confirmed("preferences.set", {{"allowNearMiss", "false"}}); },
           "matching policy type validation");
    reject([&] { confirmed("preferences.set", {{"disabledTools", Json::array({42})}}); },
           "disabled tool type validation");
    for (auto targets : Json::array({-1, 1000000001, 1.5, "8"}))
      reject(
          [&] {
            backend.invoke("policy.drive",
                           {{"cases", Json::array({{{"agent", {{"name", "Requesty"}, {"jobs", 3}}},
                                                    {"preferences", {{"useAgents", true}}},
                                                    {"targets", targets}}})}});
          },
          "driver policy rejects invalid target counts");
    auto requesty = backend.invoke(
        "policy.drive",
        {{"cases", Json::array({{{"agent", {{"name", "Requesty"}, {"jobs", 3}}},
                                 {"preferences", {{"useAgents", true}, {"agentFanout", 8.9}}},
                                 {"targets", 16}}})}});
    expect(requesty[0]["jobs"] == 1 && requesty[0]["functionsPerAgent"] == 8 &&
               requesty[0]["subAgents"] == 2,
           "Requesty remains serial while fanout groups functions per sub-agent");
    expect(backend.invoke("projects.list").empty(), "empty registry");
    confirmed("projects.register", {{"id", "fixture"}, {"repository", utf8(repo.wstring())}});
    expect(backend.invoke("projects.list").size() == 1, "registered local project");
    confirmed("projects.register", {{"id", "remote"}, {"descriptor", desc}});
    expect(backend.invoke("projects.list").size() == 2, "remote no-clone descriptor");
    auto remoteView = confirmed("projects.open", {{"id", "remote"}});
    auto viewPath = fs::u8path(remoteView.at("path").get<std::string>());
    expect(remoteView.at("cloned") == false && !fs::exists(viewPath / ".git") &&
               loadDescriptor(viewPath).title == "Backend fixture",
           "remote project opens a confined metadata-only view without cloning");
    expect(requests == 0, "project registration and opening send no external requests");
    {
      auto discoveryData = dir / "discovery";
      fs::create_directories(discoveryData);
      write(discoveryData / "connections.json",
            Json{{"projects.registry",
                  {{"enabled", true},
                   {"allowDescriptorDownloads", true},
                   {"url", "https://registry.example.invalid/projects"},
                   {"keyEnv", "USER_KEY"}}}}
                .dump());
      int descriptorRequests = 0;
      bool offline = false;
      Backend discovery(
          repo, discoveryData, settings, {{"USER_KEY", "local-fixture-secret"}},
          [&](auto &url, auto &, auto &, auto &headers) {
            if (offline)
              return HttpResponse{503, "offline"};
            if (url == "https://registry.example.invalid/projects") {
              expect(headers.at("Authorization") == "Bearer local-fixture-secret",
                     "registry uses only user-owned credentials");
              return HttpResponse{
                  200, Json{{"projects",
                             Json::array({{{"id", "remote-discovered"},
                                           {"title", "Published project"},
                                           {"glyph", "DX"},
                                           {"github", "https://github.com/fixture/project.git"}}})}}
                           .dump()};
            }
            ++descriptorRequests;
            expect(url == "https://raw.githubusercontent.com/fixture/project/HEAD/tangos.json" &&
                       headers.empty(),
                   "descriptor URL derived without forwarding registry secrets");
            return HttpResponse{200, desc.dump()};
          });
      auto run = [&](const std::string &method, Json args = Json::object()) {
        auto preview = discovery.invoke(method, args);
        args["confirmation"] = preview.at("confirmation");
        return discovery.invoke(method, args);
      };
      auto preview = discovery.invoke("projects.discover");
      expect(descriptorRequests == 0 && !fs::exists(discoveryData / "projects.json"),
             "discovery preview makes no external request or registry changes");
      expect(run("projects.discover").at("discovered") == 1,
             "registry discovery merges remote project");
      expect(discovery.invoke("projects.list")[0].at("glyph") == "DX",
             "registry discovery retains published project glyph");
      Json target{{"id", "remote-discovered"}};
      expect(!run("projects.download", target).at("cached").get<bool>() && descriptorRequests == 1,
             "download and validate remote descriptor");
      expect(run("projects.download", target).at("cached") == true && descriptorRequests == 1,
             "24 hour fresh descriptor cache makes no network request");
      auto registry = Json::parse(read(discoveryData / "projects.json"));
      registry[0]["github"] = "git@github.com:Fixture/Project.git";
      write(discoveryData / "projects.json", registry.dump(2));
      target["force"] = true;
      expect(!run("projects.download", target).at("cached").get<bool>() && descriptorRequests == 2,
             "SSH GitHub project descriptors use normalized public HTTPS without credentials");
      offline = true;
      target["force"] = true;
      expect(run("projects.download", target).at("stale") == true,
             "offline refresh preserves descriptor and reports stale cache");
      reject([&] { run("projects.discover"); }, "offline registry refresh reports failure");
      expect(discovery.invoke("projects.list").size() == 1,
             "offline registry never discards remembered projects");
      auto opened = run("projects.open", {{"id", "remote-discovered"}});
      expect(opened.at("cloned") == false && opened.at("descriptor") == desc,
             "downloaded descriptor opens in viewer-only mode without a checkout");
    }
    reject(
        [&] {
          confirmed("projects.register", {{"id", "remote\nport_only=false"}, {"descriptor", desc}});
        },
        "remote identifier cannot inject local configuration");
    auto remoteAgain = confirmed("projects.open", {{"id", "remote"}});
    expect(remoteAgain.at("path") == remoteView.at("path"), "remote metadata view path is stable");
    auto localOpen = confirmed("projects.open", {{"id", "fixture"}});
    expect(localOpen.at("cloned") == true &&
               fs::equivalent(fs::u8path(localOpen.at("path").get<std::string>()), repo),
           "registered local project opens the actual checkout");
    auto projectRows = backend.invoke("projects.list");
    expect(projectRows[0]["cloned"] == true && projectRows[1]["cloned"] == false &&
               projectRows[1]["path"].is_null(),
           "project menu summaries distinguish local and viewer-only entries");
    reject(
        [&] {
          confirmed("projects.register", {{"id", "bad-remote"}, {"descriptor", Json::object()}});
        },
        "invalid remote descriptor cannot become an executable project");

    Runner cloneRunner;
    Backend cloneBackend(repo, data, settings, {}, requestHttp, &cloneRunner);
    Json cloneArgs{{"url", utf8(repo.wstring())}};
    auto clonePreview = cloneBackend.invoke("git.clone", cloneArgs);
    cloneArgs["confirmation"] = clonePreview.at("confirmation");
    cloneRunner.cancel();
    auto cancelledClone = cloneBackend.invoke("git.clone", cloneArgs);
    expect(cancelledClone.value("cancelled", false) && cancelledClone.at("exit") == ERROR_CANCELLED,
           "clone respects caller cancellation");
    expect(!fs::exists(fs::u8path(cancelledClone.at("repository").get<std::string>())) &&
               read(fs::u8path(cancelledClone.at("log").get<std::string>())).find("CANCELLED") !=
                   std::string::npos,
           "cancelled clone launches no process and retains a complete log");
    cloneRunner.reset();
    cloneArgs.erase("confirmation");
    clonePreview = cloneBackend.invoke("git.clone", cloneArgs);
    cloneArgs["confirmation"] = clonePreview.at("confirmation");
    auto localClone = cloneBackend.invoke("git.clone", cloneArgs);
    expect(!localClone.value("cancelled", true) && localClone.at("exit") == 0 &&
               fs::exists(fs::u8path(localClone.at("repository").get<std::string>()) / ".git"),
           "clone runner reset permits a disposable local clone");
    auto destination = dir / "custom clone destination";
    auto cloneCustom = confirmed(
        "git.clone", {{"url", utf8(repo.wstring())}, {"destination", utf8(destination.wstring())}});
    expect(cloneCustom.at("exit") == 0 && fs::exists(destination / ".git"),
           "clone uses an explicit destination with spaces");
    reject(
        [&] {
          confirmed("git.clone",
                    {{"url", utf8(repo.wstring())}, {"destination", utf8(destination.wstring())}});
        },
        "clone refuses to overwrite an existing folder");
    reject([&] { confirmed("git.clone", {{"url", "ext::powershell arbitrary"}}); },
           "external clone helpers rejected");
    auto bugReport = confirmed("bug.report", {{"description", "Fixture report"}});
    expect(fs::exists(fs::u8path(bugReport.at("folder").get<std::string>()) / "bug-report.md") &&
               bugReport.at("markdown").get<std::string>().find("local-fixture-secret") ==
                   std::string::npos,
           "local report includes reviewable diagnostics without credential values");
    BITMAPFILEHEADER imageHeader{};
    BITMAPINFOHEADER imageInfo{};
    imageHeader.bfType = 0x4d42;
    imageHeader.bfOffBits = sizeof(imageHeader) + sizeof(imageInfo);
    imageHeader.bfSize = imageHeader.bfOffBits + 4;
    imageInfo.biSize = sizeof(imageInfo);
    imageInfo.biWidth = imageInfo.biHeight = 1;
    imageInfo.biPlanes = 1;
    imageInfo.biBitCount = 24;
    imageInfo.biSizeImage = 4;
    std::string image(reinterpret_cast<char *>(&imageHeader), sizeof(imageHeader));
    image.append(reinterpret_cast<char *>(&imageInfo), sizeof(imageInfo));
    image.append("\x12\x34\x56\0", 4);
    auto screenshot = dir / "fixture screenshot.bmp";
    auto orderedStats = parseStatisticsJson(
        R"({"bySize":{">0x800":{"attempts":4,"matches":2},"<=0x40":{"attempts":4,"matches":2}}})");
    expect(orderedStats.at("bySizeOrder") == Json::array({">0x800", "<=0x40"}) &&
               sizeRecommendation(orderedStats.at("bySize"), orderedStats.at("bySizeOrder")) ==
                   "Strongest on >0x800 (50% hit); weakest on <=0x40 (50%).",
           "equal-rate size recommendation retains original insertion order");
    expect(sizeRecommendation(orderedStats.at("bySize"),
                              Json::array({"missing", ">0x800", ">0x800", 42})) ==
               "Strongest on >0x800 (50% hit); weakest on <=0x40 (50%).",
           "size recommendation ignores duplicate and invalid order metadata");
    Json bestOrder = Json::object();
    auto recordedStats =
        updateAgentStats(Json::object(),
                         Json::array({{{"name", "large-order"}, {"size", 4096}, {"matched", false}},
                                      {{"name", "small-order"}, {"size", 16}, {"matched", false}}}),
                         bestOrder);
    expect(recordedStats.at("bySizeOrder") == Json::array({">0x800", "<=0x40"}),
           "recorded size buckets preserve first-observation order");
    write(screenshot, image);
    expect(dibScreenshotBitmap(image.substr(sizeof(imageHeader))) == image,
           "clipboard DIB converts to a native BMP without changing pixel data");
    reject([&] { dibScreenshotBitmap("short"); }, "truncated clipboard DIB rejected");
    auto pngPath = dir / "clipboard-compression.png";
    write(pngPath, dibScreenshotPng(image.substr(sizeof(imageHeader))));
    expect(inspectScreenshot(pngPath).format == "png", "native clipboard PNG compression");
    auto largeInfo = imageInfo;
    largeInfo.biWidth = 3840;
    largeInfo.biHeight = 2160;
    largeInfo.biBitCount = 32;
    largeInfo.biSizeImage = 3840 * 2160 * 4;
    std::string largeDib(reinterpret_cast<const char *>(&largeInfo), sizeof(largeInfo));
    largeDib.append(largeInfo.biSizeImage, '\0');
    write(pngPath, dibScreenshotPng(largeDib));
    auto largeScreenshot = inspectScreenshot(pngPath);
    expect(largeScreenshot.width == 3840 && largeScreenshot.height == 2160 &&
               largeScreenshot.bytes < 16 * 1024 * 1024,
           "4K clipboard screenshot compresses below the attachment limit");
    auto info = inspectScreenshot(screenshot);
    expect(info.width == 1 && info.height == 1 && info.format == "bmp",
           "native screenshot decoder");
    Json reportArgs = {
        {"description", "Screenshot fixture local-fixture-secret"},
        {"screenshots", Json::array({utf8(screenshot.wstring()), utf8(screenshot.wstring())})}};
    auto reportPreview = backend.invoke("bug.report", reportArgs);
    expect(reportPreview.dump().find("local-fixture-secret") == std::string::npos,
           "known credentials are redacted before saving the report confirmation");
    expect(reportPreview.at("details").at("screenshots").size() == 1 &&
               reportPreview.at("details").at("screenshots")[0].at("width") == 1,
           "report preview validates and deduplicates screenshot attachments");
    image.back() = '\x01';
    write(screenshot, image);
    reportArgs["confirmation"] = reportPreview.at("confirmation");
    reject([&] { backend.invoke("bug.report", reportArgs); },
           "changed screenshot invalidates report preview");
    reportArgs.erase("confirmation");
    activityBus().publish({{"kind", "run-started"},
                           {"run",
                            {{"runId", "report-fixture"},
                             {"repository", utf8(repo.wstring())},
                             {"toolId", "check"},
                             {"label", "local-fixture-secret"},
                             {"output", "private output"},
                             {"commandPreview", "private command"},
                             {"status", "running"}}}});
    auto withShot = confirmed("bug.report", reportArgs);
    auto reportFolder = fs::u8path(withShot.at("folder").get<std::string>());
    auto reportDebug = read(reportFolder / "debug.json");
    expect(read(reportFolder / "screenshot-1.bmp") == image &&
               withShot.at("screenshots").size() == 1,
           "confirmed local report preserves the selected screenshot");
    expect(reportDebug.find("local-fixture-secret") == std::string::npos &&
               reportDebug.find("private output") == std::string::npos &&
               reportDebug.find("private command") == std::string::npos &&
               reportDebug.find("fixture screenshot") == std::string::npos,
           "report activity omits credentials, commands, output and original attachment paths");
    reject([&] { backend.invoke("bug.report", {{"description", "  "}}); },
           "empty report description rejected");
    auto invalidImage = dir / "invalid.png";
    write(invalidImage, "this is not an image");
    reject(
        [&] {
          backend.invoke("bug.report",
                         {{"description", "Invalid fixture"},
                          {"screenshots", Json::array({utf8(invalidImage.wstring())})}});
        },
        "non-image attachment rejected");
    write(invalidImage, image);
    reject([&] { inspectScreenshot(invalidImage); }, "image container must match its extension");
    reject([&] { inspectScreenshot(fs::path("relative.bmp")); },
           "relative screenshot path rejected");
    {
      write(repo / "tools/port_refcheck.py",
            "import time\nfrom pathlib import Path\nprint('tool-started', flush=True)\n"
            "time.sleep(30)\nPath('port/finished.txt').write_text('finished')\n");
      auto toolDescriptor = desc;
      toolDescriptor["tools"] = Json::array({{{"id", "cancel_fixture"},
                                              {"label", "Cancel fixture"},
                                              {"category", "verification"},
                                              {"readOnly", true},
                                              {"command", "{python} tools/port_refcheck.py"},
                                              {"args", Json::array()}}});
      write(repo / "tangos.json", toolDescriptor.dump());
      Runner toolRunner;
      std::string streamed;
      Backend cancellable(repo, dir / "cancel-tools", settings, {}, requestHttp, &toolRunner,
                          [&](const std::string &text) {
                            streamed += text;
                            if (text.find("tool-started") != text.npos)
                              toolRunner.cancel();
                          });
      auto run = [&](const std::string &method, Json args) {
        auto preview = cancellable.invoke(method, args);
        expect(preview.at("details").contains("command"), "tool/check preview contains exact argv");
        args["confirmation"] = preview.at("confirmation");
        return cancellable.invoke(method, args);
      };
      toolRunner.cancel();
      auto before = run("checks.run", {{"name", "Port references"}});
      expect(before.at("cancelled") == true && before.at("exit") == ERROR_CANCELLED &&
                 !fs::exists(repo / "port/finished.txt"),
             "check cancellation before launch reaches backend runner");
      expect(read(fs::u8path(before.at("log").get<std::string>())).find("CANCELLED") !=
                 std::string::npos,
             "pre-launch cancellation preserves complete log");
      toolRunner.reset();
      auto during = run("tools.run", {{"tool", "cancel_fixture"}});
      expect(during.at("cancelled") == true && during.at("exit") == ERROR_CANCELLED &&
                 !fs::exists(repo / "port/finished.txt") &&
                 streamed.find("tool-started") != streamed.npos,
             "streamed tool cancellation terminates running process before later side effects");
      expect(read(fs::u8path(during.at("log").get<std::string>())).find("tool-started") !=
                 std::string::npos,
             "running cancellation preserves output in durable log");
      write(repo / "tangos.json", desc.dump());
    }
    expect(!backend.invoke("descriptor.preview")["tools"].is_null(),
           "descriptor generation preview");
    reject([&] { confirmed("connections.set", {{"API_KEY", "credential"}}); },
           "plaintext credential refused");
    confirmed("connections.set", {{"claims",
                                   {{"enabled", false},
                                    {"url", "https://user.example.invalid/claims"},
                                    {"keyEnv", "USER_KEY"}}}});
    reject([&] { backend.invoke("network.read", {{"connection", "claims"}}); },
           "disabled connection");
    expect(requests == 0, "zero requests before user enables");
    confirmed("connections.set", {{"claims",
                                   {{"enabled", true},
                                    {"url", "https://user.example.invalid/claims"},
                                    {"keyEnv", "USER_KEY"}}}});
    auto response = backend.invoke("network.read", {{"connection", "claims"}});
    expect(response["ok"] == true &&
               response.dump().find("local-fixture-secret") == std::string::npos,
           "redacted remote response");
    auto claims = backend.invoke("claims.read", {{"connection", "claims"}});
    expect(heldTarget({{"module", "arm9"}, {"addr", 33554440}, {"size", 8}}, claims),
           "remote overlap protected");
    expect(!heldTarget({{"module", "ov1"}, {"addr", 33554440}}, claims), "module scoped claim");
    auto local =
        parseClaimRows("| range | owner | since | state |\n| 0x02000000 func_02000000 | Alice | "
                       "now | **active** |\n| 0x02000020 | Bob | now | **released** |");
    expect(heldTarget({{"addr", 33554432}}, local), "markdown active claim");
    expect(!heldTarget({{"addr", 33554464}}, local), "released claim ignored");
    write(repo / "CLAIMS.md", "| 0x02000000 | Alice | now | **partial** |\n");
    write(repo / "queue.jsonl", "{\"name\":\"held\",\"addr\":33554432}\n{\"id\":\"ready\",\"name\":"
                                "\"ready\"}\n{\"id\":\"ready\",\"name\":\"ready\"}\n");
    expect(confirmed("queue.adopt", {{"path", "queue.jsonl"}})["targets"].size() == 1,
           "queue vetting and duplicate exclusion");
    fs::create_directories(repo / "src");
    write(repo / "src/fixture.cpp", "int fixture=1;\n");
    expect(backend.invoke("atlas.source", {{"path", "src/fixture.cpp"}})["source"] ==
               "int fixture=1;\n",
           "read source in port-only mode");
    reject([&] { backend.invoke("atlas.source", {{"path", "../secret"}}); },
           "source traversal denied");
    fs::create_directories(repo / "private");
    write(repo / "private/local.cpp", "int excluded_fixture=1;\n");
    reject([&] { backend.invoke("atlas.source", {{"path", "src/../private/local.cpp"}}); },
           "normalized source path preserves local exclusions");
    expect(
        backend.invoke("atlas.source", {{"id", "missing"}, {"srcPath", "src/../private/local.cpp"}})
            .is_null(),
        "protected source cannot bypass guard through id lookup");
    fs::create_directories(repo / "config");
    write(repo / "config/match_attempts.jsonl",
          "{\"name\":\"f\",\"module\":\"arm9\",\"addr\":33554432,\"attemptId\":\"a1\"}\ninvalid\n");
    expect(backend.invoke("atlas.history",
                          {{"name", "f"}, {"module", "arm9"}, {"addr", 33554432}})["attempts"]
                   .size() == 1,
           "attempt filtering tolerant corrupt row");
    write(
        repo / "config/match_attempts.jsonl",
        R"({"functionId":"arm9:0x2000000","attemptId":"b","parentAttemptId":"a","model":"fixture","c_source":"PRIVATE BODY","loggedAt":"private time","divergences":"5"}
{"name":"f","module":"arm9","addr":33554432,"attemptId":"a","parentAttemptId":null,"base":{"kind":"clean"},"matchProvenance":{"model":"reference"}}
)");
    auto history =
        backend.invoke("atlas.history", {{"name", "f"}, {"module", "arm9"}, {"addr", 33554432}});
    expect(history["attempts"].size() == 2 && history["attempts"][0]["attemptId"] == "a" &&
               history["attempts"][1]["depth"] == 1,
           "history id aliases and parent ordering");
    expect(history.dump().find("PRIVATE BODY") == std::string::npos &&
               history.dump().find("private time") == std::string::npos,
           "history excludes C bodies and private timestamps");
    expect(history["attempts"][0]["model"] == "reference" &&
               history["attempts"][1]["divergences"] == 5,
           "provenance and numeric string normalization");
    expect(classifySource("dcd 0x11223344") == "transcribed", "transcription rejected");
    expect(classifySource("dcd 0x11223344\nNONMATCHING") == "ok", "declared draft accepted");
    expect(adaptiveRole("Hard matcher", 8, 0, {{"score", 1}, {"refinerSupply", 1}}) == "Random",
           "adaptive demotion");
    expect(adaptiveRole("Refiner", 0, 0, {{"score", 1}, {"refinerSupply", 0}}) == "Drafter",
           "refiner supply fallback");
    auto ticket = backend.invoke("preferences.set", {{"disabledTools", Json::array({"unsafe"})}});
    reject(
        [&] {
          backend.invoke("preferences.set", {{"disabledTools", Json::array({"different"})},
                                             {"confirmation", ticket["confirmation"]}});
        },
        "confirmation arguments bound");
    confirmed("preferences.set", {{"disabledTools", Json::array({"unsafe"})}});
    expect(backend.invoke("preferences.get")["disabledTools"][0] == "unsafe",
           "persist preferences");
    auto report = confirmed("reports.export", Json::object());
    write(data / "stats.json", Json({{"fixture", {{"attempts", 8}}}}).dump());
    write(data / "stats-best.json", Json({{"fixture", {{"function", 5}}}}).dump());
    auto clearPreview = backend.invoke("stats.clear");
    expect(!backend.invoke("stats.get").empty(), "stats clear preview preserves tallies");
    backend.invoke("stats.clear", {{"confirmation", clearPreview.at("confirmation")}});
    expect(backend.invoke("stats.get").empty() && backend.invoke("stats.session").empty() &&
               Json::parse(read(data / "stats-best.json")).empty(),
           "confirmed stats clear wipes lifetime, session and divergence history");
    expect(read(fs::u8path(report["path"].get<std::string>())).find("local-fixture-secret") ==
               std::string::npos,
           "export no credentials");
    expect(backend.invoke("git.status")["status"].get<std::string>().find("main") !=
               std::string::npos,
           "backend status");
    reject(
        [&] {
          backend.invoke("connections.set",
                         {{"bad", {{"url", "https://user:credential@example.invalid/"}}}});
        },
        "credentials in URL refused before preview");
    auto originalCount = requests;
    auto writeConfig = backend.invoke("connections.get");
    writeConfig["lease"] = {{"enabled", true},
                            {"url", "https://user.example.invalid/try-lock"},
                            {"method", "POST"},
                            {"keyEnv", "USER_KEY"}};
    confirmed("connections.set", writeConfig);
    auto writePreview =
        backend.invoke("network.write", {{"connection", "lease"}, {"body", {{"module", "arm9"}}}});
    expect(requests == originalCount, "network write waits for confirmation");
    backend.invoke("network.write", {{"connection", "lease"},
                                     {"body", {{"module", "arm9"}}},
                                     {"confirmation", writePreview["confirmation"]}});
    expect(requests == originalCount + 1, "user confirmed network write");
    write(repo / "same-status.txt", "before");
    auto stale = backend.invoke("preferences.set", {{"reports", true}});
    write(repo / "same-status.txt", "after");
    reject(
        [&] {
          backend.invoke("preferences.set",
                         {{"reports", true}, {"confirmation", stale["confirmation"]}});
        },
        "changed content invalidates confirmation despite same status");
    reject([&] { confirmed("preferences.set", {{"autoPush", true}}); },
           "every push requires review");
    auto backupPreview = backend.invoke("git.backup");
    expect(!fs::exists(data / "backups"), "backup preview has no side effects");
    auto backup = backend.invoke("git.backup", {{"confirmation", backupPreview["confirmation"]}});
    expect(fs::exists(fs::u8path(backup["backup"].get<std::string>()) / "manifest.json"),
           "recoverable backup manifest");
    auto results = data / "results.jsonl";
    write(results,
          "{\"name\":\"good\",\"matched\":true,\"divergences\":0,\"c_source\":\"int f(){return "
          "1;}\"}\n{\"name\":\"bad\",\"matched\":true,\"c_source\":\"dcd 0x12345678\"}\n");
    backend.recordAgent("fixture", results, "review", 1);
    expect(backend.invoke("stats.get")["fixture"]["declaredMatches"] == 1,
           "transcriptions never receive match credit");
    expect(backend.invoke("harvest.list").size() == 1, "harvest recovery record");
    backend.recordAgent("fixture", results, "review", 1);
    auto uniqueStats = backend.invoke("stats.get")["fixture"];
    expect(uniqueStats["declaredMatches"] == 1 && uniqueStats["attempts"] == 2,
           "repeated verification counts unique functions once");
    expect(backend.invoke("stats.session")["fixture"]["attempts"] == 2,
           "session statistics independently deduplicate repeated attempts");
    auto hydrated = backend.invoke("stats.get");
    hydrated["previous-launch"] = uniqueStats;
    hydrated["previous-launch"]["attemptedFuncs"].push_back("earlier");
    hydrated["previous-launch"]["attempts"] = 3;
    write(data / "stats.json", hydrated.dump(2));
    backend.recordAgent("previous-launch", results, "review", 1);
    expect(backend.invoke("stats.get")["previous-launch"]["attempts"] == 3 &&
               backend.invoke("stats.session")["previous-launch"]["attempts"] == 2,
           "this-session tally counts revisited functions without inflating lifetime statistics");
    backend.resetRecent("fixture");
    expect(backend.invoke("stats.get")["fixture"]["recent"].empty(),
           "adaptive demotion resets old-rung misses");
    expect(productiveDriver({{"landedNames", Json::array({"real"})}}),
           "driver landed summary productive");
    expect(!productiveDriver(
               {{"landedNames", Json::array({"bad"})}, {"sources", {{"bad", "dcd 0x12345678"}}}}),
           "transcribed driver summary cannot reset exhaustion streak");
    expect(productiveDriver({{"nearMisses", Json::array({{{"name", "draft"}}})}}),
           "compiling near-miss summary resets exhaustion streak");
    expect(!productiveDriver({{"worked", 1}}), "generic worked count is not matching productivity");
    auto aliases = driverResultRows({{"inputTokens", 47}, {"outputTokens", 12}});
    auto aliasBest = Json::object();
    auto aliasStats = updateAgentStats(Json::object(), aliases, aliasBest);
    expect(aliasStats["tokensIn"] == 47 && aliasStats["tokensOut"] == 12 &&
               aliasStats["attempts"] == 0,
           "token-only driver summaries normalize aliases without phantom attempts");
    auto fallback = driverResultRows({{"landedNames", Json::array({"real", "asm"})},
                                      {"sources", {{"asm", "dcd 0x12345678"}}},
                                      {"tokensOut", nullptr},
                                      {"outputTokens", nullptr},
                                      {"tokensPerLanded", 9}});
    expect(fallback.back().value("tokensOut", 0) == 9,
           "null token totals fall back to real landed count after transcription gate");
    auto noLanded =
        driverResultRows({{"results", Json::array({{{"name", "real"}, {"matched", true}}})},
                          {"tokensPerLanded", 9}});
    expect(noLanded.size() == 1 && !noLanded[0].contains("tokensOut"),
           "matched result rows alone do not invent declared landed token costs");
    auto summary = data / "driver-summary.json";
    write(summary,
          Json({{"results", Json::array({{{"name", "real"}, {"matched", true}},
                                         {{"name", "transcribed"}, {"matched", true}},
                                         {{"name", "miss"}, {"matched", false}}})},
                {"sources", {{"real", "int real(){return 1;}"}, {"transcribed", "dcd 0x12345678"}}},
                {"inputTokens", 25},
                {"outputTokens", 10}})
              .dump());
    backend.recordAgent("summary", summary, "review", 1);
    auto summaryStats = backend.invoke("stats.get")["summary"];
    expect(summaryStats["attempts"] == 3 && summaryStats["declaredMatches"] == 1 &&
               summaryStats["tokensIn"] == 25 && summaryStats["tokensOut"] == 10,
           "actual driver summary ingests target rows, token aliases and transcription gate");
    expect(!summaryStats["attemptedFuncs"].empty() && summaryStats["attemptedFuncs"].size() == 3,
           "summary metadata does not create phantom unnamed attempts");
    Json best = Json::object();
    auto statsFixture = updateAgentStats(
        Json::object(),
        Json::array({{{"name", "late"}, {"matched", false}, {"size", 64}, {"divergences", 8}},
                     {{"name", "late"}, {"matched", false}, {"size", 64}, {"divergences", 4}},
                     {{"name", "late"}, {"matched", true}, {"size", 64}, {"divergences", 0}},
                     {{"name", "far"}, {"matched", false}, {"size", 64}, {"divergences", 14}},
                     {{"name", "tokens"}, {"matched", true}, {"tokensIn", 25}, {"tokensOut", 10}}}),
        best);
    expect(statsFixture["attempts"] == 3 && statsFixture["declaredMatches"] == 2,
           "late first win deduplicated attempts");
    expect(statsFixture["nearMisses"] == 1 && statsFixture["recent"].size() == 4,
           "near-miss closeness and late-win recent form");
    expect(statsFixture["bySize"]["<=0x40"]["attempts"] == 2 &&
               statsFixture["tokensPerMatch"] == 18,
           "reference size buckets and rounded token cost");

    auto profiles = backend.invoke("connections.get");
    for (auto kind : {"acquire", "heartbeat", "release"})
      profiles["claims." + std::string(kind)] = {
          {"enabled", true},       {"automatic", true},
          {"method", "POST"},      {"url", "https://user.example.invalid/" + std::string(kind)},
          {"heartbeatSeconds", 1}, {"keyEnv", "USER_KEY"}};
    confirmed("connections.set", profiles);
    int acquire = 0, heartbeat = 0, released = 0;
    bool cancelled = false;
    Backend leases(repo, data, settings, {{"USER_KEY", "local-fixture-secret"}},
                   [&](auto &url, auto &, auto &, auto &) {
                     if (url.find("acquire") != url.npos)
                       ++acquire;
                     if (url.find("heartbeat") != url.npos)
                       ++heartbeat;
                     if (url.find("release") != url.npos)
                       ++released;
                     return HttpResponse{200, "{\"ok\":true,\"lease\":\"fixture-lease\"}"};
                   });
    {
      auto lease =
          leases.reserve(Json::array({{{"module", "arm9"}, {"addr", 33554432}, {"size", 16}}}),
                         "Fixture", [&] { cancelled = true; });
      std::this_thread::sleep_for(std::chrono::milliseconds(1200));
      lease->check();
    }
    expect(acquire == 1 && heartbeat >= 1 && released == 1 && !cancelled,
           "remote lease acquire heartbeat release lifecycle");
    Backend refused(
        repo, data, settings, {{"USER_KEY", "local-fixture-secret"}},
        [](auto &, auto &, auto &, auto &) { return HttpResponse{409, "{\"ok\":false}"}; });
    reject(
        [&] {
          refused.reserve(Json::array({{{"module", "arm9"}, {"addr", 33554432}}}), "Fixture",
                          [] {});
        },
        "atomic reservation refusal stops work");
    // Destructive synchronization is tested only in this disposable repository.
    Repository fixtureGit(git, repo, settings);
    auto base = trim(fixtureGit.git({"rev-parse", "HEAD"}));
    expect(git.run({{"git", "update-ref", "refs/remotes/origin/main", base}, repo}).code == 0,
           "fixture upstream ref");
    fs::create_directories(repo / "port");
    write(repo / "port/change.txt", "local commit");
    git.run({{"git", "add", "port/change.txt"}, repo});
    expect(git.run({{"git", "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                     "commit", "-m", "local port work"},
                    repo})
                   .code == 0,
           "fixture local work");
    write(repo / "port/change.txt", "local edit");
    write(repo / "scratch.txt", "temporary work");
    write(repo / "local-assets/keep.bin", "user-owned local fixture");
    auto syncPreview = backend.invoke("git.syncPreview", {{"ref", "origin/main"}});
    expect(syncPreview["target"] == base && !syncPreview["remove"].empty(),
           "sync concrete commit and deletion preview");
    auto sync = confirmed("git.sync", {{"ref", "origin/main"}});
    expect(sync["exit"] == 0 && trim(fixtureGit.git({"rev-parse", "HEAD"})) == base,
           "confirmed sync resets to reviewed target");
    expect(fs::exists(repo / "local-assets/keep.bin") && !fs::exists(repo / "scratch.txt"),
           "sync preserves protected assets and deletes only previewed allowed files");
    expect(fs::exists(fs::u8path(sync["backup"]["backup"].get<std::string>()) / "port/change.txt"),
           "sync local edits backed up before reset");
    expect(trim(fixtureGit.git({"rev-parse", sync["backup"]["recoveryRef"].get<std::string>()})) !=
               base,
           "unpushed history pinned for recovery");
    std::cout << count << " backend assertions passed; fixture=" << utf8(dir.wstring()) << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << "\n";
    return 1;
  }
}
