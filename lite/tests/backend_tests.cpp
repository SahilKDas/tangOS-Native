#include "backend.h"
#include "repository.h"
#include <iostream>
#include <thread>
#include <chrono>
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
    expect(backend.invoke("projects.list").empty(), "empty registry");
    confirmed("projects.register", {{"id", "fixture"}, {"repository", utf8(repo.wstring())}});
    expect(backend.invoke("projects.list").size() == 1, "registered local project");
    confirmed("projects.register", {{"id", "remote"}, {"descriptor", desc}});
    expect(backend.invoke("projects.list").size() == 2, "remote no-clone descriptor");
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
