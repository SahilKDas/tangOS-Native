#include "platform.h"
#include <windows.h>
#include <bcrypt.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace lite {
namespace {
struct Handle {
  HANDLE h = nullptr;
  ~Handle() {
    if (h && h != INVALID_HANDLE_VALUE)
      CloseHandle(h);
  }
};
} // namespace
fs::path localData() {
  wchar_t p[32768];
  auto n = GetEnvironmentVariableW(L"LOCALAPPDATA", p, 32768);
  if (!n || n >= 32768)
    throw std::runtime_error("LOCALAPPDATA unavailable");
  return fs::path(p) / L"TangOSLite";
}
std::string uniqueId() {
  unsigned char bytes[16];
  if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    throw std::runtime_error("Windows secure random generation failed");
  std::ostringstream out;
  for (auto b : bytes)
    out << std::hex << std::setw(2) << std::setfill('0') << (int)b;
  return out.str();
}
std::string selfExecutable() {
  wchar_t path[32768];
  auto n = GetModuleFileNameW(nullptr, path, 32768);
  if (!n || n >= 32768)
    throw std::runtime_error("Executable path unavailable");
  return utf8(std::wstring(path, n));
}
std::string sha256File(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error("Cannot read local ROM file; set rom_path in settings.ini");
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    throw std::runtime_error("SHA256 unavailable");
  DWORD objectSize = 0, got = 0;
  BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objectSize, sizeof(objectSize), &got, 0);
  std::vector<unsigned char> object(objectSize), digest(32);
  auto cleanup = [&] {
    if (hash)
      BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
  };
  if (BCryptCreateHash(alg, &hash, object.data(), objectSize, nullptr, 0, 0) < 0) {
    cleanup();
    throw std::runtime_error("SHA256 initialization failed");
  }
  char buf[65536];
  while (file) {
    file.read(buf, sizeof(buf));
    auto n = file.gcount();
    if (n && BCryptHashData(hash, (PUCHAR)buf, (ULONG)n, 0) < 0) {
      cleanup();
      throw std::runtime_error("SHA256 update failed");
    }
  }
  if (!file.eof() || BCryptFinishHash(hash, digest.data(), (ULONG)digest.size(), 0) < 0) {
    cleanup();
    throw std::runtime_error("ROM read/hash failed");
  }
  cleanup();
  std::ostringstream out;
  for (auto byte : digest)
    out << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
  return out.str();
}
Result Runner::run(const Command &c, const Sink &sink, const fs::path &log) {
  if (c.argv.empty())
    throw std::runtime_error("Empty command");
  std::ofstream f;
  if (!log.empty()) {
    fs::create_directories(log.parent_path());
    f.open(log, std::ios::binary | std::ios::app);
    if (!f)
      throw std::runtime_error("Cannot create durable log");
  }
  Result result{0, {}};
  auto store = [&](const std::string &t) {
    if (!sink) {
      if (result.output.size() + t.size() > 64 * 1024 * 1024)
        throw std::runtime_error("Safety capture exceeded 64 MiB; external review required");
      result.output += t;
    }
    if (f.is_open()) {
      f << t;
      f.flush();
      if (!f)
        throw std::runtime_error("Log write failed");
    }
    if (sink)
      sink(t);
  };
  if (cancelled) {
    result.code = ERROR_CANCELLED;
    store("[CANCELLED] before process launch\n");
    return result;
  }
  // Filter before capture, disk and callbacks, including secrets split across
  // pipe reads. Credentials never enter the durable log in plaintext.
  std::vector<std::string> secretValues;
  for (auto &entry : c.environment)
    if ((entry.first.find("KEY") != entry.first.npos ||
         entry.first.find("TOKEN") != entry.first.npos ||
         entry.first.find("PASSWORD") != entry.first.npos) &&
        !entry.second.empty())
      secretValues.push_back(entry.second);
  std::string secretPending;
  auto filtered = [&](const std::string &chunk, bool final) {
    if (secretValues.empty()) {
      store(chunk);
      return;
    }
    secretPending += chunk;
    size_t at = 0;
    std::string clean;
    while (at < secretPending.size()) {
      bool full = false, partial = false;
      for (auto &value : secretValues) {
        auto available = secretPending.size() - at;
        auto count = std::min(available, value.size());
        if (secretPending.compare(at, count, value, 0, count) == 0) {
          if (available >= value.size()) {
            clean += "[REDACTED]";
            at += value.size();
            full = true;
            break;
          }
          partial = true;
        }
      }
      if (full)
        continue;
      if (partial && !final)
        break;
      clean += secretPending[at++];
    }
    secretPending.erase(0, at);
    if (!clean.empty())
      store(clean);
  };
  auto emit = [&](const std::string &t) { filtered(t, false); };
  emit("$ " + preview(c) + "\nWorking directory: " + utf8(c.cwd.wstring()) + "\n");
  // Explicit inherited handle list prevents children retaining other run's
  // pipes.
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  Handle rd, wr, in, job, proc, thread;
  if (!CreatePipe(&rd.h, &wr.h, &sa, 0) || !SetHandleInformation(rd.h, HANDLE_FLAG_INHERIT, 0))
    throw std::runtime_error("Cannot create output pipe");
  in.h = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                     0, nullptr);
  job.h = CreateJobObjectW(nullptr, nullptr);
  if (!job.h)
    throw std::runtime_error("Cannot create cancellation job");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    throw std::runtime_error("Cannot configure process job");
  SIZE_T bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
  std::vector<unsigned char> attrs(bytes);
  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof(si);
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdOutput = wr.h;
  si.StartupInfo.hStdError = wr.h;
  si.StartupInfo.hStdInput = in.h;
  si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
  if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes))
    throw std::runtime_error("Cannot initialize process attributes");
  HANDLE handles[] = {wr.h, in.h};
  if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                 sizeof(handles), nullptr, nullptr)) {
    DeleteProcThreadAttributeList(si.lpAttributeList);
    throw std::runtime_error("Cannot set inherited handles");
  }
  // Application path resolved before cwd change. Never execute repo-local
  // git.exe/gh.exe.
  wchar_t resolved[32768], pathEnv[32768];
  std::wstring exe = wide(c.argv[0]);
  DWORD pathSize = GetEnvironmentVariableW(L"PATH", pathEnv, 32768);
  if (!pathSize || pathSize >= 32768) {
    DeleteProcThreadAttributeList(si.lpAttributeList);
    throw std::runtime_error("PATH is unavailable or too long");
  }
  DWORD n = SearchPathW(pathEnv, exe.c_str(), L".exe", 32768, resolved, nullptr);
  if (!n || n >= 32768) {
    DeleteProcThreadAttributeList(si.lpAttributeList);
    emit("Executable missing: " + c.argv[0] + ". Install it and add it to PATH.\n");
    result.code = ERROR_FILE_NOT_FOUND;
    filtered("", true);
    return result;
  }
  auto line = commandLine(c.argv);
  // Each concurrent agent receives its own environment. Never mutate global
  // process variables to inject provider credentials into other runs.
  std::map<std::wstring, std::wstring> environment;
  auto inherited = GetEnvironmentStringsW();
  if (!inherited)
    throw std::runtime_error("Cannot read process environment");
  for (auto entry = inherited; *entry; entry += wcslen(entry) + 1) {
    std::wstring row(entry);
    auto at = row.find(L'=', row[0] == L'=' ? 1 : 0);
    if (at != row.npos)
      environment[row.substr(0, at)] = row.substr(at + 1);
  }
  FreeEnvironmentStringsW(inherited);
  environment[L"PYTHONDONTWRITEBYTECODE"] = L"1";
  for (auto &entry : c.environment) {
    auto name = wide(entry.first);
    if (name.empty() || name.find_first_of(L"=\0") != name.npos ||
        entry.second.find('\0') != std::string::npos)
      throw std::runtime_error("Invalid child environment variable");
    // Windows environment names are case insensitive.
    for (auto it = environment.begin(); it != environment.end();) {
      if (_wcsicmp(it->first.c_str(), name.c_str()) == 0)
        it = environment.erase(it);
      else
        ++it;
    }
    environment[name] = wide(entry.second);
  }
  std::vector<wchar_t> environmentBlock;
  for (auto &entry : environment) {
    auto row = entry.first + L"=" + entry.second;
    environmentBlock.insert(environmentBlock.end(), row.begin(), row.end());
    environmentBlock.push_back(0);
  }
  environmentBlock.push_back(0);
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(resolved, line.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT |
                               CREATE_UNICODE_ENVIRONMENT,
                           environmentBlock.data(), c.cwd.wstring().c_str(), &si.StartupInfo, &pi);
  auto err = GetLastError();
  DeleteProcThreadAttributeList(si.lpAttributeList);
  if (!ok) {
    emit("CreateProcess failed (Windows " + std::to_string(err) +
         "). Check executable and working directory.\n");
    result.code = err;
    filtered("", true);
    return result;
  }
  proc.h = pi.hProcess;
  thread.h = pi.hThread;
  if (!AssignProcessToJobObject(job.h, proc.h)) {
    TerminateProcess(proc.h, 1);
    throw std::runtime_error("Cannot contain process tree; refused to start");
  }
  ResumeThread(thread.h);
  CloseHandle(wr.h);
  wr.h = nullptr;
  bool killed = false;
  char buf[8192];
  for (;;) {
    if (cancelled && !killed) {
      TerminateJobObject(job.h, ERROR_CANCELLED);
      killed = true;
    }
    DWORD available = 0;
    if (!PeekNamedPipe(rd.h, nullptr, 0, nullptr, &available, nullptr))
      break;
    if (available) {
      DWORD got = 0;
      if (ReadFile(rd.h, buf, std::min<DWORD>(sizeof(buf), available), &got, nullptr) && got)
        emit(std::string(buf, got));
      continue;
    }
    if (WaitForSingleObject(proc.h, 20) == WAIT_OBJECT_0) { // All descendants must finish or be
                                                            // killed to release the pipe.
      JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
      QueryInformationJobObject(job.h, JobObjectBasicAccountingInformation, &info, sizeof(info),
                                nullptr);
      if (info.ActiveProcesses == 0) {
        // The previous Peek can precede the child's last write. All writers have
        // exited now, so drain through EOF before emitting the result footer.
        DWORD got = 0;
        while (ReadFile(rd.h, buf, sizeof(buf), &got, nullptr) && got)
          emit(std::string(buf, got));
        break;
      }
    }
  }
  WaitForSingleObject(proc.h, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(proc.h, &code);
  result.code = killed ? ERROR_CANCELLED : code;
  emit("\n[" +
       std::string(killed ? "CANCELLED"
                   : code ? "FAILED"
                          : "PASSED") +
       "] exit=" + std::to_string(result.code) + "\n");
  if (result.code && !killed)
    emit("Action: inspect file:line diagnostics above; rerun the displayed "
         "command in this repository. Missing ROM/assets must be supplied "
         "locally.\n");
  filtered("", true);
  return result;
}
} // namespace lite
