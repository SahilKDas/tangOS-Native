#include "repository.h"
#include "viewer.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <windows.h>
using namespace lite;
int passed = 0;
void expect(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(what);
  ++passed;
}
template <class F> void rejects(F f, const char *what) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  expect(caught, what);
}
int main() {
  fs::path temp = fs::temp_directory_path() / fs::u8path("TangOS Lite tests " + uniqueId());
  try {
    expect(quoteWindows(L"a b") == L"\"a b\"", "space quoting");
    expect(quoteWindows(L"a\\") == L"\"a\\\\\"", "trailing slash quoting");
    expect(quoteWindows(L"a\"b") == L"\"a\\\"b\"", "quote escaping");
    expect(tokenize("python \"a b.py\" --flag") == Args({"python", "a b.py", "--flag"}),
           "INI tokenize");
    rejects([] { tokenize("\"unfinished"); }, "reject unclosed quote");
    AtlasCamera camera;
    camera.zoomAt(2, 50, 40, 100, 80);
    expect(camera.zoom == 2 && camera.x == -50 && camera.y == -40, "zoom anchors cursor");
    auto view = camera.visible(100, 80);
    expect(view.x == 25 && view.y == 20 && view.width == 50, "minimap world viewport");
    camera.center(100, 80, 100, 80);
    expect(camera.x == -100 && camera.y == -80, "minimap center clamped");
    camera.zoomAt(.01, 50, 40, 100, 80);
    expect(camera.zoom == 1 && camera.x == 0 && camera.y == 0,
           "fit zoom cannot pan into empty space");
    std::vector<Tile> fixtureTiles = {{0, 0, 0, 50, 50}, {1, 50, 0, 50, 50}, {2, 0, 50, 100, 50}};
    expect(marqueeTiles(fixtureTiles, {60, 40, -50, -30}) == std::vector<size_t>({0, 1}),
           "reverse marquee intersects tiles");
    expect(marqueeTiles(fixtureTiles, {110, 110, 5, 5}).empty(), "marquee outside world");
    expect(marqueeTiles(fixtureTiles, {0, 0, 50, 50}) == std::vector<size_t>({0}),
           "edge contact does not select neighbor");
    AtlasLod lod;
    auto fixtureRows = parseAtlas(
        R"({"functions":[{"id":"a","module":"arm9","size":50},{"id":"b","module":"arm9","size":50},{"id":"c","module":"overlay","size":100}]})");
    lod.compute(fixtureRows, fixtureTiles, 100, 100);
    expect(atlasNeighbor(fixtureRows, fixtureTiles, 0, 1, 0) == 1,
           "directional travel prefers same-module neighbor");
    expect(atlasNeighbor(fixtureRows, fixtureTiles, 0, 0, 1) == 2,
           "directional travel crosses module edge");
    expect(atlasNeighbor(fixtureRows, fixtureTiles, 0, -1, 0) == 0,
           "directional travel stays when no forward neighbor");
    expect(lod.update(1) == 1 && lod.update(2) == 2 && lod.update(4) == 3,
           "derived atlas LOD bands");
    expect(lod.update(2.7) == 3 && lod.update(2.4) == 2, "LOD hysteresis prevents thrashing");
    expect(numberedSource("int a;\nint b;\n") == "1  int a;\n2  int b;\n",
           "source inspection line numbers");
    Settings s;
    s.repository = "C:/unicode/日本語";
    s.themeIndex = 2;
    s.checks["Custom"] = {"python", "a b.py"};
    saveSettings(temp / "settings.ini", s);
    auto loaded = loadSettings(temp / "settings.ini");
    expect(loaded.repository == s.repository && loaded.checks == s.checks && loaded.portOnly &&
               loaded.themeIndex == 2,
           "settings round trip");
    write(temp / "bad.ini", "[settings]\nport_only=typo\n");
    rejects([&] { loadSettings(temp / "bad.ini"); }, "invalid safety configuration fails closed");
    auto status = parseStatus(std::string("R  new name\0old name\0UU conflict\0?? fresh\0", 41));
    expect(status.size() == 3 && status[0].original == "old name" && status[1].conflict(),
           "NUL rename and conflict parsing");
    for (auto p :
         {"src/file.cpp", "SRC/x.c", "rom.nds", "extracted/foo", ".env", "nested/.env.local",
          "private/token.txt", "../safe", "rom.bin", "id_rsa", "build/../src/a"})
      expect(!blockedPath(p, s).empty(), "protected path");
    expect(blockedPath("port/hal.cpp", s).empty(), "port allowed");
    expect(!blockedBlob("ghp_secret").empty(), "token scan");
    std::string rom(0x200, 0);
    rom.replace(0xc0, 6, "\x24\xff\xae\x51\x69\x9a", 6);
    expect(!blockedBlob(rom).empty(), "ROM content signature");
    for (auto ref : {"-x", "HEAD~1", "foo..bar", "a b", "x@{0}", "a\\b"})
      expect(!validRef(ref), "invalid ref");
    expect(validRef("scopic/main"), "upstream ref");
    auto empty = discoverChecks(temp, s);
    expect(!empty[0].available, "missing ROM check disabled");
    write(temp / "hash-fixture", "abc");
    expect(sha256File(temp / "hash-fixture") ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "native SHA256 ROM identity verifier");
    auto r = temp / "repo";
    fs::create_directories(r);
    Runner runner;
    auto run = [&](Args a) {
      auto x = runner.run({a, r});
      if (x.code)
        throw std::runtime_error(x.output);
    };
    run({"git", "init", "-b", "main"});
    run({"git", "config", "user.email", "lite@example.invalid"});
    run({"git", "config", "user.name", "Lite Test"});
    run({"git", "config", "core.autocrlf", "false"});
    write(r / "port/ok.txt", "safe\n");
    write(r / ".gitignore", "ignored/\n");
    run({"git", "add", "port/ok.txt", ".gitignore"});
    run({"git", "commit", "-m", "initial"});
    Repository repo(runner, r / "port", s);
    expect(repo.root == fs::canonical(r), "nested repository root detection");
    expect(repo.status().find("main") != std::string::npos, "status reports branch");
    rejects([&] { Repository no(runner, temp, s); }, "non-repository rejected");
    expect(repo.action("Merge", "origin", "tango/main", "").argv.back() == "tango/main",
           "merge argv");
    write(r / "port/ok.txt", "safe changed\n");
    rejects([&] { repo.action("Rebase", "origin", "main", ""); }, "dirty rebase blocked");
    run(repo.action("Stage paths", "", "", "port/ok.txt").argv);
    expect(repo.commitPreview().find("safe changed") != std::string::npos,
           "full staged diff preview");
    run(repo.action("Commit staged", "", "", "port fix").argv);
    write(r / "src/bad.cpp", "port workaround");
    run({"git", "add", "src/bad.cpp"});
    rejects([&] { repo.commitPreview(); }, "staged src blocked");
    run({"git", "reset", "--", "src/bad.cpp"});
    fs::remove(r / "src/bad.cpp");
    write(r / "port/token.txt", "github_pat_testcredential");
    run({"git", "add", "port/token.txt"});
    rejects([&] { repo.commitPreview(); }, "staged credential content blocked");
    run({"git", "reset", "--", "port/token.txt"});
    fs::remove(r / "port/token.txt");
    write(r / "ignored/local.txt", "secret asset");
    run({"git", "add", "-f", "ignored/local.txt"});
    rejects([&] { repo.commitPreview(); }, "ignored staged assets blocked");
    run({"git", "reset", "--", "ignored/local.txt"});
    auto bare = temp / "remote.git";
    fs::create_directories(bare);
    auto b = runner.run({{"git", "init", "--bare"}, bare});
    expect(b.code == 0, "disposable bare remote");
    run({"git", "remote", "add", "origin", utf8(bare.wstring())});
    run({"git", "push", "-u", "origin", "main"});
    write(r / "port/ok.txt", "third\n");
    run({"git", "add", "port/ok.txt"});
    run({"git", "commit", "-m", "third"});
    expect(repo.pushPreview("origin", "main").find("third") != std::string::npos,
           "outgoing patch preview");
    auto peer = temp / "peer";
    auto cloned = runner.run({{"git", "-c", "core.autocrlf=false", "clone", "-b", "main",
                               utf8(bare.wstring()), utf8(peer.wstring())},
                              temp});
    expect(cloned.code == 0, "disposable peer clone");
    auto peerRun = [&](Args args) {
      auto result = runner.run({args, peer});
      if (result.code)
        throw std::runtime_error(result.output);
    };
    peerRun({"git", "config", "user.email", "peer@example.invalid"});
    peerRun({"git", "config", "user.name", "Peer"});
    peerRun({"git", "config", "core.autocrlf", "false"});
    write(peer / "port/peer.txt", "peer change\n");
    peerRun({"git", "add", "port/peer.txt"});
    peerRun({"git", "commit", "-m", "peer change"});
    peerRun({"git", "push", "origin", "main"});
    run(repo.action("Fetch", "", "", "").argv);
    expect(repo.git({"rev-list", "--left-right", "--count", "HEAD...origin/main"}).find("1\t1") !=
               std::string::npos,
           "fetch tracks divergent upstream");
    run(repo.action("Merge", "", "origin/main", "").argv);
    expect(read(r / "port/peer.txt") == "peer change\n",
           "native merge adapter integrates upstream");
    peerRun({"git", "pull", "--ff-only"});
    write(peer / "port/peer.txt", "peer next change\n");
    peerRun({"git", "add", "port/peer.txt"});
    peerRun({"git", "commit", "-m", "peer next"});
    peerRun({"git", "push", "origin", "main"});
    run(repo.action("Fetch", "", "", "").argv);
    run(repo.action("Rebase", "", "origin/main", "").argv);
    expect(read(r / "port/peer.txt") == "peer next change\n",
           "native rebase adapter integrates upstream");
    run({"git", "push", "origin", "main"});
    peerRun({"git", "pull", "--ff-only"});
    write(peer / "port/ff.txt", "fast-forward change\n");
    peerRun({"git", "add", "port/ff.txt"});
    peerRun({"git", "commit", "-m", "fast-forward"});
    peerRun({"git", "push", "origin", "main"});
    run(repo.action("Pull (fast-forward)", "", "", "").argv);
    expect(read(r / "port/ff.txt") == "fast-forward change\n", "fast-forward pull adapter");
    expect(repo.action("Compare upstreams", "origin/main", "HEAD", "").argv.back() ==
               "origin/main...HEAD",
           "upstream comparison argv");
    rejects([&] { repo.action("Push reviewed", "--force", "main", ""); },
            "push option injection blocked");
    run({"git", "remote", "set-url", "--push", "origin",
         "git@github.com:myfork/sm64ds-decomp.git"});
    auto draft = repo.action("Create draft PR", "tangosdev/sm64ds-decomp", "main", "Port fix");
    expect(std::find(draft.argv.begin(), draft.argv.end(), "myfork:main") != draft.argv.end(),
           "cross-fork PR head construction");
    expect(repo.action("PR readiness", "", "", "").argv[0] == "gh", "GitHub readiness adapter");
    run({"git", "switch", "-c", "conflict-side"});
    write(r / "port/ff.txt", "side change\n");
    run({"git", "add", "port/ff.txt"});
    run({"git", "commit", "-m", "conflicting side"});
    run({"git", "switch", "main"});
    write(r / "port/ff.txt", "main change\n");
    run({"git", "add", "port/ff.txt"});
    run({"git", "commit", "-m", "conflicting main"});
    auto mergeConflict = runner.run(repo.action("Merge", "", "conflict-side", ""));
    expect(mergeConflict.code != 0 &&
               repo.status().find("CONFLICT port/ff.txt") != std::string::npos,
           "real merge conflict detected and named");
    rejects([&] { repo.commitPreview(); }, "unresolved merge conflicts block commits");
    run({"git", "merge", "--abort"});
    write(r / "rom.nds", "forbidden");
    run({"git", "add", "rom.nds"});
    run({"git", "commit", "-m", "bad intermediate"});
    run({"git", "rm", "rom.nds"});
    run({"git", "commit", "-m", "delete bad asset"});
    rejects([&] { repo.pushPreview("origin", "main"); },
            "deleted intermediate asset still blocks push");
    auto wt = temp / "worktree";
    run({"git", "worktree", "add", "-b", "test-worktree", utf8(wt.wstring())});
    Repository wr(runner, wt, s);
    expect(wr.status().find("test-worktree") != std::string::npos, "linked worktree detection");
    write(r / "tools/port_refcheck.py", "print('port/example.cpp:42: fixture diagnostic')\n");
    expect(discoverChecks(r, s)[2].available, "existing script discovery");
    auto check = repo.check(2);
    auto result = runner.run(check, {}, temp / "full.log");
    expect(result.code == 0 &&
               read(temp / "full.log").find("port/example.cpp:42") != std::string::npos,
           "check execution and durable diagnostics");
    write(r / "cancel.py", "import "
                           "subprocess,sys,time\nsubprocess.Popen([sys.executable,'-c','import "
                           "time;time.sleep(30)'])\nprint('before "
                           "cancel',flush=True)\ntime.sleep(30)\n");
    runner.reset();
    std::thread kill([&] {
      Sleep(500);
      runner.cancel();
    });
    auto cancelled = runner.run({{"python", "cancel.py"}, r}, {}, temp / "cancel.log");
    kill.join();
    expect(cancelled.code == ERROR_CANCELLED &&
               read(temp / "cancel.log").find("before cancel") != std::string::npos,
           "cancel descendants and preserve log");
    runner.reset();
    auto missing = runner.run({{"TangOSLite-no-such-executable"}, r});
    expect(missing.code != 0, "missing executable actionable failure");
    expect(fs::canonical(temp.parent_path()) == fs::canonical(fs::temp_directory_path()) &&
               temp.filename().wstring().rfind(L"TangOS Lite tests ", 0) == 0,
           "cleanup confined to test fixture");
    for (auto &entry : fs::recursive_directory_iterator(temp))
      if (entry.is_regular_file())
        SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
    fs::remove_all(temp);
    std::cout << "Passed " << passed
              << " assertions including disposable Git integration and "
                 "process-tree cancellation.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << "\nFixture preserved: " << temp << "\n";
    return 1;
  }
}
