#include "repository.h"
#include "skin.h"
#include "console_ui.h"
#include "backend.h"
#include "updater.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <shellapi.h>
#include <shlobj.h>
#include <stdexcept>
#include <thread>
#include <windows.h>
using namespace lite;
namespace {
constexpr UINT OUTPUT = WM_APP + 1, DONE = WM_APP + 2, REVIEW = WM_APP + 3, STATE = WM_APP + 4;
HWND window, repoEdit, statusEdit, logEdit, checkCombo, actionCombo, remoteEdit, refEdit,
    detailsEdit, runButton, actionButton, cancelButton, activityLabel;
HFONT uiFont, monoFont;
Runner runner;
std::unique_ptr<ConsoleUI> consoleUI;
Settings settings;
fs::path config, repo, logPath, dataDir, consoleRepository;
std::thread worker;
std::atomic<bool> busy{false};
std::mutex outputMutex;
std::string pending;
std::vector<Check> checks;
bool smoke = false;
int smokePhase = 0, smokeExit = 0, smokeTicks = 0;
enum {
  BROWSE = 101,
  SELECT,
  REFRESH,
  RUN,
  ACTION,
  CANCEL,
  LOGS,
  CONFIG,
  AGENTS,
  MINIMIZE,
  MAXIMIZE,
  CLOSE,
  THEME,
  CONTROLLER_TAB,
  REPOSITORY_TAB,
  TOOLBOX,
  PROJECT_MENU,
  SELECT_PROJECT,
  DISCOVER_PROJECTS,
  AUTO_UPDATE
};
bool workspaceReady = false, workspaceRemote = false;
bool discoveryPending = false;
bool updatePending = false, restartAfterUpdate = false;
fs::path pendingUpdateReceipt;
bool repositoryView = false;
bool toolboxOpen = false;
HWND themeCombo, minimizeButton, maximizeButton, closeButton, projectButton;
HWND controllerTab, repositoryTab;
HWND toolboxButton;
HBRUSH fieldBrush = nullptr;
const std::vector<std::string> actions = {"Fetch",           "Pull (fast-forward)",
                                          "Merge",           "Rebase",
                                          "Stage paths",     "Commit staged",
                                          "Push reviewed",   "Compare upstreams",
                                          "Upstream diff",   "Add remote",
                                          "PR readiness",    "PR checks",
                                          "Create draft PR", "Create branch",
                                          "Switch branch",   "Delete merged branch",
                                          "Rebase continue", "Rebase abort",
                                          "Merge continue",  "Merge abort",
                                          "List stashes",    "Stash selected paths",
                                          "Apply stash",     "Drop stash",
                                          "Create tag",      "Delete tag"};
std::string value(HWND h) {
  int n = GetWindowTextLengthW(h);
  std::wstring s(n + 1, 0);
  GetWindowTextW(h, s.data(), n + 1);
  s.resize(n);
  return utf8(s);
}
void set(HWND h, const std::string &s) {
  std::string cr;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r'))
      cr += '\r';
    cr += s[i];
  }
  auto t = wide(cr);
  SetWindowTextW(h, t.c_str());
}
void append(HWND h, std::string s) { // Logs are UTF-8; malformed tool bytes are
                                     // replaced for display only.
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring t(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), t.data(), n);
  std::wstring cr;
  for (size_t i = 0; i < t.size(); ++i) {
    if (t[i] == '\n' && (i == 0 || t[i - 1] != '\r'))
      cr += '\r';
    cr += t[i];
  }
  if (GetWindowTextLengthW(h) > 750000)
    SetWindowTextW(h, L"[Earlier output is in the complete durable log.]\r\n");
  auto end = GetWindowTextLengthW(h);
  SendMessageW(h, EM_SETSEL, end, end);
  SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)cr.c_str());
}
void output(const std::string &s) {
  std::lock_guard<std::mutex> lock(outputMutex);
  pending += s;
  PostMessageW(window, OUTPUT, 0, 0);
}
bool approve(const std::string &s) { return SendMessageW(window, REVIEW, 0, (LPARAM)&s) != 0; }
struct ReviewState {
  bool done = false, approved = false;
  HWND text, yes, no;
  HFONT font;
};
LRESULT CALLBACK ReviewProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto *p = (ReviewState *)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (m == WM_NCCREATE) {
    p = (ReviewState *)((CREATESTRUCTW *)l)->lpCreateParams;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p);
  }
  if (!p)
    return DefWindowProcW(h, m, w, l);
  if (m == WM_SIZE) {
    int x = LOWORD(l), y = HIWORD(l);
    MoveWindow(p->text, 16, 16, x - 32, y - 78, TRUE);
    MoveWindow(p->yes, x - 300, y - 48, 140, 32, TRUE);
    MoveWindow(p->no, x - 150, y - 48, 130, 32, TRUE);
    return 0;
  }
  if (m == WM_COMMAND) {
    p->approved = LOWORD(w) == 1;
    p->done = true;
    DestroyWindow(h);
    return 0;
  }
  if (m == WM_CLOSE) {
    p->done = true;
    DestroyWindow(h);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
bool reviewDialog(const std::string &text, bool readonly = false) {
  if (smoke)
    throw std::runtime_error("Smoke test must never approve a mutation");
  ReviewState st;
  HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TangOSLiteReview",
                           readonly ? L"TangOS Lite - About & notices"
                                    : L"TangOS Lite - Review every change",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 940, 720,
                           window, nullptr, GetModuleHandleW(nullptr), &st);
  st.text = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL |
                                WS_HSCROLL | ES_AUTOHSCROLL,
                            16, 16, 890, 580, h, nullptr, nullptr, nullptr);
  SendMessageW(st.text, EM_SETLIMITTEXT, 64 * 1024 * 1024, 0);
  SendMessageW(st.text, WM_SETFONT, (WPARAM)monoFont, TRUE);
  set(st.text, text);
  st.yes = CreateWindowW(L"BUTTON", readonly ? L"Close" : L"Approve operation",
                         WS_CHILD | WS_VISIBLE | WS_TABSTOP, 620, 610, 140, 32, h, (HMENU)1,
                         nullptr, nullptr);
  st.no = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 770, 610, 130, 32,
                        h, (HMENU)2, nullptr, nullptr);
  SendMessageW(st.yes, WM_SETFONT, (WPARAM)uiFont, TRUE);
  SendMessageW(st.no, WM_SETFONT, (WPARAM)uiFont, TRUE);
  if (readonly)
    ShowWindow(st.no, SW_HIDE);
  EnableWindow(window, FALSE);
  SetFocus(readonly ? st.yes : st.no);
  MSG msg;
  while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(h, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  EnableWindow(window, TRUE);
  SetForegroundWindow(window);
  return st.approved;
}
std::string resourceText(int id) {
  auto r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
  if (!r)
    return {};
  auto loaded = LoadResource(nullptr, r);
  return std::string((const char *)LockResource(loaded), SizeofResource(nullptr, r));
}
void about() {
  reviewDialog(
      "TangOS Lite 0.16.0\nPortable native Windows repository workbench.\nUse Encyclopedia "
      "for checks and Git; Repository for status.\nAlways read AGENTS.md and review "
      "changes before publication.\n\n" +
          resourceText(204) + "\n\nMinGW-w64 libwinpthread\n" + resourceText(202) +
          "\n\nGCC Runtime Library Exception\n" + resourceText(203) + "\n\nGPLv3\n" +
          resourceText(205) + "\n\nNunito\n" + resourceText(207) + "\n\nRaster dependencies\n" +
          resourceText(208) + "\n\nnlohmann JSON\n" + resourceText(210),
      true);
}
void fillChecks() {
  checks = discoverChecks(repo, settings);
  SendMessageW(checkCombo, CB_RESETCONTENT, 0, 0);
  for (auto &c : checks) {
    auto s = wide(c.name + (c.available ? "" : " [unavailable]"));
    SendMessageW(checkCombo, CB_ADDSTRING, 0, (LPARAM)s.c_str());
  }
  SendMessageW(checkCombo, CB_SETCURSEL, smoke ? 2 : 0, 0);
}
void start(const std::function<void()> &job) {
  if (busy)
    return;
  if (worker.joinable())
    worker.join();
  runner.reset();
  busy = true;
  EnableWindow(runButton, FALSE);
  EnableWindow(actionButton, FALSE);
  EnableWindow(cancelButton, TRUE);
  set(activityLabel, "Running - output saved continuously. Cancel stops the process tree.");
  InvalidateRect(window, nullptr, FALSE);
  worker = std::thread([job] {
    try {
      job();
    } catch (const std::exception &e) {
      output("\nERROR: " + std::string(e.what()) + "\n");
      if (smoke)
        smokeExit = 1;
    }
    PostMessageW(window, DONE, 0, 0);
  });
}
void selectRepo(std::string projectId = {}) {
  if (consoleUI && consoleUI->running())
    throw std::runtime_error("Stop agents before switching repositories");
  auto selected = fs::u8path(value(repoEdit));
  start([selected, projectId] {
    Repository r(runner, selected, settings);
    repo = r.root;
    workspaceRemote = false;
    settings.repository = utf8(repo.wstring());
    settings.activeProject = projectId.empty() ? settings.repository : projectId;
    saveSettings(config, settings);
    Json entry{{"id", settings.activeProject},
               {"repository", settings.repository},
               {"title", utf8(repo.filename().wstring())}};
    try {
      auto descriptor = loadDescriptor(repo);
      entry["title"] = descriptor.title;
      entry["descriptor"] = descriptor.document;
    } catch (...) {
      // Repositories without a descriptor can still be remembered and set up.
    }
    Backend registry(repo, dataDir, settings);
    // Selecting a local repository authorizes remembering it in local settings.
    auto preview = registry.invoke("projects.register", entry);
    entry["confirmation"] = preview.at("confirmation");
    registry.invoke("projects.register", entry);
    auto s = new std::string(r.status());
    PostMessageW(window, STATE, 0, (LPARAM)s);
  });
}
void openProjectState(const std::string &id) {
  Backend backend(repo, dataDir, settings);
  Json args{{"id", id}};
  auto preview = backend.invoke("projects.open", args);
  args["confirmation"] = preview.at("confirmation");
  auto opened = backend.invoke("projects.open", args);
  repo = fs::u8path(opened.at("path").get<std::string>());
  workspaceRemote = !opened.at("cloned").get<bool>();
  settings.activeProject = id;
  settings.repository = workspaceRemote ? std::string() : utf8(repo.wstring());
  saveSettings(config, settings);
  auto status = workspaceRemote ? "Viewer-only project: " + opened.value("title", id) +
                                      "\nNo checkout, tools or agents are running."
                                : Repository(runner, repo, settings).status();
  PostMessageW(window, STATE, 0, (LPARAM) new std::string(status));
}
void selectProject(const std::string &id) {
  if (consoleUI && consoleUI->running())
    throw std::runtime_error("Stop active operations before switching projects");
  start([id] { openProjectState(id); });
}
void importRemoteProject() {
  IFileDialog *dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog))))
    throw std::runtime_error("Cannot open the descriptor picker");
  dialog->SetTitle(L"Add remote project: choose its downloaded tangos.json");
  COMDLG_FILTERSPEC filters[] = {{L"Project descriptor", L"*.json"}};
  dialog->SetFileTypes(1, filters);
  fs::path file;
  if (SUCCEEDED(dialog->Show(window))) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        file = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
  if (file.empty())
    return;
  if (fs::file_size(file) > 1024 * 1024)
    throw std::runtime_error("Project descriptor exceeds 1 MiB");
  auto descriptor = parseDescriptor(read(file));
  auto project = descriptor.document.at("project");
  Json entry{{"id", "remote:" + project.at("name").get<std::string>()},
             {"title", descriptor.title},
             {"descriptor", descriptor.document},
             {"github", project.value("github", std::string())}};
  start([entry] {
    Backend backend(repo, dataDir, settings);
    auto args = entry;
    auto preview = backend.invoke("projects.register", args);
    args["confirmation"] = preview.at("confirmation");
    backend.invoke("projects.register", args);
    openProjectState(entry.at("id").get<std::string>());
  });
}
void refresh() {
  if (workspaceRemote) {
    selectProject(settings.activeProject);
    return;
  }
  if (repo.empty())
    return;
  start([] {
    Repository r(runner, repo, settings);
    auto s = new std::string(r.status());
    PostMessageW(window, STATE, 0, (LPARAM)s);
  });
}
void runCheck() {
  if (repo.empty())
    return;
  auto i = (size_t)SendMessageW(checkCombo, CB_GETCURSEL, 0, 0);
  start([i] {
    Repository r(runner, repo, settings);
    auto c = r.check(i);
    if (!smoke && !approve("Run repository code? Review this command and trust its "
                           "script first. Repository scripts can write files; this is "
                           "not a sandbox.\n\n" +
                           preview(c) + "\n\nRequires: " + checks[i].requirement))
      return;
    logPath = dataDir / "logs" / (uniqueId() + "-check.log");
    output("Complete log: " + utf8(logPath.wstring()) + "\n");
    auto result = runner.run(c, output, logPath);
    if (smoke && result.code)
      smokeExit = 1;
  });
}
void runAction() {
  if (repo.empty())
    return;
  auto idx = (size_t)SendMessageW(actionCombo, CB_GETCURSEL, 0, 0);
  if (idx >= actions.size())
    return;
  auto name = actions[idx], remote = value(remoteEdit), ref = value(refEdit),
       details = value(detailsEdit);
  start([name, remote, ref, details] {
    Repository r(runner, repo, settings);
    auto c = r.action(name, remote, ref, details);
    std::string approvedSnapshot;
    if (name == "Commit staged") {
      approvedSnapshot = r.commitPreview();
      if (!approve("COMMIT STAGED\nMessage: " + details + "\n\n" + approvedSnapshot))
        return;
      if (r.commitPreview() != approvedSnapshot)
        throw std::runtime_error("Index changed after review. Preview again.");
    } else if (name == "Push reviewed") {
      approvedSnapshot = r.pushPreview(remote, ref);
      if (!approve("PUSH - review every outgoing commit below. Fetch before "
                   "review to refresh remote state.\n\n" +
                   approvedSnapshot))
        return;
      if (r.pushPreview(remote, ref) != approvedSnapshot)
        throw std::runtime_error("Outgoing commits changed after review. Preview again.");
    } else if (name == "Merge" || name == "Rebase" || name == "Pull (fast-forward)" ||
               name == "Stage paths" || name == "Add remote" || name == "Create draft PR" ||
               name == "Create branch" || name == "Switch branch" ||
               name == "Delete merged branch" || name == "Rebase continue" ||
               name == "Rebase abort" || name == "Merge continue" || name == "Merge abort" ||
               name == "Stash selected paths" || name == "Apply stash" || name == "Drop stash" ||
               name == "Create tag" || name == "Delete tag") {
      if (!approve("Confirm " + name + "\n\n" + preview(c) + "\n\n" + details +
                   "\n\nMerge/rebase can change source files. Git hooks are "
                   "repository-owned code. Resolve conflicts with Git outside "
                   "Lite. Review the exact operation; abort, stash drop and ref deletion require "
                   "this confirmation."))
        return;
      c = r.action(name, remote, ref, details);
    }
    logPath = dataDir / "logs" / (uniqueId() + "-git.log");
    output("Complete log: " + utf8(logPath.wstring()) + "\n");
    runner.run(c, output, logPath);
  });
}
HWND control(const wchar_t *className, const wchar_t *text, DWORD style, int id = 0) {
  if (std::wstring(className) == L"BUTTON")
    style |= BS_OWNERDRAW;
  if (std::wstring(className) == L"COMBOBOX")
    style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
  HWND h = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 100, 30, window,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
  SendMessageW(h, WM_SETFONT, (WPARAM)uiFont, TRUE);
  return h;
}
HWND titleLabel, repoLabel, checkLabel, actionLabel, remoteLabel, refLabel, detailLabel,
    statusLabel, logLabel, browseButton, selectButton, refreshButton, logsButton, configButton,
    agentsButton;
