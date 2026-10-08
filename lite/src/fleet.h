#pragma once
#include "descriptor.h"
#include "batches.h"
#include "repository.h"
#include <windows.h>
#include <memory>
#include <mutex>
#include <thread>
namespace lite {
class Vault {
  fs::path directory;

public:
  explicit Vault(fs::path directory) : directory(std::move(directory)) {}
  void set(const std::string &name, const std::string &secret);
  void remove(const std::string &name);
  std::map<std::string, std::string> values() const;
};
struct AgentSpec {
  std::string name, kind = "api", role = "Unassigned", effort = "high", model, baseUrl,
                    dialect = "openai", key;
  std::string cli;
  int count = 16, attempts = 4, jobs = 1;
  bool loop = false;
};
struct AgentState {
  AgentSpec spec;
  std::string id, phase = "idle", detail, branch, lastLine;
  fs::path worktree, log, prompt, worklist, results;
  Json queue = Json::array(), assigned = Json::array();
  int completed = 0, total = 0;
  bool active = false;
};
Json agentJson(const AgentState &agent);
AgentState parseAgent(const Json &json);
class Fleet {
  struct Job {
    AgentState state;
    std::string runtimeRole, executionRole;
    Runner runner;
    std::thread worker;
    std::atomic<bool> active{false};
    std::atomic<bool> externalTask{false};
    std::mutex externalMutex;
  };
  fs::path repository, directory;
  Descriptor descriptor;
  Settings settings;
  Vault vault;
  mutable std::mutex mutex;
  std::map<std::string, std::shared_ptr<Job>> jobs;
  Sink events;
  BatchBook batchBook;
  HANDLE controllerOwnership = INVALID_HANDLE_VALUE;
  void saveLocked();
  void drive(const std::shared_ptr<Job> &job, bool execute);
  Json schedule(const std::shared_ptr<Job> &job, const fs::path &cwd);
  void audit(const std::shared_ptr<Job> &job);

public:
  Fleet(fs::path repository, fs::path directory, Descriptor descriptor, Settings settings,
        Sink events = {});
  ~Fleet();
  std::string add(AgentSpec spec);
  void configure(const std::string &id, AgentSpec spec);
  void remove(const std::string &id);
  std::vector<AgentState> snapshot() const;
  void enqueue(const std::string &id, const Json &rows, const std::string &title = {},
               const std::string &prompt = {});
  Json batches() const;
  Json draft() const;
  void saveDraft(const Json &draft);
  void enqueueDraft(const std::string &id);
  void handoff(const std::string &batch, const std::string &destination);
  Json generateDraft(const std::string &role, int count, Runner &runner, Sink progress = {});
  void editBatch(const std::string &id, int direction, bool remove = false);
  void clearDoneBatches();
  void clear(const std::string &id);
  void editQueue(const std::string &id, size_t index, int direction, bool remove = false);
  void start(const std::string &id, bool execute = true);
  void land(const std::string &id);
  void stop(const std::string &id);
  void stopAll();
  void stopExternal();
  void setPolicy(const Settings &settings);
  bool running() const;
  std::string review(const std::string &id);
  void commitReviewed(const std::string &id, const std::string &message, const std::string &tree);
  Json takeBatch(const std::string &id);
  void finishBatch(const std::string &id);
  Json backend(const std::string &method, const Json &args);
  bool toolEnabled(const std::string &id);
  Result runTool(const std::string &id, const std::string &tool, const Json &args);
};
} // namespace lite
