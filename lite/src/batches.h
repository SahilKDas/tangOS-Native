#pragma once
#include "descriptor.h"
namespace lite {
std::string batchTarget(const Json &row);
class BatchBook {
  Json entries = Json::array();
  Json pending = {{"title", "Batch draft"}, {"prompt", ""}, {"items", Json::array()}};

public:
  void restore(const Json &state);
  Json serialize() const;
  Json snapshot() const { return entries; }
  Json draft() const { return pending; }
  void setDraft(const Json &draft);
  void add(const std::string &id, const std::string &agent, const std::string &name,
           const Json &rows, int64_t created, const std::string &title, const std::string &prompt);
  void activate(const std::string &agent, const Json &rows, int64_t at);
  void complete(const std::string &agent, const Json &rows);
  void reconcile(const std::string &agent, const Json &queue);
  void park(const std::string &agent, const std::string &reason);
  void clearAgent(const std::string &agent);
  void assign(const std::string &id, const std::string &agent, const std::string &name);
  void remove(const std::string &id);
  void reorder(const std::string &id, int direction);
  void clearDone();
  std::string instructions(const std::string &agent, const Json &rows) const;
};
} // namespace lite
