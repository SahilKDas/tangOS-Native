#include "activity.h"
#include <algorithm>
#include <chrono>
#include <cmath>
namespace lite {
Json measuredRole(const Json &stats) {
  auto by = stats.value("bySize", Json::object());
  auto rate = [&](const std::string &key) {
    auto tally = by.value(key, Json::object());
    auto attempts = tally.value("attempts", 0);
    return attempts >= 2 ? double(tally.value("matches", 0)) / attempts : -1.0;
  };
  if (stats.value("attempts", stats.value("matchAttempts", 0)) >= 4) {
    if (rate(">0x800") >= .4)
      return {{"role", "Hard matcher"}, {"why", "lands large functions others skip"}};
    if (rate("<=0x40") >= .6)
      return {{"role", "Refiner"}, {"why", "high hit rate - good at closing functions out"}};
    auto hit = stats.value("hitRate", 0.0);
    if (hit < .25)
      return {{"role", "Drafter"}, {"why", "gets functions close; let the Refiner finish them"}};
    if (hit >= .5)
      return {{"role", "Refiner"}, {"why", "steady, reliable at landing matches"}};
  }
  return {{"role", nullptr}, {"why", "still learning - assign it work to find its strengths"}};
}
void ActivityBus::publish(const Json &event) {
  std::lock_guard<std::mutex> lock(mutex);
  auto kind = event.at("kind").get<std::string>();
  if (kind == "run-started") {
    auto id = event.at("run").at("runId").get<std::string>();
    runs[id] = event.at("run");
    order.push_back(id);
    if (order.size() > 300) {
      runs.erase(order.front());
      order.erase(order.begin());
    }
    return;
  }
  auto id = event.value("runId", std::string());
  auto it = runs.find(id);
  if (it == runs.end())
    return;
  auto run = &it->second;
  if (kind == "run-output") {
    auto output = run->value("output", std::string()) + event.at("chunk").get<std::string>();
    // JavaScript caps UTF-16 code units; preserve that boundary for Unicode output.
    if (output.size() > 200000) {
      auto units = wide(output);
      if (units.size() > 200000) {
        units.erase(0, units.size() - 200000);
        output = utf8(units);
      }
    }
    (*run)["output"] = output;
  } else if (kind == "run-finished") {
    (*run)["status"] = event.at("status");
    (*run)["exitCode"] = event.at("exitCode");
    (*run)["finishedAt"] = event.at("finishedAt");
  }
}
Json ActivityBus::snapshot(const std::string &repository) const {
  std::lock_guard<std::mutex> lock(mutex);
  auto key = [](const std::string &value) {
    if (value.empty())
      return std::wstring();
    auto path = fs::u8path(value).lexically_normal();
    path.make_preferred();
    auto result = path.wstring();
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    return result;
  };
  auto selected = key(repository);
  auto out = Json::array();
  for (auto &id : order) {
    auto it = runs.find(id);
    if (it != runs.end() &&
        (repository.empty() || key(it->second.value("repository", std::string())) == selected))
      out.push_back(it->second);
  }
  return out;
}
void ActivityBus::clear() {
  std::lock_guard<std::mutex> lock(mutex);
  runs.clear();
  order.clear();
}
ActivityBus &activityBus() {
  static ActivityBus bus;
  return bus;
}
int64_t activityNow() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
Json activityStreams(const std::string &output) {
  auto models = Json::array(), tabs = Json::object();
  std::map<std::string, std::vector<std::string>> perModel;
  std::vector<std::string> all;
  auto lines = split(output, '\n');
  if (output.empty() || output.back() == '\n')
    lines.push_back("");
  for (auto &line : lines) {
    auto end = line.find(u8"⟧");
    if (line.rfind(u8"⟦", 0) == 0 && end != std::string::npos && end > 3) {
      auto model = line.substr(3, end - 3);
      auto bodyAt = end + 3;
      if (bodyAt < line.size() && line[bodyAt] == ' ')
        ++bodyAt;
      if (!perModel.count(model))
        models.push_back(model);
      perModel[model].push_back(line.substr(bodyAt));
      auto slash = model.find_last_of('/');
      all.push_back(model.substr(slash == model.npos ? 0 : slash + 1) + " | " +
                    line.substr(bodyAt));
    } else
      all.push_back(line);
  }
  auto join = [](const std::vector<std::string> &lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
      if (i)
        out += '\n';
      out += lines[i];
    }
    return out;
  };
  tabs["all"] = join(all);
  for (auto &entry : perModel)
    tabs[entry.first] = join(entry.second);
  return {{"models", models}, {"byTab", tabs}};
}
std::string sizeRecommendation(const Json &bySize) {
  struct Row {
    std::string name;
    double rate;
  };
  std::vector<Row> rows;
  if (bySize.is_object())
    for (auto it = bySize.begin(); it != bySize.end(); ++it)
      if (it.value().value("attempts", 0) >= 2)
        rows.push_back(
            {it.key(), double(it.value().value("matches", 0)) / it.value().value("attempts", 1)});
  std::stable_sort(rows.begin(), rows.end(),
                   [](const Row &a, const Row &b) { return a.rate > b.rate; });
  if (rows.empty())
    return "Not enough data yet - assign it some work.";
  auto percent = [](double v) {
    return std::to_string(static_cast<int>(std::floor(v * 100 + .5)));
  };
  auto out = "Strongest on " + rows.front().name + " (" + percent(rows.front().rate) + "% hit)";
  if (rows.front().name != rows.back().name)
    out += "; weakest on " + rows.back().name + " (" + percent(rows.back().rate) + "%)";
  return out + ".";
}
} // namespace lite
