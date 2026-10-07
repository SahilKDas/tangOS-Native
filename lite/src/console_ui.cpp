#include "console_ui.h"
#include "atlas_layout.h"
#include "network.h"
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <fstream>
#include <numeric>
#include <stdexcept>
namespace lite {
namespace {
std::string text(HWND h) {
  int n = GetWindowTextLengthW(h);
  std::wstring s(n + 1, 0);
  GetWindowTextW(h, s.data(), n + 1);
  s.resize(n);
  return utf8(s);
}
void setText(HWND h, const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r'))
      out += '\r';
    out += s[i];
  }
  SetWindowTextW(h, wide(out).c_str());
}
void appendText(HWND h, const std::string &s) {
  if (GetWindowTextLengthW(h) > 200000)
    SetWindowTextW(h, L"[Older output preserved in complete logs.]\r\n");
  auto n = GetWindowTextLengthW(h);
  SendMessageW(h, EM_SETSEL, n, n);
  auto wideText = wide(s);
  SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)wideText.c_str());
}
int choice(HWND h) { return (int)SendMessageW(h, CB_GETCURSEL, 0, 0); }
std::string selected(HWND h) {
  int i = choice(h);
  if (i < 0)
    return {};
  int n = (int)SendMessageW(h, CB_GETLBTEXTLEN, i, 0);
  std::wstring s(n + 1, 0);
  SendMessageW(h, CB_GETLBTEXT, i, (LPARAM)s.data());
  s.resize(n);
  return utf8(s);
}
std::string tailFile(const fs::path &p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f)
    return {};
  auto n = f.tellg();
  if (n > 100000)
    f.seekg(n - std::streamoff(100000));
  else
    f.seekg(0);
  return std::string(std::istreambuf_iterator<char>(f), {});
}
enum {
  HOME = 4000,
  ATLAS,
  ENCYCLOPEDIA,
  SETTINGS,
  GUIDE,
  GITTOOLS,
  ADD_AGENT,
  EDIT_AGENT,
  REMOVE_AGENT,
  GO,
  STOP,
  ASSIGN,
  CLEAR_QUEUE,
  ADD_CART,
  DETAIL,
  REVIEW_AGENT,
  OPEN_LOG,
  SEARCH,
  TOOL_LIST,
  TOOL_RUN,
  TOOL_CANCEL,
  TOOL_ARGS,
  SAVE_PROFILE,
  CANCEL_PROFILE,
  VAULT_SAVE,
  VAULT_REMOVE,
  SAVE_SETTINGS,
  OPEN_MCP,
  ATLAS_CART,
  ATLAS_LOAD,
  TOUR_NEXT,
  TOUR_PREVIOUS,
  TOUR_CLOSE,
  COMMIT_AGENT,
  LAND_AGENT
};
constexpr int ATLAS_LAYOUT = 4200, ATLAS_FILTER = 4201, ATLAS_RESET = 4202, ATLAS_LIVE = 4203;
constexpr int TOOL_FORM = 4300, TOOL_FORM_SAVE = 4301, TOOL_FORM_NEXT = 4302, TOOL_FORM_PREV = 4303;
enum class Screen { controller, atlas, encyclopedia, settings, detail, profile, tour, parameters };
struct Hit {
  RECT rect;
  int index;
};
} // namespace
struct ConsoleUI::Impl {
  HWND parent, window;
  HFONT font;
  fs::path repository, data;
  Settings settings;
  std::function<void()> gitTools;
  std::function<void(const Settings &)> savePreferences;
  Descriptor descriptor;
  std::string descriptorError;
  std::unique_ptr<Fleet> fleet;
  std::unique_ptr<McpServer> mcp;
  Vault vault;
  Screen screen = Screen::controller;
  std::vector<HWND> controls;
  HWND logBox = nullptr, search = nullptr, agentChoice = nullptr, toolList = nullptr,
       argsBox = nullptr, body = nullptr;
  HWND keyChoice = nullptr, keyEdit = nullptr, writes = nullptr, advanced = nullptr,
       portOnly = nullptr, loop = nullptr;
  std::map<std::string, HWND> profileFields;
  std::vector<AgentState> agents;
  std::string selectedId, profileId, toolId;
  bool allowWrites = false, advancedMode = false, runDock = false, rebuilding = false;
  int width = 800, height = 720, tourStep = 0, scroll = 0;
  std::vector<AtlasFunction> atlas;
  std::vector<size_t> filtered, cart;
  std::vector<Hit> hits;
  std::string atlasError;
  std::thread loader, manualWorker;
  Runner manualRunner;
  std::atomic<bool> manualBusy{false}, atlasReady{false};
  std::mutex outputMutex;
  std::string pending;
  HBRUSH fieldBrush = nullptr;
  std::vector<std::string> shownTools;
  Json argumentDraft = Json::object();
  int argumentPage = 0;
  std::map<std::string, HWND> argumentFields;
  std::vector<std::string> cardIds;
  size_t pickedFunction = SIZE_MAX;
  HWND layoutChoice = nullptr, filterChoice = nullptr;
  double zoom = 1, panX = 0, panY = 0;
  std::vector<Tile> tiles;
  std::string atlasQuery, atlasMode = "ov", atlasFilter = "all", cachedLayout;
  bool liveAtlas = false;
  Impl(HWND p, HFONT f, fs::path repo, fs::path d, Settings prefs, std::function<void()> git,
       std::function<void(const Settings &)> save)
      : parent(p), font(f), repository(std::move(repo)), data(std::move(d)),
        settings(std::move(prefs)), gitTools(std::move(git)), savePreferences(std::move(save)),
        vault(data / "vault") {
    try {
      descriptor = loadDescriptor(repository);
    } catch (const std::exception &e) {
      descriptorError = e.what();
      descriptor.title = utf8(repository.filename().wstring());
    }
    auto prefsFile = data / "console-ui.json";
    if (fs::exists(prefsFile)) {
      auto j = Json::parse(read(prefsFile));
      advancedMode = j.value("advanced", false);
      allowWrites = j.value("writes", false);
    }
    if (descriptorError.empty()) {
      // Stable project directory shared by windows; Fleet's exclusive ownership
      // lock prevents competing schedulers against the same checkout.
      std::string canonical = utf8(fs::weakly_canonical(repository).wstring());
      std::transform(canonical.begin(), canonical.end(), canonical.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      uint64_t hash = 1469598103934665603ULL;
      for (unsigned char c : canonical) {
        hash ^= c;
        hash *= 1099511628211ULL;
      }
      auto project = data / "projects" / fs::u8path(std::to_string(hash));
      fleet = std::make_unique<Fleet>(repository, project, descriptor, settings,
                                      [this](const std::string &s) {
                                        std::lock_guard<std::mutex> lock(outputMutex);
                                        pending += s;
                                        if (pending.size() > 1000000)
                                          pending.erase(0, pending.size() - 1000000);
                                      });
      // Vault is global, whereas queues and worktrees are scoped to a checkout.
      fs::create_directories(data / "vault");
      mcp = std::make_unique<McpServer>(*fleet, descriptor, project / "mcp-client.json");
      loader = std::thread([this] {
        try {
          atlas =
              parseAtlas(liveAtlas ? fetchHttps(descriptor.document.value("data", Json::object())
                                                    .value("committedDbUrl", std::string()))
                                   : read(confinedPath(repository, descriptor.database)));
        } catch (const std::exception &e) {
          atlasError = e.what();
        }
        atlasReady = true;
      });
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.style = CS_DBLCLKS;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TangOSLiteConsole";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    window = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"", WS_CHILD | WS_CLIPCHILDREN,
                             14, 66, 800, 720, parent, nullptr, wc.hInstance, this);
    fieldBrush = CreateSolidBrush(skin::field());
    SetTimer(window, 1, 200, nullptr);
    build();
  }
  ~Impl() {
    KillTimer(window, 1);
    manualRunner.cancel();
    if (manualWorker.joinable())
      manualWorker.join();
    mcp.reset();
    fleet.reset();
    if (loader.joinable())
      loader.join();
    DestroyWindow(window);
    if (fieldBrush)
      DeleteObject(fieldBrush);
  }
  HWND control(const wchar_t *kind, const std::string &title, int id, int x, int y, int w, int h,
               DWORD style = 0) {
    if (std::wstring(kind) == L"BUTTON" && !(style & BS_AUTOCHECKBOX))
      style |= BS_OWNERDRAW;
    auto c =
        CreateWindowExW(0, kind, wide(title).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, x,
                        y, w, h, window, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    controls.push_back(c);
    return c;
  }
  HWND button(const std::string &title, int id, int x, int y, int w = 104) {
    return control(L"BUTTON", title, id, x, y, w, 30);
  }
  HWND edit(const std::string &title, int id, int x, int y, int w, int h = 30,
            DWORD style = ES_AUTOHSCROLL) {
    return control(L"EDIT", title, id, x, y, w, h, style);
  }
  HWND combo(const std::vector<std::string> &values, int id, int x, int y, int w, int active = 0) {
    auto c = control(L"COMBOBOX", "", id, x, y, w, 300, CBS_DROPDOWNLIST | WS_VSCROLL);
    for (auto &v : values)
      SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)wide(v).c_str());
    SendMessageW(c, CB_SETCURSEL, active, 0);
    return c;
  }
  void label(const std::string &s, int x, int y, int w, int h = 22) {
    control(L"STATIC", s, 0, x, y, w, h);
  }
  void destroyControls() {
    for (auto h : controls)
      DestroyWindow(h);
    controls.clear();
    profileFields.clear();
    argumentFields.clear();
    logBox = search = agentChoice = toolList = argsBox = body = keyChoice = keyEdit = writes =
        advanced = portOnly = loop = nullptr;
  }
  std::string activeId() { return selectedId; }
  AgentState *activeAgent() {
    for (auto &a : agents)
      if (a.id == selectedId)
        return &a;
    return nullptr;
  }
  void refreshAgents() {
    if (fleet)
      agents = fleet->snapshot();
    if (selectedId.empty() && !agents.empty())
      selectedId = agents[0].id;
  }
  void navigate(Screen next) {
    screen = next;
    scroll = 0;
    build();
  }
  void build() {
    struct BuildGuard {
      bool &flag;
      BuildGuard(bool &f) : flag(f) { flag = true; }
      ~BuildGuard() { flag = false; }
    } guard(rebuilding);
    refreshAgents();
    destroyControls();
    int cw = width - 356;
    if (screen == Screen::controller) {
      cardIds.clear();
      int cardY = 64 - scroll;
      for (size_t i = 0; i < agents.size(); ++i) {
        auto &a = agents[i];
        cardIds.push_back(a.id);
        int cardHeight = advancedMode ? 192 : 160;
        if (cardY >= 62 && cardY + cardHeight <= height - 160) {
          int base = 5000 + (int)i * 16;
          button(a.active ? "Stop" : "Go", base, 28, cardY + cardHeight - 42, 76);
          button("Details", base + 1, 112, cardY + cardHeight - 42, 86);
          profileFields[a.id + "Count"] =
              edit(std::to_string(a.spec.count), base + 4, cw - 94, cardY + cardHeight - 42, 62);
          if (advancedMode) {
            std::vector<std::string> roles = {"Unassigned", "Hard matcher", "Drafter", "Refiner",
                                              "Random"};
            int at = 0;
            for (size_t r = 0; r < roles.size(); ++r)
              if (roles[r] == a.spec.role)
                at = (int)r;
            profileFields[a.id + "Role"] = combo(roles, base + 2, 28, cardY + 104, 150, at);
            profileFields[a.id + "Effort"] =
                combo({"off", "low", "medium", "high"}, base + 3, 186, cardY + 104, 96,
                      a.spec.effort == "off"      ? 0
                      : a.spec.effort == "low"    ? 1
                      : a.spec.effort == "medium" ? 2
                                                  : 3);
            button("Assign", base + 5, 206, cardY + cardHeight - 42, 84);
            button("Clear queue", base + 6, 298, cardY + cardHeight - 42, 112);
            button("Add chosen", base + 7, 418, cardY + cardHeight - 42, 112);
          }
        }
        cardY += cardHeight + 12;
      }
      button("Add AI", ADD_AGENT, cw - 108, 16, 92);
      if (!agents.empty()) {
        std::vector<std::string> names;
        int sel = 0;
        for (size_t i = 0; i < agents.size(); ++i) {
          names.push_back(agents[i].spec.name);
          if (agents[i].id == selectedId)
            sel = (int)i;
        }
        agentChoice = combo(names, DETAIL, 18, height - 136, 190, sel);
        button("Go", GO, 220, height - 136, 72);
        button("Stop", STOP, 300, height - 136, 72);
        button("Details", DETAIL, 380, height - 136, 86);
        if (advancedMode) {
          button("Assign", ASSIGN, 18, height - 98, 85);
          button("Clear queue", CLEAR_QUEUE, 112, height - 98, 112);
          button("Add chosen", ADD_CART, 232, height - 98, 112);
          button("Edit", EDIT_AGENT, 352, height - 98, 72);
        }
        button("Review", REVIEW_AGENT, cw - 110, height - 136, 92);
      }
      button("Encyclopedia", ENCYCLOPEDIA, 16, height - 48, 126);
      button("Settings", SETTINGS, 150, height - 48, 94);
      button("Git & reviews", GITTOOLS, 252, height - 48, 122);
      button("Tour", GUIDE, 382, height - 48, 68);
      button("MCP connection", OPEN_MCP, width - 332, height - 116, 144);
      button("Run logs", OPEN_LOG, width - 180, height - 116, 116);
      button("This repo needs", ENCYCLOPEDIA, width - 332, 16, 188);
    } else if (screen == Screen::atlas) {
      search = edit(atlasQuery, SEARCH, 18, 52, cw - 250);
      layoutChoice = combo({"ov", "size", "match", "author"}, ATLAS_LAYOUT, cw - 222, 52, 100,
                           atlasMode == "size"     ? 1
                           : atlasMode == "match"  ? 2
                           : atlasMode == "author" ? 3
                                                   : 0);
      filterChoice =
          combo({"all", "matched", "unmatched", "near_miss"}, ATLAS_FILTER, cw - 114, 52, 96,
                atlasFilter == "matched"     ? 1
                : atlasFilter == "unmatched" ? 2
                : atlasFilter == "near_miss" ? 3
                                             : 0);
      button("Add to cart", ATLAS_CART, width - 328, 285, 130);
      button("Assign cart", ADD_CART, width - 188, 285, 130);
      button("Controller", HOME, 18, height - 48, 108);
      button("Reload", ATLAS_LOAD, 134, height - 48, 90);
      button("Encyclopedia", ENCYCLOPEDIA, 232, height - 48, 126);
      button("Reset view", ATLAS_RESET, 366, height - 48, 104);
      button(liveAtlas ? "Local data" : "Live data", ATLAS_LIVE, 478, height - 48, 102);
      body = edit("Select a function in the treemap to inspect it.\nMatched: green; near-miss: "
                  "yellow; unmatched: blue.",
                  0, width - 332, 90, 310, 180, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
    } else if (screen == Screen::encyclopedia) {
      search = edit("", SEARCH, 18, 52, cw - 34);
      toolList = control(L"LISTBOX", "", TOOL_LIST, 18, 94, 225, height - 230,
                         LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT);
      body = edit("", 0, 255, 94, cw - 273, height - 325, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      argsBox = edit("{}", TOOL_ARGS, 255, height - 220, cw - 273, 106, ES_MULTILINE | WS_VSCROLL);
      button("Run", TOOL_RUN, 255, height - 98, 76);
      button("Cancel", TOOL_CANCEL, 339, height - 98, 88);
      button("Edit arguments", TOOL_FORM, 435, height - 98, 142);
      button("Close", HOME, 18, height - 48, 90);
      label("Run dock · complete output", width - 332, 57, 310);
      logBox = edit("", 0, width - 332, 88, 310, height - 164,
                    ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL);
      button("Open logs", OPEN_LOG, width - 332, height - 58, 114);
      filterTools();
    } else if (screen == Screen::settings) {
      label("Decomp repo", 18, 53, cw - 36);
      body = edit(utf8(repository.wstring()), 0, 18, 80, cw - 36);
      advanced = control(L"BUTTON", "Advanced interface", 0, 18, 130, cw - 36, 28, BS_AUTOCHECKBOX);
      SendMessageW(advanced, BM_SETCHECK, advancedMode, 0);
      writes = control(L"BUTTON", "Allow agent execution and mutating tools in isolated worktrees",
                       0, 18, 168, cw - 36, 48, BS_AUTOCHECKBOX);
      SendMessageW(writes, BM_SETCHECK, allowWrites, 0);
      portOnly = control(L"BUTTON", "Port only · refuse src/ changes at review", 0, 18, 226,
                         cw - 36, 28, BS_AUTOCHECKBOX);
      SendMessageW(portOnly, BM_SETCHECK, settings.portOnly, 0);
      label(
          "Every commit and push requires review. Destructive Git operations require confirmation.",
          18, 274, cw - 36, 54);
      label("API key vault · Windows DPAPI encrypted", 18, 340, cw - 36);
      auto keys = descriptor.keys;
      if (keys.empty())
        keys = {"OPENAI_API_KEY",   "ANTHROPIC_API_KEY", "GLM_API_KEY",
                "DEEPSEEK_API_KEY", "REQUESTY_API_KEY",  "CLAIMS_API_KEY"};
      keyChoice = combo(keys, 0, 18, 378, cw - 36);
      keyEdit = edit("", 0, 18, 419, cw - 36, 30, ES_PASSWORD | ES_AUTOHSCROLL);
      button("Store key", VAULT_SAVE, 18, 460, 108);
      button("Remove key", VAULT_REMOVE, 136, 460, 118);
      button("Save settings", SAVE_SETTINGS, 18, height - 98, 136);
      button("Close", HOME, 18, height - 48, 90);
      body = edit(settingsSummary(), 0, width - 332, 62, 310, height - 132,
                  ES_MULTILINE | ES_READONLY | WS_VSCROLL);
    } else if (screen == Screen::parameters) {
      auto &tool = descriptor.tool(toolId);
      int end = std::min((int)tool.args.size(), (argumentPage + 1) * 10), yy = 64;
      for (int i = argumentPage * 10; i < end; ++i) {
        auto &a = tool.args[i];
        label(a.name + (a.required ? " *" : ""), 18, yy + 5, 160);
        auto v = argumentDraft.contains(a.name) ? argumentDraft[a.name] : a.value;
        if (a.type == "boolean") {
          auto h = control(L"BUTTON", "Enabled", 0, 184, yy, cw - 204, 28, BS_AUTOCHECKBOX);
          SendMessageW(h, BM_SETCHECK, v.is_boolean() && v.get<bool>(), 0);
          argumentFields[a.name] = h;
        } else if (a.type == "enum") {
          std::vector<std::string> options;
          int active = 0;
          for (size_t k = 0; k < a.choices.size(); k++) {
            auto value =
                a.choices[k].is_string() ? a.choices[k].get<std::string>() : a.choices[k].dump();
            options.push_back(value);
            if (a.choices[k] == v)
              active = (int)k;
          }
          argumentFields[a.name] = combo(options, 0, 184, yy, cw - 204, active);
        } else
          argumentFields[a.name] = edit(v.is_null()     ? ""
                                        : v.is_string() ? v.get<std::string>()
                                                        : v.dump(),
                                        0, 184, yy, cw - 204);
        yy += 48;
      }
      button("Previous", TOOL_FORM_PREV, 18, height - 98, 100);
      button("Next", TOOL_FORM_NEXT, 128, height - 98, 100);
      button("Save values", TOOL_FORM_SAVE, 18, height - 48, 130);
      body = edit(tool.description + "\n\n" + tool.command, 0, width - 332, 62, 310, height - 132,
                  ES_MULTILINE | ES_READONLY | WS_VSCROLL);
    } else if (screen == Screen::profile) {
      buildProfile(cw);
    } else if (screen == Screen::detail) {
      button("Back", HOME, 18, height - 48, 90);
      button("Configure", EDIT_AGENT, 118, height - 48, 110);
      button("Remove AI", REMOVE_AGENT, 238, height - 48, 110);
      button("Go", GO, 18, 52, 76);
      button("Stop", STOP, 102, 52, 80);
      button("Review changes", REVIEW_AGENT, 190, 52, 148);
      button("Open worktree", OPEN_LOG, 346, 52, 144);
      body = edit("", 0, 18, 98, cw - 36, 160, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      logBox = edit("", 0, 18, 275, cw - 36, height - 344,
                    ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL);
      label("Independent verification & publication", width - 332, 57, 310, 40);
      auto a = activeAgent();
      label(a ? a->detail : "Select an agent", width - 332, 108, 310, 120);
      button("Commit reviewed", COMMIT_AGENT, width - 332, 250, 170);
      button("Land driver results", LAND_AGENT, width - 332, 290, 170);
    } else if (screen == Screen::tour) {
      button("Previous", TOUR_PREVIOUS, 50, height - 108, 110);
      button("Next", TOUR_NEXT, 168, height - 108, 100);
      button("Done", TOUR_CLOSE, 276, height - 108, 96);
      const std::vector<std::string> tips = {
          "1 / 5 · Choose your repository\n\nTangOS Lite discovers the project's tangos.json "
          "descriptor. Scripts, compiler, ROM and private assets remain yours. Review repository "
          "instructions before executing code.",
          "2 / 5 · Chaos Controller\n\nAdd an API, CLI or MCP agent. Set its model, role, "
          "reasoning effort, attempts and batch size. Enable writes in Settings, then Go. Each "
          "agent receives a separate worktree and the repository's scoped AGENTS.md instructions.",
          "3 / 5 · Chaos Viewer\n\nSearch functions, inspect module and match state, select "
          "targets and add them to the cart. Assign the cart to an agent or use a "
          "descriptor-defined scheduler. Active queues refuse duplicate target claims.",
          "4 / 5 · Encyclopedia and run dock\n\nSearch descriptor tools by category, label and "
          "argument. Inspect commands and argument definitions. Supply values as JSON, preview "
          "execution, and watch complete logs without blocking the window.",
          "5 / 5 · Verify and review\n\nStop cancels the process tree and retains output. A driver "
          "exit does not prove a match. Inspect independent checks and the complete diff before "
          "committing. Merge the reviewed branch and preview outgoing commits before pushing."};
      body = edit(tips[tourStep], 0, 50, 102, width - 100, height - 254,
                  ES_MULTILINE | ES_READONLY | WS_VSCROLL);
    }
    InvalidateRect(window, nullptr, TRUE);
  }
  std::string settingsSummary() {
    std::string s = "Project: " + descriptor.title + "\n" + descriptor.tagline + "\n\n";
    if (!descriptorError.empty())
      s += "Descriptor: " + descriptorError + "\n";
    auto keys = vault.values();
    s += "Encrypted credentials: " + std::to_string(keys.size()) + "\n";
    for (auto &k : keys)
      s += "✓ " + k.first + " (stored)\n";
    s += "\nMCP binds only to 127.0.0.1 and requires a bearer token. Click MCP connection to open "
         "its client config.\n\nRuntime: " +
         descriptor.python + "\nTools: " + std::to_string(descriptor.tools.size());
    return s;
  }
  void buildProfile(int cw) {
    AgentSpec spec;
    spec.name = "New AI";
    if (!profileId.empty())
      for (auto &a : agents)
        if (a.id == profileId)
          spec = a.spec;
    int y = 56;
    auto field = [&](const std::string &name, const std::string &value) {
      label(name, 18, y + 4, 120);
      profileFields[name] = edit(value, 0, 148, y, cw - 166);
      y += 37;
    };
    field("Name", spec.name);
    profileFields["Kind"] = combo({"api", "cli", "mcp"}, 0, 148, y, cw - 166,
                                  spec.kind == "cli"   ? 1
                                  : spec.kind == "mcp" ? 2
                                                       : 0);
    label("Kind", 18, y + 4, 120);
    y += 37;
    std::vector<std::string> roles = {"Unassigned", "Hard matcher", "Drafter", "Refiner", "Random"};
    int role = 0;
    for (size_t i = 0; i < roles.size(); i++)
      if (roles[i] == spec.role)
        role = (int)i;
    profileFields["Role"] = combo(roles, 0, 148, y, cw - 166, role);
    label("Role", 18, y + 4, 120);
    y += 37;
    field("Model", spec.model);
    field("API base URL", spec.baseUrl);
    field("API dialect", spec.dialect);
    field("Key variable", spec.key);
    field("CLI argv", spec.cli);
    field("Effort", spec.effort);
    field("Batch count", std::to_string(spec.count));
    field("Attempts", std::to_string(spec.attempts));
    field("Workers", std::to_string(spec.jobs));
    loop = control(L"BUTTON", "Continuous batches until Stop", 0, 148, y, cw - 166, 28,
                   BS_AUTOCHECKBOX);
    SendMessageW(loop, BM_SETCHECK, spec.loop, 0);
    button("Save AI", SAVE_PROFILE, 18, height - 48, 108);
    button("Cancel", CANCEL_PROFILE, 136, height - 48, 100);
    body = edit("API: uses the repository's console.driver and scheduler. Configure a model "
                "supported by that driver; its provider key stays encrypted.\n\nCLI: explicit argv "
                "only, no shell. {prompt}, {worklist} and {out} expand to files for the isolated "
                "worktree.\n\nMCP: set Name to the client's exact clientInfo.name. Go prepares a "
                "batch for next_batch.\n\nThe generated instructions include scoped AGENTS.md. No "
                "automatic commit or push.\n\nKeep batch count 1–200, attempts 1–20, workers 1–16.",
                0, width - 332, 66, 310, height - 136, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
  }
  void filterTools() {
    if (!toolList)
      return;
    auto needle = search ? text(search) : "";
    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
    SendMessageW(toolList, LB_RESETCONTENT, 0, 0);
    shownTools.clear();
    for (auto &t : descriptor.tools) {
      auto hay = t.category + " " + t.id + " " + t.label + " " + t.description;
      for (auto &a : t.args)
        hay += " " + a.name;
      std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
      if (!needle.empty() && hay.find(needle) == hay.npos)
        continue;
      shownTools.push_back(t.id);
      auto title = t.category + " · " + t.label;
      SendMessageW(toolList, LB_ADDSTRING, 0, (LPARAM)wide(title).c_str());
    }
    if (!shownTools.empty()) {
      SendMessageW(toolList, LB_SETCURSEL, 0, 0);
      selectTool();
    }
  }
  void selectTool() {
    auto at = (int)SendMessageW(toolList, LB_GETCURSEL, 0, 0);
    if (at < 0 || at >= (int)shownTools.size())
      return;
    toolId = shownTools[at];
    auto &t = descriptor.tool(toolId);
    std::string description = t.label + "\n\n" + t.description + "\n\n" +
                              (t.readOnly ? "Read only" : "Mutates repository state") +
                              "\nCommand: " + t.command + "\n\nArguments:\n";
    Json defaults = Json::object();
    for (auto &arg : t.args) {
      description += arg.name + " (" + arg.type + ")" + (arg.required ? " required" : "") + " " +
                     arg.flag + "\n" + arg.description + "\n";
      if (!arg.value.is_null())
        defaults[arg.name] = arg.value;
    }
    if (!t.docs.empty())
      description += "\nDocs: " + t.docs;
    setText(body, description);
    setText(argsBox, defaults.dump(2));
  }
  void loadAtlas() {
    if (!atlasReady)
      throw std::runtime_error("Atlas loading is already in progress");
    if (loader.joinable())
      loader.join();
    atlasReady = false;
    atlasError.clear();
    atlas.clear();
    tiles.clear();
    cachedLayout.clear();
    pickedFunction = SIZE_MAX;
    cart.clear();
    loader = std::thread([this] {
      try {
        atlas = parseAtlas(liveAtlas ? fetchHttps(descriptor.document.value("data", Json::object())
                                                      .value("committedDbUrl", std::string()))
                                     : read(confinedPath(repository, descriptor.database)));
      } catch (const std::exception &e) {
        atlasError = e.what();
      }
      atlasReady = true;
    });
  }
  void paint(HDC dc) {
    skin::panel(dc, 0, 0, width - 356, height);
    skin::panel(dc, width - 340, 0, 340, height, true);
    std::string title = screen == Screen::controller     ? "Chaos Controller"
                        : screen == Screen::atlas        ? "Chaos Viewer"
                        : screen == Screen::encyclopedia ? "Encyclopedia"
                        : screen == Screen::settings     ? "Settings"
                        : screen == Screen::profile      ? "Connect AI"
                        : screen == Screen::detail       ? "AI detail"
                        : screen == Screen::parameters   ? "Run dock · arguments"
                                                         : "Welcome to tangOS Lite";
    skin::label(dc, wide(title), 16, 17, width - 390, 28, 16, true);
    hits.clear();
    if (screen == Screen::controller) {
      if (agents.empty())
        skin::label(dc, L"No AIs connected yet.", width / 4 - 105, 105, 360, 28, 15, false, true);
      int cw = width - 388, y = 64 - scroll;
      int cardHeight = advancedMode ? 192 : 160;
      for (size_t i = 0; i < agents.size(); i++) {
        auto &a = agents[i];
        if (y + cardHeight < 62) {
          y += cardHeight + 12;
          continue;
        }
        if (y > height - 180)
          break;
        skin::panel(dc, 16, y, cw, cardHeight, true);
        skin::label(dc, wide((a.active ? "● " : "○ ") + a.spec.name), 30, y + 13, cw - 300, 26, 16,
                    true, false, true);
        skin::label(dc, wide(a.spec.kind + " · " + a.phase), 30, y + 86, 140, 24, 12, false,
                    true);
        skin::label(dc, wide(a.detail.empty() ? "Ready · " + a.spec.role : a.detail), 30, y + 47,
                    cw - 32, 40, 13, false, true);
        skin::label(dc, wide(a.lastLine), 178, y + 86, cw - 180, 20, 12, false, true);
        skin::label(dc,
                    wide(std::to_string(a.completed) + " worked · " +
                         std::to_string(a.queue.size()) + " queued · " + a.spec.effort + " effort"),
                    cw - 260, y + 13, 225, 24, 12, true);
        hits.push_back({{16, y, 16 + cw, y + cardHeight}, (int)i});
        y += cardHeight + 12;
      }
      skin::label(dc, L"Repository readiness", width - 322, 58, 305, 26, 15, true);
      int y2 = 100;
      for (auto &check : discoverChecks(repository, settings)) {
        if (y2 > height - 160)
          break;
        skin::label(dc, wide(std::string(check.available ? "✓ " : "○ ") + check.name), width - 322,
                    y2, 302, 24, 13, check.available, !check.available);
        y2 += 28;
      }
      if (!descriptorError.empty())
        skin::label(dc, wide("tangos.json: " + descriptorError), width - 322, y2 + 8, 302, 90, 12,
                    false, true);
      skin::mascot(dc, width - 128, height - 108, 90);
    } else if (screen == Screen::atlas) {
      if (!atlasReady) {
        skin::label(dc, L"Loading atlas…", 18, 108, width - 390, 40, 15, false, true);
        return;
      }
      if (!atlasError.empty()) {
        skin::label(dc,
                    wide("Atlas unavailable: " + atlasError +
                         "\nGenerate the database using the repository's tools in Encyclopedia."),
                    18, 108, width - 390, 100, 14, false, true);
        return;
      }
      auto needle = search ? text(search) : atlasQuery;
      std::transform(needle.begin(), needle.end(), needle.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      int left = 18, top = 100, w = width - 390, h = height - 170;
      std::string key = needle + "|" + atlasMode + "|" + atlasFilter + "|" + std::to_string(w) +
                        "x" + std::to_string(h);
      if (key != cachedLayout) {
        filtered.clear();
        for (size_t i = 0; i < atlas.size(); i++) {
          auto hay =
              atlas[i].id + " " + atlas[i].name + " " + atlas[i].module + " " + atlas[i].state;
          std::transform(hay.begin(), hay.end(), hay.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          if ((needle.empty() || hay.find(needle) != hay.npos) &&
              (atlasFilter == "all" || atlas[i].state == atlasFilter))
            filtered.push_back(i);
        }
        tiles = atlasLayout(atlas, filtered, w, h, atlasMode);
        cachedLayout = key;
      }
      int saved = SaveDC(dc);
      IntersectClipRect(dc, left, top, left + w, top + h);
      for (auto tile : tiles) {
        int x = left + (int)(tile.x * zoom + panX), y = top + (int)(tile.y * zoom + panY),
            tw = std::max(1, (int)(tile.width * zoom)), th = std::max(1, (int)(tile.height * zoom));
        if (x + tw < left || y + th < top || x > left + w || y > top + h)
          continue;
        auto &f = atlas[tile.index];
        COLORREF color = f.row.contains("noMatch") ? RGB(168, 50, 74)
                         : f.row.contains("claim") ? RGB(228, 134, 132)
                         : f.state == "matched"    ? RGB(63, 196, 95)
                         : f.state == "near_miss"  ? RGB(234, 179, 8)
                                                   : RGB(185, 202, 219);
        if (atlasMode == "author" && f.state == "matched") {
          uint32_t hash = 2166136261;
          for (unsigned char c : f.row.value("author", std::string("unattributed"))) {
            hash ^= c;
            hash *= 16777619;
          }
          color = RGB(80 + (hash & 127), 80 + ((hash >> 8) & 127), 80 + ((hash >> 16) & 127));
        }
        HBRUSH brush = CreateSolidBrush(color);
        RECT bounds{x, y, x + tw - 1, y + th - 1};
        FillRect(dc, &bounds, brush);
        DeleteObject(brush);
        if (std::find(cart.begin(), cart.end(), tile.index) != cart.end() ||
            tile.index == pickedFunction) {
          brush = CreateSolidBrush(RGB(255, 214, 40));
          FrameRect(dc, &bounds, brush);
          DeleteObject(brush);
        }
        if (tw > 90 && th > 24)
          skin::label(dc, wide(f.name), x + 4, y + 3, tw - 8, th - 6, 11, true);
        RECT hit{std::max(left, x), std::max(top, y), std::min(left + w, x + tw),
                 std::min(top + h, y + th)};
        hits.push_back({hit, (int)tile.index});
      }
      RestoreDC(dc, saved);
      skin::label(dc,
                  wide(std::to_string(filtered.size()) + " functions · " +
                       std::to_string(cart.size()) + " in cart"),
                  width - 322, 53, 302, 26, 13, true);
    } else if (screen == Screen::settings || screen == Screen::profile)
      skin::label(dc, L"Local configuration", width - 322, 18, 302, 28, 15, true);
    else if (screen == Screen::detail && activeAgent())
      skin::label(dc, wide(activeAgent()->spec.name), width - 322, 18, 302, 28, 16, true);
  }
  void launch(bool execute = true) {
    if (!fleet)
      throw std::runtime_error("Load a valid tangos.json before starting agents");
    if (!allowWrites)
      throw std::runtime_error("Enable agent execution in Settings first");
    if (selectedId.empty())
      throw std::runtime_error("Select an AI");
    if (MessageBoxW(window,
                    L"Start the configured repository scheduler and agent driver in an isolated "
                    L"worktree? Repository code may make model/API calls and incur charges. "
                    L"Changes and logs will be retained for review.",
                    L"Start agent", MB_YESNO | MB_ICONQUESTION) != IDYES)
      return;
    fleet->start(selectedId, execute);
    navigate(Screen::controller);
  }
  void collectArguments() {
    for (auto &a : descriptor.tool(toolId).args) {
      auto it = argumentFields.find(a.name);
      if (it == argumentFields.end())
        continue;
      auto h = it->second;
      if (a.type == "boolean")
        argumentDraft[a.name] = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
      else {
        auto value = a.type == "enum" ? selected(h) : text(h);
        if (value.empty()) {
          argumentDraft.erase(a.name);
          continue;
        }
        if (a.type == "integer")
          argumentDraft[a.name] = std::stoll(value);
        else if (a.type == "number")
          argumentDraft[a.name] = std::stod(value);
        else if (a.type == "enum")
          argumentDraft[a.name] = a.choices.at(choice(h));
        else
          argumentDraft[a.name] = value;
      }
    }
  }
  void executeTool() {
    if (manualBusy)
      return;
    auto &tool = descriptor.tool(toolId);
    if (settings.portOnly && !tool.readOnly) throw std::runtime_error("Port-only mode blocks mutating descriptor tools in the primary checkout. Use an isolated agent worktree, or explicitly disable port-only mode.");
    auto values = Json::parse(text(argsBox));
    auto c = toolCommand(descriptor, tool, values, repository, allowWrites,
                         values.value("apply", false));
    auto prompt = "Run repository code?\n\n" + preview(c) + "\n\n" + utf8(c.cwd.wstring()) +
                  "\n\n" +
                  (tool.readOnly ? "Declared read-only"
                                 : "Mutates repository state; inspect changes before any commit");
    if (MessageBoxW(window, wide(prompt).c_str(), L"Run tool", MB_YESNO | MB_ICONQUESTION) != IDYES)
      return;
    if (manualWorker.joinable())
      manualWorker.join();
    manualRunner.reset();
    manualBusy = true;
    setText(logBox, "");
    auto secrets = vault.values();
    for (auto &key : secrets)
      c.environment[key.first] = key.second;
    auto log = data / "logs" / (uniqueId() + "-" + tool.id + ".log");
    manualWorker = std::thread([this, c, log, secrets] {
      try {
        manualRunner.run(
            c,
            [&](const std::string &s) {
              std::lock_guard<std::mutex> lock(outputMutex);
              pending += redact(s, secrets);
            },
            log);
      } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(outputMutex);
        pending += e.what();
      }
      manualBusy = false;
    });
  }
  void reviewAgent(bool commit) {
    if (!fleet || selectedId.empty())
      return;
    auto preview = fleet->review(selectedId);
    auto a = activeAgent();
    if (!a)
      return;
    Runner r;
    Repository repo(r, a->worktree, settings);
    auto tree = repo.safetyIndex();
    auto agentName = a->spec.name;
    if (!commit) {
      navigate(Screen::detail);
      setText(logBox, preview);
      return;
    }
    setText(logBox, preview);
    if (MessageBoxW(window,
                    L"The complete staged diff is displayed in AI detail. Commit this reviewed "
                    L"index on the isolated agent branch? No push or merge is performed.",
                    L"Commit reviewed changes", MB_YESNO | MB_ICONQUESTION) != IDYES)
      return;
    fleet->commitReviewed(selectedId, "Reviewed agent work: " + agentName, tree);
  }
  void action(int id, int notification) {
    if (rebuilding)
      return;
    if (id >= 5000 && id < 5000 + (int)cardIds.size() * 16) {
      int index = (id - 5000) / 16, op = (id - 5000) % 16;
      selectedId = cardIds.at(index);
      auto a = activeAgent();
      if (!a)
        return;
      if (op == 0 && notification == BN_CLICKED) {
        if (a->active)
          fleet->stop(selectedId);
        else
          launch();
      } else if (op == 1 && notification == BN_CLICKED)
        navigate(Screen::detail);
      else if ((op == 2 || op == 3) && notification == CBN_SELCHANGE) {
        auto spec = a->spec;
        spec.role = selected(profileFields.at(a->id + "Role"));
        spec.effort = selected(profileFields.at(a->id + "Effort"));
        fleet->configure(a->id, spec);
      } else if (op == 4 && notification == EN_KILLFOCUS) {
        auto spec = a->spec;
        spec.count = std::stoi(text(profileFields.at(a->id + "Count")));
        fleet->configure(a->id, spec);
      } else if (op == 5 && notification == BN_CLICKED)
        launch(false);
      else if (op == 6 && notification == BN_CLICKED)
        fleet->clear(selectedId);
      else if (op == 7 && notification == BN_CLICKED)
        action(ADD_CART, BN_CLICKED);
      return;
    }
    if ((id == ATLAS_LAYOUT || id == ATLAS_FILTER) && notification == CBN_SELCHANGE) {
      atlasMode = selected(layoutChoice);
      atlasFilter = selected(filterChoice);
      cachedLayout.clear();
      InvalidateRect(window, nullptr, FALSE);
      return;
    }
    if (id == SEARCH && notification == EN_CHANGE) {
      if (screen == Screen::atlas)
        atlasQuery = text(search);
      if (screen == Screen::encyclopedia)
        filterTools();
      InvalidateRect(window, nullptr, FALSE);
      return;
    }
    if (id == TOOL_LIST && notification == LBN_SELCHANGE) {
      selectTool();
      return;
    }
    if (id == DETAIL && notification == CBN_SELCHANGE) {
      auto at = choice(agentChoice);
      if (at >= 0 && at < (int)agents.size())
        selectedId = agents[at].id;
      return;
    }
    if (notification != BN_CLICKED)
      return;
    switch (id) {
    case HOME:
      navigate(Screen::controller);
      break;
    case ATLAS:
      navigate(Screen::atlas);
      break;
    case ENCYCLOPEDIA:
      navigate(Screen::encyclopedia);
      break;
    case SETTINGS:
      navigate(Screen::settings);
      break;
    case GUIDE:
      navigate(Screen::tour);
      break;
    case GITTOOLS:
      gitTools();
      break;
    case ADD_AGENT:
      profileId.clear();
      navigate(Screen::profile);
      break;
    case EDIT_AGENT:
      profileId = selectedId;
      navigate(Screen::profile);
      break;
    case REMOVE_AGENT:
      if (fleet && MessageBoxW(window,
                               L"Remove this AI configuration and queue? Preserved worktrees and "
                               L"logs remain on disk.",
                               L"Remove AI", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        fleet->remove(selectedId);
        selectedId.clear();
        navigate(Screen::controller);
      }
      break;
    case GO:
      launch();
      break;
    case ASSIGN:
      launch(false);
      break;
    case STOP:
      if (fleet && !selectedId.empty())
        fleet->stop(selectedId);
      break;
    case CLEAR_QUEUE:
      if (fleet)
        fleet->clear(selectedId);
      build();
      break;
    case DETAIL:
      navigate(Screen::detail);
      break;
    case SAVE_PROFILE: {
      if (!fleet)
        throw std::runtime_error("A valid descriptor is required");
      AgentSpec s;
      s.name = text(profileFields.at("Name"));
      s.kind = selected(profileFields.at("Kind"));
      s.role = selected(profileFields.at("Role"));
      s.model = text(profileFields.at("Model"));
      s.baseUrl = text(profileFields.at("API base URL"));
      s.dialect = text(profileFields.at("API dialect"));
      s.key = text(profileFields.at("Key variable"));
      s.cli = text(profileFields.at("CLI argv"));
      s.effort = text(profileFields.at("Effort"));
      s.count = std::stoi(text(profileFields.at("Batch count")));
      s.attempts = std::stoi(text(profileFields.at("Attempts")));
      s.jobs = std::stoi(text(profileFields.at("Workers")));
      s.loop = SendMessageW(loop, BM_GETCHECK, 0, 0) == BST_CHECKED;
      if (s.name.empty())
        throw std::runtime_error("Give this AI a name");
      if (profileId.empty())
        selectedId = fleet->add(s);
      else
        fleet->configure(profileId, s);
      navigate(Screen::controller);
      break;
    }
    case CANCEL_PROFILE:
      navigate(Screen::controller);
      break;
    case VAULT_SAVE: {
      auto name = selected(keyChoice);
      vault.set(name, text(keyEdit));
      setText(keyEdit, "");
      build();
      break;
    }
    case VAULT_REMOVE:
      vault.remove(selected(keyChoice));
      build();
      break;
    case SAVE_SETTINGS: {
      if (fleet && fleet->running())
        throw std::runtime_error("Stop agents before changing safety settings");
      advancedMode = SendMessageW(advanced, BM_GETCHECK, 0, 0) == BST_CHECKED;
      allowWrites = SendMessageW(writes, BM_GETCHECK, 0, 0) == BST_CHECKED;
      bool next = SendMessageW(portOnly, BM_GETCHECK, 0, 0) == BST_CHECKED;
      settings.portOnly = next;
      if (fleet)
        fleet->setPolicy(settings);
      if (savePreferences)
        savePreferences(settings);
      write(data / "console-ui.json",
            Json({{"advanced", advancedMode}, {"writes", allowWrites}}).dump(2));
      navigate(Screen::controller);
      break;
    }
    case OPEN_MCP:
      if (mcp) {
        auto path = data / "mcp-client.json";
        write(path, mcp->configuration());
        ShellExecuteW(window, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
      }
      break;
    case OPEN_LOG: {
      auto a = activeAgent();
      auto path = screen == Screen::detail && a ? a->worktree : data / "logs";
      fs::create_directories(path);
      ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      break;
    }
    case TOOL_FORM:
      argumentDraft = Json::parse(text(argsBox));
      argumentPage = 0;
      navigate(Screen::parameters);
      break;
    case TOOL_FORM_NEXT:
      collectArguments();
      argumentPage = std::min((int)descriptor.tool(toolId).args.size() / 10, argumentPage + 1);
      build();
      break;
    case TOOL_FORM_PREV:
      collectArguments();
      argumentPage = std::max(0, argumentPage - 1);
      build();
      break;
    case TOOL_FORM_SAVE: {
      collectArguments();
      auto draft = argumentDraft;
      auto id = toolId;
      navigate(Screen::encyclopedia);
      for (size_t i = 0; i < shownTools.size(); ++i)
        if (shownTools[i] == id) {
          SendMessageW(toolList, LB_SETCURSEL, i, 0);
          selectTool();
          break;
        }
      setText(argsBox, draft.dump(2));
      break;
    }
    case TOOL_RUN:
      executeTool();
      break;
    case TOOL_CANCEL:
      manualRunner.cancel();
      break;
    case REVIEW_AGENT:
      reviewAgent(false);
      break;
    case LAND_AGENT:
      if (MessageBoxW(window, L"Run the repository's result-landing tool in this agent's isolated worktree? It may change src/ and requires port-only mode to be disabled. Review the resulting diff before committing.", L"Land driver results", MB_YESNO | MB_ICONQUESTION) == IDYES) fleet->land(selectedId);
      break;
    case COMMIT_AGENT:
      reviewAgent(true);
      break;
    case ATLAS_CART:
      if (pickedFunction != SIZE_MAX &&
          std::find(cart.begin(), cart.end(), pickedFunction) == cart.end())
        cart.push_back(pickedFunction);
      InvalidateRect(window, nullptr, FALSE);
      break;
    case ADD_CART: {
      if (!fleet || selectedId.empty())
        throw std::runtime_error("Add and select an AI in Controller first");
      Json rows = Json::array();
      for (auto i : cart)
        rows.push_back(atlas.at(i).row);
      fleet->enqueue(selectedId, rows);
      cart.clear();
      navigate(Screen::controller);
      break;
    }
    case ATLAS_RESET:
      zoom = 1;
      panX = panY = 0;
      InvalidateRect(window, nullptr, FALSE);
      break;
    case ATLAS_LIVE:
      if (!atlasReady)
        throw std::runtime_error("Wait for the current atlas load");
      liveAtlas = !liveAtlas;
      loadAtlas();
      build();
      break;
    case ATLAS_LOAD:
      loadAtlas();
      break;
    case TOUR_NEXT:
      tourStep = std::min(4, tourStep + 1);
      build();
      break;
    case TOUR_PREVIOUS:
      tourStep = std::max(0, tourStep - 1);
      build();
      break;
    case TOUR_CLOSE:
      navigate(Screen::controller);
      break;
    }
  }
  void tick() {
    refreshAgents();
    if (screen == Screen::controller)
      for (size_t i = 0; i < agents.size(); ++i) {
        auto b = GetDlgItem(window, 5000 + (int)i * 16);
        if (b)
          SetWindowTextW(b, agents[i].active ? L"Stop" : L"Go");
      }
    std::string out;
    {
      std::lock_guard<std::mutex> lock(outputMutex);
      out.swap(pending);
    }
    if (logBox && !out.empty())
      appendText(logBox, out);
    if (screen == Screen::detail) {
      auto a = activeAgent();
      if (a) {
        setText(body, a->spec.name + " · " + a->phase + "\n" + a->detail +
                          "\nWorktree: " + utf8(a->worktree.wstring()) + "\nBranch: " + a->branch +
                          "\n" + std::to_string(a->completed) + " worked; " +
                          std::to_string(a->queue.size()) +
                          " queued\nLog: " + utf8(a->log.wstring()));
        if (!a->log.empty() && out.empty() && GetWindowTextLengthW(logBox) == 0)
          setText(logBox, tailFile(a->log));
      }
    }
    if (screen == Screen::controller || screen == Screen::atlas)
      InvalidateRect(window, nullptr, FALSE);
  }
  static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto self = (Impl *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
      self = (Impl *)((CREATESTRUCTW *)l)->lpCreateParams;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
      self->window = h;
    }
    if (!self)
      return DefWindowProcW(h, msg, w, l);
    try {
      switch (msg) {
      case WM_ERASEBKGND:
        return 1;
      case WM_PAINT: {
        PAINTSTRUCT ps;
        auto dc = BeginPaint(h, &ps);
        HDC buffer = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, self->width, self->height);
        auto prev = SelectObject(buffer, bitmap);
        skin::background(buffer, self->width, self->height);
        self->paint(buffer);
        BitBlt(dc, 0, 0, self->width, self->height, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, prev);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(h, &ps);
        return 0;
      }
      case WM_PRINTCLIENT:
        skin::background((HDC)w, self->width, self->height);
        self->paint((HDC)w);
        return 0;
      case WM_DRAWITEM: {
        auto item = (DRAWITEMSTRUCT *)l;
        if (item->CtlType == ODT_BUTTON) {
          skin::button(*item,
                       item->CtlID == GO || item->CtlID == TOOL_RUN || item->CtlID == SAVE_PROFILE,
                       item->CtlID == STOP || item->CtlID == TOOL_CANCEL);
          return TRUE;
        }
        if (item->CtlType == ODT_COMBOBOX) {
          FillRect(item->hDC, &item->rcItem, self->fieldBrush);
          if (item->itemID != (UINT)-1) {
            int n = (int)SendMessageW(item->hwndItem, CB_GETLBTEXTLEN, item->itemID, 0);
            std::wstring title(std::max(0,n)+1, 0);
            if (n >= 0) SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, (LPARAM)title.data());
            auto oldFont = SelectObject(item->hDC, self->font);
            SetTextColor(item->hDC, skin::text()); SetBkMode(item->hDC, TRANSPARENT);
            RECT bounds = item->rcItem; bounds.left += 8; bounds.right -= 4;
            DrawTextW(item->hDC, title.c_str(), -1, &bounds, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(item->hDC, oldFont);
          }
          return TRUE;
        }
        break;
      }
      case WM_CTLCOLORSTATIC:
      case WM_CTLCOLOREDIT:
      case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)w, skin::text());
        SetBkColor((HDC)w, skin::field());
        SetBkMode((HDC)w, OPAQUE);
        if (self->fieldBrush)
          DeleteObject(self->fieldBrush);
        self->fieldBrush = CreateSolidBrush(skin::field());
        return (LRESULT)self->fieldBrush;
      case WM_COMMAND:
        self->action(LOWORD(w), HIWORD(w));
        return 0;
      case WM_TIMER:
        self->tick();
        return 0;
      case WM_MOUSEWHEEL:
        if (self->screen == Screen::atlas) {
          POINT p{(short)LOWORD(l), (short)HIWORD(l)};
          ScreenToClient(h, &p);
          auto old = self->zoom;
          self->zoom = std::clamp(old * (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1.25 : 0.8), 1., 64.);
          double ratio = self->zoom / old;
          self->panX = (p.x - 18) - (p.x - 18 - self->panX) * ratio;
          self->panY = (p.y - 100) - (p.y - 100 - self->panY) * ratio;
          InvalidateRect(h, nullptr, FALSE);
          return 0;
        }
        self->scroll = std::max(0, self->scroll - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 80);
        self->build();
        return 0;
      case WM_LBUTTONUP: {
        POINT point{(short)LOWORD(l), (short)HIWORD(l)};
        for (auto hit : self->hits)
          if (PtInRect(&hit.rect, point)) {
            if (self->screen == Screen::controller) {
              self->selectedId = self->agents.at(hit.index).id;
              self->navigate(Screen::detail);
            } else if (self->screen == Screen::atlas) {
              auto &f = self->atlas.at(hit.index);
              self->pickedFunction = hit.index;
              if (GetKeyState(VK_CONTROL) & 0x8000) {
                auto it = std::find(self->cart.begin(), self->cart.end(), hit.index);
                if (it == self->cart.end())
                  self->cart.push_back(hit.index);
                else
                  self->cart.erase(it);
              }
              setText(self->body, f.name + "\nModule: " + f.module + "\nState: " + f.state +
                                      "\nBytes: " + std::to_string(f.size) + "\n\n" +
                                      f.row.dump(2));
            }
            break;
          }
        return 0;
      }
      }
    } catch (const std::exception &e) {
      MessageBoxW(h, wide(e.what()).c_str(), L"TangOS Lite", MB_OK | MB_ICONERROR);
    }
    return DefWindowProcW(h, msg, w, l);
  }
};
ConsoleUI::ConsoleUI(HWND parent, HFONT font, fs::path repo, fs::path data, Settings settings,
                     std::function<void()> git, std::function<void(const Settings &)> save)
    : impl(std::make_unique<Impl>(parent, font, std::move(repo), std::move(data),
                                  std::move(settings), std::move(git), std::move(save))) {}
ConsoleUI::~ConsoleUI() = default;
void ConsoleUI::show(bool visible, bool atlas) {
  if (visible) {
    auto screen = atlas ? Screen::atlas : Screen::controller;
    if (impl->screen == Screen::controller || impl->screen == Screen::atlas)
      impl->navigate(screen);
  }
  ShowWindow(impl->window, visible ? SW_SHOW : SW_HIDE);
}
void ConsoleUI::resize(int width, int height) {
  if (width < 800 || height < 650 || (width == impl->width && height == impl->height))
    return;
  impl->width = width;
  impl->height = height;
  MoveWindow(impl->window, 14, 66, width, height, TRUE);
  impl->build();
}
bool ConsoleUI::running() const {
  return impl->manualBusy || (impl->fleet && impl->fleet->running());
}
void ConsoleUI::stop() {
  impl->manualRunner.cancel();
  if (impl->fleet)
    impl->fleet->stopAll();
}
void ConsoleUI::smokeScreens(const fs::path &directory,
                             const std::function<void(const fs::path &)> &capture) {
  if (impl->loader.joinable())
    impl->loader.join();
  if (!fs::exists(impl->repository / ".tangos-lite-test-fixture"))
    throw std::runtime_error("GUI fleet smoke requires an explicit disposable fixture");
  if (impl->fleet) {
    AgentSpec agent;
    agent.name = "Native CLI fixture";
    agent.kind = "cli";
    agent.cli =
        "python -c \"from pathlib import Path; import time; Path('port').mkdir(exist_ok=True); "
        "Path('port/fleet-fixture.txt').write_text('native fleet fixture'); "
        "print('port/fleet-fixture.txt:1: fleet workflow',flush=True); time.sleep(1)\"";
    impl->selectedId = impl->fleet->add(agent);
    impl->fleet->enqueue(impl->selectedId,
                         Json::array({{{"id", "native-fixture"}, {"name", "native-fixture"}}}));
    impl->fleet->start(impl->selectedId);
    impl->navigate(Screen::controller);
    auto started = GetTickCount64();
    int pumps = 0;
    while (impl->fleet->running()) {
      if (GetTickCount64() - started > 30000)
        throw std::runtime_error("Packaged fleet workflow timed out");
      MSG message;
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        ++pumps;
      }
      Sleep(10);
    }
    impl->refreshAgents();
    auto state = impl->activeAgent();
    if (!state || state->phase != "review" || pumps < 3 ||
        !fs::exists(state->worktree / "port/fleet-fixture.txt"))
      throw std::runtime_error("Packaged fleet workflow failed");
    impl->navigate(Screen::detail);
    impl->tick();
    setText(impl->logBox, impl->fleet->review(impl->selectedId));
    write(directory / "fleet-gui-report.txt",
          "PASS: packaged CLI agent, isolated worktree, instructions, independent checks, live UI, "
          "retained log, full diff review. UI messages=" +
              std::to_string(pumps));
    capture(directory / "console-4.bmp");
  }
  for (auto screen : {Screen::controller, Screen::atlas, Screen::encyclopedia, Screen::settings,
                      Screen::profile, Screen::tour}) {
    impl->navigate(screen);
    ShowWindow(impl->window, SW_SHOW);
    capture(directory / fs::u8path("console-" + std::to_string((int)screen) + ".bmp"));
  }
  impl->navigate(Screen::controller);
}
} // namespace lite
