#include "console_ui.h"
#include "help.h"
#include "atlas_layout.h"
#include "viewer.h"
#include "backend.h"
#include "network.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <chrono>
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
    return text(h);
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
constexpr int ATLAS_COLOR = 4204, ATLAS_AUTHOR = 4205, ATLAS_DRAFTS = 4206;
constexpr int TOOL_FORM = 4300, TOOL_FORM_SAVE = 4301, TOOL_FORM_NEXT = 4302, TOOL_FORM_PREV = 4303;
enum class Screen {
  controller,
  atlas,
  encyclopedia,
  settings,
  detail,
  profile,
  tour,
  parameters,
  functionDetail,
  connections,
  services,
  queue,
  requirements,
  descriptorGate,
  mcpConnection,
  batches,
  remoteGate,
  clone,
  support
};
constexpr int REQUIREMENTS = 4480, REQ_REFRESH = 4481, REQ_TERMINAL = 4482, REQ_GITHUB = 4483,
              REQ_COPY = 4484;
constexpr int QUEUE = 4470, QUEUE_UP = 4471, QUEUE_DOWN = 4472, QUEUE_REMOVE = 4473,
              TOOL_ENABLE = 4474, QUEUE_TOP = 4475, QUEUE_BOTTOM = 4476;
constexpr int CONNECTIONS = 4450, CONNECTION_LIST = 4451, CONNECTION_SAVE = 4452, SERVICES = 4453,
              SERVICE_RUN = 4454, SERVICE_CONFIRM = 4455, SERVICE_CANCEL = 4456;
constexpr int ATLAS_FUNCTION_LIST = 4410, ATLAS_FUNCTION_SORT = 4411, ATLAS_CONTRIBUTORS = 4412;
constexpr int ATLAS_INSPECT = 4400, ATLAS_MODULE = 4401, ATLAS_SOURCE = 4402, ATLAS_HISTORY = 4403;
constexpr int HELP_EDIT = 4600, HELP_TIPS = 4601;
constexpr int DESC_SCAN = 4610, DESC_PREVIEW = 4611, DESC_CONFIRM = 4612, DESC_RELOAD = 4613,
              DESC_FOLDER = 4614;
constexpr int REMOTE_FOLDER = 4800, REMOTE_CLONE = 4801;
constexpr int SUPPORT = 4850, SUPPORT_CHECK = 4851, SUPPORT_REPORT = 4852, SUPPORT_CONFIRM = 4853,
              SUPPORT_COPY = 4854, SUPPORT_FOLDER = 4855, SUPPORT_RELEASE = 4856;
