#include "batches.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace lite {
std::string batchTarget(const Json &row) {
  for (auto key : {"ref", "id"})
    if (row.contains(key))
      return row[key].is_string() ? row[key].get<std::string>() : row[key].dump();
  return row.value("module", std::string()) + ":" + row.value("name", std::string());
}
static std::set<std::string> keys(const Json &rows) {
  std::set<std::string> out;
  for (auto &row : rows)
    out.insert(batchTarget(row));
  return out;
}
void BatchBook::setDraft(const Json &draft) {
  if (!draft.is_object() || !draft.contains("items") || !draft["items"].is_array() ||
      draft["items"].size() > 500)
    throw std::runtime_error("Draft needs an items array with at most 500 targets");
  for (auto field : {"title", "prompt"})
    if (draft.contains(field) &&
        (!draft[field].is_string() || draft[field].get<std::string>().size() > 65536))
      throw std::runtime_error("Draft title/prompt must be text under 64 KiB");
  std::set<std::string> seen;
  for (auto &row : draft["items"]) {
    if (!row.is_object())
      throw std::runtime_error("Draft targets must be objects");
    auto key = batchTarget(row);
    if (key.empty() || key == ":" || !seen.insert(key).second)
      throw std::runtime_error("Draft targets need unique id/ref/name");
  }
  auto blocked = blockedBlob(draft.dump());
  if (!blocked.empty())
    throw std::runtime_error("Draft rejected: " + blocked);
  pending = draft;
  if (!pending.contains("title"))
    pending["title"] = "Batch draft";
  if (!pending.contains("prompt"))
    pending["prompt"] = "";
}
void BatchBook::restore(const Json &state) {
  if (!state.is_object())
    throw std::runtime_error("Batch state must be an object");
  entries = state.value("batches", Json::array());
  if (!entries.is_array())
    throw std::runtime_error("Batch history must be an array");
  for (auto &b : entries) {
    if (!b.is_object() || !b.contains("id") || !b["id"].is_string() || !b.contains("agentId") ||
        !b["agentId"].is_string() || !b.contains("items") || !b["items"].is_array())
      throw std::runtime_error("Invalid persisted batch");
    for (auto key : {"title", "prompt", "targetAgent", "status"})
      if (!b.contains(key) || !b[key].is_string())
        throw std::runtime_error("Invalid persisted batch metadata");
    if (b["status"] != "active" && b["status"] != "queued" && b["status"] != "done")
      throw std::runtime_error("Invalid batch status");
    for (auto &row : b["items"])
      if (!row.is_object())
        throw std::runtime_error("Invalid persisted batch target");
    if (b.value("status", std::string()) == "active") {
      b["status"] = "queued";
      b["parked"] = true;
      b["note"] = "Interrupted; preserved targets require review before resume";
    }
  }
  if (state.contains("draft"))
    setDraft(state["draft"]);
}
Json BatchBook::serialize() const { return {{"batches", entries}, {"draft", pending}}; }
void BatchBook::add(const std::string &id, const std::string &agent, const std::string &name,
                    const Json &rows, int64_t created, const std::string &title,
                    const std::string &prompt) {
  if (rows.empty())
    return;
  Json items = rows;
  for (auto &row : items) {
    row["worked"] = false;
    row["done"] = false;
  }
  entries.push_back({{"id", id},
                     {"agentId", agent},
                     {"targetAgent", name},
                     {"title", title.empty() ? name + " batch" : title},
                     {"prompt", prompt},
                     {"items", items},
                     {"status", "queued"},
                     {"createdAt", created}});
}
void BatchBook::activate(const std::string &agent, const Json &rows, int64_t at) {
  auto targets = keys(rows);
  for (auto &batch : entries)
    if (batch["agentId"] == agent && batch["status"] != "done")
      for (auto &row : batch["items"])
        if (targets.count(batchTarget(row))) {
          batch["status"] = "active";
          batch["activatedAt"] = at;
          batch["parked"] = false;
          break;
        }
}
void BatchBook::complete(const std::string &agent, const Json &rows) {
  auto targets = keys(rows);
  for (auto &batch : entries)
    if (batch["agentId"] == agent && batch["status"] != "done") {
      bool all = true;
      for (auto &row : batch["items"]) {
        if (targets.count(batchTarget(row)))
          row["worked"] = true;
        if (!row.value("worked", false) && !row.value("removed", false))
          all = false;
      }
      batch["status"] = all ? "done" : "queued";
      batch["note"] =
          "Processed targets require independent byte-match proof; worked is not matched";
    }
  size_t done = 0;
  for (auto &b : entries)
    if (b["status"] == "done")
      ++done;
  if (done > 30)
    for (auto it = entries.begin(); it != entries.end() && done > 30;) {
      if ((*it)["status"] == "done") {
        it = entries.erase(it);
        --done;
      } else
        ++it;
    }
}
void BatchBook::reconcile(const std::string &agent, const Json &queue, const Json &active) {
  auto targets = keys(queue);
  auto current = keys(active);
  for (auto &batch : entries)
    if (batch["agentId"] == agent && batch["status"] != "done") {
      bool open = false;
      for (auto &row : batch["items"])
        if (!row.value("worked", false)) {
          row["removed"] = !targets.count(batchTarget(row));
          if (!row["removed"].get<bool>())
            open = true;
        }
      bool running = false;
      for (auto &row : batch["items"])
        if (current.count(batchTarget(row)))
          running = true;
      batch["status"] = open ? running ? "active" : "queued" : "done";
    }
}
void BatchBook::park(const std::string &agent, const std::string &reason) {
  for (auto &batch : entries)
    if (batch["agentId"] == agent && batch["status"] != "done") {
      batch["status"] = "queued";
      batch["parked"] = true;
      batch["note"] = reason;
    }
}
void BatchBook::clearAgent(const std::string &agent, const Json &active) {
  auto retained = keys(active);
  for (auto it = entries.begin(); it != entries.end();)
    if ((*it)["agentId"] == agent && (*it)["status"] != "done") {
      bool current = false;
      for (auto &row : (*it)["items"])
        if (retained.count(batchTarget(row)))
          current = true;
      if (!current) {
        it = entries.erase(it);
        continue;
      }
      Json items = Json::array();
      for (auto &row : (*it)["items"])
        if (row.value("worked", false) || retained.count(batchTarget(row)))
          items.push_back(row);
      (*it)["items"] = items;
      (*it)["status"] = "active";
      ++it;
    } else
      ++it;
}
void BatchBook::assign(const std::string &id, const std::string &agent, const std::string &name) {
  for (auto &b : entries)
    if (b.at("id") == id) {
      if (b.at("status") != "queued")
        throw std::runtime_error("Only queued batches can be handed off");
      b["agentId"] = agent;
      b["targetAgent"] = name;
      b["parked"] = false;
      return;
    }
  throw std::runtime_error("Unknown batch");
}
void BatchBook::remove(const std::string &id) {
  for (auto it = entries.begin(); it != entries.end();)
    if ((*it)["id"] == id)
      it = entries.erase(it);
    else
      ++it;
}
void BatchBook::reorder(const std::string &id, int direction) {
  if (direction != -1 && direction != 1)
    throw std::runtime_error("Batch direction must be up/down");
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i]["id"] == id) {
      auto next = int64_t(i) + direction;
      if (next >= 0 && next < int64_t(entries.size()))
        std::swap(entries[i], entries[size_t(next)]);
      return;
    }
}
void BatchBook::clearDone() {
  for (auto it = entries.begin(); it != entries.end();)
    if ((*it)["status"] == "done")
      it = entries.erase(it);
    else
      ++it;
}
std::string BatchBook::instructions(const std::string &agent, const Json &rows) const {
  auto targets = keys(rows);
  std::string out;
  for (auto &batch : entries)
    if (batch["agentId"] == agent && batch["status"] != "done") {
      bool selected = false;
      for (auto &row : batch["items"])
        if (targets.count(batchTarget(row)))
          selected = true;
      if (selected && !batch.value("prompt", std::string()).empty())
        out += "\nUser batch instruction (repository and safety rules still apply):\n" +
               batch["prompt"].get<std::string>() + "\n";
    }
  return out;
}
} // namespace lite
