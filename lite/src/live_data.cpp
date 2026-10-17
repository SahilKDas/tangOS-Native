#include "live_data.h"
#include <algorithm>
#include <regex>
#include <set>
#include <stdexcept>
namespace lite {
std::vector<Json> SseDecoder::feed(const std::string &chunk) {
  pending += chunk;
  if (pending.size() > 2 * 1024 * 1024)
    throw std::runtime_error("SSE frame exceeds 2 MiB");
  std::vector<Json> result;
  for (;;) {
    // Normalize complete CRLF boundaries without changing JSON string escapes.
    auto cr = pending.find("\r\n");
    while (cr != std::string::npos) {
      pending.erase(cr, 1);
      cr = pending.find("\r\n", cr);
    }
    auto end = pending.find("\n\n");
    if (end == std::string::npos)
      break;
    auto frame = pending.substr(0, end);
    pending.erase(0, end + 2);
    std::string event = "message", data;
    bool hasData = false;
    for (auto &line : split(frame, '\n')) {
      if (line.rfind("event:", 0) == 0)
        event = trim(line.substr(6));
      else if (line.rfind("data:", 0) == 0) {
        if (hasData)
          data += '\n';
        data += trim(line.substr(5));
        hasData = true;
      }
    }
    if (!hasData)
      continue;
    auto value = Json::parse(data, nullptr, false);
    if (!value.is_discarded())
      result.push_back({{"event", event}, {"data", value}});
  }
  return result;
}
Json normalizeAtlasFeed(const std::string &method, const Json &raw) {
  auto object = raw.is_object() ? raw : Json::object();
  if (method == "atlas.cosmetics") {
    Json colors = Json::object(), stars = Json::array();
    if (object.contains("colors") && object["colors"].is_object())
      for (auto it = object["colors"].begin(); it != object["colors"].end(); ++it)
        if (it.value().is_string() &&
            std::regex_match(it.value().get<std::string>(), std::regex("#[0-9a-fA-F]{6}")))
          colors[it.key()] = it.value();
    if (object.contains("stars") && object["stars"].is_array())
      for (auto &star : object["stars"])
        if (star.is_object() && star.contains("function") && star["function"].is_string() &&
            star.contains("by") && star["by"].is_string())
          stars.push_back(
              {{"function", star["function"]},
               {"by", star["by"]},
               {"at", star.contains("at") && star["at"].is_string() ? star["at"] : Json("")}});
    return {{"colors", colors}, {"stars", stars}};
  }
  if (method == "atlas.counts") {
    Json out = {{"totals", Json::object()}, {"daily", Json::object()}};
    for (auto field : {"totals", "daily"})
      if (object.contains(field) && object[field].is_object())
        for (auto it = object[field].begin(); it != object[field].end(); ++it)
          if (it.value().is_number())
            out[field][it.key()] = it.value();
    return out;
  }
  if (method == "atlas.progress") {
    Json out = {
        {"ready", false},
        {"stats",
         {{"totalFunctions", 0}, {"matchedFunctions", 0}, {"totalBytes", 0}, {"matchedBytes", 0}}},
        {"matched", Json::array()}};
    if (object.value("ready", Json(false)) == true && object.contains("stats") &&
        object["stats"].is_object()) {
      out["ready"] = true;
      out["stats"] = object["stats"];
      if (object.contains("matched") && object["matched"].is_array())
        for (auto &id : object["matched"])
          if (id.is_string())
            out["matched"].push_back(id);
    }
    return out;
  }
  return raw;
}
Json overlayLiveAtlas(Json local, const Json &live) {
  if (!local.is_object() || !local.contains("functions") || !local["functions"].is_array() ||
      !live.is_object() || !live.contains("functions") || !live["functions"].is_array())
    return local;
  std::map<std::string, Json> byName;
  for (auto &f : live["functions"])
    if (f.is_object() && f.contains("name") && f["name"].is_string())
      byName[f["name"]] = f;
  for (auto &f : local["functions"]) {
    if (!f.is_object() || !f.contains("name") || !f["name"].is_string())
      continue;
    auto found = byName.find(f["name"].get<std::string>());
    if (found == byName.end())
      continue;
    auto &row = found->second;
    if (row.value("matched", Json(false)) == true)
      f["matched"] = true;
    if ((!f.contains("noMatch") || f["noMatch"].is_null()) && row.contains("noMatch"))
      f["noMatch"] = row["noMatch"];
  }
  return local;
}
Json mergeLiveProgress(const Json &previous, const Json &event) {
  if (!event.is_object() || event.value("ready", Json(false)) != true || !event.contains("stats") ||
      !event["stats"].is_object())
    return previous;
  auto next = normalizeAtlasFeed("atlas.progress", event);
  auto ids = event.contains("matched") ? next["matched"]
             : previous.is_object()    ? previous.value("matched", Json::array())
                                       : Json::array();
  if (!ids.is_array())
    ids = Json::array();
  auto records = previous.is_object() ? previous.value("records", Json::object()) : Json::object();
  if (!records.is_object())
    records = Json::object();
  if (event.contains("added") && event["added"].is_array())
    for (auto &id : event["added"])
      if (id.is_string() && std::find(ids.begin(), ids.end(), id) == ids.end())
        ids.push_back(id);
  if (event.contains("removed") && event["removed"].is_array())
    for (auto &id : event["removed"])
      if (id.is_string())
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
  if (event.contains("changed") && event["changed"].is_array())
    for (auto &row : event["changed"])
      if (row.is_object() && row.contains("id") && row["id"].is_string())
        records[row["id"].get<std::string>()] = row;
  next["matched"] = ids;
  next["records"] = records;
  return next;
}
Json applyLiveProgress(Json database, const Json &progress) {
  if (!database.is_object() || !database.contains("functions") ||
      !database["functions"].is_array() || !progress.is_object() ||
      progress.value("ready", Json(false)) != true)
    return database;
  std::set<std::string> matched;
  for (auto &id : progress.value("matched", Json::array()))
    if (id.is_string())
      matched.insert(id);
  auto records = progress.value("records", Json::object());
  for (auto &row : database["functions"]) {
    if (!row.is_object() || !row.contains("id") || !row["id"].is_string())
      continue;
    auto id = row["id"].get<std::string>();
    if (records.is_object() && records.contains(id) && records[id].is_object())
      row.update(records[id]);
    row["matched"] = matched.count(id) != 0;
  }
  auto stats = progress.value("stats", Json::object());
  if (stats.is_object()) {
    if (!database.contains("stats") || !database["stats"].is_object())
      database["stats"] = Json::object();
    database["stats"].update(stats);
  }
  return database;
}
Json liveClaimRows(const Json &value) {
  auto rows = value.is_array()    ? value
              : value.is_object() ? value.value("claims", Json::array())
                                  : Json::array();
  Json out = Json::array();
  if (rows.is_array())
    for (auto &row : rows)
      if (row.is_object())
        out.push_back(row);
  return out;
}
bool reuseLiveAtlas(int64_t savedAt, int64_t now, bool force) {
  return savedAt > 0 && now >= savedAt && now - savedAt < (force ? 20000 : 60000);
}
namespace {
bool bot(std::string login) {
  bool actions = login == "github-actions";
  std::transform(login.begin(), login.end(), login.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return actions || (login.size() >= 5 && login.substr(login.size() - 5) == "[bot]");
}
} // namespace
void GithubCredits::contributors(const Json &rows) {
  if (!rows.is_array())
    return;
  for (auto &row : rows)
    if (row.is_object() && row.contains("login") && row["login"].is_string() && !bot(row["login"]))
      result["logins"].push_back(
          {{"login", row["login"]}, {"contributions", row.value("contributions", Json(0))}});
}
void GithubCredits::commits(const Json &rows) {
  if (!rows.is_array())
    return;
  for (auto &row : rows) {
    if (!row.is_object() || !row.contains("author") || !row["author"].is_object())
      continue;
    auto login = row["author"].value("login", Json());
    if (!login.is_string() || login.get<std::string>().empty())
      continue;
    auto author = row.value("commit", Json::object());
    author = author.is_object() ? author.value("author", Json::object()) : Json::object();
    if (!author.is_object())
      continue;
    auto email = author.value("email", Json());
    std::string key;
    if (email.is_string() && !email.get<std::string>().empty()) {
      auto e = email.get<std::string>();
      std::smatch match;
      if (std::regex_match(e, match,
                           std::regex("(?:[0-9]+\\+)?([^@]+)@users\\.noreply\\.github\\.com")))
        key = match[1];
      else {
        key = e.substr(0, e.find('@'));
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
      }
    }
    if (key.empty() && author.contains("name") && author["name"].is_string())
      key = trim(author["name"]);
    if (!key.empty() && !result["keyToLogin"].contains(key))
      result["keyToLogin"][key] = login;
  }
}
void GithubCredits::pulls(const Json &rows) {
  if (!rows.is_array())
    return;
  for (auto &row : rows) {
    if (!row.is_object() || !row.contains("user") || !row["user"].is_object())
      continue;
    auto login = row["user"].value("login", Json());
    if (login.is_string() && !login.get<std::string>().empty() && !bot(login) &&
        std::find(result["prAuthors"].begin(), result["prAuthors"].end(), login) ==
            result["prAuthors"].end())
      result["prAuthors"].push_back(login);
  }
}
} // namespace lite