constexpr int MCP_EXPORT = 4830, DETAIL_LOOP = 4840;
constexpr int CLONE_DEST = 4810, CLONE_PREVIEW = 4811, CLONE_CONFIRM = 4812, CLONE_CANCEL = 4813;
constexpr int MCP_TOGGLE = 4620, MCP_CONFIG = 4621, MCP_PROMPT = 4622, MCP_COPY_CONFIG = 4623;
constexpr int BATCHES = 4630, BATCH_LIST = 4631, BATCH_UP = 4632, BATCH_DOWN = 4633,
              BATCH_REMOVE = 4634, BATCH_CLEAR_DONE = 4635, BATCH_SAVE_DRAFT = 4636,
              BATCH_ENQUEUE = 4637, BATCH_CART_DRAFT = 4638, BATCH_LOG = 4639, BATCH_HANDOFF = 4640,
              BATCH_GENERATE = 4641, BATCH_CANCEL_GEN = 4642, BATCH_IMPORT = 4643,
              BATCH_EXPORT = 4644;
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
  std::unique_ptr<McpServer> mcp, pendingMcp;
  std::thread mcpWorker;
  std::atomic<bool> mcpBusy{false}, mcpReady{false};
  bool mcpDesired = true;
  std::string mcpError, pendingMcpError;
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
  bool tipsMode = false;
  Json guideSteps = Json::array();
  std::string descriptorOperation;
  Json descriptorDraft = Json::object(), descriptorWriteArgs = Json::object();
  std::vector<AtlasFunction> atlas;
  std::vector<size_t> filtered, cart;
  std::vector<Hit> hits;
  std::string atlasError;
  std::thread loader, manualWorker;
  Runner manualRunner;
  Runner serviceRunner;
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
  HWND batchList = nullptr, batchTitle = nullptr, batchPrompt = nullptr;
  Json batchRows = Json::array(), draftRows = Json::array();
  std::atomic<bool> draftReady{false};
  std::string draftError;
  Json statsBaseline = Json::object();
  Json presence = Json::array();
  std::string batchShown, selectedBatch;
  ULONGLONG batchPoll = 0;
  HWND functionList = nullptr, functionSort = nullptr, contributorList = nullptr;
  size_t hoveredFunction = SIZE_MAX;
  std::string cacheNotice;
  bool forceLiveReload = false;
  std::string functionSortKey = "unmatched";
  const std::vector<std::string> functionSortKeys = {"unmatched", "size-desc", "size-asc",
                                                     "name",      "addr",      "module"};
  std::vector<size_t> functionRows;
  HWND layoutChoice = nullptr, filterChoice = nullptr, colorChoice = nullptr,
       authorChoice = nullptr, draftsChoice = nullptr;
  std::string atlasColorBy = "status", authorFilter;
  bool atlasDrafts = true;
  std::vector<std::pair<std::string, int>> contributorRank;
  AtlasCamera camera;
  double &zoom = camera.zoom, &panX = camera.x, &panY = camera.y;
  AtlasLod lod;
  bool panning = false, marquee = false, miniDragging = false, historyTab = false;
  POINT dragStart{}, dragLast{}, dragEnd{};
  bool leftDown = false, dragged = false, additiveMarquee = false, flying = false;
  AtlasCamera flightFrom, flightTo;
  ULONGLONG flightAt = 0, lastTravel = 0;

  RECT miniBounds{};
  std::string moduleFilter, inspectText, historyText;
  std::map<size_t, std::string> sourceCache;
  std::thread inspector;
  std::atomic<bool> inspectBusy{false}, inspectReady{false};
  size_t inspectIndex = SIZE_MAX, pendingInspection = SIZE_MAX;
  unsigned atlasGeneration = 1, inspectGeneration = 0;
  Json inspectResult;
  struct Popup {
    HWND window = nullptr;
    std::unique_ptr<ConsoleUI> ui;
  };
  std::vector<std::unique_ptr<Popup>> popups;
  bool viewerOnly = false;
  std::function<void(Json)> draftAdded;
  Json policy = Json::object(), connectionProfiles = Json::object(), serviceResult = Json::object(),
       pendingServiceResult, serviceRequest = Json::object();
  std::vector<std::string> connectionNames;
  std::string serviceMethod = "preflight", activeServiceMethod;
  std::string cloneUrl, cloneDestination;
  Json cloneArguments;
  Json agentStats = Json::object(), pendingStats;
  std::thread statsWorker;
  std::atomic<bool> statsBusy{false}, statsReady{false};
  ULONGLONG statsPoll = 0;
  std::thread serviceWorker;
  std::atomic<bool> serviceBusy{false}, serviceReady{false};
  HWND serviceChoice = nullptr, serviceArguments = nullptr, detailProgress = nullptr;
  std::string supportDescription;

  std::vector<Tile> tiles;
  std::string atlasQuery, atlasMode = "ov", atlasFilter = "all", cachedLayout;
  bool liveAtlas = false, remoteOnly = false, remoteAtlasStarted = false;
  Transport transport;
  std::map<std::string, std::string> atlasAliases, atlasAuthorColors;
  std::atomic<bool> atlasPublished{false};
  std::vector<std::string> retainedCart;
  std::string retainedSelection;
  Json atlasExtras = Json::object();
  fs::file_time_type atlasModified{};
  ULONGLONG atlasPoll = 0;

  Impl(HWND p, HFONT f, fs::path repo, fs::path d, Settings prefs, std::function<void()> git,
       std::function<void(const Settings &)> save, bool onlyViewer, std::string module,
       std::function<void(Json)> addDraft, bool remote, Transport http)
      : parent(p), font(f), repository(std::move(repo)), data(std::move(d)),
        settings(std::move(prefs)), gitTools(std::move(git)), savePreferences(std::move(save)),
        vault(data / "vault") {
    viewerOnly = onlyViewer;
    remoteOnly = remote;
    transport = std::move(http);
    moduleFilter = std::move(module);
    draftAdded = std::move(addDraft);
    if (viewerOnly)
      screen = Screen::atlas;
    try {
      descriptor = loadDescriptor(repository);
    } catch (const std::exception &e) {
      descriptorError = e.what();
      descriptor.title = utf8(repository.filename().wstring());
    }
    try {
      policy = Backend(repository, data, settings, vault.values()).invoke("preferences.get");
      skin::animate(policy.value("animateBackground", true));
    } catch (const std::exception &e) {
      descriptorError += std::string("\nPreferences: ") + e.what();
    }
    auto prefsFile = data / "console-ui.json";
    if (fs::exists(prefsFile)) {
      auto j = Json::parse(read(prefsFile), nullptr, false);
      if (!j.is_object())
        j = Json::object();
      advancedMode = j.contains("advanced") && j["advanced"] == true;
      allowWrites = j.contains("writes") && j["writes"] == true;
      functionSortKey = j.value("functionSort", std::string("unmatched"));
      if (std::find(functionSortKeys.begin(), functionSortKeys.end(), functionSortKey) ==
          functionSortKeys.end())
        functionSortKey = "unmatched";
      atlasColorBy = j.contains("atlasColorBy") && j["atlasColorBy"].is_string()
                         ? j["atlasColorBy"].get<std::string>()
                         : "status";
      if (atlasColorBy != "author")
        atlasColorBy = "status";
      mcpDesired = !j.contains("mcpDesired") || j["mcpDesired"] != false;
      atlasDrafts = !j.contains("atlasDrafts") || !j["atlasDrafts"].is_boolean() ||
                    j["atlasDrafts"].get<bool>();
    }
    if (descriptorError.empty()) {
      try {
        statsBaseline = Backend(repository, data, settings).invoke("stats.get");
      } catch (...) {
      }
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
      if (!viewerOnly && !remoteOnly) {
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
        if (mcpDesired)
          mcp = std::make_unique<McpServer>(*fleet, descriptor, project / "mcp-client.json");
      }
      atlasReady = true;
      if (!remoteOnly)
        loadAtlas();
      else
        screen = Screen::remoteGate;
    } else {
      atlasError = descriptorError;
      atlasReady = true;
      if (!viewerOnly)
        screen = Screen::descriptorGate;
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
    SetTimer(window, 1, 50, nullptr);
    build();
  }
  ~Impl() {
    for (auto &popup : popups) {
      popup->ui.reset();
      if (popup->window)
        DestroyWindow(popup->window);
    }
    KillTimer(window, 1);
    manualRunner.cancel();
    serviceRunner.cancel();
    if (manualWorker.joinable())
      manualWorker.join();
    if (mcpWorker.joinable())
      mcpWorker.join();
    pendingMcp.reset();
    mcp.reset();
    fleet.reset();
    if (loader.joinable())
      loader.join();
    if (inspector.joinable())
      inspector.join();
    if (serviceWorker.joinable())
      serviceWorker.join();
    if (statsWorker.joinable())
      statsWorker.join();
    DestroyWindow(window);
    if (fieldBrush)
      DeleteObject(fieldBrush);
  }
  HWND control(const wchar_t *kind, const std::string &title, int id, int x, int y, int w, int h,
               DWORD style = 0) {
    if (std::wstring(kind) == L"BUTTON" && !(style & BS_AUTOCHECKBOX))
      style |= BS_OWNERDRAW;
    std::string windowsTitle;
    for (size_t i = 0; i < title.size(); ++i) {
      if (title[i] == '\n' && (i == 0 || title[i - 1] != '\r'))
        windowsTitle += '\r';
      windowsTitle += title[i];
    }
    auto c = CreateWindowExW(0, kind, wide(windowsTitle).c_str(),
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, x, y, w, h, window,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
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
    batchList = batchTitle = batchPrompt = functionList = functionSort = nullptr;
    functionRows.clear();
    layoutChoice = filterChoice = colorChoice = authorChoice = draftsChoice = nullptr;
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
    if (remoteOnly && next == Screen::controller)
      next = Screen::remoteGate;
    if (remoteOnly && next == Screen::atlas && !remoteAtlasStarted) {
      remoteAtlasStarted = true;
      liveAtlas = true;
      loadAtlas();
    }
    if (screen == Screen::batches && next != screen && batchTitle)
      storeBatchDraft();
    screen = next;
    scroll = 0;
    build();
  }
  bool controllerNeedsRail() const {
    auto req = descriptor.document.value("requirements", Json::object());
    if (!req.empty())
      return true;
    try {
      return !fs::exists(confinedPath(repository, descriptor.database));
    } catch (...) {
      return true;
    }
  }
  int controllerWidth() const { return width - (controllerNeedsRail() ? 356 : 0); }
  RECT agentCardBounds(size_t index) const {
    int cardWidth = std::max(60, (controllerWidth() - 60) / 3);
    int cardHeight = advancedMode ? 272 : 210;
    int x = 18 + int(index % 3) * (cardWidth + 12);
    int y = 64 + int(index / 3) * (cardHeight + 12) - scroll;
    return {x, y, x + cardWidth, y + cardHeight};
  }
  void build() {
    struct BuildGuard {
      bool &flag;
      BuildGuard(bool &f) : flag(f) { flag = true; }
      ~BuildGuard() { flag = false; }
    } guard(rebuilding);
    refreshAgents();
    destroyControls();
    int cw = screen == Screen::remoteGate   ? width
             : screen == Screen::controller ? controllerWidth()
                                            : width - 356;
    if (screen == Screen::remoteGate) {
      label(descriptor.title + " isn't on this machine", 50, 96, width - 100, 46);
      label("Browse the project's published progress in Chaos Viewer. Running tools and agents "
            "needs a local Git checkout.",
            50, 158, width - 100, 66);
      button("Open Chaos Viewer", ATLAS, 50, 246, 200);
      button("Choose a local folder", REMOTE_FOLDER, 50, 292, 220);
      button("Clone project", REMOTE_CLONE, 50, 338, 160);
      button("Connections", CONNECTIONS, 50, 384, 160);
      auto project = descriptor.document.value("project", Json::object());
      label(project.value("github", std::string()) + "\n" + descriptor.tagline, 50, 446,
            width - 100, 100);
    } else if (screen == Screen::clone) {
      label("Clone URL (your Git credential helper supplies authentication)", 28, 65, cw - 56);
      profileFields["Clone URL"] = edit(cloneUrl, 0, 28, 98, cw - 56);
      label("New destination folder", 28, 140, cw - 56);
      profileFields["Clone destination"] = edit(cloneDestination, 0, 28, 172, cw - 170);
      button("Choose…", CLONE_DEST, cw - 130, 172, 100);
      button("Preview clone", CLONE_PREVIEW, 28, 222, 140);
      button("Confirm clone", CLONE_CONFIRM, 180, 222, 140);
      button("Cancel", CLONE_CANCEL, 332, 222, 100);
      body = edit(serviceResult.dump(2), 0, 28, 275, cw - 56, height - 350,
                  ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      button("Back", HOME, 28, height - 48, 90);
      EnableWindow(GetDlgItem(window, CLONE_CONFIRM),
                   serviceResult.value("requiresConfirmation", false));
    } else if (screen == Screen::descriptorGate) {
      label(fs::exists(repository / "tangos.json") ? "That tangos.json has problems"
                                                   : "No tangos.json here yet",
            18, 62, cw - 36, 36);
      label(descriptorError, 18, 106, cw - 36, 80);
      argsBox = edit(descriptorDraft.empty() ? "Generate a draft, review it, and preview the write."
                                             : descriptorDraft.dump(2),
                     0, 18, 192, cw - 36, height - 370, ES_MULTILINE | WS_VSCROLL | WS_HSCROLL);
      button("Generate descriptor", DESC_SCAN, 18, height - 160, 178);
      button("Preview write", DESC_PREVIEW, 204, height - 160, 136);
      button("Confirm write", DESC_CONFIRM, 348, height - 160, 140);
      EnableWindow(GetDlgItem(window, DESC_CONFIRM),
                   serviceResult.value("requiresConfirmation", false) &&
                       descriptorOperation == "descriptor.write");
      button("Reload descriptor", DESC_RELOAD, 18, height - 112, 164);
      button("Different folder", DESC_FOLDER, 190, height - 112, 146);
      body = edit(serviceResult.empty()
                      ? "This changes only tangos.json after a concrete preview and confirmation."
                      : serviceResult.dump(2),
                  0, width - 332, 72, 310, height - 142, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
    } else if (screen == Screen::controller) {
      cardIds.clear();
      for (size_t i = 0; i < agents.size(); ++i) {
        auto &a = agents[i];
        cardIds.push_back(a.id);
        auto bounds = agentCardBounds(i);
        int x = bounds.left, y = bounds.top, w = bounds.right - x, h = bounds.bottom - y;
        if (y >= 62 && y + h <= height - 100) {
          int base = 5000 + (int)i * 16;
          button("Details", base + 1, x + w - 70, y + 8, 58);
          profileFields[a.id + "Count"] =
              edit(!advancedMode && a.spec.loop ? std::string() : std::to_string(a.spec.count),
                   base + 4, x + 12, y + h - 48, 54);
          button(a.active ? "Stop" : "Go", base, x + 74, y + h - 48, w - 86);
          if (advancedMode) {
            std::vector<std::string> roles = {"Unassigned", "Hard matcher", "Drafter", "Refiner",
                                              "Random"};
            int at = 0;
            for (size_t r = 0; r < roles.size(); ++r)
              if (roles[r] == a.spec.role)
                at = (int)r;
            profileFields[a.id + "Role"] =
                combo(roles, base + 2, x + 12, y + h - 126, (w - 32) / 2, at);
            profileFields[a.id + "Effort"] = combo({"off", "low", "medium", "high"}, base + 3,
                                                   x + 20 + (w - 32) / 2, y + h - 126, (w - 32) / 2,
                                                   a.spec.effort == "off"      ? 0
                                                   : a.spec.effort == "low"    ? 1
                                                   : a.spec.effort == "medium" ? 2
                                                                               : 3);
            int third = (w - 40) / 3;
            button("Assign", base + 5, x + 12, y + h - 86, third);
            button("Clear", base + 6, x + 20 + third, y + h - 86, third);
            button("Cart", base + 7, x + 28 + third * 2, y + h - 86, third);
          }
        }
      }
      button("Add AI", ADD_AGENT, cw - 108, 16, 92);
      if (!agents.empty()) {
        std::vector<std::string> names;
        int sel = 0;
        for (size_t i = 0; i < agents.size(); ++i) {
          names.push_back(agents[i].spec.name);
          if (agents[i].id == selectedId)
            sel = int(i);
        }
        agentChoice = combo(names, DETAIL, cw - 300, height - 48, 184, sel);
        button("Review", REVIEW_AGENT, cw - 108, height - 48, 92);
      }
      button("Tools", ENCYCLOPEDIA, 16, height - 48, 70);
      button("Settings", SETTINGS, 94, height - 48, 76);
      button("Git", GITTOOLS, 178, height - 48, 56);
      button("Tango", GUIDE, 242, height - 48, 60);
      button("Batches", BATCHES, 310, height - 48, 76);
      button("Help", SUPPORT, 394, height - 48, 94);
      button(mcp && mcp->state().value("running", false) ? "MCP: ON" : "MCP: OFF", OPEN_MCP,
             cw - 240, 16, 108);
      if (controllerNeedsRail()) {
        button("This repo needs", REQUIREMENTS, width - 332, 16, 188);
        button("Run logs", OPEN_LOG, width - 180, height - 116, 116);
      }
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
      button("Inspect source", ATLAS_INSPECT, width - 328, 330, 140);
      button("Pop out module", ATLAS_MODULE, width - 180, 330, 120);
      label("Wheel: zoom; left drag: pan\nRight drag: select; WASD/arrows: travel\nSpace: cart; "
            "Esc: zoom out",
            width - 332, 378, 310, 80);
      label("Color", width - 332, 477, 62);
      colorChoice = combo({"status", "author"}, ATLAS_COLOR, width - 268, 474, 246,
                          atlasColorBy == "author" ? 1 : 0);
      label("Contributor", width - 332, 513, 88);
      std::vector<std::string> contributors{"Everyone"};
      int authorIndex = 0;
      for (auto &entry : contributorRank) {
        contributors.push_back(entry.first);
        if (entry.first == authorFilter)
          authorIndex = (int)contributors.size() - 1;
      }
      authorChoice = combo(contributors, ATLAS_AUTHOR, width - 238, 510, 216, authorIndex);
      draftsChoice = control(L"BUTTON", "Show drafts and near misses", ATLAS_DRAFTS, width - 332,
                             550, 310, 28, BS_AUTOCHECKBOX);
      SendMessageW(draftsChoice, BM_SETCHECK, atlasDrafts ? BST_CHECKED : BST_UNCHECKED, 0);
      button("Controller", HOME, 18, height - 48, 108);
      button("Reload", ATLAS_LOAD, 134, height - 48, 90);
      button(remoteOnly ? "Connections" : "Encyclopedia", remoteOnly ? CONNECTIONS : ENCYCLOPEDIA,
             232, height - 48, 126);
      button("Reset view", ATLAS_RESET, 366, height - 48, 104);
      button(remoteOnly  ? "Reload published"
             : liveAtlas ? "Local data"
                         : "Live data",
             ATLAS_LIVE, 478, height - 48, 102);
      auto sortIndex =
          std::find(functionSortKeys.begin(), functionSortKeys.end(), functionSortKey) -
          functionSortKeys.begin();
      functionSort = combo({"unmatched first", "size (largest)", "size (smallest)", "name (A-Z)",
                            "address", "module"},
                           ATLAS_FUNCTION_SORT, width - 332, 90, 310, (int)sortIndex);
      functionList = control(L"LISTBOX", "", ATLAS_FUNCTION_LIST, width - 332, 130, 310, 140,
                             LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT);
      contributorList = control(L"LISTBOX", "", ATLAS_CONTRIBUTORS, 18, 104, cw - 36, 32,
                                LBS_NOTIFY | LBS_MULTICOLUMN | WS_HSCROLL | LBS_NOINTEGRALHEIGHT);
      SendMessageW(contributorList, LB_SETCOLUMNWIDTH, 200, 0);
      cachedLayout.clear();
    } else if (screen == Screen::encyclopedia) {
      search = edit("", SEARCH, 18, 52, cw - 34);
      toolList = control(L"LISTBOX", "", TOOL_LIST, 18, 94, 225, height - 230,
                         LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT);
      body = edit("", 0, 255, 94, cw - 273, height - 325, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      argsBox = edit("{}", TOOL_ARGS, 255, height - 220, cw - 273, 106, ES_MULTILINE | WS_VSCROLL);
      button("Run", TOOL_RUN, 255, height - 98, 76);
      button("Cancel", TOOL_CANCEL, 339, height - 98, 88);
      button("Edit arguments", TOOL_FORM, 435, height - 98, 142);
      button("Tool visibility", TOOL_ENABLE, 18, height - 98, 164);
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
      for (auto &key : vault.values())
        if (std::find(keys.begin(), keys.end(), key.first) == keys.end())
          keys.push_back(key.first);
      keyChoice = control(L"COMBOBOX", "", 0, 18, 378, cw - 36, 300, CBS_DROPDOWN | WS_VSCROLL);
      for (auto &key : keys)
        SendMessageW(keyChoice, CB_ADDSTRING, 0, (LPARAM)wide(key).c_str());
      if (!keys.empty())
        SendMessageW(keyChoice, CB_SETCURSEL, 0, 0);
      keyEdit = edit("", 0, 18, 419, cw - 36, 30, ES_PASSWORD | ES_AUTOHSCROLL);
      button("Store key", VAULT_SAVE, 18, 460, 108);
      button("Remove key", VAULT_REMOVE, 136, 460, 118);
      button("Save settings", SAVE_SETTINGS, 18, height - 98, 136);
      button("Close", HOME, 18, height - 48, 90);
      int py = 58;
      for (auto entry : std::vector<std::pair<std::string, std::string>>{
               {"animateBackground", "Animate background"},
               {"allowNearMiss", "Allow near-miss tips"},
               {"allowGhidra", "Allow Ghidra drafts"},
               {"safeMode", "Safe mode: block agent writes"},
               {"reports", "Save activity reports"},
               {"useAgents", "Allow agent delegation"},
               {"autoLand", "Auto-land in agent worktree"},
               {"liveRefresh", "Live refresh (enabled profiles)"}}) {
        auto h = control(L"BUTTON", entry.second, 0, width - 332, py, 310, 32, BS_AUTOCHECKBOX);
        SendMessageW(h, BM_SETCHECK,
                     policy.value(entry.first, entry.first == "animateBackground" ||
                                                   entry.first == "allowNearMiss"),
                     0);
        profileFields["policy:" + entry.first] = h;
        py += 38;
      }
      label("Delegation limit", width - 332, py, 150);
      profileFields["policy:agentFanout"] =
          edit(std::to_string(policy.value("agentFanout", 8)), 0, width - 172, py, 120);
      py += 44;
      button("Connections", CONNECTIONS, width - 332, py, 136);
      button("Project services", SERVICES, width - 188, py, 156);
      py += 44;
      body = edit(settingsSummary(), 0, width - 332, py, 310, std::max(60, height - py - 30),
                  ES_MULTILINE | ES_READONLY | WS_VSCROLL);
    } else if (screen == Screen::requirements) {
      body = edit(preflightSummary(serviceResult), 0, 18, 62, cw - 36, height - 176,
                  ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      button("Check again", REQ_REFRESH, 18, height - 98, 118);
      button("Copy steps", REQ_COPY, 144, height - 98, 112);
      button("Open terminal", REQ_TERMINAL, 264, height - 98, 136);
      button("Controller", HOME, 18, height - 48, 110);
      label("Requirements", width - 332, 62, 310, 32);
      label(
          "Your repository supplies its tools, compiler and ROM inputs. Installations and external "
          "account setup are yours to control. Run the steps shown on the left, then check again.",
          width - 332, 110, 310, 180);
      button("GitHub sign-in", REQ_GITHUB, width - 332, 320, 156);
      button("Encyclopedia", ENCYCLOPEDIA, width - 332, 366, 140);
    } else if (screen == Screen::queue) {
      auto a = activeAgent();
      toolList = control(L"LISTBOX", "", 0, 18, 62, cw - 36, height - 190, LBS_NOTIFY | WS_VSCROLL);
      if (a)
        for (auto &row : a->queue)
          SendMessageW(toolList, LB_ADDSTRING, 0,
                       (LPARAM)wide(row.value("module", std::string()) + " · " +
                                    row.value("name", row.value("id", std::string("target"))))
                           .c_str());
      button("Move up", QUEUE_UP, 18, height - 108, 100);
      button("Move down", QUEUE_DOWN, 126, height - 108, 110);
      button("Remove", QUEUE_REMOVE, 244, height - 108, 100);
      button("To top", QUEUE_TOP, 352, height - 108, 84);
      button("To bottom", QUEUE_BOTTOM, 444, height - 108, 100);
      button("Clear queue", CLEAR_QUEUE, width - 332, 260, 150);
      button("AI detail", DETAIL, 18, height - 48, 110);
      button("Controller", HOME, 136, height - 48, 110);
      label("Queued targets are reserved across the fleet. Stop the agent before reordering or "
            "removing work. Targets added during a run remain queued for the next batch.",
            width - 332, 62, 310, 180);
    } else if (screen == Screen::connections) {
      connectionProfiles =
          Backend(repository, data, settings, vault.values()).invoke("connections.get");
      connectionNames.clear();
      toolList = control(L"LISTBOX", "", CONNECTION_LIST, 18, 58, 180, height - 160,
                         LBS_NOTIFY | WS_VSCROLL);
      for (auto it = connectionProfiles.begin(); it != connectionProfiles.end(); ++it) {
        connectionNames.push_back(it.key());
        SendMessageW(toolList, LB_ADDSTRING, 0, (LPARAM)wide(it.key()).c_str());
      }
      int y = 58;
      for (auto field :
           std::vector<std::pair<std::string, std::string>>{{"Name", "atlas.live"},
                                                            {"URL", ""},
                                                            {"Key variable", ""},
                                                            {"Key header", "Authorization"},
                                                            {"Key prefix", "Bearer "}}) {
        label(field.first, 214, y + 4, 110);
        profileFields[field.first] = edit(field.second, 0, 328, y, cw - 348);
        y += 40;
      }
      label("Method", 214, y + 4, 110);
      profileFields["Method"] =
          combo({"GET", "POST", "PUT", "PATCH", "DELETE"}, 0, 328, y, cw - 348);
      y += 40;
      for (auto entry : std::vector<std::pair<std::string, std::string>>{
               {"Enabled", "Enable this connection"},
               {"Automatic", "Allow automatic leases / project discovery"},
               {"Descriptors", "Allow public GitHub descriptor downloads"}}) {
        profileFields[entry.first] =
            control(L"BUTTON", entry.second, 0, 214, y, cw - 232, 30, BS_AUTOCHECKBOX);
        y += 38;
      }
      label("Lease body template (optional JSON)", 214, y, cw - 232);
      y += 28;
      profileFields["Template"] = edit("{}", 0, 214, y, cw - 232, std::max(60, height - y - 110),
                                       ES_MULTILINE | WS_VSCROLL);
      button("Preview save", CONNECTION_SAVE, 214, height - 64, 140);
      button("Settings", SETTINGS, 18, height - 48, 108);
      body = edit(
          "Connections belong to you. Paste your service's endpoint and reference a key stored in "
          "the vault or your environment. Connections start disabled.\n\nUse atlas.live, "
          "atlas.cosmetics, atlas.counts, atlas.progress, claims or update.check as read profiles. "
          "Remote leases require claims.acquire, claims.heartbeat and claims.release.\n\nNo keys "
          "belong in these fields. Review the complete configuration before saving.",
          0, width - 332, 62, 310, height - 132, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
    } else if (screen == Screen::services) {
      std::vector<std::string> methods = {
          "preflight",      "claims.read",     "stats.get",       "harvest.list",
          "reports.list",   "projects.list",   "connections.get", "preferences.get",
          "git.status",     "git.syncPreview", "git.sync",        "git.clone",
          "github.credits", "atlas.cosmetics", "atlas.counts",    "atlas.progress",
          "update.check",   "reports.export",  "stats.clear",     "git.backup"};
      if (std::find(methods.begin(), methods.end(), serviceMethod) == methods.end())
        methods.push_back(serviceMethod);
      auto at = std::find(methods.begin(), methods.end(), serviceMethod) - methods.begin();
      serviceChoice = combo(methods, 0, 18, 58, cw - 36, (int)at);
      label("Arguments (advanced; leave {} for ordinary checks)", 18, 100, cw - 36);
      serviceArguments =
          edit(serviceRequest.dump(2), 0, 18, 128, cw - 36, 110, ES_MULTILINE | WS_VSCROLL);
      button("Inspect / preview", SERVICE_RUN, 18, 252, 146);
      button("Confirm preview", SERVICE_CONFIRM, 172, 252, 150);
      button("Cancel", SERVICE_CANCEL, 330, 252, 90);
      EnableWindow(GetDlgItem(window, SERVICE_CONFIRM),
                   serviceResult.value("requiresConfirmation", false));
      body = edit(serviceResult.dump(2), 0, 18, 296, cw - 36, std::max(60, height - 360),
                  ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL);
      button("Controller", HOME, 18, height - 48, 110);
      button("Settings", SETTINGS, 136, height - 48, 108);
      label("Inspect requirements, claims, reports, recovery and connections. Destructive changes "
            "require a separate confirmation of the full preview. Credentials remain on your "
            "computer.",
            width - 332, 62, 310, 180);
    } else if (screen == Screen::support) {
      label("Help, updates and reports", 18, 60, cw - 36, 32);
      label("Updates use your enabled update.check connection. Portable binaries are replaced "
            "manually after you verify the release. Reports stay local until you choose to share "
            "them.",
            18, 100, cw - 36, 80);
      profileFields["Report description"] =
          edit(supportDescription, 0, 18, 188, cw - 36, 100, ES_MULTILINE | WS_VSCROLL);
      button("Check updates", SUPPORT_CHECK, 18, 304, 140);
      button("Preview report", SUPPORT_REPORT, 166, 304, 140);
      button("Save report", SUPPORT_CONFIRM, 314, 304, 130);
      body = edit(serviceResult.dump(2), 0, 18, 354, cw - 36, height - 460,
                  ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      button("Copy report", SUPPORT_COPY, width - 332, 108, 170);
      button("Open export folder", SUPPORT_FOLDER, width - 332, 154, 210);
      button("Tour", GUIDE, width - 332, 216, 150);
      button("Tips", HELP_TIPS, width - 332, 262, 150);
      button("Connections", CONNECTIONS, width - 332, 308, 150);
      button("Open release page", SUPPORT_RELEASE, width - 332, 354, 190);
      button("Controller", HOME, 18, height - 48, 108);
      EnableWindow(GetDlgItem(window, SUPPORT_CONFIRM),
                   serviceResult.value("requiresConfirmation", false));
    } else if (screen == Screen::functionDetail) {
      button("Viewer", ATLAS, 18, height - 48, 100);
      button("Source", ATLAS_SOURCE, 18, 54, 100);
      button("Prior tries", ATLAS_HISTORY, 126, 54, 110);
      button("Add to cart", ATLAS_CART, 244, 54, 120);
      body = edit(historyTab ? historyText : inspectText, 0, 18, 96, cw - 36, height - 158,
                  ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL);
      if (pickedFunction < atlas.size()) {
        auto &f = atlas[pickedFunction];
        label(f.name + "\nModule: " + f.module + "\nState: " + f.state +
                  "\nBytes: " + std::to_string(f.size),
              width - 332, 62, 310, 180);
      }
    } else if (screen == Screen::batches) {
      if (!fleet)
        throw std::runtime_error("Load a valid repository descriptor first");
      batchRows = fleet->batches();
      auto staged = fleet->draft();
      draftRows = staged.at("items");
      batchList = control(L"LISTBOX", "", BATCH_LIST, 18, 64, 255, height - 212,
                          LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT);
      for (auto &batch : batchRows) {
        int worked = 0;
        for (auto &row : batch.at("items"))
          if (row.value("worked", false))
            ++worked;
        auto name = wide(batch.at("status").get<std::string>() + " · " +
                         batch.at("title").get<std::string>() + " · " + std::to_string(worked) +
                         "/" + std::to_string(batch.at("items").size()));
        auto index = SendMessageW(batchList, LB_ADDSTRING, 0, (LPARAM)name.c_str());
        if (batch.at("id") == selectedBatch)
          SendMessageW(batchList, LB_SETCURSEL, index, 0);
      }
      label("Draft title", 291, 62, cw - 309);
      batchTitle = edit(staged.value("title", std::string("Batch draft")), 0, 291, 90, cw - 309);
      label("Instructions", 291, 130, cw - 309);
      batchPrompt = edit(staged.value("prompt", std::string()), 0, 291, 158, cw - 309, 110,
                         ES_MULTILINE | WS_VSCROLL);
      label(std::to_string(draftRows.size()) + " targets in saved draft", 291, 276, cw - 309);
      body = edit("Select a queued or completed batch. Worked targets still require independent "
                  "byte-match proof.",
                  0, 291, 310, cw - 309, height - 473, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      button("Up", BATCH_UP, 18, height - 134, 62);
      button("Down", BATCH_DOWN, 88, height - 134, 70);
      button("Remove", BATCH_REMOVE, 166, height - 134, 98);
      button("Clear done", BATCH_CLEAR_DONE, 18, height - 88, 116);
      button("Use Viewer cart", BATCH_CART_DRAFT, 291, height - 134, 148);
      button("Save draft", BATCH_SAVE_DRAFT, 447, height - 134, 104);
      button("Enqueue draft", BATCH_ENQUEUE, 559, height - 134, 126);
      button("Controller", HOME, 18, height - 48, 108);
      button("Viewer", ATLAS, 134, height - 48, 96);
      label("Assign saved draft to", width - 332, 64, 310);
      std::vector<std::string> names{"Unassigned / global queue"};
      int active = 0;
      for (size_t i = 0; i < agents.size(); ++i) {
        names.push_back(agents[i].spec.name);
        if (agents[i].id == selectedId)
          active = (int)i + 1;
      }
      agentChoice = combo(names, 0, width - 332, 96, 310, active);
      label("Queued/active batches and the draft persist with this project. Stop the assigned "
            "agent before removing or reordering a batch. Clear done removes history only; "
            "complete logs stay on disk.",
            width - 332, 146, 310, 150);
      button("Hand off selected", BATCH_HANDOFF, width - 332, 310, 194);
      button("Open batch logs", BATCH_LOG, width - 332, 354, 194);
      profileFields["Draft role"] =
          combo({"Hard matcher", "Drafter", "Refiner", "Random"}, 0, width - 332, 408, 200);
      profileFields["Draft count"] = edit("16", 0, width - 122, 408, 100);
      button("Generate draft", BATCH_GENERATE, width - 332, 450, 150);
      button("Cancel", BATCH_CANCEL_GEN, width - 174, 450, 100);
      button("Import draft", BATCH_IMPORT, width - 332, 496, 136);
      button("Export draft", BATCH_EXPORT, width - 188, 496, 136);
    } else if (screen == Screen::mcpConnection) {
      label("MCP server", 18, 55, cw - 36, 28);
      profileFields["MCP client"] =
          combo({"Claude Code", "Claude Desktop", "Cursor", "VS Code", "Generic"}, 0, width - 332,
                310, 310);
      button("Export client setup", MCP_EXPORT, width - 332, 354, 196);
      body = edit(mcpSummary(), 0, 18, 94, cw - 36, height - 225,
                  ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      button(mcpBusy ? "Working…" : (mcp ? "Stop server" : "Start server"), MCP_TOGGLE, 18,
             height - 108, 126);
      button("Open client config", MCP_CONFIG, 152, height - 108, 164);
      button("Copy config", MCP_COPY_CONFIG, 324, height - 108, 122);
      button("Copy AI prompt", MCP_PROMPT, 454, height - 108, 136);
      button("Controller", HOME, 18, height - 48, 108);
      label("Your clients and credentials", width - 332, 62, 310, 28);
      label("Add an MCP agent in Controller with its exact client name. Copy this local connection "
            "configuration into your chosen client. API accounts and keys remain your own. No "
            "external client configuration is changed automatically.",
            width - 332, 104, 310, 170);
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
      button("Open logs", OPEN_LOG, 346, 52, 144);
      detailProgress = control(PROGRESS_CLASSW, "", 0, 18, 87, cw - 36, 10);
      button("Toggle loop", DETAIL_LOOP, width - 332, 374, 170);
      body = edit("", 0, 18, 98, cw - 36, 310, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      logBox = edit("", 0, 18, 425, cw - 36, height - 504,
                    ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL);
      label("Independent verification & publication", width - 332, 57, 310, 40);
      auto a = activeAgent();
      label(a ? a->detail : "Select an agent", width - 332, 108, 310, 120);
      button("Commit reviewed", COMMIT_AGENT, width - 332, 250, 170);
      button("Land driver results", LAND_AGENT, width - 332, 290, 170);
      button("Manage queue", QUEUE, width - 332, 330, 170);
    } else if (screen == Screen::tour) {
      button("Previous", TOUR_PREVIOUS, 50, height - 108, 110);
      button("Next", TOUR_NEXT, 168, height - 108, 100);
      button("Done", TOUR_CLOSE, 276, height - 108, 96);
      guideSteps = readGuide(data, !tipsMode);
      tourStep = std::clamp(tourStep, 0, std::max(0, (int)guideSteps.size() - 1));
      auto &step = guideSteps.at(tourStep);
      body =
          edit(std::to_string(tourStep + 1) + " / " + std::to_string(guideSteps.size()) + " · " +
                   step.value("title", std::string()) + "\n\n" + step.value("body", std::string()),
               0, 50, 102, width - 456, height - 254, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      button("Edit text", HELP_EDIT, 384, height - 108, 100);
      button(tipsMode ? "Replay tour" : "Tips", HELP_TIPS, 492, height - 108, 110);
      EnableWindow(GetDlgItem(window, TOUR_PREVIOUS), tourStep > 0);
      EnableWindow(GetDlgItem(window, TOUR_NEXT), tourStep + 1 < (int)guideSteps.size());
    }
    if (remoteOnly)
      for (int id : std::vector<int>{ATLAS_CART, ADD_CART, ENCYCLOPEDIA, ATLAS_MODULE})
        if (auto h = GetDlgItem(window, id))
          EnableWindow(h, FALSE);
    if (viewerOnly)
      for (int id : std::vector<int>{HOME, ADD_CART, ENCYCLOPEDIA, ATLAS_MODULE}) {
        auto h = GetDlgItem(window, id);
        if (h)
          EnableWindow(h, FALSE);
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
    ++atlasGeneration;
    atlasError.clear();
    retainedCart.clear();
    for (auto i : cart)
      if (i < atlas.size())
        retainedCart.push_back(atlas[i].id);
    retainedSelection = pickedFunction < atlas.size() ? atlas[pickedFunction].id : std::string();
    atlas.clear();
    tiles.clear();
    cachedLayout.clear();
    pickedFunction = SIZE_MAX;
    cart.clear();
    sourceCache.clear();
    pendingInspection = SIZE_MAX;
    auto keys = vault.values();
    auto prefs = settings;
    bool published = liveAtlas, force = forceLiveReload;
    forceLiveReload = false;
    loader = std::thread([this, keys, prefs, published, force] {
      auto cachePath =
          data / "atlas-cache" /
          (std::to_string(std::hash<std::string>{}(utf8(repository.wstring()))) + ".json");
      std::string cacheKey;
      Json loadedDb;
      cacheNotice.clear();
      try {
        Backend backend(repository, data, prefs, keys, transport);
        auto profiles = backend.invoke("connections.get");
        auto enabled = [&](const std::string &name) {
          return profiles.contains(name) && profiles[name].value("enabled", false);
        };
        cacheKey = descriptor.document.value("data", Json::object())
                       .value("committedDbUrl", std::string()) +
                   "|" + profiles.dump();
        cacheKey = std::to_string(std::hash<std::string>{}(cacheKey));
        auto cached = published && !force
                          ? readAtlasCache(cachePath, cacheKey, std::time(nullptr), 30)
                          : Json();
        atlasExtras = Json::object();
        if (!cached.is_null()) {
          loadedDb = cached.at("database");
          atlas = parseAtlas(loadedDb.dump());
          atlasExtras = cached.value("extras", Json::object());
          cacheNotice = "Published cache (under 30 seconds old)";
        } else if (published && enabled("atlas.live")) {
          auto r = backend.invoke("atlas.live");
          if (!r.value("ok", false))
            throw std::runtime_error("Published Atlas service refused the request");
          loadedDb = r.at("data");
          atlas = parseAtlas(loadedDb.dump());
        } else {
          loadedDb =
              Json::parse(published ? fetchHttps(descriptor.document.value("data", Json::object())
                                                     .value("committedDbUrl", std::string()))
                                    : read(confinedPath(repository, descriptor.database)));
          atlas = parseAtlas(loadedDb.dump());
        }
        if (published && cached.is_null()) {
          for (auto name : {"atlas.cosmetics", "atlas.counts", "atlas.progress", "github.credits"})
            if (enabled(name)) {
              try {
                auto r = backend.invoke(name);
                if (r.value("ok", false))
                  atlasExtras[name] = r["data"];
              } catch (const std::exception &e) {
                atlasExtras["notes"].push_back(std::string(name) + ": " + e.what());
              }
            }
          if (enabled("claims"))
            try {
              auto claims = backend.invoke("claims.read", {{"connection", "claims"}});
              for (auto &f : atlas)
                if (heldTarget(f.row, claims))
                  f.row["claim"] = {{"status", "active"}};
            } catch (const std::exception &e) {
              atlasExtras["notes"].push_back(e.what());
            }
          if (atlasExtras.contains("atlas.progress")) {
            auto progress = atlasExtras["atlas.progress"];
            if (progress.value("ready", false) && progress.contains("matched") &&
                progress["matched"].is_array()) {
              for (auto &f : atlas) {
                bool matched = std::find(progress["matched"].begin(), progress["matched"].end(),
                                         f.id) != progress["matched"].end();
                f.row["matched"] = matched;
                f.state = matched ? "matched" : f.row.contains("div") ? "near_miss" : "unmatched";
              }
            }
          }
        }
        if (published && !loadedDb.is_null() && cacheNotice.empty()) {
          try {
            writeAtlasCache(cachePath, cacheKey, loadedDb, atlasExtras, std::time(nullptr));
          } catch (const std::exception &e) {
            atlasExtras["notes"].push_back(e.what());
          }
          cacheNotice = "Published data refreshed";
        }
      } catch (const std::exception &e) {
        auto cached =
            published ? readAtlasCache(cachePath, cacheKey, std::time(nullptr), -1) : Json();
        if (!cached.is_null()) {
          atlas = parseAtlas(cached.at("database").dump());
          atlasExtras = cached.value("extras", Json::object());
          cacheNotice = "Offline / stale cache: " + std::string(e.what());
        } else
          atlasError = e.what();
      }
      atlasPublished = true;
      atlasReady = true;
    });
  }
  static LRESULT CALLBACK popupProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto p = (Popup *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
      p = (Popup *)((CREATESTRUCTW *)l)->lpCreateParams;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p);
      p->window = h;
    }
    if (p) {
      if (msg == WM_SIZE && p->ui) {
        p->ui->resize(std::max(800, (int)LOWORD(l)), std::max(600, (int)HIWORD(l)) - 66);
        return 0;
      }
      if (msg == WM_CLOSE) {
        p->ui.reset();
        DestroyWindow(h);
        return 0;
      }
      if (msg == WM_NCDESTROY)
        p->window = nullptr;
      if (msg == WM_ERASEBKGND)
        return 1;
      if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        auto dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        skin::background(dc, r.right, r.bottom);
        skin::label(dc, L"Chaos Viewer · module", 18, 18, r.right - 36, 30, 18, true);
        EndPaint(h, &ps);
        return 0;
      }
    }
    return DefWindowProcW(h, msg, w, l);
  }
  void openModule(const std::string &module) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = popupProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TangOSLiteModule";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    auto popup = std::make_unique<Popup>();
    auto h = CreateWindowExW(0, wc.lpszClassName, wide(module + " · Chaos Viewer").c_str(),
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                             1160, 820, parent, nullptr, wc.hInstance, popup.get());
    if (!h)
      throw std::runtime_error("Could not open module window");
    popup->ui = std::make_unique<ConsoleUI>(
        h, font, repository, data, settings, [] {}, std::function<void(const Settings &)>{}, true,
        module,
        [this](Json row) {
          auto id = row.value("id", std::string());
          for (size_t i = 0; i < atlas.size(); ++i)
            if (atlas[i].id == id && atlas[i].state != "matched" && !exemptTarget(atlas[i].row) &&
                !claimedTarget(atlas[i].row) &&
                std::find(cart.begin(), cart.end(), i) == cart.end()) {
              cart.push_back(i);
              break;
            }
          InvalidateRect(window, nullptr, FALSE);
        });
    RECT r;
    GetClientRect(h, &r);
    popup->ui->resize(r.right - 28, r.bottom - 66);
    popup->ui->show(true, true);
    popups.push_back(std::move(popup));
    ShowWindow(h, SW_SHOWNORMAL);
  }
  void storeBatchDraft() {
    if (!fleet || !batchTitle || !batchPrompt)
      throw std::runtime_error("Open batch drafts first");
    fleet->saveDraft(
        {{"title", text(batchTitle)}, {"prompt", text(batchPrompt)}, {"items", draftRows}});
  }
  void selectBatch() {
    auto index = SendMessageW(batchList, LB_GETCURSEL, 0, 0);
    if (index < 0 || size_t(index) >= batchRows.size())
      return;
    auto &batch = batchRows.at(size_t(index));
    selectedBatch = batch.at("id").get<std::string>();
    std::string summary = batch.at("title").get<std::string>() +
                          "\nAgent: " + batch.at("targetAgent").get<std::string>() +
                          "\nStatus: " + batch.at("status").get<std::string>() + "\n\n" +
                          batch.at("prompt").get<std::string>() + "\n\nTargets\n";
    for (auto &row : batch.at("items"))
      summary += (row.value("removed", false)  ? "Removed  "
                  : row.value("worked", false) ? "Worked   "
                                               : "Pending  ") +
                 batchTarget(row) + "\n";
    summary +=
        "\n" +
        batch.value("note", std::string("Worked targets require independent byte-match proof."));
    setText(body, summary);
  }
  std::string mcpSummary() {
    if (mcpBusy)
      return "Changing MCP server state… The UI remains responsive.";
    if (!mcp)
      return "Server stopped.\n" + mcpError;
    auto state = mcp->state();
    std::string summary = "Status: running\nURL: " + state.at("url").get<std::string>() +
                          "\nClients: " + std::to_string(state.at("connectedClients").get<int>()) +
                          "\nTraffic: " + std::to_string(state.at("requestsSeen").get<uint64_t>()) +
                          " requests\n";
    for (auto &client : state.at("clients")) {
      auto elapsed = std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::system_clock::now().time_since_epoch())
                                                  .count() -
                                              client.at("lastSeen").get<int64_t>()) /
                     1000;
      summary += "\n" + client.at("name").get<std::string>() + " · last contact " +
                 std::to_string(elapsed) + "s ago";
    }
    summary += "\n\nOnly assigned isolated worktrees are exposed. Follow AGENTS.md from "
               "next_batch; independent checks and user review are required.\nSessions disconnect "
               "using DELETE /mcp or expire after 30 minutes without contact.";
    return summary;
  }
  std::string mcpPrompt() {
    if (!mcp || mcpBusy)
      throw std::runtime_error("Start the MCP server first");
    return "Connect your MCP client to TangOS Lite using the configuration below. Add an MCP agent "
           "in Controller with exactly the same clientInfo.name as your client. The user must "
           "assign/start its batch. Call next_batch, follow the returned AGENTS.md instructions, "
           "work only in its isolated worktree, and call finish_batch for independent "
           "verification. Never commit, push, modify protected src/ in port-only mode, or include "
           "ROMs, Nintendo assets or credentials.\n\n" +
           mcp->configuration();
  }
  void toggleMcp() {
    if (!fleet)
      throw std::runtime_error("Load a valid repository descriptor first");
    if (mcpBusy || mcpReady)
      return;
    if (mcpWorker.joinable())
      mcpWorker.join();
    bool starting = !mcp;
    auto old = std::move(mcp);
    mcpBusy = true;
    mcpError.clear();
    mcpWorker = std::thread([this, starting, old = std::move(old)]() mutable {
      std::unique_ptr<McpServer> next;
      std::string error;
      try {
        if (starting)
          next = std::make_unique<McpServer>(*fleet, descriptor, data / "mcp-client.json");
        else
          old.reset();
      } catch (const std::exception &e) {
        error = e.what();
      }
      {
        std::lock_guard<std::mutex> lock(outputMutex);
        pendingMcp = std::move(next);
        pendingMcpError = error;
      }
      mcpReady = true;
    });
    if (screen == Screen::mcpConnection)
      build();
  }
  static void copyText(HWND owner, const std::string &value) {
    if (!OpenClipboard(owner))
      throw std::runtime_error("Clipboard is busy");
    auto text = wide(value);
    auto memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!memory) {
      CloseClipboard();
      throw std::runtime_error("Could not allocate clipboard text");
    }
    auto bytes = GlobalLock(memory);
    memcpy(bytes, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(memory);
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, memory))
      GlobalFree(memory);
    CloseClipboard();
  }
  std::string statisticsSummary(const std::string &id) const {
    auto stat = agentStats.value(id, Json::object());
    std::string out = "Lifetime statistics\n\nDeclared matches: " +
                      std::to_string(stat.value("declaredMatches", 0)) +
                      "\nUnique attempted functions: " + std::to_string(stat.value("attempts", 0)) +
                      "\nNear misses: " + std::to_string(stat.value("nearMisses", 0));
    out += "\nHit rate: " + std::to_string((int)std::round(stat.value("hitRate", 0.0) * 100)) + "%";
    if (stat.contains("tokensIn"))
      out += "\nTokens in: " + std::to_string(stat.value("tokensIn", 0LL));
    if (stat.contains("tokensOut"))
      out += "\nTokens out: " + std::to_string(stat.value("tokensOut", 0LL));
    if (stat.contains("tokensPerMatch"))
      out += "\nTokens per declared match: " + std::to_string(stat.value("tokensPerMatch", 0LL));
    if (stat.contains("bySize")) {
      out += "\n\nSize · attempted / declared matches";
      for (auto i = stat["bySize"].begin(); i != stat["bySize"].end(); ++i)
        out += "\n" + i.key() + " · " + std::to_string(i.value().value("attempts", 0)) + " / " +
               std::to_string(i.value().value("matches", 0));
    }
    auto baseline = statsBaseline.value(id, Json::object());
    out += "\n\nThis session: " +
           std::to_string(stat.value("attempts", 0) - baseline.value("attempts", 0)) +
           " attempted, " +
           std::to_string(stat.value("declaredMatches", 0) - baseline.value("declaredMatches", 0)) +
           " declared matches";
    for (auto &client : presence)
      if (client.value("agentId", std::string()) == id)
        out += "\nMCP connected: " + client.value("name", std::string()) + " (last activity " +
               std::to_string(std::max<int64_t>(
                   0, (std::time(nullptr) * int64_t(1000) - client.value("lastSeen", int64_t(0))) /
                          1000)) +
               "s ago)";
    return out + "\n\nDeclarations require independent matching proof before publication.";
  }
  static std::string preflightSummary(const Json &r) {
    if (r.empty())
      return "Checking repository requirements...";
    if (r.contains("error"))
      return r["error"].get<std::string>();
    std::string out = "This repo needs\n\n";
    for (auto &item : r.value("checks", Json::array())) {
      out += (item.value("available", false) ? "PASS  " : "MISSING  ") +
             item.value("name", std::string()) + "\n";
      if (!item.value("available", false))
        out +=
            "  " +
            item.value(
                "fix",
                item.value(
                    "command",
                    std::string("Inspect the declared repository tool and its required inputs."))) +
            "\n";
    }
    out += "\nPython packages\n";
    for (auto &item : r.value("pythonPackages", Json::array()))
      out += (item.value("available", false) ? "PASS  " : "MISSING  ") +
             item.value("package", std::string()) + "\n  " + item.value("fix", std::string()) +
             "\n";
    out += "\nPython\n" + r.value("python", Json::object()).value("detail", std::string()) + "\n";
    out += "\nGitHub authentication\n" +
           r.value("github", Json::object()).value("detail", std::string()) + "\n";
    out += "\nSetup commands you run yourself\ngh auth login\n";
    out += "For Python packages, use the repository's pinned requirements. Compiler and ROM paths "
           "belong in local settings.\n\n";
    out += r.value("instructions", std::string());
    return out;
  }
  void serviceCall(const std::string &method, Json args) {
    if (remoteOnly && method != "git.clone" && method != "projects.list" &&
        method != "projects.get" && method != "connections.get" && method != "connections.set" &&
        method != "preferences.get" && method != "preferences.set" && method != "network.read" &&
        method != "update.check" && method != "bug.report" && method.rfind("atlas.", 0) != 0 &&
        method != "github.credits")
      throw std::runtime_error(
          "Clone or choose a local checkout before running repository operations");
    if (serviceBusy || serviceReady)
      throw std::runtime_error("Wait for the current service operation");
    if (serviceWorker.joinable())
      serviceWorker.join();
    serviceRunner.reset();
    serviceBusy = true;
    activeServiceMethod = method;
    auto prefs = settings;
    auto keys = vault.values();
    serviceWorker = std::thread([this, method, args, prefs, keys] {
      Json result;
      try {
        result = Backend(repository, data, prefs, keys, transport, &serviceRunner,
                         [this](const std::string &line) {
                           std::lock_guard<std::mutex> lock(outputMutex);
                           pending += line;
                         })
                     .invoke(method, args);
      } catch (const std::exception &e) {
        result = {{"error", e.what()}};
      }
      {
        std::lock_guard<std::mutex> lock(outputMutex);
        pendingServiceResult = std::move(result);
      }
      serviceReady = true;
      serviceBusy = false;
    });
    if (screen == Screen::services) {
      setText(body, "Working...");
      EnableWindow(GetDlgItem(window, SERVICE_CONFIRM), FALSE);
    }
  }
  void selectConnection() {
    auto at = (int)SendMessageW(toolList, LB_GETCURSEL, 0, 0);
    if (at < 0 || at >= (int)connectionNames.size())
      return;
    auto name = connectionNames[at];
    auto c = connectionProfiles[name];
    setText(profileFields["Name"], name);
    for (auto field : std::vector<std::pair<std::string, std::string>>{{"URL", "url"},
                                                                       {"Key variable", "keyEnv"},
                                                                       {"Key header", "keyHeader"},
                                                                       {"Key prefix", "keyPrefix"}})
      setText(profileFields[field.first],
              c.value(field.second, field.second == "keyHeader"   ? std::string("Authorization")
                                    : field.second == "keyPrefix" ? std::string("Bearer ")
                                                                  : std::string()));
    std::vector<std::string> methods = {"GET", "POST", "PUT", "PATCH", "DELETE"};
    auto it = std::find(methods.begin(), methods.end(), c.value("method", std::string("GET")));
    SendMessageW(profileFields["Method"], CB_SETCURSEL,
                 it == methods.end() ? 0 : it - methods.begin(), 0);
    SendMessageW(profileFields["Enabled"], BM_SETCHECK, c.value("enabled", false), 0);
    SendMessageW(profileFields["Automatic"], BM_SETCHECK, c.value("automatic", false), 0);
    SendMessageW(profileFields["Descriptors"], BM_SETCHECK,
                 c.value("allowDescriptorDownloads", false), 0);
    setText(profileFields["Template"], c.value("bodyTemplate", Json::object()).dump(2));
  }
  void requestInspection(size_t index) {
    if (!atlasReady || index >= atlas.size())
      return;
    if (inspectBusy || inspectReady) {
      pendingInspection = index;
      return;
    }
    pendingInspection = SIZE_MAX;
    if (inspector.joinable())
      inspector.join();
    inspectBusy = true;
    inspectIndex = index;
    inspectGeneration = atlasGeneration;
    auto f = atlas[index];
    auto prefs = settings;
    auto keys = vault.values();
    inspector = std::thread([this, f, prefs, keys] {
      Json r = Json::object();
      try {
        Backend backend(repository, data, prefs, keys);
        auto path = f.row.contains("srcPath") && f.row["srcPath"].is_string()
                        ? f.row["srcPath"].get<std::string>()
                        : std::string();
        if (path.empty() && f.row.contains("sourcePath") && f.row["sourcePath"].is_string())
          path = f.row["sourcePath"].get<std::string>();
        auto source = backend.invoke("atlas.source", {{"id", f.id}, {"srcPath", path}});
        if (source.is_null() && f.row.contains("disasm") && f.row["disasm"].is_string() &&
            !f.row["disasm"].get<std::string>().empty() &&
            blockedBlob(f.row["disasm"].get<std::string>()).empty())
          source = sourceEnvelope(f.row["disasm"].get<std::string>(), "disasm");
        if (source.is_null())
          r["source"] = {{"source", "No source or disassembly is available for this function."}};
        else {
          std::string text;
          for (size_t i = 0; i < source.at("lines").size(); ++i) {
            if (i)
              text += '\n';
            text += source.at("lines")[i].get<std::string>();
          }
          r["source"] = {
              {"source", text}, {"kind", source.at("kind")}, {"truncated", source.at("truncated")}};
        }
        r["history"] = backend.invoke("atlas.history", f.row);
      } catch (const std::exception &e) {
        r["error"] = e.what();
      }
      {
        std::lock_guard<std::mutex> lock(outputMutex);
        inspectResult = std::move(r);
      }
      inspectReady = true;
      inspectBusy = false;
    });
  }
  void inspectFunction() {
    if (pickedFunction >= atlas.size())
      throw std::runtime_error("Select a function first");
    inspectText = "Loading source...";
    historyText = "Loading prior tries...";
    historyTab = false;
    if (sourceCache.count(pickedFunction))
      inspectText = sourceCache[pickedFunction];
    requestInspection(pickedFunction);
    navigate(Screen::functionDetail);
  }
  void flyRect(ViewRect rect) {
    if (rect.width <= 0 || rect.height <= 0)
      return;
    flightFrom = camera;
    flightTo = camera;
    flightTo.zoom = std::clamp(
        .92 * std::min((width - 390) / rect.width, (height - 218) / rect.height), 1., 4096.);
    flightTo.center(rect.x + rect.width / 2, rect.y + rect.height / 2, width - 390, height - 218);
    flightAt = GetTickCount64();
    flying = true;
  }
  void flyFunction(size_t index) {
    for (auto t : tiles)
      if (t.index == index) {
        pickedFunction = index;
        flyRect({t.x, t.y, t.width, t.height});
        requestInspection(index);
        break;
      }
  }
  void viewerKey(WPARAM key) {
    if (key == VK_ESCAPE) {
      if (lod.update(zoom) == 3 && pickedFunction < atlas.size()) {
        double x = 1e100, y = 1e100, right = 0, bottom = 0;
        for (auto t : tiles)
          if (atlas[t.index].module == atlas[pickedFunction].module) {
            x = std::min(x, t.x);
            y = std::min(y, t.y);
            right = std::max(right, t.x + t.width);
            bottom = std::max(bottom, t.y + t.height);
          }
        flyRect({x, y, right - x, bottom - y});
      } else {
        flightFrom = camera;
        flightTo = AtlasCamera();
        flightAt = GetTickCount64();
        flying = true;
        pickedFunction = SIZE_MAX;
      }
      return;
    }
    if (!remoteOnly && key == VK_SPACE && pickedFunction < atlas.size()) {
      auto &f = atlas[pickedFunction];
      if (f.state == "matched" || exemptTarget(f.row) || claimedTarget(f.row))
        return;
      auto at = std::find(cart.begin(), cart.end(), pickedFunction);
      if (at == cart.end()) {
        cart.push_back(pickedFunction);
        if (draftAdded)
          draftAdded(f.row);
      } else
        cart.erase(at);
      InvalidateRect(window, nullptr, FALSE);
      return;
    }
    int dx = key == VK_LEFT || key == 'A' ? -1 : key == VK_RIGHT || key == 'D' ? 1 : 0;
    int dy = key == VK_UP || key == 'W' ? -1 : key == VK_DOWN || key == 'S' ? 1 : 0;
    if ((dx || dy) && lod.update(zoom) >= 2 && GetTickCount64() - lastTravel >= 160) {
      if (pickedFunction == SIZE_MAX && !tiles.empty())
        pickedFunction = tiles.front().index;
      auto next = atlasNeighbor(atlas, tiles, pickedFunction, dx, dy);
      if (next < atlas.size())
        flyFunction(next);
      lastTravel = GetTickCount64();
    }
  }
  void moveMini(POINT p) {
    double mw = std::max(1L, miniBounds.right - miniBounds.left),
           mh = std::max(1L, miniBounds.bottom - miniBounds.top);
    camera.center((p.x - miniBounds.left) * (width - 390) / mw,
                  (p.y - miniBounds.top) * (height - 218) / mh, width - 390, height - 218);
  }
  void paint(HDC dc) {
    bool fullController =
        screen == Screen::remoteGate || (screen == Screen::controller && !controllerNeedsRail());
    skin::panel(dc, 0, 0, fullController ? width : width - 356, height);
    if (!fullController)
      skin::panel(dc, width - 340, 0, 340, height, true);
    std::string title = screen == Screen::remoteGate       ? "Viewer-only project"
                        : screen == Screen::controller     ? "Chaos Controller"
                        : screen == Screen::atlas          ? "Chaos Viewer"
                        : screen == Screen::encyclopedia   ? "Encyclopedia"
                        : screen == Screen::settings       ? "Settings"
                        : screen == Screen::profile        ? "Connect AI"
                        : screen == Screen::detail         ? "AI detail"
                        : screen == Screen::descriptorGate ? "Repository setup"
                        : screen == Screen::batches        ? "Batch queues and history"
                        : screen == Screen::mcpConnection  ? "MCP connection"
                        : screen == Screen::requirements   ? "Requirements"
                        : screen == Screen::connections    ? "Connections"
                        : screen == Screen::services       ? "Project services"
                        : screen == Screen::queue          ? "AI queue"
                        : screen == Screen::functionDetail ? "Function inspection"
                        : screen == Screen::parameters     ? "Run dock · arguments"
                                                           : "Welcome to tangOS Lite";
    skin::label(dc, wide(title), 16, 17, width - 390, 28, 16, true);
    hits.clear();
    if (screen == Screen::controller) {
      if (agents.empty())
        skin::label(dc, L"No AIs connected yet.", width / 4 - 105, 105, 360, 28, 15, false, true);
      for (size_t i = 0; i < agents.size(); i++) {
        auto &a = agents[i];
        auto b = agentCardBounds(i);
        int x = b.left, y = b.top, w = b.right - x, h = b.bottom - y;
        if (y < 62 || b.bottom > height - 100)
          continue;
        static const COLORREF palette[] = {RGB(0, 153, 224),  RGB(125, 75, 216), RGB(230, 25, 75),
                                           RGB(245, 130, 49), RGB(5, 150, 105),  RGB(219, 39, 119),
                                           RGB(14, 165, 233), RGB(217, 119, 6),  RGB(67, 99, 216),
                                           RGB(168, 50, 50),  RGB(0, 160, 176),  RGB(145, 30, 180)};
        uint32_t colorHash = 0;
        for (auto c : wide(a.spec.name))
          colorHash = colorHash * 31 + uint16_t(c);
        skin::agentCard(dc, x, y, w, h, palette[colorHash % 12]);
        bool connected = false;
        for (auto &client : presence)
          if (client.value("agentId", std::string()) == a.id)
            connected = true;
        skin::label(dc, wide(((a.active || connected) ? "● " : "○ ") + a.spec.name), x + 12, y + 9,
                    w - 92, 26, 15, true, false, true);
        skin::panel(dc, x + 12, y + 46, w - 24, advancedMode ? 90 : 104);
        skin::label(dc, wide(a.detail.empty() ? "idle · " + a.spec.role : a.detail), x + 22, y + 54,
                    w - 44, 56, 12, false, true);
        skin::label(dc, wide(a.lastLine), x + 22, y + 108, w - 44, 24, 11, false, true);
        skin::label(dc,
                    wide(a.spec.kind + " · " + (connected ? "connected" : a.phase) + " · " +
                         std::to_string(a.completed) + "/" + std::to_string(a.total) + " · " +
                         std::to_string(a.queue.size()) + " queued"),
                    x + 14, y + h - (advancedMode ? 154 : 80), w - 28, 24, 11, false, true);
        hits.push_back({b, int(i)});
      }
      if (controllerNeedsRail()) {
        skin::label(dc, L"Repository readiness", width - 322, 58, 305, 26, 15, true);
        int y2 = 100;
        for (auto &check : discoverChecks(repository, settings)) {
          if (y2 > height - 160)
            break;
          skin::label(dc, wide(std::string(check.available ? "✓ " : "○ ") + check.name),
                      width - 322, y2, 302, 24, 13, check.available, !check.available);
          y2 += 28;
        }
        if (!descriptorError.empty())
          skin::label(dc, wide("tangos.json: " + descriptorError), width - 322, y2 + 8, 302, 90, 12,
                      false, true);
      }
      skin::mascot(dc, width - 156, height - 200, 140);
    } else if (screen == Screen::atlas) {
      if (!atlasReady) {
        skin::label(dc, L"Loading atlas…", 18, 108, width - 390, 40, 15, false, true);
        return;
      }
      if (!atlasError.empty()) {
        skin::label(
            dc,
            wide("Atlas unavailable: " + atlasError +
                 (remoteOnly
                      ? "\nSet your published database URL in Connections, then reload."
                      : "\nGenerate the database using the repository's tools in Encyclopedia.")),
            18, 108, width - 390, 100, 14, false, true);
        return;
      }
      auto needle = search ? text(search) : atlasQuery;
      std::transform(needle.begin(), needle.end(), needle.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      uint64_t bytes = 0, matchedBytes = 0;
      size_t matched = 0;
      for (auto &f : atlas) {
        bytes += f.size;
        if (f.state == "matched") {
          ++matched;
          matchedBytes += f.size;
        }
      }
      skin::label(dc,
                  wide("Functions " + std::to_string(matched) + "/" + std::to_string(atlas.size()) +
                       " · Code " + std::to_string(matchedBytes) + "/" + std::to_string(bytes) +
                       " bytes · " + (liveAtlas ? cacheNotice : "Local data")),
                  18, 84, width - 390, 24, 12);
      int left = 18, top = 148, w = width - 390, h = height - 218;
      std::string key = needle + "|" + atlasMode + "|" + functionSortKey + "|" + atlasFilter + "|" +
                        moduleFilter + "|" + std::to_string(w) + "x" + std::to_string(h);
      if (key != cachedLayout) {
        filtered.clear();
        for (size_t i = 0; i < atlas.size(); i++) {
          auto hay =
              atlas[i].id + " " + atlas[i].name + " " + atlas[i].module + " " + atlas[i].state;
          std::transform(hay.begin(), hay.end(), hay.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          if ((needle.empty() || hay.find(needle) != hay.npos) &&
              (atlasFilter == "all" || atlas[i].state == atlasFilter) &&
              (moduleFilter.empty() || atlas[i].module == moduleFilter))
            filtered.push_back(i);
        }
        auto aliases = atlasExtras.value("github.credits", Json::object())
                           .value("keyToLogin", std::map<std::string, std::string>{});
        functionRows = atlasOrder(atlas, filtered, functionSortKey);
        if (functionRows.size() > 500)
          functionRows.resize(500); // Original Viewer roster cap.
        if (functionList) {
          SendMessageW(functionList, WM_SETREDRAW, FALSE, 0);
          SendMessageW(functionList, LB_RESETCONTENT, 0, 0);
          for (size_t i = 0; i < functionRows.size(); ++i) {
            auto &f = atlas[functionRows[i]];
            auto item =
                wide(f.name + " · " + f.module + " · " + std::to_string(f.size) + "b · " + f.state);
            SendMessageW(functionList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
            if (functionRows[i] == pickedFunction)
              SendMessageW(functionList, LB_SETCURSEL, i, 0);
          }
          SendMessageW(functionList, LB_SETHORIZONTALEXTENT, 900, 0);
          SendMessageW(functionList, WM_SETREDRAW, TRUE, 0);
          InvalidateRect(functionList, nullptr, TRUE);
        }
        tiles = atlasLayout(atlas, filtered, w, h, atlasMode, aliases);
        lod.compute(atlas, tiles, w, h);
        camera.clamp(w, h);
        atlasAliases = atlasExtras.value("github.credits", Json::object())
                           .value("keyToLogin", std::map<std::string, std::string>{});
        std::map<std::string, int> counts;
        auto totals =
            atlasExtras.value("atlas.counts", Json::object()).value("totals", Json::object());
        if (totals.is_object() && !totals.empty()) {
          for (auto it = totals.begin(); it != totals.end(); ++it)
            if (it.value().is_number())
              counts[it.key()] = it.value().get<int>();
        } else
          for (const auto &f : atlas)
            if (f.state == "matched" && f.row.contains("author") && f.row["author"].is_string()) {
              std::string who = f.row["author"];
              if (atlasAliases.count(who))
                who = atlasAliases.at(who);
              ++counts[who];
            }
        std::vector<std::pair<std::string, int>> ranked;
        for (auto &entry : counts) {
          auto folded = entry.first;
          std::transform(folded.begin(), folded.end(), folded.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          if (entry.second >= 1 &&
              (folded.size() < 5 || folded.substr(folded.size() - 5) != "[bot]"))
            ranked.push_back(entry);
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](auto a, auto b) { return a.second > b.second; });
        const std::vector<std::string> palette = {"#38bdf8", "#f472b6", "#a78bfa", "#fb923c",
                                                  "#facc15", "#34d399", "#f87171", "#22d3ee",
                                                  "#c084fc", "#fbbf24", "#4ade80", "#e879f9"};
        atlasAuthorColors.clear();
        auto shared = atlasExtras.value("atlas.cosmetics", Json::object())
                          .value("colors", std::map<std::string, std::string>{});
        std::map<std::string, std::string> normalized;
        for (auto &entry : shared) {
          auto folded = entry.first;
          std::transform(folded.begin(), folded.end(), folded.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          normalized[folded] = entry.second;
        }
        for (size_t i = 0; i < ranked.size(); ++i) {
          auto who = ranked[i].first;
          std::string folded = who;
          std::transform(folded.begin(), folded.end(), folded.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          atlasAuthorColors[who] =
              normalized.count(folded) ? normalized.at(folded) : palette[i % palette.size()];
        }
        contributorRank = ranked;
        if (contributorList) {
          SendMessageW(contributorList, LB_RESETCONTENT, 0, 0);
          SendMessageW(contributorList, LB_ADDSTRING, 0, (LPARAM)L"Everyone");
          int at = 1;
          for (auto &entry : ranked) {
            auto name = wide(entry.first + " · " + std::to_string(entry.second) + " matches");
            SendMessageW(contributorList, LB_ADDSTRING, 0, (LPARAM)name.c_str());
            if (entry.first == authorFilter)
              SendMessageW(contributorList, LB_SETCURSEL, at, 0);
            ++at;
          }
        }
        if (authorChoice) {
          SendMessageW(authorChoice, CB_RESETCONTENT, 0, 0);
          SendMessageW(authorChoice, CB_ADDSTRING, 0, (LPARAM)L"Everyone");
          int index = 0;
          for (auto &entry : ranked) {
            auto name = wide(entry.first);
            auto added = SendMessageW(authorChoice, CB_ADDSTRING, 0, (LPARAM)name.c_str());
            if (entry.first == authorFilter)
              index = (int)added;
          }
          SendMessageW(authorChoice, CB_SETCURSEL, index, 0);
        }
        cachedLayout = key;
      }
      int band = lod.update(zoom);
      int saved = SaveDC(dc);
      IntersectClipRect(dc, left, top, left + w, top + h);
      for (const auto &tile : tiles) {
        int x = left + (int)(tile.x * zoom + panX), y = top + (int)(tile.y * zoom + panY),
            tw = std::max(1, (int)(tile.width * zoom)), th = std::max(1, (int)(tile.height * zoom));
        if (x + tw < left || y + th < top || x > left + w || y > top + h)
          continue;
        auto &f = atlas[tile.index];
        auto hex = atlasColor(f.row, atlasColorBy == "author", atlasDrafts, atlasAliases,
                              atlasAuthorColors);
        COLORREF color = RGB(185, 202, 219);
        if (hex.size() == 7 && hex[0] == '#' &&
            hex.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos) {
          auto v = std::stoul(hex.substr(1), nullptr, 16);
          color = RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
        }
        std::string who = f.row.contains("author") && f.row["author"].is_string()
                              ? f.row["author"].get<std::string>()
                              : std::string();
        if (atlasAliases.count(who))
          who = atlasAliases.at(who);
        bool dimmed = !authorFilter.empty() && who != authorFilter;
        if (dimmed) {
          auto ground = skin::field();
          color = RGB((int)(GetRValue(color) * .14 + GetRValue(ground) * .86),
                      (int)(GetGValue(color) * .14 + GetGValue(ground) * .86),
                      (int)(GetBValue(color) * .14 + GetBValue(ground) * .86));
        }
        if (!dimmed && claimedTarget(f.row))
          color = RGB((int)(GetRValue(color) * .58 + 255 * .42),
                      (int)(GetGValue(color) * .58 + 90 * .42),
                      (int)(GetBValue(color) * .58 + 90 * .42));
        HBRUSH brush = (HBRUSH)GetStockObject(DC_BRUSH);
        SetDCBrushColor(dc, color);
        RECT bounds{x, y, x + tw - 1, y + th - 1};
        FillRect(dc, &bounds, brush);
        if (!dimmed && exemptTarget(f.row)) {
          int save = SaveDC(dc);
          IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
          auto old = SelectObject(dc, GetStockObject(DC_PEN));
          SetDCPenColor(dc, RGB(216, 140, 157));
          int vx = std::max(left, x), vy = std::max(top, y), vw = std::min(left + w, x + tw) - vx,
              vh = std::min(top + h, y + th) - vy;
          for (int d = -vh; d < vw; d += 8) {
            MoveToEx(dc, vx + d, vy, nullptr);
            LineTo(dc, vx + d + vh, vy + vh);
          }
          SelectObject(dc, old);
          RestoreDC(dc, save);
        }

        if (std::find(cart.begin(), cart.end(), tile.index) != cart.end() ||
            tile.index == pickedFunction) {
          SetDCBrushColor(dc, RGB(255, 214, 40));
          FrameRect(dc, &bounds, (HBRUSH)GetStockObject(DC_BRUSH));
        }
        if (tw > 90 && th > 24) {
          if (band != 1 || tile.group.empty())
            skin::label(dc, wide(f.name), x + 4, y + 3, tw - 8, 22, 11, true);
          if (atlasExtras.contains("atlas.cosmetics"))
            for (auto &star : atlasExtras["atlas.cosmetics"].value("stars", Json::array()))
              if ((star.value("function", std::string()) == f.name ||
                   star.value("function", std::string()) == f.id)) {
                skin::label(dc, L"★", x + tw - 22, y + 3, 20, 22, 15, true);
                break;
              }
          auto code = sourceCache.find(tile.index);
          if (band == 3 && tile.index == pickedFunction && tw > 180 && th > 90 &&
              code != sourceCache.end()) {
            int savedCode = SaveDC(dc);
            IntersectClipRect(dc, x + 1, y + 24, x + tw - 1, y + th - 1);
            skin::label(dc, wide(code->second.substr(0, 16000)), x + 6, y + 28, tw - 12, th - 34,
                        11);
            RestoreDC(dc, savedCode);
          }
        }
        RECT hit{std::max(left, x), std::max(top, y), std::min(left + w, x + tw),
                 std::min(top + h, y + th)};
        hits.push_back({hit, (int)tile.index});
      }
      if (band == 1) {
        std::map<std::string, RECT> sections;
        for (const auto &tile : tiles)
          if (!tile.group.empty()) {
            RECT rect{left + (int)(tile.x * zoom + panX), top + (int)(tile.y * zoom + panY),
                      left + (int)((tile.x + tile.width) * zoom + panX),
                      top + (int)((tile.y + tile.height) * zoom + panY)};
            auto it = sections.find(tile.group);
            if (it == sections.end())
              sections[tile.group] = rect;
            else {
              auto &r = it->second;
              r.left = std::min(r.left, rect.left);
              r.top = std::min(r.top, rect.top);
              r.right = std::max(r.right, rect.right);
              r.bottom = std::max(r.bottom, rect.bottom);
            }
          }
        for (auto &section : sections) {
          auto &r = section.second;
          if (r.right > left && r.bottom > top && r.left < left + w && r.top < top + h)
            skin::label(dc, wide(section.first), r.left + 4, r.top + 3,
                        std::max(0, (int)(r.right - r.left - 8)), 24, 11, true);
        }
      }
      if (marquee) {
        RECT r{std::min(dragStart.x, dragEnd.x), std::min(dragStart.y, dragEnd.y),
               std::max(dragStart.x, dragEnd.x), std::max(dragStart.y, dragEnd.y)};
        auto b = CreateSolidBrush(RGB(255, 214, 40));
        FrameRect(dc, &r, b);
        DeleteObject(b);
      }
      RestoreDC(dc, saved);
      miniBounds = {left + w - 154, top + h - 106, left + w - 8, top + h - 8};
      auto mb = CreateSolidBrush(skin::field());
      FillRect(dc, &miniBounds, mb);
      DeleteObject(mb);
      double mw = miniBounds.right - miniBounds.left, mh = miniBounds.bottom - miniBounds.top;
      for (const auto &t : tiles) {
        RECT r{miniBounds.left + (int)(t.x * mw / w), miniBounds.top + (int)(t.y * mh / h),
               miniBounds.left + (int)((t.x + t.width) * mw / w),
               miniBounds.top + (int)((t.y + t.height) * mh / h)};
        auto color = atlasColor(atlas[t.index].row, atlasColorBy == "author", atlasDrafts,
                                atlasAliases, atlasAuthorColors);
        COLORREF miniColor = RGB(185, 202, 219);
        if (color.size() == 7 && color[0] == '#' &&
            color.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos) {
          auto v = std::stoul(color.substr(1), nullptr, 16);
          miniColor = RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
        }
        SetDCBrushColor(dc, miniColor);
        FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
      }
      if (hoveredFunction < atlas.size() && !panning && !marquee) {
        auto &f = atlas[hoveredFunction];
        skin::panel(dc, left + 14, top + 14, std::min(w - 28, 430), 100);
        skin::label(dc,
                    wide(f.name + "\n" + f.module + " · " + f.state + " · " +
                         std::to_string(f.size) + " bytes\n" +
                         (f.row.contains("author") && f.row["author"].is_string()
                              ? f.row["author"].get<std::string>()
                              : std::string())),
                    left + 24, top + 22, std::min(w - 48, 410), 84, 12);
      }
      auto v = camera.visible(w, h);
      RECT vr{miniBounds.left + (int)(v.x * mw / w), miniBounds.top + (int)(v.y * mh / h),
              miniBounds.left + (int)((v.x + v.width) * mw / w),
              miniBounds.top + (int)((v.y + v.height) * mh / h)};
      mb = CreateSolidBrush(RGB(255, 214, 40));
      FrameRect(dc, &vr, mb);
      DeleteObject(mb);
      if (!authorFilter.empty()) {
        int lifetime = 0, daily = 0;
        for (auto &entry : contributorRank)
          if (entry.first == authorFilter)
            lifetime = entry.second;
        auto days =
            atlasExtras.value("atlas.counts", Json::object()).value("daily", Json::object());
        if (days.contains(authorFilter) && days[authorFilter].is_number())
          daily = days[authorFilter].get<int>();
        skin::label(dc,
                    wide(authorFilter + " · " + std::to_string(lifetime) + " matches · +" +
                         std::to_string(daily) + " today"),
                    width - 322, 584, 302, 48, 12);
      }
      skin::label(dc,
                  wide(std::to_string(filtered.size()) + " functions · " +
                       std::to_string(cart.size()) + " in cart"),
                  width - 322, 53, 302, 26, 13, true);
    } else if (screen == Screen::tour && !guideSteps.empty()) {
      skin::mascot(dc, width - 220, 120, 180,
                   guideSteps.at(tourStep).value("emotion", std::string("smile")));
    } else if (screen == Screen::settings || screen == Screen::profile)
      skin::label(dc, L"Local configuration", width - 322, 18, 302, 28, 15, true);
    else if (screen == Screen::detail && activeAgent()) {
      skin::label(dc, wide(activeAgent()->spec.name), width - 322, 18, 302, 28, 16, true);
      skin::label(dc, wide(statisticsSummary(activeAgent()->id)), width - 322, 384, 302,
                  height - 400, 12);
    }
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
    if (remoteOnly)
      throw std::runtime_error("Choose a local checkout before running tools");
    if (manualBusy)
      return;
    auto &tool = descriptor.tool(toolId);
    auto prefs = Backend(repository, data, settings, vault.values()).invoke("preferences.get");
    if (!enabledTool(prefs, tool.id))
      throw std::runtime_error("Tool disabled by user policy");
    if (!tool.readOnly && prefs.value("safeMode", false))
      throw std::runtime_error("Safe mode blocks mutating tools");
    if (settings.portOnly && !tool.readOnly)
      throw std::runtime_error(
          "Port-only mode blocks mutating descriptor tools in the primary checkout. Use an "
          "isolated agent worktree, or explicitly disable port-only mode.");
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
        auto count = trim(text(profileFields.at(a->id + "Count")));
        if (count.empty() && !advancedMode) {
          spec.loop = true;
          spec.count = 16;
        } else {
          if (count.empty() || count.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error(
                "Use a number from 1 to 200, or leave it empty in Simple mode for continuous work");
          spec.count = std::stoi(count);
          if (!advancedMode)
            spec.loop = false;
        }
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
      if (id == ATLAS_LAYOUT && atlasMode != "ov")
        moduleFilter.clear();
      atlasFilter = selected(filterChoice);
      cachedLayout.clear();
      InvalidateRect(window, nullptr, FALSE);
      return;
    }
    if ((id == ATLAS_COLOR || id == ATLAS_AUTHOR || id == ATLAS_DRAFTS) &&
        (notification == CBN_SELCHANGE || notification == BN_CLICKED)) {
      atlasColorBy = selected(colorChoice);
      authorFilter = choice(authorChoice) <= 0 ? std::string() : selected(authorChoice);
      atlasDrafts = SendMessageW(draftsChoice, BM_GETCHECK, 0, 0) == BST_CHECKED;
      auto path = data / "console-ui.json";
      auto prefs = fs::exists(path) ? Json::parse(read(path), nullptr, false) : Json::object();
      if (!prefs.is_object())
        prefs = Json::object();
      prefs["atlasColorBy"] = atlasColorBy;
      prefs["atlasDrafts"] = atlasDrafts;
      write(path, prefs.dump(2));
      InvalidateRect(window, nullptr, FALSE);
      return;
    }
    if (id == ATLAS_FUNCTION_SORT && notification == CBN_SELCHANGE) {
      auto at = choice(functionSort);
      if (at >= 0 && (size_t)at < functionSortKeys.size())
        functionSortKey = functionSortKeys[at];
      auto path = data / "console-ui.json";
      auto prefs = fs::exists(path) ? Json::parse(read(path), nullptr, false) : Json::object();
      if (!prefs.is_object())
        prefs = Json::object();
      prefs["functionSort"] = functionSortKey;
      write(path, prefs.dump(2));
      cachedLayout.clear();
      InvalidateRect(window, nullptr, FALSE);
      return;
    }
    if (id == ATLAS_CONTRIBUTORS && notification == LBN_SELCHANGE) {
      auto row = SendMessageW(contributorList, LB_GETCURSEL, 0, 0);
      authorFilter = row <= 0 || size_t(row) > contributorRank.size()
                         ? std::string()
                         : contributorRank[size_t(row) - 1].first;
      InvalidateRect(window, nullptr, FALSE);
      return;
    }
    if (id == ATLAS_FUNCTION_LIST &&
        (notification == LBN_SELCHANGE || notification == LBN_DBLCLK)) {
      auto row = SendMessageW(functionList, LB_GETCURSEL, 0, 0);
      if (row >= 0 && (size_t)row < functionRows.size()) {
        pickedFunction = functionRows[(size_t)row];
        if (notification == LBN_DBLCLK)
          inspectFunction();
        else
          flyFunction(pickedFunction);
        InvalidateRect(window, nullptr, FALSE);
      }
      return;
    }
    if (id == BATCH_LIST && notification == LBN_SELCHANGE) {
      selectBatch();
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
    if (id == CONNECTION_LIST && notification == LBN_SELCHANGE) {
      selectConnection();
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
    case REQUIREMENTS:
      if (serviceBusy || serviceReady)
        throw std::runtime_error("Wait for the current operation");
      serviceResult = Json::object();
      navigate(Screen::requirements);
      serviceCall("preflight", Json::object());
      break;
    case REQ_REFRESH:
      serviceCall("preflight", Json::object());
      break;
    case REQ_COPY:
      copyText(window, preflightSummary(serviceResult));
      break;
    case REQ_TERMINAL:
      ShellExecuteW(window, L"open", L"powershell.exe", L"-NoExit", repository.c_str(),
                    SW_SHOWNORMAL);
      break;
    case REQ_GITHUB:
      ShellExecuteW(window, L"open", L"powershell.exe", L"-NoExit -Command \"gh auth login\"",
                    repository.c_str(), SW_SHOWNORMAL);
      break;
    case QUEUE:
      navigate(Screen::queue);
      break;
    case QUEUE_TOP:
    case QUEUE_BOTTOM:
    case QUEUE_UP:
    case QUEUE_DOWN:
    case QUEUE_REMOVE: {
      if (!fleet)
        break;
      auto index = (int)SendMessageW(toolList, LB_GETCURSEL, 0, 0);
      if (index < 0)
        throw std::runtime_error("Select a queued target");
      if (id == QUEUE_TOP || id == QUEUE_BOTTOM) {
        auto a = activeAgent();
        if (!a)
          throw std::runtime_error("Select an agent");
        int target = id == QUEUE_TOP ? 0 : int(a->queue.size()) - 1;
        while (index != target) {
          int direction = index < target ? 1 : -1;
          fleet->editQueue(selectedId, index, direction);
          index += direction;
        }
      } else
        fleet->editQueue(selectedId, index, id == QUEUE_UP ? -1 : 1, id == QUEUE_REMOVE);
      build();
      break;
    }
    case TOOL_ENABLE: {
      if (toolId.empty())
        break;
      Backend backend(repository, data, settings, vault.values());
      auto prefs = backend.invoke("preferences.get");
      if (!prefs.value("allowNearMiss", true) && toolId.rfind("nearmiss_", 0) == 0)
        throw std::runtime_error("Allow near-miss tips in Settings before enabling this tool");
      auto disabled = prefs.value("disabledTools", Json::array());
      Json next = Json::array();
      bool currently = enabledTool(prefs, toolId);
      for (auto &value : disabled)
        if (value != toolId)
          next.push_back(value);
      if (currently)
        next.push_back(toolId);
      Json args = {{"disabledTools", next}};
      auto preview = backend.invoke("preferences.set", args);
      if (MessageBoxW(window,
                      wide(std::string(currently ? "Disable " : "Enable ") + toolId +
                           " for tools and agents?")
                          .c_str(),
                      L"Tool policy", MB_YESNO | MB_ICONQUESTION) != IDYES)
        break;
      args["confirmation"] = preview.at("confirmation");
      policy = backend.invoke("preferences.set", args);
      selectTool();
      break;
    }
    case CONNECTIONS:
      navigate(Screen::connections);
      break;
    case SERVICES:
      if (serviceBusy || serviceReady)
        throw std::runtime_error("Wait for the current operation");
      serviceMethod = "preflight";
      serviceRequest = Json::object();
      serviceResult = Json::object();
      navigate(Screen::services);
      serviceCall(serviceMethod, serviceRequest);
      break;
    case CONNECTION_SAVE: {
      if (serviceBusy || serviceReady)
        throw std::runtime_error("Wait for the current operation");
      auto name = text(profileFields["Name"]);
      if (name.empty())
        throw std::runtime_error("Connection name is required");
      auto c = connectionProfiles.value(name, Json::object());
      c["url"] = text(profileFields["URL"]);
      c["method"] = selected(profileFields["Method"]);
      c["keyEnv"] = text(profileFields["Key variable"]);
      c["keyHeader"] = text(profileFields["Key header"]);
      c["keyPrefix"] = text(profileFields["Key prefix"]);
      c["enabled"] = SendMessageW(profileFields["Enabled"], BM_GETCHECK, 0, 0) == BST_CHECKED;
      c["automatic"] = SendMessageW(profileFields["Automatic"], BM_GETCHECK, 0, 0) == BST_CHECKED;
      c["allowDescriptorDownloads"] =
          SendMessageW(profileFields["Descriptors"], BM_GETCHECK, 0, 0) == BST_CHECKED;
      c["bodyTemplate"] = Json::parse(text(profileFields["Template"]));
      serviceMethod = "connections.set";
      serviceRequest = connectionProfiles;
      serviceRequest[name] = c;
      serviceResult = Json::object();
      navigate(Screen::services);
      serviceCall(serviceMethod, serviceRequest);
      break;
    }
    case SERVICE_CANCEL:
      serviceRunner.cancel();
      break;
    case SERVICE_RUN:
      serviceMethod = selected(serviceChoice);
      serviceRequest = Json::parse(text(serviceArguments));
      serviceCall(serviceMethod, serviceRequest);
      break;
    case SERVICE_CONFIRM: {
      if (!serviceResult.value("requiresConfirmation", false))
        throw std::runtime_error("Inspect a preview first");
      if (selected(serviceChoice) != serviceMethod ||
          Json::parse(text(serviceArguments)) != serviceRequest)
        throw std::runtime_error("Arguments changed: inspect a fresh preview");
      auto args = serviceRequest;
      args["confirmation"] = serviceResult.at("confirmation");
      serviceCall(serviceMethod, args);
      break;
    }
    case REMOTE_FOLDER:
      PostMessageW(parent, CONSOLE_PICK_REPO, 0, 0);
      break;
    case REMOTE_CLONE:
      cloneUrl =
          descriptor.document.value("project", Json::object()).value("github", std::string());
      serviceResult = Json::object();
      navigate(Screen::clone);
      break;
    case CLONE_DEST: {
      IFileDialog *dialog = nullptr;
      if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog))))
        throw std::runtime_error("Cannot open folder picker");
      DWORD flags;
      dialog->GetOptions(&flags);
      dialog->SetOptions(flags | FOS_PICKFOLDERS);
      dialog->SetTitle(L"Choose the parent folder for the new clone");
      if (SUCCEEDED(dialog->Show(window))) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
          PWSTR path = nullptr;
          if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            auto name = descriptor.document.at("project").at("name").get<std::string>();
            for (auto &c : name)
              if (!std::isalnum((unsigned char)c) && c != '-' && c != '_')
                c = '_';
            cloneDestination = utf8((fs::path(path) / (name.empty() ? "project" : name)).wstring());
            CoTaskMemFree(path);
            setText(profileFields["Clone destination"], cloneDestination);
          }
          item->Release();
        }
      }
      dialog->Release();
      break;
    }
    case CLONE_PREVIEW:
      cloneUrl = text(profileFields["Clone URL"]);
      cloneDestination = text(profileFields["Clone destination"]);
      if (cloneDestination.empty())
        throw std::runtime_error("Choose a new destination folder first");
      cloneArguments = {{"url", cloneUrl}, {"destination", cloneDestination}};
      serviceCall("git.clone", cloneArguments);
      break;
    case CLONE_CONFIRM: {
      if (!serviceResult.value("requiresConfirmation", false))
        throw std::runtime_error("Preview this clone first");
      if (text(profileFields["Clone URL"]) != cloneUrl ||
          text(profileFields["Clone destination"]) != cloneDestination)
        throw std::runtime_error("Clone fields changed; preview again");
      auto args = cloneArguments;
      args["confirmation"] = serviceResult.at("confirmation");
      serviceCall("git.clone", args);
      EnableWindow(GetDlgItem(window, CLONE_CONFIRM), FALSE);
      break;
    }
    case CLONE_CANCEL:
      serviceRunner.cancel();
      break;
    case SUPPORT:
      serviceResult = Json::object();
      navigate(Screen::support);
      break;
    case SUPPORT_CHECK:
      serviceCall("update.check", Json::object());
      break;
    case SUPPORT_REPORT:
      supportDescription = text(profileFields["Report description"]);
      serviceRequest = {{"description", supportDescription}};
      serviceCall("bug.report", serviceRequest);
      break;
    case SUPPORT_CONFIRM: {
      if (!serviceResult.value("requiresConfirmation", false) ||
          activeServiceMethod != "bug.report")
        throw std::runtime_error("Preview the report first");
      auto args = serviceRequest;
      args["confirmation"] = serviceResult.at("confirmation");
      serviceCall("bug.report", args);
      break;
    }
    case SUPPORT_COPY:
      copyText(window, serviceResult.value("markdown", serviceResult.dump(2)));
      break;
    case SUPPORT_RELEASE: {
      auto update = serviceResult.value("update", Json::object());
      auto url = update.value("releaseUrl", std::string());
      if (url.empty())
        throw std::runtime_error("Check your configured update endpoint first");
      ShellExecuteW(window, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      break;
    }
    case SUPPORT_FOLDER:
      fs::create_directories(data / "exports");
      ShellExecuteW(window, L"open", (data / "exports").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case DETAIL_LOOP: {
      auto a = activeAgent();
      if (!a)
        throw std::runtime_error("Select an agent");
      auto spec = a->spec;
      spec.loop = !spec.loop;
      fleet->configure(a->id, spec);
      build();
      break;
    }
    case MCP_EXPORT: {
      if (!mcp)
        throw std::runtime_error("Start MCP first");
      auto a = activeAgent();
      if (!a || a->spec.kind != "mcp")
        throw std::runtime_error("Select an MCP agent in Controller first");
      auto connection = data / "mcp-client.json";
      write(connection, mcp->configuration());
      auto client = selected(profileFields["MCP client"]);
      auto config =
          mcpClientConfiguration(client, fs::u8path(selfExecutable()), connection, a->spec.name);
      auto path = data / "client-setup" / (client + ".json");
      fs::create_directories(path.parent_path());
      write(path, config.dump(2));
      ShellExecuteW(window, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
      break;
    }
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
    case DESC_SCAN:
      descriptorOperation = "descriptor.preview";
      serviceCall(descriptorOperation, Json::object());
      break;
    case DESC_PREVIEW:
      descriptorDraft = Json::parse(text(argsBox));
      parseDescriptor(descriptorDraft.dump());
      descriptorWriteArgs = {{"descriptor", descriptorDraft}};
      descriptorOperation = "descriptor.write";
      serviceCall(descriptorOperation, descriptorWriteArgs);
      break;
    case DESC_CONFIRM: {
      if (serviceBusy || !serviceResult.value("requiresConfirmation", false))
        throw std::runtime_error("Preview the descriptor write first");
      if (Json::parse(text(argsBox)) != descriptorWriteArgs.at("descriptor"))
        throw std::runtime_error("Draft changed; preview it again");
      if (MessageBoxW(window,
                      wide("Write this reviewed tangos.json?\n\n" +
                           descriptorWriteArgs.at("descriptor").dump(2))
                          .c_str(),
                      L"Review descriptor", MB_YESNO | MB_ICONQUESTION) != IDYES)
        break;
      auto args = descriptorWriteArgs;
      args["confirmation"] = serviceResult.at("confirmation");
      serviceCall("descriptor.write", args);
      break;
    }
    case DESC_RELOAD:
      PostMessageW(parent, CONSOLE_RELOAD, 0, 0);
      break;
    case DESC_FOLDER:
      PostMessageW(parent, CONSOLE_PICK_REPO, 0, 0);
      break;
    case GUIDE:
      tipsMode = false;
      tourStep = 0;
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
      if (remoteOnly)
        throw std::runtime_error("Choose a local checkout before adding agents");
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
      Json nextPolicy = Json::object();
      for (auto field : {"animateBackground", "allowNearMiss", "allowGhidra", "safeMode", "reports",
                         "useAgents", "autoLand", "liveRefresh"})
        nextPolicy[field] = SendMessageW(profileFields.at(std::string("policy:") + field),
                                         BM_GETCHECK, 0, 0) == BST_CHECKED;
      nextPolicy["agentFanout"] = std::stoi(text(profileFields.at("policy:agentFanout")));
      Backend backend(repository, data, settings, vault.values());
      auto policyPreview = backend.invoke("preferences.set", nextPolicy);
      if (MessageBoxW(window, wide("Save these policies?\n\n" + nextPolicy.dump(2)).c_str(),
                      L"Review settings", MB_YESNO | MB_ICONQUESTION) != IDYES)
        break;
      nextPolicy["confirmation"] = policyPreview.at("confirmation");
      policy = backend.invoke("preferences.set", nextPolicy);
      skin::animate(policy.value("animateBackground", true));
      advancedMode = SendMessageW(advanced, BM_GETCHECK, 0, 0) == BST_CHECKED;
      allowWrites = SendMessageW(writes, BM_GETCHECK, 0, 0) == BST_CHECKED;
      bool next = SendMessageW(portOnly, BM_GETCHECK, 0, 0) == BST_CHECKED;
      settings.portOnly = next;
      if (fleet)
        fleet->setPolicy(settings);
      if (savePreferences)
        savePreferences(settings);
      auto uiPath = data / "console-ui.json";
      auto uiPrefs = fs::exists(uiPath) ? Json::parse(read(uiPath)) : Json::object();
      uiPrefs["advanced"] = advancedMode;
      uiPrefs["writes"] = allowWrites;
      write(uiPath, uiPrefs.dump(2));
      navigate(Screen::controller);
      break;
    }
    case BATCHES:
      navigate(Screen::batches);
      break;
    case BATCH_LOG: {
      auto batch = std::find_if(batchRows.begin(), batchRows.end(),
                                [&](const Json &b) { return b.at("id") == selectedBatch; });
      if (batch == batchRows.end())
        throw std::runtime_error("Select a batch");
      auto owner = batch->at("agentId").get<std::string>();
      auto states = fleet->snapshot();
      auto agent = std::find_if(states.begin(), states.end(),
                                [&](const AgentState &state) { return state.id == owner; });
      if (agent == states.end() || agent->log.empty())
        throw std::runtime_error("No run log yet; start its assigned agent first");
      ShellExecuteW(window, L"open", agent->log.parent_path().c_str(), nullptr, nullptr,
                    SW_SHOWNORMAL);
      break;
    }
    case BATCH_SAVE_DRAFT:
      storeBatchDraft();
      break;
    case BATCH_CART_DRAFT:
      storeBatchDraft();
      if (cart.empty())
        throw std::runtime_error("Choose targets in Viewer first");
      draftRows = Json::array();
      for (auto index : cart)
        draftRows.push_back(atlas.at(index).row);
      storeBatchDraft();
      build();
      break;
    case BATCH_ENQUEUE:
      storeBatchDraft();
      if (choice(agentChoice) < 0 || size_t(choice(agentChoice)) > agents.size())
        throw std::runtime_error("Select a destination");
      fleet->enqueueDraft(choice(agentChoice) == 0 ? std::string()
                                                   : agents.at(size_t(choice(agentChoice) - 1)).id);
      build();
      break;
    case BATCH_HANDOFF:
      if (choice(agentChoice) < 0 || size_t(choice(agentChoice)) > agents.size())
        throw std::runtime_error("Select a destination");
      fleet->handoff(selectedBatch, choice(agentChoice) == 0
                                        ? std::string()
                                        : agents.at(size_t(choice(agentChoice) - 1)).id);
      build();
      break;
    case BATCH_GENERATE: {
      storeBatchDraft();
      if (manualBusy || draftReady)
        throw std::runtime_error("Wait for current tool or generation");
      if (manualWorker.joinable())
        manualWorker.join();
      auto role = selected(profileFields.at("Draft role"));
      auto count = std::stoi(text(profileFields.at("Draft count")));
      manualRunner.reset();
      manualBusy = true;
      draftError.clear();
      manualWorker = std::thread([this, role, count] {
        try {
          fleet->generateDraft(role, count, manualRunner, [this](const std::string &line) {
            std::lock_guard<std::mutex> lock(outputMutex);
            pending += line;
          });
        } catch (const std::exception &e) {
          draftError = e.what();
        }
        draftReady = true;
        manualBusy = false;
      });
      setText(body,
              "Generating a draft; complete log preserved. Cancel leaves your saved draft intact.");
      break;
    }
    case BATCH_CANCEL_GEN:
      manualRunner.cancel();
      break;
    case BATCH_IMPORT: {
      IFileDialog *dialog = nullptr;
      if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog))))
        throw std::runtime_error("Cannot open draft picker");
      fs::path path;
      if (SUCCEEDED(dialog->Show(window))) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
          PWSTR value = nullptr;
          if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) {
            path = value;
            CoTaskMemFree(value);
          }
          item->Release();
        }
      }
      dialog->Release();
      if (!path.empty()) {
        if (fs::file_size(path) > 1024 * 1024)
          throw std::runtime_error("Draft exceeds 1 MiB");
        fleet->saveDraft(Json::parse(read(path)));
        build();
      }
      break;
    }
    case BATCH_EXPORT: {
      storeBatchDraft();
      auto path = data / "exports" / (uniqueId() + "-draft.json");
      fs::create_directories(path.parent_path());
      write(path, fleet->draft().dump(2));
      ShellExecuteW(window, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
      break;
    }
    case BATCH_UP:
    case BATCH_DOWN:
    case BATCH_REMOVE:
      storeBatchDraft();
      if (selectedBatch.empty())
        throw std::runtime_error("Select a batch");
      fleet->editBatch(selectedBatch, id == BATCH_UP ? -1 : 1, id == BATCH_REMOVE);
      build();
      break;
    case BATCH_CLEAR_DONE:
      storeBatchDraft();
      fleet->clearDoneBatches();
      build();
      break;
    case OPEN_MCP:
      navigate(Screen::mcpConnection);
      break;
    case MCP_TOGGLE:
      toggleMcp();
      break;
    case MCP_CONFIG:
    case MCP_COPY_CONFIG:
      if (!mcp || mcpBusy)
        throw std::runtime_error("Start the MCP server first");
      if (id == MCP_COPY_CONFIG)
        copyText(window, mcp->configuration());
      else {
        auto path = data / "mcp-client.json";
        write(path, mcp->configuration());
        ShellExecuteW(window, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
      }
      break;
    case MCP_PROMPT:
      copyText(window, mcpPrompt());
      break;
    case OPEN_LOG: {
      auto a = activeAgent();
      auto path = (screen == Screen::detail || screen == Screen::controller) && a && !a->log.empty()
                      ? a->log.parent_path()
                      : data / "logs";
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
      if (MessageBoxW(window,
                      L"Run the repository's result-landing tool in this agent's isolated "
                      L"worktree? It may change src/ and requires port-only mode to be disabled. "
                      L"Review the resulting diff before committing.",
                      L"Land driver results", MB_YESNO | MB_ICONQUESTION) == IDYES)
        fleet->land(selectedId);
      break;
    case COMMIT_AGENT:
      reviewAgent(true);
      break;
    case ATLAS_CART:
      if (remoteOnly)
        throw std::runtime_error("Viewer-only projects cannot assign work");
      if (pickedFunction >= atlas.size())
        throw std::runtime_error("Select a function first");
      if (atlas[pickedFunction].state == "matched" || exemptTarget(atlas[pickedFunction].row) ||
          claimedTarget(atlas[pickedFunction].row))
        throw std::runtime_error(
            "This function is matched, exempt or claimed; choose available work");
      if (draftAdded && pickedFunction < atlas.size())
        draftAdded(atlas[pickedFunction].row);
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
    case ATLAS_INSPECT:
      inspectFunction();
      break;
    case ATLAS_SOURCE:
      historyTab = false;
      setText(body, inspectText);
      break;
    case ATLAS_HISTORY:
      historyTab = true;
      setText(body, historyText);
      break;
    case ATLAS_MODULE:
      if (pickedFunction >= atlas.size())
        throw std::runtime_error("Select a function in the module first");
      openModule(atlas[pickedFunction].module);
      break;
    case ATLAS_RESET:
      zoom = 1;
      panX = panY = 0;
      InvalidateRect(window, nullptr, FALSE);
      break;
    case ATLAS_LIVE:
      if (remoteOnly) {
        forceLiveReload = true;
        liveAtlas = true;
        loadAtlas();
        break;
      }
      if (!atlasReady)
        throw std::runtime_error("Wait for the current atlas load");
      liveAtlas = !liveAtlas;
      loadAtlas();
      build();
      break;
    case ATLAS_LOAD:
      forceLiveReload = true;
      loadAtlas();
      break;
    case TOUR_NEXT:
      tourStep = std::min(std::max(0, (int)guideSteps.size() - 1), tourStep + 1);
      build();
      break;
    case TOUR_PREVIOUS:
      tourStep = std::max(0, tourStep - 1);
      build();
      break;
    case HELP_EDIT:
      ShellExecuteW(window, L"open",
                    (data / (tipsMode ? "tango-tips.txt" : "tango-tour.txt")).c_str(), nullptr,
                    nullptr, SW_SHOWNORMAL);
      break;
    case HELP_TIPS:
      tipsMode = !tipsMode;
      tourStep = 0;
      build();
      break;
    case TOUR_CLOSE: {
      auto path = data / "console-ui.json";
      auto prefs = fs::exists(path) ? Json::parse(read(path), nullptr, false) : Json::object();
      if (!prefs.is_object())
        prefs = Json::object();
      prefs["tourSeen"] = true;
      write(path, prefs.dump(2));
      navigate(Screen::controller);
      break;
    }
    }
  }
  void tick() {
    if (screen == Screen::batches && fleet && batchList && GetTickCount64() - batchPoll >= 1000) {
      batchPoll = GetTickCount64();
      auto fresh = fleet->batches();
      auto encoded = fresh.dump();
      if (encoded != batchShown) {
        batchShown = encoded;
        batchRows = fresh;
        SendMessageW(batchList, LB_RESETCONTENT, 0, 0);
        for (auto &batch : batchRows) {
          auto label = wide(batch.at("status").get<std::string>() + " · " +
                            batch.at("title").get<std::string>());
          auto index = SendMessageW(batchList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
          if (batch.at("id") == selectedBatch)
            SendMessageW(batchList, LB_SETCURSEL, index, 0);
        }
        selectBatch();
      }
    }
    if (mcpReady.exchange(false)) {
      if (mcpWorker.joinable())
        mcpWorker.join();
      {
        std::lock_guard<std::mutex> lock(outputMutex);
        mcp = std::move(pendingMcp);
        mcpError = pendingMcpError;
      }
      mcpBusy = false;
      mcpDesired = bool(mcp);
      auto path = data / "console-ui.json";
      auto prefs = fs::exists(path) ? Json::parse(read(path), nullptr, false) : Json::object();
      if (!prefs.is_object())
        prefs = Json::object();
      prefs["mcpDesired"] = mcpDesired;
      write(path, prefs.dump(2));
      if (screen == Screen::mcpConnection)
        build();
    }
    if (screen == Screen::mcpConnection && body) {
      auto summary = mcpSummary();
      if (text(body) != summary)
        setText(body, summary);
    }

    if (statsReady.exchange(false)) {
      std::lock_guard<std::mutex> lock(outputMutex);
      if (!pendingStats.contains("error")) {
        agentStats = std::move(pendingStats);
      }
      InvalidateRect(window, nullptr, FALSE);
    }
    if ((screen == Screen::controller || screen == Screen::detail) && !statsBusy && !statsReady &&
        GetTickCount64() - statsPoll >= 1000) {
      statsPoll = GetTickCount64();
      if (statsWorker.joinable())
        statsWorker.join();
      statsBusy = true;
      auto prefs = settings;
      statsWorker = std::thread([this, prefs] {
        Json result;
        try {
          result = Backend(repository, data, prefs).invoke("stats.get");
        } catch (const std::exception &e) {
          result = {{"error", e.what()}};
        }
        {
          std::lock_guard<std::mutex> lock(outputMutex);
          pendingStats = std::move(result);
        }
        statsReady = true;
        statsBusy = false;
      });
    }
    if (flying) {
      double t = std::min(1., (GetTickCount64() - flightAt) / 350.);
      t = t * t * (3 - 2 * t);
      zoom = flightFrom.zoom + (flightTo.zoom - flightFrom.zoom) * t;
      panX = flightFrom.x + (flightTo.x - flightFrom.x) * t;
      panY = flightFrom.y + (flightTo.y - flightFrom.y) * t;
      if (t >= 1)
        flying = false;
      InvalidateRect(window, nullptr, FALSE);
    }
    if (atlasReady && atlasPublished.exchange(false)) {
      for (size_t i = 0; i < atlas.size(); ++i) {
        if (std::find(retainedCart.begin(), retainedCart.end(), atlas[i].id) != retainedCart.end())
          cart.push_back(i);
        if (atlas[i].id == retainedSelection)
          pickedFunction = i;
      }
      std::error_code ec;
      atlasModified = fs::last_write_time(confinedPath(repository, descriptor.database), ec);
      atlasPoll = GetTickCount64();
    }
    if (screen == Screen::atlas && atlasReady && IsWindowVisible(window) && !IsIconic(parent) &&
        GetTickCount64() - atlasPoll >= 1000) {
      atlasPoll = GetTickCount64();
      if (!liveAtlas) {
        std::error_code ec;
        auto changed = fs::last_write_time(confinedPath(repository, descriptor.database), ec);
        if (!ec && changed != atlasModified)
          loadAtlas();
      } else if (policy.value("liveRefresh", false) &&
                 GetTickCount64() / 30000 != (GetTickCount64() - 1000) / 30000)
        loadAtlas();
    }
    if (serviceReady.exchange(false)) {
      {
        std::lock_guard<std::mutex> lock(outputMutex);
        serviceResult = std::move(pendingServiceResult);
      }
      if (screen == Screen::descriptorGate) {
        if (descriptorOperation == "descriptor.preview" && !serviceResult.contains("error")) {
          descriptorDraft = serviceResult;
          setText(argsBox, descriptorDraft.dump(2));
        }
        setText(body, serviceResult.dump(2));
        EnableWindow(GetDlgItem(window, DESC_CONFIRM),
                     serviceResult.value("requiresConfirmation", false) &&
                         descriptorOperation == "descriptor.write");
        if (descriptorOperation == "descriptor.write" && serviceResult.value("saved", false))
          PostMessageW(parent, CONSOLE_RELOAD, 0, 0);
      }
      if (screen == Screen::support) {
        setText(body, serviceResult.dump(2));
        EnableWindow(GetDlgItem(window, SUPPORT_CONFIRM),
                     serviceResult.value("requiresConfirmation", false));
      }
      if (screen == Screen::clone) {
        setText(body, serviceResult.dump(2));
        EnableWindow(GetDlgItem(window, CLONE_CONFIRM),
                     serviceResult.value("requiresConfirmation", false));
        if (activeServiceMethod == "git.clone" && serviceResult.contains("exit") &&
            serviceResult.at("exit") == 0 && !serviceResult.value("cancelled", false)) {
          if (serviceWorker.joinable())
            serviceWorker.join();
          PostMessageW(
              parent, CONSOLE_OPEN_REPO, 0,
              reinterpret_cast<LPARAM>(new std::string(
                  Json({{"path", serviceResult.at("repository")},
                        {"projectId", remoteOnly ? settings.activeProject : std::string()}})
                      .dump())));
        }
      }
      if (screen == Screen::requirements)
        setText(body, preflightSummary(serviceResult));
      if (screen == Screen::services) {
        std::lock_guard<std::mutex> lock(outputMutex);
        setText(body, serviceResult.dump(2));
        EnableWindow(GetDlgItem(window, SERVICE_CONFIRM),
                     serviceResult.value("requiresConfirmation", false));
      }
    }
    if (inspectReady.exchange(false)) {
      Json r;
      {
        std::lock_guard<std::mutex> lock(outputMutex);
        r = std::move(inspectResult);
      }
      if (inspectGeneration == atlasGeneration && inspectIndex < atlas.size()) {
        auto code =
            r.contains("error")
                ? r["error"].get<std::string>()
                : numberedSource(r.value("source", Json::object()).value("source", std::string()));
        if (r.value("source", Json::object()).value("truncated", false))
          code += "\n\n[Showing the first 400 lines, matching Console. Open the source file for "
                  "the remainder.]";
        sourceCache[inspectIndex] = code;
        if (inspectIndex == pickedFunction) {
          inspectText = code;
          auto history = r.value("history", Json::object());
          historyText = "Prior tries\n\n";
          for (auto &attempt : history.value("attempts", Json::array())) {
            historyText += std::string(std::min(100, attempt.value("depth", 0)) * 2, ' ') +
                           attempt.value("attemptId", std::string("unknown")) + " · " +
                           attempt.value("status", std::string("unknown"));
            if (attempt.contains("divergences") && !attempt["divergences"].is_null())
              historyText += " · divergence " + attempt["divergences"].dump();
            if (attempt.contains("model") && attempt["model"].is_string())
              historyText += " · " + attempt["model"].get<std::string>();
            historyText += "\n";
            if (attempt.contains("note") && attempt["note"].is_string())
              historyText += "  " + attempt["note"].get<std::string>() + "\n";
          }
          if (history.contains("tip") && !history["tip"].is_null())
            historyText += "\nBest near-miss tip\n" + history["tip"].dump(2);
          if (history.contains("note") && history["note"].is_string())
            historyText += "\n" + history["note"].get<std::string>();
          if (screen == Screen::functionDetail)
            setText(body, historyTab ? historyText : inspectText);
        }
      }
    }
    if (pendingInspection != SIZE_MAX && !inspectBusy && !inspectReady) {
      auto next = pendingInspection;
      pendingInspection = SIZE_MAX;
      requestInspection(next);
    }
    if (screen == Screen::functionDetail && inspectText == "Loading source..." && !inspectBusy &&
        !inspectReady) {
      if (sourceCache.count(pickedFunction)) {
        inspectText = sourceCache[pickedFunction];
        setText(body, inspectText);
      } else
        requestInspection(pickedFunction);
    }
    if (screen == Screen::atlas && atlasReady && lod.update(zoom) == 3 && !inspectBusy &&
        !inspectReady)
      for (auto hit : hits)
        if ((size_t)hit.index == pickedFunction && hit.rect.right - hit.rect.left > 180 &&
            hit.rect.bottom - hit.rect.top > 90 && !sourceCache.count(hit.index)) {
          requestInspection(hit.index);
          break;
        }
    if (draftReady.exchange(false)) {
      if (manualWorker.joinable())
        manualWorker.join();
      if (screen == Screen::batches) {
        build();
        if (!draftError.empty())
          setText(body, draftError);
      }
    }
    refreshAgents();
    if (mcp)
      presence = mcp->state().value("clients", Json::array());
    else
      presence = Json::array();
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
    if (logBox && !out.empty() && screen != Screen::detail)
      appendText(logBox, out);
    if ((screen == Screen::clone || screen == Screen::batches) && body && !out.empty())
      appendText(body, out);
    if (screen == Screen::detail) {
      auto a = activeAgent();
      if (a) {
        setText(body,
                a->spec.name + " · " + a->phase + "\n" + a->detail +
                    "\nWorktree: " + utf8(a->worktree.wstring()) + "\nBranch: " + a->branch + "\n" +
                    std::to_string(a->completed) + " worked; " + std::to_string(a->queue.size()) +
                    " queued\nLog: " + utf8(a->log.wstring()) + "\n\n" + statisticsSummary(a->id));
        if (detailProgress) {
          SendMessageW(detailProgress, PBM_SETRANGE32, 0, std::max(1, a->total));
          SendMessageW(detailProgress, PBM_SETPOS, a->completed, 0);
        }
        if (!a->log.empty()) {
          auto tail = tailFile(a->log);
          if (text(logBox) != tail)
            setText(logBox, tail);
        }
      }
    }
    if ((screen == Screen::controller || screen == Screen::atlas || skin::animationEnabled()) &&
        IsWindowVisible(window) && !IsIconic(parent))
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
            std::wstring title(std::max(0, n) + 1, 0);
            if (n >= 0)
              SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, (LPARAM)title.data());
            auto oldFont = SelectObject(item->hDC, self->font);
            SetTextColor(item->hDC, skin::text());
            SetBkMode(item->hDC, TRANSPARENT);
            RECT bounds = item->rcItem;
            bounds.left += 8;
            bounds.right -= 4;
            DrawTextW(item->hDC, title.c_str(), -1, &bounds,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(item->hDC, oldFont);
          }
          return TRUE;
        }
        break;
      }
      case WM_CTLCOLORSTATIC: {
        wchar_t name[32]{};
        GetClassNameW((HWND)l, name, 32);
        if (std::wstring(name) == L"Static") {
          SetTextColor((HDC)w, skin::text());
          SetBkMode((HDC)w, TRANSPARENT);
          return (LRESULT)GetStockObject(NULL_BRUSH);
        }
      }
        [[fallthrough]];
      case WM_CTLCOLOREDIT:
      case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)w, skin::text());
        SetBkColor((HDC)w, skin::field());
        SetBkMode((HDC)w, OPAQUE);
        if (self->fieldBrush)
          DeleteObject(self->fieldBrush);
        self->fieldBrush = CreateSolidBrush(skin::field());
        return (LRESULT)self->fieldBrush;
      case WM_MOUSELEAVE:
        self->hoveredFunction = SIZE_MAX;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
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
          self->camera.zoomAt(GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1.25 : .8, p.x - 18, p.y - 148,
                              self->width - 390, self->height - 218);
          InvalidateRect(h, nullptr, FALSE);
          return 0;
        }
        self->scroll = std::max(0, self->scroll - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 80);
        self->build();
        return 0;
      case WM_KEYDOWN:
        if (self->screen == Screen::atlas) {
          self->viewerKey(w);
          return 0;
        }
        break;
      case WM_MBUTTONDOWN:
        if (self->screen == Screen::atlas) {
          self->panning = true;
          self->dragLast = {(short)LOWORD(l), (short)HIWORD(l)};
          SetCapture(h);
        }
        return 0;
      case WM_MBUTTONUP:
        self->panning = false;
        ReleaseCapture();
        return 0;
      case WM_RBUTTONDOWN: {
        if (self->screen != Screen::atlas)
          break;
        POINT p{(short)LOWORD(l), (short)HIWORD(l)};
        if (p.x >= 18 && p.x < self->width - 372 && p.y >= 100 && p.y < self->height - 70) {
          SetFocus(h);
          self->flying = false;
          self->marquee = true;
          self->additiveMarquee = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
          self->dragStart = self->dragEnd = p;
          SetCapture(h);
        }
        return 0;
      }
      case WM_RBUTTONUP:
        if (self->marquee)
          return SendMessageW(h, WM_LBUTTONUP, w, l);
        return 0;
      case WM_CONTEXTMENU:
        if (self->screen == Screen::atlas)
          return 0;
        break;
      case WM_LBUTTONDOWN: {
        if (self->screen != Screen::atlas)
          break;
        POINT p{(short)LOWORD(l), (short)HIWORD(l)};
        SetFocus(h);
        self->flying = false;
        if (PtInRect(&self->miniBounds, p)) {
          self->miniDragging = true;
          self->moveMini(p);
          SetCapture(h);
        } else if ((GetKeyState(VK_SHIFT) & 0x8000) && p.x >= 18 && p.x < self->width - 372 &&
                   p.y >= 100 && p.y < self->height - 70) {
          self->marquee = true;
          self->dragStart = self->dragEnd = p;
          self->additiveMarquee = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
          SetCapture(h);
        } else if (p.x >= 18 && p.x < self->width - 372 && p.y >= 100 && p.y < self->height - 70) {
          self->leftDown = true;
          self->dragged = false;
          self->dragStart = self->dragLast = p;
          SetCapture(h);
        }
        return 0;
      }
      case WM_MOUSEMOVE: {
        if (self->screen == Screen::atlas && !self->panning && !self->marquee) {
          POINT point{(short)LOWORD(l), (short)HIWORD(l)};
          size_t hover = SIZE_MAX;
          for (auto hit : self->hits)
            if (PtInRect(&hit.rect, point)) {
              hover = size_t(hit.index);
              break;
            }
          if (hover != self->hoveredFunction) {
            self->hoveredFunction = hover;
            InvalidateRect(h, nullptr, FALSE);
          }
          TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
          TrackMouseEvent(&track);
        }
        POINT p{(short)LOWORD(l), (short)HIWORD(l)};
        if (self->leftDown && !self->marquee && !self->miniDragging) {
          if (std::abs(p.x - self->dragStart.x) + std::abs(p.y - self->dragStart.y) > 4)
            self->dragged = true;
          if (self->dragged) {
            self->panX += p.x - self->dragLast.x;
            self->panY += p.y - self->dragLast.y;
            self->dragLast = p;
            self->camera.clamp(self->width - 390, self->height - 218);
          }
        }
        if (self->panning) {
          self->panX += p.x - self->dragLast.x;
          self->panY += p.y - self->dragLast.y;
          self->dragLast = p;
          self->camera.clamp(self->width - 390, self->height - 218);
        }
        if (self->miniDragging)
          self->moveMini(p);
        if (self->marquee)
          self->dragEnd = p;
        if (self->panning || self->marquee || self->miniDragging || self->leftDown)
          InvalidateRect(h, nullptr, FALSE);
        return 0;
      }
      case WM_CAPTURECHANGED:
        self->panning = self->marquee = self->miniDragging = self->leftDown = false;
        return 0;
      case WM_LBUTTONDBLCLK:
        if (self->screen == Screen::atlas && self->pickedFunction < self->atlas.size())
          self->inspectFunction();
        return 0;
      case WM_LBUTTONUP: {
        POINT point{(short)LOWORD(l), (short)HIWORD(l)};
        bool moved = self->leftDown && self->dragged;
        self->leftDown = false;
        if (moved) {
          ReleaseCapture();
          return 0;
        }
        if (self->miniDragging) {
          self->miniDragging = false;
          ReleaseCapture();
          return 0;
        }
        if (self->marquee) {
          if (self->remoteOnly) {
            self->marquee = false;
            ReleaseCapture();
            return 0;
          }
          auto selected =
              marqueeTiles(self->tiles, {(self->dragStart.x - 18 - self->panX) / self->zoom,
                                         (self->dragStart.y - 148 - self->panY) / self->zoom,
                                         (point.x - self->dragStart.x) / self->zoom,
                                         (point.y - self->dragStart.y) / self->zoom});
          if (!self->additiveMarquee)
            self->cart.clear();
          for (auto index : selected)
            if (self->atlas[index].state != "matched" && !exemptTarget(self->atlas[index].row) &&
                !claimedTarget(self->atlas[index].row) &&
                std::find(self->cart.begin(), self->cart.end(), index) == self->cart.end())
              self->cart.push_back(index);
          self->marquee = false;
          ReleaseCapture();
          InvalidateRect(h, nullptr, FALSE);
          return 0;
        }
        ReleaseCapture();
        for (auto hit : self->hits)
          if (PtInRect(&hit.rect, point)) {
            if (self->screen == Screen::controller) {
              self->selectedId = self->agents.at(hit.index).id;
              self->navigate(Screen::detail);
            } else if (self->screen == Screen::atlas) {
              auto &f = self->atlas.at(hit.index);
              self->pickedFunction = hit.index;
              self->flyFunction(hit.index);
              if (!self->remoteOnly && (GetKeyState(VK_CONTROL) & 0x8000) && f.state != "matched" &&
                  !exemptTarget(f.row) && !claimedTarget(f.row)) {
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
                     std::function<void()> git, std::function<void(const Settings &)> save,
                     bool viewerOnly, std::string module, std::function<void(Json)> draftAdded,
                     bool remoteOnly, Transport transport)
    : impl(std::make_unique<Impl>(parent, font, std::move(repo), std::move(data),
                                  std::move(settings), std::move(git), std::move(save), viewerOnly,
                                  std::move(module), std::move(draftAdded), remoteOnly,
                                  std::move(transport))) {}
ConsoleUI::~ConsoleUI() = default;
void ConsoleUI::show(bool visible, bool atlas) {
  if (visible) {
    auto screen = atlas ? Screen::atlas : Screen::controller;
    if (impl->screen == Screen::controller || impl->screen == Screen::atlas ||
        impl->screen == Screen::remoteGate)
      impl->navigate(screen);
  }
  ShowWindow(impl->window, visible ? SW_SHOW : SW_HIDE);
}
void ConsoleUI::resize(int width, int height) {
  if (width < 800 || height < 650 || (width == impl->width && height == impl->height))
    return;
  if (impl->screen == Screen::batches && impl->batchTitle)
    impl->storeBatchDraft();
  impl->width = width;
  impl->height = height;
  MoveWindow(impl->window, 14, 72, width, height, TRUE);
  impl->build();
}
bool ConsoleUI::running() const {
  return impl->manualBusy || impl->serviceBusy || impl->serviceReady ||
         (impl->fleet && impl->fleet->running());
}
void ConsoleUI::stop() {
  impl->manualRunner.cancel();
  impl->serviceRunner.cancel();
  if (impl->fleet)
    impl->fleet->stopAll();
}
void ConsoleUI::smokeRemote(const fs::path &directory,
                            const std::function<void(const fs::path &)> &capture) {
  if (impl->screen == Screen::descriptorGate)
    return;
  if (!fs::exists(impl->repository / ".tangos-lite-test-fixture"))
    throw std::runtime_error("Remote GUI smoke requires a disposable fixture");
  auto data = directory / "remote-viewer-test";
  Backend backend(impl->repository, data, impl->settings);
  auto confirmed = [&](const std::string &method, Json args) {
    auto preview = backend.invoke(method, args);
    args["confirmation"] = preview.at("confirmation");
    return backend.invoke(method, args);
  };
  confirmed("projects.register", {{"id", "remote-gui"}, {"descriptor", impl->descriptor.document}});
  auto opened = confirmed("projects.open", {{"id", "remote-gui"}});
  auto path = fs::u8path(opened.at("path").get<std::string>());
  confirmed(
      "connections.set",
      {{"atlas.live",
        {{"enabled", true}, {"method", "GET"}, {"url", "https://fixture.invalid/database"}}}});
  int requests = 0;
  bool offline = false;
  auto database = read(confinedPath(impl->repository, impl->descriptor.database));
  ShowWindow(impl->window, SW_HIDE);
  try {
    auto remoteSettings = impl->settings;
    remoteSettings.activeProject = "remote-gui";
    ConsoleUI remote(
        impl->parent, impl->font, path, data, remoteSettings, [] {}, {}, false, {}, {}, true,
        [&](auto &, auto &, auto &, auto &) {
          ++requests;
          if (offline)
            throw std::runtime_error("offline fixture");
          return HttpResponse{200, database};
        });
    remote.resize(impl->width, impl->height);
    remote.show(true);
    if (requests || remote.impl->fleet || remote.impl->mcp || fs::exists(path / ".git") ||
        remote.impl->screen != Screen::remoteGate)
      throw std::runtime_error(
          "Remote landing unexpectedly started local execution or a connection");
    capture(directory / "remote-project.bmp");
    remote.show(true, true);
    if (remote.impl->loader.joinable())
      remote.impl->loader.join();
    remote.impl->tick();
    if (!requests || !remote.impl->atlasError.empty() || remote.impl->atlas.empty())
      throw std::runtime_error("Remote published Viewer did not load: " + remote.impl->atlasError);
    auto assign = GetDlgItem(remote.impl->window, ATLAS_CART);
    if (assign && IsWindowEnabled(assign))
      throw std::runtime_error("Remote Viewer enabled assignment");
    remote.impl->pickedFunction = 0;
    remote.impl->viewerKey(VK_SPACE);
    if (!remote.impl->cart.empty())
      throw std::runtime_error("Remote keyboard shortcut assigned work");
    capture(directory / "remote-viewer.bmp");
    auto loadedRequests = requests;
    remote.impl->loadAtlas();
    remote.impl->loader.join();
    remote.impl->tick();
    if (requests != loadedRequests || remote.impl->cacheNotice.find("cache") == std::string::npos)
      throw std::runtime_error("Published Viewer did not reuse its fresh cache");
    offline = true;
    remote.impl->forceLiveReload = true;
    remote.impl->loadAtlas();
    remote.impl->loader.join();
    remote.impl->tick();
    if (remote.impl->atlas.empty() || remote.impl->cacheNotice.find("stale") == std::string::npos)
      throw std::runtime_error("Published Viewer did not preserve an offline cache");
    offline = false;
    remote.impl->cloneUrl = utf8(impl->repository.wstring());
    remote.impl->cloneDestination = utf8((directory / "cloned remote fixture").wstring());
    remote.impl->serviceResult = Json::object();
    remote.impl->navigate(Screen::clone);
    auto awaitClone = [&] {
      auto start = GetTickCount64();
      while (remote.impl->serviceBusy || remote.impl->serviceReady) {
        if (GetTickCount64() - start > 15000)
          throw std::runtime_error("Native clone workflow timed out");
        remote.impl->tick();
        Sleep(10);
      }
      if (remote.impl->serviceResult.contains("error"))
        throw std::runtime_error(remote.impl->serviceResult.dump());
    };
    remote.impl->action(CLONE_PREVIEW, BN_CLICKED);
    awaitClone();
    if (!remote.impl->serviceResult.value("requiresConfirmation", false) ||
        fs::exists(fs::u8path(remote.impl->cloneDestination)))
      throw std::runtime_error("Clone preview made a checkout before confirmation");
    capture(directory / "clone-preview.bmp");
    remote.impl->action(CLONE_CONFIRM, BN_CLICKED);
    awaitClone();
    if (remote.impl->serviceResult.value("exit", 1) != 0 ||
        !fs::exists(fs::u8path(remote.impl->cloneDestination) / ".git"))
      throw std::runtime_error("Confirmed native clone did not create a checkout");
    MSG open{};
    if (!PeekMessageW(&open, impl->parent, CONSOLE_OPEN_REPO, CONSOLE_OPEN_REPO, PM_REMOVE))
      throw std::runtime_error("Successful clone did not request automatic opening");
    std::unique_ptr<std::string> requested(reinterpret_cast<std::string *>(open.lParam));
    auto requestedProject = Json::parse(*requested);
    if (requestedProject.at("path") != remote.impl->cloneDestination ||
        requestedProject.at("projectId") != "remote-gui")
      throw std::runtime_error(
          "Clone opened the wrong destination or lost its remote project identity");
    capture(directory / "clone-complete.bmp");
    write(directory / "remote-viewer-gui-report.txt",
          "PASS: metadata-only project, no automatic network, published Viewer, no agents or Git "
          "checkout, read-only assignment controls.");
  } catch (...) {
    ShowWindow(impl->window, SW_SHOW);
    throw;
  }
  ShowWindow(impl->window, SW_SHOW);
}
void ConsoleUI::smokeScreens(const fs::path &directory,
                             const std::function<void(const fs::path &)> &capture) {
  if (impl->loader.joinable())
    impl->loader.join();
  if (!fs::exists(impl->repository / ".tangos-lite-test-fixture"))
    throw std::runtime_error("GUI fleet smoke requires an explicit disposable fixture");
  if (impl->screen == Screen::descriptorGate) {
    auto awaitService = [&] {
      auto start = GetTickCount64();
      while (impl->serviceBusy || impl->serviceReady) {
        if (GetTickCount64() - start > 15000)
          throw std::runtime_error("Descriptor gate service timed out");
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
        impl->tick();
        Sleep(10);
      }
      if (impl->serviceResult.contains("error"))
        throw std::runtime_error(impl->serviceResult.dump());
    };
    ShowWindow(impl->window, SW_SHOW);
    capture(directory / "descriptor-missing.bmp");
    impl->action(DESC_SCAN, BN_CLICKED);
    awaitService();
    parseDescriptor(text(impl->argsBox));
    capture(directory / "descriptor-generated.bmp");
    auto path = impl->repository / "tangos.json";
    auto before = read(path);
    auto existed = fs::exists(path);
    impl->action(DESC_PREVIEW, BN_CLICKED);
    awaitService();
    if (!impl->serviceResult.value("requiresConfirmation", false) || fs::exists(path) != existed ||
        read(path) != before)
      throw std::runtime_error("Descriptor preview wrote before confirmation");
    capture(directory / "descriptor-review.bmp");
    auto args = impl->descriptorWriteArgs;
    args["confirmation"] = impl->serviceResult.at("confirmation");
    auto result =
        Backend(impl->repository, impl->data, impl->settings).invoke("descriptor.write", args);
    if (!result.value("saved", false) || loadDescriptor(impl->repository).tools.empty())
      throw std::runtime_error("Confirmed descriptor did not load discovered checks");
    write(directory / "descriptor-gui-report.txt",
          "PASS: missing/invalid descriptor gate, async scan, editable draft, side-effect-free "
          "preview, fixture-only confirmed write and validated reload");
    return;
  }
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
    std::vector<std::string> extraAgents;
    for (int i = 0; i < 3; ++i) {
      agent.name = "Grid fixture " + std::to_string(i);
      extraAgents.push_back(impl->fleet->add(agent));
    }
    bool previousMode = impl->advancedMode;
    for (bool advanced : {false, true}) {
      impl->advancedMode = advanced;
      impl->navigate(Screen::controller);
      if (impl->agents.size() != 4)
        throw std::runtime_error("Four-agent grid fixture missing");
      for (size_t i = 0; i < impl->agents.size(); ++i) {
        auto button = GetDlgItem(impl->window, 5000 + int(i) * 16);
        RECT actual{};
        if (!button || !GetWindowRect(button, &actual))
          throw std::runtime_error("Grid card lost Go/Stop control");
        MapWindowPoints(nullptr, impl->window, (POINT *)&actual, 2);
        auto card = impl->agentCardBounds(i);
        if (actual.left < card.left || actual.right > card.right || actual.top < card.top ||
            actual.bottom > card.bottom)
          throw std::runtime_error("Grid control escapes card");
      }
      auto first = impl->agentCardBounds(0), third = impl->agentCardBounds(2),
           fourth = impl->agentCardBounds(3);
      if (third.top != first.top || third.left <= first.left || fourth.top <= first.top ||
          fourth.left != first.left)
        throw std::runtime_error("Controller must render exactly three cards per row");
      capture(directory / (advanced ? "controller-grid-advanced.bmp" : "controller-grid.bmp"));
    }
    impl->advancedMode = previousMode;
    for (auto &id : extraAgents)
      impl->fleet->remove(id);
    impl->navigate(Screen::detail);
    impl->tick();
    setText(impl->logBox, impl->fleet->review(impl->selectedId));
    write(directory / "fleet-gui-report.txt",
          "PASS: packaged CLI agent, isolated worktree, instructions, independent checks, live UI, "
          "retained log, full diff review. UI messages=" +
              std::to_string(pumps));
    capture(directory / "console-4.bmp");
  }
  for (auto screen :
       {Screen::controller, Screen::atlas, Screen::encyclopedia, Screen::settings, Screen::profile,
        Screen::tour, Screen::connections, Screen::services, Screen::queue, Screen::requirements}) {
    impl->navigate(screen);
    ShowWindow(impl->window, SW_SHOW);
    capture(directory / fs::u8path("console-" + std::to_string((int)screen) + ".bmp"));
  }
  impl->navigate(Screen::tour);
  if (impl->guideSteps.size() != 10)
    throw std::runtime_error("Reference tour steps missing");
  impl->action(TOUR_NEXT, BN_CLICKED);
  capture(directory / "tour-expression.bmp");
  impl->action(HELP_TIPS, BN_CLICKED);
  if (!impl->tipsMode || impl->guideSteps.empty())
    throw std::runtime_error("Native editable tips missing");
  capture(directory / "tips.bmp");
  impl->action(TOUR_CLOSE, BN_CLICKED);
  auto uiPrefs = Json::parse(read(impl->data / "console-ui.json"));
  if (!uiPrefs.value("tourSeen", false))
    throw std::runtime_error("Tour completion did not persist");
  if (impl->atlas.empty())
    throw std::runtime_error("GUI viewer fixture has no functions");
  impl->navigate(Screen::atlas);
  capture(directory / "atlas-contributors.bmp");
  if (SendMessageW(impl->functionList, LB_GETCOUNT, 0, 0) != (LRESULT)impl->atlas.size())
    throw std::runtime_error("Viewer function list is incomplete");
  SendMessageW(impl->functionSort, CB_SETCURSEL, 2, 0);
  impl->action(ATLAS_FUNCTION_SORT, CBN_SELCHANGE);
  capture(directory / "atlas-function-list.bmp");
  if (impl->functionRows.empty() || impl->atlas[impl->functionRows.front()].size != 60)
    throw std::runtime_error("Viewer function list size sort failed");
  auto listPrefs = Json::parse(read(impl->data / "console-ui.json"));
  if (listPrefs.value("functionSort", std::string()) != "size-asc")
    throw std::runtime_error("Viewer list sort did not persist");
  SendMessageW(impl->functionList, LB_SETCURSEL, 0, 0);
  impl->action(ATLAS_FUNCTION_LIST, LBN_SELCHANGE);
  if (impl->pickedFunction != impl->functionRows.front())
    throw std::runtime_error("Viewer list selection did not select its target");
  RECT authorBounds{}, draftBounds{}, overlap{};
  GetWindowRect(impl->authorChoice, &authorBounds);
  GetWindowRect(impl->draftsChoice, &draftBounds);
  if (IntersectRect(&overlap, &authorBounds, &draftBounds))
    throw std::runtime_error("Viewer contributor and draft controls overlap");
  SendMessageW(impl->functionSort, CB_SETCURSEL, 0, 0);
  impl->action(ATLAS_FUNCTION_SORT, CBN_SELCHANGE);

  SendMessageW(impl->colorChoice, CB_SETCURSEL, 1, 0);
  SendMessageW(impl->draftsChoice, BM_SETCHECK, BST_UNCHECKED, 0);
  impl->action(ATLAS_COLOR, CBN_SELCHANGE);
  if (impl->atlasColorBy != "author" || impl->atlasDrafts)
    throw std::runtime_error("Independent Viewer color/draft controls failed");
  auto viewerPrefs = Json::parse(read(impl->data / "console-ui.json"));
  if (viewerPrefs.value("atlasColorBy", std::string()) != "author" ||
      viewerPrefs.value("atlasDrafts", true))
    throw std::runtime_error("Viewer settings did not persist");
  if (SendMessageW(impl->authorChoice, CB_GETCOUNT, 0, 0) < 2)
    throw std::runtime_error("Contributor legend did not include fixture author");
  SendMessageW(impl->authorChoice, CB_SETCURSEL, 1, 0);
  impl->action(ATLAS_AUTHOR, CBN_SELCHANGE);
  if (impl->authorFilter != "FixtureContributor")
    throw std::runtime_error("Contributor selection failed");
  capture(directory / "atlas-contributor-filter.bmp");
  impl->pickedFunction = 0;
  impl->inspectFunction();
  auto waitFor = [&](const std::function<bool()> &pending) {
    auto started = GetTickCount64();
    while (pending()) {
      if (GetTickCount64() - started > 15000)
        throw std::runtime_error("GUI parity workflow timed out");
      MSG m;
      while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
      }
      impl->tick();
      Sleep(10);
    }
    impl->tick();
  };
  impl->navigate(Screen::mcpConnection);
  if (!impl->mcp || impl->mcpSummary().find("Status: running") == std::string::npos ||
      impl->mcpPrompt().find("AGENTS.md") == std::string::npos)
    throw std::runtime_error("MCP connection screen/prompt missing");
  capture(directory / "mcp-connection.bmp");
  impl->action(MCP_TOGGLE, BN_CLICKED);
  waitFor([&] { return impl->mcpBusy.load() || impl->mcpReady.load(); });
  if (impl->mcp || Json::parse(read(impl->data / "console-ui.json")).value("mcpDesired", true))
    throw std::runtime_error("MCP stop state did not persist");
  impl->action(MCP_TOGGLE, BN_CLICKED);
  waitFor([&] { return impl->mcpBusy.load() || impl->mcpReady.load(); });
  if (!impl->mcp)
    throw std::runtime_error("MCP restart failed");
  impl->navigate(Screen::batches);
  impl->draftRows =
      Json::array({{{"id", "draft-gui-target"}, {"name", "draft_gui_target"}, {"module", "port"}}});
  setText(impl->batchTitle, "GUI saved draft");
  setText(impl->batchPrompt, "Preserve repository rules and report verification failures.");
  impl->action(BATCH_SAVE_DRAFT, BN_CLICKED);
  if (impl->fleet->draft().at("title") != "GUI saved draft")
    throw std::runtime_error("Draft editor did not save");
  impl->action(BATCH_ENQUEUE, BN_CLICKED);
  auto history = impl->fleet->batches();
  if (history.empty() || history.back().at("title") != "GUI saved draft" ||
      !impl->fleet->draft().at("items").empty())
    throw std::runtime_error("Draft assignment did not enqueue batch");
  impl->selectedBatch = history.back().at("id").get<std::string>();
  capture(directory / "batch-history.bmp");
  SendMessageW(impl->agentChoice, CB_SETCURSEL, 0, 0);
  impl->action(BATCH_HANDOFF, BN_CLICKED);
  if (impl->fleet->batches().back().at("agentId") != "")
    throw std::runtime_error("Native global handoff failed");
  SendMessageW(impl->agentChoice, CB_SETCURSEL, 1, 0);
  impl->action(BATCH_HANDOFF, BN_CLICKED);
  if (impl->fleet->batches().back().at("agentId") == "")
    throw std::runtime_error("Native agent handoff failed");
  impl->action(BATCH_REMOVE, BN_CLICKED);
  impl->action(BATCH_CLEAR_DONE, BN_CLICKED);
  impl->navigate(Screen::functionDetail);
  waitFor([&] {
    return impl->inspectBusy.load() || impl->inspectReady.load() ||
           impl->pendingInspection != SIZE_MAX;
  });
  if (impl->inspectText.find("1  int fixture_source") == std::string::npos)
    throw std::runtime_error("Native source inspection did not load numbered source");
  capture(directory / "console-8.bmp");
  impl->openModule(impl->atlas[0].module);
  auto &popup = *impl->popups.back();
  if (popup.ui->impl->loader.joinable())
    popup.ui->impl->loader.join();
  popup.ui->impl->pickedFunction = 1;
  popup.ui->impl->action(ATLAS_CART, BN_CLICKED);
  if (impl->cart.empty() || popup.ui->impl->fleet)
    throw std::runtime_error("Module popout cart relay or controller isolation failed");
  SendMessageW(popup.window, WM_CLOSE, 0, 0);
  impl->navigate(Screen::services);
  impl->serviceRunner.reset();
  impl->action(SERVICE_CANCEL, BN_CLICKED);
  if (!impl->serviceRunner.isCancelled())
    throw std::runtime_error("Native service Cancel did not reach the process runner");
  impl->serviceCall("preflight", Json::object());
  waitFor([&] { return impl->serviceBusy.load() || impl->serviceReady.load(); });
  if (impl->serviceResult.contains("error"))
    throw std::runtime_error(impl->serviceResult.dump());
  capture(directory / "console-10.bmp");
  impl->navigate(Screen::support);
  setText(impl->profileFields["Report description"], "Packaged native fixture report");
  impl->action(SUPPORT_REPORT, BN_CLICKED);
  waitFor([&] { return impl->serviceBusy.load() || impl->serviceReady.load(); });
  if (!impl->serviceResult.value("requiresConfirmation", false))
    throw std::runtime_error("Native report preview missing");
  capture(directory / "support-report-preview.bmp");
  impl->action(SUPPORT_CONFIRM, BN_CLICKED);
  waitFor([&] { return impl->serviceBusy.load() || impl->serviceReady.load(); });
  if (!impl->serviceResult.contains("folder") ||
      !fs::exists(fs::u8path(impl->serviceResult.at("folder").get<std::string>()) /
                  "bug-report.md"))
    throw std::runtime_error("Native report export failed");
  capture(directory / "support-report-complete.bmp");
  write(directory / "viewer-gui-report.txt",
        "PASS: native source/history inspection and asynchronous preflight screens; "
        "viewport/LOD/marquee validated by unit tests.");
  impl->navigate(Screen::controller);
}
} // namespace lite