void layout(int w, int h) {
  for (HWND item : {titleLabel, repoLabel, checkLabel, actionLabel, remoteLabel, refLabel,
                    detailLabel, statusLabel, logLabel})
    ShowWindow(item, SW_HIDE);
  for (HWND item :
       {checkCombo, runButton, cancelButton, actionCombo, remoteEdit, refEdit, detailsEdit,
        actionButton, agentsButton, statusEdit, logEdit, logsButton, refreshButton, configButton})
    ShowWindow(item, workspaceReady ? SW_SHOW : SW_HIDE);
  MoveWindow(themeCombo, w - 275, 12, 128, 200, TRUE);
  MoveWindow(minimizeButton, w - 137, 11, 38, 30, TRUE);
  MoveWindow(maximizeButton, w - 97, 11, 38, 30, TRUE);
  MoveWindow(closeButton, w - 57, 11, 38, 30, TRUE);
  ShowWindow(controllerTab, workspaceReady ? SW_SHOW : SW_HIDE);
  ShowWindow(repositoryTab, workspaceReady ? SW_SHOW : SW_HIDE);
  ShowWindow(toolboxButton, workspaceReady ? SW_SHOW : SW_HIDE);
  MoveWindow(controllerTab, w / 2 - 149, 11, 142, 30, TRUE);
  MoveWindow(repositoryTab, w / 2 - 4, 11, 130, 30, TRUE);
  if (!workspaceReady) {
    ShowWindow(repoEdit, SW_SHOW);
    ShowWindow(projectButton, SW_SHOW);
    MoveWindow(projectButton, 144, 13, std::max(90, w / 2 - 308), 30, TRUE);
    int x = (w - 760) / 2, y = (h - 420) / 2;
    MoveWindow(repoEdit, x + 72, y + 174, 494, 34, TRUE);
    MoveWindow(selectButton, x + 578, y + 174, 110, 34, TRUE);
    MoveWindow(browseButton, x + 270, y + 252, 220, 38, TRUE);
    MoveWindow(activityLabel, x + 36, y + 366, 688, 24, TRUE);
    set(selectButton, "Open repo");
    set(browseButton, "Choose repo folder");
  } else {
    int rail = w - 354, cw = w - 382;
    ShowWindow(repoEdit, SW_HIDE);
    ShowWindow(projectButton, SW_SHOW);
    MoveWindow(projectButton, 144, 13, std::max(90, w / 2 - 308), 30, TRUE);
    MoveWindow(browseButton, w - 416, 12, 124, 30, TRUE);
    ShowWindow(selectButton, SW_HIDE);
    set(browseButton, "Change repo");
    MoveWindow(checkCombo, 44, 168, cw - 196, 240, TRUE);
    MoveWindow(runButton, 44, 210, 100, 34, TRUE);
    MoveWindow(cancelButton, 154, 210, 86, 34, TRUE);
    MoveWindow(logsButton, 250, 210, 100, 34, TRUE);
    MoveWindow(actionCombo, 44, 313, cw - 60, 300, TRUE);
    int half = (cw - 72) / 2;
    MoveWindow(remoteEdit, 44, 369, half, 30, TRUE);
    MoveWindow(refEdit, 56 + half, 369, half, 30, TRUE);
    MoveWindow(detailsEdit, 44, 425, cw - 60, 54, TRUE);
    MoveWindow(actionButton, cw - 122, 489, 106, 32, TRUE);
    MoveWindow(logEdit, 44, 572, cw - 60, std::max(42, h - 670), TRUE);
    MoveWindow(agentsButton, 44, h - 74, 130, 32, TRUE);
    MoveWindow(configButton, cw - 122, h - 74, 106, 32, TRUE);

    MoveWindow(refreshButton, rail + 218, 85, 96, 30, TRUE);
    MoveWindow(statusEdit, rail + 16, 376, 308, std::max(70, h - 540), TRUE);
    MoveWindow(toolboxButton, 30, h - 74, 138, 32, TRUE);
    MoveWindow(agentsButton, 178, h - 74, 110, 32, TRUE);
    MoveWindow(activityLabel, 302, h - 70, std::max(90, cw - 434), 24, TRUE);
    set(toolboxButton, toolboxOpen ? "Close tools" : "Encyclopedia");
    if (!toolboxOpen && !repositoryView) {
      for (HWND item : {checkCombo, runButton, cancelButton, actionCombo, remoteEdit, refEdit,
                        detailsEdit, actionButton, logEdit, logsButton, statusEdit})
        ShowWindow(item, SW_HIDE);
    }
    if (repositoryView) {
      for (HWND item : {checkCombo, runButton, cancelButton, actionCombo, remoteEdit, refEdit,
                        detailsEdit, actionButton, logEdit})
        ShowWindow(item, SW_HIDE);
      MoveWindow(statusEdit, 30, 126, cw - 32, h - 228, TRUE);
      MoveWindow(logsButton, rail + 16, 350, 130, 32, TRUE);
    }
  }
  if (consoleUI && workspaceReady) {
    consoleUI->resize(w - 28, h - 86);
    consoleUI->show(!toolboxOpen, repositoryView);
    if (!toolboxOpen) {
      for (HWND item : {checkCombo, runButton, cancelButton, actionCombo, remoteEdit, refEdit,
                        detailsEdit, actionButton, agentsButton, statusEdit, logEdit, logsButton,
                        refreshButton, configButton, activityLabel, toolboxButton})
        ShowWindow(item, SW_HIDE);
    }
  }
  InvalidateRect(window, nullptr, TRUE);
}
void paintChrome(HDC dc, int w, int h) {
  skin::background(dc, w, h);
  skin::label(dc, L"tang", 24, 14, 52, 28, 19, true);
  skin::label(dc, L"OS", 66, 14, 40, 28, 19, true, false, true);
  skin::label(dc, L"Lite", 103, 17, 34, 24, 13, true, true);
  if (!workspaceReady) {
    int x = (w - 760) / 2, y = (h - 420) / 2;
    skin::panel(dc, x, y, 760, 420, true);
    skin::label(dc, L"tangOS  Lite", x + 300, y + 30, 210, 30, 22, true);
    skin::label(dc, L"Point it at a decomp repo", x + 160, y + 76, 480, 42, 30, true);
    skin::label(dc,
                L"Choose a folder you already have. Run repository tools, "
                L"review changes,\nand watch checks and builds live.",
                x + 110, y + 124, 570, 44, 14, false, true);
    skin::label(dc, L"or", x + 365, y + 221, 40, 24, 12, false, true);
    skin::label(dc, L"Your repository · your remotes · your workflow", x + 168, y + 317, 470, 24,
                13, true);
    skin::label(dc, L"Your own ROM and compiler stay on your machine.", x + 190, y + 342, 460, 24,
                12, false, true);
    return;
  }
  int cw = w - 382, rail = w - 354;
  skin::panel(dc, w / 2 - 153, 8, 284, 36, true);
  skin::panel(dc, 14, 66, cw, h - 86);
  skin::label(dc, toolboxOpen && !repositoryView ? L"Encyclopedia" : L"Chaos Controller", 30, 83,
              200, 25, 15, true);
  skin::label(dc, L"This session", cw - 215, 85, 92, 22, 11, true, true);
  skin::label(dc, L"LOCAL", cw - 106, 85, 72, 22, 11, true);
  if (!repositoryView && toolboxOpen) {
    skin::panel(dc, 30, 122, cw - 32, 138, true);
    skin::label(dc, L"Repository checks", 44, 134, cw - 180, 24, 14, true);
    skin::label(dc, busy ? L"● running" : L"● ready", cw - 110, 136, 86, 24, 12, true, true);
    skin::panel(dc, 30, 278, cw - 32, 254, true);
    skin::label(dc, L"Git & upstreams", 44, 289, cw - 60, 24, 14, true);
    skin::label(dc, L"Remote / first ref / owner/repo", 44, 347, (cw - 72) / 2, 22, 11, false,
                true);
    skin::label(dc, L"Branch / second ref / PR base", 56 + (cw - 72) / 2, 347, (cw - 72) / 2, 22,
                11, false, true);
    skin::label(dc, L"Details · paths, commit message, URL or PR title", 44, 403, cw - 60, 22, 11,
                false, true);
    skin::label(dc, L"Writes reviewed  ·  port-only safety", 44, 495, cw - 185, 24, 12, false,
                true);
    skin::label(dc, L"Live activity", 44, 546, cw - 60, 22, 14, true);
  } else if (repositoryView)
    skin::label(dc, L"Branches, remotes, worktrees & conflicts", 30, 109, cw - 32, 20, 12, false,
                true);
  else {
    skin::label(dc, L"No AIs connected yet.", cw / 2 - 80, 157, 300, 24, 14, false, true);
    skin::label(dc,
                L"Follow AGENTS.md to coordinate your agents in separate worktrees.\nRun "
                L"repository checks and Git tools from the Encyclopedia below.",
                cw / 2 - 230, 191, 480, 54, 13, false, true);
  }
  skin::panel(dc, rail, 66, 340, h - 86, true);
  skin::label(dc, L"This repo needs", rail + 16, 85, 200, 28, 15, true);
  skin::label(dc, L"Local tools & verification", rail + 16, 116, 308, 24, 12, false, true);
  int yy = 151;
  for (auto &c : checks) {
    if (yy > 322)
      break;
    skin::label(dc, wide(std::string(c.available ? "✓  " : "○  ") + c.name), rail + 18, yy, 302, 24,
                13, c.available, !c.available);
    yy += 25;
  }
  if (!repositoryView && toolboxOpen)
    skin::label(dc, L"Repository status", rail + 16, 345, 308, 24, 14, true);
  skin::label(dc, L"Port-only  ·  Review before push", rail + 16, h - 139, 300, 23, 12, true, true);
  skin::mascot(dc, w - 137, h - 127, 96);
  skin::label(dc, L"v0.16.0", w - 74, h - 27, 60, 18, 10, false, true);
}
void snapshot(const fs::path &path) {
  RECT rect;
  GetClientRect(window, &rect);
  int w = rect.right, h = rect.bottom;
  HDC dc = GetDC(window), memory = CreateCompatibleDC(dc);
  HBITMAP bitmap = CreateCompatibleBitmap(dc, w, h);
  auto old = SelectObject(memory, bitmap);
  paintChrome(memory, w, h);
  EnumChildWindows(
      window,
      [](HWND child, LPARAM param) -> BOOL {
        // Render nested Console controls too; hidden top-level smoke windows
        // cannot use IsWindowVisible, so inspect visibility up to this window.
        for (HWND ancestor = child; ancestor && ancestor != window; ancestor = GetParent(ancestor))
          if (!(GetWindowLongW(ancestor, GWL_STYLE) & WS_VISIBLE))
            return TRUE;
        auto dc = (HDC)param;
        RECT bounds;
        GetWindowRect(child, &bounds);
        MapWindowPoints(nullptr, window, (POINT *)&bounds, 2);
        auto state = SaveDC(dc);
        SetViewportOrgEx(dc, bounds.left, bounds.top, nullptr);
        IntersectClipRect(dc, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
        SendMessageW(child, WM_PRINT, (WPARAM)dc,
                     PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
        // ComboBox WM_PRINT omits its owner-drawn selection on hidden windows.
        wchar_t className[32];
        GetClassNameW(child, className, 32);
        if (std::wstring(className) == L"ComboBox") {
          DRAWITEMSTRUCT selection{};
          selection.CtlType = ODT_COMBOBOX;
          selection.CtlID = GetDlgCtrlID(child);
          selection.itemID = (UINT)SendMessageW(child, CB_GETCURSEL, 0, 0);
          selection.hwndItem = child;
          selection.hDC = dc;
          selection.rcItem = {1, 1, bounds.right - bounds.left - 24, 29};
          SendMessageW(GetParent(child), WM_DRAWITEM, selection.CtlID, (LPARAM)&selection);
        }
        RestoreDC(dc, state);
        return TRUE;
      },
      (LPARAM)memory);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = w;
  info.bmiHeader.biHeight = h;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  std::vector<char> bytes((size_t)w * h * 4);
  SelectObject(memory, old);
  GetDIBits(dc, bitmap, 0, h, bytes.data(), &info, DIB_RGB_COLORS);
  BITMAPFILEHEADER header{};
  header.bfType = 0x4d42;
  header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
  header.bfSize = header.bfOffBits + (DWORD)bytes.size();
  std::ofstream file(path, std::ios::binary);
  file.write((const char *)&header, sizeof(header));
  file.write((const char *)&info.bmiHeader, sizeof(info.bmiHeader));
  file.write(bytes.data(), bytes.size());
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(window, dc);
}
void browse() {
  IFileDialog *d = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&d)))) {
    DWORD opts;
    d->GetOptions(&opts);
    d->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(d->Show(window))) {
      IShellItem *i = nullptr;
      if (SUCCEEDED(d->GetResult(&i))) {
        PWSTR p = nullptr;
        if (SUCCEEDED(i->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
          SetWindowTextW(repoEdit, p);
          CoTaskMemFree(p);
          selectRepo();
        }
        i->Release();
      }
    }
    d->Release();
  }
}
fs::path chooseImportPath(const wchar_t *title, bool folder) {
  IFileDialog *dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog))))
    throw std::runtime_error("Cannot open the project import picker");
  dialog->SetTitle(title);
  if (folder) {
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  } else {
    COMDLG_FILTERSPEC filter[] = {{L"Project ZIP", L"*.zip"}};
    dialog->SetFileTypes(1, filter);
  }
  fs::path chosen;
  if (SUCCEEDED(dialog->Show(window))) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        chosen = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
  return chosen;
}
void importProjectZip() {
  auto archive = chooseImportPath(L"Import project from ZIP", false);
  if (archive.empty())
    return;
  auto parent = chooseImportPath(L"Choose parent folder for the new project", true);
  if (parent.empty())
    return;
  auto destination = parent / archive.stem();
  start([archive, destination] {
    Backend backend(repo, dataDir, settings, {}, requestHttp, &runner, output);
    Json args{{"archive", utf8(archive.wstring())}, {"destination", utf8(destination.wstring())}};
    auto preview = backend.invoke("projects.importZip", args);
    if (!approve("Import into a new folder; no archive scripts will run.\n" + preview.dump(2)))
      return;
    args["confirmation"] = preview.at("confirmation");
    auto result = backend.invoke("projects.importZip", args);
    output(result.at("notice").get<std::string>() + "\n");
    openProjectState(result.at("id").get<std::string>());
  });
}
void projectMenu() {
  if (consoleUI && consoleUI->running())
    throw std::runtime_error("Stop agents before switching repositories");
  auto projects = Backend(repo, dataDir, settings).invoke("projects.list");
  HMENU menu = CreatePopupMenu();
  std::vector<std::string> ids;
  for (auto &entry : projects) {
    auto id = entry.at("id").get<std::string>();
    auto title = entry.value("title", id);
    bool cloned = entry.value("cloned", false);
    if (!cloned)
      title += " · viewer only";
    auto flags = MF_STRING | (entry.value("active", false) ? MF_CHECKED : 0);
    AppendMenuW(menu, flags, 1 + ids.size(), wide(title).c_str());
    ids.push_back(id);
  }
  if (!ids.empty())
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 10000, L"Open another local repository…");
  AppendMenuW(menu, MF_STRING, 10001, L"Add remote project (tangos.json)…");
  AppendMenuW(menu, MF_STRING, 10002, L"Discover remote projects…");
  AppendMenuW(menu, MF_STRING, 10003, L"Import project ZIP…");
  RECT bounds;
  GetWindowRect(projectButton, &bounds);
  auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, bounds.left, bounds.bottom, 0,
                                 window, nullptr);
  DestroyMenu(menu);
  if (selected == 10000)
    browse();
  else if (selected == 10001)
    importRemoteProject();
  else if (selected == 10003)
    importProjectZip();
  else if (selected == 10002)
    start([] {
      Backend backend(repo, dataDir, settings);
      auto args = Json::object();
      auto preview = backend.invoke("projects.discover", args);
      args["confirmation"] = preview.at("confirmation");
      auto result = backend.invoke("projects.discover", args);
      output(result.dump(2) + "\n");
    });
  else if (selected > 0 && selected <= ids.size()) {
    auto id = ids[selected - 1];
    auto entry = Backend(repo, dataDir, settings).invoke("projects.get", {{"id", id}});
    if (!entry.contains("descriptor") && entry.value("repository", std::string()).empty())
      start([id] {
        Backend backend(repo, dataDir, settings);
        Json args{{"id", id}};
        auto preview = backend.invoke("projects.download", args);
        args["confirmation"] = preview.at("confirmation");
        auto result = backend.invoke("projects.download", args);
        if (result.contains("warning"))
          output(result["warning"].get<std::string>() + "\n");
        openProjectState(id);
      });
    else
      selectProject(id);
  }
}
void automaticUpdate() {
  if (smoke)
    return;
  if (busy) {
    updatePending = true;
    return;
  }
  auto profiles = Backend(repo, dataDir, settings).invoke("connections.get");
  if (!profiles.contains("update.check"))
    return;
  auto profile = profiles["update.check"];
  if (!profile.value("enabled", false) || !profile.value("automatic", false) ||
      !profile.value("allowUpdateDownloads", false))
    return;
  start([] {
    Backend backend(repo, dataDir, settings);
    auto args = Json::object();
    auto ticket = backend.invoke("update.stage", args);
    args["confirmation"] = ticket.at("confirmation");
    auto result = backend.invoke("update.stage", args);
    if (result.value("state", std::string()) == "downloaded")
      PostMessageW(
          window, CONSOLE_UPDATE_STAGED, 0,
          reinterpret_cast<LPARAM>(new std::string(result.at("receipt").get<std::string>())));
  });
}
void automaticDiscovery() {
  if (smoke)
    return;
  if (busy) {
    discoveryPending = true;
    return;
  }
  auto profiles = Backend(repo, dataDir, settings).invoke("connections.get");
  if (!profiles.contains("projects.registry"))
    return;
  auto profile = profiles["projects.registry"];
  if (!profile.value("enabled", false) || !profile.value("automatic", false))
    return;
  start([profile] {
    Backend backend(repo, dataDir, settings);
    Json args = Json::object();
    auto preview = backend.invoke("projects.discover", args);
    args["confirmation"] = preview.at("confirmation");
    auto result = backend.invoke("projects.discover", args);
    output("Refreshed remote project registry.\n");
    if (profile.value("allowDescriptorDownloads", false))
      for (auto &entry : result.at("projects")) {
        if (runner.isCancelled())
          break;
        if (entry.value("cloned", false))
          continue;
        try {
          Json download{{"id", entry.at("id")}};
          auto ticket = backend.invoke("projects.download", download);
          download["confirmation"] = ticket.at("confirmation");
          auto cached = backend.invoke("projects.download", download);
          if (cached.contains("warning"))
            output(cached["warning"].get<std::string>() + "\n");
        } catch (const std::exception &error) {
          output(std::string(error.what()) + "\n");
        }
      }
  });
}
LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
  case CONSOLE_UPDATE_STAGED:
  case CONSOLE_UPDATE_INSTALL: {
    std::unique_ptr<std::string> receipt(reinterpret_cast<std::string *>(l));
    if (!receipt)
      return 0;
    if (m == CONSOLE_UPDATE_INSTALL && (busy || (consoleUI && consoleUI->running()))) {
      MessageBoxW(h, L"Stop active checks and agents before restarting into the update.", L"Update",
                  MB_OK | MB_ICONINFORMATION);
      return 0;
    }
    pendingUpdateReceipt = fs::u8path(*receipt);
    restartAfterUpdate = m == CONSOLE_UPDATE_INSTALL;
    if (restartAfterUpdate)
      PostMessageW(h, WM_CLOSE, 0, 0);
    else {
      set(activityLabel, "Portable update ready; it will be installed when you close Lite.");
      output("A checksum-verified portable update is ready and will install on exit.\n");
    }
    return 0;
  }
  case WM_CREATE: {
    window = h;
    uiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Nunito");
    monoFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Nunito");
    titleLabel = control(L"STATIC", L"TangOS Lite   |   native repository workbench", 0);
    repoLabel = control(L"STATIC", L"Repository", 0);
    repoEdit = control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP);
    projectButton = control(L"BUTTON", L"Choose a project", WS_TABSTOP, PROJECT_MENU);
    browseButton = control(L"BUTTON", L"Browse...", WS_TABSTOP, BROWSE);
    selectButton = control(L"BUTTON", L"Select", WS_TABSTOP, SELECT);
    refreshButton = control(L"BUTTON", L"Refresh", WS_TABSTOP, REFRESH);
    checkLabel = control(L"STATIC", L"Checks", 0);
    checkCombo = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
    runButton = control(L"BUTTON", L"Run check", WS_TABSTOP, RUN);
    cancelButton = control(L"BUTTON", L"Cancel", WS_TABSTOP, CANCEL);
    EnableWindow(cancelButton, FALSE);
    logsButton = control(L"BUTTON", L"Open logs", WS_TABSTOP, LOGS);
    configButton = control(L"BUTTON", L"Settings file", WS_TABSTOP, CONFIG);
    actionLabel = control(L"STATIC", L"Git / GitHub", 0);
    actionCombo = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
    for (auto &a : actions) {
      auto t = wide(a);
      SendMessageW(actionCombo, CB_ADDSTRING, 0, (LPARAM)t.c_str());
    }
    SendMessageW(actionCombo, CB_SETCURSEL, 0, 0);
    remoteLabel = control(L"STATIC", L"Remote / first ref / owner/repo", 0);
    remoteEdit = control(L"EDIT", L"origin", ES_AUTOHSCROLL | WS_TABSTOP);
    refLabel = control(L"STATIC", L"Branch / second ref / PR base", 0);
    refEdit = control(L"EDIT", L"main", ES_AUTOHSCROLL | WS_TABSTOP);
    actionButton = control(L"BUTTON", L"Execute", WS_TABSTOP, ACTION);
    agentsButton = control(L"BUTTON", L"Agent guide", WS_TABSTOP, AGENTS);
    detailLabel = control(L"STATIC",
                          L"Details: exact paths (one per line), commit "
                          L"message, remote URL, or PR title",
                          0);
    detailsEdit = control(L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP);
    statusLabel = control(L"STATIC", L"Status / branches / worktrees", 0);
    logLabel = control(L"STATIC", L"Live output - complete logs preserved on disk", 0);
    auto style = ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL | WS_HSCROLL |
                 WS_VSCROLL | WS_TABSTOP;
    statusEdit = control(L"EDIT", L"Select a Git repository to begin.", style);
    logEdit = control(L"EDIT",
                      L"Git, Python, GitHub CLI and build tools are external "
                      L"dependencies.\r\nNo ROM data is bundled.\r\n",
                      style);
    SendMessageW(statusEdit, EM_SETLIMITTEXT, 4 * 1024 * 1024, 0);
    SendMessageW(logEdit, EM_SETLIMITTEXT, 1024 * 1024, 0);
    SendMessageW(logEdit, WM_SETFONT, (WPARAM)monoFont, TRUE);
    SendMessageW(statusEdit, WM_SETFONT, (WPARAM)monoFont, TRUE);
    activityLabel = control(L"STATIC", L"Ready - port-only safety is enabled by default.", 0);
    set(repoEdit, settings.repository);
    themeCombo = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, THEME);
    for (auto theme : {L"aero", L"sunset", L"deepsea", L"bubblegum", L"lemonlime"})
      SendMessageW(themeCombo, CB_ADDSTRING, 0, (LPARAM)theme);
    SendMessageW(themeCombo, CB_SETCURSEL, settings.themeIndex, 0);
    skin::theme(settings.themeIndex);
    minimizeButton = control(L"BUTTON", L"−", WS_TABSTOP, MINIMIZE);
    maximizeButton = control(L"BUTTON", L"□", WS_TABSTOP, MAXIMIZE);
    closeButton = control(L"BUTTON", L"×", WS_TABSTOP, CLOSE);
    controllerTab = control(L"BUTTON", L"Chaos Controller", WS_TABSTOP, CONTROLLER_TAB);
    repositoryTab = control(L"BUTTON", L"Chaos Viewer", WS_TABSTOP, REPOSITORY_TAB);
    toolboxButton = control(L"BUTTON", L"Encyclopedia", WS_TABSTOP, TOOLBOX);
    fieldBrush = CreateSolidBrush(skin::field());
    SetTimer(h, 1, 200, nullptr);
    AppendMenuW(GetSystemMenu(h, FALSE), MF_SEPARATOR, 0, nullptr);
    AppendMenuW(GetSystemMenu(h, FALSE), MF_STRING, 0x1230, L"About TangOS Lite...");
    return 0;
  }
  case WM_NCCALCSIZE:
    if (w)
      return 0;
    break;
  case WM_NCHITTEST: {
    POINT point{(short)LOWORD(l), (short)HIWORD(l)};
    ScreenToClient(h, &point);
    RECT r;
    GetClientRect(h, &r);
    if (!IsZoomed(h)) {
      bool left = point.x<6, right = point.x> r.right - 6,
           top = point.y<6, bottom = point.y> r.bottom - 6;
      if (top && left)
        return HTTOPLEFT;
      if (top && right)
        return HTTOPRIGHT;
      if (bottom && left)
        return HTBOTTOMLEFT;
      if (bottom && right)
        return HTBOTTOMRIGHT;
      if (left)
        return HTLEFT;
      if (right)
        return HTRIGHT;
      if (top)
        return HTTOP;
      if (bottom)
        return HTBOTTOM;
    }
    if (point.y < 52)
      return HTCAPTION;
    return HTCLIENT;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT r;
    GetClientRect(h, &r);
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
    auto previous = SelectObject(buffer, bitmap);
    paintChrome(buffer, r.right, r.bottom);
    BitBlt(dc, 0, 0, r.right, r.bottom, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previous);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(h, &ps);
    return 0;
  }
  case WM_PRINTCLIENT: {
    RECT r;
    GetClientRect(h, &r);
    paintChrome((HDC)w, r.right, r.bottom);
    return 0;
  }
  case WM_DRAWITEM: {
    auto *i = (DRAWITEMSTRUCT *)l;
    if (i->CtlType == ODT_BUTTON) {
      skin::button(*i, i->CtlID == RUN || i->CtlID == ACTION || i->CtlID == SELECT,
                   i->CtlID == CANCEL);
      return TRUE;
    }
    if (i->CtlType == ODT_COMBOBOX) {
      FillRect(i->hDC, &i->rcItem, fieldBrush);
      if (i->itemID != (UINT)-1) {
        int length = (int)SendMessageW(i->hwndItem, CB_GETLBTEXTLEN, i->itemID, 0);
        std::wstring title(std::max(0, length) + 1, 0);
        if (length >= 0)
          SendMessageW(i->hwndItem, CB_GETLBTEXT, i->itemID, (LPARAM)title.data());
        // GDI text respects the control DC viewport for native paint and WM_PRINT.
        auto previousFont = SelectObject(i->hDC, uiFont);
        SetTextColor(i->hDC, skin::text());
        SetBkMode(i->hDC, TRANSPARENT);
        RECT textBounds = i->rcItem;
        textBounds.left += 8;
        textBounds.right -= 4;
        DrawTextW(i->hDC, title.c_str(), -1, &textBounds, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(i->hDC, previousFont);
      }
      return TRUE;
    }
    break;
  }
  case WM_MEASUREITEM:
    if (((MEASUREITEMSTRUCT *)l)->CtlType == ODT_COMBOBOX) {
      ((MEASUREITEMSTRUCT *)l)->itemHeight = 24;
      return TRUE;
    }
    break;
  case WM_CTLCOLORSTATIC:
    if ((HWND)l == statusEdit || (HWND)l == logEdit) {
      SetTextColor((HDC)w, skin::text());
      SetBkColor((HDC)w, skin::field());
      return (LRESULT)fieldBrush;
    }
    SetTextColor((HDC)w, skin::muted());
    SetBkColor((HDC)w, skin::field());
    SetBkMode((HDC)w, OPAQUE);
    return (LRESULT)fieldBrush;
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
    SetTextColor((HDC)w, skin::text());
    SetBkColor((HDC)w, skin::field());
    return (LRESULT)fieldBrush;
  case WM_SIZE:
    layout(LOWORD(l), HIWORD(l));
    return 0;
  case WM_SYSCOMMAND:
    if ((w & 0xfff0) == 0x1230 && !busy) {
      about();
      return 0;
    }
    break;
  case WM_LBUTTONUP: {
    RECT r;
    GetClientRect(h, &r);
    if (workspaceReady && !busy && (short)LOWORD(l) > r.right - 145 &&
        (short)HIWORD(l) > r.bottom - 130) {
      about();
      return 0;
    }
    break;
  }
  case WM_GETMINMAXINFO: {
    auto *info = (MINMAXINFO *)l;
    info->ptMinTrackSize = {1100, 760};
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &monitor);
    info->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left,
                           monitor.rcWork.top - monitor.rcMonitor.top};
    info->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left,
                       monitor.rcWork.bottom - monitor.rcWork.top};
    return 0;
  }
  case WM_COMMAND:
    if (LOWORD(w) == THEME && HIWORD(w) == CBN_SELCHANGE) {
      if (busy) {
        SendMessageW(themeCombo, CB_SETCURSEL, settings.themeIndex, 0);
        return 0;
      }
      settings.themeIndex = (int)SendMessageW(themeCombo, CB_GETCURSEL, 0, 0);
      skin::theme(settings.themeIndex);
      if (!busy)
        saveSettings(config, settings);
      DeleteObject(fieldBrush);
      fieldBrush = CreateSolidBrush(skin::field());
      RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
      return 0;
    }
    if (HIWORD(w) == BN_CLICKED) {
      auto id = LOWORD(w);
      if (id == TOOLBOX) {
        if (workspaceRemote) {
          output("Choose a local checkout before opening Git tools.\n");
          return 0;
        }
        toolboxOpen = !toolboxOpen;
        repositoryView = false;
        RECT r;
        GetClientRect(h, &r);
        layout(r.right, r.bottom);
        return 0;
      }
      if (id == CONTROLLER_TAB || id == REPOSITORY_TAB) {
        repositoryView = id == REPOSITORY_TAB;
        toolboxOpen = false;
        RECT r;
        GetClientRect(h, &r);
        layout(r.right, r.bottom);
        return 0;
      }
      if (id == MINIMIZE) {
        ShowWindow(h, SW_MINIMIZE);
        return 0;
      }
      if (id == MAXIMIZE) {
        ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
        return 0;
      }
      if (id == CLOSE) {
        PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
      }
      if (id == CANCEL) {
        runner.cancel();
        set(activityLabel, "Cancelling process tree; complete partial log retained...");
        return 0;
      }
      if (busy)
        return 0;
      try {
        switch (id) {
        case SELECT_PROJECT:
          selectProject(settings.activeProject);
          break;
        case PROJECT_MENU:
          projectMenu();
          break;
        case DISCOVER_PROJECTS:
          automaticDiscovery();
          break;
        case AUTO_UPDATE:
          automaticUpdate();
          break;
        case BROWSE:
          if (consoleUI && consoleUI->running())
            throw std::runtime_error("Stop fleet runs before switching repositories");
          browse();
          break;
        case SELECT:
          selectRepo();
          break;
        case REFRESH:
          refresh();
          break;
        case RUN:
          if (workspaceRemote)
            throw std::runtime_error("Choose a local checkout before running checks");
          runCheck();
          break;
        case ACTION:
          if (workspaceRemote)
            throw std::runtime_error("Choose a local checkout before changing Git state");
          runAction();
          break;
        case LOGS: {
          auto p = dataDir / "logs";
          fs::create_directories(p);
          ShellExecuteW(h, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          break;
        }
        case CONFIG:
          ShellExecuteW(h, L"open", L"notepad.exe", config.c_str(), nullptr, SW_SHOWNORMAL);
          output("Settings reload at next launch. Use [checks] argv overrides; "
                 "no shell expansion.\n");
          break;
        case AGENTS:
          if (workspaceRemote)
            throw std::runtime_error("Choose a local checkout before starting agents");
          toolboxOpen = true;
          repositoryView = false;
          {
            RECT r;
            GetClientRect(h, &r);
            layout(r.right, r.bottom);
          }
          start([] {
            Repository r(runner, repo, settings);
            auto text = r.agentHandoff();
            auto path = dataDir / "handoffs" / (uniqueId() + ".txt");
            write(path, text);
            output(text + "\nSaved handoff: " + utf8(path.wstring()) + "\n");
          });
          break;
        }
      } catch (const std::exception &e) {
        MessageBoxW(h, wide(e.what()).c_str(), L"TangOS Lite", MB_OK | MB_ICONERROR);
      }
    }
    return 0;
  case OUTPUT: {
    std::string p;
    {
      std::lock_guard<std::mutex> lock(outputMutex);
      p.swap(pending);
    }
    append(logEdit, p);
    return 0;
  }
  case CONSOLE_RELOAD:
    consoleRepository.clear();
    refresh();
    return 0;
  case CONSOLE_OPEN_REPO: {
    std::unique_ptr<std::string> path(reinterpret_cast<std::string *>(l));
    auto opened = Json::parse(*path);
    set(repoEdit, opened.at("path").get<std::string>());
    selectRepo(opened.value("projectId", std::string()));
    return 0;
  }
  case CONSOLE_PICK_REPO:
    PostMessageW(h, WM_COMMAND, MAKEWPARAM(BROWSE, BN_CLICKED), 0);
    return 0;
  case STATE: {
    auto *s = (std::string *)l;
    set(statusEdit, *s);
    delete s;
    set(repoEdit, settings.repository);
    if (!workspaceRemote)
      fillChecks();
    else {
      checks.clear();
      SendMessageW(checkCombo, CB_RESETCONTENT, 0, 0);
    }
    workspaceReady = true;
    std::string projectTitle = utf8(repo.filename().wstring());
    try {
      projectTitle = loadDescriptor(repo).title;
    } catch (...) {
    }
    set(projectButton, projectTitle + "  ▾");
    if (!consoleUI || consoleRepository != repo) {
      consoleUI.reset();
      consoleRepository = repo;
      consoleUI = std::make_unique<ConsoleUI>(
          h, uiFont, repo, dataDir, settings,
          [] {
            if (workspaceRemote) {
              output("Choose a local checkout before opening Git tools.\n");
              return;
            }
            toolboxOpen = true;
            repositoryView = false;
            RECT r;
            GetClientRect(window, &r);
            layout(r.right, r.bottom);
          },
          [](const Settings &next) {
            settings = next;
            saveSettings(config, settings);
          },
          false, std::string(), std::function<void(Json)>(), workspaceRemote);
    }
    RECT rect;
    GetClientRect(h, &rect);
    layout(rect.right, rect.bottom);
    return 0;
  }
  case REVIEW:
    try {
      return reviewDialog(*(std::string *)l);
    } catch (...) {
      return 0;
    }
  case DONE:
    if (worker.joinable())
      worker.join();
    busy = false;
    if (updatePending) {
      updatePending = false;
      PostMessageW(h, WM_COMMAND, AUTO_UPDATE, 0);
    }
    if (discoveryPending) {
      discoveryPending = false;
      PostMessageW(h, WM_COMMAND, DISCOVER_PROJECTS, 0);
    }
    InvalidateRect(h, nullptr, FALSE);
    EnableWindow(runButton, TRUE);
    EnableWindow(actionButton, TRUE);
    EnableWindow(cancelButton, FALSE);
    set(activityLabel, "Finished - inspect output and full log. Refresh status "
                       "after Git changes.");
    if (smoke) {
      if (smokePhase == 1 && !repo.empty() && !smokeExit) {
        auto remembered = Backend(repo, dataDir, settings).invoke("projects.list");
        bool found = false;
        for (auto &project : remembered)
          if (project.value("repository", std::string()) == settings.repository &&
              project.value("title", std::string()) == "Native GUI fixture")
            found = true;
        if (!found && fs::exists(repo / "tangos.json")) {
          try {
            loadDescriptor(repo);
            smokeExit = 1;
          } catch (...) {
          }
        }
        if (consoleUI) {
          consoleUI->smokeScreens(config.parent_path(), snapshot);
          if (fs::exists(repo / "tangos.json"))
            consoleUI->smokeRemote(config.parent_path(), snapshot);
        }
        snapshot(config.parent_path() / "controller.bmp");
        SendMessageW(h, WM_COMMAND, REPOSITORY_TAB, 0);
        snapshot(config.parent_path() / "repository.bmp");
        SendMessageW(h, WM_COMMAND, CONTROLLER_TAB, 0);
        smokePhase = 2;
        smokeTicks = 0;
        PostMessageW(h, WM_COMMAND, TOOLBOX, 0);
        PostMessageW(h, WM_COMMAND, RUN, 0);
      } else if (smokePhase == 2 || smokeExit) {
        if (resourceText(202).find("mingw-w64") == std::string::npos ||
            resourceText(203).find("GCC RUNTIME") == std::string::npos)
          smokeExit = 1;
        HDC fontDC = GetDC(h);
        auto oldFont = SelectObject(fontDC, uiFont);
        wchar_t actualFont[128]{};
        GetTextFaceW(fontDC, 128, actualFont);
        if (std::wstring(actualFont) != L"Nunito")
          smokeExit = 1;
        SelectObject(fontDC, oldFont);
        ReleaseDC(h, fontDC);
        auto screen = value(statusEdit);
        auto full = read(logPath);
        if (screen.find("main") == screen.npos || full.find("fixture.cpp:42") == full.npos ||
            smokeTicks < 3 || value(logEdit).find("fixture.cpp:42") == std::string::npos)
          smokeExit = 1;
        write(config.parent_path() / "gui-smoke-report.txt",
              std::string(smokeExit ? "FAIL" : "PASS") +
                  " native window: embedded Nunito, TinySkia, repository selection, status, check "
                  "execution, UI log, durable log, responsive timer ticks=" +
                  std::to_string(smokeTicks) + "\n" + screen + "\n" + full);
        snapshot(config.parent_path() / "workspace.bmp");
        for (int theme = 0; theme < 5; ++theme) {
          SendMessageW(themeCombo, CB_SETCURSEL, theme, 0);
          SendMessageW(h, WM_COMMAND, MAKEWPARAM(THEME, CBN_SELCHANGE), (LPARAM)themeCombo);
          snapshot(config.parent_path() / fs::u8path("theme-" + std::to_string(theme) + ".bmp"));
        }
        SendMessageW(themeCombo, CB_SETCURSEL, 0, 0);
        SendMessageW(h, WM_COMMAND, MAKEWPARAM(THEME, CBN_SELCHANGE), (LPARAM)themeCombo);
        smokePhase = 3;
        PostMessageW(h, WM_CLOSE, 0, 0);
      }
    }
    return 0;
  case WM_TIMER:
    skin::advance(IsWindowVisible(h) && !IsIconic(h));
    if (skin::animationEnabled() && IsWindowVisible(h) && !IsIconic(h))
      InvalidateRect(h, nullptr, FALSE);
    if (smoke && smokePhase == 2)
      ++smokeTicks;
    if (smoke && smokePhase == 0) {
      set(activityLabel, "Previous status to replace completely.");
      UpdateWindow(activityLabel);
      set(activityLabel, "Ready - port-only safety is enabled by default.");
      UpdateWindow(activityLabel);
      HDC statusDC = GetDC(activityLabel);
      if ((HBRUSH)SendMessageW(h, WM_CTLCOLORSTATIC, (WPARAM)statusDC, (LPARAM)activityLabel) ==
              GetStockObject(NULL_BRUSH) ||
          GetBkMode(statusDC) != OPAQUE)
        smokeExit = 1;
      ReleaseDC(activityLabel, statusDC);
      snapshot(config.parent_path() / "landing.bmp");
      smokePhase = 1;
      PostMessageW(h, WM_COMMAND, SELECT, 0);
    }
    return 0;
  case WM_CLOSE:
    if (consoleUI && consoleUI->running()) {
      consoleUI->stop();
      MessageBoxW(h,
                  L"Cancelling fleet processes. Complete logs and worktrees are preserved; close "
                  L"again after they stop.",
                  L"TangOS Lite", MB_OK);
      return 0;
    }
    if (busy) {
      runner.cancel();
      set(activityLabel, "Wait for cancellation to finish, then close.");
      return 0;
    }
    consoleUI.reset();
    DestroyWindow(h);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(smokeExit);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
  SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT);
  SetEnvironmentVariableW(L"GIT_TERMINAL_PROMPT", L"0");
  SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  skin::initialize();
  INITCOMMONCONTROLSEX init{sizeof(init), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&init);
  int argc;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  try {
    if (argc == 5 && std::wstring(argv[1]) == L"--apply-portable-update") {
      auto result = runPortableUpdateHelper(fs::path(argv[2]), std::stoul(utf8(argv[3])),
                                            std::wstring(argv[4]) == L"restart");
      LocalFree(argv);
      return result;
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--mcp-stdio") {
      auto result = runMcpStdio(fs::path(argv[2]), utf8(argv[3]));
      LocalFree(argv);
      return result;
    }
    if (argc == 6 && std::wstring(argv[1]) == L"--backend") {
      fs::path repository = std::wstring(argv[2]) == L"-" ? fs::path() : fs::path(argv[2]);
      fs::path data = argv[3];
      auto request = Json::parse(read(fs::path(argv[4])));
      Settings prefs = loadSettings(data / "settings.ini");
      Vault vault(data / "vault");
      auto result = Backend(repository, data, prefs, vault.values())
                        .invoke(request.at("method"), request.value("arguments", Json::object()));
      write(fs::path(argv[5]), result.dump(2));
      LocalFree(argv);
      return 0;
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--verify-rom") {
      auto expected = trim(utf8(argv[3]));
      if (expected.size() != 64 ||
          expected.find_first_not_of("0123456789abcdefABCDEF") != expected.npos)
        throw std::runtime_error("rom_sha256 must be a trusted 64-digit SHA256");
      for (auto &c : expected)
        c = (char)tolower((unsigned char)c);
      auto digest = sha256File(fs::path(argv[2]));
      std::cout << "Local ROM SHA256: " << digest << "\n"
                << (digest == expected ? "Verified identity"
                                       : "MISMATCH - obtain the correct "
                                         "version; do not distribute ROM data")
                << "\n";
      LocalFree(argv);
      return digest == expected ? 0 : 1;
    }
    dataDir = localData();
    config = dataDir / "settings.ini";
    settings = loadSettings(config);
    if (argc == 4 && std::wstring(argv[1]) == L"--smoke-test") {
      smoke = true;
      settings.repository = utf8(argv[2]);
      settings.activeProject.clear();
      config = fs::path(argv[3]);
      dataDir = config.parent_path() / "app-state";
      if (!fs::exists(fs::path(argv[2]) / ".tangos-lite-test-fixture"))
        throw std::runtime_error("Smoke test requires explicit fixture marker");
      settings = loadSettings(config);
      settings.repository = utf8(argv[2]);
      settings.activeProject.clear();
    }
    if (!fs::exists(config))
      saveSettings(config, settings);
    WNDCLASSW wc{};
    wc.lpfnWndProc = ReviewProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"TangOSLiteReview";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"TangOSLite";
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    RegisterClassW(&wc);
    auto h = CreateWindowExW(
        0, wc.lpszClassName, L"TangOS Lite",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 820, nullptr, nullptr, instance, nullptr);
    if (!h)
      throw std::runtime_error("Cannot create native window");
    ShowWindow(h, show);
    UpdateWindow(h);
    if (!smoke && !settings.activeProject.empty())
      PostMessageW(h, WM_COMMAND, SELECT_PROJECT, 0);
    else if (!smoke && !settings.repository.empty())
      PostMessageW(h, WM_COMMAND, SELECT, 0);
    if (!smoke)
      PostMessageW(h, WM_COMMAND, DISCOVER_PROJECTS, 0);
    if (!smoke)
      PostMessageW(h, WM_COMMAND, AUTO_UPDATE, 0);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      if (!IsDialogMessageW(h, &msg)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
    if (worker.joinable())
      worker.join();
    if (!pendingUpdateReceipt.empty()) {
      try {
        launchPortableUpdate(dataDir, pendingUpdateReceipt, GetCurrentProcessId(),
                             restartAfterUpdate);
      } catch (const std::exception &error) {
        MessageBoxW(nullptr, wide(error.what()).c_str(),
                    L"Portable update retained; current executable unchanged",
                    MB_OK | MB_ICONERROR);
      }
    }
    DeleteObject(uiFont);
    DeleteObject(monoFont);
    DeleteObject(fieldBrush);
    skin::shutdown();
    LocalFree(argv);
    CoUninitialize();
    return (int)msg.wParam;
  } catch (const std::exception &e) {
    if (argc == 6 && std::wstring(argv[1]) == L"--backend") {
      write(fs::path(argv[5]), Json({{"error", e.what()}}).dump(2));
      return 1;
    }
    if (argc > 1 &&
        (std::wstring(argv[1]) == L"--verify-rom" || std::wstring(argv[1]) == L"--mcp-stdio")) {
      std::cerr << e.what() << "\n";
      return 1;
    }
    if (smoke) {
      write(config.parent_path() / "gui-smoke-report.txt",
            std::string("FAIL native GUI workflow: ") + e.what() + "\n");
      return 1;
    }
    MessageBoxW(nullptr, wide(e.what()).c_str(), L"TangOS Lite startup failed",
                MB_OK | MB_ICONERROR);
    return 1;
  }
}
