#include "controller_view.h"
#include <algorithm>
#include <cmath>
namespace lite {
double controllerProgress(double from, double to, double elapsedMilliseconds) {
  // CSS ease = cubic-bezier(.25,.1,.25,1), over the reference's 300 ms.
  double elapsed = std::clamp(elapsedMilliseconds / 300., 0., 1.);
  double low = 0, high = 1;
  auto curve = [](double t, double first, double second) {
    return 3 * (1 - t) * (1 - t) * t * first + 3 * (1 - t) * t * t * second + t * t * t;
  };
  for (int i = 0; i < 24; ++i) {
    double t = (low + high) / 2;
    if (curve(t, .25, .25) < elapsed)
      low = t;
    else
      high = t;
  }
  double eased = elapsed == 0 ? 0 : elapsed == 1 ? 1 : curve((low + high) / 2, .1, 1);
  return from + (to - from) * eased;
}
namespace {
bool javascriptSpace(wchar_t c) {
  return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0xa0 || c == 0x1680 ||
         (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f ||
         c == 0x3000 || c == 0xfeff;
}
} // namespace
Json controllerView(const Json &agent, const Json &batches, const Json &runs) {
  const auto name = agent.at("name").get<std::string>();
  const Json *batch = nullptr, *latest = nullptr;
  int remaining = 0, queued = 0;
  for (auto &row : batches) {
    if (row.value("targetAgent", std::string()) != name)
      continue;
    // JavaScript stable sort retains the first batch on equal timestamps.
    if (!batch || row.value("createdAt", int64_t(0)) > batch->value("createdAt", int64_t(0)))
      batch = &row;
    if (row.value("status", std::string()) != "done") {
      queued += row.value("status", std::string()) == "queued";
      for (auto &item : row.value("items", Json::array()))
        remaining += !(item.value("worked", false) || item.value("done", false));
    }
  }
  for (auto &run : runs) {
    auto client = run.value("client", Json::object());
    if (!client.is_object())
      client = Json::object();
    auto owner = run.value("source", std::string()) == "ai"
                     ? client.value("name", std::string("AI"))
                     : std::string("You");
    // Original latestByName intentionally takes the later run on equal timestamps.
    if (owner == name &&
        (!latest || run.value("startedAt", int64_t(0)) >= latest->value("startedAt", int64_t(0))))
      latest = &run;
  }
  int analyzed = 0, total = 0;
  if (batch) {
    auto items = batch->value("items", Json::array());
    total = int(items.size());
    for (auto &item : items)
      analyzed += item.value("worked", false) || item.value("done", false);
  }
  auto stats = agent.value("stats", Json::object());
  Json task = stats.value("currentTask", Json(nullptr));
  if (task.is_null() && batch)
    task = batch->value("title", Json(nullptr));
  if (task.is_null() && latest)
    task = latest->value("label", Json(nullptr));
  std::string line;
  if (latest) {
    line = latest->value("output", std::string());
    // Use Unicode text so the tail limits count UTF-16 units like JavaScript.
    auto tail = wide(line);
    if (tail.size() > 400)
      tail.erase(0, tail.size() - 400);
    while (!tail.empty() && javascriptSpace(tail.back()))
      tail.pop_back();
    auto end = tail.find_last_of(L'\n');
    if (end != std::wstring::npos)
      tail.erase(0, end + 1);
    if (tail.size() > 90)
      tail.resize(90);
    line = utf8(tail);
  }
  Json result = {{"agent", agent},
                 {"analyzed", analyzed},
                 {"total", total},
                 {"live", latest && latest->value("status", std::string()) == "running"},
                 {"batchDone", batch && batch->value("status", std::string()) == "done"},
                 {"liveLine", line},
                 {"queueRemaining", remaining},
                 {"queuedBatches", queued}};
  if (batch)
    result["batch"] = *batch;
  if (!task.is_null())
    result["task"] = task;
  return result;
}
} // namespace lite
