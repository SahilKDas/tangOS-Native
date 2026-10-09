#include "activity.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <regex>
#include <set>
namespace lite {
Json measuredRole(const Json &stats) {
  auto by = stats.value("bySize", Json::object());
  auto rate = [&](const std::string &key) {
    auto tally = by.value(key, Json::object());
    auto attempts = tally.value("attempts", 0);
    return attempts >= 2 ? double(tally.value("matches", 0)) / attempts : -1.0;
  };
  if (stats.contains("bySize") && stats["bySize"].is_object() &&
      stats.value("attempts", stats.value("matchAttempts", 0)) >= 4) {
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
Json automaticRole(const Json &agent) {
  auto roles = agent.value("roles", Json::array());
  if (!roles.empty())
    return {{"role", roles[0]}, {"why", "you assigned this role"}, {"source", "assigned"}};
  auto cap = [&](Json pick) {
    const std::vector<std::string> ladder{"Hard matcher", "Random", "Drafter", "Refiner"};
    auto hidden = agent.value("hiddenRole", std::string());
    auto rung = std::find(ladder.begin(), ladder.end(), hidden);
    auto current = std::find(ladder.begin(), ladder.end(), pick.at("role").get<std::string>());
    if (rung != ladder.end() && current != ladder.end() && current < rung) {
      pick["role"] = hidden;
      pick["why"] = "the pool outgrew its old role - running easier work now";
    }
    return pick;
  };
  auto measured = measuredRole(agent.value("stats", Json::object()));
  if (!measured["role"].is_null()) {
    measured["source"] = "measured";
    return cap(measured);
  }
  struct Fit {
    std::string role, strength;
    std::regex names;
    std::vector<std::string> families;
  };
  static const std::vector<Fit> fits{
      {"Hard matcher", "very high", std::regex(R"(fable|opus|gpt-?5|\bo[34]\b)"), {}},
      {"Random", "high", std::regex("sonnet|grok"), {"Claude", "Grok", "GPT"}},
      {"Drafter", "medium", std::regex("deepseek|kimi|moonshot"), {"DeepSeek", "Kimi"}},
      {"Refiner",
       "low",
       std::regex("glm|zhipu|nemotron|nemo|gemma|mistral"),
       {"GLM", "Nemotron", "Requesty"}}};
  auto name = agent.value("name", std::string());
  auto folded = name;
  std::transform(folded.begin(), folded.end(), folded.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  for (auto &fit : fits)
    if (std::regex_search(folded, fit.names))
      return cap({{"role", fit.role},
                  {"why", name + " is a " + fit.strength + " model"},
                  {"source", "model"}});
  auto family = agent.value("provider", std::string());
  const std::vector<std::string> known{"Claude",   "GLM",      "GPT",      "Grok",
                                       "DeepSeek", "Nemotron", "Requesty", "Kimi"};
  if (std::find(known.begin(), known.end(), family) == known.end()) {
    family = "default";
    for (auto entry : std::vector<std::pair<std::string, std::regex>>{
             {"Claude", std::regex("claude|opus|sonnet|haiku|fable")},
             {"GLM", std::regex("glm|zhipu")},
             {"Grok", std::regex("grok")},
             {"DeepSeek", std::regex("deepseek")},
             {"Nemotron", std::regex("nemotron|nemo")},
             {"Kimi", std::regex("kimi|moonshot")},
             {"GPT", std::regex("gpt|o1|o3|o4|chatgpt|openai")}})
      if (std::regex_search(folded, entry.second)) {
        family = entry.first;
        break;
      }
  }
  for (auto &fit : fits)
    if (std::find(fit.families.begin(), fit.families.end(), family) != fit.families.end())
      return cap(
          {{"role", fit.role}, {"why", family + " models fit this role"}, {"source", "model"}});
  return cap({{"role", "Random"},
              {"why", "unrecognised model - drawing from the whole unmatched pool"},
              {"source", "fallback"}});
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
Json parseStatisticsJson(const std::string &source) {
  auto original = nlohmann::ordered_json::parse(source);
  Json result = original;
  std::function<void(const nlohmann::ordered_json &, Json &)> preserve;
  preserve = [&](const nlohmann::ordered_json &node, Json &target) {
    if (node.is_object()) {
      if (node.contains("bySize") && node.at("bySize").is_object() &&
          !target.contains("bySizeOrder")) {
        target["bySizeOrder"] = Json::array();
        for (auto it = node.at("bySize").begin(); it != node.at("bySize").end(); ++it)
          target["bySizeOrder"].push_back(it.key());
      }
      for (auto it = node.begin(); it != node.end(); ++it)
        preserve(it.value(), target[it.key()]);
    } else if (node.is_array())
      for (size_t i = 0; i < node.size(); ++i)
        preserve(node[i], target[i]);
  };
  preserve(original, result);
  return result;
}
std::string sizeRecommendation(const Json &bySize, const Json &order) {
  struct Row {
    std::string name;
    double rate;
  };
  std::vector<Row> rows;
  std::set<std::string> seen;
  auto add = [&](const std::string &name) {
    if (!bySize.is_object() || !bySize.contains(name) || !seen.insert(name).second)
      return;
    auto &bucket = bySize.at(name);
    if (bucket.is_object() && bucket.value("attempts", 0) >= 2)
      rows.push_back({name, double(bucket.value("matches", 0)) / bucket.value("attempts", 1)});
  };
  if (order.is_array())
    for (auto &name : order)
      if (name.is_string())
        add(name.get<std::string>());
  if (bySize.is_object())
    for (auto it = bySize.begin(); it != bySize.end(); ++it)
      add(it.key());
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
Json effortPolicy(const Json &agent) {
  static const std::map<std::string, Json> catalog = {
      {"Claude",
       {{"options", {"low", "medium", "high", "xhigh", "max"}},
        {"default", "high"},
        {"note", "extended-thinking budget"}}},
      {"GLM",
       {{"options", {"off"}},
        {"default", "off"},
        {"note", "thinking off: the refine driver emits code directly, and reasoning starves its "
                 "token budget"}}},
      {"GPT",
       {{"options", {"minimal", "low", "medium", "high"}},
        {"default", "high"},
        {"note", "reasoning_effort"}}},
      {"Grok",
       {{"options", {"low", "high"}}, {"default", "high"}, {"note", "grok reasoning_effort"}}},
      {"DeepSeek",
       {{"options", {"chat", "reasoner"}},
        {"default", "reasoner"},
        {"note", "V3 chat vs R1 reasoner"}}},
      {"Nemotron",
       {{"options", {"off"}},
        {"default", "off"},
        {"note", "local LM Studio (model nemo): reasons internally, the driver reads the answer"}}},
      {"Requesty",
       {{"options",
         {"nvidia/nemotron-3-super-120b-a12b", "nvidia/nemotron-3-ultra-550b-a55b",
          "nvidia/nemotron-3-nano-30b-a3b", "nvidia/nemotron-3-nano-omni-30b-a3b-reasoning",
          "google/gemma-4-31b-it", "mistral/leanstral-1-5", "novita/tencent/hy3",
          "poolside/laguna-m.1"}},
        {"default", "nvidia/nemotron-3-super-120b-a12b"},
        {"note", "which free Requesty model to run"}}},
      {"Kimi",
       {{"options", {"off"}},
        {"default", "off"},
        {"note", "Moonshot Kimi K3: the driver reads the answer (OpenAI-compatible)"}}}};
  auto family = agent.value("provider", std::string());
  if (!catalog.count(family)) {
    family = "default";
    auto name = agent.value("name", std::string());
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    static const std::vector<std::pair<std::regex, std::string>> patterns = {
        {std::regex("claude|opus|sonnet|haiku|fable"), "Claude"},
        {std::regex("glm|zhipu"), "GLM"},
        {std::regex("grok"), "Grok"},
        {std::regex("deepseek"), "DeepSeek"},
        {std::regex("nemotron|nemo"), "Nemotron"},
        {std::regex("kimi|moonshot"), "Kimi"},
        {std::regex("gpt|o1|o3|o4|chatgpt|openai"), "GPT"}};
    for (auto &entry : patterns)
      if (std::regex_search(name, entry.first)) {
        family = entry.second;
        break;
      }
  }
  auto spec = catalog.count(family)
                  ? catalog.at(family)
                  : Json{{"options", {"low", "medium", "high"}}, {"default", "medium"}};
  auto selected = agent.value("effort", std::string());
  auto &options = spec.at("options");
  if (std::find(options.begin(), options.end(), selected) == options.end())
    selected = spec.at("default");
  return {{"family", family}, {"spec", spec}, {"current", selected}};
}
Json driverPolicy(const Json &agent, const Json &preferences, size_t targets) {
  auto raw = preferences.value("agentFanout", Json(8));
  double count = raw.is_number() ? raw.get<double>() : 0;
  int fanout = std::isfinite(count) && count >= 1 ? int(std::min(64., std::floor(count))) : 8;
  auto family = effortPolicy(agent).at("family").get<std::string>();
  bool serial = family == "GLM" || family == "GPT" || family == "Nemotron" ||
                family == "Requesty" || agent.value("name", std::string()) == "Requesty";
  bool parallel = preferences.value("useAgents", false);
  int workers = serial || !parallel ? 1 : std::clamp(agent.value("jobs", 3), 1, 32);
  return {{"jobs", workers},
          {"functionsPerAgent", fanout},
          {"subAgents", std::max(1, int(std::floor(double(targets) / fanout + .5)))},
          {"useAgents", parallel}};
}
} // namespace lite
